/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ingress.h"

#include "rcp/request.h"

#include <string.h>

//cfusa:req REQ-INGRESS-002
const char *rcp_ingress_strerror(rcp_ingress_errc_t e)
{
    switch (e) {
    case RCP_INGRESS_OK:              return "rcp/ingress: success";
    case RCP_INGRESS_ERR_SHORT_FRAME: return "rcp/ingress: frame too short";
    case RCP_INGRESS_ERR_BAD_SUBTYPE: return "rcp/ingress: unexpected AVTP subtype";
    case RCP_INGRESS_ERR_BAD_VERSION: return "rcp/ingress: unsupported AVTP header version";
    default:                          return "rcp/ingress: unknown error";
    }
}

/* The subtype has already been checked before a decoder is called, so a
 * decoder can only report a bad version or a short frame. */
static rcp_ingress_errc_t map_avtp_error(rcp_avtp_errc_t e)
{
    return (e == RCP_AVTP_ERR_BAD_VERSION) ? RCP_INGRESS_ERR_BAD_VERSION
                                           : RCP_INGRESS_ERR_SHORT_FRAME;
}

//cfusa:req REQ-INGRESS-002
rcp_ingress_errc_t rcp_ingress_parse_avtpdu(const uint8_t *b, size_t len,
                                            rcp_ingress_avtpdu_t *out)
{
    uint8_t         subtype = 0;
    const uint8_t  *payload = NULL;
    size_t          payload_len = 0;
    rcp_avtp_errc_t rc;

    if (rcp_avtp_peek_subtype(b, len, &subtype) != RCP_AVTP_OK) return RCP_INGRESS_ERR_SHORT_FRAME;

    if (subtype == RCP_AVTP_SUBTYPE_NTSCF) {
        rcp_avtp_ntscf_header_t h;

        rc = rcp_avtp_decode_ntscf(b, len, &h, &payload, &payload_len);
        if (rc != RCP_AVTP_OK) return map_avtp_error(rc);

        memset(out, 0, sizeof(*out));
        out->sv           = h.sv;
        out->version      = h.version;
        out->sequence_num = h.sequence_num;
        out->stream_id    = rcp_stream_id_to_u64(h.stream_id);
    } else if (subtype == RCP_AVTP_SUBTYPE_TSCF) {
        rcp_avtp_tscf_header_t h;

        rc = rcp_avtp_decode_tscf(b, len, &h, &payload, &payload_len);
        if (rc != RCP_AVTP_OK) return map_avtp_error(rc);

        memset(out, 0, sizeof(*out));
        out->sv                     = h.sv;
        out->version                = h.version;
        out->mr                     = h.mr;
        out->tv                     = h.tv;
        out->tu                     = h.tu;
        out->sequence_num           = h.sequence_num;
        out->stream_id              = rcp_stream_id_to_u64(h.stream_id);
        out->avtp_timestamp         = h.avtp_timestamp;
        out->tscf_reserved_all_zero = rcp_avtp_tscf_reserved_all_zero(&h);
    } else {
        return RCP_INGRESS_ERR_BAD_SUBTYPE;
    }

    /* The decoders above have already proved len >= the fixed header, so
     * b[1] is in bounds. Kept raw: the safe-command-mode CRC covers the
     * byte exactly as transmitted (TC18-13.6-004/-005). */
    out->subtype       = subtype;
    out->header_octet1 = b[1];
    out->payload       = payload;
    out->payload_len   = payload_len;
    return RCP_INGRESS_OK;
}

//cfusa:req REQ-INGRESS-003
//cfusa:req REQ-INGRESS-004
rcp_ingress_timing_t rcp_ingress_resolve_timing(const rcp_ingress_avtpdu_t *f,
                                                bool time_sync_supported,
                                                rcp_avtp_tscf_fallback_t unsupported_policy,
                                                rcp_avtp_tscf_reserved_zero_policy_t
                                                    reserved_zero_policy,
                                                uint8_t *out_effective_subtype,
                                                bool *out_effective_tv)
{
    *out_effective_subtype = f->subtype;
    *out_effective_tv      = false;

    /* 1. NTSCF carries no presentation time at all. */
    if (f->subtype != RCP_AVTP_SUBTYPE_TSCF) return RCP_INGRESS_TIMING_IMMEDIATE;

    /* 2. §13.3 rule 1 decides the unsupported-time-sync case completely;
     * the reserved-bytes rule below never sees such a frame. */
    if (!time_sync_supported) {
        return (unsupported_policy == RCP_AVTP_TSCF_FALLBACK_IGNORE) ? RCP_INGRESS_TIMING_IMMEDIATE
                                                                      : RCP_INGRESS_TIMING_DROP;
    }

    /* 3. TC18-13.3-003, under the configured interpretation (see
     * rcp_avtp_tscf_reserved_zero_policy_t). TIME_GATE falls through. */
    if (f->tscf_reserved_all_zero) {
        if (reserved_zero_policy == RCP_AVTP_TSCF_RESERVED_ZERO_DROP) return RCP_INGRESS_TIMING_DROP;
        if (reserved_zero_policy == RCP_AVTP_TSCF_RESERVED_ZERO_AS_NTSCF) {
            *out_effective_subtype = RCP_AVTP_SUBTYPE_NTSCF;
            return RCP_INGRESS_TIMING_IMMEDIATE;
        }
    }

    /* 4./5. tv decides; tu is deliberately never read (REQ-AVTP-023). */
    if (f->tv == 0u) return RCP_INGRESS_TIMING_IMMEDIATE;

    *out_effective_tv = true;
    return RCP_INGRESS_TIMING_PRESENTATION;
}

//cfusa:req REQ-INGRESS-008
void rcp_ingress_member_iter_init(rcp_ingress_member_iter_t *it, const uint8_t *payload,
                                  size_t payload_len)
{
    it->buf       = payload;
    it->len       = payload_len;
    it->offset    = 0;
    it->index     = 0;
    it->malformed = false;
}

//cfusa:req REQ-INGRESS-008
bool rcp_ingress_member_next(rcp_ingress_member_iter_t *it, const uint8_t **out_member,
                             size_t *out_member_len)
{
    const uint8_t *m;
    size_t         remaining;
    size_t         header_len;
    size_t         msg_len;
    uint8_t        msg_type;

    if (it->malformed || it->offset >= it->len) return false;

    remaining = it->len - it->offset;
    m         = &it->buf[it->offset];

    /* acf_msg_type is octet 0 bits 7:1, acf_msg_length spans octet 0 bit 0
     * and octet 1 -- both octets must be present before either is read. */
    if (remaining < 2u) {
        it->malformed = true;
        return false;
    }

    msg_type = (uint8_t)(m[0] >> 1);
    if (msg_type == RCP_ACF_MSG_TYPE_ABB) {
        header_len = RCP_ACF_ABB_HEADER_LEN;
    } else if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        header_len = RCP_ACF_GBB_HEADER_LEN;
    } else {
        it->malformed = true;
        return false;
    }

    msg_len = (size_t)((((uint16_t)(m[0] & 0x01u)) << 8) | (uint16_t)m[1]) * 4u;
    if (remaining < header_len || msg_len < header_len || msg_len > remaining) {
        it->malformed = true;
        return false;
    }

    *out_member     = m;
    *out_member_len = msg_len;
    it->offset += msg_len;
    it->index++;
    return true;
}

//cfusa:req REQ-INGRESS-010
bool rcp_ingress_request_fixed_fields_valid(const uint8_t member[8], bool cs_has_meaning)
{
    rcp_acf_byte_message_info_t hdr;

    (void)rcp_acf_unpack_header(member, &hdr);

    if (!rcp_acf_request_header_constraints_valid(&hdr, cs_has_meaning)) return false;
    if (!rcp_acf_header_reserved_bits_zero(member)) return false;
    /* Table 4 is the ABB table: there mtv has nothing to describe and is
     * fixed at 0. GBB's mtv is a real field (message_timestamp valid). */
    if (hdr.acf_msg_type == RCP_ACF_MSG_TYPE_ABB && hdr.mtv != 0u) return false;
    return true;
}

//cfusa:req REQ-INGRESS-010
bool rcp_ingress_cs_has_meaning(const uint8_t *member, size_t member_len)
{
    uint8_t request_type = 0;

    if (rcp_compound_peek_request_type(member, member_len, &request_type) != RCP_COMPOUND_OK) {
        return false;
    }
    return rcp_request_type_is_compound(request_type) || request_type == RCP_REQUEST_TYPE_CHAINED;
}
