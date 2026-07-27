#include "rcp/canbr.h"

#include <stdlib.h>

rcp_can_config_t rcp_can_default_config(void)
{
    rcp_can_config_t c;
    c.can_id_base = 0x100;
    c.fd_mode     = false;
    c.timeout_ms  = 100;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} can_controller_t;

static rcp_zone_t can_ctrl_zone(rcp_controller_t *self)
{
    return ((can_controller_t *)self)->zone;
}

//cfusa:req REQ-CAN-001
static int can_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                          const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-CAN-003
static int can_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-CAN-004
static int can_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void can_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t can_controller_vtable = {
    can_ctrl_zone,
    can_ctrl_send,
    can_ctrl_subscribe,
    can_ctrl_close,
    can_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-CAN-002
rcp_controller_t *rcp_can_controller_new(rcp_zone_t zone, rcp_can_config_t cfg)
{
    can_controller_t *c = (can_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &can_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
