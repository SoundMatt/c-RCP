#include "rcp/faultinject.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    rcp_controller_t  base;
    rcp_controller_t *inner; /* retained */
    rcp_mutex_t         mu;  /* protects rules[], closed */
    bool                closed;
    rcp_fi_rule_t      *rules;
    size_t              rules_len;
    size_t              rules_cap;
} faultinject_controller_t;

/* Deviation from cpp-RCP: its pick_rule() returns a raw Rule* into the
 * vector, but erase()s that same element first when a count-based rule has
 * just expired -- the returned pointer (and the caller's subsequent
 * rule->type/rule->latency reads) is a genuine use-after-free. This port
 * copies the rule's value out before deciding whether to remove it from
 * the list, so the caller only ever reads its own local copy.
 *
 * rcp_fi_rule_t.count is treated as "firings remaining": a >0 count is
 * decremented on each pick, and the rule is removed once it reaches 0. A
 * -1 count is left untouched (fires forever). This matches the external
 * contract in faultinject.h (count = "fires N times") because
 * rcp_faultinject_add_rule() stores the caller's N as-is and every pick
 * consumes one. */
static bool fi_pick(faultinject_controller_t *fi, rcp_fi_rule_t *out)
{
    rcp_fi_rule_t picked;

    if (fi->rules_len == 0) return false;

    picked = fi->rules[0]; /* copy out first -- see deviation note above */
    *out = picked;

    if (fi->rules[0].count > 0) {
        fi->rules[0].count--;
        if (fi->rules[0].count == 0) {
            memmove(&fi->rules[0], &fi->rules[1], (fi->rules_len - 1) * sizeof(*fi->rules));
            fi->rules_len--;
        }
    }
    return true;
}

static rcp_zone_t fi_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((faultinject_controller_t *)self)->inner);
}

//cfusa:req REQ-FI-001
//cfusa:req REQ-FI-002
//cfusa:req REQ-FI-003
//cfusa:req REQ-FI-004
//cfusa:req REQ-FI-005
//cfusa:req REQ-FI-007
static int fi_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                         const rcp_command_t *cmd, rcp_response_t *out)
{
    faultinject_controller_t *fi = (faultinject_controller_t *)self;
    bool closed_now;
    rcp_fi_rule_t rule;
    bool have_rule;

    rcp_mutex_lock(&fi->mu);
    closed_now = fi->closed;
    if (!closed_now) have_rule = fi_pick(fi, &rule);
    else have_rule = false;
    rcp_mutex_unlock(&fi->mu);

    if (closed_now) return RCP_ERR_CLOSED;
    if (!have_rule) return rcp_controller_send(fi->inner, ctx, cmd, out);

    switch (rule.type) {
    case RCP_FI_DROP:
        return RCP_ERR_CLOSED; /* injected drop */
    case RCP_FI_SLOW:
        rcp_sleep_ms((unsigned)rule.latency_ms);
        return rcp_controller_send(fi->inner, ctx, cmd, out);
    case RCP_FI_ERROR:
        out->command_id = cmd->id;
        out->zone       = rcp_controller_zone(fi->inner);
        out->status     = RCP_RESPONSE_ERROR;
        return RCP_OK;
    case RCP_FI_TIMEOUT:
        return RCP_ERR_TIMEOUT;
    default:
        return rcp_controller_send(fi->inner, ctx, cmd, out);
    }
}

static int fi_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    faultinject_controller_t *fi = (faultinject_controller_t *)self;
    return rcp_controller_subscribe(fi->inner, ctx, out);
}

static int fi_ctrl_close(rcp_controller_t *self)
{
    faultinject_controller_t *fi = (faultinject_controller_t *)self;
    rcp_mutex_lock(&fi->mu);
    fi->closed = true;
    rcp_mutex_unlock(&fi->mu);
    return rcp_controller_close(fi->inner);
}

static void fi_ctrl_destroy(rcp_controller_t *self)
{
    faultinject_controller_t *fi = (faultinject_controller_t *)self;
    rcp_controller_release(fi->inner);
    free(fi->rules);
    rcp_mutex_destroy(&fi->mu);
    free(fi);
}

static const rcp_controller_vtable_t faultinject_controller_vtable = {
    fi_ctrl_zone,
    fi_ctrl_send,
    fi_ctrl_subscribe,
    fi_ctrl_close,
    fi_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_faultinject_controller_new(rcp_controller_t *inner)
{
    faultinject_controller_t *fi = (faultinject_controller_t *)calloc(1, sizeof(*fi));
    if (!fi) return NULL;
    fi->base.vt       = &faultinject_controller_vtable;
    fi->base.refcount = 1;
    fi->inner         = rcp_controller_retain(inner);
    rcp_mutex_init(&fi->mu);
    return &fi->base;
}

//cfusa:req REQ-FI-006
bool rcp_faultinject_add_rule(rcp_controller_t *ctrl, rcp_fi_rule_t rule)
{
    faultinject_controller_t *fi = (faultinject_controller_t *)ctrl;
    bool ok = true;

    rcp_mutex_lock(&fi->mu);
    if (fi->rules_len == fi->rules_cap) {
        size_t new_cap = (fi->rules_cap == 0) ? 4 : fi->rules_cap * 2;
        rcp_fi_rule_t *grown = (rcp_fi_rule_t *)realloc(fi->rules, new_cap * sizeof(*grown));
        if (!grown) {
            ok = false;
        } else {
            fi->rules     = grown;
            fi->rules_cap = new_cap;
        }
    }
    if (ok) fi->rules[fi->rules_len++] = rule;
    rcp_mutex_unlock(&fi->mu);
    return ok;
}

void rcp_faultinject_clear_rules(rcp_controller_t *ctrl)
{
    faultinject_controller_t *fi = (faultinject_controller_t *)ctrl;
    rcp_mutex_lock(&fi->mu);
    fi->rules_len = 0;
    rcp_mutex_unlock(&fi->mu);
}
