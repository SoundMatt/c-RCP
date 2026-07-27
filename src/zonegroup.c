#include "rcp/zonegroup.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

rcp_zone_group_t rcp_zone_group_empty(void)
{
    rcp_zone_group_t g;
    memset(&g, 0, sizeof(g));
    return g;
}

//cfusa:req REQ-ZG-001
rcp_zone_group_t rcp_zone_group_all(void)
{
    rcp_zone_group_t g = rcp_zone_group_empty();
    g.zones[0] = RCP_ZONE_FRONT_LEFT;
    g.zones[1] = RCP_ZONE_FRONT_RIGHT;
    g.zones[2] = RCP_ZONE_REAR_LEFT;
    g.zones[3] = RCP_ZONE_REAR_RIGHT;
    g.zones[4] = RCP_ZONE_CENTRAL;
    g.len = 5;
    return g;
}

rcp_zone_group_t rcp_zone_group_rear(void)
{
    rcp_zone_group_t g = rcp_zone_group_empty();
    g.zones[0] = RCP_ZONE_REAR_LEFT;
    g.zones[1] = RCP_ZONE_REAR_RIGHT;
    g.len = 2;
    return g;
}

rcp_zone_group_t rcp_zone_group_front(void)
{
    rcp_zone_group_t g = rcp_zone_group_empty();
    g.zones[0] = RCP_ZONE_FRONT_LEFT;
    g.zones[1] = RCP_ZONE_FRONT_RIGHT;
    g.len = 2;
    return g;
}

//cfusa:req REQ-ZG-005
bool rcp_zone_group_add(rcp_zone_group_t *g, rcp_zone_t z)
{
    if (g->len >= RCP_ZONE_GROUP_MAX) return false;
    g->zones[g->len] = z;
    g->len++;
    return true;
}

//cfusa:req REQ-ZG-004
bool rcp_group_response_ok(const rcp_group_response_t *r)
{
    size_t i;
    for (i = 0; i < r->results_len; i++) {
        if (r->results[i].error != RCP_OK || r->results[i].response.status != RCP_RESPONSE_OK) {
            return false;
        }
    }
    return true;
}

size_t rcp_group_response_errors(const rcp_group_response_t *r, rcp_zone_t *out, size_t cap)
{
    size_t i;
    size_t n = 0;

    for (i = 0; i < r->results_len; i++) {
        if (r->results[i].error != RCP_OK) {
            if (n < cap) out[n] = r->results[i].zone;
            n++;
        }
    }
    return n;
}

void rcp_group_response_free(rcp_group_response_t *r)
{
    size_t i;

    if (!r->results) return;
    for (i = 0; i < r->results_len; i++) {
        rcp_response_free(&r->results[i].response);
    }
    free(r->results);
    r->results = NULL;
    r->results_len = 0;
}

typedef struct {
    rcp_registry_t       *reg;
    const rcp_context_t  *ctx;
    const rcp_command_t  *cmd;
    rcp_zone_t             zone;
    rcp_zone_result_t      result;
} zg_worker_args_t;

//cfusa:req REQ-ZG-003
//cfusa:req REQ-ZG-006
static void zg_worker_fn(void *arg)
{
    zg_worker_args_t *w = (zg_worker_args_t *)arg;
    rcp_controller_t *ctrl = NULL;
    rcp_command_t zone_cmd;
    rcp_response_t resp = {0};
    int ec;

    w->result.zone = w->zone;

    ec = rcp_registry_lookup(w->reg, w->zone, &ctrl);
    if (ec != RCP_OK) {
        w->result.error    = ec;
        w->result.response = resp; /* zeroed */
        return;
    }

    zone_cmd      = *w->cmd;
    zone_cmd.zone = w->zone;
    zone_cmd.id   = 0; /* mirrors cpp-RCP: the registry/zone dispatch owns id assignment */

    ec = rcp_controller_send(ctrl, w->ctx, &zone_cmd, &resp);
    rcp_controller_release(ctrl);

    w->result.error    = ec;
    w->result.response = resp;
}

//cfusa:req REQ-ZG-002
rcp_group_response_t rcp_zonegroup_send(rcp_registry_t *reg, const rcp_context_t *ctx,
                                         const rcp_zone_group_t *group, const rcp_command_t *cmd)
{
    rcp_group_response_t out = {0};
    zg_worker_args_t      workers[RCP_ZONE_GROUP_MAX];
    rcp_thread_t           threads[RCP_ZONE_GROUP_MAX];
    bool                   started[RCP_ZONE_GROUP_MAX];
    size_t                 n = group->len;
    size_t                 i;

    if (n == 0) return out;

    out.results = (rcp_zone_result_t *)calloc(n, sizeof(rcp_zone_result_t));
    if (!out.results) return out;

    for (i = 0; i < n; i++) {
        workers[i].reg  = reg;
        workers[i].ctx  = ctx;
        workers[i].cmd  = cmd;
        workers[i].zone = group->zones[i];
        memset(&workers[i].result, 0, sizeof(workers[i].result));

        if (rcp_thread_start(&threads[i], zg_worker_fn, &workers[i]) == 0) {
            started[i] = true;
        } else {
            /* Couldn't spawn a thread for this zone: run it synchronously
             * rather than silently dropping the result. */
            started[i] = false;
            zg_worker_fn(&workers[i]);
        }
    }
    for (i = 0; i < n; i++) {
        if (started[i]) rcp_thread_join(threads[i]);
        out.results[i] = workers[i].result;
    }
    out.results_len = n;
    return out;
}
