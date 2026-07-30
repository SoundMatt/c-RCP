/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/mock.h"

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
