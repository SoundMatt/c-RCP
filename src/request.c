/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/request.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers ────────────────────────────────────────────────────
 * Before c-RCP-165's merge, each of this file's five predecessor
 * translation units (request_compound.c, request_triggered.c,
 * request_chained.c, request_timed.c, request_cancel.c) carried its own
 * byte-identical static copy of put_u64(), each justified in its own
 * comment as "this TU's own copy... matching acf.c's/avtp.c's/wire.c's
 * house convention of not sharing a byte-order util across modules". Now
 * that all five live in one translation unit, five identical static
 * definitions of the same function would not compile (duplicate symbol) --
 * collapsing them to the one definition below is required by the merge
 * itself, not a discretionary simplification, and changes no behavior: the
 * five bodies were byte-for-byte identical. */
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

/* ════════════════════════════════════════════════════════════════════════
 * Compound / compound-wait (0x0F/0x8F, 0x0B/0x8B) and clear-non-safestate
 * (0x06)
 * ════════════════════════════════════════════════════════════════════════ */

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
 * see request.h's "wire sub-field layout" section:
 *   0 request_type | 1 start_state | 2 next_state | 3 sequencer_index |
 *   4..5 exec_delay (BE) | 6..7 repeat_count (BE) */
static uint64_t compound_pack_ts(uint8_t request_type, const rcp_compound_step_t *step)
{
    return (((uint64_t)request_type) << 56) |
           (((uint64_t)step->start_state) << 48) |
           (((uint64_t)step->next_state) << 40) |
           (((uint64_t)step->sequencer_index) << 32) |
           (((uint64_t)step->exec_delay) << 16) |
           ((uint64_t)step->repeat_count);
}

static void compound_unpack_ts(uint64_t ts, rcp_compound_step_t *out_step)
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

    b = (uint8_t *)rcp_malloc(total);
    if (!b) return frame;

    info.byte_bus_id     = byte_bus_id;
    info.cs               = cs;
    info.evt               = evt;
    info.transaction_num  = transaction_num;
    info.pad               = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &info);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) rcp_memcpy_bounded(&b[RCP_ACF_GBB_HEADER_LEN], payload_len, payload, payload_len);
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
                                  compound_pack_ts(request_type, step), payload, payload_len);
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

    compound_unpack_ts(hdr.message_timestamp, out_step);

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
     * Table 14). */
    if ((hdr.message_timestamp & 0x00FFFFFFFFFFFFFFull) != 0ull) {
        return RCP_COMPOUND_ERR_RESERVED_NONZERO;
    }

    /* REQ-CMP-029: TC18 Table 14 states explicitly -- "evt[2:0], hs, cs:
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

/* ════════════════════════════════════════════════════════════════════════
 * Triggered (0x0E/0x8E)
 * ════════════════════════════════════════════════════════════════════════ */

/* ── request_type classification ─────────────────────────────────────────── */

//cfusa:req REQ-TRIG-001
bool rcp_request_type_is_triggered(uint8_t request_type)
{
    return request_type == RCP_REQUEST_TYPE_TRIGGERED ||
           request_type == RCP_REQUEST_TYPE_TRIGGERED_SAFETY;
}

/* ── Errors ─────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-TRIG-002
const char *rcp_triggered_strerror(rcp_triggered_errc_t e)
{
    switch (e) {
    case RCP_TRIGGERED_OK:                 return "rcp/triggered: success";
    case RCP_TRIGGERED_ERR_SHORT_FRAME:    return "rcp/triggered: frame too short";
    case RCP_TRIGGERED_ERR_BAD_MSG_TYPE:   return "rcp/triggered: unexpected ACF message type";
    case RCP_TRIGGERED_ERR_NOT_REPURPOSED: return "rcp/triggered: message_timestamp not repurposed";
    case RCP_TRIGGERED_ERR_UNKNOWN_TYPE:   return "rcp/triggered: unrecognized request_type";
    default:                               return "rcp/triggered: unknown error";
    }
}

/* ── rcp_triggered_step_t <-> the repurposed 8-byte message_timestamp region ── */

/* Octet offsets within the repurposed 8-byte message_timestamp region --
 * see request.h's "trigger-selection sub-fields" section:
 *   0 request_type | 1 trigger_source_ep | 2 trigger_signal_nr |
 *   3 trigger_threshold | 4..5 exec_delay (BE) | 6..7 repeat_count (BE) */
static uint64_t triggered_pack_ts(uint8_t request_type, const rcp_triggered_step_t *step)
{
    return (((uint64_t)request_type) << 56) |
           (((uint64_t)step->trigger_source_ep) << 48) |
           (((uint64_t)step->trigger_signal_nr) << 40) |
           (((uint64_t)step->trigger_threshold) << 32) |
           (((uint64_t)step->exec_delay) << 16) |
           ((uint64_t)step->repeat_count);
}

static void triggered_unpack_ts(uint64_t ts, rcp_triggered_step_t *out_step)
{
    out_step->trigger_source_ep = (uint8_t)((ts >> 48) & 0xFFu);
    out_step->trigger_signal_nr = (uint8_t)((ts >> 40) & 0xFFu);
    out_step->trigger_threshold = (uint8_t)((ts >> 32) & 0xFFu);
    out_step->exec_delay        = (uint16_t)((ts >> 16) & 0xFFFFu);
    out_step->repeat_count      = (uint16_t)(ts & 0xFFFFu);
}

/* ── Triggered request encode/decode ──────────────────────────────────────── */

//cfusa:req REQ-TRIG-003
//cfusa:req REQ-TRIG-004
rcp_bytes_t rcp_triggered_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                          const rcp_triggered_step_t *step, uint8_t transaction_num,
                                          const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  info  = {0};
    size_t                       unpadded, total;
    uint16_t                     quadlets;
    uint8_t                      pad;
    uint8_t                     *b;
    uint64_t                     ts;

    if (!rcp_request_type_is_triggered(request_type)) return frame;
    if (payload_len > RCP_ACF_GBB_MAX_PAYLOAD) return frame;

    unpadded = RCP_ACF_GBB_HEADER_LEN + payload_len;
    pad      = rcp_acf_pad_len(unpadded);
    total    = unpadded + pad;
    quadlets = (uint16_t)(total / 4u);

    b = (uint8_t *)rcp_malloc(total);
    if (!b) return frame;

    /* mtv=RCP_ACF_MTV_UNTIMED(0) -- see the file header: this repurposes
     * message_timestamp, so this module builds the header directly
     * (rcp_acf_pack_header()) rather than calling rcp_acf_encode_gbb(),
     * which would zero it. */
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    info.pad               = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &info);

    ts = triggered_pack_ts(request_type, step);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) rcp_memcpy_bounded(&b[RCP_ACF_GBB_HEADER_LEN], payload_len, payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

//cfusa:req REQ-TRIG-005
//cfusa:req REQ-TRIG-006
//cfusa:req REQ-TRIG-007
rcp_triggered_errc_t rcp_triggered_decode_request(const uint8_t *b, size_t len,
                                                   uint8_t *out_request_type,
                                                   rcp_byte_bus_id_t *out_byte_bus_id,
                                                   rcp_triggered_step_t *out_step,
                                                   const uint8_t **out_payload, size_t *out_payload_len,
                                                   uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, out_payload, out_payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_TRIGGERED_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_TRIGGERED_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_TRIGGERED_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (!rcp_request_type_is_triggered(rt)) return RCP_TRIGGERED_ERR_UNKNOWN_TYPE;

    triggered_unpack_ts(hdr.message_timestamp, out_step);

    *out_request_type   = rt;
    *out_byte_bus_id     = hdr.info.byte_bus_id;
    *out_transaction_num = hdr.info.transaction_num;
    return RCP_TRIGGERED_OK;
}

/* ── The trigger-occurrence counter and fire tick ─────────────────────────── */

//cfusa:req REQ-TRIG-008
void rcp_triggered_runtime_enter_started(rcp_triggered_runtime_t *rt)
{
    rt->occurrence_count = 0;
    rt->started           = true;
}

//cfusa:req REQ-TRIG-009
bool rcp_triggered_runtime_record_occurrence(rcp_triggered_runtime_t *rt,
                                              const rcp_triggered_step_t *step,
                                              uint8_t source_ep, uint8_t signal_nr)
{
    if (!rt->started) return false;
    if (source_ep != step->trigger_source_ep) return false;
    if (signal_nr != step->trigger_signal_nr) return false;

    rt->occurrence_count++;
    return true;
}

//cfusa:req REQ-TRIG-010
bool rcp_triggered_threshold_reached(const rcp_triggered_step_t *step,
                                      const rcp_triggered_runtime_t *rt)
{
    return rt->occurrence_count > (uint32_t)step->trigger_threshold;
}

//cfusa:req REQ-TRIG-011
bool rcp_triggered_exec_delay_elapsed(const rcp_triggered_step_t *step, uint32_t elapsed)
{
    return elapsed >= (uint32_t)step->exec_delay;
}

//cfusa:req REQ-TRIG-012
//cfusa:req REQ-TRIG-013
bool rcp_triggered_tick(const rcp_triggered_step_t *step, rcp_triggered_runtime_t *rt,
                         uint32_t elapsed, bool endpoint_idle)
{
    if (!rt->started) return false;
    if (!rcp_triggered_threshold_reached(step, rt)) return false;
    if (!rcp_triggered_exec_delay_elapsed(step, elapsed)) return false;
    if (!endpoint_idle) return false;

    rt->occurrence_count = 0;
    rt->started           = false;
    return true;
}

/* ════════════════════════════════════════════════════════════════════════
 * Chained (0x01)
 * ════════════════════════════════════════════════════════════════════════ */

/* ── Errors ─────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-CHAIN-001
const char *rcp_chained_strerror(rcp_chained_errc_t e)
{
    switch (e) {
    case RCP_CHAINED_OK:                        return "rcp/chained: success";
    case RCP_CHAINED_ERR_SHORT_FRAME:           return "rcp/chained: frame too short";
    case RCP_CHAINED_ERR_BAD_MSG_TYPE:          return "rcp/chained: unexpected ACF message type";
    case RCP_CHAINED_ERR_NOT_REPURPOSED:        return "rcp/chained: message_timestamp not repurposed";
    case RCP_CHAINED_ERR_UNKNOWN_TYPE:          return "rcp/chained: unrecognized request_type";
    case RCP_CHAINED_ERR_RESERVED_NONZERO:      return "rcp/chained: reserved sub-field octet is not zero";
    default:                                    return "rcp/chained: unknown error";
    }
}

/* ── Chain member encode/decode ───────────────────────────────────────────── */

//cfusa:req REQ-CHAIN-002
//cfusa:req REQ-CHAIN-003
//cfusa:req REQ-CHAIN-004
rcp_bytes_t rcp_chained_encode_member(rcp_byte_bus_id_t byte_bus_id, uint16_t chain_exec_delay,
                                       uint8_t cs, uint8_t transaction_num,
                                       const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  info  = {0};
    size_t                       unpadded, total;
    uint16_t                     quadlets;
    uint8_t                      pad;
    uint8_t                     *b;
    uint64_t                     ts;

    if (payload_len > RCP_ACF_GBB_MAX_PAYLOAD) return frame;

    unpadded = RCP_ACF_GBB_HEADER_LEN + payload_len;
    pad      = rcp_acf_pad_len(unpadded);
    total    = unpadded + pad;
    quadlets = (uint16_t)(total / 4u);

    b = (uint8_t *)rcp_malloc(total);
    if (!b) return frame;

    /* pad computed above, mtv=RCP_ACF_MTV_UNTIMED(0), hs=0, cs=this
     * member's own abort/continue selection, rsp=err=0 -- see the file
     * header for cs. mtv=0 repurposes message_timestamp, so this module
     * builds the header directly (rcp_acf_pack_header()) rather than
     * calling rcp_acf_encode_gbb(), which would zero it. */
    info.byte_bus_id     = byte_bus_id;
    info.cs               = (uint8_t)(cs & 0x1u);
    info.transaction_num  = transaction_num;
    info.pad               = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &info);

    /* Octet 0 opcode, octets 1..3 reserved-zero, octets 4..5
     * chain_exec_delay (big-endian), octets 6..7 reserved-zero -- see
     * request.h's "wire sub-field layout" section for chained. */
    ts = (((uint64_t)RCP_REQUEST_TYPE_CHAINED) << 56) |
         (((uint64_t)chain_exec_delay) << 16);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) rcp_memcpy_bounded(&b[RCP_ACF_GBB_HEADER_LEN], payload_len, payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

//cfusa:req REQ-CHAIN-005
//cfusa:req REQ-CHAIN-006
//cfusa:req REQ-CHAIN-007
rcp_chained_errc_t rcp_chained_decode_member(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t *out_byte_bus_id,
                                              uint16_t *out_chain_exec_delay,
                                              uint8_t *out_cs, const uint8_t **out_payload,
                                              size_t *out_payload_len, uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, out_payload, out_payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_CHAINED_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_CHAINED_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_CHAINED_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (rt != RCP_REQUEST_TYPE_CHAINED) return RCP_CHAINED_ERR_UNKNOWN_TYPE;

    /* Reserved octets 1..3 (mask 0x00FFFFFF00000000) and 6..7 (mask
     * 0x000000000000FFFF) must all be zero. */
    if ((hdr.message_timestamp & 0x00FFFFFF0000FFFFull) != 0ull) {
        return RCP_CHAINED_ERR_RESERVED_NONZERO;
    }

    *out_chain_exec_delay = (uint16_t)((hdr.message_timestamp >> 16) & 0xFFFFu);
    *out_cs              = hdr.info.cs;
    *out_byte_bus_id      = hdr.info.byte_bus_id;
    *out_transaction_num  = hdr.info.transaction_num;
    return RCP_CHAINED_OK;
}

//cfusa:req REQ-CHAIN-011
bool rcp_chained_exec_delay_elapsed(uint16_t chain_exec_delay, uint32_t elapsed)
{
    return elapsed >= (uint32_t)chain_exec_delay;
}

/* ── Sequencing: the cs-bit-driven abort/continue rule ────────────────────── */

//cfusa:req REQ-CHAIN-008
//cfusa:req REQ-CHAIN-009
//cfusa:req REQ-CHAIN-010
//cfusa:req REQ-CHAIN-012
rcp_chained_member_outcome_t rcp_chained_advance(bool *chain_aborted, bool has_predecessor,
                                                   bool predecessor_errored, uint8_t cs)
{
    /* No predecessor at all: nothing to chain to, so the whole chain --
     * this member included -- is ignored. */
    if (!has_predecessor) {
        *chain_aborted = true;
        return RCP_CHAINED_MEMBER_CHAIN_ERROR;
    }

    if (*chain_aborted) return RCP_CHAINED_MEMBER_CHAIN_ABORTED;

    /* cs is read off *this* member, about its predecessor's outcome. */
    if (predecessor_errored && cs == RCP_CHAINED_CS_ABORT_ON_ERROR) {
        *chain_aborted = true;
        return RCP_CHAINED_MEMBER_CHAIN_ABORTED;
    }

    return RCP_CHAINED_MEMBER_OK;
}

/* ════════════════════════════════════════════════════════════════════════
 * Timed (0x0A)
 * ════════════════════════════════════════════════════════════════════════ */

/* ── Errors ─────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-TIMED-001
const char *rcp_timed_strerror(rcp_timed_errc_t e)
{
    switch (e) {
    case RCP_TIMED_OK:                 return "rcp/timed: success";
    case RCP_TIMED_ERR_SHORT_FRAME:    return "rcp/timed: frame too short";
    case RCP_TIMED_ERR_BAD_MSG_TYPE:   return "rcp/timed: unexpected ACF message type";
    case RCP_TIMED_ERR_NOT_REPURPOSED: return "rcp/timed: message_timestamp not repurposed";
    case RCP_TIMED_ERR_UNKNOWN_TYPE:   return "rcp/timed: unrecognized request_type";
    case RCP_TIMED_ERR_RESERVED_NONZERO:
        return "rcp/timed: reserved sub-field octet is not zero";
    case RCP_TIMED_ERR_UNSUPPORTED_CMD:
        return "rcp/timed: hs/cs must be clear on a timed request";
    default:                           return "rcp/timed: unknown error";
    }
}

/* ── Feature gating ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-TIMED-002
//cfusa:req REQ-RMAP-030
bool rcp_timed_feature_enabled(uint8_t options)
{
    return (options & RCP_REGMAP_OPT_TIME_SYNC) != 0;
}

/* ── Timed request encode/decode ──────────────────────────────────────────── */

//cfusa:req REQ-TIMED-003
rcp_bytes_t rcp_timed_encode_request(rcp_byte_bus_id_t byte_bus_id, uint64_t presentation_time,
                                      uint8_t transaction_num, const uint8_t *payload,
                                      size_t payload_len)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  info  = {0};
    size_t                       unpadded, total;
    uint16_t                     quadlets;
    uint8_t                      pad;
    uint8_t                     *b;
    uint64_t                     ts;

    if (presentation_time > RCP_TIMED_PRESENTATION_TIME_MAX) return frame;
    if (payload_len > RCP_ACF_GBB_MAX_PAYLOAD) return frame;

    unpadded = RCP_ACF_GBB_HEADER_LEN + payload_len;
    pad      = rcp_acf_pad_len(unpadded);
    total    = unpadded + pad;
    quadlets = (uint16_t)(total / 4u);

    b = (uint8_t *)rcp_malloc(total);
    if (!b) return frame;

    /* mtv=RCP_ACF_MTV_UNTIMED(0), see the file header: this repurposes
     * message_timestamp instead of it holding a real timestamp -- so this
     * module builds the header directly (rcp_acf_pack_header()) rather
     * than calling rcp_acf_encode_gbb(), which would zero it. */
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    info.pad               = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &info);

    /* Octet 0 opcode, octet 1 reserved (left zero by the shift), octets
     * 2..7 the 48-bit big-endian presentation_time -- see request.h's
     * "wire sub-field layout" section for timed. */
    ts = (((uint64_t)RCP_REQUEST_TYPE_TIMED) << 56) |
         (presentation_time & RCP_TIMED_PRESENTATION_TIME_MAX);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) rcp_memcpy_bounded(&b[RCP_ACF_GBB_HEADER_LEN], payload_len, payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

//cfusa:req REQ-TIMED-013
rcp_bytes_t rcp_timed_encode_request_tscf(const rcp_acf_byte_message_info_t *hdr,
                                           const uint8_t *payload, size_t payload_len,
                                           rcp_stream_id_t stream_id,
                                           uint32_t avtp_timestamp, uint8_t sequence_num)
{
    rcp_bytes_t             acf_frame;
    rcp_bytes_t             frame = {0};
    rcp_avtp_tscf_header_t  tscf_hdr = {0};

    acf_frame = rcp_acf_encode_abb(hdr, payload, payload_len);
    if (!acf_frame.data) return frame;

    tscf_hdr.sv             = 1;
    tscf_hdr.stream_id      = stream_id;
    tscf_hdr.tv             = 1; /* avtp_timestamp valid -- this function's own point */
    tscf_hdr.avtp_timestamp = avtp_timestamp;
    tscf_hdr.sequence_num   = sequence_num;

    frame = rcp_avtp_encode_tscf(&tscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);
    return frame;
}

//cfusa:req REQ-TIMED-004
//cfusa:req REQ-TIMED-005
//cfusa:req REQ-TIMED-009
//cfusa:req REQ-TIMED-010
rcp_timed_errc_t rcp_timed_decode_request(const uint8_t *b, size_t len,
                                           rcp_byte_bus_id_t *out_byte_bus_id,
                                           uint64_t *out_presentation_time,
                                           const uint8_t **out_payload, size_t *out_payload_len,
                                           uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    uint8_t rt;
    uint8_t reserved;

    ae = rcp_acf_decode_gbb(b, len, &hdr, out_payload, out_payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_TIMED_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_TIMED_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_TIMED_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (rt != RCP_REQUEST_TYPE_TIMED) return RCP_TIMED_ERR_UNKNOWN_TYPE;

    /* The reserved octet at offset 1 must be all-zero, and hs/cs must
     * both be clear -- a request violating either is rejected rather than
     * executed against a sub-field this build cannot interpret. */
    reserved = (uint8_t)((hdr.message_timestamp >> 48) & 0xFFu);
    if (reserved != 0u) return RCP_TIMED_ERR_RESERVED_NONZERO;
    if (hdr.info.hs != 0u || hdr.info.cs != 0u) return RCP_TIMED_ERR_UNSUPPORTED_CMD;

    *out_presentation_time = hdr.message_timestamp & RCP_TIMED_PRESENTATION_TIME_MAX;
    *out_byte_bus_id         = hdr.info.byte_bus_id;
    *out_transaction_num     = hdr.info.transaction_num;
    return RCP_TIMED_OK;
}

/* ── Admission ─────────────────────────────────────────────────────────────── */

/* presentation_time - now, reduced into the field's own 48-bit wrapping
 * domain. A result above half the modulus is read as "presentation_time
 * is in the past", the usual unambiguous split for a wrapping domain. */
static uint64_t timed_forward_delta(uint64_t presentation_time, uint64_t now)
{
    return (presentation_time - now) & RCP_TIMED_PRESENTATION_TIME_MAX;
}

static bool timed_in_the_past(uint64_t delta)
{
    return delta > (RCP_TIMED_PRESENTATION_TIME_MODULUS / 2u);
}

//cfusa:req REQ-TIMED-006
bool rcp_timed_too_far(uint64_t presentation_time, uint64_t now, uint64_t max_horizon)
{
    uint64_t delta = timed_forward_delta(presentation_time, now);

    if (timed_in_the_past(delta)) return false; /* already due or past: never "too far" */
    return delta > max_horizon;
}

//cfusa:req REQ-TIMED-011
bool rcp_timed_due(uint64_t presentation_time, uint64_t now)
{
    uint64_t delta = timed_forward_delta(presentation_time, now);

    return delta == 0u || timed_in_the_past(delta);
}

//cfusa:req REQ-TIMED-007
//cfusa:req REQ-TIMED-008
rcp_timed_admission_t rcp_timed_admit(bool gptp_locked, uint64_t presentation_time, uint64_t now,
                                       uint64_t max_horizon)
{
    if (!gptp_locked) return RCP_TIMED_REJECT_GPTP_FAIL;
    if (rcp_timed_too_far(presentation_time, now, max_horizon)) {
        return RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR;
    }
    return RCP_TIMED_ACCEPT;
}

//cfusa:req REQ-WIREERR-006
rcp_wire_error_t rcp_timed_wire_error(rcp_timed_admission_t a)
{
    switch (a) {
    /* "In case the time synchronization hasn't been established, timed
     * requests... shall be rejected and an error response shall be sent
     * (error code = GPTP_FAIL)." */
    case RCP_TIMED_REJECT_GPTP_FAIL: return RCP_ERROR_GPTP_FAIL;
    /* "The RC Server may reject the request, when the presentation_time
     * is too far in the future... (error code = PRESENTATION_TIME_TOO_
     * FAR)." */
    case RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR: return RCP_ERROR_PRESENTATION_TIME_TOO_FAR;
    /* RCP_TIMED_ACCEPT: nothing to report. */
    default: return RCP_ERROR_NONE;
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Cancellation: clear-all (0x05) and clear-single (0x07)
 * ════════════════════════════════════════════════════════════════════════ */

/* ── Errors ─────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-CANCEL-001
const char *rcp_cancel_strerror(rcp_cancel_errc_t e)
{
    switch (e) {
    case RCP_CANCEL_OK:                 return "rcp/cancel: success";
    case RCP_CANCEL_ERR_SHORT_FRAME:    return "rcp/cancel: frame too short";
    case RCP_CANCEL_ERR_BAD_MSG_TYPE:   return "rcp/cancel: unexpected ACF message type";
    case RCP_CANCEL_ERR_NOT_REPURPOSED: return "rcp/cancel: message_timestamp not repurposed";
    case RCP_CANCEL_ERR_UNKNOWN_TYPE:   return "rcp/cancel: unrecognized request_type";
    case RCP_CANCEL_ERR_RESERVED_NONZERO:
        return "rcp/cancel: reserved sub-field octet is not zero";
    case RCP_CANCEL_ERR_EVT_HS_CS_NONZERO:
        return "rcp/cancel: evt[2:0], hs, or cs is not zero";
    default:                            return "rcp/cancel: unknown error";
    }
}

/* ── Shared raw-header ACF_GBB builder (mtv=0, repurposed timestamp,
 * no payload -- every request kind in this section is a fixed 16-byte
 * frame) -- see acf.h's rcp_acf_pack_header() doc comment and this file's
 * own encode_gbb_repurposed() (compound section, above) for why this
 * module builds the header directly instead of calling
 * rcp_acf_encode_gbb(). ─────────────────────────────────────────────── */

static rcp_bytes_t cancel_encode_gbb_repurposed(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num,
                                                 uint64_t ts)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  info  = {0};
    uint8_t                     *b;

    b = (uint8_t *)rcp_malloc(RCP_ACF_GBB_HEADER_LEN);
    if (!b) return frame;

    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    /* RCP_ACF_GBB_HEADER_LEN (16) is already a quadlet multiple, so with
     * no payload pad is always 0 and quadlets is always 4. */
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, (uint16_t)(RCP_ACF_GBB_HEADER_LEN / 4u), &info);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    frame.data = b;
    frame.len  = RCP_ACF_GBB_HEADER_LEN;
    return frame;
}

/* ── clear-all (0x05) ─────────────────────────────────────────────────────── */

//cfusa:req REQ-CANCEL-002
rcp_bytes_t rcp_cancel_encode_clear_all(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num)
{
    uint64_t ts = ((uint64_t)RCP_REQUEST_TYPE_CLEAR_ALL) << 56;

    return cancel_encode_gbb_repurposed(byte_bus_id, transaction_num, ts);
}

//cfusa:req REQ-CANCEL-003
//cfusa:req REQ-CANCEL-004
//cfusa:req REQ-CANCEL-013
//cfusa:req REQ-CANCEL-014
rcp_cancel_errc_t rcp_cancel_decode_clear_all(const uint8_t *b, size_t len,
                                               rcp_byte_bus_id_t *out_byte_bus_id,
                                               uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, &payload, &payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_CANCEL_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_CANCEL_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_CANCEL_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (rt != RCP_REQUEST_TYPE_CLEAR_ALL) return RCP_CANCEL_ERR_UNKNOWN_TYPE;

    /* REQ-CANCEL-013: clear-all carries no sub-field of its own -- all 7
     * trailing octets of message_timestamp are reserved (TC18 Table 13). */
    if ((hdr.message_timestamp & 0x00FFFFFFFFFFFFFFull) != 0ull) {
        return RCP_CANCEL_ERR_RESERVED_NONZERO;
    }

    /* REQ-CANCEL-014: TC18 Table 13 -- evt is either 0000b (no ack) or
     * 1000b (ack requested), so evt[2:0] must be zero; hs and cs are
     * always zero. */
    if ((hdr.info.evt & 0x07u) != 0u || hdr.info.hs != 0u || hdr.info.cs != 0u) {
        return RCP_CANCEL_ERR_EVT_HS_CS_NONZERO;
    }

    *out_byte_bus_id     = hdr.info.byte_bus_id;
    *out_transaction_num = hdr.info.transaction_num;
    return RCP_CANCEL_OK;
}

/* ── clear-single (0x07) ──────────────────────────────────────────────────── */

//cfusa:req REQ-CANCEL-005
rcp_bytes_t rcp_cancel_encode_clear_single(rcp_byte_bus_id_t byte_bus_id,
                                            uint8_t clear_transaction_num,
                                            uint8_t transaction_num)
{
    /* Octet 0 opcode, octets 1..2 reserved-zero, octet 3
     * clear_transaction_num, octets 4..7 reserved-zero -- see request.h's
     * "wire sub-field layout" section for cancellation. */
    uint64_t ts = (((uint64_t)RCP_REQUEST_TYPE_CLEAR_SINGLE) << 56) |
                  (((uint64_t)clear_transaction_num) << 32);

    return cancel_encode_gbb_repurposed(byte_bus_id, transaction_num, ts);
}

//cfusa:req REQ-CANCEL-006
//cfusa:req REQ-CANCEL-007
//cfusa:req REQ-CANCEL-015
rcp_cancel_errc_t rcp_cancel_decode_clear_single(const uint8_t *b, size_t len,
                                                  rcp_byte_bus_id_t *out_byte_bus_id,
                                                  uint8_t *out_clear_transaction_num,
                                                  uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, &payload, &payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_CANCEL_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_CANCEL_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_CANCEL_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (rt != RCP_REQUEST_TYPE_CLEAR_SINGLE) return RCP_CANCEL_ERR_UNKNOWN_TYPE;

    /* Reserved octets 1..2 (mask 0x00FFFF0000000000) and 4..7 (mask
     * 0x00000000FFFFFFFF) must all be zero. */
    if ((hdr.message_timestamp & 0x00FFFF00FFFFFFFFull) != 0ull) {
        return RCP_CANCEL_ERR_RESERVED_NONZERO;
    }

    /* REQ-CANCEL-015: TC18 Table 15 -- "Evt, hs and cs shall be zero"
     * (same evt[3]-is-the-only-free-bit rule as clear-all/Table 13). */
    if ((hdr.info.evt & 0x07u) != 0u || hdr.info.hs != 0u || hdr.info.cs != 0u) {
        return RCP_CANCEL_ERR_EVT_HS_CS_NONZERO;
    }

    *out_clear_transaction_num = (uint8_t)((hdr.message_timestamp >> 32) & 0xFFu);
    *out_byte_bus_id            = hdr.info.byte_bus_id;
    *out_transaction_num        = hdr.info.transaction_num;
    return RCP_CANCEL_OK;
}

/* ── General cancellation semantics ───────────────────────────────────────── */

//cfusa:req REQ-CANCEL-008
bool rcp_cancel_is_cancellable(rcp_cancel_lifecycle_t state)
{
    return state == RCP_CANCEL_LIFECYCLE_QUEUED;
}

//cfusa:req REQ-CANCEL-009
//cfusa:req REQ-CANCEL-010
//cfusa:req REQ-CANCEL-011
rcp_cancel_result_t rcp_cancel_attempt(bool found, rcp_cancel_lifecycle_t state)
{
    if (!found) return RCP_CANCEL_RESULT_NOT_FOUND;
    if (!rcp_cancel_is_cancellable(state)) return RCP_CANCEL_RESULT_NOT_CANCELLABLE;
    return RCP_CANCEL_RESULT_CANCELED;
}

//cfusa:req REQ-CANCEL-012
bool rcp_cancel_chain_should_cascade(uint8_t member_position, uint8_t canceled_position)
{
    return member_position >= canceled_position;
}
