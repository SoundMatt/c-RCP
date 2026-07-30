/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/deadline.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>

rcp_deadline_config_t rcp_deadline_default_config(void)
{
    rcp_deadline_config_t c;
    c.default_deadline_ms = 50;
    c.poll_interval_ms    = 5;
    return c;
}

typedef struct {
    uint64_t stream_id;
    uint64_t deadline_ms;   /* effective, after applying the config default */
    uint64_t last_signal_ms; /* last heartbeat, or monitor construction time */
    bool     alive;
    bool     ever_reported;
} stream_watch_t;

struct rcp_deadline_monitor {
    rcp_deadline_config_t    cfg;
    rcp_mutex_t               mu; /* protects states[], callbacks[], closed */
    stream_watch_t           *states;
    size_t                    n_states;
    rcp_deadline_liveness_fn *callbacks;
    void                    **callback_ctx;
    size_t                    n_callbacks;
    size_t                    callbacks_cap;
    bool                      closed;
    rcp_thread_t              run_thread;
    bool                      have_run_thread;
};

static stream_watch_t *find_state(rcp_deadline_monitor_t *m, uint64_t stream_id)
{
    size_t i;
    for (i = 0; i < m->n_states; i++) {
        if (m->states[i].stream_id == stream_id) return &m->states[i];
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

//cfusa:req REQ-DL-004
//cfusa:req REQ-DL-005
static void emit(rcp_deadline_monitor_t *m, stream_watch_t *st, bool alive)
{
    rcp_liveness_event_t ev;
    size_t i;

    rcp_mutex_lock(&m->mu);
    st->alive         = alive;
    st->ever_reported = true;
    rcp_mutex_unlock(&m->mu);

    ev.stream_id = st->stream_id;
    ev.alive     = alive;

    /* Invoked outside the lock, matching watchdog.c's evaluate(): subscribe()
     * is documented as not thread-safe with close(), so this read is safe
     * without holding mu (only ever appended to before the run thread
     * starts touching them concurrently). */
    for (i = 0; i < m->n_callbacks; i++) {
        m->callbacks[i](&ev, m->callback_ctx[i]);
    }
}

//cfusa:req REQ-DL-001
bool rcp_deadline_monitor_heartbeat(rcp_deadline_monitor_t *m, uint64_t stream_id)
{
    stream_watch_t *st;
    bool was_alive = false;

    rcp_mutex_lock(&m->mu);
    st = find_state(m, stream_id);
    if (st) {
        st->last_signal_ms = rcp_monotonic_ms();
        was_alive = st->alive;
    }
    rcp_mutex_unlock(&m->mu);

    if (!st) return false;
    if (!was_alive) emit(m, st, true);
    return true;
}

//cfusa:req REQ-DL-006
bool rcp_deadline_monitor_notify_overflow(rcp_deadline_monitor_t *m, uint64_t stream_id)
{
    stream_watch_t *st;
    bool should_emit = false;

    rcp_mutex_lock(&m->mu);
    st = find_state(m, stream_id);
    if (st) should_emit = st->alive || !st->ever_reported;
    rcp_mutex_unlock(&m->mu);

    if (!st) return false;
    if (should_emit) emit(m, st, false);
    return true;
}

//cfusa:req REQ-DL-002
//cfusa:req REQ-DL-003
static void check_deadlines(rcp_deadline_monitor_t *m)
{
    uint64_t now_ms = rcp_monotonic_ms();
    size_t i;

    for (i = 0; i < m->n_states; i++) {
        stream_watch_t *st = &m->states[i];
        uint64_t elapsed;
        bool should_emit;

        rcp_mutex_lock(&m->mu);
        elapsed = (now_ms >= st->last_signal_ms) ? (now_ms - st->last_signal_ms) : 0;
        should_emit = (elapsed >= st->deadline_ms) && (st->alive || !st->ever_reported);
        rcp_mutex_unlock(&m->mu);

        if (should_emit) {
            emit(m, st, false);
        }
    }
}

static void run_thread_fn(void *arg)
{
    rcp_deadline_monitor_t *m = (rcp_deadline_monitor_t *)arg;

    for (;;) {
        bool closed_now;
        uint64_t waited_ms;

        rcp_mutex_lock(&m->mu);
        closed_now = m->closed;
        rcp_mutex_unlock(&m->mu);
        if (closed_now) break;

        check_deadlines(m);

        waited_ms = 0;
        while (waited_ms < m->cfg.poll_interval_ms) {
            unsigned chunk = (m->cfg.poll_interval_ms - waited_ms) < 5
                                 ? (unsigned)(m->cfg.poll_interval_ms - waited_ms) : 5;
            rcp_mutex_lock(&m->mu);
            closed_now = m->closed;
            rcp_mutex_unlock(&m->mu);
            if (closed_now) break;
            rcp_sleep_ms(chunk == 0 ? 1 : chunk);
            waited_ms += (chunk == 0 ? 1 : chunk);
        }
    }
}

rcp_deadline_monitor_t *rcp_deadline_monitor_new(rcp_deadline_config_t cfg,
                                                  const rcp_deadline_stream_cfg_t *streams,
                                                  size_t n_streams)
{
    rcp_deadline_monitor_t *m = (rcp_deadline_monitor_t *)calloc(1, sizeof(*m));
    uint64_t now_ms;
    size_t i;

    if (!m) return NULL;
    m->cfg = cfg;
    rcp_mutex_init(&m->mu);

    if (n_streams > 0) {
        stream_watch_t *states = (stream_watch_t *)calloc(n_streams, sizeof(*states));
        m->states = states;
        if (!m->states) {
            rcp_mutex_destroy(&m->mu);
            free(m);
            return NULL;
        }
    }
    now_ms = rcp_monotonic_ms();
    for (i = 0; i < n_streams; i++) {
        m->states[i].stream_id     = streams[i].stream_id;
        m->states[i].deadline_ms   = streams[i].deadline_ms != 0 ? streams[i].deadline_ms
                                                                   : cfg.default_deadline_ms;
        m->states[i].last_signal_ms = now_ms;
        m->states[i].alive          = false;
        m->states[i].ever_reported  = false;
    }
    m->n_states = n_streams;

    if (rcp_thread_start(&m->run_thread, run_thread_fn, m) == 0) {
        m->have_run_thread = true;
    }

    return m;
}

bool rcp_deadline_monitor_alive(rcp_deadline_monitor_t *m, uint64_t stream_id)
{
    stream_watch_t *st;
    bool alive;

    rcp_mutex_lock(&m->mu);
    st = find_state(m, stream_id);
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
    rcp_mutex_lock(&m->mu);
    m->closed = true;
    rcp_mutex_unlock(&m->mu);

    if (m->have_run_thread) {
        rcp_thread_join(m->run_thread);
        m->have_run_thread = false;
    }
}

void rcp_deadline_monitor_destroy(rcp_deadline_monitor_t *m)
{
    if (!m) return;
    rcp_deadline_monitor_close(m);

    free(m->states);
    free(m->callbacks);
    free(m->callback_ctx);
    rcp_mutex_destroy(&m->mu);
    free(m);
}
