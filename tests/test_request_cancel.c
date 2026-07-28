//cfusa:test REQ-CANCEL-001
//cfusa:test REQ-CANCEL-002
//cfusa:test REQ-CANCEL-003
//cfusa:test REQ-CANCEL-004
//cfusa:test REQ-CANCEL-005
//cfusa:test REQ-CANCEL-006
//cfusa:test REQ-CANCEL-007
//cfusa:test REQ-CANCEL-008
//cfusa:test REQ-CANCEL-009
//cfusa:test REQ-CANCEL-010
//cfusa:test REQ-CANCEL-011
//cfusa:test REQ-CANCEL-012
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/request_cancel.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    const char *a = rcp_cancel_strerror(RCP_CANCEL_OK);
    const char *b = rcp_cancel_strerror(RCP_CANCEL_ERR_SHORT_FRAME);
    const char *c = rcp_cancel_strerror(RCP_CANCEL_ERR_BAD_MSG_TYPE);
    const char *d = rcp_cancel_strerror(RCP_CANCEL_ERR_NOT_REPURPOSED);
    const char *e = rcp_cancel_strerror(RCP_CANCEL_ERR_UNKNOWN_TYPE);
    const char *unk = rcp_cancel_strerror((rcp_cancel_errc_t)999);

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

/* ── clear-all (0x05) ─────────────────────────────────────────────────────── */

static void test_clear_all_round_trip(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint8_t txn = 0;

    frame = rcp_cancel_encode_clear_all(4, 21);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_OK, rcp_cancel_decode_clear_all(frame.data, frame.len, &bbid,
                                                                      &txn));
    TEST_ASSERT_EQUAL_UINT8(4, bbid);
    TEST_ASSERT_EQUAL_UINT8(21, txn);

    rcp_bytes_free(&frame);
}

static void test_clear_all_decode_rejects_clear_single(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint8_t txn = 0;

    frame = rcp_cancel_encode_clear_single(0, 3, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_ERR_UNKNOWN_TYPE,
                           rcp_cancel_decode_clear_all(frame.data, frame.len, &bbid, &txn));

    rcp_bytes_free(&frame);
}

static void test_clear_all_decode_rejects_short_frame(void)
{
    uint8_t buf[2] = {0};
    rcp_byte_bus_id_t bbid = 0;
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_ERR_SHORT_FRAME,
                           rcp_cancel_decode_clear_all(buf, sizeof(buf), &bbid, &txn));
}

/* ── clear-single (0x07) ──────────────────────────────────────────────────── */

static void test_clear_single_round_trip(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint8_t clear_txn = 0;
    uint8_t txn = 0;

    frame = rcp_cancel_encode_clear_single(2, 99, 5);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_OK,
                           rcp_cancel_decode_clear_single(frame.data, frame.len, &bbid, &clear_txn,
                                                           &txn));
    TEST_ASSERT_EQUAL_UINT8(2, bbid);
    TEST_ASSERT_EQUAL_UINT8(99, clear_txn);
    TEST_ASSERT_EQUAL_UINT8(5, txn);

    rcp_bytes_free(&frame);
}

static void test_clear_single_decode_rejects_clear_all(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint8_t clear_txn = 0;
    uint8_t txn = 0;

    frame = rcp_cancel_encode_clear_all(0, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_ERR_UNKNOWN_TYPE,
                           rcp_cancel_decode_clear_single(frame.data, frame.len, &bbid, &clear_txn,
                                                           &txn));

    rcp_bytes_free(&frame);
}

static void test_clear_single_decode_rejects_bad_msg_type(void)
{
    uint8_t buf[16] = {0};
    rcp_byte_bus_id_t bbid = 0;
    uint8_t clear_txn = 0;
    uint8_t txn = 0;

    buf[0] = RCP_ACF_MSG_TYPE_ABB;

    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_ERR_BAD_MSG_TYPE,
                           rcp_cancel_decode_clear_single(buf, sizeof(buf), &bbid, &clear_txn,
                                                           &txn));
}

/* ── General cancellation semantics ───────────────────────────────────────── */

static void test_is_cancellable_only_when_queued(void)
{
    TEST_ASSERT_TRUE(rcp_cancel_is_cancellable(RCP_CANCEL_LIFECYCLE_QUEUED));
    TEST_ASSERT_FALSE(rcp_cancel_is_cancellable(RCP_CANCEL_LIFECYCLE_EXECUTING));
    TEST_ASSERT_FALSE(rcp_cancel_is_cancellable(RCP_CANCEL_LIFECYCLE_DONE));
}

static void test_attempt_not_found(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_RESULT_NOT_FOUND,
                           rcp_cancel_attempt(false, RCP_CANCEL_LIFECYCLE_QUEUED));
}

static void test_attempt_not_cancellable(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_RESULT_NOT_CANCELLABLE,
                           rcp_cancel_attempt(true, RCP_CANCEL_LIFECYCLE_EXECUTING));
    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_RESULT_NOT_CANCELLABLE,
                           rcp_cancel_attempt(true, RCP_CANCEL_LIFECYCLE_DONE));
}

static void test_attempt_canceled(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_CANCEL_RESULT_CANCELED,
                           rcp_cancel_attempt(true, RCP_CANCEL_LIFECYCLE_QUEUED));
}

static void test_chain_cascade(void)
{
    /* Canceling position 1 of a chain cascades to positions 1, 2, 3, ...
     * but never to position 0 (already executed, sequential execution). */
    TEST_ASSERT_FALSE(rcp_cancel_chain_should_cascade(0, 1));
    TEST_ASSERT_TRUE(rcp_cancel_chain_should_cascade(1, 1));
    TEST_ASSERT_TRUE(rcp_cancel_chain_should_cascade(2, 1));
    TEST_ASSERT_TRUE(rcp_cancel_chain_should_cascade(3, 1));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_clear_all_round_trip);
    RUN_TEST(test_clear_all_decode_rejects_clear_single);
    RUN_TEST(test_clear_all_decode_rejects_short_frame);

    RUN_TEST(test_clear_single_round_trip);
    RUN_TEST(test_clear_single_decode_rejects_clear_all);
    RUN_TEST(test_clear_single_decode_rejects_bad_msg_type);

    RUN_TEST(test_is_cancellable_only_when_queued);
    RUN_TEST(test_attempt_not_found);
    RUN_TEST(test_attempt_not_cancellable);
    RUN_TEST(test_attempt_canceled);
    RUN_TEST(test_chain_cascade);

    return UNITY_END();
}
