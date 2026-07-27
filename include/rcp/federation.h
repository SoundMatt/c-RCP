/*
 * Multi-HPC federation: cross-HPC zone forwarding with lease-based ownership.
 *
 * A rcp_registry_t created by rcp_federation_registry_new() transparently
 * forwards lookup() to a remote HPC's controller when the zone is not
 * locally owned. Lease ownership is time-bounded; an expired lease causes
 * lookup() to return RCP_ERR_NOT_FOUND until refreshed by the owning HPC.
 *
 * Deviation from cpp-RCP: cpp-RCP's add_lease(Zone, Lease) takes a raw
 * struct whose remote_ctrl field is a std::shared_ptr the caller must
 * populate correctly (sharing ownership implicitly via the copy). This
 * port instead exposes rcp_federation_registry_add_lease() taking the
 * remote controller directly and retaining it internally -- there is no
 * public Lease type with ownership-ambiguous fields for a caller to get
 * wrong.
 */
#ifndef RCP_FEDERATION_H
#define RCP_FEDERATION_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum length (including the NUL terminator) of an HPC id or lease
 * owner string. */
#define RCP_FED_HPC_ID_MAX 64

/* Creates a federated registry identifying this HPC node as local_id
 * (copied, truncated to RCP_FED_HPC_ID_MAX-1 bytes). Release with
 * rcp_registry_close() then rcp_registry_destroy(). */
rcp_registry_t *rcp_federation_registry_new(const char *local_id);

/* Returns the local_id passed to rcp_federation_registry_new(), valid for
 * reg's lifetime. reg must have been created by
 * rcp_federation_registry_new(). */
const char *rcp_federation_registry_local_id(rcp_registry_t *reg);

/* Publishes a remote-HPC lease for zone: reg's lookup() forwards to
 * remote_ctrl (retained) until expires_at_ms (a rcp_monotonic_ms()
 * timestamp) passes, unless a local controller is registered for the same
 * zone, which always takes precedence. Replaces any existing lease for
 * zone. owner is copied (truncated to RCP_FED_HPC_ID_MAX-1 bytes) and is
 * purely informational -- it is not currently exposed by any query
 * function, matching cpp-RCP's own Lease::owner (recorded but never read
 * back). reg must have been created by rcp_federation_registry_new().
 * Returns RCP_ERR_CLOSED if reg is closed. */
int rcp_federation_registry_add_lease(rcp_registry_t *reg, rcp_zone_t zone, const char *owner,
                                       uint64_t expires_at_ms, rcp_controller_t *remote_ctrl);

/* Removes the lease for zone, if any (a no-op, not an error, if none
 * exists) -- matching cpp-RCP's own revoke_lease(), which always returns
 * success. reg must have been created by rcp_federation_registry_new(). */
int rcp_federation_registry_revoke_lease(rcp_registry_t *reg, rcp_zone_t zone);

#ifdef __cplusplus
}
#endif

#endif /* RCP_FEDERATION_H */
