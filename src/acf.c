/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/acf.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-ACF-001
const char *rcp_acf_strerror(rcp_acf_errc_t e)
{
    switch (e) {
    case RCP_ACF_OK:                  return "rcp/acf: success";
    case RCP_ACF_ERR_SHORT_FRAME:     return "rcp/acf: frame too short";
    case RCP_ACF_ERR_BAD_MSG_TYPE:    return "rcp/acf: unexpected ACF message type";
    case RCP_ACF_ERR_BUS_ID_OVERFLOW: return "rcp/acf: byte_bus_id exceeds this build's 8-bit range";
    default:                          return "rcp/acf: unknown error";
    }
}

/* ── Byte-order helpers (this TU's own copy, matching avtp.c's/wire.c's
 * house convention of not sharing a byte-order util across modules) ────── */

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

static uint64_t get_u64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |  (uint64_t)p[7];
}

/* ── byte_message_info bit packing (TC18 v0.5.1_RC Figure 7 / Table 4) ────── */

//cfusa:req REQ-ACF-016
void rcp_acf_pack_header(uint8_t out[8], uint8_t acf_msg_type, uint16_t acf_msg_length,
                          const rcp_acf_byte_message_info_t *hdr)
{
    /* op is a single wire bit -- see rcp_acf_op_t's doc comment. Both
     * RCP_ACF_OP_NONE and RCP_ACF_OP_WRITE encode as 1 (no data response
     * expected); only RCP_ACF_OP_READ encodes as 0. */
    uint8_t op_bit = (hdr->op == (uint8_t)RCP_ACF_OP_READ) ? 0u : 1u;

    out[0] = (uint8_t)((acf_msg_type << 1) | ((acf_msg_length >> 8) & 0x01u));
    out[1] = (uint8_t)(acf_msg_length & 0xFFu);

    /* bits 4:3 (rsv) always 0. byte_bus_id is widened to uint16_t before
     * shifting: rcp_byte_bus_id_t is only 8 bits wide today (see avtp.h),
     * so bits 10:8 are always 0, but shifting the narrower type directly
     * right by 8 trips MSVC's C4333 ("right shift by too large amount")
     * under /W4 /WX even though the shifted-in-int-promoted value never
     * actually loses data -- widen first so the shift amount is provably
     * within the operand's width on every compiler. */
    out[2] = (uint8_t)(((hdr->pad & 0x3u) << 6) |
                        ((hdr->mtv & 0x1u) << 5) |
                        ((uint8_t)(((uint16_t)hdr->byte_bus_id >> 8) & 0x7u)));
    out[3] = (uint8_t)(hdr->byte_bus_id & 0xFFu);

    /* bits 3:2 (rsv) always 0. */
    out[4] = (uint8_t)(((hdr->evt & 0xFu) << 4) |
                        ((hdr->hs & 0x1u) << 1) |
                        (hdr->cs & 0x1u));
    out[5] = hdr->transaction_num;
    out[6] = (uint8_t)((op_bit << 7) |
                        ((hdr->rsp & 0x1u) << 6) |
                        ((hdr->err & 0x1u) << 5) |
                        ((hdr->ms & 0x1u) << 4) |
                        ((hdr->read_size_or_segment_num >> 8) & 0xFu));
    out[7] = (uint8_t)(hdr->read_size_or_segment_num & 0xFFu);
}

//cfusa:req REQ-ACF-016
rcp_acf_errc_t rcp_acf_unpack_header(const uint8_t in[8], rcp_acf_byte_message_info_t *out_hdr)
{
    uint16_t busid_full;
    uint8_t  op_bit;

    out_hdr->acf_msg_type   = (uint8_t)(in[0] >> 1);
    out_hdr->acf_msg_length = (uint16_t)(((uint16_t)(in[0] & 0x01u) << 8) | (uint16_t)in[1]);

    out_hdr->pad = (uint8_t)((in[2] >> 6) & 0x3u);
    out_hdr->mtv = (uint8_t)((in[2] >> 5) & 0x1u);
    busid_full   = (uint16_t)(((uint16_t)(in[2] & 0x07u) << 8) | (uint16_t)in[3]);
    if (busid_full > 0xFFu) return RCP_ACF_ERR_BUS_ID_OVERFLOW;
    out_hdr->byte_bus_id = (rcp_byte_bus_id_t)busid_full;

    out_hdr->evt = (uint8_t)((in[4] >> 4) & 0xFu);
    out_hdr->hs  = (uint8_t)((in[4] >> 1) & 0x1u);
    out_hdr->cs  = (uint8_t)(in[4] & 0x1u);
    out_hdr->transaction_num = in[5];

    /* See rcp_acf_op_t's doc comment: a decoded header's op is always
     * READ or WRITE, never the encode-only NONE convenience value. */
    op_bit = (uint8_t)((in[6] >> 7) & 0x1u);
    out_hdr->op  = op_bit ? (uint8_t)RCP_ACF_OP_WRITE : (uint8_t)RCP_ACF_OP_READ;
    out_hdr->rsp = (uint8_t)((in[6] >> 6) & 0x1u);
    out_hdr->err = (uint8_t)((in[6] >> 5) & 0x1u);
    out_hdr->ms  = (uint8_t)((in[6] >> 4) & 0x1u);
    out_hdr->read_size_or_segment_num =
        (uint16_t)((((uint16_t)(in[6] & 0x0Fu)) << 8) | (uint16_t)in[7]);

    return RCP_ACF_OK;
}

//cfusa:req REQ-ACF-016
uint8_t rcp_acf_pad_len(size_t unpadded_len)
{
    return (uint8_t)((4u - (unpadded_len % 4u)) % 4u);
}

/* ── Response semantics ───────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-002
rcp_acf_response_kind_t rcp_acf_classify_response(const rcp_acf_byte_message_info_t *hdr)
{
    /* TC18 §11.3.1: evt[3:0] = 0xF identifies an Acknowledge regardless of
     * op or err -- must be checked first, or a real decoded Acknowledge
     * (op is always WRITE or READ once decoded; see rcp_acf_op_t's doc
     * comment) falls through and is misclassified as a Write/Read/Error
     * response instead. */
    if (hdr->evt == RCP_ACF_EVT_ACKNOWLEDGE) return RCP_ACF_RESP_ACKNOWLEDGE;

    if (hdr->err) return RCP_ACF_RESP_ERROR;

    switch (hdr->op) {
    case RCP_ACF_OP_WRITE: return RCP_ACF_RESP_WRITE;
    case RCP_ACF_OP_READ:  return RCP_ACF_RESP_READ;
    default:                return RCP_ACF_RESP_ACKNOWLEDGE;
    }
}

//cfusa:req REQ-ACF-003
bool rcp_acf_hdr_ack_has_event(const rcp_acf_byte_message_info_t *hdr)
{
    return rcp_acf_classify_response(hdr) == RCP_ACF_RESP_ACKNOWLEDGE && hdr->evt != 0;
}

//cfusa:req REQ-ACF-023
bool rcp_acf_evt_row2_is_plain(uint8_t evt)
{
    return (evt & 0x7u) == 0u;
}

/* ── ACF_ABB ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-004
//cfusa:req REQ-ACF-006
//cfusa:req REQ-ACF-014
rcp_bytes_t rcp_acf_encode_abb(const rcp_acf_byte_message_info_t *hdr,
                                const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  h;
    size_t                       unpadded, total;
    uint16_t                     quadlets;
    uint8_t                      pad;
    uint8_t                     *b;

    if (payload_len > RCP_ACF_ABB_MAX_PAYLOAD) return frame;

    unpadded = RCP_ACF_ABB_HEADER_LEN + payload_len;
    pad      = rcp_acf_pad_len(unpadded);
    total    = unpadded + pad;
    quadlets = (uint16_t)(total / 4u);
    if (quadlets > RCP_ACF_MAX_QUADLETS) return frame;

    b = (uint8_t *)malloc(total);
    if (!b) return frame;

    h     = *hdr;
    h.pad = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_ABB, quadlets, &h);

    if (payload_len > 0) memcpy(&b[RCP_ACF_ABB_HEADER_LEN], payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_ABB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

//cfusa:req REQ-ACF-005
//cfusa:req REQ-ACF-007
//cfusa:req REQ-ACF-009
//cfusa:req REQ-ACF-010
//cfusa:req REQ-ACF-015
rcp_acf_errc_t rcp_acf_decode_abb(const uint8_t *b, size_t len,
                                  rcp_acf_byte_message_info_t *out_hdr,
                                  const uint8_t **out_payload, size_t *out_payload_len)
{
    uint8_t         msg_type;
    rcp_acf_errc_t  uerr;
    size_t          total, body_len;

    if (len < RCP_ACF_ABB_HEADER_LEN) return RCP_ACF_ERR_SHORT_FRAME;

    msg_type = (uint8_t)(b[0] >> 1);
    if (msg_type != RCP_ACF_MSG_TYPE_ABB) return RCP_ACF_ERR_BAD_MSG_TYPE;

    uerr = rcp_acf_unpack_header(b, out_hdr);
    if (uerr != RCP_ACF_OK) return uerr;

    total = (size_t)out_hdr->acf_msg_length * 4u;
    if (total < RCP_ACF_ABB_HEADER_LEN) return RCP_ACF_ERR_SHORT_FRAME;
    if (len < total) return RCP_ACF_ERR_SHORT_FRAME;

    body_len = total - RCP_ACF_ABB_HEADER_LEN;
    if ((size_t)out_hdr->pad > body_len) return RCP_ACF_ERR_SHORT_FRAME;

    /* ABB has no timestamp field at all -- there is nothing to be
     * uncertain or valid about, so it always folds to untimed. */
    out_hdr->mtv = RCP_ACF_MTV_UNTIMED;

    *out_payload     = &b[RCP_ACF_ABB_HEADER_LEN];
    *out_payload_len = body_len - out_hdr->pad;
    return RCP_ACF_OK;
}

/* ── ACF_GBB ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-004
//cfusa:req REQ-ACF-006
//cfusa:req REQ-ACF-011
//cfusa:req REQ-ACF-014
rcp_bytes_t rcp_acf_encode_gbb(const rcp_acf_gbb_header_t *hdr,
                                const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  h;
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
    if (quadlets > RCP_ACF_MAX_QUADLETS) return frame;

    b = (uint8_t *)malloc(total);
    if (!b) return frame;

    h     = hdr->info;
    h.pad = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &h);

    /* An untimed message always carries a zeroed timestamp region on the
     * wire, regardless of whatever hdr->message_timestamp happens to
     * hold -- this is the on-wire half of the mtv=0-means-zeroed rule. */
    ts = (hdr->info.mtv == RCP_ACF_MTV_UNTIMED) ? 0u : hdr->message_timestamp;
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

//cfusa:req REQ-ACF-005
//cfusa:req REQ-ACF-008
//cfusa:req REQ-ACF-009
//cfusa:req REQ-ACF-010
//cfusa:req REQ-ACF-015
rcp_acf_errc_t rcp_acf_decode_gbb(const uint8_t *b, size_t len,
                                  rcp_acf_gbb_header_t *out_hdr,
                                  const uint8_t **out_payload, size_t *out_payload_len)
{
    uint8_t         msg_type;
    rcp_acf_errc_t  uerr;
    size_t          total, body_len;

    if (len < RCP_ACF_GBB_HEADER_LEN) return RCP_ACF_ERR_SHORT_FRAME;

    msg_type = (uint8_t)(b[0] >> 1);
    if (msg_type != RCP_ACF_MSG_TYPE_GBB) return RCP_ACF_ERR_BAD_MSG_TYPE;

    uerr = rcp_acf_unpack_header(b, &out_hdr->info);
    if (uerr != RCP_ACF_OK) return uerr;

    total = (size_t)out_hdr->info.acf_msg_length * 4u;
    if (total < RCP_ACF_GBB_HEADER_LEN) return RCP_ACF_ERR_SHORT_FRAME;
    if (len < total) return RCP_ACF_ERR_SHORT_FRAME;

    body_len = total - RCP_ACF_GBB_HEADER_LEN;
    if ((size_t)out_hdr->info.pad > body_len) return RCP_ACF_ERR_SHORT_FRAME;

    out_hdr->message_timestamp = get_u64(&b[RCP_ACF_ABB_HEADER_LEN]);

    *out_payload     = &b[RCP_ACF_GBB_HEADER_LEN];
    *out_payload_len = body_len - out_hdr->info.pad;
    return RCP_ACF_OK;
}

//cfusa:req REQ-ACF-012
bool rcp_acf_gbb_is_timed(const rcp_acf_gbb_header_t *hdr)
{
    return hdr->info.mtv == RCP_ACF_MTV_VALID;
}

/* ── Message-type dispatch ─────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-013
rcp_acf_errc_t rcp_acf_peek_msg_type(const uint8_t *b, size_t len, uint8_t *out_msg_type)
{
    if (len < 1) return RCP_ACF_ERR_SHORT_FRAME;
    *out_msg_type = (uint8_t)(b[0] >> 1);
    return RCP_ACF_OK;
}
