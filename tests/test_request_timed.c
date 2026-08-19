/* SPDX-License-Identifier: MPL-2.0 */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/regmap.h>
#include <rcp/request.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── strerror ──────────────────────────────────────────────────────────────── */

//cfusa:test REQ-TIMED-001
static void test_strerror_never_null_and_distinct(void)
{
    const char *a = rcp_timed_strerror(RCP_TIMED_OK);
    const char *b = rcp_timed_strerror(RCP_TIMED_ERR_SHORT_FRAME);
    const char *c = rcp_timed_strerror(RCP_TIMED_ERR_BAD_MSG_TYPE);
    const char *d = rcp_timed_strerror(RCP_TIMED_ERR_NOT_REPURPOSED);
    const char *e = rcp_timed_strerror(RCP_TIMED_ERR_UNKNOWN_TYPE);
    const char *f = rcp_timed_strerror(RCP_TIMED_ERR_RESERVED_NONZERO);
    const char *g = rcp_timed_strerror(RCP_TIMED_ERR_UNSUPPORTED_CMD);
    const char *unk = rcp_timed_strerror((rcp_timed_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
    TEST_ASSERT_TRUE(strcmp(c, d) != 0);
    TEST_ASSERT_TRUE(strcmp(d, e) != 0);
    TEST_ASSERT_TRUE(strcmp(e, f) != 0);
    TEST_ASSERT_TRUE(strcmp(f, g) != 0);
}

/* ── Feature gating ────────────────────────────────────────────────────────── */

/* REQ-RMAP-030: a single bit now, not a required pair (this function's
 * own former two-bit-pair check retired alongside REQ-RMAP-004..008). */
//cfusa:test REQ-TIMED-002
static void test_feature_enabled_requires_the_time_sync_bit(void)
{
    TEST_ASSERT_FALSE(rcp_timed_feature_enabled(0));
    TEST_ASSERT_FALSE(rcp_timed_feature_enabled(RCP_REGMAP_OPT_COMPOUND_WAIT));
    TEST_ASSERT_TRUE(rcp_timed_feature_enabled(RCP_REGMAP_OPT_TIME_SYNC));
    TEST_ASSERT_TRUE(rcp_timed_feature_enabled((uint8_t)(RCP_REGMAP_OPT_TIME_SYNC |
                                                          RCP_REGMAP_OPT_ENH_CANCEL)));
}

/* ── encode/decode round trip ─────────────────────────────────────────────── */

//cfusa:test REQ-TIMED-003
//cfusa:test REQ-TIMED-005
static void test_timed_request_round_trip(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint64_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;
    uint8_t body[4] = {1, 2, 3, 4};

    frame = rcp_timed_encode_request(6, 0x0000DEADBEEFCAFEull, 17, body, sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_OK,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));
    TEST_ASSERT_EQUAL_UINT8(6, bbid);
    TEST_ASSERT_EQUAL_UINT64(0x0000DEADBEEFCAFEull, pt);
    TEST_ASSERT_EQUAL_UINT8(17, txn);
    TEST_ASSERT_EQUAL_size_t(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-TIMED-003
//cfusa:test REQ-TIMED-005
static void test_timed_request_zero_payload(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint64_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_timed_encode_request(0, 0, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_OK,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));
    TEST_ASSERT_EQUAL_UINT64(0, pt);
    TEST_ASSERT_EQUAL_size_t(0, payload_len);

    rcp_bytes_free(&frame);
}

static const uint8_t kTscfMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

/* REQ-TIMED-013: TC18's OTHER encoding path -- a plain ACF_ABB request
 * wrapped in a TSCF header, presentation time carried by the header's own
 * avtp_timestamp rather than packed into the payload. */
static void test_tscf_request_round_trip(void)
{
    rcp_bytes_t                  frame;
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_acf_byte_message_info_t  out_hdr;
    rcp_avtp_tscf_header_t       out_tscf;
    const uint8_t               *acf_payload;
    size_t                       acf_payload_len;
    const uint8_t               *tscf_payload;
    size_t                       tscf_payload_len;
    rcp_stream_id_t              sid = rcp_stream_id_make(kTscfMac, 7u);
    uint8_t                      body[3] = {0xAA, 0xBB, 0xCC};

    hdr.byte_bus_id     = 9;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 42;

    frame = rcp_timed_encode_request_tscf(&hdr, body, sizeof(body), sid, 0xDEADBEEFu, 3u);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_AVTP_OK,
                           rcp_avtp_decode_tscf(frame.data, frame.len, &out_tscf,
                                                 &tscf_payload, &tscf_payload_len));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, out_tscf.avtp_timestamp);
    TEST_ASSERT_EQUAL_UINT8(1u, out_tscf.tv); /* timestamp valid -- this function's own point */
    TEST_ASSERT_EQUAL_UINT8(3u, out_tscf.sequence_num);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(sid, out_tscf.stream_id));

    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                           rcp_acf_decode_abb(tscf_payload, tscf_payload_len, &out_hdr,
                                               &acf_payload, &acf_payload_len));
    /* A PLAIN ACF_ABB message -- no request_type opcode, no repurposing
     * trick, unlike rcp_timed_encode_request()'s own NTSCF path. */
    TEST_ASSERT_EQUAL_UINT8(9u, out_hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OP_WRITE, out_hdr.op);
    TEST_ASSERT_EQUAL_UINT8(42u, out_hdr.transaction_num);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_MTV_UNTIMED, out_hdr.mtv);
    TEST_ASSERT_EQUAL_size_t(sizeof(body), acf_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, acf_payload, sizeof(body));

    rcp_bytes_free(&frame);
}

static void test_tscf_request_zero_payload(void)
{
    rcp_bytes_t                  frame;
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_stream_id_t              sid = {0};

    frame = rcp_timed_encode_request_tscf(&hdr, NULL, 0, sid, 0u, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_bytes_free(&frame);
}

/* Failure at the ACF_ABB layer (oversized payload) must propagate as a
 * zeroed rcp_bytes_t, not a TSCF frame wrapping garbage. rcp_acf_encode_abb()
 * itself rejects against RCP_ACF_ABB_MAX_PAYLOAD, not the smaller
 * RCP_ACF_MAX_PAYLOAD (which aliases GBB's own, shorter-header-derived
 * capacity) -- see acf.h's own file header for why the two differ. */
static void test_tscf_request_rejects_oversized_payload(void)
{
    rcp_bytes_t                  frame;
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_stream_id_t              sid = {0};
    static uint8_t                too_big[RCP_ACF_ABB_MAX_PAYLOAD + 1];

    memset(too_big, 0, sizeof(too_big));
    frame = rcp_timed_encode_request_tscf(&hdr, too_big, sizeof(too_big), sid, 0u, 0u);
    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-TIMED-004
static void test_decode_rejects_short_frame(void)
{
    uint8_t buf[4] = {0};
    rcp_byte_bus_id_t bbid = 0;
    uint64_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ERR_SHORT_FRAME,
                           rcp_timed_decode_request(buf, sizeof(buf), &bbid, &pt, &payload,
                                                     &payload_len, &txn));
}

//cfusa:test REQ-TIMED-005
static void test_decode_rejects_unknown_request_type(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint64_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_timed_encode_request(0, 0, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    frame.data[RCP_ACF_ABB_HEADER_LEN] = 0x0Fu;

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ERR_UNKNOWN_TYPE,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));

    rcp_bytes_free(&frame);
}

/* ── Admission ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-TIMED-006
static void test_too_far_future_beyond_horizon(void)
{
    TEST_ASSERT_TRUE(rcp_timed_too_far(2000, 1000, 500));
    TEST_ASSERT_FALSE(rcp_timed_too_far(1500, 1000, 500));
    TEST_ASSERT_FALSE(rcp_timed_too_far(1000, 1000, 0));
}

//cfusa:test REQ-TIMED-006
static void test_too_far_past_never_too_far(void)
{
    TEST_ASSERT_FALSE(rcp_timed_too_far(500, 1000, 0));
    TEST_ASSERT_FALSE(rcp_timed_too_far(0, 1000, 0));
}

//cfusa:test REQ-TIMED-006
static void test_too_far_wraparound_safe(void)
{
    /* presentation_time is reduced modulo 2^48, so "just after the
     * rollover" is genuinely just after "just before it". */
    uint64_t now = RCP_TIMED_PRESENTATION_TIME_MAX - 0x0Full;
    uint64_t pt  = 0x00000010ull; /* wraps forward past the 48-bit maximum */

    TEST_ASSERT_FALSE(rcp_timed_too_far(pt, now, 100));
    TEST_ASSERT_TRUE(rcp_timed_too_far(pt, now, 5));
}

//cfusa:test REQ-TIMED-007
static void test_admit_gptp_fail_takes_priority(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_REJECT_GPTP_FAIL, rcp_timed_admit(false, 100000, 0, 10));
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_REJECT_GPTP_FAIL, rcp_timed_admit(false, 0, 0, 0xFFFFFFFFull));
}

//cfusa:test REQ-TIMED-008
static void test_admit_presentation_time_too_far(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR,
                           rcp_timed_admit(true, 2000, 1000, 500));
}

//cfusa:test REQ-TIMED-008
static void test_admit_accept(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ACCEPT, rcp_timed_admit(true, 1200, 1000, 500));
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ACCEPT, rcp_timed_admit(true, 0, 1000, 500));
}

/* ── REQ-WIREERR-006 (issue #163): rcp_timed_wire_error() ─────────────────── */

static void test_wire_error_maps_gptp_fail_to_the_numbered_code(void)
{
    const int wire_code = (int)rcp_timed_wire_error(RCP_TIMED_REJECT_GPTP_FAIL);

    TEST_ASSERT_EQUAL_INT(14, wire_code);
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_GPTP_FAIL, wire_code);
}

static void test_wire_error_maps_presentation_time_too_far_to_the_numbered_code(void)
{
    const int wire_code = (int)rcp_timed_wire_error(RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR);

    TEST_ASSERT_EQUAL_INT(13, wire_code);
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_PRESENTATION_TIME_TOO_FAR, wire_code);
}

static void test_wire_error_accept_maps_to_none(void)
{
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE, (int)rcp_timed_wire_error(RCP_TIMED_ACCEPT));
}

/* End-to-end: whatever rcp_timed_admit() itself decides, for a range of
 * inputs, rcp_timed_wire_error() of that decision is always the correct
 * numbered code -- ties the two functions together rather than testing
 * each in isolation only against hand-picked enum values. */
static void test_wire_error_matches_admit_across_inputs(void)
{
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_GPTP_FAIL,
                          (int)rcp_timed_wire_error(rcp_timed_admit(false, 100000, 0, 10)));
    TEST_ASSERT_EQUAL_INT(
        (int)RCP_ERROR_PRESENTATION_TIME_TOO_FAR,
        (int)rcp_timed_wire_error(rcp_timed_admit(true, 2000, 1000, 500)));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_timed_wire_error(rcp_timed_admit(true, 1200, 1000, 500)));
}


/* ── Literal wire layout ──────────────────────────────────────────────────────
 *
 * Written from the TC18 v0.5.1_RC timed-request figure and field table
 * (§11.2.2.5, Figure 12 / Table 10), NOT copied back out of this encoder.
 * The figure splits the repurposed message_timestamp region as:
 *
 *   offset 0     request_type              (one octet, 0x0A)
 *   offset 1     reserved                  (one octet, all bits zero)
 *   offsets 2..3 presentation_time [47:32]
 *   offsets 4..7 presentation_time [31:0]
 *
 * i.e. one 48-bit big-endian quantity spanning offsets 2..7. Before
 * v0.102.0 this module packed a 32-bit value starting at offset 1, which
 * both overwrote the mandatory reserved octet and truncated the field. */

#define TS_OFF RCP_ACF_ABB_HEADER_LEN

//cfusa:test REQ-TIMED-003
static void test_timed_wire_sub_field_offsets(void)
{
    rcp_bytes_t frame = rcp_timed_encode_request(7, 0x0000112233445566ull, 1, NULL, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len >= TS_OFF + 8u);

    TEST_ASSERT_EQUAL_HEX8(0x0A, frame.data[TS_OFF + 0]); /* request_type = timed */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 1]); /* reserved, mandatorily zero */
    TEST_ASSERT_EQUAL_HEX8(0x11, frame.data[TS_OFF + 2]); /* presentation_time [47:40] */
    TEST_ASSERT_EQUAL_HEX8(0x22, frame.data[TS_OFF + 3]); /* presentation_time [39:32] */
    TEST_ASSERT_EQUAL_HEX8(0x33, frame.data[TS_OFF + 4]); /* presentation_time [31:24] */
    TEST_ASSERT_EQUAL_HEX8(0x44, frame.data[TS_OFF + 5]);
    TEST_ASSERT_EQUAL_HEX8(0x55, frame.data[TS_OFF + 6]);
    TEST_ASSERT_EQUAL_HEX8(0x66, frame.data[TS_OFF + 7]); /* presentation_time [7:0] */

    rcp_bytes_free(&frame);
}

/* The reserved octet stays zero even for the largest encodable
 * presentation_time -- i.e. the field really is 48 bits, not 56. */
//cfusa:test REQ-TIMED-003
static void test_timed_reserved_octet_stays_zero_at_max(void)
{
    rcp_bytes_t frame = rcp_timed_encode_request(0, RCP_TIMED_PRESENTATION_TIME_MAX, 0, NULL, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_HEX8(0x0A, frame.data[TS_OFF + 0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame.data[TS_OFF + 2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame.data[TS_OFF + 7]);

    rcp_bytes_free(&frame);
}

/* A presentation_time above the 48-bit maximum is rejected rather than
 * silently truncated into a different instant. */
//cfusa:test REQ-TIMED-003
static void test_timed_encode_rejects_out_of_range_presentation_time(void)
{
    rcp_bytes_t frame = rcp_timed_encode_request(0, RCP_TIMED_PRESENTATION_TIME_MAX + 1ull, 0,
                                                   NULL, 0);
    TEST_ASSERT_NULL(frame.data);
}

/* Table 10: reserved "All bits shall be written as 0, else the request
 * shall be rejected". */
//cfusa:test REQ-TIMED-009
static void test_timed_decode_rejects_nonzero_reserved_octet(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint64_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_timed_encode_request(0, 42, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    frame.data[TS_OFF + 1] = 0x01; /* any set bit at all */

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ERR_RESERVED_NONZERO,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));

    rcp_bytes_free(&frame);
}

/* Table 10: "hs, cs -- All bits shall be written as 0, else the request
 * shall be rejected with error code = UNSUPPORTED_CMD". cs is octet 4
 * bit 0 and hs octet 4 bit 1 of the shared byte_message_info header
 * (acf.h's Table 4 layout). */
//cfusa:test REQ-TIMED-010
static void test_timed_decode_rejects_hs_or_cs_set(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint64_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;
    uint8_t saved;

    frame = rcp_timed_encode_request(0, 42, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    saved = frame.data[4];

    frame.data[4] = (uint8_t)(saved | (1u << 0)); /* cs */
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ERR_UNSUPPORTED_CMD,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));

    frame.data[4] = (uint8_t)(saved | (1u << 1)); /* hs */
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ERR_UNSUPPORTED_CMD,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));

    frame.data[4] = saved;
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_OK,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));

    rcp_bytes_free(&frame);
}

/* The full 48-bit range round-trips: a value needing more than 32 bits is
 * recovered exactly, which the pre-v0.102.0 encoding could not do. */
//cfusa:test REQ-TIMED-005
static void test_timed_round_trips_beyond_32_bits(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint64_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;
    const uint64_t big = 0x0000A5A5FFFFFFFFull; /* > 2^32 */

    frame = rcp_timed_encode_request(2, big, 3, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_OK,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));
    TEST_ASSERT_EQUAL_UINT64(big, pt);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-TIMED-011
static void test_timed_due(void)
{
    TEST_ASSERT_TRUE(rcp_timed_due(1000, 1000));  /* exactly due */
    TEST_ASSERT_TRUE(rcp_timed_due(999, 1000));   /* already past */
    TEST_ASSERT_FALSE(rcp_timed_due(1001, 1000)); /* still future */

    /* Wraparound: a presentation_time just past the 48-bit rollover is
     * still in the future of a "now" just before it. */
    TEST_ASSERT_FALSE(rcp_timed_due(0x10ull, RCP_TIMED_PRESENTATION_TIME_MAX - 0x0Full));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);
    RUN_TEST(test_feature_enabled_requires_the_time_sync_bit);

    RUN_TEST(test_timed_request_round_trip);
    RUN_TEST(test_timed_request_zero_payload);
    RUN_TEST(test_tscf_request_round_trip);
    RUN_TEST(test_tscf_request_zero_payload);
    RUN_TEST(test_tscf_request_rejects_oversized_payload);
    RUN_TEST(test_decode_rejects_short_frame);
    RUN_TEST(test_decode_rejects_unknown_request_type);

    RUN_TEST(test_too_far_future_beyond_horizon);
    RUN_TEST(test_too_far_past_never_too_far);
    RUN_TEST(test_too_far_wraparound_safe);
    RUN_TEST(test_admit_gptp_fail_takes_priority);
    RUN_TEST(test_admit_presentation_time_too_far);
    RUN_TEST(test_admit_accept);

    RUN_TEST(test_wire_error_maps_gptp_fail_to_the_numbered_code);
    RUN_TEST(test_wire_error_maps_presentation_time_too_far_to_the_numbered_code);
    RUN_TEST(test_wire_error_accept_maps_to_none);
    RUN_TEST(test_wire_error_matches_admit_across_inputs);

    RUN_TEST(test_timed_wire_sub_field_offsets);
    RUN_TEST(test_timed_reserved_octet_stays_zero_at_max);
    RUN_TEST(test_timed_encode_rejects_out_of_range_presentation_time);
    RUN_TEST(test_timed_decode_rejects_nonzero_reserved_octet);
    RUN_TEST(test_timed_decode_rejects_hs_or_cs_set);
    RUN_TEST(test_timed_round_trips_beyond_32_bits);
    RUN_TEST(test_timed_due);

    return UNITY_END();
}
