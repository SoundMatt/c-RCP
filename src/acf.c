#include "rcp/acf.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-ACF-001
const char *rcp_acf_strerror(rcp_acf_errc_t e)
{
    switch (e) {
    case RCP_ACF_OK:              return "rcp/acf: success";
    case RCP_ACF_ERR_SHORT_FRAME:  return "rcp/acf: frame too short";
    case RCP_ACF_ERR_BAD_MSG_TYPE: return "rcp/acf: unexpected ACF message type";
    default:                       return "rcp/acf: unknown error";
    }
}

/* ── Byte-order helpers (this TU's own copy, matching avtp.c's/wire.c's
 * house convention of not sharing a byte-order util across modules) ────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
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

static uint64_t get_u64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |  (uint64_t)p[7];
}

/* ── byte_message_info bit packing (offsets 4 and 5, see acf.h) ──────────── */

static uint8_t pack_flags1(const rcp_acf_byte_message_info_t *hdr)
{
    return (uint8_t)(((hdr->pad & 0x3u) << 6) |
                      ((hdr->mtv & 0x3u) << 4) |
                      ((hdr->hs & 0x1u) << 3) |
                      ((hdr->cs & 0x1u) << 2) |
                      ((hdr->rsp & 0x1u) << 1) |
                      (hdr->err & 0x1u));
}

static void unpack_flags1(uint8_t b, rcp_acf_byte_message_info_t *out_hdr)
{
    out_hdr->pad = (uint8_t)((b >> 6) & 0x3u);
    out_hdr->mtv = (uint8_t)((b >> 4) & 0x3u);
    out_hdr->hs  = (uint8_t)((b >> 3) & 0x1u);
    out_hdr->cs  = (uint8_t)((b >> 2) & 0x1u);
    out_hdr->rsp = (uint8_t)((b >> 1) & 0x1u);
    out_hdr->err = (uint8_t)(b & 0x1u);
}

static uint8_t pack_flags2(const rcp_acf_byte_message_info_t *hdr)
{
    return (uint8_t)(((hdr->op & 0x7u) << 5) |
                      ((hdr->evt & 0xFu) << 1) |
                      (hdr->ms & 0x1u));
}

static void unpack_flags2(uint8_t b, rcp_acf_byte_message_info_t *out_hdr)
{
    out_hdr->op  = (uint8_t)((b >> 5) & 0x7u);
    out_hdr->evt = (uint8_t)((b >> 1) & 0xFu);
    out_hdr->ms  = (uint8_t)(b & 0x1u);
}

/* ── Response semantics ───────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-002
rcp_acf_response_kind_t rcp_acf_classify_response(const rcp_acf_byte_message_info_t *hdr)
{
    if (hdr->err) return RCP_ACF_RESP_ERROR;

    switch (hdr->op) {
    case RCP_ACF_OP_WRITE: return RCP_ACF_RESP_WRITE;
    case RCP_ACF_OP_READ:  return RCP_ACF_RESP_READ;
    default:               return RCP_ACF_RESP_ACKNOWLEDGE;
    }
}

//cfusa:req REQ-ACF-003
bool rcp_acf_hdr_ack_has_event(const rcp_acf_byte_message_info_t *hdr)
{
    return rcp_acf_classify_response(hdr) == RCP_ACF_RESP_ACKNOWLEDGE && hdr->evt != 0;
}

/* ── ACF_ABB ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-ACF-004
//cfusa:req REQ-ACF-006
//cfusa:req REQ-ACF-014
rcp_bytes_t rcp_acf_encode_abb(const rcp_acf_byte_message_info_t *hdr,
                                const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};
    size_t n;
    uint8_t *b;

    if (payload_len > RCP_ACF_MAX_PAYLOAD) return frame;

    n = RCP_ACF_ABB_HEADER_LEN + payload_len;
    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    b[0] = RCP_ACF_MSG_TYPE_ABB;
    put_u16(&b[1], (uint16_t)payload_len);
    b[3] = hdr->byte_bus_id;
    b[4] = pack_flags1(hdr);
    b[5] = pack_flags2(hdr);
    b[6] = hdr->transaction_num;
    b[7] = hdr->read_size_or_segment_num;

    if (payload_len > 0) memcpy(&b[RCP_ACF_ABB_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
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
    uint16_t dlen;

    if (len < RCP_ACF_ABB_HEADER_LEN) return RCP_ACF_ERR_SHORT_FRAME;
    if (b[0] != RCP_ACF_MSG_TYPE_ABB) return RCP_ACF_ERR_BAD_MSG_TYPE;

    dlen = get_u16(&b[1]);
    if (len < RCP_ACF_ABB_HEADER_LEN + (size_t)dlen) return RCP_ACF_ERR_SHORT_FRAME;

    out_hdr->acf_msg_type   = b[0];
    out_hdr->acf_msg_length = dlen;
    out_hdr->byte_bus_id    = b[3];
    unpack_flags1(b[4], out_hdr);
    unpack_flags2(b[5], out_hdr);
    out_hdr->transaction_num           = b[6];
    out_hdr->read_size_or_segment_num  = b[7];

    /* ABB has no timestamp field at all -- there is nothing to be
     * uncertain or valid about, so it always folds to untimed. */
    out_hdr->mtv = RCP_ACF_MTV_UNTIMED;

    *out_payload     = &b[RCP_ACF_ABB_HEADER_LEN];
    *out_payload_len = dlen;
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
    rcp_bytes_t frame = {0};
    size_t n;
    uint8_t *b;
    uint64_t ts;

    if (payload_len > RCP_ACF_MAX_PAYLOAD) return frame;

    n = RCP_ACF_GBB_HEADER_LEN + payload_len;
    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    b[0] = RCP_ACF_MSG_TYPE_GBB;
    put_u16(&b[1], (uint16_t)payload_len);
    b[3] = hdr->info.byte_bus_id;
    b[4] = pack_flags1(&hdr->info);
    b[5] = pack_flags2(&hdr->info);
    b[6] = hdr->info.transaction_num;
    b[7] = hdr->info.read_size_or_segment_num;

    /* An untimed message always carries a zeroed timestamp region on the
     * wire, regardless of whatever hdr->message_timestamp happens to
     * hold -- this is the on-wire half of the mtv=0-means-zeroed rule. */
    ts = (hdr->info.mtv == RCP_ACF_MTV_UNTIMED) ? 0u : hdr->message_timestamp;
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
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
    uint16_t dlen;

    if (len < RCP_ACF_GBB_HEADER_LEN) return RCP_ACF_ERR_SHORT_FRAME;
    if (b[0] != RCP_ACF_MSG_TYPE_GBB) return RCP_ACF_ERR_BAD_MSG_TYPE;

    dlen = get_u16(&b[1]);
    if (len < RCP_ACF_GBB_HEADER_LEN + (size_t)dlen) return RCP_ACF_ERR_SHORT_FRAME;

    out_hdr->info.acf_msg_type   = b[0];
    out_hdr->info.acf_msg_length = dlen;
    out_hdr->info.byte_bus_id    = b[3];
    unpack_flags1(b[4], &out_hdr->info);
    unpack_flags2(b[5], &out_hdr->info);
    out_hdr->info.transaction_num          = b[6];
    out_hdr->info.read_size_or_segment_num = b[7];
    out_hdr->message_timestamp             = get_u64(&b[RCP_ACF_ABB_HEADER_LEN]);

    *out_payload     = &b[RCP_ACF_GBB_HEADER_LEN];
    *out_payload_len = dlen;
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
    *out_msg_type = b[0];
    return RCP_ACF_OK;
}
