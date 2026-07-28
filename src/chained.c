#include "rcp/chained.h"

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

//cfusa:req REQ-CHAIN-001
const char *rcp_chained_strerror(rcp_chained_errc_t e)
{
    switch (e) {
    case RCP_CHAINED_OK:                        return "rcp/chained: success";
    case RCP_CHAINED_ERR_SHORT_FRAME:           return "rcp/chained: frame too short";
    case RCP_CHAINED_ERR_BAD_MSG_TYPE:          return "rcp/chained: unexpected ACF message type";
    case RCP_CHAINED_ERR_NOT_REPURPOSED:        return "rcp/chained: message_timestamp not repurposed";
    case RCP_CHAINED_ERR_UNKNOWN_TYPE:          return "rcp/chained: unrecognized request_type";
    case RCP_CHAINED_ERR_TOO_FEW_MEMBERS:       return "rcp/chained: chain_length below RCP_CHAINED_MIN_MEMBERS";
    case RCP_CHAINED_ERR_POSITION_OUT_OF_RANGE: return "rcp/chained: chain_position >= chain_length";
    default:                                    return "rcp/chained: unknown error";
    }
}

/* ── Chain member encode/decode ───────────────────────────────────────────── */

//cfusa:req REQ-CHAIN-002
//cfusa:req REQ-CHAIN-003
//cfusa:req REQ-CHAIN-004
rcp_bytes_t rcp_chained_encode_member(rcp_byte_bus_id_t byte_bus_id, uint8_t chain_length,
                                       uint8_t chain_position, uint8_t cs, uint8_t transaction_num,
                                       const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};
    size_t n;
    uint8_t *b;
    uint64_t ts;

    if (chain_length < RCP_CHAINED_MIN_MEMBERS) return frame;
    if (chain_position >= chain_length) return frame;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return frame;

    n = RCP_ACF_GBB_HEADER_LEN + payload_len;
    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    b[0] = RCP_ACF_MSG_TYPE_GBB;
    put_u16(&b[1], (uint16_t)payload_len);
    b[3] = byte_bus_id;
    /* pad=0, mtv=RCP_ACF_MTV_UNTIMED(0), hs=0, cs=this member's own
     * abort/continue selection, rsp=err=0 -- see the file header for cs. */
    b[4] = (uint8_t)((cs & 0x1u) << 2);
    b[5] = 0x00u;
    b[6] = transaction_num;
    b[7] = 0x00u;

    ts = (((uint64_t)RCP_REQUEST_TYPE_CHAINED) << 56) |
         (((uint64_t)chain_length) << 48) |
         (((uint64_t)chain_position) << 40);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-CHAIN-005
//cfusa:req REQ-CHAIN-006
//cfusa:req REQ-CHAIN-007
rcp_chained_errc_t rcp_chained_decode_member(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t *out_byte_bus_id,
                                              uint8_t *out_chain_length, uint8_t *out_chain_position,
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

    *out_chain_length   = (uint8_t)((hdr.message_timestamp >> 48) & 0xFFu);
    *out_chain_position = (uint8_t)((hdr.message_timestamp >> 40) & 0xFFu);
    *out_cs              = hdr.info.cs;
    *out_byte_bus_id      = hdr.info.byte_bus_id;
    *out_transaction_num  = hdr.info.transaction_num;
    return RCP_CHAINED_OK;
}

/* ── Sequencing: the cs-bit-driven abort/continue rule ────────────────────── */

//cfusa:req REQ-CHAIN-008
//cfusa:req REQ-CHAIN-009
//cfusa:req REQ-CHAIN-010
rcp_chained_member_outcome_t rcp_chained_advance(bool *chain_aborted, bool member_errored,
                                                   uint8_t cs)
{
    if (*chain_aborted) return RCP_CHAINED_MEMBER_CHAIN_ABORTED;

    if (!member_errored) return RCP_CHAINED_MEMBER_OK;

    if (cs == RCP_CHAINED_CS_ABORT_ON_ERROR) *chain_aborted = true;
    return RCP_CHAINED_MEMBER_CHAIN_ERROR;
}
