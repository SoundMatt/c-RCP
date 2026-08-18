/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/admin.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char   name[64];
    char    labels[128];
    double  value;
} admin_counter_t;

typedef struct {
    rcp_admin_event_fn cb;
    void               *user_data;
} admin_subscriber_t;

/* [c-RCP-17] Worst-case rendered size of one rcp_admin_server_metrics_
 * text() line: bounded by that function's own `line[256]` stack buffer
 * and its `written = min(n, sizeof(line) - 1)` clamp, so 255 bytes is the
 * true per-line ceiling, not an estimate. */
#define ADMIN_METRICS_LINE_MAX ((size_t)255u)
#define ADMIN_METRICS_TEXT_MAX ((size_t)(RCP_ADMIN_MAX_COUNTERS * ADMIN_METRICS_LINE_MAX) + 1u)

struct rcp_admin_server {
    rcp_mutex_t          mu; /* protects endpoints[], subscribers[], counters[] */
    /* [c-RCP-17] Fixed-capacity embedded arrays, not heap-allocated: see
     * RCP_ADMIN_MAX_ENDPOINTS/RCP_ADMIN_MAX_SUBSCRIBERS/RCP_ADMIN_MAX_
     * COUNTERS's own doc comment (admin.h) for the rationale and the
     * resulting capacity-exceeded failure modes. */
    rcp_avtp_addr_t      endpoints[RCP_ADMIN_MAX_ENDPOINTS];
    size_t               endpoints_len;
    admin_subscriber_t   subscribers[RCP_ADMIN_MAX_SUBSCRIBERS];
    size_t               subscribers_len;
    admin_counter_t      counters[RCP_ADMIN_MAX_COUNTERS];
    size_t               counters_len;
    /* rcp_admin_server_metrics_text()'s own rendering scratch space --
     * embedded here (this whole struct is already srv's one, single,
     * already-existing rcp_calloc() at construction time) rather than a
     * per-call heap allocation, sized to ADMIN_METRICS_TEXT_MAX's own
     * worst case (RCP_ADMIN_MAX_COUNTERS lines at ADMIN_METRICS_LINE_MAX
     * bytes each) so it can never overflow regardless of what counters[]
     * holds. */
    char                 metrics_scratch[ADMIN_METRICS_TEXT_MAX];
};

//cfusa:req REQ-ADMIN-009
//cfusa:req REQ-ADMIN-011
rcp_admin_server_t *rcp_admin_server_new(void)
{
    rcp_admin_server_t *srv = (rcp_admin_server_t *)rcp_calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    rcp_mutex_init(&srv->mu);
    return srv;
}

//cfusa:req REQ-ADMIN-010
//cfusa:req REQ-ADMIN-012
void rcp_admin_server_destroy(rcp_admin_server_t *srv)
{
    if (!srv) return;
    rcp_mutex_destroy(&srv->mu);
    rcp_free(srv);
    srv = NULL;
}

static size_t find_endpoint_index(rcp_admin_server_t *srv, rcp_avtp_addr_t addr)
{
    size_t i;
    for (i = 0; i < srv->endpoints_len; i++) {
        if (rcp_avtp_addr_equal(srv->endpoints[i], addr)) return i;
    }
    return srv->endpoints_len; /* not found */
}

//cfusa:req REQ-ADMIN-002
//cfusa:req REQ-ADMIN-008
bool rcp_admin_server_register_endpoint(rcp_admin_server_t *srv, rcp_avtp_addr_t addr)
{
    bool changed = false;

    rcp_mutex_lock(&srv->mu);
    if (find_endpoint_index(srv, addr) == srv->endpoints_len) {
        if (srv->endpoints_len < RCP_ADMIN_MAX_ENDPOINTS) {
            srv->endpoints[srv->endpoints_len++] = addr;
            changed = true;
        }
    }
    rcp_mutex_unlock(&srv->mu);
    return changed;
}

//cfusa:req REQ-ADMIN-002
bool rcp_admin_server_deregister_endpoint(rcp_admin_server_t *srv, rcp_avtp_addr_t addr)
{
    bool changed = false;
    size_t idx;

    rcp_mutex_lock(&srv->mu);
    idx = find_endpoint_index(srv, addr);
    if (idx < srv->endpoints_len) {
        srv->endpoints[idx] = srv->endpoints[srv->endpoints_len - 1];
        srv->endpoints_len--;
        changed = true;
    }
    rcp_mutex_unlock(&srv->mu);
    return changed;
}

//cfusa:req REQ-ADMIN-001
size_t rcp_admin_server_endpoints(rcp_admin_server_t *srv, rcp_endpoint_info_t *out, size_t cap)
{
    size_t n;
    size_t i;

    rcp_mutex_lock(&srv->mu);
    n = srv->endpoints_len;
    for (i = 0; i < n && i < cap; i++) {
        out[i].addr       = srv->endpoints[i];
        out[i].registered = true;
        out[i].extra[0]   = '\0';
    }
    rcp_mutex_unlock(&srv->mu);
    return n;
}

//cfusa:req REQ-ADMIN-003
bool rcp_admin_server_subscribe(rcp_admin_server_t *srv, rcp_admin_event_fn cb, void *user_data)
{
    bool ok;

    rcp_mutex_lock(&srv->mu);
    ok = srv->subscribers_len < RCP_ADMIN_MAX_SUBSCRIBERS;
    if (ok) {
        srv->subscribers[srv->subscribers_len].cb        = cb;
        srv->subscribers[srv->subscribers_len].user_data  = user_data;
        srv->subscribers_len++;
    }
    rcp_mutex_unlock(&srv->mu);
    return ok;
}

//cfusa:req REQ-ADMIN-004
void rcp_admin_server_emit(rcp_admin_server_t *srv, rcp_admin_event_t ev)
{
    /* [c-RCP-17] A fixed-size stack snapshot, not a per-call heap
     * allocation -- subscribers[] is itself now capped at
     * RCP_ADMIN_MAX_SUBSCRIBERS (admin.h), small enough that copying the
     * whole thing onto the stack costs nothing and can never fail the way
     * the old rcp_malloc()'d snapshot could. */
    admin_subscriber_t local[RCP_ADMIN_MAX_SUBSCRIBERS];
    size_t              n;
    size_t              i;

    rcp_mutex_lock(&srv->mu);
    n = srv->subscribers_len;
    for (i = 0; i < n; i++) local[i] = srv->subscribers[i];
    rcp_mutex_unlock(&srv->mu);

    /* Invoked outside the lock -- see the deviation note in admin.h. */
    for (i = 0; i < n; i++) {
        local[i].cb(&ev, local[i].user_data);
    }
}

//cfusa:req REQ-ADMIN-005
//cfusa:req REQ-ADMIN-007
bool rcp_admin_server_record_counter(rcp_admin_server_t *srv, const char *name, const char *labels, double delta)
{
    bool ok;
    size_t i;

    rcp_mutex_lock(&srv->mu);
    for (i = 0; i < srv->counters_len; i++) {
        if (strcmp(srv->counters[i].name, name) == 0 && strcmp(srv->counters[i].labels, labels) == 0) {
            srv->counters[i].value += delta;
            rcp_mutex_unlock(&srv->mu);
            return true;
        }
    }

    ok = srv->counters_len < RCP_ADMIN_MAX_COUNTERS;
    if (ok) {
        admin_counter_t *c = &srv->counters[srv->counters_len];
        rcp_strncpy_bounded(c->name, sizeof(c->name), name);
        rcp_strncpy_bounded(c->labels, sizeof(c->labels), labels);
        c->value = delta;
        srv->counters_len++;
    }
    rcp_mutex_unlock(&srv->mu);
    return ok;
}

//cfusa:req REQ-ADMIN-006
size_t rcp_admin_server_metrics_text(rcp_admin_server_t *srv, char *out, size_t cap)
{
    size_t total;
    size_t i;
    size_t scratch_len = 0;

    rcp_mutex_lock(&srv->mu);
    for (i = 0; i < srv->counters_len; i++) {
        char line[256];
        int n;

        if (srv->counters[i].labels[0] != '\0') {
            n = snprintf(line, sizeof(line), "# TYPE %s counter\n%s{%s} %g\n",
                         srv->counters[i].name, srv->counters[i].name, srv->counters[i].labels,
                         srv->counters[i].value);
        } else {
            n = snprintf(line, sizeof(line), "# TYPE %s counter\n%s %g\n",
                         srv->counters[i].name, srv->counters[i].name, srv->counters[i].value);
        }
        if (n < 0) continue;
        /* snprintf returns the length that WOULD have been written; clamp
           to what's actually present in line[] before copying out of it,
           or a long name/labels pair reads past the stack buffer. */
        size_t written = ((size_t)n >= sizeof(line)) ? sizeof(line) - 1 : (size_t)n;

        /* [c-RCP-17] srv->metrics_scratch is a fixed ADMIN_METRICS_TEXT_MAX-
         * byte embedded buffer, sized to the true worst case (counters_len
         * is capped at RCP_ADMIN_MAX_COUNTERS, and `written` is capped at
         * ADMIN_METRICS_LINE_MAX above) -- this bound check can never
         * actually trip given those two invariants; kept as defense in
         * depth rather than assumed. */
        if (scratch_len + written + 1 > sizeof(srv->metrics_scratch)) continue;
        rcp_memcpy_bounded(srv->metrics_scratch + scratch_len,
                            sizeof(srv->metrics_scratch) - scratch_len, line, written);
        scratch_len += written;
        srv->metrics_scratch[scratch_len] = '\0';
    }

    total = scratch_len;
    if (out && cap > 0) {
        size_t to_copy = total < cap - 1 ? total : cap - 1;
        rcp_memcpy_bounded(out, cap - 1, srv->metrics_scratch, to_copy);
        out[to_copy] = '\0';
    }
    rcp_mutex_unlock(&srv->mu);

    return total;
}
