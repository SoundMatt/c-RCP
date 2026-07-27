/*
 * Structured fault injection for validating safety mechanisms (v0.11.0-v0.16.0).
 *
 * Wraps any rcp_controller_t and intercepts send() calls according to an
 * ordered list of rules. Rules may drop responses, add latency, return
 * errors, or return timeouts. Count-based rules auto-expire after N
 * applications.
 */
#ifndef RCP_FAULTINJECT_H
#define RCP_FAULTINJECT_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RCP_FI_DROP    = 1, /* return an error without calling inner send() */
    RCP_FI_SLOW    = 2, /* sleep rule.latency_ms, then call inner send() */
    RCP_FI_ERROR   = 3, /* return RCP_RESPONSE_ERROR without calling inner */
    RCP_FI_TIMEOUT = 4, /* return RCP_ERR_TIMEOUT without calling inner */
} rcp_fi_fault_type_t;

typedef struct {
    rcp_fi_fault_type_t type;
    uint64_t             latency_ms; /* used by RCP_FI_SLOW */
    int                  count;       /* -1 = forever; >0 = fires N times, then auto-removed */
} rcp_fi_rule_t;

/* Wraps inner (retains it). Returned with refcount 1; release with
 * rcp_controller_release(), which also releases this wrapper's reference
 * to inner. */
rcp_controller_t *rcp_faultinject_controller_new(rcp_controller_t *inner);

/* Appends rule to ctrl's ordered rule list. ctrl must have been created by
 * rcp_faultinject_controller_new(). Returns false on allocation failure
 * (rule not added). */
bool rcp_faultinject_add_rule(rcp_controller_t *ctrl, rcp_fi_rule_t rule);

/* Removes every active rule from ctrl. ctrl must have been created by
 * rcp_faultinject_controller_new(). */
void rcp_faultinject_clear_rules(rcp_controller_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* RCP_FAULTINJECT_H */
