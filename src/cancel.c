#include "rcp/cancel.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy -- see compound.c's identical
 * comment for the house convention this follows) ─────────────────────────── */

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
    default:                            return "rcp/cancel: unknown error";
    }
}

/* ── clear-all (0x05) ─────────────────────────────────────────────────────── */

//cfusa:req REQ-CANCEL-002
rcp_bytes_t rcp_cancel_encode_clear_all(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num)
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

    ts = ((uint64_t)RCP_REQUEST_TYPE_CLEAR_ALL) << 56;
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-CANCEL-003
//cfusa:req REQ-CANCEL-004
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

    ts = (((uint64_t)RCP_REQUEST_TYPE_CLEAR_SINGLE) << 56) |
         (((uint64_t)clear_transaction_num) << 48);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-CANCEL-006
//cfusa:req REQ-CANCEL-007
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

    *out_clear_transaction_num = (uint8_t)((hdr.message_timestamp >> 48) & 0xFFu);
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
