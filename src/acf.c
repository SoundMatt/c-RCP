/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/acf.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-ACF-001
const char *rcp_acf_strerror(rcp_acf_errc_t e)
{
    switch (e) {
    case RCP_ACF_OK:                  return "rcp/acf: success";
    case RCP_ACF_ERR_SHORT_FRAME:     return "rcp/acf: frame too short";
    case RCP_ACF_ERR_BAD_MSG_TYPE:    return "rcp/acf: unexpected ACF message type";
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
//cfusa:req REQ-ACF-019
void rcp_acf_pack_header(uint8_t out[8], uint8_t acf_msg_type, uint16_t acf_msg_length,
                          const rcp_acf_byte_message_info_t *hdr)
{
    /* op is a single wire bit -- see rcp_acf_op_t's doc comment. Both
     * RCP_ACF_OP_NONE and RCP_ACF_OP_WRITE encode as 1 (no data response
     * expected); only RCP_ACF_OP_READ encodes as 0. */
    uint8_t op_bit = (hdr->op == (uint8_t)RCP_ACF_OP_READ) ? 0u : 1u;

    out[0] = (uint8_t)((acf_msg_type << 1) | ((acf_msg_length >> 8) & 0x01u));
    out[1] = (uint8_t)(acf_msg_length & 0xFFu);

    /* bits 4:3 (rsv) always 0. rcp_byte_bus_id_t (avtp.h) is uint16_t,
     * wide enough for the full 11-bit wire field (REQ-RMAP-053/
     * REQ-ACF-020) -- bits 10:8 land in octet 2, bits 7:0 in octet 3,
     * both real, both possibly nonzero now, not the always-0 case an
     * earlier (8-bit) version of this type produced. */
    out[2] = (uint8_t)(((hdr->pad & 0x3u) << 6) |
                        ((hdr->mtv & 0x1u) << 5) |
                        ((uint8_t)((hdr->byte_bus_id >> 8) & 0x7u)));
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

//cfusa:req REQ-ACF-046
//cfusa:req REQ-ACF-049
rcp_acf_errc_t rcp_acf_unpack_header(const uint8_t in[8], rcp_acf_byte_message_info_t *out_hdr)
{
    uint16_t busid_full;
    uint8_t  op_bit;

    out_hdr->acf_msg_type   = (uint8_t)(in[0] >> 1);
    out_hdr->acf_msg_length = (uint16_t)(((uint16_t)(in[0] & 0x01u) << 8) | (uint16_t)in[1]);

    out_hdr->pad = (uint8_t)((in[2] >> 6) & 0x3u);
    out_hdr->mtv = (uint8_t)((in[2] >> 5) & 0x1u);
    /* busid_full is mathematically bounded to 0x7FF (in[2]'s own mask
     * limits the high part to 3 bits, in[3] contributes the low 8), a
     * range rcp_byte_bus_id_t (uint16_t, avtp.h) always represents --
     * no overflow check needed here anymore (REQ-RMAP-053/REQ-ACF-020;
     * this used to reject busid_full > 0xFFu when the type was 8 bits
     * wide). */
    busid_full   = (uint16_t)(((uint16_t)(in[2] & 0x07u) << 8) | (uint16_t)in[3]);
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

//cfusa:req REQ-ACF-032
bool rcp_acf_peek_gbb_request_type(const uint8_t *frame, size_t frame_len,
                                    uint8_t *out_request_type)
{
    rcp_acf_byte_message_info_t hdr;

    if (frame_len < 9u) return false;
    if (rcp_acf_unpack_header(frame, &hdr) != RCP_ACF_OK) return false;
    if (hdr.acf_msg_type != RCP_ACF_MSG_TYPE_GBB) return false;

    *out_request_type = frame[8];
    return true;
}

//cfusa:req REQ-ACF-047
uint8_t rcp_acf_pad_len(size_t unpadded_len)
{
    return (uint8_t)((4u - (unpadded_len % 4u)) % 4u);
}

//cfusa:req REQ-RMAP-069
size_t rcp_acf_reg_write_len(uint16_t acf_msg_length, uint8_t pad)
{
    size_t total_octets;
    size_t overhead;

    if (acf_msg_length < 3u) return 0;

    total_octets = (size_t)(acf_msg_length - 3u) * 4u;
    /* pad plus the 2-octet register start address that leads the payload
     * (TC18 §13.7.1.2, corrected in spec revision 0.5.1_RC5: "Effective
     * number of bytes to be written to register map = (acf_msg_length -
     * 3) x 4 - pad - 2" -- see REQ-RMAP-069's own doc comment in acf.h). */
    overhead = (size_t)pad + 2u;
    if (overhead > total_octets) return 0;

    return total_octets - overhead;
}

/* ── Response semantics ───────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-002
//cfusa:req REQ-ACF-034
//cfusa:req REQ-ACF-035
//cfusa:req REQ-ACF-036
//cfusa:req REQ-ACF-037
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

//cfusa:req REQ-ACF-018
rcp_acf_rss_kind_t rcp_acf_read_size_or_segment_num_kind(const rcp_acf_byte_message_info_t *hdr)
{
    return (hdr->op == (uint8_t)RCP_ACF_OP_READ) ? RCP_ACF_RSS_READ_SIZE
                                                  : RCP_ACF_RSS_SEGMENT_NUM;
}

//cfusa:req REQ-ACF-021
bool rcp_acf_request_header_constraints_valid(const rcp_acf_byte_message_info_t *hdr,
                                               bool cs_has_meaning)
{
    if (hdr->hs != 0u)  return false;
    if (hdr->rsp != 0u) return false;
    if (hdr->err != 0u) return false;
    if (!cs_has_meaning && hdr->cs != 0u) return false;
    return true;
}

//cfusa:req REQ-ACF-050
bool rcp_acf_header_is_request(const rcp_acf_byte_message_info_t *hdr)
{
    return hdr->rsp == 0u;
}

//cfusa:req REQ-ACF-023
bool rcp_acf_evt_row2_is_plain(uint8_t evt)
{
    return (evt & 0x7u) == 0u;
}

/* ── §13.5.1: compound-wait's own evt[2:0] comparison-mode rule ──────────── */

#define COMPOUND_WAIT_MODE_EXACT     ((uint8_t)0x0u)
#define COMPOUND_WAIT_MODE_AND_ONES  ((uint8_t)0x1u)
#define COMPOUND_WAIT_MODE_AND_ZEROS ((uint8_t)0x2u)
#define COMPOUND_WAIT_MODE_RESERVED  ((uint8_t)0x3u)
#define COMPOUND_WAIT_MODE_HI_GE     ((uint8_t)0x4u)
#define COMPOUND_WAIT_MODE_HI_LE     ((uint8_t)0x5u)
#define COMPOUND_WAIT_MODE_LO_GE     ((uint8_t)0x6u)
#define COMPOUND_WAIT_MODE_LO_LE     ((uint8_t)0x7u)

//cfusa:req REQ-ACF-024
bool rcp_acf_compound_wait_evt_valid(uint8_t evt)
{
    return (evt & 0x7u) != COMPOUND_WAIT_MODE_RESERVED;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

//cfusa:req REQ-ACF-025
//cfusa:req REQ-ACF-026
//cfusa:req REQ-ACF-027
//cfusa:req REQ-ACF-028
//cfusa:req REQ-ACF-029
//cfusa:req REQ-ACF-030
//cfusa:req REQ-ACF-051
//cfusa:req REQ-ACF-052
//cfusa:req REQ-ACF-053
bool rcp_acf_compound_wait_match(uint8_t evt, const uint8_t *payload, size_t payload_len,
                                  const uint8_t *status, size_t status_len)
{
    uint8_t mode = (uint8_t)(evt & 0x7u);
    size_t  i;

    if (status_len < payload_len) return false;
    /* status is implicitly capped to payload_len from here on: every branch
     * below only ever indexes status[0..payload_len) (memcmp's own length
     * argument, or the switch cases' shared `i < payload_len` loop bound),
     * so status_len itself is never consulted again after this guard. */
    (void)status_len;

    switch (mode) {
    case COMPOUND_WAIT_MODE_EXACT:
        if (payload_len == 0u) return true;
        return memcmp(payload, status, payload_len) == 0;

    case COMPOUND_WAIT_MODE_AND_ONES:
        for (i = 0; i < payload_len; i++) {
            uint8_t v = (uint8_t)((payload[i] & status[i]) | (uint8_t)~payload[i]);
            if (v != 0xFFu) return false;
        }
        return true;

    case COMPOUND_WAIT_MODE_AND_ZEROS:
        for (i = 0; i < payload_len; i++) {
            if ((uint8_t)(payload[i] & status[i]) != 0x00u) return false;
        }
        return true;

    case COMPOUND_WAIT_MODE_HI_GE:
    case COMPOUND_WAIT_MODE_HI_LE:
        if (payload_len < 4u) return false;
        return (mode == COMPOUND_WAIT_MODE_HI_GE) ? (be16(payload) >= be16(status))
                                                   : (be16(payload) <= be16(status));

    case COMPOUND_WAIT_MODE_LO_GE:
    case COMPOUND_WAIT_MODE_LO_LE:
        if (payload_len < 4u) return false;
        return (mode == COMPOUND_WAIT_MODE_LO_GE) ? (be16(&payload[2]) >= be16(&status[2]))
                                                   : (be16(&payload[2]) <= be16(&status[2]));

    case COMPOUND_WAIT_MODE_RESERVED:
    default:
        return false;
    }
}

/* ── ACF_ABB ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-031
rcp_bytes_t rcp_acf_build_error_response(rcp_byte_bus_id_t byte_bus_id,
                                          uint8_t transaction_num,
                                          rcp_wire_error_t error_code)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload = (uint8_t)error_code;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.transaction_num = transaction_num;
    hdr.evt             = 0;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
    hdr.err             = 1;

    return rcp_acf_encode_abb(&hdr, &payload, 1);
}

//cfusa:req REQ-SRV-016
bool rcp_acf_evt_requests_acknowledge(uint8_t evt)
{
    return (evt & 0x08u) != 0u;
}

//cfusa:req REQ-SRV-016
rcp_bytes_t rcp_acf_build_acknowledge_response(rcp_byte_bus_id_t byte_bus_id,
                                                uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.transaction_num = transaction_num;
    hdr.evt             = RCP_ACF_EVT_ACKNOWLEDGE;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
    hdr.err             = 0;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-ACF-033
rcp_bytes_t rcp_acf_build_acknowledge_rejected_response(rcp_byte_bus_id_t byte_bus_id,
                                                          uint8_t transaction_num,
                                                          rcp_wire_error_t error_code)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload = (uint8_t)error_code;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.transaction_num = transaction_num;
    hdr.evt             = RCP_ACF_EVT_ACKNOWLEDGE;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
    hdr.err             = 1; /* TC18 §11.3.1: "err = 1 indicates that the
                               * request has been rejected." -- distinct
                               * from rcp_acf_build_error_response()'s
                               * §11.3.4 shape; see this function's own
                               * doc comment in acf.h. */

    return rcp_acf_encode_abb(&hdr, &payload, 1);
}

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

    b = (uint8_t *)rcp_malloc(total);
    if (!b) return frame;

    h     = *hdr;
    h.pad = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_ABB, quadlets, &h);

    if (payload_len > 0) rcp_memcpy_bounded(&b[RCP_ACF_ABB_HEADER_LEN], payload_len, payload, payload_len);
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

//cfusa:req REQ-ACF-011
//cfusa:req REQ-ACF-038
//cfusa:req REQ-ACF-040
//cfusa:req REQ-ACF-044
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

    b = (uint8_t *)rcp_malloc(total);
    if (!b) return frame;

    h     = hdr->info;
    h.pad = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &h);

    /* An untimed message always carries a zeroed timestamp region on the
     * wire, regardless of whatever hdr->message_timestamp happens to
     * hold -- this is the on-wire half of the mtv=0-means-zeroed rule. */
    ts = (hdr->info.mtv == RCP_ACF_MTV_UNTIMED) ? 0u : hdr->message_timestamp;
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) rcp_memcpy_bounded(&b[RCP_ACF_GBB_HEADER_LEN], payload_len, payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

//cfusa:req REQ-ACF-008
//cfusa:req REQ-ACF-039
//cfusa:req REQ-ACF-041
//cfusa:req REQ-ACF-042
//cfusa:req REQ-ACF-043
//cfusa:req REQ-ACF-045
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
