/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/request_chained.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy -- see request_compound.c's identical
 * comment for the house convention this follows) ─────────────────────────── */

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
     * request_chained.h's "wire sub-field layout" section. */
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
