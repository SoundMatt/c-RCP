#include "rcp/deadline.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>

rcp_deadline_config_t rcp_deadline_default_config(void)
{
    rcp_deadline_config_t c;
    c.deadline_ms = 50;
    return c;
}

typedef struct {
    rcp_controller_t *ctrl; /* retained */
    rcp_zone_t         zone;
    bool                alive;
    rcp_thread_t        thread;
    bool                have_thread;
} zone_watch_t;

struct rcp_deadline_monitor {
    rcp_deadline_config_t    cfg;
    rcp_mutex_t               mu; /* protects states[], callbacks[], closed */
    zone_watch_t             *states;
    size_t                    n_states;
    rcp_deadline_liveness_fn *callbacks;
    void                    **callback_ctx;
    size_t                    n_callbacks;
    size_t                    callbacks_cap;
    bool                      closed;
};

static zone_watch_t *find_state(rcp_deadline_monitor_t *m, rcp_zone_t z)
{
    size_t i;
    for (i = 0; i < m->n_states; i++) {
        if (m->states[i].zone == z) return &m->states[i];
    }
    return NULL;
}

static bool callbacks_append(rcp_deadline_monitor_t *m, rcp_deadline_liveness_fn cb, void *user_data)
{
    if (m->n_callbacks == m->callbacks_cap) {
        size_t new_cap = (m->callbacks_cap == 0) ? 4 : m->callbacks_cap * 2;
        rcp_deadline_liveness_fn *grown_cb = (rcp_deadline_liveness_fn *)realloc(m->callbacks, new_cap * sizeof(*grown_cb));
        void                    **grown_ctx;
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

//cfusa:req REQ-DL-003
//cfusa:req REQ-DL-004
//cfusa:req REQ-DL-005
static void emit(rcp_deadline_monitor_t *m, zone_watch_t *st, bool alive, int err)
{
    rcp_liveness_event_t ev;
    size_t i;

    rcp_mutex_lock(&m->mu);
    st->alive = alive;
    rcp_mutex_unlock(&m->mu);

    ev.zone  = st->zone;
    ev.alive = alive;
    ev.err   = err;

    /* Callbacks are invoked outside the lock, matching watchdog.c's kick():
     * subscribe() is documented as not thread-safe with close(), so reading
     * n_callbacks/callbacks here without the lock is safe (they're only
     * ever appended to before any watch thread starts touching them). */
    for (i = 0; i < m->n_callbacks; i++) {
        m->callbacks[i](&ev, m->callback_ctx[i]);
    }
}

typedef struct {
    rcp_deadline_monitor_t *m;
    zone_watch_t            *st;
} watch_args_t;

//cfusa:req REQ-DL-001
//cfusa:req REQ-DL-002
//cfusa:req REQ-DL-006
static void watch_thread_fn(void *arg)
{
    watch_args_t *a = (watch_args_t *)arg;
    rcp_deadline_monitor_t *m = a->m;
    zone_watch_t *st = a->st;
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    bool is_alive = false;
    bool ever_reported = false;
    uint64_t deadline_ms;
    int ec;

    free(a);

    ec = rcp_controller_subscribe(st->ctrl, &ctx, &ch);
    if (ec != RCP_OK) {
        emit(m, st, false, ec);
        return;
    }

    deadline_ms = rcp_monotonic_ms() + m->cfg.deadline_ms;

    for (;;) {
        bool closed_now;
        uint64_t now;
        rcp_status_t status;

        rcp_mutex_lock(&m->mu);
        closed_now = m->closed;
        rcp_mutex_unlock(&m->mu);
        if (closed_now) break;

        now = rcp_monotonic_ms();
        if (now >= deadline_ms) {
            if (is_alive || !ever_reported) {
                is_alive      = false;
                ever_reported = true;
                emit(m, st, false, RCP_OK);
            }
            deadline_ms = now + m->cfg.deadline_ms;
            rcp_sleep_ms(1);
            continue;
        }

        if (rcp_status_channel_is_closed(ch)) break;

        if (rcp_status_channel_try_recv(ch, &status)) {
            rcp_status_free(&status);
            deadline_ms = rcp_monotonic_ms() + m->cfg.deadline_ms;
            if (!is_alive) {
                is_alive = true;
                emit(m, st, true, RCP_OK);
            }
        } else {
            rcp_sleep_ms(1);
        }
    }

    if (is_alive) emit(m, st, false, RCP_OK);

    rcp_status_channel_release(ch);
}

rcp_deadline_monitor_t *rcp_deadline_monitor_new(rcp_deadline_config_t cfg,
                                                  rcp_controller_t *const *ctrls, size_t n_ctrls)
{
    rcp_deadline_monitor_t *m = (rcp_deadline_monitor_t *)calloc(1, sizeof(*m));
    size_t i;

    if (!m) return NULL;
    m->cfg = cfg;
    rcp_mutex_init(&m->mu);

    if (n_ctrls > 0) {
        zone_watch_t *states = (zone_watch_t *)calloc(n_ctrls, sizeof(*states));
        m->states = states;
        if (!m->states) {
            rcp_mutex_destroy(&m->mu);
            free(m);
            return NULL;
        }
    }
    for (i = 0; i < n_ctrls; i++) {
        m->states[i].ctrl  = rcp_controller_retain(ctrls[i]);
        m->states[i].zone  = rcp_controller_zone(ctrls[i]);
        m->states[i].alive = false;
    }
    m->n_states = n_ctrls;

    for (i = 0; i < n_ctrls; i++) {
        watch_args_t *a = (watch_args_t *)malloc(sizeof(*a));
        if (!a) continue;
        a->m  = m;
        a->st = &m->states[i];
        if (rcp_thread_start(&m->states[i].thread, watch_thread_fn, a) == 0) {
            m->states[i].have_thread = true;
        } else {
            free(a);
        }
    }

    return m;
}

bool rcp_deadline_monitor_alive(rcp_deadline_monitor_t *m, rcp_zone_t zone)
{
    zone_watch_t *st;
    bool alive;

    rcp_mutex_lock(&m->mu);
    st = find_state(m, zone);
    alive = st ? st->alive : false;
    rcp_mutex_unlock(&m->mu);
    return alive;
}

bool rcp_deadline_monitor_subscribe(rcp_deadline_monitor_t *m, rcp_deadline_liveness_fn cb, void *user_data)
{
    bool ok;
    rcp_mutex_lock(&m->mu);
    ok = callbacks_append(m, cb, user_data);
    rcp_mutex_unlock(&m->mu);
    return ok;
}

//cfusa:req REQ-DL-007
//cfusa:req REQ-DL-008
void rcp_deadline_monitor_close(rcp_deadline_monitor_t *m)
{
    size_t i;

    rcp_mutex_lock(&m->mu);
    m->closed = true;
    rcp_mutex_unlock(&m->mu);

    for (i = 0; i < m->n_states; i++) {
        if (m->states[i].have_thread) {
            rcp_thread_join(m->states[i].thread);
            m->states[i].have_thread = false;
        }
    }
}

void rcp_deadline_monitor_destroy(rcp_deadline_monitor_t *m)
{
    size_t i;

    if (!m) return;
    rcp_deadline_monitor_close(m);

    for (i = 0; i < m->n_states; i++) {
        rcp_controller_release(m->states[i].ctrl);
    }
    free(m->states);
    free(m->callbacks);
    free(m->callback_ctx);
    rcp_mutex_destroy(&m->mu);
    free(m);
}
