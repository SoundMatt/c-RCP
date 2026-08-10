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
    case RCP_LIFECYCLE_ERR_UNAUTHORIZED:         return "rcp/lifecycle: writer not authorized for this transition";
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
//cfusa:req REQ-LIFECYCLE-031
rcp_lifecycle_errc_t rcp_lifecycle_transition(rcp_lifecycle_state_t *state,
                                               rcp_lifecycle_state_t target,
                                               const rcp_lifecycle_plausibility_snapshot_t *snap,
                                               rcp_lifecycle_writer_ctx_t writer)
{
    rcp_lifecycle_state_t from = *state;
    /* TC18 §12.3.1.2 (REQ-LIFECYCLE-031): a svr_lifecycle_state write is
     * accepted only via the discovery stream or the root client -- see
     * this function's own header doc comment for why via_root_client_ep0
     * alone (not a general "any valid stream" case) is the honestly-
     * achievable form of TC18's further root-client-configured-vs-not
     * narrowing given this library's current architecture. */
    bool authorized = writer.via_discovery_stream || writer.via_root_client_ep0;

    if (target == from) return RCP_LIFECYCLE_OK; /* no-op; writer not consulted */

    if (from == RCP_LIFECYCLE_HW_UNCONFIGURED &&
        target == RCP_LIFECYCLE_HW_CONFIGURED) {
        /* writer not consulted for this transition -- see header doc
         * comment: already enforced one layer up by should_accept(). */
        rcp_lifecycle_errc_t rc = rcp_lifecycle_check_hw_cfg(snap);
        if (rc != RCP_LIFECYCLE_OK) return rc;
        *state = target;
        return RCP_LIFECYCLE_OK;
    }

    if (from == RCP_LIFECYCLE_HW_CONFIGURED &&
        target == RCP_LIFECYCLE_RCP_CONFIGURED) {
        rcp_lifecycle_errc_t rc;
        if (!authorized) return RCP_LIFECYCLE_ERR_UNAUTHORIZED;
        rc = rcp_lifecycle_check_rcp_cfg(snap);
        if (rc != RCP_LIFECYCLE_OK) return rc;
        *state = target;
        return RCP_LIFECYCLE_OK;
    }

    /* Demotion back to HW_UNCONFIGURED via the discovery-stream/root-client
     * reset path -- from either configured state -- is unconditional once
     * authorized; snap is not consulted for a reset. */
    if (target == RCP_LIFECYCLE_HW_UNCONFIGURED &&
        (from == RCP_LIFECYCLE_HW_CONFIGURED ||
         from == RCP_LIFECYCLE_RCP_CONFIGURED)) {
        if (!authorized) return RCP_LIFECYCLE_ERR_UNAUTHORIZED;
        *state = target;
        return RCP_LIFECYCLE_OK;
    }

    /* Everything else -- skipping a state on the way up, or downgrading
     * from RCP_CONFIGURED to HW_CONFIGURED directly -- is not a modeled
     * transition, regardless of writer. */
    return RCP_LIFECYCLE_ERR_INVALID_TRANSITION;
}

/* ── Per-state request filtering ───────────────────────────────────────────── */

//cfusa:req REQ-LIFECYCLE-014
//cfusa:req REQ-LIFECYCLE-015
//cfusa:req REQ-LIFECYCLE-016
//cfusa:req REQ-LIFECYCLE-017
//cfusa:req REQ-LIFECYCLE-028
//cfusa:req REQ-LIFECYCLE-032
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

    if (state == RCP_LIFECYCLE_HW_CONFIGURED) {
        /* TC18 §12.3.1.2 (a section whose own printed heading confusingly
         * repeats "HW_UNCONFIGURED" -- verified against the primary-source
         * PDF directly, not just the pre-extracted TC18.txt line range, to
         * rule out an extraction artifact before trusting it; the
         * section's actual content -- "access to HW_config...shall have
         * been concluded and locked", advancing state to RCP_CONFIGURED,
         * root-client write access -- is unambiguously HW_CONFIGURED's
         * behavior, not HW_UNCONFIGURED's, whose own correctly-labeled
         * §12.3.1.1 immediately precedes it and already matches this
         * function's HW_UNCONFIGURED branch above): a TSCF-headed AVTPDU
         * is dropped unconditionally, the same rule HW_UNCONFIGURED
         * already applies above -- TSCF's presentation-time semantics
         * still presuppose configuration (stream/byte_bus_id mapping,
         * response queues) that does not exist until RCP_CONFIGURED, so a
         * time-sync-capable server must not process timed requests here
         * either, regardless of the general time-sync rule already
         * applied at the top of this function.
         *
         * The same section also requires dropping ACF_GBB-format requests
         * addressed to EP0 in HW_CONFIGURED (REQ-LIFECYCLE-029) --
         * deliberately NOT implemented here: every conditional request
         * kind (compound/compound-wait/triggered/chained/timed/cancel,
         * request_compound.h and siblings) is wire-encoded as ACF_GBB
         * unconditionally (the mtv-repurposing scheme those modules' own
         * file headers document at length). An unconditional ACF_GBB drop
         * would only matter for a conditional request literally addressed
         * to EP0 itself now that the byte_bus_id restriction just below
         * already drops everything else -- a narrow, low-impact residual
         * rather than this milestone's own concern; tracked as its own
         * follow-up in issue #198, not attempted here.
         *
         * TC18 §12.3.1.2 (further down the same section): "Request to EPs
         * other than EP0 that are not config requests will be ignored and
         * dropped without response." c-RCP has no wire-level encode/decode
         * pair for a functional-configuration read/write request at all,
         * for any endpoint type -- regmap.h's own file header documents
         * this as deliberately deferred ("Exact on-wire encodings for the
         * structures below are deliberately left unimplemented... wiring
         * them to an actual byte_message_info read/write exchange is
         * later phases' job"); confirmed by grepping for any regmap
         * encode/decode pair anywhere in this codebase and finding none
         * outside discovery.c's own read-only discovery-response encoder.
         * Every wire request this library can currently decode for a
         * non-EP0 endpoint is therefore, by construction, an operational
         * one -- so the achievable, currently-correct interpretation of
         * this rule, given that real scope, is simply: only EP0 is
         * reachable in HW_CONFIGURED, the exact same byte_bus_id
         * restriction HW_UNCONFIGURED's branch above already applies (see
         * REQ-LIFECYCLE-025/034/036, this issue's remaining Group 2/3
         * items, for the finer-grained stream-identity/authorization
         * rules layered on top of this once register-map wire I/O
         * eventually exists). */
        if (avtp_subtype == RCP_AVTP_SUBTYPE_TSCF) return false;
        if (byte_bus_id != RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID) return false;

        return true;
    }

    /* RCP_CONFIGURED: frame-level acceptance beyond the time-sync rule
     * already applied above is unrestricted at this milestone --
     * register-level write filtering is rcp_lifecycle_field_writable()'s
     * job, and full endpoint/stream routing is milestone 62's
     * register-map job. */
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
    bool authorized              = writer.via_root_client_ep0 || writer.via_owning_stream;
    /* TC18 §12.3.1.2/§12.7.3 (REQ-LIFECYCLE-030/036): in HW_CONFIGURED,
     * functional-config write access is authorized for the root client
     * via EP0, the endpoint's own owning stream, OR the discovery
     * stream -- one condition wider than RCP_CONFIGURED's own
     * `authorized` above, which does not (yet) admit the discovery
     * stream on its own (see this function's own header doc comment). */
    bool hw_configured_authorized = authorized || writer.via_discovery_stream;
    bool writable;

    switch (kind) {
    case RCP_LIFECYCLE_FIELD_HW_GENERIC:
        /* Read-only the moment the server leaves HW_UNCONFIGURED, for any
         * writer. */
        writable = (state == RCP_LIFECYCLE_HW_UNCONFIGURED);
        break;

    case RCP_LIFECYCLE_FIELD_FUNCTIONAL_W:
        if (state == RCP_LIFECYCLE_HW_UNCONFIGURED) {
            writable = false;
        } else if (state == RCP_LIFECYCLE_RCP_CONFIGURED) {
            writable = authorized;
        } else {
            writable = hw_configured_authorized; /* HW_CONFIGURED */
        }
        break;

    case RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR:
        /* TC18 Table 22's own legend: "This configuration table can only be
         * changed in the life-cycle states HW_UNCONFIGURED and HW_CONFIGURED.
         * In RCP_CONFIGURED ... this is read-only. (As indicated by W*)" --
         * writable in both HW_UNCONFIGURED and HW_CONFIGURED (the latter
         * now subject to the same authorization gate FUNCTIONAL_W applies
         * above), locked only once RCP_CONFIGURED is reached. */
        if (state == RCP_LIFECYCLE_RCP_CONFIGURED) {
            writable = false; /* permanently locked once reached */
        } else if (state == RCP_LIFECYCLE_HW_CONFIGURED) {
            writable = hw_configured_authorized;
        } else {
            writable = true; /* HW_UNCONFIGURED */
        }
        break;

    default:
        writable = false;
        break;
    }

    /* TC18 §12.3.1.1/§12.3.1.2/§12.3.1.3 (REQ-LIFECYCLE-027): a write
     * request is accepted only when sent in a unicast frame, restated
     * once per lifecycle state. ANDed in uniformly here rather than
     * duplicated per branch above -- see this function's own header
     * doc comment. */
    return writable && !writer.via_non_unicast_frame;
}
