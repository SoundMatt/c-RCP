#include "rcp/doipbr.h"

#include <stdlib.h>

rcp_doip_config_t rcp_doip_default_config(void)
{
    rcp_doip_config_t c;
    c.server_ip      = NULL;
    c.server_port    = 13400;
    c.logical_addr   = 0x0001;
    c.tcp_timeout_ms = 2000;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} doip_controller_t;

static rcp_zone_t doip_ctrl_zone(rcp_controller_t *self)
{
    return ((doip_controller_t *)self)->zone;
}

//cfusa:req REQ-DOIP-001
static int doip_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                           const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-DOIP-003
static int doip_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-DOIP-004
static int doip_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void doip_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t doip_controller_vtable = {
    doip_ctrl_zone,
    doip_ctrl_send,
    doip_ctrl_subscribe,
    doip_ctrl_close,
    doip_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-DOIP-002
rcp_controller_t *rcp_doip_controller_new(rcp_zone_t zone, rcp_doip_config_t cfg)
{
    doip_controller_t *c = (doip_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &doip_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
