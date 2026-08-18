/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-AVTP-001
//cfusa:test REQ-AVTP-002
//cfusa:test REQ-AVTP-003
//cfusa:test REQ-AVTP-004
//cfusa:test REQ-AVTP-005
//cfusa:test REQ-AVTP-006
//cfusa:test REQ-AVTP-007
//cfusa:test REQ-AVTP-008
//cfusa:test REQ-AVTP-009
//cfusa:test REQ-AVTP-010
//cfusa:test REQ-AVTP-011
//cfusa:test REQ-AVTP-012
//cfusa:test REQ-AVTP-013
//cfusa:test REQ-AVTP-014
//cfusa:test REQ-AVTP-015
//cfusa:test REQ-AVTP-016
//cfusa:test REQ-AVTP-017
//cfusa:test REQ-AVTP-018
//cfusa:test REQ-AVTP-019
//cfusa:test REQ-AVTP-020
//cfusa:test REQ-AVTP-021
//cfusa:test REQ-AVTP-022
//cfusa:test REQ-AVTP-023
//cfusa:test REQ-AVTP-024
//cfusa:test REQ-AVTP-025
//cfusa:test REQ-AVTP-026
//cfusa:test REQ-AVTP-027
//cfusa:test REQ-AVTP-028
//cfusa:test REQ-AVTP-031
//cfusa:test REQ-TIMED-012
#include "unity.h"

#include <rcp/avtp.h>
#include <rcp/rcp.h>
#include <rcp/request.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMacA[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t kMacB[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

/* ── Subtype constants ─────────────────────────────────────────────────────── */

static void test_subtype_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x82, RCP_AVTP_SUBTYPE_NTSCF);
    TEST_ASSERT_EQUAL_HEX8(0x05, RCP_AVTP_SUBTYPE_TSCF);
}

/* ── stream_id ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-AVTP-025
static void test_stream_id_make_preserves_mac_and_unique_id(void)
{
    rcp_stream_id_t id = rcp_stream_id_make(kMacA, 0xBEEF);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, id.mac, 6);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, id.unique_id);
}

//cfusa:test REQ-AVTP-010
static void test_stream_id_u64_roundtrip(void)
{
    rcp_stream_id_t id = rcp_stream_id_make(kMacA, 0x1234);
    uint64_t packed = rcp_stream_id_to_u64(id);
    rcp_stream_id_t back = rcp_stream_id_from_u64(packed);

    TEST_ASSERT_TRUE(rcp_stream_id_equal(id, back));
}

static void test_stream_id_u64_matches_expected_layout(void)
{
    /* MAC in the high 48 bits, unique_id in the low 16 bits. */
    rcp_stream_id_t id = rcp_stream_id_make(kMacA, 0x0102);
    uint64_t packed = rcp_stream_id_to_u64(id);
    uint64_t expected = ((uint64_t)0x02 << 40) | ((uint64_t)0x11 << 32) |
                         ((uint64_t)0x22 << 24) | ((uint64_t)0x33 << 16) |
                         ((uint64_t)0x44 << 8)  |  (uint64_t)0x55;
    expected = (expected << 16) | 0x0102u;

    TEST_ASSERT_EQUAL_UINT64(expected, packed);
}

static void test_stream_id_equal_requires_mac_and_unique_id_match(void)
{
    rcp_stream_id_t a = rcp_stream_id_make(kMacA, 1);
    rcp_stream_id_t b = rcp_stream_id_make(kMacA, 2);
    rcp_stream_id_t c = rcp_stream_id_make(kMacB, 1);
    rcp_stream_id_t d = rcp_stream_id_make(kMacA, 1);

    TEST_ASSERT_FALSE(rcp_stream_id_equal(a, b));
    TEST_ASSERT_FALSE(rcp_stream_id_equal(a, c));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(a, d));
}

/* ── byte_bus_id / addressing ──────────────────────────────────────────────── */

static void test_avtp_addr_equal_requires_byte_bus_id_match(void)
{
    rcp_avtp_addr_t a1, a2, a3;

    a1.stream_id = rcp_stream_id_make(kMacA, 7);
    a1.byte_bus_id = 3;

    a2.stream_id = rcp_stream_id_make(kMacA, 7);
    a2.byte_bus_id = 4; /* same stream_id, different endpoint */

    a3.stream_id = rcp_stream_id_make(kMacA, 7);
    a3.byte_bus_id = 3;

    TEST_ASSERT_FALSE(rcp_avtp_addr_equal(a1, a2));
    TEST_ASSERT_TRUE(rcp_avtp_addr_equal(a1, a3));
}

/* ── NTSCF round-trip ──────────────────────────────────────────────────────── */

static void test_ntscf_roundtrip_no_payload(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.version = 0;
    hdr.sequence_num = 0x42;
    hdr.stream_id = rcp_stream_id_make(kMacA, 0x0001);

    frame = rcp_avtp_encode_ntscf(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_NTSCF_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_NTSCF, frame.data[0]);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(hdr.sv, out.sv);
    TEST_ASSERT_EQUAL_UINT8(hdr.version, out.version);
    TEST_ASSERT_EQUAL_UINT8(hdr.sequence_num, out.sequence_num);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));
    TEST_ASSERT_EQUAL_UINT(0, payload_len);
    TEST_ASSERT_EQUAL_UINT16(0, out.ntscf_data_length);

    rcp_bytes_free(&frame);
}

static void test_ntscf_roundtrip_with_payload(void)
{
    uint8_t body[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.sequence_num = 7;
    hdr.stream_id = rcp_stream_id_make(kMacB, 0x9988);

    frame = rcp_avtp_encode_ntscf(&hdr, body, sizeof(body));
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_NTSCF_HEADER_LEN + sizeof(body), frame.len);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(body), out.ntscf_data_length);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));

    rcp_bytes_free(&frame);
}

static void test_ntscf_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_AVTP_NTSCF_HEADER_LEN - 1] = {0};
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_ntscf(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_ntscf_decode_rejects_wrong_subtype(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, NULL, 0);
    frame.data[0] = RCP_AVTP_SUBTYPE_TSCF;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_BAD_SUBTYPE,
                       rcp_avtp_decode_ntscf(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-AVTP-009
static void test_ntscf_decode_rejects_declared_length_past_buffer(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3};

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, body, sizeof(body));

    /* Claim more payload than the buffer actually holds by decoding with a
     * truncated length, while the data-length field still says 3. */
    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_ntscf(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_ntscf_encode_rejects_oversized_payload(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, NULL, RCP_AVTP_NTSCF_MAX_PAYLOAD + 1);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(0, frame.len);
}

/* ── TSCF round-trip ───────────────────────────────────────────────────────── */

static void test_tscf_roundtrip_no_payload(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.version = 3; /* REQ-AVTP-004: nonzero test value -- TC18 fixes this
                       * field at 0 for this spec revision, but the wire
                       * codec itself must round-trip whatever value it's
                       * given; a version-always-0 bug would otherwise pass
                       * unnoticed since every other test in this file also
                       * leaves it at its {0}-init default. */
    hdr.tv = 1;
    hdr.tu = 0;
    hdr.mr = 1;
    hdr.sequence_num = 0x99;
    hdr.stream_id = rcp_stream_id_make(kMacA, 0x0002);
    hdr.avtp_timestamp = 0x12345678u;

    frame = rcp_avtp_encode_tscf(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_TSCF_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_TSCF, frame.data[0]);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_tscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(hdr.sv, out.sv);
    TEST_ASSERT_EQUAL_UINT8(hdr.version, out.version);
    TEST_ASSERT_EQUAL_UINT8(hdr.mr, out.mr);
    TEST_ASSERT_EQUAL_UINT8(hdr.tv, out.tv);
    TEST_ASSERT_EQUAL_UINT8(hdr.tu, out.tu);
    TEST_ASSERT_EQUAL_UINT8(hdr.sequence_num, out.sequence_num);
    TEST_ASSERT_EQUAL_UINT32(hdr.avtp_timestamp, out.avtp_timestamp);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_tscf_roundtrip_with_payload(void)
{
    uint8_t body[] = {0xAA, 0xBB, 0xCC};
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.tu = 1;
    hdr.stream_id = rcp_stream_id_make(kMacB, 0x4321);
    hdr.avtp_timestamp = 42;

    frame = rcp_avtp_encode_tscf(&hdr, body, sizeof(body));
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_TSCF_HEADER_LEN + sizeof(body), frame.len);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_tscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(body), out.stream_data_length);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));
    TEST_ASSERT_EQUAL_UINT8(1, out.tu);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));

    rcp_bytes_free(&frame);
}

static void test_tscf_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_AVTP_TSCF_HEADER_LEN - 1] = {0};
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_tscf(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_tscf_decode_rejects_wrong_subtype(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_tscf(&hdr, NULL, 0);
    frame.data[0] = RCP_AVTP_SUBTYPE_NTSCF;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_BAD_SUBTYPE,
                       rcp_avtp_decode_tscf(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-AVTP-024
static void test_tscf_decode_rejects_declared_length_past_buffer(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3, 4};

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_tscf(&hdr, body, sizeof(body));

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_tscf(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

/* ── REQ-TIMED-012: TSCF avtp_timestamp -> gPTP-domain reconstruction ───────── */

static void test_extend_timestamp_exact_match_returns_reference_now(void)
{
    /* wire_ts's low 32 bits already equal reference_now's own -- no
     * adjustment needed at all. */
    uint64_t now = 0x0000123456789ABCull;
    TEST_ASSERT_EQUAL_UINT64(now, rcp_avtp_extend_timestamp((uint32_t)now, now));
}

static void test_extend_timestamp_near_future_within_half_period(void)
{
    /* wire_ts is 1000ns ahead of reference_now, well within the +-2.147s
     * (2^31 ns) nearest-window -- no period wraparound needed. */
    uint64_t now      = 0x0000000100000000ull; /* low 32 bits == 0 */
    uint32_t wire_ts  = 1000u;
    TEST_ASSERT_EQUAL_UINT64(now + 1000u, rcp_avtp_extend_timestamp(wire_ts, now));
}

static void test_extend_timestamp_near_past_within_half_period(void)
{
    /* wire_ts is 1000ns behind reference_now's own low bits -- still the
     * closest candidate without crossing into the next period down. */
    uint64_t now      = 0x0000000100001000ull; /* low 32 bits == 0x1000 */
    uint32_t wire_ts  = 0x1000u - 500u;
    TEST_ASSERT_EQUAL_UINT64(now - 500u, rcp_avtp_extend_timestamp(wire_ts, now));
}

static void test_extend_timestamp_wraps_forward_when_wire_ts_just_past_boundary(void)
{
    /* reference_now sits just below a 2^32 boundary; wire_ts's own low
     * bits are numerically small (just above 0), which naive zero-
     * extension would misread as ~4.29 seconds in the past. The correct
     * reconstruction recognizes wire_ts is actually ~100ns in the
     * FUTURE, one period up from the naive candidate. */
    uint64_t now     = ((uint64_t)1u << 32) - 100u; /* 100ns before the boundary */
    uint32_t wire_ts = 0u; /* the boundary itself, i.e. now + 100 */
    uint64_t got     = rcp_avtp_extend_timestamp(wire_ts, now);
    TEST_ASSERT_EQUAL_UINT64(now + 100u, got);
}

static void test_extend_timestamp_wraps_backward_when_wire_ts_just_before_boundary(void)
{
    /* Symmetric case: reference_now sits just above a 2^32 boundary;
     * wire_ts's own low bits are numerically large (near 2^32-1), which
     * naive zero-extension would misread as ~4.29 seconds in the future.
     * The correct reconstruction is one period DOWN, ~100ns in the past. */
    uint64_t now     = ((uint64_t)1u << 32) + 100u; /* 100ns after the boundary */
    uint32_t wire_ts = 0xFFFFFFFFu; /* the boundary minus 1, i.e. now - 101 */
    uint64_t got     = rcp_avtp_extend_timestamp(wire_ts, now);
    TEST_ASSERT_EQUAL_UINT64(now - 101u, got);
}

static void test_extend_timestamp_exactly_half_period_prefers_no_wrap(void)
{
    /* wire_ts exactly half a period (2^31 ns) ahead of reference_now:
     * the tie-break condition is "> half", not ">=", so exactly half
     * stays with the un-wrapped (forward) candidate -- pinning the
     * boundary's own direction explicitly rather than leaving it
     * implicit. */
    uint64_t now      = 0x0000000200000000ull; /* low 32 bits == 0 */
    uint32_t wire_ts  = (uint32_t)(((uint64_t)1u << 32) / 2u); /* 2^31 */
    uint64_t got      = rcp_avtp_extend_timestamp(wire_ts, now);
    TEST_ASSERT_EQUAL_UINT64(now + ((uint64_t)1u << 31), got);
}

static void test_extend_timestamp_exactly_half_period_backward_prefers_no_wrap(void)
{
    /* Symmetric to the forward case above: wire_ts exactly half a period
     * BEHIND reference_now's own low bits. The tie-break is the same
     * "> half", not ">=", on this branch too -- exactly half stays with
     * the un-wrapped (backward, i.e. candidate < reference_now)
     * candidate rather than wrapping an extra period further back. */
    uint64_t now      = 0x0000000300000000ull | ((uint64_t)1u << 31); /* low 32 bits == 2^31 */
    uint32_t wire_ts  = 0u;
    uint64_t got      = rcp_avtp_extend_timestamp(wire_ts, now);
    TEST_ASSERT_EQUAL_UINT64(now - ((uint64_t)1u << 31), got);
}

static void test_extend_timestamp_result_feeds_rcp_timed_due_correctly(void)
{
    /* End-to-end: a TSCF avtp_timestamp reconstructed at admission time
     * (reference_now == the admitting tick's own gptp_now) is not yet
     * due one tick before it, and becomes due exactly at and after it --
     * the same rcp_timed_due() contract request_timed.h's own
     * presentation_time already gets, now fed a TSCF-derived value
     * instead. */
    uint64_t admit_now   = 0x0000000500000000ull;
    uint32_t wire_ts     = 5000u; /* 5000ns after admit_now, low bits */
    uint64_t due_instant = rcp_avtp_extend_timestamp(wire_ts, admit_now);

    TEST_ASSERT_FALSE(rcp_timed_due(due_instant, admit_now + 4999u));
    TEST_ASSERT_TRUE(rcp_timed_due(due_instant, admit_now + 5000u));
    TEST_ASSERT_TRUE(rcp_timed_due(due_instant, admit_now + 5001u));
}

/* ── Subtype dispatch & TSCF drop rule ─────────────────────────────────────── */

//cfusa:test REQ-AVTP-013
static void test_peek_subtype_reads_first_byte(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    uint8_t subtype = 0;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_peek_subtype(frame.data, frame.len, &subtype));
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_NTSCF, subtype);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-AVTP-026
static void test_peek_subtype_rejects_empty_buffer(void)
{
    uint8_t subtype = 0;
    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME, rcp_avtp_peek_subtype(NULL, 0, &subtype));
}

static void test_should_drop_tscf_without_time_sync(void)
{
    /* RCP_AVTP_TSCF_FALLBACK_DROP: TC18 §11.1's own unconditional wording
     * and this library's original (still default) disposition. */
    TEST_ASSERT_TRUE(rcp_avtp_should_drop_tscf(false, RCP_AVTP_SUBTYPE_TSCF,
                                                RCP_AVTP_TSCF_FALLBACK_DROP));
}

static void test_should_not_drop_tscf_with_time_sync(void)
{
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(true, RCP_AVTP_SUBTYPE_TSCF,
                                                 RCP_AVTP_TSCF_FALLBACK_DROP));
}

static void test_should_not_drop_ntscf_regardless_of_time_sync(void)
{
    /* Neither subtype nor policy nor server_time_sync_supported changes
     * NTSCF's own outcome -- this rule is TSCF-only, whatever policy is
     * in effect. */
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(false, RCP_AVTP_SUBTYPE_NTSCF,
                                                 RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(true, RCP_AVTP_SUBTYPE_NTSCF,
                                                 RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(false, RCP_AVTP_SUBTYPE_NTSCF,
                                                 RCP_AVTP_TSCF_FALLBACK_IGNORE));
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(true, RCP_AVTP_SUBTYPE_NTSCF,
                                                 RCP_AVTP_TSCF_FALLBACK_IGNORE));
}

/* REQ-AVTP-021, TC18 §13.3's own configurable alternative to §11.1's
 * unconditional wording: a caller that opts into
 * RCP_AVTP_TSCF_FALLBACK_IGNORE gets false (not dropped) for exactly the
 * same unsupported-time-sync/TSCF combination that
 * test_should_drop_tscf_without_time_sync() above proves gets dropped
 * under the default RCP_AVTP_TSCF_FALLBACK_DROP policy -- the same
 * inputs, only the new policy parameter differs, isolating this fix's
 * own behavior change from every pre-existing case above. */
//cfusa:test REQ-AVTP-021
static void test_should_not_drop_tscf_without_time_sync_when_policy_is_ignore(void)
{
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(false, RCP_AVTP_SUBTYPE_TSCF,
                                                 RCP_AVTP_TSCF_FALLBACK_IGNORE));
}

/* time_sync_supported == true means this rule's own trigger condition
 * never held in the first place -- RCP_AVTP_TSCF_FALLBACK_IGNORE changes
 * nothing here, matching test_should_not_drop_tscf_with_time_sync()
 * above exactly. */
static void test_ignore_policy_irrelevant_when_time_sync_supported(void)
{
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(true, RCP_AVTP_SUBTYPE_TSCF,
                                                 RCP_AVTP_TSCF_FALLBACK_IGNORE));
}

/* ── §13.3 reserved-bytes-all-zero rule (REQ-AVTP-022) ─────────────────────── */

//cfusa:test REQ-AVTP-031
static void test_tscf_reserved_all_zero_true_for_freshly_decoded_conformant_header(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t             frame;
    const uint8_t           payload[] = {9, 9};
    rcp_avtp_tscf_header_t  decoded;
    const uint8_t          *out_payload;
    size_t                  out_payload_len;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_tscf(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    /* rcp_avtp_encode_tscf() always zero-fills the reserved octets
     * regardless of hdr's own (here zero-initialized, but irrelevant)
     * reserved0/reserved1 -- see rcp_avtp_tscf_header_t's own doc
     * comment -- so a conformant round trip always decodes all-zero. */
    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_decode_tscf(frame.data, frame.len, &decoded,
                                                          &out_payload, &out_payload_len));
    TEST_ASSERT_TRUE(rcp_avtp_tscf_reserved_all_zero(&decoded));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-AVTP-031
static void test_tscf_reserved_all_zero_false_when_reserved0_nonzero(void)
{
    rcp_avtp_tscf_header_t hdr = {0};

    hdr.reserved0 = 1u;
    hdr.reserved1 = 0u;
    TEST_ASSERT_FALSE(rcp_avtp_tscf_reserved_all_zero(&hdr));
}

//cfusa:test REQ-AVTP-031
static void test_tscf_reserved_all_zero_false_when_reserved1_nonzero(void)
{
    rcp_avtp_tscf_header_t hdr = {0};

    hdr.reserved0 = 0u;
    hdr.reserved1 = 1u;
    TEST_ASSERT_FALSE(rcp_avtp_tscf_reserved_all_zero(&hdr));
}

/* Decoding a hand-built wire frame whose own bytes 16-19 are nonzero
 * (simulating a non-conformant or future-revision sender) proves decode
 * actually reads the reserved octets off the wire, not merely that a
 * hand-set struct field round-trips -- see this test's own sibling
 * decode tests elsewhere in this file for the established convention of
 * hand-building raw frame bytes to exercise decode independent of
 * encode. */
//cfusa:test REQ-AVTP-022
static void test_decode_tscf_reads_nonzero_reserved_bytes_off_the_wire(void)
{
    uint8_t                 b[RCP_AVTP_TSCF_HEADER_LEN] = {0};
    rcp_avtp_tscf_header_t  decoded;
    const uint8_t          *out_payload;
    size_t                  out_payload_len;

    b[0] = RCP_AVTP_SUBTYPE_TSCF;
    b[1] = (uint8_t)(1u << 7); /* sv=1 */
    /* bytes 4-11 (stream_id), 12-15 (avtp_timestamp) left zero. */
    b[16] = 0xDEu; b[17] = 0xADu; b[18] = 0xBEu; b[19] = 0xEFu; /* reserved0 */
    /* bytes 20-21 (stream_data_length) left zero -- no payload. */
    b[22] = 0xAAu; b[23] = 0xBBu; /* reserved1 */

    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_decode_tscf(b, sizeof(b), &decoded, &out_payload,
                                                          &out_payload_len));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, decoded.reserved0);
    TEST_ASSERT_EQUAL_HEX16(0xAABBu, decoded.reserved1);
    TEST_ASSERT_FALSE(rcp_avtp_tscf_reserved_all_zero(&decoded));
}

/* ── §13.3 tu=1/tu=0 equivalence (REQ-AVTP-023) ────────────────────────────── */

/* rcp_avtp_decode_tscf() itself still faithfully reports tu (nothing
 * about this fix touches decode's own fidelity) -- this is the "decode
 * captures the wire value" half of REQ-AVTP-023; the "nothing downstream
 * of decode treats tu=1 differently from tu=0" half is proven at the
 * dispatch layer instead (test_mock.c's own REQ-AVTP-023 test), since
 * this library's decode/dispatch split means no single function decides
 * both. */
//cfusa:test REQ-AVTP-023
static void test_decode_tscf_reports_tu_one_and_tu_zero_faithfully(void)
{
    uint8_t                 b[RCP_AVTP_TSCF_HEADER_LEN] = {0};
    rcp_avtp_tscf_header_t  decoded;
    const uint8_t          *out_payload;
    size_t                  out_payload_len;

    b[0] = RCP_AVTP_SUBTYPE_TSCF;
    b[1] = (uint8_t)(1u << 7); /* sv=1 */
    b[3] = 0x1u; /* tu=1 */

    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_decode_tscf(b, sizeof(b), &decoded, &out_payload,
                                                          &out_payload_len));
    TEST_ASSERT_EQUAL(1u, decoded.tu);

    b[3] = 0x0u; /* tu=0 */
    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_decode_tscf(b, sizeof(b), &decoded, &out_payload,
                                                          &out_payload_len));
    TEST_ASSERT_EQUAL(0u, decoded.tu);
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_avtp_strerror_unique_nonempty(void)
{
    const rcp_avtp_errc_t codes[] = {
        RCP_AVTP_OK, RCP_AVTP_ERR_SHORT_FRAME, RCP_AVTP_ERR_BAD_SUBTYPE,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_avtp_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_avtp_strerror(codes[j])) != 0);
        }
    }
}

/* ── Loopback transport ────────────────────────────────────────────────────── */

static void test_loopback_transport_send_recv_fifo_order(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 4);
    rcp_context_t ctx = rcp_context_with_timeout_ms(50);
    uint8_t frame_a[] = {1, 2, 3};
    uint8_t frame_b[] = {4, 5};
    uint8_t buf[16];
    size_t out_len = 0;

    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(t->time_sync_supported);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame_a, sizeof(frame_a)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame_b, sizeof(frame_b)));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame_a), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame_a, buf, sizeof(frame_a));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame_b), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame_b, buf, sizeof(frame_b));

    rcp_avtp_transport_release(t);
}

static void test_loopback_transport_recv_times_out_when_empty(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 2);
    rcp_context_t ctx = rcp_context_with_timeout_ms(20);
    uint8_t buf[8];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(t);
}

//cfusa:test REQ-AVTP-019
static void test_loopback_transport_send_rejects_after_close(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 2);
    uint8_t frame[] = {9};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(t));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(t, frame, sizeof(frame)));

    rcp_avtp_transport_release(t);
}

/* REQ-AVTP-028: recv() on an already-closed, already-empty transport
 * returns RCP_ERR_CLOSED -- test_loopback_transport_close_drains_queued_
 * frame_first() below proves the companion "drains queued frames first"
 * nuance for a transport that had something queued at close() time; this
 * is the simpler immediate-empty-queue case. */
//cfusa:test REQ-AVTP-028
static void test_loopback_transport_recv_rejects_after_close_when_empty(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 2);
    rcp_context_t ctx = rcp_context_with_timeout_ms(20);
    uint8_t buf[8];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(t));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(t);
}

/* REQ-AVTP-028's own "once any already-queued frames are exhausted"
 * clause: close() must not discard frames already queued before it was
 * called -- recv() drains them first and only then starts returning
 * RCP_ERR_CLOSED. test_loopback_transport_recv_rejects_after_close_when_
 * empty() above only proves the immediate-empty-queue case. */
//cfusa:test REQ-AVTP-028
static void test_loopback_transport_close_drains_queued_frame_first(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 4);
    rcp_context_t ctx = rcp_context_with_timeout_ms(20);
    uint8_t frame[] = {7, 8, 9};
    uint8_t buf[16];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(t));

    /* The frame queued before close() is still delivered once. */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, buf, sizeof(frame));

    /* Only once the queue is genuinely exhausted does recv() report closed. */
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(t, frame, sizeof(frame)));

    rcp_avtp_transport_release(t);
}

static void test_loopback_transport_send_rejects_when_full(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 1);
    uint8_t frame[] = {1};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_avtp_transport_send(t, frame, sizeof(frame)));

    rcp_avtp_transport_release(t);
}

/* REQ-AVTP-016: retain() itself increments the refcount and returns the
 * same pointer -- the pointer-identity half is asserted directly here,
 * not merely relied upon implicitly by a later call, so a retain() that
 * returned a different (or NULL) pointer would fail this assertion on
 * its own. */
//cfusa:test REQ-AVTP-016
static void test_loopback_transport_retain_returns_same_pointer(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 1);
    rcp_avtp_transport_t *retained = rcp_avtp_transport_retain(t);

    TEST_ASSERT_EQUAL_PTR(t, retained);

    rcp_avtp_transport_release(t); /* undo the retain above */
    rcp_avtp_transport_release(t); /* drops to 0: frees */
}

/* REQ-AVTP-027: release() invokes destroy() only once refcount reaches
 * zero, never before -- retain()+release() nets back to the original
 * refcount (1), and the transport is still genuinely usable (not
 * use-after-freed) afterwards; only the SECOND release() below actually
 * drops to zero and frees. Under this project's own ASan CI job, a
 * release() that destroyed prematurely would turn the send() below into
 * a detectable use-after-free, and one that never destroyed at refcount
 * zero would show up as a detected leak. */
//cfusa:test REQ-AVTP-027
static void test_loopback_transport_release_defers_destroy_until_zero(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 1);
    uint8_t frame[] = {1};

    rcp_avtp_transport_retain(t);
    rcp_avtp_transport_release(t); /* back to refcount 1: still usable */

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame, sizeof(frame)));

    rcp_avtp_transport_release(t); /* drops to 0: frees */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_subtype_constants);

    RUN_TEST(test_stream_id_make_preserves_mac_and_unique_id);
    RUN_TEST(test_stream_id_u64_roundtrip);
    RUN_TEST(test_stream_id_u64_matches_expected_layout);
    RUN_TEST(test_stream_id_equal_requires_mac_and_unique_id_match);

    RUN_TEST(test_avtp_addr_equal_requires_byte_bus_id_match);

    RUN_TEST(test_ntscf_roundtrip_no_payload);
    RUN_TEST(test_ntscf_roundtrip_with_payload);
    RUN_TEST(test_ntscf_decode_rejects_short_frame);
    RUN_TEST(test_ntscf_decode_rejects_wrong_subtype);
    RUN_TEST(test_ntscf_decode_rejects_declared_length_past_buffer);
    RUN_TEST(test_ntscf_encode_rejects_oversized_payload);

    RUN_TEST(test_tscf_roundtrip_no_payload);
    RUN_TEST(test_tscf_roundtrip_with_payload);
    RUN_TEST(test_tscf_decode_rejects_short_frame);
    RUN_TEST(test_tscf_decode_rejects_wrong_subtype);
    RUN_TEST(test_tscf_decode_rejects_declared_length_past_buffer);

    RUN_TEST(test_extend_timestamp_exact_match_returns_reference_now);
    RUN_TEST(test_extend_timestamp_near_future_within_half_period);
    RUN_TEST(test_extend_timestamp_near_past_within_half_period);
    RUN_TEST(test_extend_timestamp_wraps_forward_when_wire_ts_just_past_boundary);
    RUN_TEST(test_extend_timestamp_wraps_backward_when_wire_ts_just_before_boundary);
    RUN_TEST(test_extend_timestamp_exactly_half_period_prefers_no_wrap);
    RUN_TEST(test_extend_timestamp_exactly_half_period_backward_prefers_no_wrap);
    RUN_TEST(test_extend_timestamp_result_feeds_rcp_timed_due_correctly);

    RUN_TEST(test_peek_subtype_reads_first_byte);
    RUN_TEST(test_peek_subtype_rejects_empty_buffer);
    RUN_TEST(test_should_drop_tscf_without_time_sync);
    RUN_TEST(test_should_not_drop_tscf_with_time_sync);
    RUN_TEST(test_should_not_drop_ntscf_regardless_of_time_sync);
    RUN_TEST(test_should_not_drop_tscf_without_time_sync_when_policy_is_ignore);
    RUN_TEST(test_ignore_policy_irrelevant_when_time_sync_supported);

    RUN_TEST(test_tscf_reserved_all_zero_true_for_freshly_decoded_conformant_header);
    RUN_TEST(test_tscf_reserved_all_zero_false_when_reserved0_nonzero);
    RUN_TEST(test_tscf_reserved_all_zero_false_when_reserved1_nonzero);
    RUN_TEST(test_decode_tscf_reads_nonzero_reserved_bytes_off_the_wire);

    RUN_TEST(test_decode_tscf_reports_tu_one_and_tu_zero_faithfully);

    RUN_TEST(test_avtp_strerror_unique_nonempty);

    RUN_TEST(test_loopback_transport_send_recv_fifo_order);
    RUN_TEST(test_loopback_transport_recv_times_out_when_empty);
    RUN_TEST(test_loopback_transport_send_rejects_after_close);
    RUN_TEST(test_loopback_transport_recv_rejects_after_close_when_empty);
    RUN_TEST(test_loopback_transport_close_drains_queued_frame_first);
    RUN_TEST(test_loopback_transport_send_rejects_when_full);
    RUN_TEST(test_loopback_transport_retain_returns_same_pointer);
    RUN_TEST(test_loopback_transport_release_defers_destroy_until_zero);

    return UNITY_END();
}
