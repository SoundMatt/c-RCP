#include "rcp/request_timed.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy -- see request_compound.c's identical
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

//cfusa:req REQ-TIMED-001
const char *rcp_timed_strerror(rcp_timed_errc_t e)
{
    switch (e) {
    case RCP_TIMED_OK:                 return "rcp/timed: success";
    case RCP_TIMED_ERR_SHORT_FRAME:    return "rcp/timed: frame too short";
    case RCP_TIMED_ERR_BAD_MSG_TYPE:   return "rcp/timed: unexpected ACF message type";
    case RCP_TIMED_ERR_NOT_REPURPOSED: return "rcp/timed: message_timestamp not repurposed";
    case RCP_TIMED_ERR_UNKNOWN_TYPE:   return "rcp/timed: unrecognized request_type";
    default:                           return "rcp/timed: unknown error";
    }
}

/* ── Feature gating ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-TIMED-002
bool rcp_timed_feature_enabled(uint32_t options)
{
    uint32_t need = RCP_REGMAP_OPT_TIME_SYNC_TSCF | RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION;
    return (options & need) == need;
}

/* ── Timed request encode/decode ──────────────────────────────────────────── */

//cfusa:req REQ-TIMED-003
rcp_bytes_t rcp_timed_encode_request(rcp_byte_bus_id_t byte_bus_id, uint32_t presentation_time,
                                      uint8_t transaction_num, const uint8_t *payload,
                                      size_t payload_len)
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
    b[3] = byte_bus_id;
    b[4] = 0x00u; /* mtv=RCP_ACF_MTV_UNTIMED(0), see the file header */
    b[5] = 0x00u;
    b[6] = transaction_num;
    b[7] = 0x00u;

    ts = (((uint64_t)RCP_REQUEST_TYPE_TIMED) << 56) |
         (((uint64_t)presentation_time) << 24);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-TIMED-004
//cfusa:req REQ-TIMED-005
rcp_timed_errc_t rcp_timed_decode_request(const uint8_t *b, size_t len,
                                           rcp_byte_bus_id_t *out_byte_bus_id,
                                           uint32_t *out_presentation_time,
                                           const uint8_t **out_payload, size_t *out_payload_len,
                                           uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, out_payload, out_payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_TIMED_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_TIMED_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_TIMED_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (rt != RCP_REQUEST_TYPE_TIMED) return RCP_TIMED_ERR_UNKNOWN_TYPE;

    *out_presentation_time = (uint32_t)((hdr.message_timestamp >> 24) & 0xFFFFFFFFu);
    *out_byte_bus_id         = hdr.info.byte_bus_id;
    *out_transaction_num     = hdr.info.transaction_num;
    return RCP_TIMED_OK;
}

/* ── Admission ─────────────────────────────────────────────────────────────── */

//cfusa:req REQ-TIMED-006
bool rcp_timed_too_far(uint32_t presentation_time, uint32_t now, uint32_t max_horizon)
{
    int32_t delta = (int32_t)(presentation_time - now);

    if (delta < 0) return false; /* already due or in the past: never "too far" */
    return (uint32_t)delta > max_horizon;
}

//cfusa:req REQ-TIMED-007
//cfusa:req REQ-TIMED-008
rcp_timed_admission_t rcp_timed_admit(bool gptp_locked, uint32_t presentation_time, uint32_t now,
                                       uint32_t max_horizon)
{
    if (!gptp_locked) return RCP_TIMED_REJECT_GPTP_FAIL;
    if (rcp_timed_too_far(presentation_time, now, max_horizon)) {
        return RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR;
    }
    return RCP_TIMED_ACCEPT;
}
