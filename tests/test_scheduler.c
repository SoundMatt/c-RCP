//cfusa:test REQ-SCHED-001
//cfusa:test REQ-SCHED-002
//cfusa:test REQ-SCHED-003
//cfusa:test REQ-SCHED-004
//cfusa:test REQ-SCHED-005
//cfusa:test REQ-SCHED-006
//cfusa:test REQ-SCHED-007
//cfusa:test REQ-SCHED-008
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/cancel.h>
#include <rcp/chained.h>
#include <rcp/compound.h>
#include <rcp/scheduler.h>
#include <rcp/timed.h>
#include <rcp/triggered.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Request kind classification ──────────────────────────────────────────── */

static void test_classify_standard_when_not_repurposed(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_STANDARD,
                           rcp_sched_classify(false, RCP_REQUEST_TYPE_TRIGGERED));
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_STANDARD, rcp_sched_classify(false, 0x00u));
}

static void test_classify_cancellation(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_CANCELLATION,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_CLEAR_ALL));
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_CANCELLATION,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE));
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_CANCELLATION,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_CLEAR_SINGLE));
}

static void test_classify_triggered(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_TRIGGERED,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_TRIGGERED));
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_TRIGGERED,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_TRIGGERED_SAFETY));
}

static void test_classify_timed(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_TIMED, rcp_sched_classify(true, RCP_REQUEST_TYPE_TIMED));
}

static void test_classify_compound_and_compound_wait(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_COMPOUND,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_COMPOUND));
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_COMPOUND,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_COMPOUND_SAFETY));
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_COMPOUND_WAIT,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_COMPOUND_WAIT));
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_COMPOUND_WAIT,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY));
}

static void test_classify_chained(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_CHAINED,
                           rcp_sched_classify(true, RCP_REQUEST_TYPE_CHAINED));
}

static void test_classify_unrecognized_opcode_falls_back_to_standard(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_SCHED_KIND_STANDARD, rcp_sched_classify(true, 0x77u));
}

/* ── Priority rank + total ordering ───────────────────────────────────────── */

static void test_rank_ordering_matches_roadmap(void)
{
    TEST_ASSERT_GREATER_THAN_UINT8(rcp_sched_kind_rank(RCP_SCHED_KIND_TRIGGERED),
                                    rcp_sched_kind_rank(RCP_SCHED_KIND_CANCELLATION));
    TEST_ASSERT_GREATER_THAN_UINT8(rcp_sched_kind_rank(RCP_SCHED_KIND_TIMED),
                                    rcp_sched_kind_rank(RCP_SCHED_KIND_TRIGGERED));
    TEST_ASSERT_GREATER_THAN_UINT8(rcp_sched_kind_rank(RCP_SCHED_KIND_COMPOUND),
                                    rcp_sched_kind_rank(RCP_SCHED_KIND_TIMED));
    TEST_ASSERT_GREATER_THAN_UINT8(rcp_sched_kind_rank(RCP_SCHED_KIND_COMPOUND_WAIT),
                                    rcp_sched_kind_rank(RCP_SCHED_KIND_COMPOUND));
    TEST_ASSERT_GREATER_THAN_UINT8(rcp_sched_kind_rank(RCP_SCHED_KIND_CHAINED),
                                    rcp_sched_kind_rank(RCP_SCHED_KIND_COMPOUND_WAIT));
    TEST_ASSERT_GREATER_THAN_UINT8(rcp_sched_kind_rank(RCP_SCHED_KIND_STANDARD),
                                    rcp_sched_kind_rank(RCP_SCHED_KIND_CHAINED));
}

static void test_compare_higher_kind_first(void)
{
    rcp_sched_entry_t cancel = {RCP_SCHED_KIND_CANCELLATION, 100};
    rcp_sched_entry_t standard = {RCP_SCHED_KIND_STANDARD, 1};

    /* A cancellation arriving *after* a standard request still services
     * first -- kind dominates arrival order. */
    TEST_ASSERT_TRUE(rcp_sched_compare(&cancel, &standard) < 0);
    TEST_ASSERT_TRUE(rcp_sched_compare(&standard, &cancel) > 0);
}

static void test_compare_fifo_within_equal_kind(void)
{
    rcp_sched_entry_t first = {RCP_SCHED_KIND_COMPOUND, 5};
    rcp_sched_entry_t second = {RCP_SCHED_KIND_COMPOUND, 6};

    TEST_ASSERT_TRUE(rcp_sched_compare(&first, &second) < 0);
    TEST_ASSERT_TRUE(rcp_sched_compare(&second, &first) > 0);
}

static void test_compare_equal_entries(void)
{
    rcp_sched_entry_t a = {RCP_SCHED_KIND_TIMED, 42};
    rcp_sched_entry_t b = {RCP_SCHED_KIND_TIMED, 42};

    TEST_ASSERT_EQUAL_INT(0, rcp_sched_compare(&a, &b));
}

/* ── Multi-request-per-frame handling ─────────────────────────────────────── */

static void test_split_frame_members_multiple(void)
{
    rcp_bytes_t m1;
    rcp_bytes_t m2;
    uint8_t frame[64];
    size_t offsets[4] = {0};
    size_t n;

    m1 = rcp_cancel_encode_clear_all(0, 0);
    m2 = rcp_cancel_encode_clear_all(1, 0);
    TEST_ASSERT_NOT_NULL(m1.data);
    TEST_ASSERT_NOT_NULL(m2.data);
    TEST_ASSERT_TRUE(m1.len + m2.len <= sizeof(frame));

    memcpy(&frame[0], m1.data, m1.len);
    memcpy(&frame[m1.len], m2.data, m2.len);

    n = rcp_sched_split_frame_members(frame, m1.len + m2.len, offsets, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_size_t(0, offsets[0]);
    TEST_ASSERT_EQUAL_size_t(m1.len, offsets[1]);

    rcp_bytes_free(&m1);
    rcp_bytes_free(&m2);
}

static void test_split_frame_members_out_cap_still_counts(void)
{
    rcp_bytes_t m1;
    rcp_bytes_t m2;
    uint8_t frame[64];
    size_t offsets[1] = {0xFFu};
    size_t n;

    m1 = rcp_cancel_encode_clear_all(0, 0);
    m2 = rcp_cancel_encode_clear_all(1, 0);
    memcpy(&frame[0], m1.data, m1.len);
    memcpy(&frame[m1.len], m2.data, m2.len);

    n = rcp_sched_split_frame_members(frame, m1.len + m2.len, offsets, 1);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_size_t(0, offsets[0]); /* only the first entry was written */

    rcp_bytes_free(&m1);
    rcp_bytes_free(&m2);
}

static void test_split_frame_members_malformed_returns_zero(void)
{
    uint8_t bad_type[8] = {0};
    uint8_t truncated[4] = {0};
    size_t offsets[4] = {0};

    bad_type[0] = 0xFFu; /* neither ABB nor GBB */
    TEST_ASSERT_EQUAL_size_t(0, rcp_sched_split_frame_members(bad_type, sizeof(bad_type), offsets,
                                                                4));

    truncated[0] = RCP_ACF_MSG_TYPE_GBB; /* GBB header is 16 bytes, buffer is 4 */
    TEST_ASSERT_EQUAL_size_t(0, rcp_sched_split_frame_members(truncated, sizeof(truncated),
                                                                offsets, 4));
}

static void test_split_frame_members_empty(void)
{
    size_t offsets[1] = {0};
    TEST_ASSERT_EQUAL_size_t(0, rcp_sched_split_frame_members(NULL, 0, offsets, 1));
}

static void test_frame_timing_consistent_ntscf_always_true(void)
{
    bool mixed[2] = {true, false};
    TEST_ASSERT_TRUE(rcp_sched_frame_timing_consistent(false, mixed, 2));
}

static void test_frame_timing_consistent_tscf_uniform(void)
{
    bool all_timed[3] = {true, true, true};
    bool all_untimed[3] = {false, false, false};

    TEST_ASSERT_TRUE(rcp_sched_frame_timing_consistent(true, all_timed, 3));
    TEST_ASSERT_TRUE(rcp_sched_frame_timing_consistent(true, all_untimed, 3));
    TEST_ASSERT_TRUE(rcp_sched_frame_timing_consistent(true, NULL, 0));
}

static void test_frame_timing_consistent_tscf_mixed_rejected(void)
{
    bool mixed[3] = {true, false, true};
    TEST_ASSERT_FALSE(rcp_sched_frame_timing_consistent(true, mixed, 3));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_classify_standard_when_not_repurposed);
    RUN_TEST(test_classify_cancellation);
    RUN_TEST(test_classify_triggered);
    RUN_TEST(test_classify_timed);
    RUN_TEST(test_classify_compound_and_compound_wait);
    RUN_TEST(test_classify_chained);
    RUN_TEST(test_classify_unrecognized_opcode_falls_back_to_standard);

    RUN_TEST(test_rank_ordering_matches_roadmap);
    RUN_TEST(test_compare_higher_kind_first);
    RUN_TEST(test_compare_fifo_within_equal_kind);
    RUN_TEST(test_compare_equal_entries);

    RUN_TEST(test_split_frame_members_multiple);
    RUN_TEST(test_split_frame_members_out_cap_still_counts);
    RUN_TEST(test_split_frame_members_malformed_returns_zero);
    RUN_TEST(test_split_frame_members_empty);

    RUN_TEST(test_frame_timing_consistent_ntscf_always_true);
    RUN_TEST(test_frame_timing_consistent_tscf_uniform);
    RUN_TEST(test_frame_timing_consistent_tscf_mixed_rejected);

    return UNITY_END();
}
