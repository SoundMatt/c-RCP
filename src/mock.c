#include "rcp/mock.h"

#include <stdlib.h>
#include <string.h>

#include "platform.h"

/* ── Mock controller ───────────────────────────────────────────────────────── */

typedef struct rcp_mock_controller {
    rcp_controller_t    base; /* first member: allows rcp_controller_t* <-> this cast */
    rcp_zone_t          zone;
    rcp_mock_handler_fn handler;
    void               *user_data;

    /* All mutable state below is protected by mu. A single mutex (rather
     * than an atomic closed flag plus a separate subs-list mutex, as
     * cpp-RCP's mock uses) is a deliberate simplification: this is a
     * test-only, zero-I/O backend, not a hot transport path. */
    rcp_mutex_t            mu;
    bool                   closed;
    uint32_t               seq;
    rcp_status_channel_t **subs;
    size_t                 subs_len;
    size_t                 subs_cap;
} rcp_mock_controller_t;

static bool mc_subs_append(rcp_mock_controller_t *mc, rcp_status_channel_t *ch)
{
    if (mc->subs_len == mc->subs_cap) {
        size_t new_cap = (mc->subs_cap == 0) ? 4 : mc->subs_cap * 2;
        rcp_status_channel_t **grown =
            (rcp_status_channel_t **)realloc(mc->subs, new_cap * sizeof(*grown));
        if (!grown) return false;
        mc->subs     = grown;
        mc->subs_cap = new_cap;
    }
    mc->subs[mc->subs_len++] = ch; /* subs array owns this reference */
    return true;
}

static void mc_subs_remove(rcp_mock_controller_t *mc, rcp_status_channel_t *ch)
{
    size_t i;
    for (i = 0; i < mc->subs_len; i++) {
        if (mc->subs[i] == ch) {
            mc->subs[i] = mc->subs[mc->subs_len - 1];
            mc->subs_len--;
            return;
        }
    }
}

static rcp_zone_t mock_controller_zone(rcp_controller_t *self)
{
    return ((rcp_mock_controller_t *)self)->zone;
}

//cfusa:req REQ-CTRL-001
//cfusa:req REQ-CTRL-002
//cfusa:req REQ-CTRL-003
//cfusa:req REQ-CTRL-004
//cfusa:req REQ-CTRL-013
//cfusa:req REQ-CTRL-014
//cfusa:req REQ-CTRL-015
//cfusa:req REQ-CTRL-016
//cfusa:req REQ-CTRL-018
//cfusa:req REQ-CTRL-023
//cfusa:req REQ-CTRL-024
//cfusa:req REQ-CTRL-025
//cfusa:req REQ-CTRL-026
//cfusa:req REQ-RESP-001
//cfusa:req REQ-RESP-002
//cfusa:req REQ-RESP-003
static int mock_controller_send(rcp_controller_t *self, const rcp_context_t *ctx,
                                 const rcp_command_t *cmd, rcp_response_t *out)
{
    rcp_mock_controller_t *mc = (rcp_mock_controller_t *)self;
    bool is_closed;

    rcp_mutex_lock(&mc->mu);
    is_closed = mc->closed;
    rcp_mutex_unlock(&mc->mu);
    if (is_closed) return RCP_ERR_CLOSED;

    if (rcp_context_done(ctx)) return RCP_ERR_TIMEOUT;
    if (cmd->zone != mc->zone) return RCP_ERR_ZONE_MISMATCH;

    memset(out, 0, sizeof(*out));

    if (mc->handler) {
        /* Copy payload before invoking the handler (REQ-CTRL-026): the
         * handler observes its own private copy, so caller-side mutation
         * of cmd->payload after this call cannot affect it. */
        rcp_command_t safe = *cmd;
        safe.payload = rcp_bytes_dup(cmd->payload.data, cmd->payload.len);
        mc->handler(&safe, out, mc->user_data);
        rcp_bytes_free(&safe.payload);
    } else {
        out->command_id = cmd->id;
        out->zone        = mc->zone;
        out->status      = RCP_RESPONSE_OK;
    }
    return RCP_OK;
}

typedef struct {
    rcp_mock_controller_t *mc; /* retained */
    rcp_status_channel_t   *ch; /* retained */
    rcp_context_t           ctx;
} watcher_args_t;

//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-011
static void watcher_thread_fn(void *arg)
{
    watcher_args_t *w = (watcher_args_t *)arg;

    for (;;) {
        bool mc_closed;

        rcp_mutex_lock(&w->mc->mu);
        mc_closed = w->mc->closed;
        rcp_mutex_unlock(&w->mc->mu);

        if (mc_closed) break;
        if (rcp_status_channel_is_closed(w->ch)) break;
        if (rcp_context_done(&w->ctx)) break;

        rcp_sleep_ms(1);
    }

    rcp_mutex_lock(&w->mc->mu);
    if (!w->mc->closed) {
        mc_subs_remove(w->mc, w->ch);
        rcp_status_channel_release(w->ch); /* release the subs array's reference */
    }
    rcp_mutex_unlock(&w->mc->mu);

    rcp_status_channel_close(w->ch);
    rcp_status_channel_release(w->ch); /* this watcher's own reference */
    rcp_controller_release(&w->mc->base);
    free(w);
}

//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-008
//cfusa:req REQ-CTRL-011
static int mock_controller_subscribe(rcp_controller_t *self, const rcp_context_t *ctx,
                                      rcp_status_channel_t **out)
{
    rcp_mock_controller_t *mc = (rcp_mock_controller_t *)self;
    rcp_status_channel_t   *ch;
    watcher_args_t         *w;

    rcp_mutex_lock(&mc->mu);
    if (mc->closed) {
        rcp_mutex_unlock(&mc->mu);
        return RCP_ERR_CLOSED;
    }
    ch = rcp_status_channel_new(16);
    if (!ch) {
        rcp_mutex_unlock(&mc->mu);
        return RCP_ERR_BUSY; /* allocation failure; no dedicated OOM sentinel */
    }
    if (!mc_subs_append(mc, ch)) {
        rcp_mutex_unlock(&mc->mu);
        rcp_status_channel_release(ch);
        return RCP_ERR_BUSY;
    }
    rcp_mutex_unlock(&mc->mu);

    *out = rcp_status_channel_retain(ch);

    /* Watcher thread: removes + closes the channel once ctx expires or the
     * controller closes. Best-effort — if the thread can't be started, the
     * subscription still works; it just won't self-expire on ctx timeout
     * (close() still closes every channel in subs directly). */
    w = (watcher_args_t *)malloc(sizeof(*w));
    if (w) {
        w->mc  = (rcp_mock_controller_t *)rcp_controller_retain(&mc->base);
        w->ch  = rcp_status_channel_retain(ch);
        w->ctx = *ctx;
        if (rcp_thread_start_detached(watcher_thread_fn, w) != 0) {
            rcp_controller_release(&w->mc->base);
            rcp_status_channel_release(w->ch);
            free(w);
        }
    }
    return RCP_OK;
}

//cfusa:req REQ-CTRL-006
//cfusa:req REQ-CTRL-010
//cfusa:req REQ-CTRL-012
//cfusa:req REQ-CTRL-017
//cfusa:req REQ-CTRL-019
//cfusa:req REQ-CTRL-020
//cfusa:req REQ-CTRL-021
//cfusa:req REQ-CTRL-022
//cfusa:req REQ-CTRL-027
//cfusa:req REQ-STAT-001
//cfusa:req REQ-STAT-002
//cfusa:req REQ-STAT-003
//cfusa:req REQ-STAT-004
//cfusa:req REQ-STAT-005
void rcp_mock_controller_publish(rcp_controller_t *ctrl, const uint8_t *payload, size_t len)
{
    rcp_mock_controller_t *mc = (rcp_mock_controller_t *)ctrl;
    rcp_status_t            st;
    rcp_status_channel_t  **snapshot = NULL;
    size_t                  snapshot_len = 0;
    size_t                  i;

    rcp_mutex_lock(&mc->mu);
    mc->seq++;
    st.zone    = mc->zone;
    st.seq     = mc->seq;
    st.healthy = !mc->closed;
    st.payload = rcp_bytes_dup(payload, len); /* copy before delivery (REQ-CTRL-027) */

    if (mc->subs_len > 0) {
        size_t n = mc->subs_len;
        snapshot = (rcp_status_channel_t **)malloc(n * sizeof(*snapshot));
        if (snapshot) {
            for (i = 0; i < n; i++) {
                snapshot[i] = rcp_status_channel_retain(mc->subs[i]);
            }
            snapshot_len = n;
        }
    }
    rcp_mutex_unlock(&mc->mu);

    for (i = 0; i < snapshot_len; i++) {
        rcp_status_channel_push(snapshot[i], &st);
        rcp_status_channel_release(snapshot[i]);
    }
    free(snapshot);
    rcp_status_free(&st);
}

//cfusa:req REQ-CTRL-005
//cfusa:req REQ-CTRL-007
static int mock_controller_close(rcp_controller_t *self)
{
    rcp_mock_controller_t *mc = (rcp_mock_controller_t *)self;
    bool                    was_open;
    rcp_status_channel_t  **local = NULL;
    size_t                  local_len = 0;
    size_t                  i;

    rcp_mutex_lock(&mc->mu);
    was_open = !mc->closed;
    if (was_open) {
        mc->closed = true;
        local      = mc->subs;
        local_len  = mc->subs_len;
        mc->subs     = NULL;
        mc->subs_len = 0;
        mc->subs_cap = 0;
    }
    rcp_mutex_unlock(&mc->mu);

    if (!was_open) return RCP_OK;

    for (i = 0; i < local_len; i++) {
        rcp_status_channel_close(local[i]);
        rcp_status_channel_release(local[i]);
    }
    free(local);
    return RCP_OK;
}

static void mock_controller_destroy(rcp_controller_t *self)
{
    rcp_mock_controller_t *mc = (rcp_mock_controller_t *)self;
    (void)mock_controller_close(self); /* idempotent; releases any remaining subs */
    rcp_mutex_destroy(&mc->mu);
    free(mc->subs); /* NULL after close(); freeing NULL is a no-op */
    free(mc);
}

static const rcp_controller_vtable_t mock_controller_vtable = {
    mock_controller_zone,
    mock_controller_send,
    mock_controller_subscribe,
    mock_controller_close,
    mock_controller_destroy,
};

rcp_controller_t *rcp_mock_controller_new(rcp_zone_t zone, rcp_mock_handler_fn handler, void *user_data)
{
    rcp_mock_controller_t *mc = (rcp_mock_controller_t *)calloc(1, sizeof(*mc));
    if (!mc) return NULL;

    mc->base.vt       = &mock_controller_vtable;
    mc->base.refcount = 1;
    mc->zone          = zone;
    mc->handler       = handler;
    mc->user_data     = user_data;
    rcp_mutex_init(&mc->mu);
    return &mc->base;
}

/* ── Mock registry ─────────────────────────────────────────────────────────── */

typedef struct {
    rcp_zone_t         zone;
    rcp_controller_t  *ctrl; /* registry holds one reference */
} rcp_mock_registry_entry_t;

typedef struct rcp_mock_registry {
    rcp_registry_t              base;
    rcp_mutex_t                 mu;
    bool                        closed;
    rcp_mock_registry_entry_t  *entries;
    size_t                      len;
    size_t                      cap;
} rcp_mock_registry_t;

//cfusa:req REQ-REG-002
//cfusa:req REQ-REG-007
//cfusa:req REQ-REG-009
static int mock_registry_register(rcp_registry_t *self, rcp_controller_t *ctrl)
{
    rcp_mock_registry_t *mr = (rcp_mock_registry_t *)self;
    rcp_zone_t            zone = rcp_controller_zone(ctrl);
    size_t                i;

    rcp_mutex_lock(&mr->mu);
    if (mr->closed) {
        rcp_mutex_unlock(&mr->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < mr->len; i++) {
        if (mr->entries[i].zone == zone) {
            rcp_mutex_unlock(&mr->mu);
            return RCP_ERR_ALREADY_EXISTS;
        }
    }
    if (mr->len == mr->cap) {
        size_t new_cap = (mr->cap == 0) ? 8 : mr->cap * 2;
        rcp_mock_registry_entry_t *grown =
            (rcp_mock_registry_entry_t *)realloc(mr->entries, new_cap * sizeof(*grown));
        if (!grown) {
            rcp_mutex_unlock(&mr->mu);
            return RCP_ERR_BUSY;
        }
        mr->entries = grown;
        mr->cap     = new_cap;
    }
    mr->entries[mr->len].zone = zone;
    mr->entries[mr->len].ctrl = rcp_controller_retain(ctrl);
    mr->len++;
    rcp_mutex_unlock(&mr->mu);
    return RCP_OK;
}

//cfusa:req REQ-REG-003
//cfusa:req REQ-REG-008
//cfusa:req REQ-REG-012
static int mock_registry_deregister(rcp_registry_t *self, rcp_zone_t zone)
{
    rcp_mock_registry_t *mr = (rcp_mock_registry_t *)self;
    rcp_controller_t     *found = NULL;
    size_t                i;

    rcp_mutex_lock(&mr->mu);
    for (i = 0; i < mr->len; i++) {
        if (mr->entries[i].zone == zone) {
            found = mr->entries[i].ctrl;
            mr->entries[i] = mr->entries[mr->len - 1];
            mr->len--;
            break;
        }
    }
    rcp_mutex_unlock(&mr->mu);

    if (!found) return RCP_ERR_NOT_FOUND;

    rcp_controller_close(found);
    rcp_controller_release(found);
    return RCP_OK;
}

//cfusa:req REQ-REG-004
//cfusa:req REQ-REG-011
//cfusa:req REQ-REG-013
static int mock_registry_lookup(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out)
{
    rcp_mock_registry_t *mr = (rcp_mock_registry_t *)self;
    size_t                i;

    rcp_mutex_lock(&mr->mu);
    if (mr->closed) {
        rcp_mutex_unlock(&mr->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < mr->len; i++) {
        if (mr->entries[i].zone == zone) {
            *out = rcp_controller_retain(mr->entries[i].ctrl);
            rcp_mutex_unlock(&mr->mu);
            return RCP_OK;
        }
    }
    rcp_mutex_unlock(&mr->mu);
    return RCP_ERR_NOT_FOUND;
}

//cfusa:req REQ-REG-006
static size_t mock_registry_controllers(rcp_registry_t *self, rcp_controller_t **out, size_t cap)
{
    rcp_mock_registry_t *mr = (rcp_mock_registry_t *)self;
    size_t                i, n;

    rcp_mutex_lock(&mr->mu);
    n = mr->len;
    for (i = 0; i < n && i < cap; i++) {
        out[i] = rcp_controller_retain(mr->entries[i].ctrl);
    }
    rcp_mutex_unlock(&mr->mu);
    return n;
}

//cfusa:req REQ-REG-005
//cfusa:req REQ-REG-010
static int mock_registry_close(rcp_registry_t *self)
{
    rcp_mock_registry_t       *mr = (rcp_mock_registry_t *)self;
    bool                        was_open;
    rcp_mock_registry_entry_t  *local = NULL;
    size_t                      local_len = 0;
    size_t                      i;

    rcp_mutex_lock(&mr->mu);
    was_open = !mr->closed;
    if (was_open) {
        mr->closed = true;
        local      = mr->entries;
        local_len  = mr->len;
        mr->entries = NULL;
        mr->len     = 0;
        mr->cap     = 0;
    }
    rcp_mutex_unlock(&mr->mu);

    if (!was_open) return RCP_OK;

    for (i = 0; i < local_len; i++) {
        rcp_controller_close(local[i].ctrl);
        rcp_controller_release(local[i].ctrl);
    }
    free(local);
    return RCP_OK;
}

static void mock_registry_destroy(rcp_registry_t *self)
{
    rcp_mock_registry_t *mr = (rcp_mock_registry_t *)self;
    (void)mock_registry_close(self); /* idempotent; releases any remaining refs */
    rcp_mutex_destroy(&mr->mu);
    free(mr->entries); /* NULL after close(); freeing NULL is a no-op */
    free(mr);
}

static const rcp_registry_vtable_t mock_registry_vtable = {
    mock_registry_register,
    mock_registry_deregister,
    mock_registry_lookup,
    mock_registry_controllers,
    mock_registry_close,
    mock_registry_destroy,
};

//cfusa:req REQ-REG-001
rcp_registry_t *rcp_mock_registry_new(void)
{
    static const rcp_zone_t standard_zones[5] = {
        RCP_ZONE_FRONT_LEFT, RCP_ZONE_FRONT_RIGHT,
        RCP_ZONE_REAR_LEFT,  RCP_ZONE_REAR_RIGHT,
        RCP_ZONE_CENTRAL,
    };
    rcp_mock_registry_t *mr;
    size_t                i;

    mr = (rcp_mock_registry_t *)calloc(1, sizeof(*mr));
    if (!mr) return NULL;

    mr->base.vt = &mock_registry_vtable;
    rcp_mutex_init(&mr->mu);

    {
        rcp_mock_registry_entry_t *entries =
            (rcp_mock_registry_entry_t *)calloc(5, sizeof(*entries));
        mr->entries = entries;
        mr->cap     = entries ? 5 : 0;
    }

    for (i = 0; i < 5 && mr->entries; i++) {
        rcp_controller_t *ctrl = rcp_mock_controller_new(standard_zones[i], NULL, NULL);
        if (!ctrl) continue;
        mr->entries[mr->len].zone = standard_zones[i];
        mr->entries[mr->len].ctrl = ctrl; /* transfers the fresh refcount-1 reference */
        mr->len++;
    }
    return &mr->base;
}
