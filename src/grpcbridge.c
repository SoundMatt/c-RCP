#include "rcp/grpcbridge.h"

#include <stdlib.h>

rcp_grpc_config_t rcp_grpc_default_config(void)
{
    rcp_grpc_config_t c;
    c.server_address = NULL;
    c.max_retries     = 3;
    c.rpc_timeout_ms  = 1000;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} grpc_controller_t;

static rcp_zone_t grpc_ctrl_zone(rcp_controller_t *self)
{
    return ((grpc_controller_t *)self)->zone;
}

//cfusa:req REQ-GRPC-001
static int grpc_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                           const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-GRPC-003
static int grpc_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-GRPC-004
static int grpc_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void grpc_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t grpc_controller_vtable = {
    grpc_ctrl_zone,
    grpc_ctrl_send,
    grpc_ctrl_subscribe,
    grpc_ctrl_close,
    grpc_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-GRPC-002
rcp_controller_t *rcp_grpc_controller_new(rcp_zone_t zone, rcp_grpc_config_t cfg)
{
    grpc_controller_t *c = (grpc_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &grpc_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
