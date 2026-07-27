#include "rcp/ddsbr.h"

#include <stdlib.h>

rcp_dds_config_t rcp_dds_default_config(void)
{
    rcp_dds_config_t c;
    c.topic_prefix = "rcp";
    c.domain_id    = 0;
    c.timeout_ms   = 500;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} dds_controller_t;

static rcp_zone_t dds_ctrl_zone(rcp_controller_t *self)
{
    return ((dds_controller_t *)self)->zone;
}

//cfusa:req REQ-DDS-001
static int dds_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                          const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-DDS-003
static int dds_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-DDS-004
static int dds_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void dds_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t dds_controller_vtable = {
    dds_ctrl_zone,
    dds_ctrl_send,
    dds_ctrl_subscribe,
    dds_ctrl_close,
    dds_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-DDS-002
rcp_controller_t *rcp_dds_controller_new(rcp_zone_t zone, rcp_dds_config_t cfg)
{
    dds_controller_t *c = (dds_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &dds_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
