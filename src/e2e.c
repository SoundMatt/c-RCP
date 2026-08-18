/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/e2e.h"
#include "rcp/alloc.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-E2E-001
const char *rcp_e2e_strerror(rcp_e2e_errc_t e)
{
    switch (e) {
    case RCP_E2E_OK:               return "e2e: ok";
    case RCP_E2E_ERR_SHORT_FRAME:  return "e2e: frame too short for a CRC32 trailer";
    /* Not "CRC_ERROR": the spec's own prose names a CRC mismatch
     * CRC_ERROR, but its authoritative numbered error-code table has no
     * such entry -- the code it actually assigns a CRC mismatch is
     * POCI_FAILURE (12), see rcp_e2e_wire_error(). Naming this string
     * after the table's real code, not the prose alias, avoids baking a
     * name with no wire representation into this implementation. */
    case RCP_E2E_ERR_CRC_MISMATCH: return "e2e: POCI failure -- CRC32 mismatch, execution skipped";
    default:                          return "e2e: unknown error";
    }
}

//cfusa:req REQ-WIREERR-003
rcp_wire_error_t rcp_e2e_wire_error(rcp_e2e_errc_t e)
{
    switch (e) {
    /* The governing spec's numbered error-code table assigns a CRC
     * mismatch the code POCI_FAILURE (12) -- see errors.h's file header
     * for why this doesn't literally match the "CRC_ERROR" name used in
     * the spec's own prose elsewhere. */
    case RCP_E2E_ERR_CRC_MISMATCH: return RCP_ERROR_POCI_FAILURE;
    /* RCP_E2E_OK and RCP_E2E_ERR_SHORT_FRAME are both local framing
     * outcomes with no numbered wire-error-code counterpart: OK means
     * nothing went wrong, and a too-short frame never reaches the point
     * of being a transmittable Response at all. */
    default: return RCP_ERROR_NONE;
    }
}

/* ── CRC32 (poly 0xF4ACFB13, init/xorout 0xFFFFFFFF, refin/refout true) ────── */

/* Bit-reversed form of 0xF4ACFB13, used directly by the reflected
 * (LSB-first) update below -- the standard technique for implementing a
 * refin=true/refout=true CRC without reflecting each input byte and the
 * running remainder separately. */
#define RCP_E2E_CRC32_RPOLY 0xC8DF352Fu

static uint32_t crc32_update(uint32_t crc, uint8_t b)
{
    int i;

    crc ^= b;
    for (i = 0; i < 8; i++) {
        crc = (crc & 1u) ? ((crc >> 1) ^ RCP_E2E_CRC32_RPOLY) : (crc >> 1);
    }
    return crc;
}

//cfusa:req REQ-E2E-002
uint32_t rcp_e2e_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;

    for (i = 0; i < len; i++) crc = crc32_update(crc, data[i]);
    return crc ^ 0xFFFFFFFFu;
}

static void put_u64(uint8_t *p, uint64_t v)
{
    size_t i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (56u - 8u * i));
    }
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* Reads the byte_message_info "pad" field -- acf.h Figure 7's own 2-bit
 * "count of trailing zero pad octets, 0-3" field, octet 2 bits 7:6 --
 * directly out of a wire-format ACF header, by documented offset, the
 * same "no dependency on acf.c" layering discipline adapt_acf_msg_length()
 * already follows for acf_msg_length (see this file's own "Coverage span"
 * doc comment in e2e.h). acf.c's rcp_acf_encode_abb()/_encode_gbb() always
 * populate this field with the exact number of trailing 0x00 octets they
 * themselves appended to reach quadlet alignment -- the same value
 * rcp_acf_pad_len() computed -- so a caller handing this module one of
 * their frames gives this function everything it needs to find the
 * real/pad boundary without any new parameter. Returns 0 if frame_len < 3
 * (too short to contain octet 2): conservatively treats the whole frame
 * as real (un-padded) rather than reading out of bounds. */
static uint8_t acf_pad_octets(const uint8_t *frame, size_t frame_len)
{
    if (frame_len < 3u) return 0u;
    return (uint8_t)((frame[2] >> 6) & 0x3u);
}

/* AVTP subtype byte values (IEEE 1722-2016's public subtype registry),
 * hardcoded here rather than included from avtp.h, per this module's own
 * "no dependency on avtp.c" layering discipline (see the file header) --
 * numerically identical to avtp.h's own RCP_AVTP_SUBTYPE_TSCF/_NTSCF.
 * Used only by rcp_e2e_wrap_framed()/_unwrap_framed() to derive
 * avtp_subtype from is_ntscf_framed themselves, below. */
#define RCP_E2E_AVTP_SUBTYPE_TSCF  ((uint8_t)0x05u)
#define RCP_E2E_AVTP_SUBTYPE_NTSCF ((uint8_t)0x82u)

//cfusa:req REQ-E2E-003
uint32_t rcp_e2e_compute_crc(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                                 uint64_t stream_id, uint32_t avtp_timestamp,
                                 const uint8_t *acf_frame, size_t acf_frame_len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t  sid[8];
    uint8_t  ts[4];
    size_t   i;

    put_u64(sid, stream_id);
    put_u32(ts, avtp_timestamp);

    /* TC18 §13.6 Figures 20/21's own orange "header CRC" bytes, in their
     * exact left-to-right wire order (issue #465 -- see e2e.h's file
     * header "Figure 20/21 header-CRC bytes" section for the rendered-page
     * investigation this order comes from and why running it continuously
     * into the green ACF-CRC bytes below is exact, not approximate). */
    crc = crc32_update(crc, avtp_subtype);
    crc = crc32_update(crc, header_octet1);
    crc = crc32_update(crc, tu ? (uint8_t)0x01u : (uint8_t)0x00u);

    for (i = 0; i < 8; i++) crc = crc32_update(crc, sid[i]);
    for (i = 0; i < 4; i++) crc = crc32_update(crc, ts[i]);
    for (i = 0; i < acf_frame_len; i++) crc = crc32_update(crc, acf_frame[i]);

    return crc ^ 0xFFFFFFFFu;
}

/* Adapts the acf_msg_length field (acf.h's byte_message_info layout: a
 * 9-bit quadlet count spanning bit 0 of octet 0 and all of octet 1; octet
 * 0's other 7 bits are acf_msg_type, preserved unmodified) of
 * frame[0..frame_len) by delta_quadlets quadlets, in place. Returns
 * false, leaving frame unmodified, if frame_len < 2 (too short to
 * contain the field) or if the adaptation would under/overflow the 9-bit
 * field (0x1FF, matching acf.h's RCP_E2E_CRC_LEN of exactly one quadlet
 * -- this module hardcodes 0x1FF rather than including acf.h's own
 * RCP_ACF_MAX_QUADLETS, matching this module's "no dependency on acf.c"
 * layering discipline, see the file header); true on success. Private to
 * this module -- e2e.c reads this field by documented offset rather than
 * including acf.h. */
static bool adapt_acf_msg_length(uint8_t *frame, size_t frame_len, int delta_quadlets)
{
    long    adapted;
    uint8_t type_bits;
    uint16_t len_field;

    if (frame_len < 2u) return false;

    type_bits = (uint8_t)(frame[0] & 0xFEu); /* top 7 bits: acf_msg_type */
    len_field = (uint16_t)(((uint16_t)(frame[0] & 0x01u) << 8) | (uint16_t)frame[1]);
    adapted   = (long)len_field + (long)delta_quadlets;
    if (adapted < 0 || adapted > 0x1FF) return false;

    frame[0] = (uint8_t)(type_bits | (uint8_t)((adapted >> 8) & 0x01));
    frame[1] = (uint8_t)(adapted & 0xFFu);
    return true;
}

//cfusa:req REQ-E2E-004
size_t rcp_e2e_length_with_crc(size_t payload_len)
{
    if (payload_len > (size_t)-1 - RCP_E2E_CRC_LEN) return (size_t)-1;
    return payload_len + RCP_E2E_CRC_LEN;
}

//cfusa:req REQ-E2E-037
size_t rcp_e2e_data_length_for_protected_members(size_t protected_member_count)
{
    if (protected_member_count > (size_t)-1 / RCP_E2E_CRC_LEN) return (size_t)-1;
    return protected_member_count * RCP_E2E_CRC_LEN;
}

/* ── wrap / unwrap ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-E2E-005
//cfusa:req REQ-E2E-006
rcp_bytes_t rcp_e2e_wrap(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                             uint64_t stream_id, uint32_t avtp_timestamp,
                             const uint8_t *acf_frame, size_t acf_frame_len)
{
    rcp_bytes_t out = {0};
    uint8_t    *data;
    uint32_t    crc;
    uint8_t     pad_octets;
    size_t      real_len;

    if (!acf_frame && acf_frame_len > 0) return out;
    if (acf_frame_len > (size_t)-1 - RCP_E2E_CRC_LEN) return out;
    /* TC18 §13.6 Figures 20/21: the ACF message (header quadlets plus
     * byte_msg_payload including its own 0x00 pad octets) must still be a
     * whole number of quadlets on entry -- acf.c's rcp_acf_encode_abb()/
     * _encode_gbb() always produce exactly that, quadlet-aligned for
     * their own plain-mode acf_msg_length accounting whether or not the
     * frame ever reaches this function. A non-quadlet-aligned
     * acf_frame_len means the caller handed this function something other
     * than one of those encoders' output -- reject it rather than risk
     * leaving the adapted acf_msg_length describing a length nothing on
     * the wire actually has. */
    if (acf_frame_len % 4u != 0u) return out;

    /* Figures 20/21 place the CRC32 immediately after the REAL (unpadded)
     * payload, computed over the header and that real payload only --
     * "the entire payload (except padding)" -- with acf.c's own trailing
     * 0x00 quadlet-alignment pad octets appended strictly AFTER the
     * complete trailer, not before it. acf_pad_octets() recovers exactly
     * how many of acf_frame's own trailing bytes are that padding,
     * straight out of the wire header's own "pad" field, so this
     * function can re-seat them after the trailer instead of covering
     * them with the CRC and leaving them ahead of it (issue #420). */
    pad_octets = acf_pad_octets(acf_frame, acf_frame_len);
    if ((size_t)pad_octets > acf_frame_len) return out; /* malformed pad field */
    real_len = acf_frame_len - pad_octets;

    data = (uint8_t *)rcp_malloc(acf_frame_len + RCP_E2E_CRC_LEN);
    if (!data) return out;

    if (real_len > 0) memcpy(data, acf_frame, real_len);

    /* Adapt the copy's acf_msg_length by +1 quadlet before computing the
     * CRC, per the coverage-span-and-length-accounting rule in the file
     * header -- the trailer about to be appended must already be
     * reflected in the length a conformant peer will read. This is still
     * exactly +1 quadlet regardless of the real/pad split above: adding a
     * whole quadlet (the CRC32 trailer) never changes how many pad octets
     * are needed to reach the next quadlet boundary, only where they
     * sit. */
    if (!adapt_acf_msg_length(data, real_len, 1)) {
        rcp_free(data);
        data = NULL;
        return out;
    }

    /* Coverage span: header-and-real-payload only, excluding the pad
     * octets sitting at acf_frame[real_len..acf_frame_len). */
    crc = rcp_e2e_compute_crc(avtp_subtype, header_octet1, tu, stream_id, avtp_timestamp,
                               data, real_len);
    put_u32(data + real_len, crc);

    /* Re-seat the original trailing pad octets after the trailer instead
     * of before it -- copied from the caller's own frame (not
     * memset(0)), so this function stays a faithful transport for
     * whatever bytes the caller's pad octets actually held, matching
     * unwrap()'s own byte-identical round-trip contract. */
    if (pad_octets > 0) memcpy(data + real_len + RCP_E2E_CRC_LEN, acf_frame + real_len, pad_octets);

    out.data = data;
    out.len  = acf_frame_len + RCP_E2E_CRC_LEN;
    return out;
}

//cfusa:req REQ-E2E-007
//cfusa:req REQ-E2E-008
//cfusa:req REQ-E2E-009
rcp_e2e_errc_t rcp_e2e_unwrap(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                                     uint64_t stream_id, uint32_t avtp_timestamp,
                                     const uint8_t *frame, size_t frame_len,
                                     rcp_bytes_t *out_acf_frame)
{
    size_t      real_len;
    size_t      body_len;
    uint8_t     pad_octets;
    uint32_t    got;
    uint32_t    want;
    rcp_bytes_t body_copy = {0};

    out_acf_frame->data = NULL;
    out_acf_frame->len  = 0;

    if (frame_len < RCP_E2E_CRC_LEN) return RCP_E2E_ERR_SHORT_FRAME;

    /* Mirrors rcp_e2e_wrap()'s own real/pad split: on the wire the CRC32
     * sits immediately after the real (unpadded) header-and-payload
     * region, with any quadlet-alignment pad octets AFTER the trailer
     * (TC18 §13.6 Figures 20/21) -- frame = [real_len][CRC32][pad_octets],
     * not the last-4-octets-are-the-trailer layout a pre-issue-#420 wire
     * frame used. acf_pad_octets() recovers pad_octets from the header's
     * own "pad" field, the same wire-format source rcp_e2e_wrap() reads. */
    pad_octets = acf_pad_octets(frame, frame_len);
    if ((size_t)pad_octets + RCP_E2E_CRC_LEN > frame_len) return RCP_E2E_ERR_SHORT_FRAME;
    real_len = frame_len - RCP_E2E_CRC_LEN - pad_octets;

    got      = get_u32(frame + real_len);
    want     = rcp_e2e_compute_crc(avtp_subtype, header_octet1, tu, stream_id, avtp_timestamp,
                                    frame, real_len);

    /* Reassemble the plain ACF message acf.c's decoders expect --
     * header-and-real-payload immediately followed by the pad octets,
     * with the CRC32 that used to sit between them on the wire removed --
     * by copying the two regions independently (they are no longer
     * contiguous in `frame`, the CRC32 trailer now sits between them). */
    body_len = real_len + pad_octets; /* == frame_len - RCP_E2E_CRC_LEN */
    if (body_len > 0) {
        body_copy.data = (uint8_t *)rcp_malloc(body_len);
        if (body_copy.data) {
            if (real_len > 0) memcpy(body_copy.data, frame, real_len);
            if (pad_octets > 0) {
                memcpy(body_copy.data + real_len, frame + real_len + RCP_E2E_CRC_LEN, pad_octets);
            }
            body_copy.len = body_len;
        }
    }
    if (!body_copy.data && body_len > 0) return got == want ? RCP_E2E_OK : RCP_E2E_ERR_CRC_MISMATCH;

    /* Restore the un-adapted acf_msg_length so the returned copy is
     * ready for acf.c's decoders exactly as their own encoders produced
     * it (mirrors rcp_e2e_wrap()'s +1 quadlet in reverse). Left
     * unadapted (harmless) if real_len < 3: too short to contain the
     * field, which acf.c's own decoders will reject as RCP_ACF_ERR_
     * SHORT_FRAME regardless. */
    (void)adapt_acf_msg_length(body_copy.data, real_len, -1);

    *out_acf_frame = body_copy;

    if (got != want) return RCP_E2E_ERR_CRC_MISMATCH;
    return RCP_E2E_OK;
}

/* ── Framing-aware wrap/unwrap: the NTSCF all-zero timestamp stand-in ───────── */

//cfusa:req REQ-E2E-035
rcp_bytes_t rcp_e2e_wrap_framed(uint64_t stream_id, bool is_ntscf_framed,
                                 uint8_t header_octet1, bool tu,
                                 uint32_t avtp_timestamp,
                                 const uint8_t *acf_frame, size_t acf_frame_len)
{
    uint8_t avtp_subtype = is_ntscf_framed ? RCP_E2E_AVTP_SUBTYPE_NTSCF
                                            : RCP_E2E_AVTP_SUBTYPE_TSCF;
    return rcp_e2e_wrap(avtp_subtype, header_octet1, is_ntscf_framed ? false : tu,
                         stream_id, is_ntscf_framed ? 0u : avtp_timestamp,
                         acf_frame, acf_frame_len);
}

//cfusa:req REQ-E2E-035
rcp_e2e_errc_t rcp_e2e_unwrap_framed(uint64_t stream_id, bool is_ntscf_framed,
                                      uint8_t header_octet1, bool tu,
                                      uint32_t avtp_timestamp,
                                      const uint8_t *frame, size_t frame_len,
                                      rcp_bytes_t *out_acf_frame)
{
    uint8_t avtp_subtype = is_ntscf_framed ? RCP_E2E_AVTP_SUBTYPE_NTSCF
                                            : RCP_E2E_AVTP_SUBTYPE_TSCF;
    return rcp_e2e_unwrap(avtp_subtype, header_octet1, is_ntscf_framed ? false : tu,
                           stream_id, is_ntscf_framed ? 0u : avtp_timestamp,
                           frame, frame_len, out_acf_frame);
}

/* ── Fragmentation/CRC interaction ─────────────────────────────────────────── */

//cfusa:req REQ-E2E-010
bool rcp_e2e_fragment_carries_crc(bool is_last_fragment)
{
    return is_last_fragment;
}

//cfusa:req REQ-E2E-038
uint32_t rcp_e2e_compute_fragmented_crc(uint8_t avtp_subtype, uint8_t header_octet1, bool tu,
                                         uint64_t stream_id, uint32_t avtp_timestamp,
                                         const uint8_t *first_fragment_header,
                                         size_t first_fragment_header_len,
                                         const uint8_t *reassembled_payload,
                                         size_t reassembled_payload_len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t  sid[8];
    uint8_t  ts[4];
    size_t   i;

    put_u64(sid, stream_id);
    put_u32(ts, avtp_timestamp);

    /* Same orange header-CRC bytes, same order, as rcp_e2e_compute_crc()
     * above -- issue #465, see e2e.h's file header. */
    crc = crc32_update(crc, avtp_subtype);
    crc = crc32_update(crc, header_octet1);
    crc = crc32_update(crc, tu ? (uint8_t)0x01u : (uint8_t)0x00u);

    for (i = 0; i < 8; i++) crc = crc32_update(crc, sid[i]);
    for (i = 0; i < 4; i++) crc = crc32_update(crc, ts[i]);
    for (i = 0; i < first_fragment_header_len; i++) crc = crc32_update(crc, first_fragment_header[i]);
    for (i = 0; i < reassembled_payload_len; i++) crc = crc32_update(crc, reassembled_payload[i]);

    return crc ^ 0xFFFFFFFFu;
}

/* ── Safety-tagged request classification ──────────────────────────────────── */

//cfusa:req REQ-E2E-011
bool rcp_e2e_is_safety_request(uint8_t request_type)
{
    return (request_type & 0x80u) != 0u;
}

//cfusa:req REQ-E2E-012
//cfusa:req REQ-E2E-013
bool rcp_e2e_request_may_execute(uint8_t request_type, bool endpoint_in_safe_state)
{
    if (rcp_e2e_is_safety_request(request_type)) return endpoint_in_safe_state;
    return true;
}

/* ── The watchdog-purge-vs-safety-survive rule ─────────────────────────────── */

//cfusa:req REQ-E2E-014
bool rcp_e2e_watchdog_purge_should_keep(uint8_t request_type)
{
    return rcp_e2e_is_safety_request(request_type);
}

//cfusa:req REQ-E2E-015
void rcp_e2e_watchdog_purge_classify(const uint8_t *request_types, size_t count,
                                         bool *out_keep)
{
    size_t i;
    for (i = 0; i < count; i++) {
        out_keep[i] = rcp_e2e_watchdog_purge_should_keep(request_types[i]);
    }
}

/* ── The configured safe state ─────────────────────────────────────────────── */

//cfusa:req REQ-E2E-016
bool rcp_e2e_measure_valid(uint8_t rx_safety_measure)
{
    return rx_safety_measure == (uint8_t)RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE ||
           rx_safety_measure == (uint8_t)RCP_E2E_MEASURE_SEQUENCER;
}

//cfusa:req REQ-E2E-017
//cfusa:req REQ-E2E-018
//cfusa:req REQ-E2E-019
//cfusa:req REQ-SEQ-012
bool rcp_e2e_endpoint_in_safe_state(uint8_t rx_safety_measure,
                                        const rcp_sequencer_table_t *table,
                                        uint16_t safestate_sequencer,
                                        uint8_t safe_sequencer_state)
{
    uint8_t current;

    if (rx_safety_measure == (uint8_t)RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE) return true;
    if (rx_safety_measure != (uint8_t)RCP_E2E_MEASURE_SEQUENCER) return false; /* fail closed */

    if (!table) return false; /* fail closed */
    if (!rcp_sequencer_get_state(table, safestate_sequencer, &current)) return false; /* fail closed */

    /* REQ-SEQ-012 (TC18 Table 28): a manually-disabled (state==0)
     * sequencer conveys no application-state information at all -- it is
     * "off," not "reached state 0" -- so it can never itself satisfy a
     * safe-state check, even if safe_sequencer_state also happens to be
     * (mis)configured to 0. Fail closed rather than let a disabled
     * sequencer accidentally read as "safe." */
    if (current == 0u) return false; /* fail closed */

    return current == safe_sequencer_state;
}

/* ── Per-stream watchdog ────────────────────────────────────────────────────── */

//cfusa:req REQ-E2E-024
//cfusa:req REQ-E2E-025
//cfusa:req REQ-E2E-026
//cfusa:req REQ-E2E-027
rcp_e2e_wd_result_t rcp_e2e_wd_evaluate(bool rx_wd_enable, uint32_t rx_wd_timeout_ms,
                                               bool rx_wd_safestate_enable,
                                               bool rx_wd_info_enable,
                                               uint64_t elapsed_since_last_kick_ms)
{
    rcp_e2e_wd_result_t r;

    r.overflowed = rx_wd_enable && (elapsed_since_last_kick_ms >= (uint64_t)rx_wd_timeout_ms);
    r.enter_safe_state = r.overflowed && rx_wd_safestate_enable;
    r.notify           = r.overflowed && rx_wd_info_enable;
    return r;
}

/* ── Request-storage overflow ──────────────────────────────────────────────── */

//cfusa:req REQ-E2E-030
bool rcp_e2e_overflow_should_enter_safe_state(bool rx_ovrflw_safestate_enable)
{
    return rx_ovrflw_safestate_enable;
}

/* ── Per-stream sequence-number enforcement ────────────────────────────────── */

void rcp_e2e_seq_tracker_init(rcp_e2e_seq_tracker_t *t)
{
    t->has_prev = false;
    t->prev_seq = 0;
}

//cfusa:req REQ-E2E-028
//cfusa:req REQ-E2E-029
rcp_e2e_seq_result_t rcp_e2e_seq_evaluate(rcp_e2e_seq_tracker_t *t, bool rx_enforce_seq,
                                           bool rx_seq_safestate_enable, uint8_t seq)
{
    rcp_e2e_seq_result_t r;
    uint8_t               fwd_distance; /* (seq - prev_seq) mod 256 */

    if (!t->has_prev) {
        /* Nothing to compare against yet: the first request on a stream
         * always accepts and can never itself be a discontinuity. */
        r.accept          = true;
        r.discontinuity   = false;
        r.enter_safe_state = false;
        t->has_prev = true;
        t->prev_seq = seq;
        return r;
    }

    fwd_distance = (uint8_t)(seq - t->prev_seq);

    /* RFC 1982 serial-number comparison: seq is "ahead" of prev_seq iff
     * their forward distance lies in the nearer half of the circle,
     * [1, 127] -- see this function's declaration-site doc comment for
     * the full wraparound rationale. */
    r.accept        = !rx_enforce_seq || (fwd_distance >= 1u && fwd_distance <= 127u);
    r.discontinuity = (fwd_distance != 1u);
    r.enter_safe_state = r.discontinuity && rx_seq_safestate_enable;

    /* Tracked state advances only on accept: t->prev_seq is specifically
     * "the previously ACCEPTED request on this stream" (this function's
     * own contract, matching REQ-E2E-028's text), not merely the last
     * seq observed. Advancing on a rejected (stale/replayed) seq would
     * drag the reference point backward and let a replay weaken
     * detection of the genuine next request; leaving it unmoved means a
     * rejected seq changes nothing about what the stream still expects
     * next. */
    if (r.accept) t->prev_seq = seq;
    return r;
}

/* ── rx_enforce_e2e: single-request drop vs. whole-stream latch-to-fault ────── */

//cfusa:req REQ-E2E-020
rcp_e2e_crc_action_t rcp_e2e_crc_error_action(bool rx_enforce_e2e)
{
    return rx_enforce_e2e ? RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT
                           : RCP_E2E_CRC_ACTION_DROP_REQUEST;
}

//cfusa:req REQ-E2E-043
void rcp_e2e_stream_fault_init(rcp_e2e_stream_fault_t *f)
{
    f->faulted = false;
}

//cfusa:req REQ-E2E-021
//cfusa:req REQ-E2E-022
bool rcp_e2e_stream_fault_on_crc_error(rcp_e2e_stream_fault_t *f, bool rx_enforce_e2e)
{
    if (rcp_e2e_crc_error_action(rx_enforce_e2e) == RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT) {
        f->faulted = true;
    }
    return true; /* a CRC_ERROR always skips this request's execution */
}

//cfusa:req REQ-E2E-044
bool rcp_e2e_stream_fault_is_faulted(const rcp_e2e_stream_fault_t *f)
{
    return f->faulted;
}

//cfusa:req REQ-E2E-023
void rcp_e2e_stream_fault_reset(rcp_e2e_stream_fault_t *f)
{
    f->faulted = false;
}

//cfusa:req REQ-E2E-045
bool rcp_e2e_crc_error_should_enter_safe_state(bool rx_enforce_e2e)
{
    return rx_enforce_e2e;
}

/* ── Per-stream fault tracker (issue #201, REQ-E2E-021) ─────────────────────── */

static rcp_e2e_stream_fault_tracker_slot_t *
stream_fault_tracker_find(rcp_e2e_stream_fault_tracker_t *t, uint64_t stream_id)
{
    size_t i;

    for (i = 0; i < RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS; i++) {
        if (t->slots[i].used && t->slots[i].stream_id == stream_id) return &t->slots[i];
    }
    return NULL;
}

static rcp_e2e_stream_fault_tracker_slot_t *
stream_fault_tracker_find_free(rcp_e2e_stream_fault_tracker_t *t)
{
    size_t i;

    for (i = 0; i < RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS; i++) {
        if (!t->slots[i].used) return &t->slots[i];
    }
    return NULL;
}

//cfusa:req REQ-E2E-021
void rcp_e2e_stream_fault_tracker_init(rcp_e2e_stream_fault_tracker_t *t)
{
    size_t i;

    for (i = 0; i < RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS; i++) {
        t->slots[i].used      = false;
        t->slots[i].stream_id = 0;
        rcp_e2e_stream_fault_init(&t->slots[i].fault);
    }
}

//cfusa:req REQ-E2E-021
bool rcp_e2e_stream_fault_tracker_on_crc_error(rcp_e2e_stream_fault_tracker_t *t,
                                                uint64_t stream_id, bool rx_enforce_e2e)
{
    rcp_e2e_stream_fault_tracker_slot_t *slot = stream_fault_tracker_find(t, stream_id);

    if (!slot) {
        slot = stream_fault_tracker_find_free(t);
        if (!slot) return false; /* capacity exhausted -- t left entirely unchanged */
        slot->used      = true;
        slot->stream_id = stream_id;
        rcp_e2e_stream_fault_init(&slot->fault);
    }

    return rcp_e2e_stream_fault_on_crc_error(&slot->fault, rx_enforce_e2e);
}

//cfusa:req REQ-E2E-021
bool rcp_e2e_stream_fault_tracker_is_faulted(const rcp_e2e_stream_fault_tracker_t *t,
                                              uint64_t stream_id)
{
    size_t i;

    for (i = 0; i < RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS; i++) {
        if (t->slots[i].used && t->slots[i].stream_id == stream_id) {
            return rcp_e2e_stream_fault_is_faulted(&t->slots[i].fault);
        }
    }
    return false; /* never seen -- vacuously not faulted */
}

//cfusa:req REQ-E2E-021
void rcp_e2e_stream_fault_tracker_reset(rcp_e2e_stream_fault_tracker_t *t, uint64_t stream_id)
{
    rcp_e2e_stream_fault_tracker_slot_t *slot = stream_fault_tracker_find(t, stream_id);

    if (slot) rcp_e2e_stream_fault_reset(&slot->fault);
}

/* ── Aggregate rx_stream_status (issue #201/#336, REQ-E2E-046) ─────────────── */

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_init(rcp_e2e_stream_status_t *s)
{
    rcp_e2e_stream_fault_init(&s->crc);
    s->seq_blocked      = false;
    s->wd_blocked       = false;
    s->overflow_blocked = false;
}

//cfusa:req REQ-E2E-046
bool rcp_e2e_stream_status_note_crc_error(rcp_e2e_stream_status_t *s, bool rx_enforce_e2e)
{
    return rcp_e2e_stream_fault_on_crc_error(&s->crc, rx_enforce_e2e);
}

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_note_seq(rcp_e2e_stream_status_t *s, rcp_e2e_seq_result_t result)
{
    if (result.enter_safe_state) s->seq_blocked = true;
}

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_note_wd(rcp_e2e_stream_status_t *s, rcp_e2e_wd_result_t result)
{
    if (result.enter_safe_state) s->wd_blocked = true;
}

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_note_overflow(rcp_e2e_stream_status_t *s, bool enter_safe_state)
{
    if (enter_safe_state) s->overflow_blocked = true;
}

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_reset_crc(rcp_e2e_stream_status_t *s)
{
    rcp_e2e_stream_fault_reset(&s->crc);
}

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_reset_seq(rcp_e2e_stream_status_t *s)
{
    s->seq_blocked = false;
}

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_reset_wd(rcp_e2e_stream_status_t *s)
{
    s->wd_blocked = false;
}

//cfusa:req REQ-E2E-046
void rcp_e2e_stream_status_reset_overflow(rcp_e2e_stream_status_t *s)
{
    s->overflow_blocked = false;
}

//cfusa:req REQ-E2E-046
bool rcp_e2e_stream_status_rx_blocked(const rcp_e2e_stream_status_t *s)
{
    return rcp_e2e_stream_fault_is_faulted(&s->crc) || s->seq_blocked || s->wd_blocked ||
           s->overflow_blocked;
}
