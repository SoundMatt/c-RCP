/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-E2E-031
//cfusa:test REQ-E2E-032
//cfusa:test REQ-E2E-033
//cfusa:test REQ-E2E-034
//cfusa:test REQ-E2E-035
//cfusa:test REQ-E2E-036
//cfusa:test REQ-E2E-037
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
//cfusa:test REQ-E2E-040
//cfusa:test REQ-E2E-041
//cfusa:test REQ-E2E-042

/*
 * test_tc18_gaps_e2e.c -- spec-literal conformance-and-deviation suite for
 * the TC18 sec. 13.6 ("End-to-End data protection / safe endpoints")
 * clauses catalogued during the requirements-corpus completeness pass
 * (.fusa-reqs.json REQ-E2E-031..042).
 *
 * Two kinds of test live here, and the difference matters when reading a
 * failure:
 *
 *   - For a requirement catalogued "implemented", the test asserts the
 *     clause literally, against values derived from the clause itself
 *     (the 8-octet stream_id + 4-octet avtp_timestamp big-endian CRC
 *     prefix, the +1-quadlet acf_msg_length adaptation, the 4-octet
 *     trailer) rather than round-tripping this module against itself.
 *
 *   - For a requirement catalogued "partial" or "not-implemented", the
 *     test PINS THE DEVIATION: it asserts the current, real, observable
 *     behaviour of this code, and the comment above it names the sec. 13.6
 *     clause that is not met and what a conforming implementation would
 *     do instead. Such a test failing means the behaviour changed --
 *     most likely because the gap was closed, in which case the test
 *     (and the requirement's status) must be rewritten to the conforming
 *     expectation.
 */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/e2e.h>
#include <rcp/regmap.h>
#include <rcp/scheduler.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Fixed, deliberately non-palindromic test vectors: byte-order mistakes
 * in either the stream_id or the avtp_timestamp change the CRC. */
#define TEST_SID ((uint64_t)0x0102030405060708ULL)
#define TEST_TS  ((uint32_t)0x11223344u)

/* Encodes one ACF_ABB message through acf.c's own encoder, i.e. exactly
 * the input rcp_e2e_wrap() documents itself as taking. */
static rcp_bytes_t make_abb(uint8_t ms, uint8_t rsp, uint16_t seg,
                            const uint8_t *payload, size_t payload_len)
{
    rcp_acf_byte_message_info_t h;

    memset(&h, 0, sizeof(h));
    h.byte_bus_id              = 0x11;
    h.transaction_num          = 0x22;
    h.op                       = (uint8_t)RCP_ACF_OP_WRITE;
    h.ms                       = ms;
    h.rsp                      = rsp;
    h.read_size_or_segment_num = seg;
    return rcp_acf_encode_abb(&h, payload, payload_len);
}

/* The 9-bit acf_msg_length quadlet count: bit 0 of octet 0 plus octet 1. */
static uint16_t len_field(const uint8_t *frame)
{
    return (uint16_t)(((uint16_t)(frame[0] & 0x01u) << 8) | (uint16_t)frame[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ── REQ-E2E-031: plain vs. safe command mode ──────────────────────────────── */

/* DEVIATION PIN. TC18 sec. 13.6 puts the plain/safe command-mode bit in the
 * server-owned generic endpoint config and makes it gate execution: in
 * safe command mode a request arriving without a valid CRC32 trailer must
 * not be executed. c-RCP's only such flag is ep_req_crc_enable, and it
 * sits in the CLIENT-writable functional block (regmap.h's
 * rcp_regmap_ep_functional_cfg_t, not rcp_regmap_ep_generic_cfg_t, whose
 * initializer below leaves it no field to live in), and no code path
 * consults it. A conforming implementation would reject the unprotected
 * request the last two assertions show sailing through. */
static void test_plain_vs_safe_mode_bit_is_inert(void)
{
    rcp_regmap_ep_functional_cfg_t fcfg;
    rcp_regmap_ep_generic_cfg_t    gcfg;
    rcp_acf_byte_message_info_t    hdr;
    rcp_bytes_t                    body    = {0};
    const uint8_t                  pl[4]   = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t                    plain   = make_abb(0, 0, 0, pl, sizeof(pl));
    const uint8_t                 *out_pl  = NULL;
    size_t                         out_len = 0;

    rcp_regmap_ep_generic_cfg_init(&gcfg);
    rcp_regmap_ep_functional_cfg_init(&fcfg);
    TEST_ASSERT_FALSE(fcfg.ep_req_crc_enable);
    TEST_ASSERT_FALSE(gcfg.ep_used);

    fcfg.ep_req_crc_enable = true; /* "this endpoint is in safe command mode" */

    /* Inert: the unprotected request still decodes, and would execute,
     * exactly as it does in plain command mode. */
    TEST_ASSERT_NOT_NULL(plain.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(plain.data, plain.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(pl), out_len);

    /* The only gate that exists is caller-driven: a caller that does
     * invoke rcp_e2e_unwrap() on the unprotected frame misreads its last
     * quadlet as a trailer and reports a mismatch. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, plain.data, plain.len, &body));

    rcp_bytes_free(&body);
    rcp_bytes_free(&plain);
}

/* ── REQ-E2E-032 / REQ-E2E-040: what safe mode changes, in both directions ─── */

/* Both catalogued "implemented": safe command mode alters nothing but the
 * appended trailer and the length accounting, and one identical scheme
 * serves requests and responses (no direction parameter exists). */
static void test_safe_mode_changes_only_trailer_and_length(void)
{
    const uint8_t pl[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t   req   = make_abb(0, 0, 0, pl, sizeof(pl)); /* request-shaped  */
    rcp_bytes_t   rsp   = make_abb(0, 1, 0, pl, sizeof(pl)); /* response-shaped */
    rcp_bytes_t   wreq  = rcp_e2e_wrap(TEST_SID, TEST_TS, req.data, req.len);
    rcp_bytes_t   wrsp  = rcp_e2e_wrap(TEST_SID, TEST_TS, rsp.data, rsp.len);

    TEST_ASSERT_NOT_NULL(wreq.data);
    TEST_ASSERT_NOT_NULL(wrsp.data);

    /* Exactly four octets longer, acf_msg_type preserved, and every octet
     * from the byte_message_info's third onward (i.e. all but the two
     * octets holding acf_msg_length) byte-identical to plain mode. */
    TEST_ASSERT_EQUAL_UINT(req.len + RCP_E2E_CRC_LEN, wreq.len);
    TEST_ASSERT_EQUAL_HEX8(req.data[0] & 0xFEu, wreq.data[0] & 0xFEu);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req.data + 2, wreq.data + 2, req.len - 2);
    TEST_ASSERT_EQUAL_UINT16(len_field(req.data) + 1u, len_field(wreq.data));

    /* The response direction differs in no respect at all. */
    TEST_ASSERT_EQUAL_UINT(rsp.len + RCP_E2E_CRC_LEN, wrsp.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rsp.data + 2, wrsp.data + 2, rsp.len - 2);
    TEST_ASSERT_EQUAL_UINT16(len_field(rsp.data) + 1u, len_field(wrsp.data));

    /* Same trailer derivation for both: CRC over the length-adapted frame,
     * big-endian, in the message's final quadlet. */
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SID, TEST_TS, wreq.data, req.len),
                            be32(wreq.data + req.len));
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SID, TEST_TS, wrsp.data, rsp.len),
                            be32(wrsp.data + rsp.len));

    rcp_bytes_free(&wrsp);
    rcp_bytes_free(&wreq);
    rcp_bytes_free(&rsp);
    rcp_bytes_free(&req);
}

/* ── REQ-E2E-034: the CRC coverage prefix ──────────────────────────────────── */

/* Catalogued "implemented": stream_id contributes 8 big-endian octets and
 * avtp_timestamp 4 (IEEE 1722's own field width), in that order, ahead of
 * the ACF frame. Asserted against a hand-built concatenation, plus two
 * negative controls that would pass if the width or the byte order were
 * wrong. */
static void test_crc_coverage_prefix_is_stream_id_8_then_timestamp_4(void)
{
    const uint8_t body[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t       be_ref[8 + 4 + 4];
    uint8_t       le_ts[8 + 4 + 4];
    uint8_t       ts8[8 + 8 + 4];
    uint32_t      actual;
    size_t        i;

    for (i = 0; i < 8; i++) be_ref[i] = (uint8_t)(TEST_SID >> (56u - 8u * i));
    for (i = 0; i < 4; i++) be_ref[8 + i] = (uint8_t)(TEST_TS >> (24u - 8u * i));
    memcpy(be_ref + 12, body, sizeof(body));

    memcpy(le_ts, be_ref, sizeof(le_ts));
    for (i = 0; i < 4; i++) le_ts[8 + i] = (uint8_t)(TEST_TS >> (8u * i));

    memcpy(ts8, be_ref, 8);
    for (i = 0; i < 8; i++) ts8[8 + i] = (uint8_t)((uint64_t)TEST_TS >> (56u - 8u * i));
    memcpy(ts8 + 16, body, sizeof(body));

    actual = rcp_e2e_compute_crc(TEST_SID, TEST_TS, body, sizeof(body));

    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_crc32(be_ref, sizeof(be_ref)), actual);
    /* Not little-endian, and not an 8-octet timestamp. */
    TEST_ASSERT_TRUE(rcp_e2e_crc32(le_ts, sizeof(le_ts)) != actual);
    TEST_ASSERT_TRUE(rcp_e2e_crc32(ts8, sizeof(ts8)) != actual);
}

/* ── REQ-E2E-035: the NTSCF all-zero timestamp stand-in ────────────────────── */

/* DEVIATION PIN. TC18 sec. 13.6 requires an NTSCF-framed message (an NTSCF
 * header carries no timestamp of its own) to contribute four all-zero
 * octets as its avtp_timestamp. c-RCP takes avtp_timestamp as a bare
 * uint32_t with no NTSCF/TSCF discriminator, so the stand-in is a caller
 * convention only: wrapping an NTSCF-framed message with a real timestamp
 * succeeds silently and yields a trailer a conforming peer rejects. A
 * conforming implementation would force the zero contribution for NTSCF
 * framing rather than trust the caller. */
static void test_ntscf_zero_timestamp_is_caller_convention_only(void)
{
    const uint8_t pl[4]   = {0x5A, 0x5B, 0x5C, 0x5D};
    rcp_bytes_t   frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t   as_spec = rcp_e2e_wrap(TEST_SID, 0u, frame.data, frame.len);
    rcp_bytes_t   as_told = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t   body    = {0};

    /* Both wraps succeed and are the same size: nothing rejects or
     * corrects the non-zero timestamp on an NTSCF-framed message. */
    TEST_ASSERT_NOT_NULL(as_spec.data);
    TEST_ASSERT_NOT_NULL(as_told.data);
    TEST_ASSERT_EQUAL_UINT(as_spec.len, as_told.len);
    TEST_ASSERT_TRUE(be32(as_spec.data + frame.len) != be32(as_told.data + frame.len));

    /* The error surfaces only as a mismatch at a peer applying the
     * all-zero stand-in the clause mandates. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                          rcp_e2e_unwrap(TEST_SID, 0u, as_told.data, as_told.len, &body));
    rcp_bytes_free(&body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, 0u, as_spec.data, as_spec.len, &body));

    rcp_bytes_free(&body);
    rcp_bytes_free(&as_told);
    rcp_bytes_free(&as_spec);
    rcp_bytes_free(&frame);
}

/* ── REQ-E2E-036: the +1-quadlet acf_msg_length adaptation ─────────────────── */

/* Catalogued "implemented": +1 quadlet on wrap (before the CRC is
 * computed), -1 on unwrap, fail-safe at both ends of the 9-bit field. */
static void test_acf_msg_length_adaptation_and_reversal(void)
{
    const uint8_t pl[4]      = {0x10, 0x20, 0x30, 0x40};
    rcp_bytes_t   frame      = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t   wrapped;
    rcp_bytes_t   body       = {0};
    /* acf_msg_length already at its 9-bit maximum (0x1FF): +1 overflows.
     * Octet 0 is (RCP_ACF_MSG_TYPE_ABB << 1) | length bit 8. */
    uint8_t       maxed[8]   = {0x1Du, 0xFFu, 0, 0, 0, 0, 0, 0};
    uint8_t       one_octet  = 0x1Cu;
    rcp_bytes_t   too_short;
    rcp_bytes_t   overflowed;

    /* 8-octet header + 4-octet payload = 3 quadlets; the trailer makes 4. */
    TEST_ASSERT_EQUAL_UINT(12u, frame.len);
    TEST_ASSERT_EQUAL_UINT16(3u, len_field(frame.data));

    wrapped = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL_UINT16(4u, len_field(wrapped.data));
    TEST_ASSERT_EQUAL_UINT16(3u, len_field(frame.data)); /* caller's copy untouched */

    /* unwrap() reverses it exactly: byte-identical to what wrap() got. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, wrapped.data, wrapped.len, &body));
    TEST_ASSERT_EQUAL_UINT(frame.len, body.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, body.data, frame.len);

    /* Fail safe: too short to hold the field, and adaptation overflow. */
    too_short = rcp_e2e_wrap(TEST_SID, TEST_TS, &one_octet, 1u);
    TEST_ASSERT_NULL(too_short.data);
    TEST_ASSERT_EQUAL_UINT(0u, too_short.len);
    overflowed = rcp_e2e_wrap(TEST_SID, TEST_TS, maxed, sizeof(maxed));
    TEST_ASSERT_NULL(overflowed.data);
    TEST_ASSERT_EQUAL_UINT(0u, overflowed.len);

    rcp_bytes_free(&body);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&frame);
}

/* ── REQ-E2E-033: per-ACF-message CRC in a multi-ACF AVTPDU ────────────────── */

/* DEVIATION PIN (partial). TC18 sec. 13.6 requires a separate CRC32 per
 * E2E-protected ACF message, verified individually. rcp_e2e_wrap()/
 * _unwrap() are at that granularity, and the first block below asserts
 * that; what is missing is any composition across a multi-member AVTPDU.
 * rcp_sched_split_frame_members() -- the only walker c-RCP has -- reports
 * the same two members whether or not their trailers verify, and neither
 * it nor rcp_mock_server_dispatch_frame() calls any e2e function. A
 * conforming server would verify each protected member as it walked. */
static void test_each_member_of_a_multi_acf_frame_carries_its_own_crc(void)
{
    const uint8_t a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    const uint8_t b[4] = {0xB1, 0xB2, 0xB3, 0xB4};
    rcp_bytes_t   m1   = make_abb(0, 0, 0, a, sizeof(a));
    rcp_bytes_t   m2   = make_abb(0, 0, 0, b, sizeof(b));
    rcp_bytes_t   w1   = rcp_e2e_wrap(TEST_SID, TEST_TS, m1.data, m1.len);
    rcp_bytes_t   w2   = rcp_e2e_wrap(TEST_SID, TEST_TS, m2.data, m2.len);
    rcp_bytes_t   body = {0};
    uint8_t       joined[32];
    size_t        offs[4];

    TEST_ASSERT_EQUAL_UINT(16u, w1.len);
    TEST_ASSERT_EQUAL_UINT(16u, w2.len);
    memcpy(joined, w1.data, w1.len);
    memcpy(joined + w1.len, w2.data, w2.len);

    TEST_ASSERT_EQUAL_UINT(2u, rcp_sched_split_frame_members(joined, sizeof(joined), offs, 4));
    TEST_ASSERT_EQUAL_UINT(0u, offs[0]);
    TEST_ASSERT_EQUAL_UINT(16u, offs[1]);

    /* Each member verifies against its own trailer, not one CRC across
     * the whole payload. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, joined, 16u, &body));
    rcp_bytes_free(&body);

    /* Corrupt only the second member's trailer. */
    joined[31] ^= 0xFFu;
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, joined, 16u, &body));
    rcp_bytes_free(&body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, joined + 16u, 16u, &body));
    /* ...yet the frame walker still reports two good members. */
    TEST_ASSERT_EQUAL_UINT(2u, rcp_sched_split_frame_members(joined, sizeof(joined), offs, 4));

    rcp_bytes_free(&body);
    rcp_bytes_free(&w2);
    rcp_bytes_free(&w1);
    rcp_bytes_free(&m2);
    rcp_bytes_free(&m1);
}

/* ── REQ-E2E-037: the AVTPDU data-length adjustment ────────────────────────── */

/* DEVIATION PIN (partial). TC18 sec. 13.6 requires the AVTPDU's
 * ntscf_data_length / stream_data_length to be raised by 4 octets for
 * every E2E-protected ACF message it carries. In c-RCP that is implicit
 * only: rcp_avtp_encode_ntscf() recomputes the field from whatever
 * payload it is handed (it ignores hdr.ntscf_data_length entirely, as the
 * bogus value below shows), so the +4-per-protected-member accounting is
 * right if and only if the caller wrapped every protected member first.
 * No function expresses the adjustment and nothing checks it. */
static void test_avtpdu_data_length_grows_four_octets_per_protected_member(void)
{
    const uint8_t             p[4] = {0xC1, 0xC2, 0xC3, 0xC4};
    rcp_bytes_t               m    = make_abb(0, 0, 0, p, sizeof(p));
    rcp_bytes_t               w    = rcp_e2e_wrap(TEST_SID, TEST_TS, m.data, m.len);
    uint8_t                   plain_payload[24];
    uint8_t                   safe_payload[32];
    rcp_avtp_ntscf_header_t   hdr;
    rcp_avtp_ntscf_header_t   got;
    rcp_bytes_t               enc_plain;
    rcp_bytes_t               enc_safe;
    const uint8_t            *pl  = NULL;
    size_t                    len = 0;

    memcpy(plain_payload, m.data, m.len);
    memcpy(plain_payload + m.len, m.data, m.len);
    memcpy(safe_payload, w.data, w.len);
    memcpy(safe_payload + w.len, w.data, w.len);

    memset(&hdr, 0, sizeof(hdr));
    hdr.sv                = 1;
    hdr.stream_id         = rcp_stream_id_from_u64(TEST_SID);
    hdr.ntscf_data_length = 0x7FFu; /* deliberately wrong: encode ignores it */

    enc_plain = rcp_avtp_encode_ntscf(&hdr, plain_payload, sizeof(plain_payload));
    enc_safe  = rcp_avtp_encode_ntscf(&hdr, safe_payload, sizeof(safe_payload));
    TEST_ASSERT_NOT_NULL(enc_plain.data);
    TEST_ASSERT_NOT_NULL(enc_safe.data);

    TEST_ASSERT_EQUAL_INT(RCP_AVTP_OK,
                          rcp_avtp_decode_ntscf(enc_plain.data, enc_plain.len, &got, &pl, &len));
    TEST_ASSERT_EQUAL_UINT16(24u, got.ntscf_data_length);
    TEST_ASSERT_EQUAL_INT(RCP_AVTP_OK,
                          rcp_avtp_decode_ntscf(enc_safe.data, enc_safe.len, &got, &pl, &len));
    /* Two protected members: exactly 2 * RCP_E2E_CRC_LEN more. */
    TEST_ASSERT_EQUAL_UINT16(24u + 2u * RCP_E2E_CRC_LEN, got.ntscf_data_length);

    rcp_bytes_free(&enc_safe);
    rcp_bytes_free(&enc_plain);
    rcp_bytes_free(&w);
    rcp_bytes_free(&m);
}

/* ── REQ-E2E-038: the fragmentation coverage rule ──────────────────────────── */

/* DEVIATION PIN (not implemented). TC18 sec. 13.6 requires a fragmented
 * message's CRC32 to span the FIRST AVTPDU's stream_id/avtp_timestamp and
 * the FIRST fragment's ACF header, followed by the concatenated
 * byte_msg_payload of EVERY segment in order. c-RCP's documented
 * integration (fragment.h) wraps only the final fragment's own encoded
 * bytes, so the trailer covers the LAST fragment's header and payload
 * alone and every earlier segment is entirely unprotected -- as the last
 * two assertions show: mutating segment 0's payload changes the CRC a
 * conforming implementation would have computed, while this one still
 * verifies the message as good. */
static void test_fragmented_crc_covers_only_the_last_fragment(void)
{
    const uint8_t p0[4] = {0xF0, 0xF1, 0xF2, 0xF3};
    const uint8_t p1[4] = {0xE0, 0xE1, 0xE2, 0xE3};
    const uint8_t p2[4] = {0xD0, 0xD1, 0xD2, 0xD3};
    rcp_bytes_t   f0    = make_abb(1, 0, 0, p0, sizeof(p0)); /* ms=1, segment 0 */
    rcp_bytes_t   f1    = make_abb(1, 0, 1, p1, sizeof(p1)); /* ms=1, segment 1 */
    rcp_bytes_t   f2    = make_abb(0, 0, 2, p2, sizeof(p2)); /* ms=0, final     */
    rcp_bytes_t   w2;
    rcp_bytes_t   body  = {0};
    uint8_t       span[8 + 4 + 4 + 4]; /* first fragment's ACF header + all payloads */
    uint32_t      conforming;

    TEST_ASSERT_TRUE(rcp_e2e_fragment_carries_crc(true));
    w2 = rcp_e2e_wrap(TEST_SID, TEST_TS, f2.data, f2.len);
    TEST_ASSERT_NOT_NULL(w2.data);

    /* What c-RCP actually computes: the final fragment's bytes alone. */
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SID, TEST_TS, w2.data, f2.len),
                            be32(w2.data + f2.len));

    memcpy(span, f0.data, 8);
    memcpy(span + 8, p0, 4);
    memcpy(span + 12, p1, 4);
    memcpy(span + 16, p2, 4);
    conforming = rcp_e2e_compute_crc(TEST_SID, TEST_TS, span, sizeof(span));
    TEST_ASSERT_TRUE(conforming != be32(w2.data + f2.len));

    /* Segment 0 is unprotected: its corruption moves the conforming CRC
     * but leaves this implementation's verification happy. */
    span[8] ^= 0xFFu;
    TEST_ASSERT_TRUE(rcp_e2e_compute_crc(TEST_SID, TEST_TS, span, sizeof(span)) != conforming);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, w2.data, w2.len, &body));

    rcp_bytes_free(&body);
    rcp_bytes_free(&w2);
    rcp_bytes_free(&f2);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
}

/* ── REQ-E2E-039: the ms bit / trailer binding ─────────────────────────────── */

/* DEVIATION PIN (partial). TC18 sec. 13.6 binds the trailer to the ACF
 * header's ms bit: ms=0 carries the CRC32 in its last quadlet, ms=1
 * carries none. c-RCP expresses only half of that -- the binding below
 * holds when a caller derives the argument from a decoded ms bit itself,
 * but rcp_e2e_fragment_carries_crc() takes a bare bool, is called nowhere
 * in src/, and rcp_e2e_wrap() never reads ms: it appends a trailer to an
 * ms=1 (non-final) fragment just as readily. A conforming implementation
 * would refuse to protect a fragment whose ms bit is set. */
static void test_ms_bit_to_carries_crc_binding_is_not_enforced(void)
{
    const uint8_t               pl[4] = {0x71, 0x72, 0x73, 0x74};
    rcp_bytes_t                 last  = make_abb(0, 0, 3, pl, sizeof(pl));
    rcp_bytes_t                 mid   = make_abb(1, 0, 1, pl, sizeof(pl));
    rcp_acf_byte_message_info_t h_last;
    rcp_acf_byte_message_info_t h_mid;
    const uint8_t              *out_pl  = NULL;
    size_t                      out_len = 0;
    rcp_bytes_t                 wmid;

    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(last.data, last.len, &h_last, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(mid.data, mid.len, &h_mid, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(0u, h_last.ms);
    TEST_ASSERT_EQUAL_UINT8(1u, h_mid.ms);

    /* The binding, driven off a real decoded ms bit. */
    TEST_ASSERT_TRUE(rcp_e2e_fragment_carries_crc(!h_last.ms));
    TEST_ASSERT_FALSE(rcp_e2e_fragment_carries_crc(!h_mid.ms));

    /* Unenforced: wrap() protects the ms=1 fragment anyway, leaving its ms
     * bit (octet 6, bit 4) set alongside a trailer sec. 13.6 forbids. */
    wmid = rcp_e2e_wrap(TEST_SID, TEST_TS, mid.data, mid.len);
    TEST_ASSERT_NOT_NULL(wmid.data);
    TEST_ASSERT_EQUAL_UINT(mid.len + RCP_E2E_CRC_LEN, wmid.len);
    TEST_ASSERT_EQUAL_HEX8(0x10u, wmid.data[6] & 0x10u);

    rcp_bytes_free(&wmid);
    rcp_bytes_free(&mid);
    rcp_bytes_free(&last);
}

/* ── REQ-E2E-041: CRC_ERROR handling ───────────────────────────────────────── */

/* DEVIATION PIN (partial). TC18 sec. 13.6 requires a CRC mismatch on a
 * protected stream both to skip execution AND to generate an error
 * response carrying the CRC-mismatch code (POCI_FAILURE, 12, per sec.
 * 12.9.6 Table 27). c-RCP does the first two halves -- the mismatch is
 * reported and maps to code 12 -- but has no caller of rcp_e2e_unwrap()
 * and no error-response emitter, so nothing turns a mismatch into a
 * frame. All that comes back is a diagnostic copy of the REQUEST: its
 * err and rsp bits are still clear, i.e. it is not a Response at all. */
static void test_crc_mismatch_skips_execution_without_error_response(void)
{
    const uint8_t               pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                 frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                 w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                 body  = {0};
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *out_pl  = NULL;
    size_t                      out_len = 0;
    rcp_e2e_errc_t              rc;

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    rc = rcp_e2e_unwrap(TEST_SID, TEST_TS, w.data, w.len, &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH, rc); /* execution skipped */
    TEST_ASSERT_EQUAL_INT(12, (int)RCP_ERROR_POCI_FAILURE);
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_POCI_FAILURE, rcp_e2e_wire_error(rc));

    /* No response is generated: what comes back is the request itself. */
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(body.data, body.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(0u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(0u, hdr.rsp);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, body.data, frame.len);

    rcp_bytes_free(&body);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
}

/* ── REQ-E2E-042: pad coverage and quadlet alignment ───────────────────────── */

/* Half implemented, half deviation. TC18 sec. 13.6 Figures 19/20 compute
 * the CRC over whole quadlets -- header quadlets plus byte_msg_payload
 * INCLUDING the 0x00 pad octets -- so that the CRC itself occupies the
 * message's final whole quadlet. The pad octets are covered (mutating one
 * changes the CRC). Alignment, though, is not enforced: rcp_e2e_wrap()
 * accepts any acf_frame_len >= 2 and appends the trailer at that offset,
 * so a caller passing an unpadded frame gets a misaligned trailer whose
 * adapted acf_msg_length no longer describes the message. A conforming
 * implementation would reject a frame that is not a whole quadlet. */
static void test_crc_covers_pad_octets_but_alignment_is_unenforced(void)
{
    const uint8_t               pl[5] = {0x01, 0x02, 0x03, 0x04, 0x05};
    rcp_bytes_t                 frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *out_pl  = NULL;
    size_t                      out_len = 0;
    uint32_t                    with_zero_pad;
    uint8_t                     misaligned[6] = {0x1Cu, 0x02u, 0, 0, 0, 0};
    rcp_bytes_t                 mis;

    /* 8 + 5 = 13 octets, so 3 pad octets bring it to 4 whole quadlets. */
    TEST_ASSERT_EQUAL_UINT(16u, frame.len);
    TEST_ASSERT_EQUAL_UINT8(3u, rcp_acf_pad_len(8u + sizeof(pl)));
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(3u, hdr.pad);
    TEST_ASSERT_EQUAL_UINT8(0u, frame.data[15]);

    /* The pad octets are inside the coverage span. */
    with_zero_pad = rcp_e2e_compute_crc(TEST_SID, TEST_TS, frame.data, frame.len);
    frame.data[15] = 0xFFu;
    TEST_ASSERT_TRUE(rcp_e2e_compute_crc(TEST_SID, TEST_TS, frame.data, frame.len) !=
                     with_zero_pad);

    /* Unenforced: 6 octets is not a whole quadlet, yet wrap() succeeds and
     * puts the trailer at octets 6..9, straddling a quadlet boundary. */
    mis = rcp_e2e_wrap(TEST_SID, TEST_TS, misaligned, sizeof(misaligned));
    TEST_ASSERT_NOT_NULL(mis.data);
    TEST_ASSERT_EQUAL_UINT(10u, mis.len);
    TEST_ASSERT_EQUAL_UINT(2u, mis.len % 4u);

    rcp_bytes_free(&mis);
    rcp_bytes_free(&frame);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_plain_vs_safe_mode_bit_is_inert);
    RUN_TEST(test_safe_mode_changes_only_trailer_and_length);
    RUN_TEST(test_crc_coverage_prefix_is_stream_id_8_then_timestamp_4);
    RUN_TEST(test_ntscf_zero_timestamp_is_caller_convention_only);
    RUN_TEST(test_acf_msg_length_adaptation_and_reversal);
    RUN_TEST(test_each_member_of_a_multi_acf_frame_carries_its_own_crc);
    RUN_TEST(test_avtpdu_data_length_grows_four_octets_per_protected_member);
    RUN_TEST(test_fragmented_crc_covers_only_the_last_fragment);
    RUN_TEST(test_ms_bit_to_carries_crc_binding_is_not_enforced);
    RUN_TEST(test_crc_mismatch_skips_execution_without_error_response);
    RUN_TEST(test_crc_covers_pad_octets_but_alignment_is_unenforced);
    return UNITY_END();
}
