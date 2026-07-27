#include "rcp/udsbr.h"

#include <stdlib.h>

rcp_uds_config_t rcp_uds_default_config(void)
{
    rcp_uds_config_t c;
    c.routine_id       = 0x0100;
    c.p2_timeout_ms    = 50;
    c.p2ext_timeout_ms = 5000;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} uds_controller_t;

static rcp_zone_t uds_ctrl_zone(rcp_controller_t *self)
{
    return ((uds_controller_t *)self)->zone;
}

//cfusa:req REQ-UDS-001
static int uds_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                          const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-UDS-003
static int uds_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-UDS-004
static int uds_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void uds_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t uds_controller_vtable = {
    uds_ctrl_zone,
    uds_ctrl_send,
    uds_ctrl_subscribe,
    uds_ctrl_close,
    uds_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-UDS-002
rcp_controller_t *rcp_uds_controller_new(rcp_zone_t zone, rcp_uds_config_t cfg)
{
    uds_controller_t *c = (uds_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &uds_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
