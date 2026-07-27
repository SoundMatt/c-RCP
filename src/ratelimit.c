#include "rcp/ratelimit.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>

rcp_ratelimit_config_t rcp_ratelimit_default_config(void)
{
    rcp_ratelimit_config_t c;
    c.rate            = 100.0;
    c.burst           = 20;
    c.exempt_critical = true;
    return c;
}

typedef struct {
    rcp_controller_t        base;
    rcp_controller_t       *inner; /* retained */
    rcp_ratelimit_config_t   cfg;
    rcp_mutex_t               mu; /* protects tokens, last_ms, closed */
    double                    tokens;
    uint64_t                  last_ms;
    bool                      closed;
} ratelimit_controller_t;

static rcp_zone_t rl_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((ratelimit_controller_t *)self)->inner);
}

//cfusa:req REQ-RL-001
//cfusa:req REQ-RL-002
static bool take_token(ratelimit_controller_t *rl)
{
    uint64_t now;
    double secs;
    bool ok;

    rcp_mutex_lock(&rl->mu);
    now      = rcp_monotonic_ms();
    secs     = (double)(now - rl->last_ms) / 1000.0;
    rl->last_ms = now;
    rl->tokens += secs * rl->cfg.rate;
    if (rl->tokens > (double)rl->cfg.burst) rl->tokens = (double)rl->cfg.burst;

    if (rl->tokens < 1.0) {
        ok = false;
    } else {
        rl->tokens -= 1.0;
        ok = true;
    }
    rcp_mutex_unlock(&rl->mu);
    return ok;
}

//cfusa:req REQ-RL-003
//cfusa:req REQ-RL-004
//cfusa:req REQ-RL-005
//cfusa:req REQ-RL-008
static int rl_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                         const rcp_command_t *cmd, rcp_response_t *out)
{
    ratelimit_controller_t *rl = (ratelimit_controller_t *)self;
    bool closed_now;
    bool exempt;

    rcp_mutex_lock(&rl->mu);
    closed_now = rl->closed;
    rcp_mutex_unlock(&rl->mu);
    if (closed_now) return RCP_ERR_CLOSED;

    exempt = rl->cfg.exempt_critical && (cmd->priority == RCP_PRIORITY_CRITICAL);
    if (!exempt && !take_token(rl)) return RCP_ERR_BUSY;

    return rcp_controller_send(rl->inner, ctx, cmd, out);
}

static int rl_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    ratelimit_controller_t *rl = (ratelimit_controller_t *)self;
    return rcp_controller_subscribe(rl->inner, ctx, out);
}

//cfusa:req REQ-RL-007
static int rl_ctrl_close(rcp_controller_t *self)
{
    ratelimit_controller_t *rl = (ratelimit_controller_t *)self;
    rcp_mutex_lock(&rl->mu);
    rl->closed = true;
    rcp_mutex_unlock(&rl->mu);
    return rcp_controller_close(rl->inner);
}

static void rl_ctrl_destroy(rcp_controller_t *self)
{
    ratelimit_controller_t *rl = (ratelimit_controller_t *)self;
    rcp_controller_release(rl->inner);
    rcp_mutex_destroy(&rl->mu);
    free(rl);
}

static const rcp_controller_vtable_t ratelimit_controller_vtable = {
    rl_ctrl_zone,
    rl_ctrl_send,
    rl_ctrl_subscribe,
    rl_ctrl_close,
    rl_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_ratelimit_controller_new(rcp_controller_t *inner, rcp_ratelimit_config_t cfg)
{
    ratelimit_controller_t *rl = (ratelimit_controller_t *)calloc(1, sizeof(*rl));
    if (!rl) return NULL;
    rl->base.vt       = &ratelimit_controller_vtable;
    rl->base.refcount = 1;
    rl->inner         = rcp_controller_retain(inner);
    rl->cfg           = cfg;
    rl->tokens        = (double)cfg.burst;
    rl->last_ms       = rcp_monotonic_ms();
    rcp_mutex_init(&rl->mu);
    return &rl->base;
}
