/*
 * faultinject.h -- Structured fault injection for validating safety
 * mechanisms, for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 21, "Satellite Package Rework", milestone 80,
 * "Generic decorators, batch 1").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: an ordered list of
 * drop/slow/error/timeout rules, each optionally count-limited, is
 * exactly as useful for exercising TC18 client-side fault handling as it
 * was before -- this module's rule shape (rcp_fi_rule_t) is unchanged.
 * What changes is the interception point it attaches to: the old
 * rcp_faultinject_controller_new() wrapped a whole rcp_controller_t's
 * send() call, a choke point that no longer exists (ROADMAP.md's
 * Protocol Replacement Notice retires rcp_controller_t's vtable along
 * with rcp_zone_t/rcp_command_t/rcp_response_t; Phase 16/19 instead built
 * 13 heterogeneous, independently-typed endpoint modules, each with its
 * own encode/apply function pairs). There is no single generic send()
 * left to wrap, so this module drops the controller wrapper entirely:
 * rcp_faultinject_evaluate() is now the whole interception point --
 * called directly by the caller immediately before it would otherwise
 * drive whichever endpoint-specific encode/send call applies, telling it
 * whether to proceed normally or to synthesize (a drop/error/timeout) or
 * delay (a slow) the outcome itself. This is the same caller-driven
 * shape milestone 79's watchdog.c/deadline.c/powerstate.c already
 * established, and mirrors this module's own pre-rebind fi_pick()
 * helper, now made public instead of hidden behind a vtable.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_FAULTINJECT_H
#define RCP_FAULTINJECT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RCP_FI_PROCEED = 0, /* no active rule fired: caller should perform its request normally */
    RCP_FI_DROP    = 1, /* caller should synthesize a failure without performing the request */
    RCP_FI_SLOW    = 2, /* caller should sleep the returned latency_ms, then perform the request normally */
    RCP_FI_ERROR   = 3, /* caller should synthesize an error result without performing the request */
    RCP_FI_TIMEOUT = 4, /* caller should synthesize a timeout without performing the request */
} rcp_fi_action_t;

typedef struct {
    rcp_fi_action_t type;
    uint64_t         latency_ms; /* used by RCP_FI_SLOW */
    int              count;       /* -1 = forever; >0 = fires N times, then auto-removed */
} rcp_fi_rule_t;

typedef struct rcp_faultinject rcp_faultinject_t;

/* Creates an injector with no active rules (every evaluate() call returns
 * RCP_FI_PROCEED until a rule is added). Returns NULL on allocation
 * failure. */
rcp_faultinject_t *rcp_faultinject_new(void);

/* Appends rule to fi's ordered rule list. Returns false on allocation
 * failure (rule not added). Thread-safe with concurrent
 * rcp_faultinject_evaluate() calls. */
bool rcp_faultinject_add_rule(rcp_faultinject_t *fi, rcp_fi_rule_t rule);

/* Removes every active rule from fi. Thread-safe with concurrent
 * rcp_faultinject_evaluate() calls. */
void rcp_faultinject_clear_rules(rcp_faultinject_t *fi);

/* Caller calls this immediately before issuing any request (any endpoint
 * type/transport -- this module sends no wire traffic and owns no
 * transport itself). Picks the oldest still-active rule in fi's ordered
 * list (FIFO), decrementing (and, once exhausted, removing) a
 * count-limited rule as it fires, and returns the rcp_fi_action_t the
 * caller should take -- RCP_FI_PROCEED if fi currently has no active
 * rules. *out_latency_ms receives the configured latency for an
 * RCP_FI_SLOW result (undefined for every other result); out_latency_ms
 * may be NULL if the caller doesn't need it. */
rcp_fi_action_t rcp_faultinject_evaluate(rcp_faultinject_t *fi, uint64_t *out_latency_ms);

/* Frees every rule fi tracks, then fi itself. Call exactly once. */
void rcp_faultinject_destroy(rcp_faultinject_t *fi);

#ifdef __cplusplus
}
#endif

#endif /* RCP_FAULTINJECT_H */
