#include "rcp/request_compound.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/avtp.c's/
 * wire.c's house convention of not sharing a byte-order util across
 * modules) ──────────────────────────────────────────────────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

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
    uint8_t flags1;
    uint8_t mtv;

    if (len < RCP_ACF_GBB_HEADER_LEN) return RCP_COMPOUND_ERR_SHORT_FRAME;
    if (b[0] != RCP_ACF_MSG_TYPE_GBB) return RCP_COMPOUND_ERR_BAD_MSG_TYPE;

    flags1 = b[4];
    mtv    = (uint8_t)((flags1 >> 4) & 0x3u);
    if (mtv != (uint8_t)RCP_ACF_MTV_UNTIMED) return RCP_COMPOUND_ERR_NOT_REPURPOSED;

    *out_request_type = b[RCP_ACF_ABB_HEADER_LEN];
    return RCP_COMPOUND_OK;
}

/* ── rcp_compound_step_t <-> the repurposed 8-byte message_timestamp region ─── */

static uint64_t pack_ts(uint8_t request_type, const rcp_compound_step_t *step)
{
    return (((uint64_t)request_type) << 56) |
           ((((uint64_t)step->sequencer_index) & 0xFFFFu) << 40) |
           (((uint64_t)step->start_state) << 32) |
           (((uint64_t)step->next_state) << 24) |
           ((((uint64_t)step->exec_delay_ms) & 0xFFFFu) << 8) |
           ((uint64_t)step->repeat_count);
}

static void unpack_ts(uint64_t ts, rcp_compound_step_t *out_step)
{
    out_step->sequencer_index = (uint16_t)((ts >> 40) & 0xFFFFu);
    out_step->start_state     = (uint8_t)((ts >> 32) & 0xFFu);
    out_step->next_state      = (uint8_t)((ts >> 24) & 0xFFu);
    out_step->exec_delay_ms   = (uint16_t)((ts >> 8) & 0xFFFFu);
    out_step->repeat_count    = (uint8_t)(ts & 0xFFu);
}

/* ── Compound / compound-wait request encode/decode ──────────────────────────── */

//cfusa:req REQ-CMP-008
//cfusa:req REQ-CMP-009
//cfusa:req REQ-CMP-010
rcp_bytes_t rcp_compound_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                         const rcp_compound_step_t *step, uint8_t transaction_num,
                                         const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};
    size_t n;
    uint8_t *b;
    uint64_t ts;

    if (!rcp_request_type_is_compound(request_type) &&
        !rcp_request_type_is_compound_wait(request_type)) {
        return frame;
    }
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return frame;

    n = RCP_ACF_GBB_HEADER_LEN + payload_len;
    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    b[0] = RCP_ACF_MSG_TYPE_GBB;
    put_u16(&b[1], (uint16_t)payload_len);
    b[3] = byte_bus_id;
    /* pad=0, mtv=RCP_ACF_MTV_UNTIMED(0), hs=cs=rsp=err=0 -- mtv MUST be
     * encoded as 0 for the repurposing trick to apply, see the file
     * header. */
    b[4] = 0x00u;
    /* op=RCP_ACF_OP_NONE(0), evt=0, ms=0 -- this request type carries no
     * read/write data operation of its own. */
    b[5] = 0x00u;
    b[6] = transaction_num;
    b[7] = 0x00u; /* read_size_or_segment_num unused by this request kind */

    ts = pack_ts(request_type, step);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-CMP-011
//cfusa:req REQ-CMP-012
//cfusa:req REQ-CMP-013
//cfusa:req REQ-CMP-014
//cfusa:req REQ-CMP-015
rcp_compound_errc_t rcp_compound_decode_request(const uint8_t *b, size_t len,
                                                 uint8_t *out_request_type,
                                                 rcp_byte_bus_id_t *out_byte_bus_id,
                                                 rcp_compound_step_t *out_step,
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
    *out_transaction_num  = hdr.info.transaction_num;
    return RCP_COMPOUND_OK;
}

/* ── clear-non-safestate (0x06) ──────────────────────────────────────────────── */

//cfusa:req REQ-CMP-016
rcp_bytes_t rcp_compound_encode_clear_non_safestate(rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t transaction_num)
{
    rcp_bytes_t frame = {0};
    uint8_t *b;
    size_t n = RCP_ACF_GBB_HEADER_LEN;
    uint64_t ts;

    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    b[0] = RCP_ACF_MSG_TYPE_GBB;
    put_u16(&b[1], 0u);
    b[3] = byte_bus_id;
    b[4] = 0x00u; /* mtv=RCP_ACF_MTV_UNTIMED(0), see the file header */
    b[5] = 0x00u;
    b[6] = transaction_num;
    b[7] = 0x00u;

    ts = ((uint64_t)RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE) << 56;
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-CMP-017
//cfusa:req REQ-CMP-018
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

    *out_byte_bus_id     = hdr.info.byte_bus_id;
    *out_transaction_num = hdr.info.transaction_num;
    return RCP_COMPOUND_OK;
}

/* ── The advance-only-if-still-in-start_state guard, delay timer, and tick ──── */

//cfusa:req REQ-CMP-019
bool rcp_compound_advance_guard(const rcp_sequencer_table_t *table,
                                 const rcp_compound_step_t *step)
{
    uint8_t current;

    if (!rcp_sequencer_get_state(table, step->sequencer_index, &current)) return false;
    return current == step->start_state;
}

//cfusa:req REQ-CMP-020
bool rcp_compound_exec_delay_elapsed(const rcp_compound_step_t *step, uint32_t elapsed_ms)
{
    return elapsed_ms >= (uint32_t)step->exec_delay_ms;
}

//cfusa:req REQ-CMP-021
//cfusa:req REQ-CMP-022
bool rcp_compound_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                        uint32_t elapsed_ms)
{
    if (!rcp_compound_exec_delay_elapsed(step, elapsed_ms)) return false;
    if (!rcp_compound_advance_guard(table, step)) return false;
    return rcp_sequencer_set_state(table, step->sequencer_index, step->next_state);
}

//cfusa:req REQ-CMP-023
//cfusa:req REQ-CMP-024
bool rcp_compound_wait_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                             bool condition_met)
{
    if (!condition_met) return false;
    if (!rcp_compound_advance_guard(table, step)) return false;
    return rcp_sequencer_set_state(table, step->sequencer_index, step->next_state);
}
