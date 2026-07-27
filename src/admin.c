#include "rcp/admin.h"

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

struct rcp_admin_server {
    rcp_registry_t       *reg; /* borrowed, not retained */
    rcp_mutex_t             mu; /* protects subscribers[], counters[] */
    admin_subscriber_t    *subscribers;
    size_t                  subscribers_len;
    size_t                  subscribers_cap;
    admin_counter_t       *counters;
    size_t                  counters_len;
    size_t                  counters_cap;
};

rcp_admin_server_t *rcp_admin_server_new(rcp_registry_t *reg)
{
    rcp_admin_server_t *srv = (rcp_admin_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->reg = reg;
    rcp_mutex_init(&srv->mu);
    return srv;
}

void rcp_admin_server_destroy(rcp_admin_server_t *srv)
{
    if (!srv) return;
    rcp_mutex_destroy(&srv->mu);
    free(srv->subscribers);
    free(srv->counters);
    free(srv);
}

//cfusa:req REQ-ADMIN-001
size_t rcp_admin_server_zones(rcp_admin_server_t *srv, rcp_zone_info_t *out, size_t cap)
{
    size_t              n = rcp_registry_controllers(srv->reg, NULL, 0);
    rcp_controller_t **ctrls;
    size_t              i;

    if (n == 0) return 0;

    ctrls = (rcp_controller_t **)malloc(n * sizeof(*ctrls));
    if (!ctrls) return 0; /* best-effort on OOM; no dedicated sentinel */

    n = rcp_registry_controllers(srv->reg, ctrls, n);
    for (i = 0; i < n; i++) {
        if (i < cap) {
            out[i].zone       = rcp_controller_zone(ctrls[i]);
            out[i].registered = true;
            out[i].extra[0]   = '\0';
        }
        rcp_controller_release(ctrls[i]);
    }
    free(ctrls);
    return n;
}

//cfusa:req REQ-ADMIN-002
bool rcp_admin_server_subscribe(rcp_admin_server_t *srv, rcp_admin_event_fn cb, void *user_data)
{
    bool ok = true;

    rcp_mutex_lock(&srv->mu);
    if (srv->subscribers_len == srv->subscribers_cap) {
        size_t new_cap = (srv->subscribers_cap == 0) ? 4 : srv->subscribers_cap * 2;
        admin_subscriber_t *grown = (admin_subscriber_t *)realloc(srv->subscribers, new_cap * sizeof(*grown));
        if (!grown) {
            ok = false;
        } else {
            srv->subscribers     = grown;
            srv->subscribers_cap = new_cap;
        }
    }
    if (ok) {
        srv->subscribers[srv->subscribers_len].cb        = cb;
        srv->subscribers[srv->subscribers_len].user_data  = user_data;
        srv->subscribers_len++;
    }
    rcp_mutex_unlock(&srv->mu);
    return ok;
}

//cfusa:req REQ-ADMIN-003
//cfusa:req REQ-ADMIN-008
void rcp_admin_server_emit(rcp_admin_server_t *srv, rcp_admin_event_t ev)
{
    admin_subscriber_t *local;
    size_t               n;
    size_t               i;

    rcp_mutex_lock(&srv->mu);
    n = srv->subscribers_len;
    local = (admin_subscriber_t *)malloc(n > 0 ? n * sizeof(*local) : 1);
    if (local) {
        for (i = 0; i < n; i++) local[i] = srv->subscribers[i];
    } else {
        n = 0; /* best-effort on OOM: skip delivery rather than crash */
    }
    rcp_mutex_unlock(&srv->mu);

    /* Invoked outside the lock -- see the deviation note in admin.h. */
    for (i = 0; i < n; i++) {
        local[i].cb(&ev, local[i].user_data);
    }
    free(local);
}

//cfusa:req REQ-ADMIN-004
//cfusa:req REQ-ADMIN-005
//cfusa:req REQ-ADMIN-007
bool rcp_admin_server_record_counter(rcp_admin_server_t *srv, const char *name, const char *labels, double delta)
{
    bool ok = true;
    size_t i;

    rcp_mutex_lock(&srv->mu);
    for (i = 0; i < srv->counters_len; i++) {
        if (strcmp(srv->counters[i].name, name) == 0 && strcmp(srv->counters[i].labels, labels) == 0) {
            srv->counters[i].value += delta;
            rcp_mutex_unlock(&srv->mu);
            return true;
        }
    }

    if (srv->counters_len == srv->counters_cap) {
        size_t new_cap = (srv->counters_cap == 0) ? 8 : srv->counters_cap * 2;
        admin_counter_t *grown = (admin_counter_t *)realloc(srv->counters, new_cap * sizeof(*grown));
        if (!grown) {
            ok = false;
        } else {
            srv->counters     = grown;
            srv->counters_cap = new_cap;
        }
    }
    if (ok) {
        admin_counter_t *c = &srv->counters[srv->counters_len];
        strncpy(c->name, name, sizeof(c->name) - 1);
        c->name[sizeof(c->name) - 1] = '\0';
        strncpy(c->labels, labels, sizeof(c->labels) - 1);
        c->labels[sizeof(c->labels) - 1] = '\0';
        c->value = delta;
        srv->counters_len++;
    }
    rcp_mutex_unlock(&srv->mu);
    return ok;
}

//cfusa:req REQ-ADMIN-006
size_t rcp_admin_server_metrics_text(rcp_admin_server_t *srv, char *out, size_t cap)
{
    size_t total = 0;
    size_t i;
    char  *scratch = NULL;
    size_t scratch_len = 0;
    size_t scratch_cap = 0;

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

        if (scratch_len + (size_t)n + 1 > scratch_cap) {
            size_t new_cap = scratch_cap == 0 ? 256 : scratch_cap * 2;
            char *grown;
            while (new_cap < scratch_len + (size_t)n + 1) new_cap *= 2;
            grown = (char *)realloc(scratch, new_cap);
            if (!grown) continue;
            scratch     = grown;
            scratch_cap = new_cap;
        }
        memcpy(scratch + scratch_len, line, (size_t)n);
        scratch_len += (size_t)n;
        scratch[scratch_len] = '\0';
    }
    rcp_mutex_unlock(&srv->mu);

    total = scratch_len;
    if (out && cap > 0) {
        size_t to_copy = total < cap - 1 ? total : cap - 1;
        if (scratch) memcpy(out, scratch, to_copy);
        out[to_copy] = '\0';
    }
    free(scratch);
    return total;
}
