//cfusa:test REQ-TIMED-001
//cfusa:test REQ-TIMED-002
//cfusa:test REQ-TIMED-003
//cfusa:test REQ-TIMED-004
//cfusa:test REQ-TIMED-005
//cfusa:test REQ-TIMED-006
//cfusa:test REQ-TIMED-007
//cfusa:test REQ-TIMED-008
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/regmap.h>
#include <rcp/timed.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    const char *a = rcp_timed_strerror(RCP_TIMED_OK);
    const char *b = rcp_timed_strerror(RCP_TIMED_ERR_SHORT_FRAME);
    const char *c = rcp_timed_strerror(RCP_TIMED_ERR_BAD_MSG_TYPE);
    const char *d = rcp_timed_strerror(RCP_TIMED_ERR_NOT_REPURPOSED);
    const char *e = rcp_timed_strerror(RCP_TIMED_ERR_UNKNOWN_TYPE);
    const char *unk = rcp_timed_strerror((rcp_timed_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
    TEST_ASSERT_TRUE(strcmp(c, d) != 0);
    TEST_ASSERT_TRUE(strcmp(d, e) != 0);
}

/* ── Feature gating ────────────────────────────────────────────────────────── */

static void test_feature_enabled_requires_both_bits(void)
{
    TEST_ASSERT_FALSE(rcp_timed_feature_enabled(0));
    TEST_ASSERT_FALSE(rcp_timed_feature_enabled(RCP_REGMAP_OPT_TIME_SYNC_TSCF));
    TEST_ASSERT_FALSE(rcp_timed_feature_enabled(RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION));
    TEST_ASSERT_TRUE(rcp_timed_feature_enabled(RCP_REGMAP_OPT_TIME_SYNC_TSCF |
                                                RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION));
    TEST_ASSERT_TRUE(rcp_timed_feature_enabled(RCP_REGMAP_OPT_TIME_SYNC_TSCF |
                                                RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION |
                                                RCP_REGMAP_OPT_ENH_CANCEL_REQUEST));
}

/* ── encode/decode round trip ─────────────────────────────────────────────── */

static void test_timed_request_round_trip(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint32_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;
    uint8_t body[4] = {1, 2, 3, 4};

    frame = rcp_timed_encode_request(6, 0xDEADBEEFu, 17, body, sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_OK,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));
    TEST_ASSERT_EQUAL_UINT8(6, bbid);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, pt);
    TEST_ASSERT_EQUAL_UINT8(17, txn);
    TEST_ASSERT_EQUAL_size_t(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    rcp_bytes_free(&frame);
}

static void test_timed_request_zero_payload(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint32_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_timed_encode_request(0, 0, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_OK,
                           rcp_timed_decode_request(frame.data, frame.len, &bbid, &pt, &payload,
                                                     &payload_len, &txn));
    TEST_ASSERT_EQUAL_UINT32(0, pt);
    TEST_ASSERT_EQUAL_size_t(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_decode_rejects_short_frame(void)
{
    uint8_t buf[4] = {0};
    rcp_byte_bus_id_t bbid = 0;
    uint32_t pt = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ERR_SHORT_FRAME,
                           rcp_timed_decode_request(buf, sizeof(buf), &bbid, &pt, &payload,
                                                     &payload_len, &txn));
}

static void test_decode_rejects_unknown_request_type(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint32_t pt = 0;
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

static void test_too_far_future_beyond_horizon(void)
{
    TEST_ASSERT_TRUE(rcp_timed_too_far(2000, 1000, 500));
    TEST_ASSERT_FALSE(rcp_timed_too_far(1500, 1000, 500));
    TEST_ASSERT_FALSE(rcp_timed_too_far(1000, 1000, 0));
}

static void test_too_far_past_never_too_far(void)
{
    TEST_ASSERT_FALSE(rcp_timed_too_far(500, 1000, 0));
    TEST_ASSERT_FALSE(rcp_timed_too_far(0, 1000, 0));
}

static void test_too_far_wraparound_safe(void)
{
    uint32_t now = 0xFFFFFFF0u;
    uint32_t pt  = 0x00000010u; /* wraps forward past UINT32_MAX */

    TEST_ASSERT_FALSE(rcp_timed_too_far(pt, now, 100));
    TEST_ASSERT_TRUE(rcp_timed_too_far(pt, now, 5));
}

static void test_admit_gptp_fail_takes_priority(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_REJECT_GPTP_FAIL, rcp_timed_admit(false, 100000, 0, 10));
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_REJECT_GPTP_FAIL, rcp_timed_admit(false, 0, 0, 0xFFFFFFFFu));
}

static void test_admit_presentation_time_too_far(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR,
                           rcp_timed_admit(true, 2000, 1000, 500));
}

static void test_admit_accept(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ACCEPT, rcp_timed_admit(true, 1200, 1000, 500));
    TEST_ASSERT_EQUAL_INT(RCP_TIMED_ACCEPT, rcp_timed_admit(true, 0, 1000, 500));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);
    RUN_TEST(test_feature_enabled_requires_both_bits);

    RUN_TEST(test_timed_request_round_trip);
    RUN_TEST(test_timed_request_zero_payload);
    RUN_TEST(test_decode_rejects_short_frame);
    RUN_TEST(test_decode_rejects_unknown_request_type);

    RUN_TEST(test_too_far_future_beyond_horizon);
    RUN_TEST(test_too_far_past_never_too_far);
    RUN_TEST(test_too_far_wraparound_safe);
    RUN_TEST(test_admit_gptp_fail_takes_priority);
    RUN_TEST(test_admit_presentation_time_too_far);
    RUN_TEST(test_admit_accept);

    return UNITY_END();
}
