/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ratelimit.h"
#include "rcp/alloc.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>

//cfusa:req REQ-RL-010
rcp_ratelimit_config_t rcp_ratelimit_default_config(void)
{
    rcp_ratelimit_config_t c;
    c.rate          = 100.0;
    c.burst         = 20;
    c.exempt_safety = true;
    return c;
}

typedef struct {
    rcp_avtp_addr_t addr;
    double           tokens;
    uint64_t         last_ms;
} bucket_t;

struct rcp_ratelimit_limiter {
    rcp_ratelimit_config_t cfg;
    rcp_mutex_t              mu; /* protects buckets[] */
    bucket_t                *buckets;
    size_t                    n_buckets;
    size_t                    cap_buckets;
};

//cfusa:req REQ-RL-011
rcp_ratelimit_limiter_t *rcp_ratelimit_limiter_new(rcp_ratelimit_config_t cfg)
{
    rcp_ratelimit_limiter_t *rl = (rcp_ratelimit_limiter_t *)rcp_calloc(1, sizeof(*rl));
    if (!rl) return NULL;
    rl->cfg = cfg;
    rcp_mutex_init(&rl->mu);
    return rl;
}

static bucket_t *find_or_create_bucket(rcp_ratelimit_limiter_t *rl, rcp_avtp_addr_t addr, uint64_t now_ms)
{
    size_t i;

    for (i = 0; i < rl->n_buckets; i++) {
        if (rcp_avtp_addr_equal(rl->buckets[i].addr, addr)) return &rl->buckets[i];
    }

    if (rl->n_buckets == rl->cap_buckets) {
        size_t new_cap = (rl->cap_buckets == 0) ? 4 : rl->cap_buckets * 2;
        bucket_t *grown = (bucket_t *)rcp_realloc(rl->buckets, new_cap * sizeof(*grown));
        if (!grown) return NULL;
        rl->buckets     = grown;
        rl->cap_buckets = new_cap;
    }

    rl->buckets[rl->n_buckets].addr    = addr;
    rl->buckets[rl->n_buckets].tokens  = (double)rl->cfg.burst;
    rl->buckets[rl->n_buckets].last_ms = now_ms;
    return &rl->buckets[rl->n_buckets++];
}

//cfusa:req REQ-RL-001
//cfusa:req REQ-RL-002
//cfusa:req REQ-RL-006
static bool take_token(rcp_ratelimit_limiter_t *rl, bucket_t *b, uint64_t now_ms)
{
    double secs = (double)(now_ms - b->last_ms) / 1000.0;
    b->last_ms = now_ms;
    b->tokens += secs * rl->cfg.rate;
    if (b->tokens > (double)rl->cfg.burst) b->tokens = (double)rl->cfg.burst;

    if (b->tokens < 1.0) return false;
    b->tokens -= 1.0;
    return true;
}

//cfusa:req REQ-RL-003
//cfusa:req REQ-RL-004
//cfusa:req REQ-RL-005
//cfusa:req REQ-RL-007
//cfusa:req REQ-RL-008
bool rcp_ratelimit_limiter_allow(rcp_ratelimit_limiter_t *rl, rcp_avtp_addr_t addr, uint8_t request_type)
{
    uint64_t now_ms = rcp_monotonic_ms();
    bucket_t *b;
    bool ok;

    if (rl->cfg.exempt_safety && rcp_e2e_is_safety_request(request_type)) return true;

    rcp_mutex_lock(&rl->mu);
    b = find_or_create_bucket(rl, addr, now_ms);
    ok = b ? take_token(rl, b, now_ms) : false;
    rcp_mutex_unlock(&rl->mu);

    return ok;
}

//cfusa:req REQ-RL-009
void rcp_ratelimit_limiter_destroy(rcp_ratelimit_limiter_t *rl)
{
    if (!rl) return;
    rcp_mutex_destroy(&rl->mu);
    rcp_free(rl->buckets);
    rcp_free(rl);
}
