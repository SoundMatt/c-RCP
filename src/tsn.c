/* SO_PRIORITY is a glibc extension, not exposed by <sys/socket.h> under
 * strict -std=c99 unless _DEFAULT_SOURCE is defined first — must be the
 * literal first thing in the translation unit, before any include (even
 * "rcp/tsn.h", which pulls in <stdint.h> and could lock in feature-test
 * macros via glibc's <features.h> before we get a chance to set this). */
#define _DEFAULT_SOURCE

#include "rcp/tsn.h"

#include <stdlib.h>

#if defined(__linux__)
#  include <sys/socket.h>
#  define RCP_TSN_SO_PRIORITY 1
#endif

//cfusa:req REQ-TSN-002
rcp_tsn_pcp_map_t rcp_tsn_default_pcp_map(void)
{
    rcp_tsn_pcp_map_t m;
    m.normal   = 2;
    m.high     = 5;
    m.critical = 7;
    return m;
}

//cfusa:req REQ-TSN-003
//cfusa:req REQ-TSN-004
//cfusa:req REQ-TSN-005
uint8_t rcp_tsn_pcp_for(const rcp_tsn_pcp_map_t *m, rcp_priority_t p)
{
    switch (p) {
    case RCP_PRIORITY_HIGH:     return m->high;
    case RCP_PRIORITY_CRITICAL: return m->critical;
    case RCP_PRIORITY_NORMAL:
    default:                    return m->normal;
    }
}

rcp_tsn_config_t rcp_tsn_default_config(void)
{
    rcp_tsn_config_t c;
    c.pcp_map  = rcp_tsn_default_pcp_map();
    c.vlan_id  = 0;
    c.cycle_ns = 0;
    return c;
}

/* ── Controller wrapper ────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t  base;
    rcp_controller_t *inner; /* retained */
    int               fd;
    rcp_tsn_config_t  cfg;
} tsn_controller_t;

static rcp_zone_t tsn_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((tsn_controller_t *)self)->inner);
}

//cfusa:req REQ-TSN-001
static int tsn_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                          const rcp_command_t *cmd, rcp_response_t *out)
{
    tsn_controller_t *tc = (tsn_controller_t *)self;

#if defined(RCP_TSN_SO_PRIORITY)
    if (tc->fd >= 0) {
        int pcp = (int)rcp_tsn_pcp_for(&tc->cfg.pcp_map, cmd->priority);
        (void)setsockopt(tc->fd, SOL_SOCKET, SO_PRIORITY, &pcp, sizeof(pcp));
    }
#endif

    return rcp_controller_send(tc->inner, ctx, cmd, out);
}

//cfusa:req REQ-TSN-006
static int tsn_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    tsn_controller_t *tc = (tsn_controller_t *)self;
    return rcp_controller_subscribe(tc->inner, ctx, out);
}

static int tsn_ctrl_close(rcp_controller_t *self)
{
    tsn_controller_t *tc = (tsn_controller_t *)self;
    return rcp_controller_close(tc->inner);
}

static void tsn_ctrl_destroy(rcp_controller_t *self)
{
    tsn_controller_t *tc = (tsn_controller_t *)self;
    rcp_controller_release(tc->inner);
    free(tc);
}

static const rcp_controller_vtable_t tsn_controller_vtable = {
    tsn_ctrl_zone,
    tsn_ctrl_send,
    tsn_ctrl_subscribe,
    tsn_ctrl_close,
    tsn_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_tsn_controller_new(rcp_controller_t *inner, int socket_fd,
                                          rcp_tsn_config_t cfg)
{
    tsn_controller_t *tc = (tsn_controller_t *)calloc(1, sizeof(*tc));
    if (!tc) return NULL;
    tc->base.vt       = &tsn_controller_vtable;
    tc->base.refcount = 1;
    tc->inner         = rcp_controller_retain(inner);
    tc->fd            = socket_fd;
    tc->cfg           = cfg;
    return &tc->base;
}
