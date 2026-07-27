#include "rcp/powerstate.h"

#include "platform.h"

#include <stdlib.h>

//cfusa:req REQ-PWR-009
const char *rcp_power_state_string(rcp_power_state_t p)
{
    switch (p) {
    case RCP_POWER_ACTIVE:   return "active";
    case RCP_POWER_SLEEPING: return "sleeping";
    case RCP_POWER_BUS_OFF:  return "bus-off";
    default:                 return "unknown";
    }
}

rcp_powerstate_config_t rcp_powerstate_default_config(void)
{
    rcp_powerstate_config_t c;
    c.recovery_interval_ms = 100;
    c.recovery_timeout_ms  = 50;
    return c;
}

typedef struct {
    rcp_controller_t  *ctrl; /* retained */
    rcp_zone_t          zone;
    rcp_power_state_t   state;
} zone_entry_t;

struct rcp_powerstate_manager {
    rcp_powerstate_config_t  cfg;
    rcp_mutex_t               mu; /* protects entries[].state, callbacks[], closed */
    zone_entry_t             *entries;
    size_t                    n_entries;
    rcp_powerstate_power_fn  *callbacks;
    void                    **callback_ctx;
    size_t                    n_callbacks;
    size_t                    callbacks_cap;
    bool                      closed;
    rcp_thread_t              recover_thread;
    bool                      have_recover_thread;
};

static zone_entry_t *find_entry(rcp_powerstate_manager_t *m, rcp_zone_t z)
{
    size_t i;
    for (i = 0; i < m->n_entries; i++) {
        if (m->entries[i].zone == z) return &m->entries[i];
    }
    return NULL;
}

//cfusa:req REQ-PWR-010
static bool callbacks_append(rcp_powerstate_manager_t *m, rcp_powerstate_power_fn cb, void *user_data)
{
    if (m->n_callbacks == m->callbacks_cap) {
        size_t new_cap = (m->callbacks_cap == 0) ? 4 : m->callbacks_cap * 2;
        rcp_powerstate_power_fn *grown_cb = (rcp_powerstate_power_fn *)realloc(m->callbacks, new_cap * sizeof(*grown_cb));
        void                   **grown_ctx;
        if (!grown_cb) return false;
        m->callbacks = grown_cb;
        grown_ctx = (void **)realloc(m->callback_ctx, new_cap * sizeof(*grown_ctx));
        if (!grown_ctx) return false;
        m->callback_ctx  = grown_ctx;
        m->callbacks_cap = new_cap;
    }
    m->callbacks[m->n_callbacks]    = cb;
    m->callback_ctx[m->n_callbacks] = user_data;
    m->n_callbacks++;
    return true;
}

static void transition(rcp_powerstate_manager_t *m, zone_entry_t *e, rcp_power_state_t next, int err)
{
    rcp_power_event_t ev;
    size_t i;
    bool changed;

    rcp_mutex_lock(&m->mu);
    changed = (e->state != next);
    if (changed) e->state = next;
    rcp_mutex_unlock(&m->mu);

    if (!changed) return;

    ev.zone  = e->zone;
    ev.state = next;
    ev.err   = err;

    /* Invoked outside the lock, matching watchdog.c/deadline.c: subscribe()
     * is documented as not thread-safe with close(), so this read is safe
     * without holding mu. */
    for (i = 0; i < m->n_callbacks; i++) {
        m->callbacks[i](&ev, m->callback_ctx[i]);
    }
}

//cfusa:req REQ-PWR-001
//cfusa:req REQ-PWR-002
int rcp_powerstate_manager_sleep(rcp_powerstate_manager_t *m, const rcp_context_t *ctx, rcp_zone_t zone)
{
    zone_entry_t *e;
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    int ec;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, zone);
    if (!e) {
        rcp_mutex_unlock(&m->mu);
        return RCP_ERR_NOT_FOUND;
    }
    if (e->state != RCP_POWER_ACTIVE) {
        rcp_mutex_unlock(&m->mu);
        return RCP_ERR_BUSY;
    }
    rcp_mutex_unlock(&m->mu);

    cmd.zone     = zone;
    cmd.type     = RCP_CMD_SLEEP;
    cmd.priority = RCP_PRIORITY_HIGH;
    ec = rcp_controller_send(e->ctrl, ctx, &cmd, &resp);
    rcp_response_free(&resp);

    transition(m, e, ec == RCP_OK ? RCP_POWER_SLEEPING : RCP_POWER_BUS_OFF, ec);
    return ec;
}

//cfusa:req REQ-PWR-003
//cfusa:req REQ-PWR-004
int rcp_powerstate_manager_wake(rcp_powerstate_manager_t *m, const rcp_context_t *ctx, rcp_zone_t zone)
{
    zone_entry_t *e;
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    int ec;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, zone);
    if (!e) {
        rcp_mutex_unlock(&m->mu);
        return RCP_ERR_NOT_FOUND;
    }
    if (e->state == RCP_POWER_ACTIVE) {
        rcp_mutex_unlock(&m->mu);
        return RCP_ERR_BUSY;
    }
    rcp_mutex_unlock(&m->mu);

    cmd.zone     = zone;
    cmd.type     = RCP_CMD_WAKE;
    cmd.priority = RCP_PRIORITY_HIGH;
    ec = rcp_controller_send(e->ctrl, ctx, &cmd, &resp);
    rcp_response_free(&resp);

    transition(m, e, ec == RCP_OK ? RCP_POWER_ACTIVE : RCP_POWER_BUS_OFF, ec);
    return ec;
}

//cfusa:req REQ-PWR-005
//cfusa:req REQ-PWR-006
static void attempt_recovery(rcp_powerstate_manager_t *m)
{
    size_t i;

    for (i = 0; i < m->n_entries; i++) {
        zone_entry_t *e = &m->entries[i];
        rcp_command_t cmd = {0};
        rcp_response_t resp = {0};
        rcp_context_t ctx;
        int ec;
        bool is_bus_off;

        rcp_mutex_lock(&m->mu);
        is_bus_off = (e->state == RCP_POWER_BUS_OFF);
        rcp_mutex_unlock(&m->mu);
        if (!is_bus_off) continue;

        cmd.zone     = e->zone;
        cmd.type     = RCP_CMD_WAKE;
        cmd.priority = RCP_PRIORITY_HIGH;
        ctx = rcp_context_with_timeout_ms(m->cfg.recovery_timeout_ms);
        ec = rcp_controller_send(e->ctrl, &ctx, &cmd, &resp);
        rcp_response_free(&resp);

        if (ec == RCP_OK) transition(m, e, RCP_POWER_ACTIVE, RCP_OK);
    }
}

static void recover_thread_fn(void *arg)
{
    rcp_powerstate_manager_t *m = (rcp_powerstate_manager_t *)arg;

    for (;;) {
        bool closed_now;
        uint64_t waited_ms;

        rcp_mutex_lock(&m->mu);
        closed_now = m->closed;
        rcp_mutex_unlock(&m->mu);
        if (closed_now) break;

        attempt_recovery(m);

        waited_ms = 0;
        while (waited_ms < m->cfg.recovery_interval_ms) {
            unsigned chunk = (m->cfg.recovery_interval_ms - waited_ms) < 5
                                 ? (unsigned)(m->cfg.recovery_interval_ms - waited_ms) : 5;
            rcp_mutex_lock(&m->mu);
            closed_now = m->closed;
            rcp_mutex_unlock(&m->mu);
            if (closed_now) break;
            rcp_sleep_ms(chunk == 0 ? 1 : chunk);
            waited_ms += (chunk == 0 ? 1 : chunk);
        }
    }
}

rcp_powerstate_manager_t *rcp_powerstate_manager_new(rcp_powerstate_config_t cfg,
                                                      rcp_controller_t *const *ctrls, size_t n_ctrls)
{
    rcp_powerstate_manager_t *m = (rcp_powerstate_manager_t *)calloc(1, sizeof(*m));
    size_t i;

    if (!m) return NULL;
    m->cfg = cfg;
    rcp_mutex_init(&m->mu);

    if (n_ctrls > 0) {
        zone_entry_t *entries = (zone_entry_t *)calloc(n_ctrls, sizeof(*entries));
        m->entries = entries;
        if (!m->entries) {
            rcp_mutex_destroy(&m->mu);
            free(m);
            return NULL;
        }
    }
    for (i = 0; i < n_ctrls; i++) {
        m->entries[i].ctrl  = rcp_controller_retain(ctrls[i]);
        m->entries[i].zone  = rcp_controller_zone(ctrls[i]);
        m->entries[i].state = RCP_POWER_ACTIVE;
    }
    m->n_entries = n_ctrls;

    if (rcp_thread_start(&m->recover_thread, recover_thread_fn, m) == 0) {
        m->have_recover_thread = true;
    }

    return m;
}

rcp_power_state_t rcp_powerstate_manager_state(rcp_powerstate_manager_t *m, rcp_zone_t zone)
{
    zone_entry_t *e;
    rcp_power_state_t s;

    rcp_mutex_lock(&m->mu);
    e = find_entry(m, zone);
    s = e ? e->state : RCP_POWER_BUS_OFF;
    rcp_mutex_unlock(&m->mu);
    return s;
}

//cfusa:req REQ-PWR-007
bool rcp_powerstate_manager_subscribe(rcp_powerstate_manager_t *m, rcp_powerstate_power_fn cb, void *user_data)
{
    bool ok;
    rcp_mutex_lock(&m->mu);
    ok = callbacks_append(m, cb, user_data);
    rcp_mutex_unlock(&m->mu);
    return ok;
}

//cfusa:req REQ-PWR-008
void rcp_powerstate_manager_close(rcp_powerstate_manager_t *m)
{
    rcp_mutex_lock(&m->mu);
    m->closed = true;
    rcp_mutex_unlock(&m->mu);

    if (m->have_recover_thread) {
        rcp_thread_join(m->recover_thread);
        m->have_recover_thread = false;
    }
}

void rcp_powerstate_manager_destroy(rcp_powerstate_manager_t *m)
{
    size_t i;

    if (!m) return;
    rcp_powerstate_manager_close(m);

    for (i = 0; i < m->n_entries; i++) {
        rcp_controller_release(m->entries[i].ctrl);
    }
    free(m->entries);
    free(m->callbacks);
    free(m->callback_ctx);
    rcp_mutex_destroy(&m->mu);
    free(m);
}
