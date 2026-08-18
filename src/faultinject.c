/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/faultinject.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include "alloc_overflow.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

struct rcp_faultinject {
    rcp_mutex_t     mu; /* protects rules[] */
    rcp_fi_rule_t  *rules;
    size_t          rules_len;
    size_t          rules_cap;
};

//cfusa:req REQ-FI-011
rcp_faultinject_t *rcp_faultinject_new(void)
{
    rcp_faultinject_t *fi = (rcp_faultinject_t *)rcp_calloc(1, sizeof(*fi));
    if (!fi) return NULL;
    rcp_mutex_init(&fi->mu);
    return fi;
}

//cfusa:req REQ-FI-006
bool rcp_faultinject_add_rule(rcp_faultinject_t *fi, rcp_fi_rule_t rule)
{
    bool ok = true;

    rcp_mutex_lock(&fi->mu);
    if (fi->rules_len == fi->rules_cap) {
        size_t new_cap = (fi->rules_cap == 0) ? 4 : fi->rules_cap * 2;
        size_t alloc_bytes = rcp_alloc_checked_size(new_cap, sizeof(*fi->rules));
        rcp_fi_rule_t *grown = alloc_bytes == 0
            ? NULL
            : (rcp_fi_rule_t *)rcp_realloc(fi->rules, alloc_bytes);
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

//cfusa:req REQ-FI-012
void rcp_faultinject_clear_rules(rcp_faultinject_t *fi)
{
    rcp_mutex_lock(&fi->mu);
    fi->rules_len = 0;
    rcp_mutex_unlock(&fi->mu);
}

/* rcp_fi_rule_t.count is treated as "firings remaining": a >0 count is
 * decremented on each pick, and the rule is removed once it reaches 0. A
 * -1 count is left untouched (fires forever). Deviation from cpp-RCP: its
 * pick_rule() returns a raw Rule* into the vector but erase()s that same
 * element first when a count-based rule has just expired -- the returned
 * pointer (and the caller's subsequent rule->type/rule->latency reads) is
 * a genuine use-after-free. This port copies the rule's value out before
 * deciding whether to remove it from the list, so the caller only ever
 * reads its own local copy. */
static bool fi_pick(rcp_faultinject_t *fi, rcp_fi_rule_t *out)
{
    if (fi->rules_len == 0) return false;

    *out = fi->rules[0];

    if (fi->rules[0].count > 0) {
        fi->rules[0].count--;
        if (fi->rules[0].count == 0) {
            rcp_memmove_bounded(&fi->rules[0], fi->rules_cap * sizeof(*fi->rules), &fi->rules[1], (fi->rules_len - 1) * sizeof(*fi->rules));
            fi->rules_len--;
        }
    }
    return true;
}

//cfusa:req REQ-FI-001
//cfusa:req REQ-FI-002
//cfusa:req REQ-FI-003
//cfusa:req REQ-FI-004
//cfusa:req REQ-FI-005
//cfusa:req REQ-FI-007
//cfusa:req REQ-FI-008
//cfusa:req REQ-FI-009
rcp_fi_action_t rcp_faultinject_evaluate(rcp_faultinject_t *fi, uint64_t *out_latency_ms)
{
    rcp_fi_rule_t rule = {0};
    bool have_rule;

    rcp_mutex_lock(&fi->mu);
    have_rule = fi_pick(fi, &rule);
    rcp_mutex_unlock(&fi->mu);

    if (!have_rule) return RCP_FI_PROCEED;

    if (rule.type == RCP_FI_SLOW && out_latency_ms) *out_latency_ms = rule.latency_ms;
    return rule.type;
}

//cfusa:req REQ-FI-010
void rcp_faultinject_destroy(rcp_faultinject_t *fi)
{
    if (!fi) return;
    rcp_free(fi->rules);
    fi->rules = NULL;
    rcp_mutex_destroy(&fi->mu);
    rcp_free(fi);
    fi = NULL;
}
