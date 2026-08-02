/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/mock.h"

#include "rcp/acf.h"
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
} rcp_mock_endpoint_slot_t;

/* ── The server double ─────────────────────────────────────────────────────── */

struct rcp_mock_server {
    rcp_lifecycle_state_t    state;
    rcp_regmap_general_t     regmap;
    rcp_mock_endpoint_slot_t endpoints[RCP_MOCK_MAX_ENDPOINTS];
    size_t                   endpoint_count;
    /* The sequencer-state registers compound/compound-wait requests read
     * and advance. Server-wide rather than per-endpoint: a sequencer is a
     * server register, and requests on different endpoints routinely
     * drive the same one. */
    rcp_sequencer_table_t    sequencers;
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
rcp_lifecycle_errc_t rcp_mock_server_transition(rcp_mock_server_t *srv,
                                                 rcp_lifecycle_state_t target,
                                                 const rcp_lifecycle_plausibility_snapshot_t *snap)
{
    return rcp_lifecycle_transition(&srv->state, target, snap);
}

//cfusa:req REQ-MOCK-006
rcp_regmap_general_t *rcp_mock_server_regmap(rcp_mock_server_t *srv)
{
    return &srv->regmap;
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
/* Applies an already-classified cancellation request against slot's own
 * request store. Which of the three cancellation opcodes it is decides
 * what gets removed; a clear-single additionally needs its target
 * transaction_num decoded out of the message. */
static void apply_cancellation(rcp_mock_endpoint_slot_t *slot, uint8_t request_type,
                                const uint8_t *request, size_t request_len)
{
    rcp_byte_bus_id_t bus;
    uint8_t           target_tn;
    uint8_t           tn;

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
            /* Every request still sitting in the store is by definition
             * queued rather than under execution -- this dispatcher runs a
             * selected request to completion synchronously inside
             * rcp_mock_server_tick(), so nothing is ever observably
             * mid-execution here. */
            (void)rcp_server_endpoint_cancel_single(&slot->queue, target_tn,
                                                     RCP_CANCEL_LIFECYCLE_QUEUED);
        }
        break;

    default:
        break;
    }
}

/* Maps one server.h admission outcome onto this module's own dispatch
 * result, running the endpoint's handler for the execute-now case. */
static rcp_mock_dispatch_result_t finish_admission(rcp_mock_endpoint_slot_t *slot,
                                                    rcp_server_admit_t admit, uint8_t request_type,
                                                    const uint8_t *request, size_t request_len,
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
        apply_cancellation(slot, request_type, request, request_len);
        return RCP_MOCK_DISPATCH_CANCELLED;
    case RCP_SERVER_ADMIT_REJECTED:
    default:
        return RCP_MOCK_DISPATCH_REJECTED;
    }
}

//cfusa:req REQ-MOCK-021
rcp_mock_dispatch_result_t rcp_mock_server_dispatch(rcp_mock_server_t *srv,
                                                     rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                     bool time_sync_supported,
                                                     const uint8_t *request, size_t request_len,
                                                     rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_server_admit_t        admit;
    uint8_t                   request_type = 0;

    memset(out_response, 0, sizeof(*out_response));

    if (!rcp_lifecycle_should_accept(srv->state, time_sync_supported, avtp_subtype, acf_msg_type,
                                      byte_bus_id)) {
        return RCP_MOCK_DISPATCH_DROPPED;
    }

    slot = find_slot(srv, byte_bus_id);
    if (!slot) return RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;

    /* The request_type-aware routing decision lives in server.h: a
     * standard request keeps the original submit-or-queue behavior, a
     * conditional one is decoded and stored, a cancellation one is
     * reported back for this module to apply. Admission carries no tick of
     * its own (0): a stored request's exec_delay is measured from the
     * moment its own start condition first holds, which is decided later
     * by rcp_server_endpoint_select_due() against the caller's tick. */
    admit = rcp_server_endpoint_admit(&slot->queue, request, request_len, 0u, &request_type, NULL);
    return finish_admission(slot, admit, request_type, request, request_len, out_response);
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
 * *out_cs receives its own conditional-start selector. */
static bool is_chained_member(const uint8_t *member, size_t member_len, uint8_t *out_cs)
{
    uint8_t           request_type = 0;
    rcp_byte_bus_id_t bus;
    uint16_t          exec_delay;
    const uint8_t    *payload;
    size_t            payload_len;
    uint8_t           tn;

    if (rcp_compound_peek_request_type(member, member_len, &request_type) != RCP_COMPOUND_OK) {
        return false;
    }
    if (request_type != RCP_REQUEST_TYPE_CHAINED) return false;

    if (rcp_chained_decode_member(member, member_len, &bus, &exec_delay, out_cs, &payload,
                                   &payload_len, &tn) != RCP_CHAINED_OK) {
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
size_t rcp_mock_server_dispatch_frame(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                       bool time_sync_supported, const uint8_t *frame,
                                       size_t frame_len,
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
        if (is_chained_member(member, member_len, &cs)) {
            rcp_chained_member_outcome_t outcome =
                rcp_chained_advance(&chain_aborted, i > 0, prev_errored, cs);

            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ERROR) {
                out->result = RCP_MOCK_DISPATCH_CHAIN_ERROR;
                memset(&out->response, 0, sizeof(out->response));
                prev_errored = true;
                dispatched++;
                continue;
            }
            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ABORTED) {
                out->result = RCP_MOCK_DISPATCH_CHAIN_ABORTED;
                memset(&out->response, 0, sizeof(out->response));
                prev_errored = true;
                dispatched++;
                continue;
            }
        }

        out->result      = rcp_mock_server_dispatch(srv, byte_bus_id, avtp_subtype, msg_type,
                                                      time_sync_supported, member, member_len,
                                                      &out->response);

        /* What "the predecessor errored" means for the next member: a
         * member that never reached its endpoint at all (unknown bus,
         * rejected, dropped) counts as an error for chaining purposes;
         * one that ran, queued, or was stored does not. */
        prev_errored = (out->result == RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS ||
                        out->result == RCP_MOCK_DISPATCH_REJECTED ||
                        out->result == RCP_MOCK_DISPATCH_DROPPED);

        /* A chained member accepted into its endpoint's store has its
         * predecessor behind it already, so its chain_exec_delay timer
         * starts now. */
        if (out->result == RCP_MOCK_DISPATCH_PENDING) {
            rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
            if (slot) {
                size_t last = last_pending_index(&slot->queue);
                (void)rcp_server_endpoint_chain_predecessor_done(&slot->queue, last, 0u);
            }
        }

        dispatched++;
    }

    return dispatched;
}

/* ── Conditional-request execution (TC18 §11.2.2) ─────────────────────────── */

//cfusa:req REQ-MOCK-022
bool rcp_mock_server_set_sequencer_count(rcp_mock_server_t *srv, uint16_t count)
{
    rcp_sequencer_table_free(&srv->sequencers);
    srv->sequencers = rcp_sequencer_table_new(count);
    return count == 0u || srv->sequencers.state != NULL;
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
