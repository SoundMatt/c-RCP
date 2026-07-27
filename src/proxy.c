#include "rcp/proxy.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>

rcp_proxy_config_t rcp_proxy_default_config(void)
{
    rcp_proxy_config_t c;
    c.latency_budget_ms = 50;
    return c;
}

/* ── ProxyController ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t   base;
    rcp_controller_t  *upstream; /* retained */
    rcp_proxy_config_t  cfg;
} proxy_controller_t;

static rcp_zone_t proxy_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((proxy_controller_t *)self)->upstream);
}

//cfusa:req REQ-PROXY-001
static int proxy_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                            const rcp_command_t *cmd, rcp_response_t *out)
{
    proxy_controller_t *pc = (proxy_controller_t *)self;
    uint64_t budget_deadline_ms = rcp_monotonic_ms() + pc->cfg.latency_budget_ms;
    rcp_context_t proxy_ctx;

    if (ctx->has_deadline && ctx->deadline_ms <= budget_deadline_ms) {
        proxy_ctx = *ctx;
    } else {
        proxy_ctx = rcp_context_with_deadline_ms(budget_deadline_ms);
    }

    return rcp_controller_send(pc->upstream, &proxy_ctx, cmd, out);
}

//cfusa:req REQ-PROXY-007
static int proxy_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    proxy_controller_t *pc = (proxy_controller_t *)self;
    return rcp_controller_subscribe(pc->upstream, ctx, out);
}

static int proxy_ctrl_close(rcp_controller_t *self)
{
    proxy_controller_t *pc = (proxy_controller_t *)self;
    return rcp_controller_close(pc->upstream);
}

static void proxy_ctrl_destroy(rcp_controller_t *self)
{
    proxy_controller_t *pc = (proxy_controller_t *)self;
    rcp_controller_release(pc->upstream);
    free(pc);
}

static const rcp_controller_vtable_t proxy_controller_vtable = {
    proxy_ctrl_zone,
    proxy_ctrl_send,
    proxy_ctrl_subscribe,
    proxy_ctrl_close,
    proxy_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_proxy_controller_new(rcp_controller_t *upstream, rcp_proxy_config_t cfg)
{
    proxy_controller_t *pc = (proxy_controller_t *)calloc(1, sizeof(*pc));
    if (!pc) return NULL;
    pc->base.vt       = &proxy_controller_vtable;
    pc->base.refcount = 1;
    pc->upstream      = rcp_controller_retain(upstream);
    pc->cfg           = cfg;
    return &pc->base;
}

/* ── ProxyRegistry ─────────────────────────────────────────────────────────── */

typedef struct {
    rcp_zone_t         zone;
    rcp_controller_t  *ctrl; /* registry holds one reference */
} proxy_registry_entry_t;

typedef struct {
    rcp_registry_t            base;
    rcp_mutex_t                 mu;
    bool                        closed;
    proxy_registry_entry_t    *entries;
    size_t                      len;
    size_t                      cap;
} proxy_registry_t;

static int proxy_registry_register(rcp_registry_t *self, rcp_controller_t *ctrl)
{
    proxy_registry_t *pr = (proxy_registry_t *)self;
    rcp_zone_t          zone = rcp_controller_zone(ctrl);
    size_t              i;

    rcp_mutex_lock(&pr->mu);
    if (pr->closed) {
        rcp_mutex_unlock(&pr->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < pr->len; i++) {
        if (pr->entries[i].zone == zone) {
            rcp_mutex_unlock(&pr->mu);
            return RCP_ERR_ALREADY_EXISTS;
        }
    }
    if (pr->len == pr->cap) {
        size_t new_cap = (pr->cap == 0) ? 8 : pr->cap * 2;
        proxy_registry_entry_t *grown =
            (proxy_registry_entry_t *)realloc(pr->entries, new_cap * sizeof(*grown));
        if (!grown) {
            rcp_mutex_unlock(&pr->mu);
            return RCP_ERR_BUSY;
        }
        pr->entries = grown;
        pr->cap     = new_cap;
    }
    pr->entries[pr->len].zone = zone;
    pr->entries[pr->len].ctrl = rcp_controller_retain(ctrl);
    pr->len++;
    rcp_mutex_unlock(&pr->mu);
    return RCP_OK;
}

//cfusa:req REQ-PROXY-003
static int proxy_registry_deregister(rcp_registry_t *self, rcp_zone_t zone)
{
    proxy_registry_t *pr = (proxy_registry_t *)self;
    rcp_controller_t   *found = NULL;
    size_t               i;

    rcp_mutex_lock(&pr->mu);
    for (i = 0; i < pr->len; i++) {
        if (pr->entries[i].zone == zone) {
            found = pr->entries[i].ctrl;
            pr->entries[i] = pr->entries[pr->len - 1];
            pr->len--;
            break;
        }
    }
    rcp_mutex_unlock(&pr->mu);

    if (!found) return RCP_ERR_NOT_FOUND;

    rcp_controller_close(found);
    rcp_controller_release(found);
    return RCP_OK;
}

//cfusa:req REQ-PROXY-002
//cfusa:req REQ-PROXY-004
static int proxy_registry_lookup(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out)
{
    proxy_registry_t *pr = (proxy_registry_t *)self;
    size_t              i;

    rcp_mutex_lock(&pr->mu);
    if (pr->closed) {
        rcp_mutex_unlock(&pr->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < pr->len; i++) {
        if (pr->entries[i].zone == zone) {
            *out = rcp_controller_retain(pr->entries[i].ctrl);
            rcp_mutex_unlock(&pr->mu);
            return RCP_OK;
        }
    }
    rcp_mutex_unlock(&pr->mu);
    return RCP_ERR_NOT_FOUND;
}

static size_t proxy_registry_controllers(rcp_registry_t *self, rcp_controller_t **out, size_t cap)
{
    proxy_registry_t *pr = (proxy_registry_t *)self;
    size_t              i, n;

    rcp_mutex_lock(&pr->mu);
    n = pr->len;
    for (i = 0; i < n && i < cap; i++) {
        out[i] = rcp_controller_retain(pr->entries[i].ctrl);
    }
    rcp_mutex_unlock(&pr->mu);
    return n;
}

//cfusa:req REQ-PROXY-005
static int proxy_registry_close(rcp_registry_t *self)
{
    proxy_registry_t         *pr = (proxy_registry_t *)self;
    bool                        was_open;
    proxy_registry_entry_t    *local = NULL;
    size_t                      local_len = 0;
    size_t                      i;

    rcp_mutex_lock(&pr->mu);
    was_open = !pr->closed;
    if (was_open) {
        pr->closed  = true;
        local       = pr->entries;
        local_len   = pr->len;
        pr->entries = NULL;
        pr->len     = 0;
        pr->cap     = 0;
    }
    rcp_mutex_unlock(&pr->mu);

    if (!was_open) return RCP_OK;

    for (i = 0; i < local_len; i++) {
        rcp_controller_close(local[i].ctrl);
        rcp_controller_release(local[i].ctrl);
    }
    free(local);
    return RCP_OK;
}

static void proxy_registry_destroy(rcp_registry_t *self)
{
    proxy_registry_t *pr = (proxy_registry_t *)self;
    (void)proxy_registry_close(self); /* idempotent; releases any remaining refs */
    rcp_mutex_destroy(&pr->mu);
    free(pr->entries); /* NULL after close(); freeing NULL is a no-op */
    free(pr);
}

static const rcp_registry_vtable_t proxy_registry_vtable = {
    proxy_registry_register,
    proxy_registry_deregister,
    proxy_registry_lookup,
    proxy_registry_controllers,
    proxy_registry_close,
    proxy_registry_destroy,
};

rcp_registry_t *rcp_proxy_registry_new(void)
{
    proxy_registry_t *pr = (proxy_registry_t *)calloc(1, sizeof(*pr));
    if (!pr) return NULL;
    pr->base.vt = &proxy_registry_vtable;
    rcp_mutex_init(&pr->mu);
    return &pr->base;
}

//cfusa:req REQ-PROXY-006
int rcp_proxy_registry_add_route(rcp_registry_t *reg, rcp_controller_t *upstream, rcp_proxy_config_t cfg)
{
    rcp_controller_t *pc = rcp_proxy_controller_new(upstream, cfg);
    int ec;

    if (!pc) return RCP_ERR_BUSY;
    ec = rcp_registry_register(reg, pc);
    rcp_controller_release(pc); /* registry retained its own reference in register_ctrl */
    return ec;
}
