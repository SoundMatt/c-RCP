/*
 * Hot-standby controller failover for ASIL-B fault tolerance.
 *
 * rcp_redundancy_controller_new() holds a primary and a standby
 * rcp_controller_t for the same zone. All sends go to the active
 * controller (primary by default); on RCP_ERR_CLOSED or RCP_ERR_TIMEOUT
 * the controller promotes the standby automatically and retries.
 */
#ifndef RCP_REDUNDANCY_H
#define RCP_REDUNDANCY_H

#include "rcp/rcp.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool auto_promote; /* promote standby on primary failure without operator confirmation (default: true) */
    int  max_retries;  /* number of retries on the standby before giving up (default: 1) */
} rcp_redundancy_config_t;

/* { auto_promote = true, max_retries = 1 }. */
rcp_redundancy_config_t rcp_redundancy_default_config(void);

/* Wraps primary and standby (retains both). Both must serve the same
 * zone. Returned with refcount 1; release with rcp_controller_release(),
 * which also releases this wrapper's references to primary and standby. */
rcp_controller_t *rcp_redundancy_controller_new(rcp_controller_t *primary, rcp_controller_t *standby,
                                                 rcp_redundancy_config_t cfg);

/* Manually promotes the standby to active, or demotes it back to primary
 * if the standby is already active (a second call toggles back). ctrl
 * must have been created by rcp_redundancy_controller_new(). */
void rcp_redundancy_controller_promote(rcp_controller_t *ctrl);

/* Returns true iff the primary controller is currently active. ctrl must
 * have been created by rcp_redundancy_controller_new(). */
bool rcp_redundancy_controller_is_primary_active(rcp_controller_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REDUNDANCY_H */
