#include "rcp/shmem.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

/* ── ZoneServer ────────────────────────────────────────────────────────────── */

struct rcp_shmem_zone_server {
    rcp_zone_t            zone;
    rcp_mutex_t           mu; /* protects handler/user_data/healthy/closed/seq/subs */
    rcp_shmem_handler_fn  handler;
    void                 *user_data;
    bool                  closed;
    bool                  healthy;
    uint32_t              seq;
    rcp_status_channel_t **subs;
    size_t                 subs_len;
    size_t                 subs_cap;
    int                    refcount;
};

rcp_shmem_zone_server_t *rcp_shmem_zone_server_new(rcp_zone_t zone)
{
    rcp_shmem_zone_server_t *srv = (rcp_shmem_zone_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->zone     = zone;
    srv->healthy  = true;
    srv->refcount = 1;
    rcp_mutex_init(&srv->mu);
    return srv;
}

rcp_shmem_zone_server_t *rcp_shmem_zone_server_retain(rcp_shmem_zone_server_t *srv)
{
    if (srv) rcp_atomic_inc(&srv->refcount);
    return srv;
}

void rcp_shmem_zone_server_release(rcp_shmem_zone_server_t *srv)
{
    if (!srv) return;
    if (rcp_atomic_dec(&srv->refcount) != 0) return;
    rcp_mutex_destroy(&srv->mu);
    free(srv->subs);
    free(srv);
}

rcp_zone_t rcp_shmem_zone_server_zone(const rcp_shmem_zone_server_t *srv)
{
    return srv->zone;
}

void rcp_shmem_zone_server_set_handler(rcp_shmem_zone_server_t *srv, rcp_shmem_handler_fn handler, void *user_data)
{
    rcp_mutex_lock(&srv->mu);
    srv->handler   = handler;
    srv->user_data = user_data;
    rcp_mutex_unlock(&srv->mu);
}

void rcp_shmem_zone_server_set_healthy(rcp_shmem_zone_server_t *srv, bool healthy)
{
    rcp_mutex_lock(&srv->mu);
    srv->healthy = healthy;
    rcp_mutex_unlock(&srv->mu);
}

//cfusa:req REQ-SHMEM-005
void rcp_shmem_zone_server_publish(rcp_shmem_zone_server_t *srv, const uint8_t *payload, size_t len)
{
    rcp_status_t st;
    rcp_status_channel_t **snapshot = NULL;
    size_t snapshot_len = 0;
    size_t i;

    memset(&st, 0, sizeof(st));

    rcp_mutex_lock(&srv->mu);
    srv->seq++;
    st.zone         = srv->zone;
    st.seq          = srv->seq;
    st.healthy      = srv->healthy;
    st.payload.data = (uint8_t *)(uintptr_t)payload; /* read-only: push() below copies it */
    st.payload.len  = len;

    if (srv->subs_len > 0) {
        size_t n = srv->subs_len;
        snapshot = (rcp_status_channel_t **)malloc(n * sizeof(*snapshot));
        if (snapshot) {
            for (i = 0; i < n; i++) snapshot[i] = rcp_status_channel_retain(srv->subs[i]);
            snapshot_len = n;
        }
    }
    rcp_mutex_unlock(&srv->mu);

    for (i = 0; i < snapshot_len; i++) {
        rcp_status_channel_push(snapshot[i], &st);
        rcp_status_channel_release(snapshot[i]);
    }
    free(snapshot);
}

//cfusa:req REQ-SHMEM-001
//cfusa:req REQ-SHMEM-002
bool rcp_shmem_zone_server_dispatch_one(rcp_shmem_zone_server_t *srv, const rcp_command_t *cmd, rcp_response_t *out)
{
    bool closed_now;

    rcp_mutex_lock(&srv->mu);
    closed_now = srv->closed;
    if (!closed_now) {
        memset(out, 0, sizeof(*out));
        if (srv->handler) {
            srv->handler(cmd, out, srv->user_data);
        } else {
            out->command_id = cmd->id;
            out->zone       = srv->zone;
            out->status     = RCP_RESPONSE_OK;
        }
    }
    rcp_mutex_unlock(&srv->mu);
    return !closed_now;
}

static bool zserv_subs_append(rcp_shmem_zone_server_t *srv, rcp_status_channel_t *ch)
{
    if (srv->subs_len == srv->subs_cap) {
        size_t new_cap = (srv->subs_cap == 0) ? 4 : srv->subs_cap * 2;
        rcp_status_channel_t **grown =
            (rcp_status_channel_t **)realloc(srv->subs, new_cap * sizeof(*grown));
        if (!grown) return false;
        srv->subs     = grown;
        srv->subs_cap = new_cap;
    }
    srv->subs[srv->subs_len++] = ch;
    return true;
}

void rcp_shmem_zone_server_add_sub(rcp_shmem_zone_server_t *srv, rcp_status_channel_t *ch)
{
    rcp_mutex_lock(&srv->mu);
    (void)zserv_subs_append(srv, ch);
    rcp_mutex_unlock(&srv->mu);
}

void rcp_shmem_zone_server_remove_sub(rcp_shmem_zone_server_t *srv, rcp_status_channel_t *ch)
{
    size_t i;
    rcp_mutex_lock(&srv->mu);
    for (i = 0; i < srv->subs_len; i++) {
        if (srv->subs[i] == ch) {
            srv->subs[i] = srv->subs[srv->subs_len - 1];
            srv->subs_len--;
            break;
        }
    }
    rcp_mutex_unlock(&srv->mu);
}

void rcp_shmem_zone_server_close(rcp_shmem_zone_server_t *srv)
{
    rcp_status_channel_t **local = NULL;
    size_t local_len = 0;
    size_t i;

    rcp_mutex_lock(&srv->mu);
    srv->closed = true;
    local        = srv->subs;
    local_len    = srv->subs_len;
    srv->subs     = NULL;
    srv->subs_len = 0;
    srv->subs_cap = 0;
    rcp_mutex_unlock(&srv->mu);

    for (i = 0; i < local_len; i++) {
        rcp_status_channel_close(local[i]);
    }
    free(local);
}

bool rcp_shmem_zone_server_ok(const rcp_shmem_zone_server_t *srv)
{
    /* const-correctness note: reading a mutex-protected bool without the
     * lock here would be a data race in the general case, but this mirrors
     * cpp-RCP's own `!closed_.load()` snapshot read — a point-in-time
     * liveness check, not a synchronization primitive. */
    return !srv->closed;
}

/* ── Controller ────────────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t          base;
    rcp_shmem_zone_server_t  *server; /* retained */
    rcp_mutex_t               mu;     /* protects closed */
    bool                      closed;
} shmem_controller_t;

static rcp_zone_t shmem_ctrl_zone(rcp_controller_t *self)
{
    shmem_controller_t *c = (shmem_controller_t *)self;
    return rcp_shmem_zone_server_zone(c->server);
}

//cfusa:req REQ-SHMEM-003
//cfusa:req REQ-SHMEM-004
//cfusa:req REQ-SHMEM-006
static int shmem_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                            const rcp_command_t *cmd, rcp_response_t *out)
{
    shmem_controller_t *c = (shmem_controller_t *)self;
    rcp_command_t safe;
    bool closed_now;

    rcp_mutex_lock(&c->mu);
    closed_now = c->closed;
    rcp_mutex_unlock(&c->mu);
    if (closed_now) return RCP_ERR_CLOSED;

    if (rcp_context_done(ctx)) return RCP_ERR_TIMEOUT;
    if (cmd->zone != rcp_shmem_zone_server_zone(c->server)) return RCP_ERR_ZONE_MISMATCH;

    /* Copy payload before dispatch (REQ-SHMEM-006: no aliasing) — the
     * handler must not observe caller-side mutation after this call. */
    safe = *cmd;
    safe.payload = rcp_bytes_dup(cmd->payload.data, cmd->payload.len);

    if (!rcp_shmem_zone_server_dispatch_one(c->server, &safe, out)) {
        rcp_bytes_free(&safe.payload);
        return RCP_ERR_CLOSED;
    }
    rcp_bytes_free(&safe.payload);
    return RCP_OK;
}

typedef struct {
    rcp_controller_t         *ctrl_base; /* retained, cast back to shmem_controller_t */
    rcp_shmem_zone_server_t  *server;     /* retained */
    rcp_status_channel_t     *ch;         /* retained */
    rcp_context_t             ctx;
} shmem_watcher_args_t;

static void shmem_watcher_thread_fn(void *arg)
{
    shmem_watcher_args_t *w = (shmem_watcher_args_t *)arg;
    shmem_controller_t *c = (shmem_controller_t *)w->ctrl_base;

    for (;;) {
        bool closed_now;
        rcp_mutex_lock(&c->mu);
        closed_now = c->closed;
        rcp_mutex_unlock(&c->mu);

        if (closed_now) break;
        if (rcp_status_channel_is_closed(w->ch)) break;
        if (rcp_context_done(&w->ctx)) break;

        rcp_sleep_ms(1);
    }

    rcp_shmem_zone_server_remove_sub(w->server, w->ch);
    rcp_status_channel_close(w->ch);

    rcp_status_channel_release(w->ch);
    rcp_shmem_zone_server_release(w->server);
    rcp_controller_release(w->ctrl_base);
    free(w);
}

//cfusa:req REQ-SHMEM-005
static int shmem_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    shmem_controller_t *c = (shmem_controller_t *)self;
    rcp_status_channel_t *ch;
    shmem_watcher_args_t *w;
    bool closed_now;

    rcp_mutex_lock(&c->mu);
    closed_now = c->closed;
    rcp_mutex_unlock(&c->mu);
    if (closed_now) return RCP_ERR_CLOSED;

    ch = rcp_status_channel_new(16);
    if (!ch) return RCP_ERR_BUSY;

    rcp_shmem_zone_server_add_sub(c->server, ch);
    *out = rcp_status_channel_retain(ch);

    w = (shmem_watcher_args_t *)malloc(sizeof(*w));
    if (w) {
        w->ctrl_base = rcp_controller_retain(self);
        w->server    = rcp_shmem_zone_server_retain(c->server);
        w->ch        = rcp_status_channel_retain(ch);
        w->ctx       = *ctx;
        if (rcp_thread_start_detached(shmem_watcher_thread_fn, w) != 0) {
            rcp_controller_release(w->ctrl_base);
            rcp_shmem_zone_server_release(w->server);
            rcp_status_channel_release(w->ch);
            free(w);
        }
    }
    return RCP_OK;
}

static int shmem_ctrl_close(rcp_controller_t *self)
{
    shmem_controller_t *c = (shmem_controller_t *)self;
    rcp_mutex_lock(&c->mu);
    c->closed = true;
    rcp_mutex_unlock(&c->mu);
    return RCP_OK;
}

static void shmem_ctrl_destroy(rcp_controller_t *self)
{
    shmem_controller_t *c = (shmem_controller_t *)self;
    rcp_mutex_destroy(&c->mu);
    rcp_shmem_zone_server_release(c->server);
    free(c);
}

static const rcp_controller_vtable_t shmem_controller_vtable = {
    shmem_ctrl_zone,
    shmem_ctrl_send,
    shmem_ctrl_subscribe,
    shmem_ctrl_close,
    shmem_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_shmem_controller_new(rcp_shmem_zone_server_t *server)
{
    shmem_controller_t *c = (shmem_controller_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->base.vt       = &shmem_controller_vtable;
    c->base.refcount = 1;
    c->server        = rcp_shmem_zone_server_retain(server);
    rcp_mutex_init(&c->mu);
    return &c->base;
}

/* ── Registry ──────────────────────────────────────────────────────────────── */

typedef struct {
    rcp_zone_t         zone;
    rcp_controller_t  *ctrl;
} shmem_registry_entry_t;

typedef struct {
    rcp_registry_t           base;
    rcp_mutex_t              mu;
    bool                     closed;
    shmem_registry_entry_t  *entries;
    size_t                   len;
    size_t                   cap;
} shmem_registry_t;

//cfusa:req REQ-SHMEM-007
static int shmem_reg_register(rcp_registry_t *self, rcp_controller_t *ctrl)
{
    shmem_registry_t *r = (shmem_registry_t *)self;
    rcp_zone_t zone = rcp_controller_zone(ctrl);
    size_t i;

    rcp_mutex_lock(&r->mu);
    if (r->closed) {
        rcp_mutex_unlock(&r->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < r->len; i++) {
        if (r->entries[i].zone == zone) {
            rcp_mutex_unlock(&r->mu);
            return RCP_ERR_ALREADY_EXISTS;
        }
    }
    if (r->len == r->cap) {
        size_t new_cap = (r->cap == 0) ? 8 : r->cap * 2;
        shmem_registry_entry_t *grown =
            (shmem_registry_entry_t *)realloc(r->entries, new_cap * sizeof(*grown));
        if (!grown) {
            rcp_mutex_unlock(&r->mu);
            return RCP_ERR_BUSY;
        }
        r->entries = grown;
        r->cap     = new_cap;
    }
    r->entries[r->len].zone = zone;
    r->entries[r->len].ctrl = rcp_controller_retain(ctrl);
    r->len++;
    rcp_mutex_unlock(&r->mu);
    return RCP_OK;
}

static int shmem_reg_deregister(rcp_registry_t *self, rcp_zone_t zone)
{
    shmem_registry_t *r = (shmem_registry_t *)self;
    rcp_controller_t *found = NULL;
    size_t i;

    rcp_mutex_lock(&r->mu);
    for (i = 0; i < r->len; i++) {
        if (r->entries[i].zone == zone) {
            found = r->entries[i].ctrl;
            r->entries[i] = r->entries[r->len - 1];
            r->len--;
            break;
        }
    }
    rcp_mutex_unlock(&r->mu);

    if (!found) return RCP_ERR_NOT_FOUND;

    rcp_controller_close(found);
    rcp_controller_release(found);
    return RCP_OK;
}

static int shmem_reg_lookup(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out)
{
    shmem_registry_t *r = (shmem_registry_t *)self;
    size_t i;

    rcp_mutex_lock(&r->mu);
    if (r->closed) {
        rcp_mutex_unlock(&r->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < r->len; i++) {
        if (r->entries[i].zone == zone) {
            *out = rcp_controller_retain(r->entries[i].ctrl);
            rcp_mutex_unlock(&r->mu);
            return RCP_OK;
        }
    }
    rcp_mutex_unlock(&r->mu);
    return RCP_ERR_NOT_FOUND;
}

static size_t shmem_reg_controllers(rcp_registry_t *self, rcp_controller_t **out, size_t cap)
{
    shmem_registry_t *r = (shmem_registry_t *)self;
    size_t i, n;

    rcp_mutex_lock(&r->mu);
    n = r->len;
    for (i = 0; i < n && i < cap; i++) {
        out[i] = rcp_controller_retain(r->entries[i].ctrl);
    }
    rcp_mutex_unlock(&r->mu);
    return n;
}

//cfusa:req REQ-SHMEM-008
static int shmem_reg_close(rcp_registry_t *self)
{
    shmem_registry_t *r = (shmem_registry_t *)self;
    bool was_open;
    shmem_registry_entry_t *local = NULL;
    size_t local_len = 0;
    size_t i;

    rcp_mutex_lock(&r->mu);
    was_open = !r->closed;
    if (was_open) {
        r->closed  = true;
        local      = r->entries;
        local_len  = r->len;
        r->entries = NULL;
        r->len     = 0;
        r->cap     = 0;
    }
    rcp_mutex_unlock(&r->mu);

    if (!was_open) return RCP_OK;

    for (i = 0; i < local_len; i++) {
        rcp_controller_close(local[i].ctrl);
        rcp_controller_release(local[i].ctrl);
    }
    free(local);
    return RCP_OK;
}

static void shmem_reg_destroy(rcp_registry_t *self)
{
    shmem_registry_t *r = (shmem_registry_t *)self;
    (void)shmem_reg_close(self);
    rcp_mutex_destroy(&r->mu);
    free(r->entries);
    free(r);
}

static const rcp_registry_vtable_t shmem_registry_vtable = {
    shmem_reg_register,
    shmem_reg_deregister,
    shmem_reg_lookup,
    shmem_reg_controllers,
    shmem_reg_close,
    shmem_reg_destroy,
};

rcp_registry_t *rcp_shmem_registry_new(void)
{
    shmem_registry_t *r = (shmem_registry_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->base.vt = &shmem_registry_vtable;
    rcp_mutex_init(&r->mu);
    return &r->base;
}

int rcp_shmem_registry_add_server(rcp_registry_t *reg, rcp_shmem_zone_server_t *server)
{
    rcp_controller_t *ctrl = rcp_shmem_controller_new(server);
    int rc;
    if (!ctrl) return RCP_ERR_BUSY;
    rc = rcp_registry_register(reg, ctrl);
    rcp_controller_release(ctrl); /* register() took its own reference */
    return rc;
}
