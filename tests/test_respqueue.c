/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-RMAP-059
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/rcp.h>
#include <rcp/respqueue.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Basic FIFO push/pop ─────────────────────────────────────────────────── */

static void test_push_pop_is_fifo(void)
{
    rcp_respqueue_t q;
    const uint8_t   a[] = {1, 2, 3};
    const uint8_t   b[] = {4, 5};
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, a, sizeof(a)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, b, sizeof(b)));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(5u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_UINT(3u, out.len);
    TEST_ASSERT_EQUAL_MEMORY(a, out.data, sizeof(a));
    rcp_bytes_free(&out);
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_UINT(2u, out.len);
    TEST_ASSERT_EQUAL_MEMORY(b, out.data, sizeof(b));
    rcp_bytes_free(&out);

    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));

    rcp_respqueue_destroy(&q);
}

static void test_pop_on_empty_queue_returns_false(void)
{
    rcp_respqueue_t q;
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 0, 0);
    out.data = (uint8_t *)0x1; /* sentinel: must stay untouched */
    out.len  = 42;

    TEST_ASSERT_FALSE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_PTR((uint8_t *)0x1, out.data);
    TEST_ASSERT_EQUAL_UINT(42u, out.len);

    rcp_respqueue_destroy(&q);
}

/* ── REQ-RMAP-059: capacity is an octet budget, not an entry-count limit ──── */

static void test_zero_capacity_is_unbounded(void)
{
    rcp_respqueue_t q;
    uint8_t         big[4096] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, big, sizeof(big)));
    TEST_ASSERT_EQUAL_UINT(sizeof(big), rcp_respqueue_octets(&q));

    rcp_respqueue_destroy(&q);
}

static void test_push_refused_once_capacity_octets_would_be_exceeded(void)
{
    rcp_respqueue_t q;
    const uint8_t   five[5]  = {1, 2, 3, 4, 5};
    const uint8_t   six[6]   = {1, 2, 3, 4, 5, 6};

    /* TC18 §12.7.9 Table 24: queue_size is a memory reservation ("assigned
     * memory in 32bit words"), not a message-count limit -- capacity is
     * checked in octets, and refusal leaves the queue entirely unchanged. */
    rcp_respqueue_init(&q, 10u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, five, sizeof(five)));
    TEST_ASSERT_EQUAL_UINT(5u, rcp_respqueue_octets(&q));

    /* 5 + 6 = 11 > capacity 10: refused. */
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, six, sizeof(six)));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(5u, rcp_respqueue_octets(&q));

    /* Exactly at the remaining budget (5 more octets, total 10): accepted. */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, five, sizeof(five)));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(10u, rcp_respqueue_octets(&q));

    rcp_respqueue_destroy(&q);
}

static void test_pop_frees_capacity_for_a_later_push(void)
{
    rcp_respqueue_t q;
    const uint8_t   frame[8] = {0};
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 8u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    rcp_bytes_free(&out);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
}

/* ── Destroy frees every remaining entry ───────────────────────────────────── */

static void test_destroy_on_nonempty_queue_is_safe(void)
{
    rcp_respqueue_t q;
    const uint8_t   frame[3] = {1, 2, 3};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));
}

/* ── REQ-RMAP-061: a per-message Max_AVTPDUsize ceiling, independent
 * of the aggregate queue_size capacity ──────────────────────────────────── */

static void test_zero_max_avtpdu_size_is_unbounded(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[64] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
}

static void test_push_refused_once_a_single_frame_exceeds_max_avtpdu_size(void)
{
    rcp_respqueue_t q;
    uint8_t         ok[10]  = {0};
    uint8_t         over[11] = {0};

    /* max_avtpdu_size_octets is checked independently of capacity_octets:
     * capacity here (1000) is far larger than either frame, so only the
     * per-message ceiling (10) can be refusing these pushes. */
    rcp_respqueue_init(&q, 1000u, 10u);

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, ok, sizeof(ok)));
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, over, sizeof(over)));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(10u, rcp_respqueue_octets(&q));

    rcp_respqueue_destroy(&q);
}

/* ── REQ-RMAP-062: the fragmentation-budget helper ─────────────────────── */

static void test_max_fragment_payload_reserves_header_and_worst_case_pad(void)
{
    /* max_avtpdu_size_octets=32, ABB header=8: 32 - 8 - 3(pad) = 21. */
    TEST_ASSERT_EQUAL_UINT((size_t)21u,
                           rcp_respqueue_max_fragment_payload(32u, RCP_ACF_ABB_HEADER_LEN));

    /* Same total, but the larger GBB header (16) leaves less budget:
     * 32 - 16 - 3 = 13. */
    TEST_ASSERT_EQUAL_UINT((size_t)13u,
                           rcp_respqueue_max_fragment_payload(32u, RCP_ACF_GBB_HEADER_LEN));
}

static void test_max_fragment_payload_is_zero_when_unbounded_or_no_budget_remains(void)
{
    /* max_avtpdu_size_octets == 0: unbounded, matches
     * RCP_FRAGMENT_ERR_DISABLED's own "fragmentation disabled" reading. */
    TEST_ASSERT_EQUAL_UINT((size_t)0u,
                           rcp_respqueue_max_fragment_payload(0u, RCP_ACF_ABB_HEADER_LEN));

    /* header_len + worst-case pad (3) already meets max_avtpdu_size_octets:
     * no payload budget remains at all. */
    TEST_ASSERT_EQUAL_UINT((size_t)0u,
                           rcp_respqueue_max_fragment_payload(11u, RCP_ACF_ABB_HEADER_LEN));
    TEST_ASSERT_EQUAL_UINT((size_t)0u,
                           rcp_respqueue_max_fragment_payload(5u, RCP_ACF_ABB_HEADER_LEN));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_push_pop_is_fifo);
    RUN_TEST(test_pop_on_empty_queue_returns_false);
    RUN_TEST(test_zero_capacity_is_unbounded);
    RUN_TEST(test_push_refused_once_capacity_octets_would_be_exceeded);
    RUN_TEST(test_pop_frees_capacity_for_a_later_push);
    RUN_TEST(test_destroy_on_nonempty_queue_is_safe);

    RUN_TEST(test_zero_max_avtpdu_size_is_unbounded);
    RUN_TEST(test_push_refused_once_a_single_frame_exceeds_max_avtpdu_size);

    RUN_TEST(test_max_fragment_payload_reserves_header_and_worst_case_pad);
    RUN_TEST(test_max_fragment_payload_is_zero_when_unbounded_or_no_budget_remains);

    return UNITY_END();
}
