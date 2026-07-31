/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/mock.h"

#include "rcp/acf.h"
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
rcp_mock_dispatch_result_t rcp_mock_server_dispatch(rcp_mock_server_t *srv,
                                                     rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                     bool time_sync_supported,
                                                     const uint8_t *request, size_t request_len,
                                                     rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;

    memset(out_response, 0, sizeof(*out_response));

    if (!rcp_lifecycle_should_accept(srv->state, time_sync_supported, avtp_subtype, acf_msg_type,
                                      byte_bus_id)) {
        return RCP_MOCK_DISPATCH_DROPPED;
    }

    slot = find_slot(srv, byte_bus_id);
    if (!slot) return RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;

    if (rcp_server_endpoint_submit(&slot->queue, request, request_len)) {
        run_handler(slot, request, request_len, out_response);
        return RCP_MOCK_DISPATCH_OK;
    }
    return RCP_MOCK_DISPATCH_QUEUED;
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
            dispatched++;
            continue;
        }

        out->byte_bus_id = byte_bus_id;
        out->result      = rcp_mock_server_dispatch(srv, byte_bus_id, avtp_subtype, msg_type,
                                                      time_sync_supported, member, member_len,
                                                      &out->response);
        dispatched++;
    }

    return dispatched;
}
