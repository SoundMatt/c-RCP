#include "rcp/server.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-SRV-024
const char *rcp_server_strerror(rcp_server_errc_t e)
{
    switch (e) {
    case RCP_SERVER_OK:                      return "rcp/server: success";
    case RCP_SERVER_ERR_HW_CFG_INCONSISTENT:  return "rcp/server: HW configuration inconsistent";
    case RCP_SERVER_ERR_RCP_CFG_INCONSISTENT: return "rcp/server: RCP configuration inconsistent";
    case RCP_SERVER_ERR_INVALID_TRANSITION:   return "rcp/server: invalid lifecycle transition";
    default:                                  return "rcp/server: unknown error";
    }
}

/* ── Plausibility checks ───────────────────────────────────────────────────── */

//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
//cfusa:req REQ-SRV-004
rcp_server_errc_t rcp_server_check_hw_cfg(const rcp_server_plausibility_snapshot_t *snap)
{
    size_t i;

    /* Fail-safe: no configuration evidence at all is treated as
     * inconsistent, never as vacuously plausible. */
    if (!snap) return RCP_SERVER_ERR_HW_CFG_INCONSISTENT;

    for (i = 0; i < snap->endpoint_count; i++) {
        const rcp_server_endpoint_plausibility_t *ep = &snap->endpoints[i];

        if (!ep->ep_used) continue;
        if (!ep->hw_pin_mapped)      return RCP_SERVER_ERR_HW_CFG_INCONSISTENT;
        if (!ep->has_request_stream) return RCP_SERVER_ERR_HW_CFG_INCONSISTENT;
    }

    return RCP_SERVER_OK;
}

//cfusa:req REQ-SRV-005
//cfusa:req REQ-SRV-006
//cfusa:req REQ-SRV-007
rcp_server_errc_t rcp_server_check_rcp_cfg(const rcp_server_plausibility_snapshot_t *snap)
{
    size_t i;

    if (!snap) return RCP_SERVER_ERR_RCP_CFG_INCONSISTENT;

    for (i = 0; i < snap->endpoint_count; i++) {
        const rcp_server_endpoint_plausibility_t *ep = &snap->endpoints[i];

        if (!ep->ep_used) continue;
        if (!ep->has_stream_assoc) return RCP_SERVER_ERR_RCP_CFG_INCONSISTENT;
    }

    for (i = 0; i < snap->request_stream_count; i++) {
        const rcp_server_request_stream_plausibility_t *rs = &snap->request_streams[i];

        if (!rs->configured) continue;
        if (!rs->has_response_stream) return RCP_SERVER_ERR_RCP_CFG_INCONSISTENT;
    }

    return RCP_SERVER_OK;
}

/* ── Lifecycle transitions ─────────────────────────────────────────────────── */

//cfusa:req REQ-SRV-008
//cfusa:req REQ-SRV-009
//cfusa:req REQ-SRV-010
//cfusa:req REQ-SRV-011
//cfusa:req REQ-SRV-012
//cfusa:req REQ-SRV-013
rcp_server_errc_t rcp_server_lifecycle_transition(rcp_server_lifecycle_t *state,
                                                   rcp_server_lifecycle_t target,
                                                   const rcp_server_plausibility_snapshot_t *snap)
{
    rcp_server_lifecycle_t from = *state;

    if (target == from) return RCP_SERVER_OK; /* no-op */

    if (from == RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED &&
        target == RCP_SERVER_LIFECYCLE_HW_CONFIGURED) {
        rcp_server_errc_t rc = rcp_server_check_hw_cfg(snap);
        if (rc != RCP_SERVER_OK) return rc;
        *state = target;
        return RCP_SERVER_OK;
    }

    if (from == RCP_SERVER_LIFECYCLE_HW_CONFIGURED &&
        target == RCP_SERVER_LIFECYCLE_RCP_CONFIGURED) {
        rcp_server_errc_t rc = rcp_server_check_rcp_cfg(snap);
        if (rc != RCP_SERVER_OK) return rc;
        *state = target;
        return RCP_SERVER_OK;
    }

    /* Demotion back to HW_UNCONFIGURED via the discovery-stream/root-client
     * reset path is unconditional -- from either configured state. */
    if (target == RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED &&
        (from == RCP_SERVER_LIFECYCLE_HW_CONFIGURED ||
         from == RCP_SERVER_LIFECYCLE_RCP_CONFIGURED)) {
        *state = target;
        return RCP_SERVER_OK;
    }

    /* Everything else -- skipping a state on the way up, or downgrading
     * from RCP_CONFIGURED to HW_CONFIGURED directly -- is not a modeled
     * transition. */
    return RCP_SERVER_ERR_INVALID_TRANSITION;
}

/* ── Per-state request filtering ───────────────────────────────────────────── */

//cfusa:req REQ-SRV-014
//cfusa:req REQ-SRV-015
//cfusa:req REQ-SRV-016
//cfusa:req REQ-SRV-017
bool rcp_server_lifecycle_should_accept(rcp_server_lifecycle_t state,
                                        bool time_sync_supported,
                                        uint8_t avtp_subtype,
                                        uint8_t acf_msg_type,
                                        rcp_byte_bus_id_t byte_bus_id)
{
    if (rcp_avtp_should_drop_tscf(time_sync_supported, avtp_subtype)) return false;

    if (state == RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED) {
        /* TSCF's presentation-time semantics presuppose a configured
         * request stream, which cannot exist yet during bootstrap --
         * dropped outright regardless of the node's own time-sync
         * capability, unlike the general rule just applied above. */
        if (avtp_subtype == RCP_AVTP_SUBTYPE_TSCF) return false;

        return avtp_subtype == RCP_AVTP_SUBTYPE_NTSCF &&
               acf_msg_type == RCP_ACF_MSG_TYPE_ABB &&
               byte_bus_id == RCP_SERVER_DISCOVERY_BYTE_BUS_ID;
    }

    /* HW_CONFIGURED / RCP_CONFIGURED: frame-level acceptance beyond the
     * time-sync rule already applied above is unrestricted at this
     * milestone -- register-level write filtering is
     * rcp_server_field_writable()'s job, and full endpoint/stream routing
     * is milestone 62's register-map job. */
    return true;
}

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

//cfusa:req REQ-SRV-018
//cfusa:req REQ-SRV-019
//cfusa:req REQ-SRV-020
bool rcp_server_field_writable(rcp_server_lifecycle_t state,
                               rcp_server_field_kind_t kind,
                               rcp_server_writer_ctx_t writer)
{
    bool authorized = writer.via_root_client_ep0 || writer.via_owning_stream;

    switch (kind) {
    case RCP_SERVER_FIELD_HW_GENERIC:
        /* Read-only the moment the server leaves HW_UNCONFIGURED, for any
         * writer. */
        return state == RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED;

    case RCP_SERVER_FIELD_FUNCTIONAL_W:
        if (state == RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED) return false;
        if (state == RCP_SERVER_LIFECYCLE_RCP_CONFIGURED) return authorized;
        return true; /* HW_CONFIGURED */

    case RCP_SERVER_FIELD_FUNCTIONAL_W_STAR:
        if (state == RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED)  return false;
        if (state == RCP_SERVER_LIFECYCLE_RCP_CONFIGURED)   return false; /* permanently locked */
        return true; /* HW_CONFIGURED */

    default:
        return false;
    }
}

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

//cfusa:req REQ-SRV-021
//cfusa:req REQ-SRV-022
//cfusa:req REQ-SRV-023
void rcp_server_endpoint_init(rcp_server_endpoint_t *ep, bool ep_enable)
{
    ep->ep_enable = ep_enable;
    ep->queue     = NULL;
    ep->queue_len = 0;
    ep->queue_cap = 0;
}

//cfusa:req REQ-SRV-021
//cfusa:req REQ-SRV-022
//cfusa:req REQ-SRV-023
void rcp_server_endpoint_destroy(rcp_server_endpoint_t *ep)
{
    size_t i;

    for (i = 0; i < ep->queue_len; i++) {
        rcp_bytes_free(&ep->queue[i]);
    }
    free(ep->queue);
    ep->queue     = NULL;
    ep->queue_len = 0;
    ep->queue_cap = 0;
}

//cfusa:req REQ-SRV-021
//cfusa:req REQ-SRV-022
bool rcp_server_endpoint_submit(rcp_server_endpoint_t *ep,
                                const uint8_t *frame, size_t frame_len)
{
    rcp_bytes_t *grown;

    if (ep->ep_enable) return true; /* caller must execute this now */

    if (ep->queue_len == ep->queue_cap) {
        size_t new_cap = (ep->queue_cap == 0) ? 4 : ep->queue_cap * 2;

        grown = (rcp_bytes_t *)realloc(ep->queue, new_cap * sizeof(*grown));
        if (!grown) return false; /* still "queued": nothing to execute now */
        ep->queue     = grown;
        ep->queue_cap = new_cap;
    }

    ep->queue[ep->queue_len] = rcp_bytes_dup(frame, frame_len);
    ep->queue_len++;
    return false;
}

//cfusa:req REQ-SRV-023
void rcp_server_endpoint_set_enable(rcp_server_endpoint_t *ep, bool enable)
{
    ep->ep_enable = enable;
}

//cfusa:req REQ-SRV-023
bool rcp_server_endpoint_drain_one(rcp_server_endpoint_t *ep, rcp_bytes_t *out_frame)
{
    size_t i;

    if (!ep->ep_enable) return false;
    if (ep->queue_len == 0) return false;

    *out_frame = ep->queue[0];
    for (i = 1; i < ep->queue_len; i++) {
        ep->queue[i - 1] = ep->queue[i];
    }
    ep->queue_len--;
    return true;
}

//cfusa:req REQ-SRV-021
size_t rcp_server_endpoint_queue_len(const rcp_server_endpoint_t *ep)
{
    return ep->queue_len;
}
