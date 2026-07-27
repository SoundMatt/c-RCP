#include "rcp/restbridge.h"

#include <stdlib.h>

rcp_rest_config_t rcp_rest_default_config(void)
{
    rcp_rest_config_t c;
    c.base_url           = NULL;
    c.max_retries         = 3;
    c.request_timeout_ms  = 1000;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} rest_controller_t;

static rcp_zone_t rest_ctrl_zone(rcp_controller_t *self)
{
    return ((rest_controller_t *)self)->zone;
}

//cfusa:req REQ-REST-001
static int rest_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                           const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-REST-003
static int rest_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-REST-004
static int rest_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void rest_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t rest_controller_vtable = {
    rest_ctrl_zone,
    rest_ctrl_send,
    rest_ctrl_subscribe,
    rest_ctrl_close,
    rest_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-REST-002
rcp_controller_t *rcp_rest_controller_new(rcp_zone_t zone, rcp_rest_config_t cfg)
{
    rest_controller_t *c = (rest_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &rest_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
