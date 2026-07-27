#include "rcp/redundancy.h"

#include "platform.h"

#include <stdlib.h>

rcp_redundancy_config_t rcp_redundancy_default_config(void)
{
    rcp_redundancy_config_t c;
    c.auto_promote = true;
    c.max_retries  = 1;
    return c;
}

typedef struct {
    rcp_controller_t         base;
    rcp_controller_t        *primary; /* retained */
    rcp_controller_t        *standby; /* retained */
    rcp_controller_t        *active;  /* borrowed alias of primary or standby */
    rcp_redundancy_config_t   cfg;
    rcp_mutex_t                mu; /* protects active */
} redundancy_controller_t;

static rcp_controller_t *current_active(redundancy_controller_t *rc)
{
    rcp_controller_t *act;
    rcp_mutex_lock(&rc->mu);
    act = rc->active;
    rcp_mutex_unlock(&rc->mu);
    return act;
}

//cfusa:req REQ-RED-008
static rcp_zone_t redundancy_ctrl_zone(rcp_controller_t *self)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)self;
    return rcp_controller_zone(current_active(rc));
}

//cfusa:req REQ-RED-004
//cfusa:req REQ-RED-005
void rcp_redundancy_controller_promote(rcp_controller_t *ctrl)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)ctrl;
    rcp_mutex_lock(&rc->mu);
    rc->active = (rc->active == rc->primary) ? rc->standby : rc->primary;
    rcp_mutex_unlock(&rc->mu);
}

bool rcp_redundancy_controller_is_primary_active(rcp_controller_t *ctrl)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)ctrl;
    bool result;
    rcp_mutex_lock(&rc->mu);
    result = (rc->active == rc->primary);
    rcp_mutex_unlock(&rc->mu);
    return result;
}

//cfusa:req REQ-RED-001
//cfusa:req REQ-RED-002
//cfusa:req REQ-RED-003
//cfusa:req REQ-RED-007
static int redundancy_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                                 const rcp_command_t *cmd, rcp_response_t *out)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)self;
    int ec = rcp_controller_send(current_active(rc), ctx, cmd, out);
    int i;

    if (ec == RCP_OK) return RCP_OK;
    if (!rc->cfg.auto_promote) return ec;

    if (ec == RCP_ERR_CLOSED || ec == RCP_ERR_TIMEOUT) {
        rcp_redundancy_controller_promote(self);
        for (i = 0; i < rc->cfg.max_retries; i++) {
            ec = rcp_controller_send(current_active(rc), ctx, cmd, out);
            if (ec == RCP_OK) return RCP_OK;
        }
    }
    return ec;
}

static int redundancy_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)self;
    return rcp_controller_subscribe(current_active(rc), ctx, out);
}

//cfusa:req REQ-RED-006
static int redundancy_ctrl_close(rcp_controller_t *self)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)self;
    int ec1 = rcp_controller_close(rc->primary);
    int ec2 = rcp_controller_close(rc->standby);
    return (ec1 != RCP_OK) ? ec1 : ec2;
}

static void redundancy_ctrl_destroy(rcp_controller_t *self)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)self;
    rcp_controller_release(rc->primary);
    rcp_controller_release(rc->standby);
    rcp_mutex_destroy(&rc->mu);
    free(rc);
}

static const rcp_controller_vtable_t redundancy_controller_vtable = {
    redundancy_ctrl_zone,
    redundancy_ctrl_send,
    redundancy_ctrl_subscribe,
    redundancy_ctrl_close,
    redundancy_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_redundancy_controller_new(rcp_controller_t *primary, rcp_controller_t *standby,
                                                 rcp_redundancy_config_t cfg)
{
    redundancy_controller_t *rc = (redundancy_controller_t *)calloc(1, sizeof(*rc));
    if (!rc) return NULL;
    rc->base.vt       = &redundancy_controller_vtable;
    rc->base.refcount = 1;
    rc->primary       = rcp_controller_retain(primary);
    rc->standby       = rcp_controller_retain(standby);
    rc->active        = rc->primary;
    rc->cfg           = cfg;
    rcp_mutex_init(&rc->mu);
    return &rc->base;
}
