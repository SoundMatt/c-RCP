/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/request_compound.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/avtp.c's/
 * wire.c's house convention of not sharing a byte-order util across
 * modules) ──────────────────────────────────────────────────────────────── */

static void put_u64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)((v >> 56) & 0xFFu);
    p[1] = (uint8_t)((v >> 48) & 0xFFu);
    p[2] = (uint8_t)((v >> 40) & 0xFFu);
    p[3] = (uint8_t)((v >> 32) & 0xFFu);
    p[4] = (uint8_t)((v >> 24) & 0xFFu);
    p[5] = (uint8_t)((v >> 16) & 0xFFu);
    p[6] = (uint8_t)((v >> 8) & 0xFFu);
    p[7] = (uint8_t)(v & 0xFFu);
}

/* ── request_type classification ─────────────────────────────────────────── */

//cfusa:req REQ-CMP-001
bool rcp_request_type_is_safety(uint8_t request_type)
{
    return (request_type & 0x80u) != 0u;
}

//cfusa:req REQ-CMP-002
bool rcp_request_type_is_compound(uint8_t request_type)
{
    return request_type == RCP_REQUEST_TYPE_COMPOUND ||
           request_type == RCP_REQUEST_TYPE_COMPOUND_SAFETY;
}

//cfusa:req REQ-CMP-003
bool rcp_request_type_is_compound_wait(uint8_t request_type)
{
    return request_type == RCP_REQUEST_TYPE_COMPOUND_WAIT ||
           request_type == RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY;
}

/* ── Errors ─────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-CMP-004
const char *rcp_compound_strerror(rcp_compound_errc_t e)
{
    switch (e) {
    case RCP_COMPOUND_OK:                 return "rcp/compound: success";
    case RCP_COMPOUND_ERR_SHORT_FRAME:    return "rcp/compound: frame too short";
    case RCP_COMPOUND_ERR_BAD_MSG_TYPE:   return "rcp/compound: unexpected ACF message type";
    case RCP_COMPOUND_ERR_NOT_REPURPOSED: return "rcp/compound: message_timestamp not repurposed";
    case RCP_COMPOUND_ERR_UNKNOWN_TYPE:   return "rcp/compound: unrecognized request_type";
    case RCP_COMPOUND_ERR_RESERVED_NONZERO:
        return "rcp/compound: reserved sub-field octet is not zero";
    case RCP_COMPOUND_ERR_EVT_HS_CS_NONZERO:
        return "rcp/compound: evt[2:0], hs, or cs is not zero";
    default:                              return "rcp/compound: unknown error";
    }
}

/* ── Dispatch ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-CMP-005
//cfusa:req REQ-CMP-006
//cfusa:req REQ-CMP-007
rcp_compound_errc_t rcp_compound_peek_request_type(const uint8_t *b, size_t len,
                                                    uint8_t *out_request_type)
{
    uint8_t msg_type;
    uint8_t mtv;

    if (len < RCP_ACF_GBB_HEADER_LEN) return RCP_COMPOUND_ERR_SHORT_FRAME;

    msg_type = (uint8_t)(b[0] >> 1); /* acf_msg_type: octet 0 bits 7:1 */
    if (msg_type != RCP_ACF_MSG_TYPE_GBB) return RCP_COMPOUND_ERR_BAD_MSG_TYPE;

    mtv = (uint8_t)((b[2] >> 5) & 0x1u); /* mtv: octet 2 bit 5 */
    if (mtv != (uint8_t)RCP_ACF_MTV_UNTIMED) return RCP_COMPOUND_ERR_NOT_REPURPOSED;

    *out_request_type = b[RCP_ACF_ABB_HEADER_LEN];
    return RCP_COMPOUND_OK;
}

/* ── rcp_compound_step_t <-> the repurposed 8-byte message_timestamp region ─── */

/* Octet offsets within the repurposed 8-byte message_timestamp region --
 * see request_compound.h's "wire sub-field layout" section:
 *   0 request_type | 1 start_state | 2 next_state | 3 sequencer_index |
 *   4..5 exec_delay (BE) | 6..7 repeat_count (BE) */
static uint64_t pack_ts(uint8_t request_type, const rcp_compound_step_t *step)
{
    return (((uint64_t)request_type) << 56) |
           (((uint64_t)step->start_state) << 48) |
           (((uint64_t)step->next_state) << 40) |
           (((uint64_t)step->sequencer_index) << 32) |
           (((uint64_t)step->exec_delay) << 16) |
           ((uint64_t)step->repeat_count);
}

static void unpack_ts(uint64_t ts, rcp_compound_step_t *out_step)
{
    out_step->start_state     = (uint8_t)((ts >> 48) & 0xFFu);
    out_step->next_state      = (uint8_t)((ts >> 40) & 0xFFu);
    out_step->sequencer_index = (uint8_t)((ts >> 32) & 0xFFu);
    out_step->exec_delay      = (uint16_t)((ts >> 16) & 0xFFFFu);
    out_step->repeat_count    = (uint16_t)(ts & 0xFFFFu);
}

/* ── Shared raw-header ACF_GBB builder (mtv=0, repurposed timestamp) ─────── */

/* Builds a complete ACF_GBB frame with mtv=RCP_ACF_MTV_UNTIMED(0) and the
 * message_timestamp region set to the caller-supplied ts (this module's
 * whole reason for going around rcp_acf_encode_gbb() -- see the file
 * header), using acf.h's shared rcp_acf_pack_header()/rcp_acf_pad_len()
 * so the bit layout and quadlet/pad accounting stay in lockstep with
 * every other module that builds an ACF header. op=RCP_ACF_OP_NONE,
 * ms=0, hs=0, rsp=0, err=0 -- this request kind carries no read/write data
 * operation of its own. evt is caller-supplied: a compound-wait request
 * uses it to select its §13.5.1 comparison mode (see
 * rcp_compound_encode_request()'s own doc comment); every other caller
 * passes 0. Returns a zeroed rcp_bytes_t
 * (data=NULL) if payload_len exceeds RCP_ACF_GBB_MAX_PAYLOAD or on
 * allocation failure. */
static rcp_bytes_t encode_gbb_repurposed(rcp_byte_bus_id_t byte_bus_id, uint8_t cs, uint8_t evt,
                                          uint8_t transaction_num, uint64_t ts,
                                          const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  info  = {0};
    size_t                       unpadded, total;
    uint16_t                     quadlets;
    uint8_t                      pad;
    uint8_t                     *b;

    if (payload_len > RCP_ACF_GBB_MAX_PAYLOAD) return frame;

    unpadded = RCP_ACF_GBB_HEADER_LEN + payload_len;
    pad      = rcp_acf_pad_len(unpadded);
    total    = unpadded + pad;
    quadlets = (uint16_t)(total / 4u);

    b = (uint8_t *)malloc(total);
    if (!b) return frame;

    info.byte_bus_id     = byte_bus_id;
    info.cs               = cs;
    info.evt               = evt;
    info.transaction_num  = transaction_num;
    info.pad               = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &info);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

/* ── Compound / compound-wait request encode/decode ──────────────────────────── */

//cfusa:req REQ-CMP-008
//cfusa:req REQ-CMP-009
//cfusa:req REQ-CMP-010
//cfusa:req REQ-CMP-026
rcp_bytes_t rcp_compound_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                         const rcp_compound_step_t *step, uint8_t evt,
                                         uint8_t transaction_num,
                                         const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};

    if (!rcp_request_type_is_compound(request_type) &&
        !rcp_request_type_is_compound_wait(request_type)) {
        return frame;
    }

    return encode_gbb_repurposed(byte_bus_id, 0u, evt, transaction_num,
                                  pack_ts(request_type, step), payload, payload_len);
}

//cfusa:req REQ-CMP-011
//cfusa:req REQ-CMP-012
//cfusa:req REQ-CMP-013
//cfusa:req REQ-CMP-014
//cfusa:req REQ-CMP-015
//cfusa:req REQ-CMP-027
rcp_compound_errc_t rcp_compound_decode_request(const uint8_t *b, size_t len,
                                                 uint8_t *out_request_type,
                                                 rcp_byte_bus_id_t *out_byte_bus_id,
                                                 rcp_compound_step_t *out_step,
                                                 uint8_t *out_evt,
                                                 const uint8_t **out_payload, size_t *out_payload_len,
                                                 uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, out_payload, out_payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_COMPOUND_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_COMPOUND_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_COMPOUND_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (!rcp_request_type_is_compound(rt) && !rcp_request_type_is_compound_wait(rt)) {
        return RCP_COMPOUND_ERR_UNKNOWN_TYPE;
    }

    unpack_ts(hdr.message_timestamp, out_step);

    *out_request_type    = rt;
    *out_byte_bus_id      = hdr.info.byte_bus_id;
    *out_evt              = hdr.info.evt;
    *out_transaction_num  = hdr.info.transaction_num;
    return RCP_COMPOUND_OK;
}

/* ── clear-non-safestate (0x06) ──────────────────────────────────────────────── */

//cfusa:req REQ-CMP-016
rcp_bytes_t rcp_compound_encode_clear_non_safestate(rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t transaction_num)
{
    uint64_t ts = ((uint64_t)RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE) << 56;

    return encode_gbb_repurposed(byte_bus_id, 0u, 0u, transaction_num, ts, NULL, 0);
}

//cfusa:req REQ-CMP-017
//cfusa:req REQ-CMP-018
//cfusa:req REQ-CMP-028
//cfusa:req REQ-CMP-029
rcp_compound_errc_t rcp_compound_decode_clear_non_safestate(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t *out_byte_bus_id,
                                                             uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, &payload, &payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_COMPOUND_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_COMPOUND_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_COMPOUND_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (rt != RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE) return RCP_COMPOUND_ERR_UNKNOWN_TYPE;

    /* REQ-CMP-028: clear-non-safestate carries no sub-field of its own --
     * all 7 trailing octets of message_timestamp are reserved (TC18
     * Table 12). */
    if ((hdr.message_timestamp & 0x00FFFFFFFFFFFFFFull) != 0ull) {
        return RCP_COMPOUND_ERR_RESERVED_NONZERO;
    }

    /* REQ-CMP-029: TC18 Table 12 states explicitly -- "evt[2:0], hs, cs:
     * All bits shall be written as 0, else the request shall be rejected
     * with error code = UNSUPPORTED_CMD". */
    if ((hdr.info.evt & 0x07u) != 0u || hdr.info.hs != 0u || hdr.info.cs != 0u) {
        return RCP_COMPOUND_ERR_EVT_HS_CS_NONZERO;
    }

    *out_byte_bus_id     = hdr.info.byte_bus_id;
    *out_transaction_num = hdr.info.transaction_num;
    return RCP_COMPOUND_OK;
}

/* ── The advance-only-if-still-in-start_state guard, delay timer, and tick ──── */

//cfusa:req REQ-CMP-019
//cfusa:req REQ-SEQ-012
bool rcp_compound_advance_guard(const rcp_sequencer_table_t *table,
                                 const rcp_compound_step_t *step)
{
    uint8_t current;

    if (!rcp_sequencer_get_state(table, step->sequencer_index, &current)) return false;
    /* REQ-SEQ-012 (TC18 Table 28): a sequencer manually written to 0 is
     * DISABLED -- no advance may move it out of 0 until it is explicitly
     * rewritten to a nonzero state. A step's own start_state can only
     * ever be nonzero here already (rcp_compound_start_condition_met()'s
     * "start in any state" wildcard is a start_state==0 convention, not a
     * live register value), so current==0 can never legitimately equal
     * step->start_state -- this guard exists to make that failure
     * explicit and safety-relevant, not to change the comparison's
     * ordinary outcome. */
    if (current == 0u) return false;
    return current == step->start_state;
}

//cfusa:req REQ-CMP-025
//cfusa:req REQ-SEQ-012
bool rcp_compound_start_condition_met(const rcp_sequencer_table_t *table,
                                       const rcp_compound_step_t *step)
{
    uint8_t current;

    if (!rcp_sequencer_get_state(table, step->sequencer_index, &current)) return false;
    /* REQ-SEQ-012 (TC18 Table 28): a disabled (state==0) sequencer holds
     * no compound or compound-wait step executable, including a step
     * whose own start_state is the "start in any state" wildcard
     * (start_state==0) below -- disabled is not itself a state any step
     * may start from. This check must come before the wildcard, not
     * after: the wildcard's own "any state" answer would otherwise be
     * true for a disabled sequencer too, exactly the bug this exists to
     * close. */
    if (current == 0u) return false;
    if (step->start_state == 0u) return true; /* start in any state */
    return current == step->start_state;
}

//cfusa:req REQ-CMP-020
bool rcp_compound_exec_delay_elapsed(const rcp_compound_step_t *step, uint32_t elapsed)
{
    return elapsed >= (uint32_t)step->exec_delay;
}

/* Applies step->next_state to its sequencer, honouring the "remain in the
 * current state" sentinel: a next_state of zero leaves the sequencer
 * exactly where it is rather than driving it to state zero. The execution
 * itself still succeeded, so this reports true either way (provided the
 * sequencer exists at all). */
static bool apply_next_state(rcp_sequencer_table_t *table, const rcp_compound_step_t *step)
{
    if (step->next_state == 0u) return rcp_sequencer_index_valid(table, step->sequencer_index);
    return rcp_sequencer_set_state(table, step->sequencer_index, step->next_state);
}

//cfusa:req REQ-CMP-021
//cfusa:req REQ-CMP-022
bool rcp_compound_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                        uint32_t elapsed)
{
    if (!rcp_compound_exec_delay_elapsed(step, elapsed)) return false;
    if (!rcp_compound_advance_guard(table, step)) return false;
    return apply_next_state(table, step);
}

//cfusa:req REQ-CMP-023
//cfusa:req REQ-CMP-024
bool rcp_compound_wait_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                             bool condition_met)
{
    if (!condition_met) return false;
    if (!rcp_compound_advance_guard(table, step)) return false;
    return apply_next_state(table, step);
}
