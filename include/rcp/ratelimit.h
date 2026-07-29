/*
 * ratelimit.h -- Per-endpoint token-bucket admission control for the TC18
 * Remote Control Protocol wire layer (ROADMAP.md Phase 21, "Satellite
 * Package Rework", milestone 80, "Generic decorators, batch 1").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: a client-side
 * self-throttling decorator is still exactly the right shape for TC18 --
 * each endpoint's request queue (server.h's rcp_server_endpoint_t) is a
 * finite, un-flow-controlled FIFO with no backpressure signal of its own,
 * so a well-behaved client still wants to cap its own send rate rather
 * than relying on the server to reject overflow. What changes is the key
 * a bucket is kept per: the old design wrapped a whole rcp_controller_t
 * (one shared bucket for every zone that controller happened to address);
 * this one keeps one independent token bucket per (stream_id,
 * byte_bus_id) endpoint address (avtp.h's rcp_avtp_addr_t), lazily
 * created the first time that address is seen -- matching the "an
 * endpoint's own finite capacity" framing this milestone's own roadmap
 * entry uses, rather than one undifferentiated budget across every
 * endpoint a caller happens to talk to.
 *
 * There is no longer a single generic rcp_controller_t::send() choke
 * point to interpose a wrapper on (ROADMAP.md's Protocol Replacement
 * Notice). This module drops the old RateLimitingController vtable
 * wrapper entirely: rcp_ratelimit_limiter_allow() is now the whole
 * interception point, called directly by the caller immediately before
 * it drives whichever endpoint-specific encode/send call applies --
 * the same caller-driven shape milestone 79's watchdog.c/deadline.c/
 * powerstate.c already established.
 *
 * The old exempt_critical flag (RCP_PRIORITY_CRITICAL, a client-assigned
 * rcp_command_t field) has no counterpart in TC18: the Satellite
 * Disposition table's own prioqueue.h entry explains that request
 * execution priority is now a protocol-defined, server-side property of
 * request *kind*, not a client-chosen tag. This module's exemption is
 * re-anchored on the one client-visible request-kind signal that
 * actually survives: e2e.h's rcp_e2e_is_safety_request() MSB(0x80) test
 * on a caller-supplied request_type byte (the same opaque-label
 * convention authz.h uses) -- a safety-tagged request (watchdog kicks,
 * emergency actuations, and any other safety-request-sequence traffic,
 * Phase 18) still bypasses the bucket by default, matching the old
 * module's own stated rationale for exempt_critical.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_RATELIMIT_H
#define RCP_RATELIMIT_H

#include "rcp/avtp.h"
#include "rcp/e2e.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double rate;          /* sustained token refill rate, tokens/second (default: 100.0) */
    int    burst;          /* maximum token accumulation per endpoint (default: 20) */
    bool   exempt_safety;  /* if true, a safety-tagged request_type (e2e.h's
                               rcp_e2e_is_safety_request()) bypasses the
                               bucket (default: true) */
} rcp_ratelimit_config_t;

/* ASIL-B recommended values: { rate = 100.0, burst = 20, exempt_safety = true }. */
rcp_ratelimit_config_t rcp_ratelimit_default_config(void);

typedef struct rcp_ratelimit_limiter rcp_ratelimit_limiter_t;

/* Creates a Limiter with no endpoints registered yet -- see
 * rcp_ratelimit_limiter_allow() for how endpoints join. Returns NULL on
 * allocation failure. */
rcp_ratelimit_limiter_t *rcp_ratelimit_limiter_new(rcp_ratelimit_config_t cfg);

/* Caller calls this immediately before issuing a request_type request to
 * addr (any endpoint type/transport -- this module sends no wire traffic
 * and owns no transport itself). The first call naming a given addr
 * lazily creates that endpoint's own token bucket, seeded full (cfg.burst
 * tokens). Returns true if the request is admitted (either a token was
 * available and has now been consumed, or request_type is safety-tagged
 * and cfg.exempt_safety is set -- which consumes no token). Returns false
 * if the caller should back off; no token is consumed on a false
 * return. */
bool rcp_ratelimit_limiter_allow(rcp_ratelimit_limiter_t *rl, rcp_avtp_addr_t addr, uint8_t request_type);

/* Frees every per-endpoint bucket rl tracks, then rl itself. Call exactly
 * once. */
void rcp_ratelimit_limiter_destroy(rcp_ratelimit_limiter_t *rl);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RATELIMIT_H */
