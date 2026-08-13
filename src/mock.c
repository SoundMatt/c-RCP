/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/mock.h"

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/e2e.h"
#include "rcp/request_cancel.h"
#include "rcp/request_chained.h"
#include "rcp/request_compound.h"
#include "rcp/scheduler.h"

#include <stdlib.h>
#include <string.h>

/* ── Endpoint slot ─────────────────────────────────────────────────────────── */

typedef struct {
    bool                         in_use;
    rcp_byte_bus_id_t            byte_bus_id;
    rcp_regmap_ep_generic_cfg_t  generic;
    rcp_server_endpoint_t        queue;
    rcp_mock_endpoint_handler_fn handler;
    void                        *user_data;
    /* This test double's own stand-in for TC18 §12.7.1's
     * ep_req_crc_enable -- see
     * rcp_mock_server_set_endpoint_req_crc_enable()'s own doc comment
     * for why this can't just be read out of
     * rcp_regmap_ep_functional_cfg_t directly. Defaults false (every
     * slot starts zeroed -- srv itself is calloc()'d in
     * rcp_mock_server_new(), and rcp_mock_server_add_endpoint() never
     * sets this field). Consulted only by
     * rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e(); the plain
     * dispatch()/dispatch_frame() ignore it. */
    bool                         req_crc_enable;
    /* REQ-E2E-021 (issue #201): this test double's own stand-in for
     * TC18 §12.7.7 Table 24's own rx_enforce_e2e (a per-REQUEST-STREAM
     * config bit in the real spec, not per-endpoint -- kept here as a
     * per-endpoint stand-in for the same "type-erased slot has no way
     * to read a real config table generically" reason
     * req_crc_enable's own doc comment already gives). Defaults false
     * (RCP_E2E_CRC_ACTION_DROP_REQUEST), matching req_crc_enable's own
     * default disposition. Consulted only by
     * rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e() on a CRC
     * mismatch, to decide whether that mismatch also latches the whole
     * stream faulted (see rcp_mock_server_set_stream_fault_tracker()). */
    bool                         rx_enforce_e2e;
} rcp_mock_endpoint_slot_t;

/* ── The server double ─────────────────────────────────────────────────────── */

struct rcp_mock_server {
    rcp_lifecycle_state_t    state;
    rcp_regmap_general_t     regmap;
    rcp_mock_endpoint_slot_t endpoints[RCP_MOCK_MAX_ENDPOINTS];
    size_t                   endpoint_count;
    /* HW_config table (REQ-RMAP-040/041) -- see mock.h's own doc comment
     * on rcp_mock_server_set_hw_pin_map()/_hw_pin_map(). */
    rcp_regmap_hw_pin_map_entry_t hw_pin_map[RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES];
    size_t                        hw_pin_map_len;
    /* request-stream-cfg table (REQ-SEQ-013, issue #335) -- see mock.h's
     * own doc comment on rcp_mock_server_set_request_stream_cfg(). Its
     * own rx_stream_id fields are this server's only way to resolve an
     * incoming request's stream_id into the request_stream_index
     * identity TC18's own access-control-bearing fields (Table 28's
     * Request_stream_index among them) are expressed in terms of. */
    rcp_regmap_request_stream_cfg_t request_stream_cfg[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES];
    size_t                          request_stream_cfg_count;
    /* EP_ID_config table (issue #335) -- see mock.h's own doc comment on
     * rcp_mock_server_set_ep_id_map(). Srv's own only way to know which
     * byte_bus_ids are bound to a given request_stream_index --
     * rcp_mock_server_broadcast_safe_state()'s sole data source. */
    rcp_regmap_ep_id_map_entry_t ep_id_map[RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES];
    size_t                       ep_id_map_count;
    /* The sequencer-state registers compound/compound-wait requests read
     * and advance. Server-wide rather than per-endpoint: a sequencer is a
     * server register, and requests on different endpoints routinely
     * drive the same one. */
    rcp_sequencer_table_t    sequencers;
    /* REQ-WDG-010 (issue #201): not owned by srv -- see
     * rcp_mock_server_set_watchdog_keeper()'s own doc comment (mock.h)
     * for the full lifecycle contract. NULL (every slot starts zeroed,
     * srv itself is calloc()'d) disables kicking entirely, the default
     * for every rcp_mock_server_t. */
    rcp_watchdog_keeper_t   *watchdog;
    /* REQ-E2E-021 (issue #201): not owned by srv, same lifecycle
     * contract as watchdog above -- see
     * rcp_mock_server_set_stream_fault_tracker()'s own doc comment
     * (mock.h). NULL disables stream-fault blocking entirely, the
     * default for every rcp_mock_server_t. */
    rcp_e2e_stream_fault_tracker_t *stream_fault_tracker;
};

//cfusa:req REQ-MOCK-001
const char *rcp_mock_strerror(rcp_mock_errc_t e)
{
    switch (e) {
        case RCP_MOCK_OK:                   return "ok";
        case RCP_MOCK_ERR_DUPLICATE_BUS_ID: return "duplicate byte_bus_id";
        case RCP_MOCK_ERR_CAPACITY:         return "endpoint table full";
        case RCP_MOCK_ERR_NOT_FOUND:        return "byte_bus_id not registered";
        default:                            return "unknown rcp_mock_errc_t";
    }
}

//cfusa:req REQ-MOCK-002
rcp_mock_server_t *rcp_mock_server_new(void)
{
    rcp_mock_server_t *srv = (rcp_mock_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;

    srv->state = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_regmap_general_init(&srv->regmap);
    return srv;
}

//cfusa:req REQ-MOCK-003
void rcp_mock_server_destroy(rcp_mock_server_t *srv)
{
    size_t i;

    if (!srv) return;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use) {
            rcp_server_endpoint_destroy(&srv->endpoints[i].queue);
        }
    }
    rcp_sequencer_table_free(&srv->sequencers);
    free(srv);
}

//cfusa:req REQ-MOCK-004
rcp_lifecycle_state_t rcp_mock_server_state(const rcp_mock_server_t *srv)
{
    return srv->state;
}

//cfusa:req REQ-MOCK-005
//cfusa:req REQ-RMAP-023
rcp_lifecycle_errc_t rcp_mock_server_transition(rcp_mock_server_t *srv,
                                                 rcp_lifecycle_state_t target,
                                                 const rcp_lifecycle_plausibility_snapshot_t *snap,
                                                 rcp_lifecycle_writer_ctx_t writer,
                                                 bool all_other_eps_idle)
{
    rcp_lifecycle_errc_t rc =
        rcp_lifecycle_transition(&srv->state, target, snap, writer, all_other_eps_idle);
    /* srv->state reflects its own post-call value either way (updated on
     * success, left unchanged on failure per rcp_lifecycle_transition()'s
     * own doc comment) -- an unconditional sync here is therefore always
     * correct, not just an on-success special case. Keeps
     * regmap.svr_lifecycle_state (REQ-RMAP-023's own content field) from
     * ever silently drifting out of sync with the authoritative state
     * this same call just updated. */
    srv->regmap.svr_lifecycle_state = (uint8_t)srv->state;
    return rc;
}

//cfusa:req REQ-PWRMODE-019
bool rcp_mock_server_pwrmode_resume(rcp_mock_server_t *srv, rcp_pwrmode_handshake_t *hs)
{
    size_t i;

    if (!rcp_pwrmode_handshake_resume_queues(hs)) return false;

    /* TC18 §12.4.1: "After reception of valid message from the sleep
     * request Client all used endpoints and response queues will be
     * enabled." power.h's own rcp_pwrmode_handshake_resume_queues()
     * deliberately never touches server.h (that module's own file header:
     * "driving the actual re-init sequence... remains a caller's job") --
     * this srv-aware wrapper is that caller, re-enabling every registered
     * endpoint's queue the same way rcp_mock_server_set_endpoint_enable()
     * does one at a time. Response-queue objects and heartbeat-stream
     * re-emission have no implementation anywhere in this codebase yet
     * (see test_flush_triggers_and_heartbeat_are_absent()) -- a separate,
     * already-tracked gap this function cannot close. */
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use) {
            rcp_server_endpoint_set_enable(&srv->endpoints[i].queue, true);
        }
    }
    return true;
}

//cfusa:req REQ-MOCK-006
rcp_regmap_general_t *rcp_mock_server_regmap(rcp_mock_server_t *srv)
{
    return &srv->regmap;
}

//cfusa:req REQ-RMAP-040
bool rcp_mock_server_set_hw_pin_map(rcp_mock_server_t *srv,
                                     const rcp_regmap_hw_pin_map_entry_t *entries, size_t len)
{
    if (len > RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES) return false;

    if (len > 0) memcpy(srv->hw_pin_map, entries, len * sizeof(*entries));
    srv->hw_pin_map_len = len;
    return true;
}

//cfusa:req REQ-RMAP-040
const rcp_regmap_hw_pin_map_entry_t *rcp_mock_server_hw_pin_map(const rcp_mock_server_t *srv,
                                                                  size_t *out_len)
{
    *out_len = srv->hw_pin_map_len;
    return srv->hw_pin_map;
}

//cfusa:req REQ-SEQ-013
bool rcp_mock_server_set_request_stream_cfg(rcp_mock_server_t *srv,
                                             const rcp_regmap_request_stream_cfg_t *entries,
                                             size_t count)
{
    if (count > RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES) return false;

    if (count > 0) memcpy(srv->request_stream_cfg, entries, count * sizeof(*entries));
    srv->request_stream_cfg_count = count;
    return true;
}

//cfusa:req REQ-E2E-029
//cfusa:req REQ-E2E-030
//cfusa:req REQ-E2E-045
bool rcp_mock_server_set_ep_id_map(rcp_mock_server_t *srv,
                                    const rcp_regmap_ep_id_map_entry_t *entries, size_t count)
{
    if (count > RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES) return false;

    if (count > 0) memcpy(srv->ep_id_map, entries, count * sizeof(*entries));
    srv->ep_id_map_count = count;
    return true;
}

/* Finds the slot addressed at byte_bus_id, or NULL if none is registered. */
static rcp_mock_endpoint_slot_t *find_slot(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use && srv->endpoints[i].byte_bus_id == byte_bus_id) {
            return &srv->endpoints[i];
        }
    }
    return NULL;
}

/* const counterpart of find_slot(), for read-only accessors. */
static const rcp_mock_endpoint_slot_t *find_slot_const(const rcp_mock_server_t *srv,
                                                         rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use && srv->endpoints[i].byte_bus_id == byte_bus_id) {
            return &srv->endpoints[i];
        }
    }
    return NULL;
}

//cfusa:req REQ-MOCK-007
//cfusa:req REQ-MOCK-008
rcp_mock_errc_t rcp_mock_server_add_endpoint(rcp_mock_server_t *srv,
                                              rcp_byte_bus_id_t byte_bus_id,
                                              uint8_t ep_type, bool ep_enable,
                                              rcp_mock_endpoint_handler_fn handler,
                                              void *user_data)
{
    size_t i;
    rcp_mock_endpoint_slot_t *slot = NULL;

    if (find_slot(srv, byte_bus_id)) return RCP_MOCK_ERR_DUPLICATE_BUS_ID;
    if (srv->endpoint_count >= RCP_MOCK_MAX_ENDPOINTS) return RCP_MOCK_ERR_CAPACITY;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (!srv->endpoints[i].in_use) {
            slot = &srv->endpoints[i];
            break;
        }
    }
    /* Unreachable given the endpoint_count check above, but guarded rather
     * than trusted -- fail closed instead of dereferencing NULL. */
    if (!slot) return RCP_MOCK_ERR_CAPACITY;

    memset(slot, 0, sizeof(*slot));
    slot->in_use      = true;
    slot->byte_bus_id = byte_bus_id;
    rcp_regmap_ep_generic_cfg_init(&slot->generic);
    slot->generic.ep_type = ep_type;
    slot->generic.ep_used = true;
    rcp_server_endpoint_init(&slot->queue, ep_enable);
    slot->handler   = handler;
    slot->user_data = user_data;

    srv->endpoint_count++;
    srv->regmap.svr_ep_count = (uint16_t)srv->endpoint_count;
    return RCP_MOCK_OK;
}

//cfusa:req REQ-MOCK-009
bool rcp_mock_server_remove_endpoint(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    rcp_server_endpoint_destroy(&slot->queue);
    memset(slot, 0, sizeof(*slot));

    srv->endpoint_count--;
    srv->regmap.svr_ep_count = (uint16_t)srv->endpoint_count;
    return true;
}

//cfusa:req REQ-MOCK-010
bool rcp_mock_server_set_endpoint_enable(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                          bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    rcp_server_endpoint_set_enable(&slot->queue, enable);
    return true;
}

//cfusa:req REQ-E2E-031
bool rcp_mock_server_set_endpoint_req_crc_enable(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id, bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    slot->req_crc_enable = enable;
    return true;
}

//cfusa:req REQ-E2E-021
bool rcp_mock_server_set_endpoint_rx_enforce_e2e(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id, bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    slot->rx_enforce_e2e = enable;
    return true;
}

//cfusa:req REQ-WDG-010
void rcp_mock_server_set_watchdog_keeper(rcp_mock_server_t *srv,
                                          rcp_watchdog_keeper_t *keeper)
{
    srv->watchdog = keeper;
}

//cfusa:req REQ-E2E-021
void rcp_mock_server_set_stream_fault_tracker(rcp_mock_server_t *srv,
                                               rcp_e2e_stream_fault_tracker_t *tracker)
{
    srv->stream_fault_tracker = tracker;
}

//cfusa:req REQ-MOCK-011
size_t rcp_mock_server_endpoint_queue_len(const rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    const rcp_mock_endpoint_slot_t *slot = find_slot_const(srv, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_queue_len(&slot->queue);
}

/* Runs slot's own handler (if any) against one already-dequeued request,
 * writing its result into *out_response (left zeroed if handler is NULL or
 * declines to populate it). Shared by rcp_mock_server_dispatch()'s
 * immediate-execution path and rcp_mock_server_drain_endpoint(). */
static void run_handler(rcp_mock_endpoint_slot_t *slot, const uint8_t *request, size_t request_len,
                         rcp_bytes_t *out_response)
{
    memset(out_response, 0, sizeof(*out_response));
    if (slot->handler) {
        slot->handler(request, request_len, out_response, slot->user_data);
    }
}

//cfusa:req REQ-MOCK-012
//cfusa:req REQ-MOCK-013
//cfusa:req REQ-MOCK-014
//cfusa:req REQ-MOCK-015
//cfusa:req REQ-MOCK-028
/* Applies an already-classified cancellation request against slot's own
 * request store. Which of the three cancellation opcodes it is decides
 * what gets removed; a clear-single additionally needs its target
 * transaction_num decoded out of the message.
 *
 * TC18 §11.2.3.3: "The request initiating the cancellation will create an
 * error response with the error code = REQUEST_NOT_FOUND, when the
 * clear_transaction_num was not found." That response carries the
 * cancellation request's own byte_bus_id/transaction_num (TC18 §12.9.6's
 * general rule), not the not-found target's -- out_response is populated
 * accordingly when rcp_server_endpoint_cancel_single() reports
 * RCP_CANCEL_RESULT_NOT_FOUND. byte_bus_id is this endpoint's own
 * address (see finish_admission()'s own doc comment for why this is
 * already known to the caller rather than re-decoded).
 *
 * Clear-all and clear-non-safestate cancel every request each removes
 * with its own REQUEST_CANCELED error response (TC18 §11.2.3, one
 * response per cancelled request, not one for the cancellation itself)
 * -- a multi-response fanout this function's single out_response cannot
 * represent; not attempted here, tracked separately
 * (github.com/SoundMatt/c-RCP/issues/163). A clear-single's own
 * successful cascade (below, REQ-CANCEL-012) carries the identical
 * fanout gap for the same reason -- each cascaded removal is, by the
 * same TC18 §11.2.3 rule, its own REQUEST_CANCELED response too. */
static void apply_cancellation(rcp_mock_endpoint_slot_t *slot, uint8_t request_type,
                                const uint8_t *request, size_t request_len,
                                rcp_byte_bus_id_t byte_bus_id, rcp_bytes_t *out_response)
{
    rcp_byte_bus_id_t    bus;
    uint8_t              target_tn;
    uint8_t              tn;
    rcp_cancel_result_t  result;

    switch (request_type) {
    case RCP_REQUEST_TYPE_CLEAR_ALL:
        (void)rcp_server_endpoint_cancel_all(&slot->queue);
        break;

    case RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE:
        (void)rcp_server_endpoint_cancel_non_safestate(&slot->queue);
        break;

    case RCP_REQUEST_TYPE_CLEAR_SINGLE:
        if (rcp_cancel_decode_clear_single(request, request_len, &bus, &target_tn, &tn) ==
            RCP_CANCEL_OK) {
            /* REQ-CANCEL-012: read out BEFORE cancelling -- a successful
             * rcp_server_endpoint_cancel_single() call below already
             * frees and clears the target's own store slot, so its
             * chain_group/chain_position have to be captured first if
             * TC18 §11.2.3's cascade rule (cancelling this request also
             * cancels every chained successor at or after its own
             * position) is to be applied afterward. Left at their
             * zero-valued defaults (chain_group 0, the "not part of a
             * chain" sentinel) if no matching entry is found -- the
             * cascade call below is then a guaranteed no-op, exactly
             * matching a target that reports NOT_FOUND. */
            uint32_t target_chain_group    = 0;
            uint8_t  target_chain_position = 0;
            size_t   j;

            for (j = 0; j < RCP_SERVER_MAX_PENDING; j++) {
                if (slot->queue.pending[j].in_use &&
                    slot->queue.pending[j].transaction_num == target_tn) {
                    target_chain_group    = slot->queue.pending[j].chain_group;
                    target_chain_position = slot->queue.pending[j].chain_position;
                    break;
                }
            }

            /* Every request still sitting in the store is by definition
             * queued rather than under execution -- this dispatcher runs a
             * selected request to completion synchronously inside
             * rcp_mock_server_tick(), so nothing is ever observably
             * mid-execution here. */
            result = rcp_server_endpoint_cancel_single(&slot->queue, target_tn,
                                                        RCP_CANCEL_LIFECYCLE_QUEUED);
            if (result == RCP_CANCEL_RESULT_NOT_FOUND) {
                *out_response =
                    rcp_acf_build_error_response(byte_bus_id, tn, RCP_ERROR_REQUEST_NOT_FOUND);
            } else if (result == RCP_CANCEL_RESULT_CANCELED) {
                (void)rcp_server_endpoint_cancel_chain_from(&slot->queue, target_chain_group,
                                                              target_chain_position);
            }
        }
        break;

    default:
        break;
    }
}

/* Maps one server.h admission outcome onto this module's own dispatch
 * result, running the endpoint's handler for the execute-now case.
 *
 * error is rcp_server_endpoint_admit()'s *out_error: RCP_ERROR_NONE for
 * every outcome except the rejection paths that determined a specific
 * TC18 Table 30 code (see that function's own doc comment for which
 * paths currently do). When non-RCP_ERROR_NONE, out_response is
 * populated with a real TC18 sec 12.9.6 error response
 * (rcp_acf_build_error_response()) instead of being left zeroed --
 * byte_bus_id is this endpoint's own address (already known to the
 * caller, which routed to this slot by it); transaction_num is read
 * back out of the request frame's own header via
 * rcp_acf_unpack_header(), which populates it correctly regardless of
 * mtv/request-type repurposing (transaction_num is not part of the
 * repurposed region). */
static rcp_mock_dispatch_result_t finish_admission(rcp_mock_endpoint_slot_t *slot,
                                                    rcp_server_admit_t admit, uint8_t request_type,
                                                    const uint8_t *request, size_t request_len,
                                                    rcp_wire_error_t error,
                                                    rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_bytes_t *out_response)
{
    switch (admit) {
    case RCP_SERVER_ADMIT_EXECUTE_NOW:
        run_handler(slot, request, request_len, out_response);
        return RCP_MOCK_DISPATCH_OK;
    case RCP_SERVER_ADMIT_QUEUED:
        return RCP_MOCK_DISPATCH_QUEUED;
    case RCP_SERVER_ADMIT_PENDING:
        return RCP_MOCK_DISPATCH_PENDING;
    case RCP_SERVER_ADMIT_CANCELLATION:
        apply_cancellation(slot, request_type, request, request_len, byte_bus_id, out_response);
        return RCP_MOCK_DISPATCH_CANCELLED;
    case RCP_SERVER_ADMIT_REJECTED:
    default:
        if (error != RCP_ERROR_NONE) {
            rcp_acf_byte_message_info_t hdr = {0};
            if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
                *out_response =
                    rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num, error);
            }
        }
        return RCP_MOCK_DISPATCH_REJECTED;
    }
}

//cfusa:req REQ-MOCK-021
//cfusa:req REQ-MOCK-030
/* The actual body of rcp_mock_server_dispatch(), factored out so
 * rcp_mock_server_dispatch_e2e() can reach it directly (dispatch_plain())
 * without going back through the public rcp_mock_server_dispatch() --
 * which, as of REQ-WDG-010, kicks the watchdog itself. dispatch_e2e()
 * already kicks once, unconditionally, at its own top (covering the CRC-
 * mismatch/short-frame paths that return before ever reaching this
 * helper) -- routing its own delegation calls through the public
 * function too would kick a second time for the exact same received
 * request. No behavior other than the watchdog kick's own call site
 * changes here. */
static rcp_mock_dispatch_result_t dispatch_plain(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id,
                                                  uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                  bool time_sync_supported, uint64_t stream_id,
                                                  const uint8_t *request, size_t request_len,
                                                  rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_server_admit_t        admit;
    uint8_t                   request_type = 0;
    rcp_wire_error_t          error        = RCP_ERROR_NONE;
    rcp_lifecycle_accept_t    accept;
    size_t                    admitted_index = 0;

    memset(out_response, 0, sizeof(*out_response));

    accept = rcp_lifecycle_should_accept(srv->state, time_sync_supported, avtp_subtype,
                                          acf_msg_type, byte_bus_id);
    if (accept == RCP_LIFECYCLE_DROP) {
        return RCP_MOCK_DISPATCH_DROPPED;
    }
    if (accept == RCP_LIFECYCLE_REJECT) {
        /* REQ-LIFECYCLE-033: admitted far enough to identify (TC18 §12.7's
         * own EP0-scoped rule), but answered with REQUEST_REJECTED rather
         * than processed -- same transaction_num-recovery technique this
         * function used to use for its own (now-removed) EP_NOT_FOUND
         * path; see REQ-MOCK-030's own history below. */
        rcp_acf_byte_message_info_t hdr = {0};
        if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
            *out_response = rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num,
                                                          RCP_ERROR_REQUEST_REJECTED);
        }
        return RCP_MOCK_DISPATCH_REJECTED;
    }

    slot = find_slot(srv, byte_bus_id);
    if (!slot) {
        /* TC18 §12.9.1: "If the lookup of the byte_bus_id in the context
         * of the stream_id does not point to an Endpoint, the request is
         * dropped without further notification." No response is sent --
         * out_response stays zeroed, per this function's own entry
         * memset() above (REQ-MOCK-030, corrected 2026-08-10,
         * c-RCP-AUDIT-06 issue #256: this branch previously sent a
         * Table 30 EP_NOT_FOUND response for exactly this case, but
         * Table 30's own EP_NOT_FOUND row is scoped to a different,
         * unimplemented scenario -- "if a Trigger request refers to a
         * nonexisting EP", a Trigger request's own trigger_source_ep
         * sub-field naming a nonexistent EP, not the addressed
         * byte_bus_id of the request itself). */
        return RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
    }

    /* The request_type-aware routing decision lives in server.h: a
     * standard request keeps the original submit-or-queue behavior, a
     * conditional one is decoded and stored, a cancellation one is
     * reported back for this module to apply. Admission carries no tick of
     * its own (0): a stored request's exec_delay is measured from the
     * moment its own start condition first holds, which is decided later
     * by rcp_server_endpoint_select_due() against the caller's tick. */
    admit = rcp_server_endpoint_admit(&slot->queue, request, request_len, 0u, &request_type,
                                       &admitted_index, &error);

    /* REQ-E2E-030 (issue #335): a request-storage overflow on THIS
     * endpoint's own queue is answered locally exactly as before
     * (RCP_SERVER_ADMIT_REJECTED, error == RCP_ERROR_REQUEST_STORAGE_
     * OVERFLOW, handled by finish_admission() below) -- but TC18 §12.7.7
     * Table 24's own rx_ovrflw_safestate_enable names a STREAM-WIDE
     * consequence, not a single-endpoint one: every endpoint bound to
     * the same request stream this overflow occurred on must be driven
     * toward its configured safe state too, not just the one whose own
     * queue happened to fill up. rcp_e2e_overflow_should_enter_safe_state()
     * (e2e.h) is the pure per-cause decision; rcp_mock_server_broadcast_
     * safe_state() (mock.h) is this test double's own actuator for
     * "every endpoint bound to the stream" -- see both functions' own doc
     * comments for the architectural background this closes. A stream_id
     * this srv cannot resolve to a configured request stream (resolve_index()
     * returning 0, e.g. no rcp_mock_server_set_request_stream_cfg() call
     * was ever made) broadcasts nothing -- the same fail-toward-no-action
     * disposition rcp_mock_server_broadcast_safe_state() itself already
     * documents for an unresolvable stream. */
    if (error == RCP_ERROR_REQUEST_STORAGE_OVERFLOW) {
        uint8_t overflow_stream_index = rcp_regmap_request_stream_cfg_resolve_index(
            srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
        if (overflow_stream_index != 0u &&
            rcp_e2e_overflow_should_enter_safe_state(
                srv->request_stream_cfg[overflow_stream_index - 1u].rx_ovrflw_safestate_enable)) {
            (void)rcp_mock_server_broadcast_safe_state(srv, overflow_stream_index);
        }
    }

    /* REQ-SEQ-013 (issue #335): a newly-admitted COMPOUND/COMPOUND_WAIT
     * step names a sequencer_index it will read or advance -- TC18
     * §12.7.10 Table 28's own access-control rule ("Request_stream_index
     * refers the Client Nr allowed to access this sequencer") applies to
     * this indirect path exactly as much as to a direct register-map
     * write, since a compound-wait request never touches the register
     * map at all. Checked here, right after admission, rather than
     * inside rcp_server_endpoint_admit() itself -- server.h has no
     * sequencer-table dependency of its own (the same "mechanism lives
     * below, context lives here" layering REQ-CANCEL-012's own
     * chain_group bookkeeping already established in this same
     * function's caller). An unauthorized step is admitted then
     * immediately cancelled rather than never admitted at all, since the
     * decode (and thus sequencer_index) is only known after
     * rcp_server_endpoint_admit() itself has already run.
     *
     * Deliberately skipped when the sequencer table itself is
     * unsupported (rcp_sequencer_table_unsupported(), count == 0) --
     * that is a distinct, already-established "compound operations
     * unsupported entirely" scenario (request_sequencer.h's own file
     * header) orthogonal to REQ-SEQ-013's own ownership concern, and a
     * request admitted before any sequencer table exists is expected to
     * stay validly PENDING (simply never becoming due) rather than being
     * newly rejected here -- conflating the two would move an existing,
     * separately-tested "no table configured yet" behavior onto this
     * access-control gate's own fail-closed default. */
    if (admit == RCP_SERVER_ADMIT_PENDING && !rcp_sequencer_table_unsupported(&srv->sequencers) &&
        (slot->queue.pending[admitted_index].kind == RCP_SCHED_KIND_COMPOUND ||
         slot->queue.pending[admitted_index].kind == RCP_SCHED_KIND_COMPOUND_WAIT)) {
        uint8_t sequencer_index = slot->queue.pending[admitted_index].compound.sequencer_index;
        uint8_t requester = rcp_regmap_request_stream_cfg_resolve_index(
            srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);

        if (!rcp_sequencer_access_permitted(&srv->sequencers, sequencer_index, requester)) {
            uint8_t tn = slot->queue.pending[admitted_index].transaction_num;

            (void)rcp_server_endpoint_cancel_single(&slot->queue, tn, RCP_CANCEL_LIFECYCLE_QUEUED);
            *out_response =
                rcp_acf_build_error_response(byte_bus_id, tn, RCP_ERROR_UNAUTHORIZED_ACCESS);
            return RCP_MOCK_DISPATCH_REJECTED;
        }
    }

    return finish_admission(slot, admit, request_type, request, request_len, error, byte_bus_id,
                             out_response);
}

//cfusa:req REQ-WDG-010
rcp_mock_dispatch_result_t rcp_mock_server_dispatch(rcp_mock_server_t *srv,
                                                     rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                     bool time_sync_supported, uint64_t stream_id,
                                                     const uint8_t *request, size_t request_len,
                                                     rcp_bytes_t *out_response)
{
    /* REQ-WDG-010: kicked unconditionally, before dispatch_plain()'s own
     * lifecycle/admission checks -- same "receipt, not validation" rule
     * and ordering as rcp_mock_server_dispatch_e2e()'s own identical
     * kick (see that function's doc comment for the full rationale). */
    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, stream_id);

    return dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                           stream_id, request, request_len, out_response);
}

//cfusa:req REQ-E2E-021
//cfusa:req REQ-E2E-031
//cfusa:req REQ-E2E-041
//cfusa:req REQ-WDG-010
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_e2e(rcp_mock_server_t *srv,
                                                          rcp_byte_bus_id_t byte_bus_id,
                                                          uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                          bool time_sync_supported,
                                                          uint64_t stream_id, uint32_t avtp_timestamp,
                                                          const uint8_t *request, size_t request_len,
                                                          rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_bytes_t                unwrapped;
    rcp_e2e_errc_t             unwrap_result;
    rcp_mock_dispatch_result_t result;

    memset(out_response, 0, sizeof(*out_response));

    /* REQ-WDG-010: kicked unconditionally, before any of the checks
     * below -- TC18 §12.7.7's own rule is about RECEIPT ("the watchdog
     * is reset with each request received from this RC Client"), not
     * successful validation or admission. A request this call goes on
     * to reject via plain-command-mode delegation, CRC mismatch, or
     * admission failure still means the RC Client is alive and talking
     * on this stream, which is the watchdog's own entire concern. */
    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, stream_id);

    /* REQ-E2E-021 (TC18 §12.7.7 Table 24, rx_enforce_e2e's "stream is
     * blocked until released" consequence): checked BEFORE
     * plain-command-mode delegation, CRC validation, or admission --
     * the block is a whole-STREAM property (this tracker is keyed by
     * stream_id, not byte_bus_id), so it applies uniformly regardless
     * of whether the addressed endpoint itself has req_crc_enable set.
     * *out_response carries a real Table 30 POCI_FAILURE error response
     * (the same code CRC_ERROR itself uses -- the block's own root
     * cause is a CRC failure) when the frame is at least long enough to
     * read a transaction_num back out of. */
    if (srv->stream_fault_tracker != NULL &&
        rcp_e2e_stream_fault_tracker_is_faulted(srv->stream_fault_tracker, stream_id)) {
        rcp_acf_byte_message_info_t hdr = {0};
        if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
            *out_response =
                rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num, RCP_ERROR_POCI_FAILURE);
        }
        return RCP_MOCK_DISPATCH_STREAM_FAULTED;
    }

    /* "plain command mode" (TC18 §13.6): an endpoint with req_crc_enable
     * not set, or an unknown byte_bus_id, is untouched by this function
     * -- delegate outright, including its own EP_NOT_FOUND handling.
     * Goes to dispatch_plain() directly, not the public dispatch()
     * wrapper -- this call's own watchdog kick already happened above,
     * unconditionally, before this branch was even reached; delegating
     * through the public wrapper would kick a second time for the same
     * received request (see dispatch_plain()'s own doc comment). */
    slot = find_slot(srv, byte_bus_id);
    if (!slot || !slot->req_crc_enable) {
        return dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                               stream_id, request, request_len, out_response);
    }

    unwrap_result = rcp_e2e_unwrap_framed(stream_id, avtp_subtype == RCP_AVTP_SUBTYPE_NTSCF,
                                           avtp_timestamp, request, request_len, &unwrapped);
    if (unwrap_result != RCP_E2E_OK) {
        /* Not executed, not even admitted -- TC18 §13.6. RCP_E2E_ERR_CRC_MISMATCH
         * still populates unwrapped (for diagnostic use, per rcp_e2e_unwrap()'s
         * own doc comment) so it must still be freed even though it is not
         * used as a request; RCP_E2E_ERR_SHORT_FRAME leaves it zeroed. */
        rcp_wire_error_t werr = rcp_e2e_wire_error(unwrap_result);
        if (werr != RCP_ERROR_NONE) {
            rcp_acf_byte_message_info_t hdr = {0};
            if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
                *out_response = rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num, werr);
            }
        }
        /* REQ-E2E-021: records this CRC error against stream_id's own
         * tracked fault state -- latches the whole stream faulted iff
         * slot->rx_enforce_e2e (this test double's own per-endpoint
         * stand-in for the real per-request-stream register bit; see
         * its own doc comment). A future request on this same stream_id
         * hits the check above before reaching this point again. */
        if (srv->stream_fault_tracker != NULL) {
            (void)rcp_e2e_stream_fault_tracker_on_crc_error(srv->stream_fault_tracker, stream_id,
                                                              slot->rx_enforce_e2e);
        }
        /* REQ-E2E-045 (issue #335): TC18 §12.7.7 Table 24's own
         * rx_enforce_e2e/rx_enforce_crc (0x000D.0) names TWO consequences
         * for a CRC failure at the SAME single bit -- "stream is blocked
         * until released" (the fault-tracker latch immediately above) AND
         * "Safe state will be entered" -- and unlike its wd/overflow/seq
         * siblings, that second consequence has no separate enable bit of
         * its own to gate it: it is rx_enforce_e2e's own value, evaluated
         * by rcp_e2e_crc_error_should_enter_safe_state() (e2e.h). "Safe
         * state" is stream-wide (every endpoint bound to stream_id, not
         * just the one slot this CRC mismatch was addressed to) --
         * rcp_mock_server_broadcast_safe_state() (mock.h) is this test
         * double's own actuator for that, resolved via the same
         * rcp_regmap_request_stream_cfg_resolve_index() call
         * rcp_mock_server_broadcast_safe_state()'s own overflow-cause
         * sibling (dispatch_plain(), above) already uses. slot->rx_enforce_e2e
         * (not the resolved stream's own request_stream_cfg entry) is
         * reused here deliberately, matching the fault-tracker latch call
         * immediately above it: both consequences of the SAME wire bit
         * are read from the SAME per-endpoint stand-in this function
         * already relies on for the CRC-mismatch path specifically, not
         * a second, potentially-inconsistent source. */
        {
            uint8_t crc_stream_index = rcp_regmap_request_stream_cfg_resolve_index(
                srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
            if (crc_stream_index != 0u &&
                rcp_e2e_crc_error_should_enter_safe_state(slot->rx_enforce_e2e)) {
                (void)rcp_mock_server_broadcast_safe_state(srv, crc_stream_index);
            }
        }
        rcp_bytes_free(&unwrapped);
        return RCP_MOCK_DISPATCH_CRC_ERROR;
    }

    /* CRC validated: dispatch the unwrapped header-and-payload region
     * (acf_msg_length already adapted back down) exactly as
     * rcp_mock_server_dispatch() would have dispatched request itself --
     * via dispatch_plain() directly, same already-kicked-once reasoning
     * as the delegation branch above. */
    result = dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                             stream_id, unwrapped.data, unwrapped.len, out_response);
    rcp_bytes_free(&unwrapped);
    return result;
}

//cfusa:req REQ-MOCK-016
//cfusa:req REQ-MOCK-017
//cfusa:req REQ-MOCK-018
bool rcp_mock_server_drain_endpoint(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                     rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    rcp_bytes_t                frame = {0};

    memset(out_response, 0, sizeof(*out_response));
    if (!slot) return false;

    if (!rcp_server_endpoint_drain_one(&slot->queue, &frame)) return false;

    run_handler(slot, frame.data, frame.len, out_response);
    rcp_bytes_free(&frame);
    return true;
}

/* Decodes just member[0..member_len)'s shared byte_message_info header
 * far enough to learn its byte_bus_id, without caring whether it is
 * ACF_ABB or ACF_GBB. Returns true and sets *out_byte_bus_id on success;
 * false (leaving *out_byte_bus_id untouched) if member does not decode as
 * either variant -- see rcp_mock_server_dispatch_frame()'s own doc
 * comment for how that outcome is surfaced. */
static bool peek_member_byte_bus_id(const uint8_t *member, size_t member_len, uint8_t *out_msg_type,
                                     rcp_byte_bus_id_t *out_byte_bus_id)
{
    if (rcp_acf_peek_msg_type(member, member_len, out_msg_type) != RCP_ACF_OK) return false;

    if (*out_msg_type == RCP_ACF_MSG_TYPE_ABB) {
        rcp_acf_byte_message_info_t hdr;
        const uint8_t              *payload;
        size_t                       payload_len;

        if (rcp_acf_decode_abb(member, member_len, &hdr, &payload, &payload_len) != RCP_ACF_OK) {
            return false;
        }
        *out_byte_bus_id = hdr.byte_bus_id;
        return true;
    }

    if (*out_msg_type == RCP_ACF_MSG_TYPE_GBB) {
        rcp_acf_gbb_header_t  hdr;
        const uint8_t        *payload;
        size_t                 payload_len;

        if (rcp_acf_decode_gbb(member, member_len, &hdr, &payload, &payload_len) != RCP_ACF_OK) {
            return false;
        }
        *out_byte_bus_id = hdr.info.byte_bus_id;
        return true;
    }

    return false;
}

/* True iff member[0..member_len) is a chained request; on true,
 * *out_cs receives its own conditional-start selector and *out_tn its own
 * transaction_num -- needed by CHAIN_ERROR/CHAIN_ABORTED error-response
 * construction (TC18 §12.9.6), which this function's own decode call
 * already has on hand and would otherwise have to be re-derived. */
static bool is_chained_member(const uint8_t *member, size_t member_len, uint8_t *out_cs,
                               uint8_t *out_tn)
{
    uint8_t           request_type = 0;
    rcp_byte_bus_id_t bus;
    uint16_t          exec_delay;
    const uint8_t    *payload;
    size_t            payload_len;

    if (rcp_compound_peek_request_type(member, member_len, &request_type) != RCP_COMPOUND_OK) {
        return false;
    }
    if (request_type != RCP_REQUEST_TYPE_CHAINED) return false;

    if (rcp_chained_decode_member(member, member_len, &bus, &exec_delay, out_cs, &payload,
                                   &payload_len, out_tn) != RCP_CHAINED_OK) {
        /* Claims to be chained but does not decode as one -- not treated
         * as a chain member here; rcp_mock_server_dispatch() will reject
         * it on its own for the same reason. */
        return false;
    }
    return true;
}

/* The store index of the most recently admitted request on ep -- the one
 * rcp_server_endpoint_admit() just placed, identified by its highest
 * sequence number. Returns RCP_SERVER_MAX_PENDING if the store is empty. */
static size_t last_pending_index(const rcp_server_endpoint_t *ep)
{
    size_t   i;
    size_t   best  = RCP_SERVER_MAX_PENDING;
    uint64_t max_seq = 0;

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        if (!ep->pending[i].in_use) continue;
        if (best == RCP_SERVER_MAX_PENDING || ep->pending[i].sequence >= max_seq) {
            max_seq = ep->pending[i].sequence;
            best    = i;
        }
    }
    return best;
}

//cfusa:req REQ-MOCK-019
//cfusa:req REQ-MOCK-020
//cfusa:req REQ-MOCK-029
size_t rcp_mock_server_dispatch_frame(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                       bool time_sync_supported, uint64_t stream_id,
                                       const uint8_t *frame, size_t frame_len,
                                       rcp_mock_frame_member_result_t *out_results,
                                       size_t out_cap)
{
    size_t offsets[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t real_count;
    size_t stored_count; /* how many of offsets[] rcp_sched_split_frame_members()
                             actually wrote -- may be < real_count */
    size_t process_count;
    size_t i;
    size_t dispatched = 0;
    /* Chain sequencing state, carried across this frame's members in
     * order -- see request_chained.h's rcp_chained_advance(). */
    bool   chain_aborted = false;
    bool   prev_errored  = false;
    uint8_t cs           = 0;
    /* REQ-CANCEL-012 (issue #334): chain-group/position bookkeeping, the
     * same "properties of the enclosing frame, not of any member's own
     * sub-fields" scope server.h's own rcp_server_pending_t.chain_group
     * doc comment describes -- this loop is where frame order is known,
     * so it is where these values are derived. chain_group == 0 is the
     * "not part of a chain" sentinel; every member (chained or not)
     * starts a fresh potential chain_group == i+1 (the +1 avoids
     * colliding with the sentinel when i==0) unless it is itself chained,
     * in which case it keeps its predecessor's own chain_group and
     * advances chain_position by one. */
    uint32_t chain_group    = 0;
    size_t   chain_position = 0;

    real_count = rcp_sched_split_frame_members(frame, frame_len, offsets, RCP_MOCK_MAX_FRAME_MEMBERS);
    if (real_count == 0) return 0;

    stored_count = (real_count < RCP_MOCK_MAX_FRAME_MEMBERS) ? real_count : RCP_MOCK_MAX_FRAME_MEMBERS;
    process_count = (stored_count < out_cap) ? stored_count : out_cap;

    for (i = 0; i < process_count; i++) {
        size_t                           member_off;
        size_t                           member_end;
        size_t                           member_len;
        const uint8_t                   *member;
        uint8_t                          msg_type    = 0;
        rcp_byte_bus_id_t                byte_bus_id = 0;
        rcp_mock_frame_member_result_t  *out         = &out_results[dispatched];
        bool                              chained_flag;
        uint8_t                           member_tn   = 0;

        member_off = offsets[i];
        if (i + 1 < stored_count) {
            member_end = offsets[i + 1];
        } else if (i + 1 == real_count) {
            /* i is genuinely the last member in the whole frame. */
            member_end = frame_len;
        } else {
            /* real_count exceeds RCP_MOCK_MAX_FRAME_MEMBERS and i is the
             * last offset rcp_sched_split_frame_members() had room to
             * store: there is no reliable way to know where this member
             * ends without its successor's own offset, so stop here
             * rather than guess (and possibly swallow the remainder of
             * the frame into one oversized "member"). */
            break;
        }

        member_len = member_end - member_off;
        member     = &frame[member_off];

        if (!peek_member_byte_bus_id(member, member_len, &msg_type, &byte_bus_id)) {
            out->result      = RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
            out->byte_bus_id = 0;
            memset(&out->response, 0, sizeof(out->response));
            prev_errored     = true;
            dispatched++;
            continue;
        }

        out->byte_bus_id = byte_bus_id;

        /* A chained member's execution condition is its *predecessor*
         * within this same frame -- a chain never spans AVTPDUs, and a
         * member's position in the chain is its position in the frame,
         * not a sub-field of its own. So the chain decision has to be
         * made here, where frame order is known, rather than inside the
         * per-endpoint store. */
        chained_flag = is_chained_member(member, member_len, &cs, &member_tn);

        if (!chained_flag) {
            chain_group    = (uint32_t)i + 1u;
            chain_position = 0;
        } else {
            chain_position++;
        }

        if (chained_flag) {
            rcp_chained_member_outcome_t outcome =
                rcp_chained_advance(&chain_aborted, i > 0, prev_errored, cs);

            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ERROR) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ERROR;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ERROR);
                prev_errored  = true;
                dispatched++;
                continue;
            }
            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ABORTED) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ABORTED;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ABORTED);
                prev_errored  = true;
                dispatched++;
                continue;
            }
        }

        out->result      = rcp_mock_server_dispatch(srv, byte_bus_id, avtp_subtype, msg_type,
                                                      time_sync_supported, stream_id, member,
                                                      member_len, &out->response);

        /* What "the predecessor errored" means for the next member: a
         * member that never reached its endpoint at all (unknown bus,
         * rejected, dropped) counts as an error for chaining purposes;
         * one that ran, queued, or was stored does not. */
        prev_errored = (out->result == RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS ||
                        out->result == RCP_MOCK_DISPATCH_REJECTED ||
                        out->result == RCP_MOCK_DISPATCH_DROPPED);

        /* A chained member accepted into its endpoint's store has its
         * predecessor behind it already, so its chain_exec_delay timer
         * starts now. REQ-CANCEL-012: this same PENDING entry also
         * records its own chain_group/chain_position -- unconditionally,
         * exactly like the chain_predecessor_done() call below, which is
         * itself a no-op for a non-chained entry (see that function's
         * own doc comment); a standalone (non-chained) member that lands
         * here is its own chain's own anchor, tagged chain_position 0,
         * ready to cascade to any actual successors admitted after it. */
        if (out->result == RCP_MOCK_DISPATCH_PENDING) {
            rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
            if (slot) {
                size_t last = last_pending_index(&slot->queue);
                (void)rcp_server_endpoint_chain_predecessor_done(&slot->queue, last, 0u);
                if (last < RCP_SERVER_MAX_PENDING) {
                    slot->queue.pending[last].chain_group    = chain_group;
                    slot->queue.pending[last].chain_position = (uint8_t)chain_position;
                }
            }
        }

        dispatched++;
    }

    return dispatched;
}

//cfusa:req REQ-E2E-033
size_t rcp_mock_server_dispatch_frame_e2e(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                           bool time_sync_supported, uint64_t stream_id,
                                           uint32_t avtp_timestamp, const uint8_t *frame,
                                           size_t frame_len,
                                           rcp_mock_frame_member_result_t *out_results,
                                           size_t out_cap)
{
    size_t offsets[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t real_count;
    size_t stored_count;
    size_t process_count;
    size_t i;
    size_t dispatched = 0;
    bool   chain_aborted = false;
    bool   prev_errored  = false;
    uint8_t cs           = 0;
    /* REQ-CANCEL-012 (issue #334): same chain-group/position bookkeeping
     * as rcp_mock_server_dispatch_frame()'s own identical local state --
     * see that function's own comment for the full rationale. */
    uint32_t chain_group    = 0;
    size_t   chain_position = 0;

    real_count = rcp_sched_split_frame_members(frame, frame_len, offsets, RCP_MOCK_MAX_FRAME_MEMBERS);
    if (real_count == 0) return 0;

    stored_count = (real_count < RCP_MOCK_MAX_FRAME_MEMBERS) ? real_count : RCP_MOCK_MAX_FRAME_MEMBERS;
    process_count = (stored_count < out_cap) ? stored_count : out_cap;

    for (i = 0; i < process_count; i++) {
        size_t                           member_off;
        size_t                           member_end;
        size_t                           member_len;
        const uint8_t                   *member;
        uint8_t                          msg_type    = 0;
        rcp_byte_bus_id_t                byte_bus_id = 0;
        rcp_mock_frame_member_result_t  *out         = &out_results[dispatched];
        bool                              chained_flag;
        uint8_t                           member_tn   = 0;

        member_off = offsets[i];
        if (i + 1 < stored_count) {
            member_end = offsets[i + 1];
        } else if (i + 1 == real_count) {
            member_end = frame_len;
        } else {
            break;
        }

        member_len = member_end - member_off;
        member     = &frame[member_off];

        if (!peek_member_byte_bus_id(member, member_len, &msg_type, &byte_bus_id)) {
            out->result      = RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
            out->byte_bus_id = 0;
            memset(&out->response, 0, sizeof(out->response));
            prev_errored     = true;
            dispatched++;
            continue;
        }

        out->byte_bus_id = byte_bus_id;

        chained_flag = is_chained_member(member, member_len, &cs, &member_tn);

        if (!chained_flag) {
            chain_group    = (uint32_t)i + 1u;
            chain_position = 0;
        } else {
            chain_position++;
        }

        if (chained_flag) {
            rcp_chained_member_outcome_t outcome =
                rcp_chained_advance(&chain_aborted, i > 0, prev_errored, cs);

            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ERROR) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ERROR;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ERROR);
                prev_errored  = true;
                dispatched++;
                continue;
            }
            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ABORTED) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ABORTED;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ABORTED);
                prev_errored  = true;
                dispatched++;
                continue;
            }
        }

        /* The one real difference from rcp_mock_server_dispatch_frame():
         * each member is independently unwrapped-and-verified against
         * its own CRC32 (if the addressed endpoint has req_crc_enable
         * set) via rcp_mock_server_dispatch_e2e() -- TC18 §13.6's "a
         * separate CRC32... for each E2E-protected ACF message"
         * (REQ-E2E-033), never one CRC across the whole frame. */
        out->result      = rcp_mock_server_dispatch_e2e(srv, byte_bus_id, avtp_subtype, msg_type,
                                                          time_sync_supported, stream_id,
                                                          avtp_timestamp, member, member_len,
                                                          &out->response);

        /* A member whose CRC failed never reached its endpoint's store
         * either -- same chaining-error treatment as unknown-bus/
         * rejected/dropped. */
        prev_errored = (out->result == RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS ||
                        out->result == RCP_MOCK_DISPATCH_REJECTED ||
                        out->result == RCP_MOCK_DISPATCH_DROPPED ||
                        out->result == RCP_MOCK_DISPATCH_CRC_ERROR);

        /* REQ-CANCEL-012: see rcp_mock_server_dispatch_frame()'s own
         * identical block for the full rationale. */
        if (out->result == RCP_MOCK_DISPATCH_PENDING) {
            rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
            if (slot) {
                size_t last = last_pending_index(&slot->queue);
                (void)rcp_server_endpoint_chain_predecessor_done(&slot->queue, last, 0u);
                if (last < RCP_SERVER_MAX_PENDING) {
                    slot->queue.pending[last].chain_group    = chain_group;
                    slot->queue.pending[last].chain_position = (uint8_t)chain_position;
                }
            }
        }

        dispatched++;
    }

    return dispatched;
}

/* ── Conditional-request execution (TC18 §11.2.2) ─────────────────────────── */

//cfusa:req REQ-MOCK-022
//cfusa:req REQ-RMAP-028
bool rcp_mock_server_set_sequencer_count(rcp_mock_server_t *srv, uint16_t count)
{
    bool     ok;
    uint16_t actual;

    rcp_sequencer_table_free(&srv->sequencers);
    srv->sequencers = rcp_sequencer_table_new(count);
    ok = count == 0u || srv->sequencers.state != NULL;
    /* Sync from srv->sequencers.count (the table's own ACTUAL size),
     * never the raw count argument -- on an allocation failure for a
     * nonzero count, rcp_sequencer_table_new() returns a zeroed table
     * (count=0) per its own doc comment, and the register field must
     * reflect that same "unsupported" reality, not the caller's
     * unmet request. Mirrors rcp_mock_server_transition()'s own
     * "sync from the authoritative post-call value" convention
     * (REQ-RMAP-023). svr_sequencers_max (REQ-RMAP-028) is 8 bit on the
     * wire -- an actual count this test double's own uint16_t API could
     * in principle produce but the real register could never hold is
     * capped at the register's own representable maximum (0xFF), never
     * silently truncated/wrapped, so the recorded value is never
     * smaller than the truth. */
    actual = srv->sequencers.count;
    srv->regmap.svr_sequencers_max = (actual > 0xFFu) ? (uint8_t)0xFFu : (uint8_t)actual;
    return ok;
}

//cfusa:req REQ-MOCK-022
rcp_sequencer_table_t *rcp_mock_server_sequencers(rcp_mock_server_t *srv)
{
    return &srv->sequencers;
}

//cfusa:req REQ-MOCK-023
//cfusa:req REQ-MOCK-024
bool rcp_mock_server_tick(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                           const rcp_server_tick_ctx_t *ctx, rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_server_tick_ctx_t     local;
    size_t                    index = 0;

    memset(out_response, 0, sizeof(*out_response));

    slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    /* The sequencer table is the server's, never the caller's. */
    local            = *ctx;
    local.sequencers = &srv->sequencers;

    if (!rcp_server_endpoint_select_due(&slot->queue, &local, &index)) return false;

    /* Run the selected request's own stored frame through the endpoint's
     * handler -- the identical execution path a standard request takes. */
    run_handler(slot, slot->queue.pending[index].frame.data, slot->queue.pending[index].frame.len,
                 out_response);

    (void)rcp_server_endpoint_complete(&slot->queue, index, &local);
    return true;
}

//cfusa:req REQ-MOCK-025
size_t rcp_mock_server_notify_trigger(rcp_mock_server_t *srv, uint8_t source_ep,
                                       uint8_t signal_nr)
{
    size_t i;
    size_t matched = 0;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (!srv->endpoints[i].in_use) continue;
        matched += rcp_server_endpoint_notify_trigger(&srv->endpoints[i].queue, source_ep,
                                                       signal_nr);
    }
    return matched;
}

//cfusa:req REQ-MOCK-027
size_t rcp_mock_server_pending_count(const rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    const rcp_mock_endpoint_slot_t *slot = find_slot_const(srv, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_pending_count(&slot->queue);
}

//cfusa:req REQ-MOCK-026
size_t rcp_mock_server_watchdog_purge(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_watchdog_purge(&slot->queue);
}

//cfusa:req REQ-E2E-029
//cfusa:req REQ-E2E-030
//cfusa:req REQ-E2E-045
size_t rcp_mock_server_broadcast_safe_state(rcp_mock_server_t *srv, uint8_t request_stream_index)
{
    rcp_byte_bus_id_t bound[RCP_MOCK_MAX_ENDPOINTS];
    size_t             total_bound;
    size_t             purged = 0;
    size_t             i;

    if (request_stream_index == 0u) return 0;

    total_bound = rcp_regmap_ep_id_map_byte_bus_ids_for_stream(
        srv->ep_id_map, srv->ep_id_map_count, request_stream_index, bound,
        RCP_MOCK_MAX_ENDPOINTS);
    /* This test double never registers more than RCP_MOCK_MAX_ENDPOINTS
     * live slots (RCP_MOCK_ERR_CAPACITY, rcp_mock_server_add_endpoint()),
     * so a bound byte_bus_id beyond that many distinct values can never
     * name a slot this srv actually holds -- the "ask first" total isn't
     * separately re-scanned here for that reason. */
    if (total_bound > RCP_MOCK_MAX_ENDPOINTS) total_bound = RCP_MOCK_MAX_ENDPOINTS;

    for (i = 0; i < total_bound; i++) {
        rcp_mock_endpoint_slot_t *slot = find_slot(srv, bound[i]);
        if (!slot) continue; /* bound in EP_ID_config, not (or no longer) registered */
        purged += rcp_server_endpoint_watchdog_purge(&slot->queue);
    }

    return purged;
}
