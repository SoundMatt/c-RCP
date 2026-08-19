/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-E2E-001
//cfusa:test REQ-E2E-002
//cfusa:test REQ-E2E-003
//cfusa:test REQ-E2E-004
//cfusa:test REQ-E2E-005
//cfusa:test REQ-E2E-006
//cfusa:test REQ-E2E-007
//cfusa:test REQ-E2E-008
//cfusa:test REQ-E2E-009
//cfusa:test REQ-E2E-010
//cfusa:test REQ-E2E-011
//cfusa:test REQ-E2E-012
//cfusa:test REQ-E2E-013
//cfusa:test REQ-E2E-014
//cfusa:test REQ-E2E-015
//cfusa:test REQ-E2E-016
//cfusa:test REQ-E2E-017
//cfusa:test REQ-E2E-018
//cfusa:test REQ-E2E-019
//cfusa:test REQ-E2E-020
//cfusa:test REQ-E2E-021
//cfusa:test REQ-E2E-022
//cfusa:test REQ-E2E-023
//cfusa:test REQ-E2E-043
//cfusa:test REQ-E2E-044
//cfusa:test REQ-E2E-024
//cfusa:test REQ-E2E-025
//cfusa:test REQ-E2E-026
//cfusa:test REQ-E2E-027
//cfusa:test REQ-WIREERR-003
//cfusa:test REQ-E2E-046
// Security-relevant subset (CYBERSECURITY.md §1.3, Layer 3 — E2E Safe
// Points and Safety-Request Execution Gating): CRC32 frame integrity,
// safety-tagged execution gating, and per-stream watchdog behavior.
// See CYBERSECURITY.md and tara.md TS-002 (replay is explicitly NOT
// covered here -- no requirement in this subset claims replay
// mitigation).
//cfusa:sec-test REQ-E2E-011
//cfusa:sec-test REQ-E2E-012
//cfusa:sec-test REQ-E2E-014
//cfusa:sec-test REQ-E2E-015
//cfusa:sec-test REQ-E2E-020
//cfusa:sec-test REQ-E2E-021
//cfusa:sec-test REQ-E2E-022
//cfusa:sec-test REQ-E2E-023
//cfusa:sec-test REQ-E2E-024
//cfusa:sec-test REQ-E2E-025
//cfusa:sec-test REQ-E2E-026
//cfusa:sec-test REQ-E2E-027
#include "unity.h"

#include "../src/mem_bounded.h"

#include <rcp/alloc.h>
#include <rcp/e2e.h>
#include <rcp/request_sequencer.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    const char *a   = rcp_e2e_strerror(RCP_E2E_OK);
    const char *b   = rcp_e2e_strerror(RCP_E2E_ERR_SHORT_FRAME);
    const char *c   = rcp_e2e_strerror(RCP_E2E_ERR_CRC_MISMATCH);
    const char *unk = rcp_e2e_strerror((rcp_e2e_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
}

/* ── wire error code mapping (c-RCP-04) ───────────────────────────────────── */

static void test_wire_error_crc_mismatch_maps_to_poci_failure(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_POCI_FAILURE,
                           rcp_e2e_wire_error(RCP_E2E_ERR_CRC_MISMATCH));
}

static void test_wire_error_ok_and_short_frame_map_to_none(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_NONE, rcp_e2e_wire_error(RCP_E2E_OK));
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_NONE, rcp_e2e_wire_error(RCP_E2E_ERR_SHORT_FRAME));
}

/* ── CRC32 known-answer test ───────────────────────────────────────────────── */

static void test_crc32_known_answer_vector(void)
{
    /* CRC-32/AUTOSAR's published check value -- this module's parameter
     * set (poly 0xF4ACFB13, init/xorout 0xFFFFFFFF, refin/refout true)
     * happens to coincide with that catalog entry exactly. */
    const uint8_t vec[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0x1697D06Au, rcp_e2e_crc32(vec, 9));
}

static void test_crc32_empty_input(void)
{
    /* CRC of nothing is init XORed with final XOR, i.e. 0. */
    TEST_ASSERT_EQUAL_HEX32(0u, rcp_e2e_crc32(NULL, 0));
}

static void test_crc32_differs_for_different_data(void)
{
    const uint8_t a[] = {0x01, 0x02, 0x03};
    const uint8_t b[] = {0x01, 0x02, 0x04};
    TEST_ASSERT_NOT_EQUAL(rcp_e2e_crc32(a, sizeof(a)), rcp_e2e_crc32(b, sizeof(b)));
}

/* ── compute_crc coverage span ────────────────────────────────────────────── */

static void test_compute_crc_matches_manual_concatenation(void)
{
    /* issue #465: the coverage span now leads with avtp_subtype +
     * header_octet1 + a tu byte (Figure 20/21's own orange header-CRC
     * bytes), ahead of stream_id/avtp_timestamp. */
    uint8_t concat[1 + 1 + 1 + 8 + 4 + 5];
    uint8_t acf_frame[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    uint8_t avtp_subtype    = 0x05u; /* TSCF */
    uint8_t header_octet1   = 0x91u; /* arbitrary sv|version|mr|rsv|tv byte */
    bool    tu              = true;
    uint64_t stream_id       = 0x0102030405060708ULL;
    uint32_t avtp_timestamp  = 0x11223344u;
    size_t i;

    concat[0] = avtp_subtype;
    concat[1] = header_octet1;
    concat[2] = tu ? 0x01u : 0x00u;
    for (i = 0; i < 8; i++) concat[3 + i]  = (uint8_t)(stream_id >> (56 - 8 * i));
    for (i = 0; i < 4; i++) concat[11 + i] = (uint8_t)(avtp_timestamp >> (24 - 8 * i));
    rcp_memcpy_bounded(concat + 15, sizeof(concat) - 15, acf_frame, sizeof(acf_frame));

    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_crc32(concat, sizeof(concat)),
                             rcp_e2e_compute_crc(avtp_subtype, header_octet1, tu,
                                                     stream_id, avtp_timestamp, acf_frame,
                                                     sizeof(acf_frame)));
}

static void test_compute_crc_zero_stream_and_timestamp_ntscf_standin(void)
{
    /* The all-zero StreamID/avtp_timestamp stand-in an NTSCF-framed
     * message uses (rcp_e2e_wrap_framed()'s own job -- this raw function
     * just takes whatever it's given) should compute identically to an
     * explicit zero-filled 12-byte prefix (8-byte StreamID + 4-byte
     * avtp_timestamp), still preceded by avtp_subtype/header_octet1/tu
     * (issue #465). */
    uint8_t acf_frame[3] = {0x01, 0x02, 0x03};
    uint8_t avtp_subtype  = 0x82u; /* NTSCF */
    uint8_t header_octet1 = 0x80u;
    uint8_t prefix[3 + 12] = {0};
    uint8_t concat[3 + 12 + 3];

    prefix[0] = avtp_subtype;
    prefix[1] = header_octet1;
    prefix[2] = 0x00u; /* tu = false */
    rcp_memcpy_bounded(concat, sizeof(concat), prefix, sizeof(prefix));
    rcp_memcpy_bounded(concat + sizeof(prefix), sizeof(concat) - sizeof(prefix), acf_frame, 3);

    TEST_ASSERT_EQUAL_HEX32(
        rcp_e2e_crc32(concat, sizeof(concat)),
        rcp_e2e_compute_crc(avtp_subtype, header_octet1, false, 0, 0, acf_frame, 3));
}

static void test_compute_crc_timestamp_is_4_octets_not_8(void)
{
    /* c-RCP-01: the CRC must fold avtp_timestamp in as exactly 4
     * octets (its real IEEE 1722 wire width), not 8. A timestamp value
     * with any of its high 32 bits set is meaningless for a uint32_t
     * parameter and can't be constructed here, but this pins the
     * *width actually fed to the CRC*: the result must match a manual
     * concatenation using a 4-byte big-endian timestamp, and must NOT
     * match one using an 8-byte big-endian encoding of the same value
     * (the pre-fix behavior). Still preceded by the 3 header-CRC bytes
     * (issue #465). */
    uint8_t acf_frame[2] = {0x55, 0x66};
    uint8_t avtp_subtype  = 0x05u;
    uint8_t header_octet1 = 0x00u;
    uint32_t avtp_timestamp = 0xCAFEBABEu;
    uint8_t concat4[3 + 8 + 4 + 2];
    uint8_t concat8[3 + 8 + 8 + 2];
    size_t i;

    memset(concat4, 0, sizeof(concat4));
    memset(concat8, 0, sizeof(concat8));
    concat4[0] = concat8[0] = avtp_subtype;
    concat4[1] = concat8[1] = header_octet1;
    concat4[2] = concat8[2] = 0x00u; /* tu = false */
    for (i = 0; i < 4; i++) concat4[11 + i] = (uint8_t)(avtp_timestamp >> (24 - 8 * i));
    rcp_memcpy_bounded(concat4 + 15, sizeof(concat4) - 15, acf_frame, sizeof(acf_frame));

    for (i = 0; i < 4; i++) concat8[3 + 8 + 4 + i] = (uint8_t)(avtp_timestamp >> (24 - 8 * i));
    rcp_memcpy_bounded(concat8 + 19, sizeof(concat8) - 19, acf_frame, sizeof(acf_frame));

    TEST_ASSERT_EQUAL_HEX32(
        rcp_e2e_crc32(concat4, sizeof(concat4)),
        rcp_e2e_compute_crc(avtp_subtype, header_octet1, false, 0, avtp_timestamp, acf_frame,
                             sizeof(acf_frame)));
    TEST_ASSERT_NOT_EQUAL(
        rcp_e2e_crc32(concat8, sizeof(concat8)),
        rcp_e2e_compute_crc(avtp_subtype, header_octet1, false, 0, avtp_timestamp, acf_frame,
                             sizeof(acf_frame)));
}

/* ── issue #465: Figure 20/21 header-CRC bytes actually feed the CRC ────────
 *
 * TC18 §13.6 Figures 20/21 (rendered page, not plain-text extraction --
 * see e2e.h's file header) mark avtp_subtype, header_octet1 (TSCF's
 * sv|version|mr|rsv|tv / NTSCF's sv|version|r byte), and the tu bit
 * orange: header-CRC input, ahead of stream_id/avtp_timestamp. Each test
 * below flips exactly one of those three inputs, holding every other
 * argument byte-identical, and asserts the CRC32 result differs --
 * proving each one is actually being fed into the running CRC, not
 * silently ignored (the pre-fix defect this issue reported). */

static void test_compute_crc_avtp_subtype_changes_result(void)
{
    uint8_t acf_frame[4] = {0x10, 0x20, 0x30, 0x40};
    uint32_t tscf_crc  = rcp_e2e_compute_crc(0x05u, 0x00u, false, 42u, 7u, acf_frame,
                                              sizeof(acf_frame));
    uint32_t ntscf_crc = rcp_e2e_compute_crc(0x82u, 0x00u, false, 42u, 7u, acf_frame,
                                              sizeof(acf_frame));
    TEST_ASSERT_NOT_EQUAL(tscf_crc, ntscf_crc);
}

static void test_compute_crc_header_octet1_changes_result(void)
{
    uint8_t acf_frame[4] = {0x10, 0x20, 0x30, 0x40};
    uint32_t a = rcp_e2e_compute_crc(0x05u, 0x00u, false, 42u, 7u, acf_frame, sizeof(acf_frame));
    uint32_t b = rcp_e2e_compute_crc(0x05u, 0x01u, false, 42u, 7u, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_EQUAL(a, b);
}

static void test_compute_crc_tu_bit_changes_result(void)
{
    uint8_t acf_frame[4] = {0x10, 0x20, 0x30, 0x40};
    uint32_t tu_false = rcp_e2e_compute_crc(0x05u, 0x00u, false, 42u, 7u, acf_frame,
                                             sizeof(acf_frame));
    uint32_t tu_true  = rcp_e2e_compute_crc(0x05u, 0x00u, true, 42u, 7u, acf_frame,
                                             sizeof(acf_frame));
    TEST_ASSERT_NOT_EQUAL(tu_false, tu_true);
}

/* ── length_with_crc ───────────────────────────────────────────────────────── */

static void test_length_with_crc_adds_trailer_size(void)
{
    TEST_ASSERT_EQUAL_UINT((size_t)0 + RCP_E2E_CRC_LEN, rcp_e2e_length_with_crc(0));
    TEST_ASSERT_EQUAL_UINT((size_t)100 + RCP_E2E_CRC_LEN, rcp_e2e_length_with_crc(100));
}

static void test_length_with_crc_saturates(void)
{
    TEST_ASSERT_EQUAL_UINT((size_t)-1, rcp_e2e_length_with_crc((size_t)-1));
    TEST_ASSERT_EQUAL_UINT((size_t)-1, rcp_e2e_length_with_crc((size_t)-1 - 1));
}

/* ── wrap / unwrap round trip ──────────────────────────────────────────────── */

/* A minimal, syntactically-plausible ACF_ABB header-and-payload region:
 * byte 0 = acf_msg_type (arbitrary here, this module doesn't inspect
 * it), bytes 1-2 = acf_msg_length (the field rcp_e2e_wrap()/_unwrap()
 * adapt), followed by whatever the rest of the header-and-payload
 * happens to be. acf_msg_length is set to payload_len (5, matching
 * acf.c's own encode_abb() convention of payload-only, header
 * excluded) so the adaptation assertions below have a real value to
 * check against.
 */
/* This module (e2e.c) is deliberately agnostic to acf.h's actual header
 * semantics -- it only reads/writes the acf_msg_length field by
 * documented bit position (octet 0 bit 0 | octet 1, a 9-bit quadlet
 * count -- see e2e.c's adapt_acf_msg_length()) and never validates it
 * against a real ACF frame's true length. This helper's bytes therefore
 * don't need to describe a *valid* ACF message, only consistent,
 * recognizable ones: out[0]'s top 7 bits are a placeholder acf_msg_type
 * (0x0E, RCP_ACF_MSG_TYPE_ABB -- this module doesn't care), out[0] bit 0
 * | out[1] is a placeholder acf_msg_length this test suite's own
 * adaptation tests read back by the same bit position. */
static void make_test_acf_frame(uint8_t *out, size_t out_len)
{
    size_t i;
    TEST_ASSERT_TRUE(out_len >= 3);
    out[0] = 0x1Cu; /* (0x0E << 1) | 0: acf_msg_type=ABB, length MSB=0 */
    out[1] = 0x00u; /* placeholder acf_msg_length, low 8 bits */
    out[2] = (uint8_t)(out_len - 8u); /* pretend header is 8 bytes; payload = rest */
    for (i = 3; i < out_len; i++) out[i] = (uint8_t)(i & 0xFFu);
}

/* ── issue #465: rcp_e2e_wrap_framed()/_unwrap_framed() NTSCF tu stand-in ─── */

static void test_wrap_framed_forces_tu_false_under_ntscf(void)
{
    /* Mirrors this file's existing avtp_timestamp-is-zeroed-under-NTSCF
     * convention (see rcp_e2e_wrap_framed()'s own doc comment): a caller
     * passing tu=true under is_ntscf_framed=true must still get the
     * SAME wire bytes as tu=false, since NTSCF has no tu bit to carry
     * it -- unlike a TSCF-framed call, where tu really does change the
     * output. */
    uint8_t acf_frame[8];
    rcp_bytes_t ntscf_tu_true;
    rcp_bytes_t ntscf_tu_false;
    rcp_bytes_t tscf_tu_true;
    rcp_bytes_t tscf_tu_false;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    ntscf_tu_true  = rcp_e2e_wrap_framed(1u, true, 0x00u, true, 999u, acf_frame,
                                          sizeof(acf_frame));
    ntscf_tu_false = rcp_e2e_wrap_framed(1u, true, 0x00u, false, 999u, acf_frame,
                                          sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(ntscf_tu_true.data);
    TEST_ASSERT_EQUAL_UINT(ntscf_tu_true.len, ntscf_tu_false.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ntscf_tu_false.data, ntscf_tu_true.data, ntscf_tu_true.len);

    tscf_tu_true  = rcp_e2e_wrap_framed(1u, false, 0x00u, true, 999u, acf_frame,
                                         sizeof(acf_frame));
    tscf_tu_false = rcp_e2e_wrap_framed(1u, false, 0x00u, false, 999u, acf_frame,
                                         sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(tscf_tu_true.data);
    TEST_ASSERT_EQUAL_UINT(tscf_tu_true.len, tscf_tu_false.len);
    /* Real TSCF traffic: tu does change the wire trailer -- the last
     * RCP_E2E_CRC_LEN bytes (the CRC32 trailer) must differ. */
    TEST_ASSERT_NOT_EQUAL(
        0, memcmp(tscf_tu_true.data + (tscf_tu_true.len - RCP_E2E_CRC_LEN),
                  tscf_tu_false.data + (tscf_tu_false.len - RCP_E2E_CRC_LEN), RCP_E2E_CRC_LEN));

    rcp_bytes_free(&ntscf_tu_true);
    rcp_bytes_free(&ntscf_tu_false);
    rcp_bytes_free(&tscf_tu_true);
    rcp_bytes_free(&tscf_tu_false);
}

static void test_wrap_appends_crc_len_bytes(void)
{
    /* Must be quadlet-aligned (a multiple of 4) -- REQ-E2E-042's fix
     * rejects wrap() calls that aren't, matching every real ACF-encoded
     * frame's own already-padded length. */
    uint8_t acf_frame[16];
    rcp_bytes_t out;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    out = rcp_e2e_wrap(0x05u, 0x00u, false, 42, 7, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(out.data);
    TEST_ASSERT_EQUAL_UINT(sizeof(acf_frame) + RCP_E2E_CRC_LEN, out.len);

    rcp_bytes_free(&out);
}

static void test_wrap_null_frame_nonzero_len_fails_safe(void)
{
    rcp_bytes_t out = rcp_e2e_wrap(0x05u, 0x00u, false, 1, 1, NULL, 4);
    TEST_ASSERT_NULL(out.data);
    TEST_ASSERT_EQUAL_UINT(0, out.len);
}

static void test_wrap_too_short_for_length_field_fails_safe(void)
{
    /* c-RCP-02: the adaptation needs at least 2 octets (acf_msg_length
     * spans bit 0 of octet 0 and all of octet 1 -- see
     * adapt_acf_msg_length()) to write into. A 0/1-byte "frame" has
     * nowhere for acf_msg_length to live, so wrap must fail safe rather
     * than silently skip the adaptation and produce a non-conformant
     * frame. */
    uint8_t tiny[1] = {0xAA};
    rcp_bytes_t out = rcp_e2e_wrap(0x05u, 0x00u, false, 1, 1, tiny, sizeof(tiny));
    TEST_ASSERT_NULL(out.data);
    TEST_ASSERT_EQUAL_UINT(0, out.len);
}

/* c-RCP-02: wrap() must adapt acf_msg_length by +1 quadlet before
 * appending the trailer, and unwrap() must adapt it back down -- this
 * is the actual defect this test targets, distinct from the
 * "round-trips at all" check below. */
static void test_wrap_adapts_acf_msg_length_by_one_quadlet(void)
{
    /* Must be quadlet-aligned -- see test_wrap_appends_crc_len_bytes()'s
     * own comment. */
    uint8_t acf_frame[16];
    uint8_t original_type_bits;
    uint16_t original_len;
    uint8_t adapted_type_bits;
    uint16_t adapted_len;
    rcp_bytes_t wrapped;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));
    original_type_bits = (uint8_t)(acf_frame[0] & 0xFEu);
    original_len = (uint16_t)(((uint16_t)(acf_frame[0] & 0x01u) << 8) | (uint16_t)acf_frame[1]);

    wrapped = rcp_e2e_wrap(0x05u, 0x00u, false, 1, 1, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    adapted_type_bits = (uint8_t)(wrapped.data[0] & 0xFEu);
    adapted_len = (uint16_t)(((uint16_t)(wrapped.data[0] & 0x01u) << 8) | (uint16_t)wrapped.data[1]);

    /* +1 quadlet (RCP_E2E_CRC_LEN is exactly one quadlet, 4 octets) --
     * NOT +RCP_E2E_CRC_LEN, since the field is already in quadlets, not
     * octets (c-RCP-02). acf_msg_type (the other 7 bits of octet 0) must
     * be untouched. */
    TEST_ASSERT_EQUAL_UINT16(original_len + 1u, adapted_len);
    TEST_ASSERT_EQUAL_HEX8(original_type_bits, adapted_type_bits);

    /* The caller's original frame must be untouched -- wrap() adapts a
     * copy, never the input. */
    TEST_ASSERT_EQUAL_UINT16(
        original_len, (uint16_t)(((uint16_t)(acf_frame[0] & 0x01u) << 8) | (uint16_t)acf_frame[1]));

    rcp_bytes_free(&wrapped);
}

static void test_wrap_unwrap_round_trip_ok(void)
{
    /* Must be quadlet-aligned -- see test_wrap_appends_crc_len_bytes()'s
     * own comment. */
    uint8_t acf_frame[12];
    rcp_bytes_t wrapped;
    rcp_bytes_t body = {0};
    rcp_e2e_errc_t rc;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    wrapped = rcp_e2e_wrap(0x05u, 0x00u, false, 0xDEADBEEFu, 0xCAFEu, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    rc = rcp_e2e_unwrap(0x05u, 0x00u, false, 0xDEADBEEFu, 0xCAFEu, wrapped.data, wrapped.len, &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK, rc);
    TEST_ASSERT_EQUAL_UINT(sizeof(acf_frame), body.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(acf_frame, body.data, body.len);

    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&body);
}

/* MC/DC closure: rcp_e2e_wrap()'s own "!acf_frame && acf_frame_len > 0"
 * guard only ever evaluates its own second condition when acf_frame is
 * NULL -- every existing NULL-acf_frame test uses a nonzero length (the
 * genuine misuse this guard rejects), so that condition's own
 * independent effect (acf_frame==NULL with length ZERO) was never
 * demonstrated. Discovered while designing this case: a NULL frame
 * with zero length is not actually a supported "no ACF payload"
 * convention -- it falls through this guard (correctly) but is then
 * rejected downstream by adapt_acf_msg_length()'s own shared
 * frame_len<2 guard (0 octets can never hold the 9-bit acf_msg_length
 * field), the same rejection every acf_frame_len<2 call gets. */
static void test_wrap_null_frame_with_zero_length_is_rejected(void)
{
    rcp_bytes_t wrapped;

    wrapped = rcp_e2e_wrap(0x05u, 0x00u, false, 1u, 1u, NULL, 0u);
    TEST_ASSERT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL_UINT(0u, wrapped.len);
}

/* MC/DC closure: adapt_acf_msg_length() (the static helper both
 * rcp_e2e_wrap()/rcp_e2e_unwrap() share) has an "adapted < 0" branch no
 * real caller can reach through wrap() (its own +1 delta can never go
 * negative) or through a genuinely wrap()-produced frame fed back into
 * unwrap() (wrap() already added 1, so unwrap()'s own -1 delta can
 * never underflow a real round trip). The only way to reach it is an
 * adversarial frame that was never actually produced by wrap() -- one
 * whose own acf_msg_length field already reads 0. Both call sites
 * ignore adapt_acf_msg_length()'s own return value (cast to void), so
 * the only observable effect is whether the field gets rewritten --
 * confirmed here that it does not, leaving the already-invalid field
 * exactly as it arrived rather than underflowing it to 0x1FF. */
static void test_unwrap_leaves_already_zero_acf_msg_length_unadapted(void)
{
    /* real_len=4 (bytes 0-3, acf_msg_length pre-encoded to 0, pad=0),
     * followed by an arbitrary 4-octet CRC32 trailer that deliberately
     * does not match -- this test's own concern is the header field,
     * not the CRC verdict. */
    uint8_t        wrapped[8] = {0x00u, 0x00u, 0x00u, 0x00u, 0xDEu, 0xADu, 0xBEu, 0xEFu};
    rcp_bytes_t    body       = {0};
    rcp_e2e_errc_t rc;

    rc = rcp_e2e_unwrap(0x05u, 0x00u, false, 1u, 1u, wrapped, sizeof(wrapped), &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH, rc);
    TEST_ASSERT_NOT_NULL(body.data);
    TEST_ASSERT_EQUAL_UINT8(0x00u, body.data[0]); /* still 0 -- not underflowed */
    TEST_ASSERT_EQUAL_UINT8(0x00u, body.data[1]);

    rcp_bytes_free(&body);
}

static void *e2e_always_fails_malloc(size_t size)
{
    (void)size;
    return NULL;
}

/* MC/DC closure: rcp_e2e_unwrap()'s own "!body_copy.data && body_len > 0"
 * guard was never exercised at all -- rcp_malloc() failing is normally
 * unreachable in these tests. Fault-injected via the same
 * rcp_alloc_set_hooks() idiom test_fragment.c's own alloc-failure test
 * uses. When body_len>0 but the allocation fails, the function must
 * still report the real CRC verdict (the one piece of information it
 * CAN compute without the copy) rather than silently losing it. */
static void test_unwrap_malloc_failure_still_reports_crc_verdict(void)
{
    uint8_t           acf_frame[12];
    rcp_bytes_t       wrapped;
    rcp_bytes_t       body = {0};
    rcp_e2e_errc_t    rc;
    rcp_alloc_hooks_t hooks = {0};

    make_test_acf_frame(acf_frame, sizeof(acf_frame));
    wrapped = rcp_e2e_wrap(0x05u, 0x00u, false, 0xDEADBEEFu, 0xCAFEu, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    hooks.malloc_fn = e2e_always_fails_malloc;
    rcp_alloc_set_hooks(&hooks);

    rc = rcp_e2e_unwrap(0x05u, 0x00u, false, 0xDEADBEEFu, 0xCAFEu, wrapped.data, wrapped.len, &body);

    rcp_alloc_reset_hooks();

    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK, rc); /* CRC matched -- reported despite the failed alloc */
    TEST_ASSERT_NULL(body.data);           /* no copy could be produced */
    TEST_ASSERT_EQUAL_UINT(0u, body.len);

    rcp_bytes_free(&wrapped);
}

/* MC/DC closure: "!body_copy.data && body_len > 0"'s own second
 * condition (body_len > 0) is only ever EVALUATED when the first is
 * true (body_copy.data is NULL) -- the previous malloc-failure test
 * demonstrates body_len>0 in that branch, but needs a companion where
 * body_len==0 too (the malloc block is skipped entirely, so
 * body_copy.data stays NULL "naturally," with no allocation ever
 * attempted): a wrapped frame that is nothing but the CRC32 trailer
 * (no header-and-payload region, no pad octets) is a legitimate
 * "empty ACF frame" round trip, correctly reported as an empty
 * (NULL, 0) body rather than falling into the malloc-failure path's
 * own early return at all. */
static void test_unwrap_crc_only_frame_yields_empty_body(void)
{
    uint8_t        wrapped[4] = {0x00u, 0x00u, 0x00u, 0x00u}; /* pad_octets bits (frame[2]
                                                                   bits 7:6) are 0 */
    rcp_bytes_t    body       = {0};
    rcp_e2e_errc_t rc;

    rc = rcp_e2e_unwrap(0x05u, 0x00u, false, 1u, 1u, wrapped, sizeof(wrapped), &body);
    /* The all-zero 4-octet CRC32 trailer above doesn't happen to match
     * the real CRC over zero real octets -- this test's own concern is
     * body_len==0's own independent effect on the guard, not the CRC
     * verdict, so a mismatch is fine; the body is populated (as empty)
     * either way (see the malloc-failure test above for why). */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH, rc);
    TEST_ASSERT_NULL(body.data);
    TEST_ASSERT_EQUAL_UINT(0u, body.len);
}

static void test_unwrap_short_frame(void)
{
    uint8_t buf[3] = {0};
    rcp_bytes_t body = {0};

    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_SHORT_FRAME,
                           rcp_e2e_unwrap(0x05u, 0x00u, false, 0, 0, buf, sizeof(buf), &body));
    TEST_ASSERT_NULL(body.data);
}

static void test_unwrap_crc_mismatch_on_corruption(void)
{
    uint8_t acf_frame[8];
    rcp_bytes_t wrapped;
    rcp_bytes_t body = {0};
    rcp_e2e_errc_t rc;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    wrapped = rcp_e2e_wrap(0x05u, 0x00u, false, 5, 5, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    wrapped.data[3] ^= 0xFFu; /* corrupt the payload, not the header/trailer */

    rc = rcp_e2e_unwrap(0x05u, 0x00u, false, 5, 5, wrapped.data, wrapped.len, &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH, rc);

    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&body);
}

static void test_unwrap_crc_mismatch_on_wrong_stream_id(void)
{
    uint8_t acf_frame[8];
    rcp_bytes_t wrapped;
    rcp_bytes_t body = {0};

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    wrapped = rcp_e2e_wrap(0x05u, 0x00u, false, 1, 1, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    /* Same bytes, but unwrapped against the wrong stream_id -- the
     * coverage span includes stream_id, so this must fail too. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                           rcp_e2e_unwrap(0x05u, 0x00u, false, 2, 1, wrapped.data, wrapped.len, &body));

    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&body);
}

/* ── fragmentation/CRC interaction rule ───────────────────────────────────── */

static void test_fragment_carries_crc_only_when_last(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_fragment_carries_crc(true));
    TEST_ASSERT_FALSE(rcp_e2e_fragment_carries_crc(false));
}

/* ── safety-tagged request classification ─────────────────────────────────── */

static void test_is_safety_request(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_is_safety_request(0x8Fu));  /* compound, safety */
    TEST_ASSERT_TRUE(rcp_e2e_is_safety_request(0x8Bu));  /* compound-wait, safety */
    TEST_ASSERT_TRUE(rcp_e2e_is_safety_request(0x8Eu));  /* triggered, safety */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x0Fu)); /* compound */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x0Bu)); /* compound-wait */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x0Eu)); /* triggered */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x00u)); /* standard */
}

static void test_request_may_execute_safety_gated_on_safe_state(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x8Fu, true));
    TEST_ASSERT_FALSE(rcp_e2e_request_may_execute(0x8Fu, false));
}

static void test_request_may_execute_non_safety_always_permitted(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x0Fu, true));
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x0Fu, false));
}

/* ── watchdog-purge-vs-safety-survive ─────────────────────────────────────── */

static void test_watchdog_purge_should_keep_only_safety_tagged(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_watchdog_purge_should_keep(0x8Fu));
    TEST_ASSERT_TRUE(rcp_e2e_watchdog_purge_should_keep(0x8Bu));
    TEST_ASSERT_TRUE(rcp_e2e_watchdog_purge_should_keep(0x8Eu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x0Fu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x0Bu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x0Eu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x00u));
}

static void test_watchdog_purge_classify_matches_per_entry_rule(void)
{
    const uint8_t types[6] = {0x00u, 0x8Fu, 0x0Bu, 0x8Bu, 0x0Eu, 0x8Eu};
    bool keep[6] = {false};

    rcp_e2e_watchdog_purge_classify(types, 6, keep);

    TEST_ASSERT_FALSE(keep[0]);
    TEST_ASSERT_TRUE(keep[1]);
    TEST_ASSERT_FALSE(keep[2]);
    TEST_ASSERT_TRUE(keep[3]);
    TEST_ASSERT_FALSE(keep[4]);
    TEST_ASSERT_TRUE(keep[5]);
}

static void test_watchdog_purge_classify_zero_count_is_noop(void)
{
    rcp_e2e_watchdog_purge_classify(NULL, 0, NULL);
    /* No crash, nothing to assert -- this is the "may be NULL" contract. */
}

/* ── configured safe state ─────────────────────────────────────────────────── */

static void test_measure_valid(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_measure_valid((uint8_t)RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE));
    TEST_ASSERT_TRUE(rcp_e2e_measure_valid((uint8_t)RCP_E2E_MEASURE_SEQUENCER));
    TEST_ASSERT_FALSE(rcp_e2e_measure_valid(2));
    TEST_ASSERT_FALSE(rcp_e2e_measure_valid(0xFFu));
}

static void test_endpoint_in_safe_state_force_high_impedance_always_true(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE, NULL, 0, 0));
}

static void test_endpoint_in_safe_state_sequencer_matches_target_state(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    TEST_ASSERT_EQUAL_UINT16(4, table.count);

    /* Powers on to RCP_SEQUENCER_POWER_ON_STATE (1), not yet "5". */
    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 2, 5));

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 2, 5));

    TEST_ASSERT_TRUE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 2, 5));

    rcp_sequencer_table_free(&table);
}

static void test_endpoint_in_safe_state_fails_closed_on_bad_measure(void)
{
    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(0xFFu, NULL, 0, 0));
}

static void test_endpoint_in_safe_state_fails_closed_on_null_table(void)
{
    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, NULL, 0, 1));
}

static void test_endpoint_in_safe_state_fails_closed_on_invalid_index(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);

    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 99, 1));

    rcp_sequencer_table_free(&table);
}

/* REQ-SEQ-012: a manually-disabled (state==0) sequencer conveys no
 * application-state information -- it never satisfies a safe-state check,
 * even when safe_sequencer_state is itself (mis)configured to 0, which a
 * naive current==safe_sequencer_state comparison would otherwise treat as
 * a match. */
static void test_endpoint_in_safe_state_fails_closed_when_sequencer_disabled(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 1, 0));
    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 1, 0));

    /* Re-enabling to the same value the safe-state check targets
     * legitimately passes -- confirms the fix is scoped to state==0
     * specifically, not a blanket rejection of target 0. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 1, 3));
    TEST_ASSERT_TRUE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 1, 3));

    rcp_sequencer_table_free(&table);
}

/* ── rx_enforce_e2e: drop vs. latch ────────────────────────────────────────── */

static void test_crc_error_action_maps_rx_enforce_e2e(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_E2E_CRC_ACTION_DROP_REQUEST, rcp_e2e_crc_error_action(false));
    TEST_ASSERT_EQUAL_INT(RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT, rcp_e2e_crc_error_action(true));
}

//cfusa:test REQ-E2E-043
//cfusa:test REQ-E2E-044
static void test_stream_fault_drop_request_never_latches(void)
{
    rcp_e2e_stream_fault_t f;
    rcp_e2e_stream_fault_init(&f);

    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, false));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));

    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, false));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));
}

//cfusa:test REQ-E2E-043
//cfusa:test REQ-E2E-044
static void test_stream_fault_latches_and_stays_latched_until_reset(void)
{
    rcp_e2e_stream_fault_t f;
    rcp_e2e_stream_fault_init(&f);
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));

    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_is_faulted(&f));

    /* Stays latched even across a later drop-mode call (state doesn't
     * un-latch just because a caller's config later says drop). */
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, false));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_is_faulted(&f));

    rcp_e2e_stream_fault_reset(&f);
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));
}

/* ── per-stream fault tracker (issue #201, REQ-E2E-021) ─────────────────────── */

//cfusa:test REQ-E2E-021
static void test_stream_fault_tracker_never_seen_stream_is_not_faulted(void)
{
    rcp_e2e_stream_fault_tracker_t t;
    rcp_e2e_stream_fault_tracker_init(&t);

    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 0x1122334455667788ULL));
}

//cfusa:test REQ-E2E-021
static void test_stream_fault_tracker_registers_and_isolates_streams(void)
{
    rcp_e2e_stream_fault_tracker_t t;
    rcp_e2e_stream_fault_tracker_init(&t);

    /* Stream A latches (rx_enforce_e2e=true); stream B never does. */
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_on_crc_error(&t, 1u, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 1u));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 2u));

    /* A drop-mode CRC error on B never latches it, regardless of A's
     * own already-latched state. */
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_on_crc_error(&t, 2u, false));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 2u));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 1u));
}

//cfusa:test REQ-E2E-021
static void test_stream_fault_tracker_reset_clears_only_that_stream(void)
{
    rcp_e2e_stream_fault_tracker_t t;
    rcp_e2e_stream_fault_tracker_init(&t);

    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_on_crc_error(&t, 1u, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_on_crc_error(&t, 2u, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 1u));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 2u));

    rcp_e2e_stream_fault_tracker_reset(&t, 1u);
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 1u));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 2u));

    /* A no-op, not an error, for a never-seen stream_id. */
    rcp_e2e_stream_fault_tracker_reset(&t, 999u);
}

//cfusa:test REQ-E2E-021
static void test_stream_fault_tracker_capacity_exhaustion_is_honestly_reported(void)
{
    rcp_e2e_stream_fault_tracker_t t;
    size_t                          i;

    rcp_e2e_stream_fault_tracker_init(&t);

    /* Fill every slot with a distinct, drop-mode (non-latching) stream. */
    for (i = 0; i < RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS; i++) {
        TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_on_crc_error(&t, (uint64_t)i, false));
    }

    /* One more, brand-new stream_id: capacity exhausted, honestly false. */
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_tracker_on_crc_error(
        &t, (uint64_t)RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS, true));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_tracker_is_faulted(
        &t, (uint64_t)RCP_E2E_STREAM_FAULT_TRACKER_MAX_STREAMS));

    /* An ALREADY-tracked stream (not a new one) still works fine even
     * at full capacity -- only registering a NEW stream_id can fail. */
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_on_crc_error(&t, 0u, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&t, 0u));
}

/* ── aggregate rx_stream_status (issue #201/#336, REQ-E2E-046) ──────────────── */

//cfusa:test REQ-E2E-046
static void test_stream_status_init_is_not_blocked(void)
{
    rcp_e2e_stream_status_t s;
    rcp_e2e_stream_status_init(&s);

    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));
}

//cfusa:test REQ-E2E-046
static void test_stream_status_crc_cause_blocks_and_resets_independently(void)
{
    rcp_e2e_stream_status_t s;
    rcp_e2e_stream_status_init(&s);

    TEST_ASSERT_TRUE(rcp_e2e_stream_status_note_crc_error(&s, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_reset_crc(&s);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));
}

//cfusa:test REQ-E2E-046
static void test_stream_status_crc_drop_mode_never_blocks(void)
{
    rcp_e2e_stream_status_t s;
    rcp_e2e_stream_status_init(&s);

    TEST_ASSERT_TRUE(rcp_e2e_stream_status_note_crc_error(&s, false));
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));
}

//cfusa:test REQ-E2E-046
static void test_stream_status_seq_cause_blocks_only_on_enter_safe_state(void)
{
    rcp_e2e_stream_status_t s;
    rcp_e2e_seq_result_t     no_block   = {true, true, false};
    rcp_e2e_seq_result_t     do_block   = {false, true, true};

    rcp_e2e_stream_status_init(&s);
    rcp_e2e_stream_status_note_seq(&s, no_block);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_note_seq(&s, do_block);
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_reset_seq(&s);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));
}

//cfusa:test REQ-E2E-046
static void test_stream_status_wd_cause_blocks_only_on_enter_safe_state(void)
{
    rcp_e2e_stream_status_t s;
    rcp_e2e_wd_result_t      no_block = {true, false, true};
    rcp_e2e_wd_result_t      do_block = {true, true, false};

    rcp_e2e_stream_status_init(&s);
    rcp_e2e_stream_status_note_wd(&s, no_block);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_note_wd(&s, do_block);
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_reset_wd(&s);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));
}

//cfusa:test REQ-E2E-046
static void test_stream_status_overflow_cause_blocks_only_when_told(void)
{
    rcp_e2e_stream_status_t s;
    rcp_e2e_stream_status_init(&s);

    rcp_e2e_stream_status_note_overflow(&s, false);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_note_overflow(&s, true);
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_reset_overflow(&s);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s));
}

//cfusa:test REQ-E2E-046
static void test_stream_status_causes_are_independent_of_one_another(void)
{
    /* Resetting one cause must not clear a different, still-latched
     * cause -- each of the four has its own distinct TC18 release
     * condition, so each latch must be independently resettable. */
    rcp_e2e_stream_status_t s;
    rcp_e2e_seq_result_t     seq_block = {false, true, true};

    rcp_e2e_stream_status_init(&s);
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_note_crc_error(&s, true));
    rcp_e2e_stream_status_note_seq(&s, seq_block);
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_rx_blocked(&s));

    rcp_e2e_stream_status_reset_crc(&s);
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_rx_blocked(&s)); /* seq still latched */

    rcp_e2e_stream_status_reset_seq(&s);
    TEST_ASSERT_FALSE(rcp_e2e_stream_status_rx_blocked(&s)); /* now both clear */
}

/* ── per-stream watchdog ───────────────────────────────────────────────────── */

static void test_wd_evaluate_disabled_never_overflows(void)
{
    rcp_e2e_wd_result_t r = rcp_e2e_wd_evaluate(false, 10, true, true, 1000000);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);
}

static void test_wd_evaluate_below_timeout_no_overflow(void)
{
    rcp_e2e_wd_result_t r = rcp_e2e_wd_evaluate(true, 100, true, true, 99);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);
}

static void test_wd_evaluate_at_and_beyond_timeout_overflows(void)
{
    rcp_e2e_wd_result_t r1 = rcp_e2e_wd_evaluate(true, 100, true, true, 100);
    rcp_e2e_wd_result_t r2 = rcp_e2e_wd_evaluate(true, 100, true, true, 500);
    TEST_ASSERT_TRUE(r1.overflowed);
    TEST_ASSERT_TRUE(r2.overflowed);
}

static void test_wd_evaluate_enter_safe_state_and_notify_are_independent(void)
{
    rcp_e2e_wd_result_t both = rcp_e2e_wd_evaluate(true, 10, true, true, 50);
    rcp_e2e_wd_result_t only_safestate = rcp_e2e_wd_evaluate(true, 10, true, false, 50);
    rcp_e2e_wd_result_t only_notify = rcp_e2e_wd_evaluate(true, 10, false, true, 50);
    rcp_e2e_wd_result_t neither = rcp_e2e_wd_evaluate(true, 10, false, false, 50);

    TEST_ASSERT_TRUE(both.enter_safe_state);
    TEST_ASSERT_TRUE(both.notify);

    TEST_ASSERT_TRUE(only_safestate.enter_safe_state);
    TEST_ASSERT_FALSE(only_safestate.notify);

    TEST_ASSERT_FALSE(only_notify.enter_safe_state);
    TEST_ASSERT_TRUE(only_notify.notify);

    TEST_ASSERT_FALSE(neither.enter_safe_state);
    TEST_ASSERT_FALSE(neither.notify);
}

/* ── The primary safety mechanism, end to end ─────────────────────────────── */

static void test_watchdog_overflow_drives_the_full_purge_and_survive_flow(void)
{
    /* This is the scenario the roadmap calls out explicitly: on watchdog
     * overflow, normal requests are purged while the safety variants
     * survive and become what actually drives the endpoint through its
     * configured safe state. */
    const uint8_t queued[4] = {0x0Fu /* compound */, 0x8Fu /* compound, safety */,
                                0x00u /* standard */,  0x8Eu /* triggered, safety */};
    bool keep[4] = {false};
    rcp_e2e_wd_result_t wd;
    rcp_sequencer_table_t table = rcp_sequencer_table_new(1);

    wd = rcp_e2e_wd_evaluate(true, 50, true, false, 200);
    TEST_ASSERT_TRUE(wd.overflowed);
    TEST_ASSERT_TRUE(wd.enter_safe_state);

    rcp_e2e_watchdog_purge_classify(queued, 4, keep);
    TEST_ASSERT_FALSE(keep[0]);
    TEST_ASSERT_TRUE(keep[1]);
    TEST_ASSERT_FALSE(keep[2]);
    TEST_ASSERT_TRUE(keep[3]);

    /* Before the endpoint reaches its configured safe state, the
     * survivors still may not execute... */
    TEST_ASSERT_FALSE(rcp_e2e_request_may_execute(0x8Fu, false));
    TEST_ASSERT_FALSE(rcp_e2e_request_may_execute(0x8Eu, false));

    /* ...once the sequencer-based safe state is actually reached, they
     * may. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 9));
    TEST_ASSERT_TRUE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 0, 9));
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x8Fu, true));
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x8Eu, true));

    rcp_sequencer_table_free(&table);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_wire_error_crc_mismatch_maps_to_poci_failure);
    RUN_TEST(test_wire_error_ok_and_short_frame_map_to_none);

    RUN_TEST(test_crc32_known_answer_vector);
    RUN_TEST(test_crc32_empty_input);
    RUN_TEST(test_crc32_differs_for_different_data);

    RUN_TEST(test_compute_crc_matches_manual_concatenation);
    RUN_TEST(test_compute_crc_zero_stream_and_timestamp_ntscf_standin);
    RUN_TEST(test_compute_crc_timestamp_is_4_octets_not_8);
    RUN_TEST(test_compute_crc_avtp_subtype_changes_result);
    RUN_TEST(test_compute_crc_header_octet1_changes_result);
    RUN_TEST(test_compute_crc_tu_bit_changes_result);

    RUN_TEST(test_length_with_crc_adds_trailer_size);
    RUN_TEST(test_length_with_crc_saturates);

    RUN_TEST(test_wrap_framed_forces_tu_false_under_ntscf);
    RUN_TEST(test_wrap_appends_crc_len_bytes);
    RUN_TEST(test_wrap_null_frame_nonzero_len_fails_safe);
    RUN_TEST(test_wrap_too_short_for_length_field_fails_safe);
    RUN_TEST(test_wrap_adapts_acf_msg_length_by_one_quadlet);
    RUN_TEST(test_wrap_unwrap_round_trip_ok);
    RUN_TEST(test_wrap_null_frame_with_zero_length_is_rejected);
    RUN_TEST(test_unwrap_leaves_already_zero_acf_msg_length_unadapted);
    RUN_TEST(test_unwrap_malloc_failure_still_reports_crc_verdict);
    RUN_TEST(test_unwrap_crc_only_frame_yields_empty_body);
    RUN_TEST(test_unwrap_short_frame);
    RUN_TEST(test_unwrap_crc_mismatch_on_corruption);
    RUN_TEST(test_unwrap_crc_mismatch_on_wrong_stream_id);

    RUN_TEST(test_fragment_carries_crc_only_when_last);

    RUN_TEST(test_is_safety_request);
    RUN_TEST(test_request_may_execute_safety_gated_on_safe_state);
    RUN_TEST(test_request_may_execute_non_safety_always_permitted);

    RUN_TEST(test_watchdog_purge_should_keep_only_safety_tagged);
    RUN_TEST(test_watchdog_purge_classify_matches_per_entry_rule);
    RUN_TEST(test_watchdog_purge_classify_zero_count_is_noop);

    RUN_TEST(test_measure_valid);
    RUN_TEST(test_endpoint_in_safe_state_force_high_impedance_always_true);
    RUN_TEST(test_endpoint_in_safe_state_sequencer_matches_target_state);
    RUN_TEST(test_endpoint_in_safe_state_fails_closed_on_bad_measure);
    RUN_TEST(test_endpoint_in_safe_state_fails_closed_on_null_table);
    RUN_TEST(test_endpoint_in_safe_state_fails_closed_on_invalid_index);
    RUN_TEST(test_endpoint_in_safe_state_fails_closed_when_sequencer_disabled);

    RUN_TEST(test_crc_error_action_maps_rx_enforce_e2e);
    RUN_TEST(test_stream_fault_drop_request_never_latches);
    RUN_TEST(test_stream_fault_latches_and_stays_latched_until_reset);
    RUN_TEST(test_stream_fault_tracker_never_seen_stream_is_not_faulted);
    RUN_TEST(test_stream_fault_tracker_registers_and_isolates_streams);
    RUN_TEST(test_stream_fault_tracker_reset_clears_only_that_stream);
    RUN_TEST(test_stream_fault_tracker_capacity_exhaustion_is_honestly_reported);

    RUN_TEST(test_stream_status_init_is_not_blocked);
    RUN_TEST(test_stream_status_crc_cause_blocks_and_resets_independently);
    RUN_TEST(test_stream_status_crc_drop_mode_never_blocks);
    RUN_TEST(test_stream_status_seq_cause_blocks_only_on_enter_safe_state);
    RUN_TEST(test_stream_status_wd_cause_blocks_only_on_enter_safe_state);
    RUN_TEST(test_stream_status_overflow_cause_blocks_only_when_told);
    RUN_TEST(test_stream_status_causes_are_independent_of_one_another);

    RUN_TEST(test_wd_evaluate_disabled_never_overflows);
    RUN_TEST(test_wd_evaluate_below_timeout_no_overflow);
    RUN_TEST(test_wd_evaluate_at_and_beyond_timeout_overflows);
    RUN_TEST(test_wd_evaluate_enter_safe_state_and_notify_are_independent);

    RUN_TEST(test_watchdog_overflow_drives_the_full_purge_and_survive_flow);

    return UNITY_END();
}
