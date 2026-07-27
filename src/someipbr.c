#include "rcp/someipbr.h"

#include <stdlib.h>

rcp_someip_config_t rcp_someip_default_config(void)
{
    rcp_someip_config_t c;
    c.service_id  = 0x0100;
    c.instance_id = 0x0001;
    c.method_id   = 0x0001;
    c.timeout_ms  = 500;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} someip_controller_t;

static rcp_zone_t someip_ctrl_zone(rcp_controller_t *self)
{
    return ((someip_controller_t *)self)->zone;
}

//cfusa:req REQ-SOMEIP-001
static int someip_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                             const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-SOMEIP-003
static int someip_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-SOMEIP-004
static int someip_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void someip_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t someip_controller_vtable = {
    someip_ctrl_zone,
    someip_ctrl_send,
    someip_ctrl_subscribe,
    someip_ctrl_close,
    someip_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-SOMEIP-002
rcp_controller_t *rcp_someip_controller_new(rcp_zone_t zone, rcp_someip_config_t cfg)
{
    someip_controller_t *c = (someip_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &someip_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
