/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-LOAN-001
//cfusa:test REQ-LOAN-002
//cfusa:test REQ-LOAN-003
//cfusa:test REQ-LOAN-004
//cfusa:test REQ-LOAN-005
//cfusa:test REQ-LOAN-006
//cfusa:test REQ-LOAN-007
//cfusa:test REQ-LOAN-008
#include "unity.h"

#include <rcp/loan.h>

void setUp(void) {}
void tearDown(void) {}

/* ── acquire() ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-LOAN-001
static void test_acquire_returns_zeroed_buffer_of_requested_size(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *loan = rcp_loan_pool_acquire(pool, 16);
    size_t i;

    TEST_ASSERT_NOT_NULL(loan);
    TEST_ASSERT_EQUAL_UINT(16, loan->payload.len);
    for (i = 0; i < loan->payload.len; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, loan->payload.data[i]);
    }

    rcp_loan_release(loan);
    rcp_loan_pool_destroy(pool);
}

//cfusa:test REQ-LOAN-003
static void test_acquire_zero_size_returns_valid_empty_loan(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *loan = rcp_loan_pool_acquire(pool, 0);

    TEST_ASSERT_NOT_NULL(loan);
    TEST_ASSERT_EQUAL_UINT(0, loan->payload.len);

    rcp_loan_release(loan);
    rcp_loan_pool_destroy(pool);
}

/* ── Pool reuse ────────────────────────────────────────────────────────────── */

//cfusa:test REQ-LOAN-002
//cfusa:test REQ-LOAN-007
static void test_returned_buffer_is_reused_by_a_later_acquire(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *loan1 = rcp_loan_pool_acquire(pool, 64);
    uint8_t *first_data = loan1->payload.data;
    rcp_loan_t *loan2;

    rcp_loan_return(loan1); /* back to the pool, loan1 itself not freed yet */

    loan2 = rcp_loan_pool_acquire(pool, 32); /* same-or-smaller: eligible for reuse */
    TEST_ASSERT_NOT_NULL(loan2);
    TEST_ASSERT_EQUAL_PTR(first_data, loan2->payload.data);

    rcp_loan_release(loan1);
    rcp_loan_release(loan2);
    rcp_loan_pool_destroy(pool);
}

/* ── rcp_loan_return()/rcp_loan_release() ─────────────────────────────────── */

//cfusa:test REQ-LOAN-004
//cfusa:test REQ-LOAN-006
static void test_return_is_idempotent_and_does_not_free_the_loan(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *loan = rcp_loan_pool_acquire(pool, 8);

    rcp_loan_return(loan);
    rcp_loan_return(loan); /* idempotent: must not double-free or crash */

    /* loan itself is still a live allocation -- release exactly once now. */
    rcp_loan_release(loan);
    rcp_loan_pool_destroy(pool);
}

//cfusa:test REQ-LOAN-005
static void test_release_returns_buffer_and_frees_loan(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *loan1 = rcp_loan_pool_acquire(pool, 8);
    rcp_loan_t *loan2;

    rcp_loan_release(loan1); /* returns buffer to pool, frees loan1 */

    loan2 = rcp_loan_pool_acquire(pool, 8); /* pool still has the buffer available */
    TEST_ASSERT_NOT_NULL(loan2);

    rcp_loan_release(loan2);
    rcp_loan_pool_destroy(pool);
}

/* ── pool_destroy() ────────────────────────────────────────────────────────── */

//cfusa:test REQ-LOAN-008
static void test_pool_destroy_frees_every_returned_buffer(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *loans[8];
    int i;

    for (i = 0; i < 8; i++) {
        loans[i] = rcp_loan_pool_acquire(pool, (size_t)(16 * (i + 1)));
        rcp_loan_return(loans[i]);
    }
    for (i = 0; i < 8; i++) rcp_loan_release(loans[i]);

    rcp_loan_pool_destroy(pool); /* must not leak or crash (ASan-checked in CI) */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_acquire_returns_zeroed_buffer_of_requested_size);
    RUN_TEST(test_acquire_zero_size_returns_valid_empty_loan);
    RUN_TEST(test_returned_buffer_is_reused_by_a_later_acquire);
    RUN_TEST(test_return_is_idempotent_and_does_not_free_the_loan);
    RUN_TEST(test_release_returns_buffer_and_frees_loan);
    RUN_TEST(test_pool_destroy_frees_every_returned_buffer);

    return UNITY_END();
}
