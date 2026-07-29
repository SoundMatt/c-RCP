#include "rcp/watchdog.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>
#include <string.h>

rcp_watchdog_config_t rcp_watchdog_default_config(void)
{
    rcp_watchdog_config_t c;
    c.poll_interval_ms = 10;
    return c;
}

typedef struct {
    rcp_watchdog_stream_cfg_t cfg;
    uint64_t                  last_kick_ms;
    rcp_e2e_wd_result_t       last_result;
} stream_state_t;

struct rcp_watchdog_keeper {
    rcp_watchdog_config_t  cfg;
    rcp_mutex_t             mu; /* protects states[], callbacks[], closed */
    stream_state_t         *states;
    size_t                  n_states;
    rcp_watchdog_event_fn  *callbacks;
    void                  **callback_ctx;
    size_t                  n_callbacks;
    size_t                  callbacks_cap;
    bool                    closed;
    rcp_thread_t            run_thread;
    bool                    have_run_thread;
};

static stream_state_t *find_state(rcp_watchdog_keeper_t *k, uint64_t stream_id)
{
    size_t i;
    for (i = 0; i < k->n_states; i++) {
        if (k->states[i].cfg.stream_id == stream_id) return &k->states[i];
    }
    return NULL;
}

static bool callbacks_append(rcp_watchdog_keeper_t *k, rcp_watchdog_event_fn cb, void *user_data)
{
    if (k->n_callbacks == k->callbacks_cap) {
        size_t new_cap = (k->callbacks_cap == 0) ? 4 : k->callbacks_cap * 2;
        rcp_watchdog_event_fn *grown_cb  = (rcp_watchdog_event_fn *)realloc(k->callbacks, new_cap * sizeof(*grown_cb));
        void                  **grown_ctx;
        if (!grown_cb) return false;
        k->callbacks     = grown_cb;
        grown_ctx = (void **)realloc(k->callback_ctx, new_cap * sizeof(*grown_ctx));
        if (!grown_ctx) return false;
        k->callback_ctx  = grown_ctx;
        k->callbacks_cap = new_cap;
    }
    k->callbacks[k->n_callbacks]    = cb;
    k->callback_ctx[k->n_callbacks] = user_data;
    k->n_callbacks++;
    return true;
}

static bool wd_result_equal(rcp_e2e_wd_result_t a, rcp_e2e_wd_result_t b)
{
    return a.overflowed == b.overflowed && a.enter_safe_state == b.enter_safe_state
           && a.notify == b.notify;
}

//cfusa:req REQ-WDG-001
//cfusa:req REQ-WDG-002
//cfusa:req REQ-WDG-008
static void evaluate(rcp_watchdog_keeper_t *k, stream_state_t *st, uint64_t now_ms)
{
    uint64_t elapsed;
    rcp_e2e_wd_result_t result;
    rcp_watchdog_event_t ev;
    bool fire_event = false;
    size_t i;

    elapsed = (now_ms >= st->last_kick_ms) ? (now_ms - st->last_kick_ms) : 0;
    result  = rcp_e2e_wd_evaluate(st->cfg.rx_wd_enable, st->cfg.rx_wd_timeout_ms,
                                   st->cfg.rx_wd_safestate_enable, st->cfg.rx_wd_info_enable,
                                   elapsed);

    rcp_mutex_lock(&k->mu);
    if (!wd_result_equal(result, st->last_result)) {
        st->last_result = result;
        ev.stream_id = st->cfg.stream_id;
        ev.result    = result;
        fire_event   = true;
    }
    rcp_mutex_unlock(&k->mu);

    if (fire_event) {
        /* Invoked outside the lock, matching every other satellite in this
         * neighborhood: subscribe() is documented as not thread-safe with
         * close(), so n_callbacks/callbacks are only ever appended to
         * before the run thread starts touching them concurrently, making
         * this read safe without holding mu. */
        for (i = 0; i < k->n_callbacks; i++) {
            k->callbacks[i](&ev, k->callback_ctx[i]);
        }
    }
}

static void evaluate_all(rcp_watchdog_keeper_t *k)
{
    uint64_t now_ms = rcp_monotonic_ms();
    size_t i;
    for (i = 0; i < k->n_states; i++) {
        evaluate(k, &k->states[i], now_ms);
    }
}

static void run_thread_fn(void *arg)
{
    rcp_watchdog_keeper_t *k = (rcp_watchdog_keeper_t *)arg;

    for (;;) {
        bool closed_now;
        uint64_t waited_ms;

        rcp_mutex_lock(&k->mu);
        closed_now = k->closed;
        rcp_mutex_unlock(&k->mu);
        if (closed_now) break;

        evaluate_all(k);

        waited_ms = 0;
        while (waited_ms < k->cfg.poll_interval_ms) {
            unsigned chunk = (k->cfg.poll_interval_ms - waited_ms) < 5
                                 ? (unsigned)(k->cfg.poll_interval_ms - waited_ms) : 5;
            rcp_mutex_lock(&k->mu);
            closed_now = k->closed;
            rcp_mutex_unlock(&k->mu);
            if (closed_now) break;
            rcp_sleep_ms(chunk == 0 ? 1 : chunk);
            waited_ms += (chunk == 0 ? 1 : chunk);
        }
    }
}

//cfusa:req REQ-WDG-009
rcp_watchdog_keeper_t *rcp_watchdog_keeper_new(rcp_watchdog_config_t cfg,
                                                const rcp_watchdog_stream_cfg_t *streams,
                                                size_t n_streams)
{
    rcp_watchdog_keeper_t *k = (rcp_watchdog_keeper_t *)calloc(1, sizeof(*k));
    uint64_t now_ms;
    size_t i;

    if (!k) return NULL;
    k->cfg = cfg;
    rcp_mutex_init(&k->mu);

    if (n_streams > 0) {
        stream_state_t *states = (stream_state_t *)calloc(n_streams, sizeof(*states));
        k->states = states;
        if (!k->states) {
            rcp_mutex_destroy(&k->mu);
            free(k);
            return NULL;
        }
    }
    now_ms = rcp_monotonic_ms();
    for (i = 0; i < n_streams; i++) {
        k->states[i].cfg          = streams[i];
        k->states[i].last_kick_ms = now_ms;
        memset(&k->states[i].last_result, 0, sizeof(k->states[i].last_result));
    }
    k->n_states = n_streams;

    /* Establish each stream's initial verdict synchronously, before the
     * background thread starts, so rcp_watchdog_keeper_status() never
     * observes a transient all-false placeholder for an already-registered
     * stream. */
    evaluate_all(k);

    if (rcp_thread_start(&k->run_thread, run_thread_fn, k) == 0) {
        k->have_run_thread = true;
    }

    return k;
}

//cfusa:req REQ-WDG-003
bool rcp_watchdog_keeper_kick(rcp_watchdog_keeper_t *k, uint64_t stream_id)
{
    stream_state_t *st;

    rcp_mutex_lock(&k->mu);
    st = find_state(k, stream_id);
    if (st) st->last_kick_ms = rcp_monotonic_ms();
    rcp_mutex_unlock(&k->mu);

    return st != NULL;
}

//cfusa:req REQ-WDG-004
//cfusa:req REQ-WDG-005
rcp_e2e_wd_result_t rcp_watchdog_keeper_status(rcp_watchdog_keeper_t *k, uint64_t stream_id)
{
    stream_state_t *st;
    rcp_e2e_wd_result_t result;

    memset(&result, 0, sizeof(result));

    rcp_mutex_lock(&k->mu);
    st = find_state(k, stream_id);
    if (st) result = st->last_result;
    rcp_mutex_unlock(&k->mu);

    return result;
}

//cfusa:req REQ-WDG-006
bool rcp_watchdog_keeper_subscribe(rcp_watchdog_keeper_t *k, rcp_watchdog_event_fn cb, void *user_data)
{
    bool ok;
    rcp_mutex_lock(&k->mu);
    ok = callbacks_append(k, cb, user_data);
    rcp_mutex_unlock(&k->mu);
    return ok;
}

//cfusa:req REQ-WDG-007
void rcp_watchdog_keeper_close(rcp_watchdog_keeper_t *k)
{
    rcp_mutex_lock(&k->mu);
    k->closed = true;
    rcp_mutex_unlock(&k->mu);

    if (k->have_run_thread) {
        rcp_thread_join(k->run_thread);
        k->have_run_thread = false;
    }
}

void rcp_watchdog_keeper_destroy(rcp_watchdog_keeper_t *k)
{
    if (!k) return;
    rcp_watchdog_keeper_close(k);

    free(k->states);
    free(k->callbacks);
    free(k->callback_ctx);
    rcp_mutex_destroy(&k->mu);
    free(k);
}
