#include "rcp/linbr.h"

#include <stdlib.h>

rcp_lin_config_t rcp_lin_default_config(void)
{
    rcp_lin_config_t c;
    c.frame_id   = 0x10;
    c.timeout_ms = 50;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} lin_controller_t;

static rcp_zone_t lin_ctrl_zone(rcp_controller_t *self)
{
    return ((lin_controller_t *)self)->zone;
}

//cfusa:req REQ-LIN-001
static int lin_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                          const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-LIN-003
static int lin_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-LIN-004
static int lin_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void lin_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t lin_controller_vtable = {
    lin_ctrl_zone,
    lin_ctrl_send,
    lin_ctrl_subscribe,
    lin_ctrl_close,
    lin_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-LIN-002
rcp_controller_t *rcp_lin_controller_new(rcp_zone_t zone, rcp_lin_config_t cfg)
{
    lin_controller_t *c = (lin_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &lin_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
