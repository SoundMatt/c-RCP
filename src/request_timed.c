/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/request_timed.h"

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

    b = (uint8_t *)malloc(total);
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
     * 2..7 the 48-bit big-endian presentation_time -- see
     * request_timed.h's "wire sub-field layout" section. */
    ts = (((uint64_t)RCP_REQUEST_TYPE_TIMED) << 56) |
         (presentation_time & RCP_TIMED_PRESENTATION_TIME_MAX);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
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
static uint64_t forward_delta(uint64_t presentation_time, uint64_t now)
{
    return (presentation_time - now) & RCP_TIMED_PRESENTATION_TIME_MAX;
}

static bool in_the_past(uint64_t delta)
{
    return delta > (RCP_TIMED_PRESENTATION_TIME_MODULUS / 2u);
}

//cfusa:req REQ-TIMED-006
bool rcp_timed_too_far(uint64_t presentation_time, uint64_t now, uint64_t max_horizon)
{
    uint64_t delta = forward_delta(presentation_time, now);

    if (in_the_past(delta)) return false; /* already due or past: never "too far" */
    return delta > max_horizon;
}

//cfusa:req REQ-TIMED-011
bool rcp_timed_due(uint64_t presentation_time, uint64_t now)
{
    uint64_t delta = forward_delta(presentation_time, now);

    return delta == 0u || in_the_past(delta);
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
