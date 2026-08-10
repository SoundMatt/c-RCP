/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-RMAP-059
#include "unity.h"

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

    rcp_respqueue_init(&q, 0);
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

    rcp_respqueue_init(&q, 0);
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

    rcp_respqueue_init(&q, 0);
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
    rcp_respqueue_init(&q, 10u);
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

    rcp_respqueue_init(&q, 8u);
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

    rcp_respqueue_init(&q, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));
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

    return UNITY_END();
}
