#include "rcp/sim.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>
#include <string.h>

rcp_sim_config_t rcp_sim_default_config(rcp_zone_t z)
{
    rcp_sim_config_t c;
    c.zone                = z;
    c.base_latency_ms     = 2;
    c.jitter_ms           = 1;
    c.status_interval_ms  = 10;
    c.watchdog_timeout_ms = 50;
    c.latency_model       = RCP_SIM_LATENCY_JITTER;
    return c;
}

/* ── Simulated controller ──────────────────────────────────────────────────── */

typedef struct rcp_sim_controller {
    rcp_controller_t   base; /* first member: allows rcp_controller_t* <-> this cast */
    rcp_sim_config_t    cfg;
    rcp_sim_handler_fn  handler;
    void               *user_data;

    /* All mutable state below is protected by mu -- same single-mutex
     * simplification already documented in mock.c: this is a test/SiL
     * helper, not a hot transport path. */
    rcp_mutex_t            mu;
    bool                   closed;
    uint32_t               seq;
    int                    fault_err; /* RCP_OK (0) iff no fault injected */
    uint32_t               rng_state;
    uint64_t               wd_last_kick_ms; /* 0 iff never kicked */
    rcp_status_channel_t **subs;
    size_t                 subs_len;
    size_t                 subs_cap;

    rcp_thread_t status_thread;
    bool         have_status_thread;
    rcp_thread_t wd_thread;
    bool         have_wd_thread;
} rcp_sim_controller_t;

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static bool sc_subs_append(rcp_sim_controller_t *sc, rcp_status_channel_t *ch)
{
    if (sc->subs_len == sc->subs_cap) {
        size_t new_cap = (sc->subs_cap == 0) ? 4 : sc->subs_cap * 2;
        rcp_status_channel_t **grown =
            (rcp_status_channel_t **)realloc(sc->subs, new_cap * sizeof(*grown));
        if (!grown) return false;
        sc->subs     = grown;
        sc->subs_cap = new_cap;
    }
    sc->subs[sc->subs_len++] = ch;
    return true;
}

static void sc_subs_remove(rcp_sim_controller_t *sc, rcp_status_channel_t *ch)
{
    size_t i;
    for (i = 0; i < sc->subs_len; i++) {
        if (sc->subs[i] == ch) {
            sc->subs[i] = sc->subs[sc->subs_len - 1];
            sc->subs_len--;
            return;
        }
    }
}

//cfusa:req REQ-SIM-002
static rcp_zone_t sim_ctrl_zone(rcp_controller_t *self)
{
    return ((rcp_sim_controller_t *)self)->cfg.zone;
}

//cfusa:req REQ-SIM-001
//cfusa:req REQ-SIM-003
//cfusa:req REQ-SIM-004
static int sim_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                          const rcp_command_t *cmd, rcp_response_t *out)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)self;
    int fault_err;
    uint64_t delay_ms;

    if (rcp_context_done(ctx)) return RCP_ERR_TIMEOUT;
    if (cmd->zone != sc->cfg.zone) return RCP_ERR_ZONE_MISMATCH;

    rcp_mutex_lock(&sc->mu);
    if (sc->closed) {
        rcp_mutex_unlock(&sc->mu);
        return RCP_ERR_CLOSED;
    }
    fault_err = sc->fault_err;
    rcp_mutex_unlock(&sc->mu);
    if (fault_err != RCP_OK) return fault_err;

    delay_ms = sc->cfg.base_latency_ms;
    if (sc->cfg.latency_model == RCP_SIM_LATENCY_JITTER && sc->cfg.jitter_ms > 0) {
        rcp_mutex_lock(&sc->mu);
        delay_ms += xorshift32(&sc->rng_state) % (sc->cfg.jitter_ms + 1);
        rcp_mutex_unlock(&sc->mu);
    }
    if (delay_ms > 0) rcp_sleep_ms((unsigned)delay_ms);

    if (rcp_context_done(ctx)) return RCP_ERR_TIMEOUT;

    if (cmd->type == RCP_CMD_WATCHDOG) {
        rcp_mutex_lock(&sc->mu);
        sc->wd_last_kick_ms = rcp_monotonic_ms();
        rcp_mutex_unlock(&sc->mu);
    }

    if (sc->handler) {
        rcp_mutex_lock(&sc->mu);
        sc->handler(cmd, out, sc->user_data);
        rcp_mutex_unlock(&sc->mu);
    } else {
        out->command_id = cmd->id;
        out->zone       = sc->cfg.zone;
        out->status     = RCP_RESPONSE_OK;
    }
    return RCP_OK;
}

typedef struct {
    rcp_sim_controller_t *sc; /* retained */
    rcp_status_channel_t   *ch; /* retained */
    rcp_context_t           ctx;
} sim_watcher_args_t;

static void sim_watcher_thread_fn(void *arg)
{
    sim_watcher_args_t *w = (sim_watcher_args_t *)arg;

    for (;;) {
        bool sc_closed;

        rcp_mutex_lock(&w->sc->mu);
        sc_closed = w->sc->closed;
        rcp_mutex_unlock(&w->sc->mu);

        if (sc_closed) break;
        if (rcp_status_channel_is_closed(w->ch)) break;
        if (rcp_context_done(&w->ctx)) break;

        rcp_sleep_ms(1);
    }

    rcp_mutex_lock(&w->sc->mu);
    if (!w->sc->closed) {
        sc_subs_remove(w->sc, w->ch);
        rcp_status_channel_release(w->ch); /* release the subs array's reference */
    }
    rcp_mutex_unlock(&w->sc->mu);

    rcp_status_channel_close(w->ch);
    rcp_status_channel_release(w->ch); /* this watcher's own reference */
    rcp_controller_release(&w->sc->base);
    free(w);
}

static int sim_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)self;
    rcp_status_channel_t *ch;
    sim_watcher_args_t   *w;

    rcp_mutex_lock(&sc->mu);
    if (sc->closed) {
        rcp_mutex_unlock(&sc->mu);
        return RCP_ERR_CLOSED;
    }
    ch = rcp_status_channel_new(16);
    if (!ch) {
        rcp_mutex_unlock(&sc->mu);
        return RCP_ERR_BUSY;
    }
    if (!sc_subs_append(sc, ch)) {
        rcp_mutex_unlock(&sc->mu);
        rcp_status_channel_release(ch);
        return RCP_ERR_BUSY;
    }
    rcp_mutex_unlock(&sc->mu);

    *out = rcp_status_channel_retain(ch);

    w = (sim_watcher_args_t *)malloc(sizeof(*w));
    if (w) {
        w->sc  = (rcp_sim_controller_t *)rcp_controller_retain(&sc->base);
        w->ch  = rcp_status_channel_retain(ch);
        w->ctx = *ctx;
        if (rcp_thread_start_detached(sim_watcher_thread_fn, w) != 0) {
            rcp_controller_release(&w->sc->base);
            rcp_status_channel_release(w->ch);
            free(w);
        }
    }
    return RCP_OK;
}

//cfusa:req REQ-SIM-007
void rcp_sim_controller_publish(rcp_controller_t *ctrl, const uint8_t *payload, size_t len)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)ctrl;
    rcp_status_t            st;
    rcp_status_channel_t  **snapshot = NULL;
    size_t                  snapshot_len = 0;
    size_t                  i;

    rcp_mutex_lock(&sc->mu);
    sc->seq++;
    st.zone    = sc->cfg.zone;
    st.seq     = sc->seq;
    st.healthy = !sc->closed;
    st.payload = rcp_bytes_dup(payload, len);

    if (sc->subs_len > 0) {
        size_t n = sc->subs_len;
        snapshot = (rcp_status_channel_t **)malloc(n * sizeof(*snapshot));
        if (snapshot) {
            for (i = 0; i < n; i++) {
                snapshot[i] = rcp_status_channel_retain(sc->subs[i]);
            }
            snapshot_len = n;
        }
    }
    rcp_mutex_unlock(&sc->mu);

    for (i = 0; i < snapshot_len; i++) {
        rcp_status_channel_push(snapshot[i], &st);
        rcp_status_channel_release(snapshot[i]);
    }
    free(snapshot);
    rcp_status_free(&st);
}

static void status_thread_fn(void *arg)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)arg;

    for (;;) {
        bool closed_now;
        uint64_t waited_ms;

        rcp_mutex_lock(&sc->mu);
        closed_now = sc->closed;
        rcp_mutex_unlock(&sc->mu);
        if (closed_now) break;

        rcp_sim_controller_publish(&sc->base, NULL, 0);

        waited_ms = 0;
        while (waited_ms < sc->cfg.status_interval_ms) {
            unsigned chunk = (sc->cfg.status_interval_ms - waited_ms) < 5
                                 ? (unsigned)(sc->cfg.status_interval_ms - waited_ms) : 5;
            rcp_mutex_lock(&sc->mu);
            closed_now = sc->closed;
            rcp_mutex_unlock(&sc->mu);
            if (closed_now) break;
            rcp_sleep_ms(chunk == 0 ? 1 : chunk);
            waited_ms += (chunk == 0 ? 1 : chunk);
        }
    }
}

/* Deviation from cpp-RCP: cpp-RCP's watchdog_loop() polls on an interval to
 * refresh a cached wd_miss_ atomic bool. This port instead computes the
 * miss state on demand in rcp_sim_controller_watchdog_missed() directly
 * from wd_last_kick_ms -- always accurate, with no polling-interval
 * staleness window. The background thread itself is kept only so close()
 * still has a second thread to join, matching REQ-SIM-008's "status and
 * watchdog background threads" wording; its body is otherwise a no-op
 * responsive sleep loop. */
static void wd_thread_fn(void *arg)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)arg;

    for (;;) {
        bool closed_now;

        rcp_mutex_lock(&sc->mu);
        closed_now = sc->closed;
        rcp_mutex_unlock(&sc->mu);
        if (closed_now) break;

        rcp_sleep_ms(sc->cfg.watchdog_timeout_ms < 5 ? 1 : 5);
    }
}

//cfusa:req REQ-SIM-008
static int sim_ctrl_close(rcp_controller_t *self)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)self;
    bool                    was_open;
    rcp_status_channel_t  **local = NULL;
    size_t                  local_len = 0;
    size_t                  i;

    rcp_mutex_lock(&sc->mu);
    was_open = !sc->closed;
    if (was_open) {
        sc->closed   = true;
        local        = sc->subs;
        local_len    = sc->subs_len;
        sc->subs     = NULL;
        sc->subs_len = 0;
        sc->subs_cap = 0;
    }
    rcp_mutex_unlock(&sc->mu);

    if (was_open) {
        for (i = 0; i < local_len; i++) {
            rcp_status_channel_close(local[i]);
            rcp_status_channel_release(local[i]);
        }
        free(local);
    }

    if (sc->have_status_thread) {
        rcp_thread_join(sc->status_thread);
        sc->have_status_thread = false;
    }
    if (sc->have_wd_thread) {
        rcp_thread_join(sc->wd_thread);
        sc->have_wd_thread = false;
    }
    return RCP_OK;
}

static void sim_ctrl_destroy(rcp_controller_t *self)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)self;
    (void)sim_ctrl_close(self); /* idempotent; releases any remaining subs, joins threads */
    rcp_mutex_destroy(&sc->mu);
    free(sc->subs); /* NULL after close(); freeing NULL is a no-op */
    free(sc);
}

static const rcp_controller_vtable_t sim_controller_vtable = {
    sim_ctrl_zone,
    sim_ctrl_send,
    sim_ctrl_subscribe,
    sim_ctrl_close,
    sim_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_sim_controller_new(rcp_sim_config_t cfg, rcp_sim_handler_fn handler, void *user_data)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)calloc(1, sizeof(*sc));
    if (!sc) return NULL;

    sc->base.vt       = &sim_controller_vtable;
    sc->base.refcount = 1;
    sc->cfg           = cfg;
    sc->handler       = handler;
    sc->user_data     = user_data;
    sc->rng_state     = (uint32_t)rcp_monotonic_ms() | 1u; /* xorshift needs a nonzero seed */
    rcp_mutex_init(&sc->mu);

    if (cfg.status_interval_ms > 0) {
        if (rcp_thread_start(&sc->status_thread, status_thread_fn, sc) == 0) {
            sc->have_status_thread = true;
        }
    }
    if (cfg.watchdog_timeout_ms > 0) {
        if (rcp_thread_start(&sc->wd_thread, wd_thread_fn, sc) == 0) {
            sc->have_wd_thread = true;
        }
    }

    return &sc->base;
}

//cfusa:req REQ-SIM-005
void rcp_sim_controller_fault(rcp_controller_t *ctrl, int err)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)ctrl;
    rcp_mutex_lock(&sc->mu);
    sc->fault_err = err;
    rcp_mutex_unlock(&sc->mu);
}

//cfusa:req REQ-SIM-006
void rcp_sim_controller_recover(rcp_controller_t *ctrl)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)ctrl;
    rcp_mutex_lock(&sc->mu);
    sc->fault_err = RCP_OK;
    rcp_mutex_unlock(&sc->mu);
}

bool rcp_sim_controller_watchdog_missed(rcp_controller_t *ctrl)
{
    rcp_sim_controller_t *sc = (rcp_sim_controller_t *)ctrl;
    uint64_t last;
    uint64_t now;
    bool missed;

    if (sc->cfg.watchdog_timeout_ms == 0) return false;

    rcp_mutex_lock(&sc->mu);
    last = sc->wd_last_kick_ms;
    now  = rcp_monotonic_ms();
    rcp_mutex_unlock(&sc->mu);

    if (last == 0) return true; /* never kicked */
    missed = (now - last) >= sc->cfg.watchdog_timeout_ms;
    return missed;
}
