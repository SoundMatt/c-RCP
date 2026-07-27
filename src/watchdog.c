#include "rcp/watchdog.h"

#include "platform.h"

#include <stdlib.h>

//cfusa:req REQ-WDG-009
const char *rcp_health_state_string(rcp_health_state_t h)
{
    switch (h) {
    case RCP_HEALTH_HEALTHY:  return "healthy";
    case RCP_HEALTH_DEGRADED: return "degraded";
    case RCP_HEALTH_FAULTED:  return "faulted";
    default:                  return "unknown";
    }
}

rcp_watchdog_config_t rcp_watchdog_default_config(void)
{
    rcp_watchdog_config_t c;
    c.interval_ms   = 10;
    c.timeout_ms    = 5;
    c.degrade_after = 3;
    c.fault_after   = 5;
    return c;
}

typedef struct {
    rcp_controller_t  *ctrl; /* retained */
    rcp_health_state_t health;
    int                misses;
    uint32_t           cmd_id;
} zone_state_t;

struct rcp_watchdog_keeper {
    rcp_watchdog_config_t cfg;
    rcp_mutex_t            mu; /* protects states[], callbacks[], closed */
    zone_state_t           *states;
    size_t                  n_states;
    rcp_watchdog_health_fn *callbacks;
    void                  **callback_ctx;
    size_t                  n_callbacks;
    size_t                  callbacks_cap;
    bool                    closed;
    rcp_thread_t            run_thread;
    bool                    have_run_thread;
};

static zone_state_t *find_state(rcp_watchdog_keeper_t *k, rcp_zone_t z)
{
    size_t i;
    for (i = 0; i < k->n_states; i++) {
        if (rcp_controller_zone(k->states[i].ctrl) == z) return &k->states[i];
    }
    return NULL;
}

static bool callbacks_append(rcp_watchdog_keeper_t *k, rcp_watchdog_health_fn cb, void *user_data)
{
    if (k->n_callbacks == k->callbacks_cap) {
        size_t new_cap = (k->callbacks_cap == 0) ? 4 : k->callbacks_cap * 2;
        rcp_watchdog_health_fn *grown_cb  = (rcp_watchdog_health_fn *)realloc(k->callbacks, new_cap * sizeof(*grown_cb));
        void                  **grown_ctx;
        if (!grown_cb) return false;
        k->callbacks     = grown_cb;
        grown_ctx = (void **)realloc(k->callback_ctx, new_cap * sizeof(*grown_ctx));
        if (!grown_ctx) return false;
        k->callback_ctx  = grown_ctx;
        k->callbacks_cap = new_cap;
    }
    k->callbacks[k->n_callbacks]     = cb;
    k->callback_ctx[k->n_callbacks]  = user_data;
    k->n_callbacks++;
    return true;
}

//cfusa:req REQ-WDG-001
//cfusa:req REQ-WDG-007
static void kick(rcp_watchdog_keeper_t *k, zone_state_t *st)
{
    rcp_command_t  cmd = {0};
    rcp_response_t resp = {0};
    rcp_context_t  ctx;
    int            ec;
    rcp_health_state_t next;
    rcp_health_event_t ev;
    size_t i;
    bool fire_event = false;

    rcp_mutex_lock(&k->mu);
    cmd.id = ++st->cmd_id;
    rcp_mutex_unlock(&k->mu);

    cmd.zone     = rcp_controller_zone(st->ctrl);
    cmd.type     = RCP_CMD_WATCHDOG;
    cmd.priority = RCP_PRIORITY_HIGH;
    ctx = rcp_context_with_timeout_ms(k->cfg.timeout_ms);

    ec = rcp_controller_send(st->ctrl, &ctx, &cmd, &resp);
    rcp_response_free(&resp);

    rcp_mutex_lock(&k->mu);
    next = st->health;
    if (ec == RCP_OK) {
        st->misses = 0;
        next       = RCP_HEALTH_HEALTHY;
    } else {
        st->misses++;
        if (st->misses >= k->cfg.fault_after)        next = RCP_HEALTH_FAULTED;
        else if (st->misses >= k->cfg.degrade_after)  next = RCP_HEALTH_DEGRADED;
    }
    if (next != st->health) {
        st->health = next;
        ev.zone  = cmd.zone;
        ev.state = next;
        ev.err   = ec;
        fire_event = true;
    }
    rcp_mutex_unlock(&k->mu);

    if (fire_event) {
        /* Callbacks are invoked outside the lock so a callback that calls
         * back into the keeper (e.g. rcp_watchdog_keeper_health()) can't
         * deadlock. n_callbacks/callbacks are only ever appended to before
         * the run thread starts touching them concurrently (subscribe() is
         * documented as not thread-safe with close()), so this read is
         * safe without holding mu. */
        for (i = 0; i < k->n_callbacks; i++) {
            k->callbacks[i](&ev, k->callback_ctx[i]);
        }
    }
}

static void kick_all(rcp_watchdog_keeper_t *k)
{
    size_t i;
    for (i = 0; i < k->n_states; i++) {
        kick(k, &k->states[i]);
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

        kick_all(k);

        waited_ms = 0;
        while (waited_ms < k->cfg.interval_ms) {
            unsigned chunk = (k->cfg.interval_ms - waited_ms) < 5 ? (unsigned)(k->cfg.interval_ms - waited_ms) : 5;
            rcp_mutex_lock(&k->mu);
            closed_now = k->closed;
            rcp_mutex_unlock(&k->mu);
            if (closed_now) break;
            rcp_sleep_ms(chunk == 0 ? 1 : chunk);
            waited_ms += (chunk == 0 ? 1 : chunk);
        }
    }
}

rcp_watchdog_keeper_t *rcp_watchdog_keeper_new(rcp_watchdog_config_t cfg,
                                                rcp_controller_t *const *ctrls, size_t n_ctrls)
{
    rcp_watchdog_keeper_t *k = (rcp_watchdog_keeper_t *)calloc(1, sizeof(*k));
    size_t i;

    if (!k) return NULL;
    k->cfg = cfg;
    rcp_mutex_init(&k->mu);

    if (n_ctrls > 0) {
        zone_state_t *states = (zone_state_t *)calloc(n_ctrls, sizeof(*states));
        k->states = states;
        if (!k->states) {
            rcp_mutex_destroy(&k->mu);
            free(k);
            return NULL;
        }
    }
    for (i = 0; i < n_ctrls; i++) {
        k->states[i].ctrl   = rcp_controller_retain(ctrls[i]);
        k->states[i].health = RCP_HEALTH_HEALTHY;
        k->states[i].misses = 0;
        k->states[i].cmd_id = 0;
    }
    k->n_states = n_ctrls;

    if (rcp_thread_start(&k->run_thread, run_thread_fn, k) == 0) {
        k->have_run_thread = true;
    }

    return k;
}

//cfusa:req REQ-WDG-002
//cfusa:req REQ-WDG-003
//cfusa:req REQ-WDG-004
//cfusa:req REQ-WDG-005
rcp_health_state_t rcp_watchdog_keeper_health(rcp_watchdog_keeper_t *k, rcp_zone_t zone)
{
    zone_state_t *st;
    rcp_health_state_t h;

    rcp_mutex_lock(&k->mu);
    st = find_state(k, zone);
    h  = st ? st->health : RCP_HEALTH_FAULTED;
    rcp_mutex_unlock(&k->mu);
    return h;
}

//cfusa:req REQ-WDG-006
bool rcp_watchdog_keeper_subscribe(rcp_watchdog_keeper_t *k, rcp_watchdog_health_fn cb, void *user_data)
{
    bool ok;
    rcp_mutex_lock(&k->mu);
    ok = callbacks_append(k, cb, user_data);
    rcp_mutex_unlock(&k->mu);
    return ok;
}

//cfusa:req REQ-WDG-008
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
    size_t i;

    if (!k) return;
    rcp_watchdog_keeper_close(k);

    for (i = 0; i < k->n_states; i++) {
        rcp_controller_release(k->states[i].ctrl);
    }
    free(k->states);
    free(k->callbacks);
    free(k->callback_ctx);
    rcp_mutex_destroy(&k->mu);
    free(k);
}
