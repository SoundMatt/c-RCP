/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/lifecycle.h"

//cfusa:req REQ-LIFECYCLE-021
const char *rcp_lifecycle_strerror(rcp_lifecycle_errc_t e)
{
    switch (e) {
    case RCP_LIFECYCLE_OK:                       return "rcp/lifecycle: success";
    case RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT:  return "rcp/lifecycle: HW configuration inconsistent";
    case RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT: return "rcp/lifecycle: RCP configuration inconsistent";
    case RCP_LIFECYCLE_ERR_INVALID_TRANSITION:   return "rcp/lifecycle: invalid lifecycle transition";
    default:                                     return "rcp/lifecycle: unknown error";
    }
}

/* ── Plausibility checks ───────────────────────────────────────────────────── */

//cfusa:req REQ-LIFECYCLE-002
//cfusa:req REQ-LIFECYCLE-003
//cfusa:req REQ-LIFECYCLE-004
rcp_lifecycle_errc_t rcp_lifecycle_check_hw_cfg(const rcp_lifecycle_plausibility_snapshot_t *snap)
{
    size_t i;

    /* Fail-safe: no configuration evidence at all is treated as
     * inconsistent, never as vacuously plausible. */
    if (!snap) return RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT;

    for (i = 0; i < snap->endpoint_count; i++) {
        const rcp_lifecycle_endpoint_plausibility_t *ep = &snap->endpoints[i];

        if (!ep->ep_used) continue;
        if (!ep->hw_pin_mapped)      return RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT;
        if (!ep->has_request_stream) return RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT;
    }

    return RCP_LIFECYCLE_OK;
}

//cfusa:req REQ-LIFECYCLE-005
//cfusa:req REQ-LIFECYCLE-006
//cfusa:req REQ-LIFECYCLE-007
rcp_lifecycle_errc_t rcp_lifecycle_check_rcp_cfg(const rcp_lifecycle_plausibility_snapshot_t *snap)
{
    size_t i;

    if (!snap) return RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT;

    for (i = 0; i < snap->endpoint_count; i++) {
        const rcp_lifecycle_endpoint_plausibility_t *ep = &snap->endpoints[i];

        if (!ep->ep_used) continue;
        if (!ep->has_stream_assoc) return RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT;
    }

    for (i = 0; i < snap->request_stream_count; i++) {
        const rcp_lifecycle_request_stream_plausibility_t *rs = &snap->request_streams[i];

        if (!rs->configured) continue;
        if (!rs->has_response_stream) return RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT;
    }

    return RCP_LIFECYCLE_OK;
}

/* ── Lifecycle transitions ─────────────────────────────────────────────────── */

//cfusa:req REQ-LIFECYCLE-008
//cfusa:req REQ-LIFECYCLE-009
//cfusa:req REQ-LIFECYCLE-010
//cfusa:req REQ-LIFECYCLE-011
//cfusa:req REQ-LIFECYCLE-012
//cfusa:req REQ-LIFECYCLE-013
rcp_lifecycle_errc_t rcp_lifecycle_transition(rcp_lifecycle_state_t *state,
                                               rcp_lifecycle_state_t target,
                                               const rcp_lifecycle_plausibility_snapshot_t *snap)
{
    rcp_lifecycle_state_t from = *state;

    if (target == from) return RCP_LIFECYCLE_OK; /* no-op */

    if (from == RCP_LIFECYCLE_HW_UNCONFIGURED &&
        target == RCP_LIFECYCLE_HW_CONFIGURED) {
        rcp_lifecycle_errc_t rc = rcp_lifecycle_check_hw_cfg(snap);
        if (rc != RCP_LIFECYCLE_OK) return rc;
        *state = target;
        return RCP_LIFECYCLE_OK;
    }

    if (from == RCP_LIFECYCLE_HW_CONFIGURED &&
        target == RCP_LIFECYCLE_RCP_CONFIGURED) {
        rcp_lifecycle_errc_t rc = rcp_lifecycle_check_rcp_cfg(snap);
        if (rc != RCP_LIFECYCLE_OK) return rc;
        *state = target;
        return RCP_LIFECYCLE_OK;
    }

    /* Demotion back to HW_UNCONFIGURED via the discovery-stream/root-client
     * reset path is unconditional -- from either configured state. */
    if (target == RCP_LIFECYCLE_HW_UNCONFIGURED &&
        (from == RCP_LIFECYCLE_HW_CONFIGURED ||
         from == RCP_LIFECYCLE_RCP_CONFIGURED)) {
        *state = target;
        return RCP_LIFECYCLE_OK;
    }

    /* Everything else -- skipping a state on the way up, or downgrading
     * from RCP_CONFIGURED to HW_CONFIGURED directly -- is not a modeled
     * transition. */
    return RCP_LIFECYCLE_ERR_INVALID_TRANSITION;
}

/* ── Per-state request filtering ───────────────────────────────────────────── */

//cfusa:req REQ-LIFECYCLE-014
//cfusa:req REQ-LIFECYCLE-015
//cfusa:req REQ-LIFECYCLE-016
//cfusa:req REQ-LIFECYCLE-017
bool rcp_lifecycle_should_accept(rcp_lifecycle_state_t state,
                                  bool time_sync_supported,
                                  uint8_t avtp_subtype,
                                  uint8_t acf_msg_type,
                                  rcp_byte_bus_id_t byte_bus_id)
{
    if (rcp_avtp_should_drop_tscf(time_sync_supported, avtp_subtype)) return false;

    if (state == RCP_LIFECYCLE_HW_UNCONFIGURED) {
        /* TSCF's presentation-time semantics presuppose a configured
         * request stream, which cannot exist yet during bootstrap --
         * dropped outright regardless of the node's own time-sync
         * capability, unlike the general rule just applied above. */
        if (avtp_subtype == RCP_AVTP_SUBTYPE_TSCF) return false;

        return avtp_subtype == RCP_AVTP_SUBTYPE_NTSCF &&
               acf_msg_type == RCP_ACF_MSG_TYPE_ABB &&
               byte_bus_id == RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    }

    /* HW_CONFIGURED / RCP_CONFIGURED: frame-level acceptance beyond the
     * time-sync rule already applied above is unrestricted at this
     * milestone -- register-level write filtering is
     * rcp_lifecycle_field_writable()'s job, and full endpoint/stream routing
     * is milestone 62's register-map job. */
    return true;
}

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

//cfusa:req REQ-LIFECYCLE-018
//cfusa:req REQ-LIFECYCLE-019
//cfusa:req REQ-LIFECYCLE-020
bool rcp_lifecycle_field_writable(rcp_lifecycle_state_t state,
                                   rcp_lifecycle_field_kind_t kind,
                                   rcp_lifecycle_writer_ctx_t writer)
{
    bool authorized = writer.via_root_client_ep0 || writer.via_owning_stream;

    switch (kind) {
    case RCP_LIFECYCLE_FIELD_HW_GENERIC:
        /* Read-only the moment the server leaves HW_UNCONFIGURED, for any
         * writer. */
        return state == RCP_LIFECYCLE_HW_UNCONFIGURED;

    case RCP_LIFECYCLE_FIELD_FUNCTIONAL_W:
        if (state == RCP_LIFECYCLE_HW_UNCONFIGURED) return false;
        if (state == RCP_LIFECYCLE_RCP_CONFIGURED) return authorized;
        return true; /* HW_CONFIGURED */

    case RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR:
        if (state == RCP_LIFECYCLE_HW_UNCONFIGURED)  return false;
        if (state == RCP_LIFECYCLE_RCP_CONFIGURED)   return false; /* permanently locked */
        return true; /* HW_CONFIGURED */

    default:
        return false;
    }
}
