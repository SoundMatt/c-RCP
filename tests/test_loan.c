/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-LOAN-001
//cfusa:test REQ-LOAN-002
//cfusa:test REQ-LOAN-003
//cfusa:test REQ-LOAN-004
//cfusa:test REQ-LOAN-005
//cfusa:test REQ-LOAN-006
//cfusa:test REQ-LOAN-007
//cfusa:test REQ-LOAN-008
//cfusa:test REQ-LOAN-009
#include "unity.h"

#include <rcp/alloc.h>
#include <rcp/loan.h>

void setUp(void) {}
void tearDown(void) { rcp_alloc_reset_hooks(); } /* never leak a fault-injection hook across tests */

/* ── acquire() ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-LOAN-001
//cfusa:test REQ-LOAN-009
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

/* ── Allocation-failure paths (issue #520 category 2: rcp_loan_pool_acquire()'s
 * and pool_append()'s malloc-failure branches were entirely unexercised even
 * though this project's own alloc.h fault-injection harness -- REQ-SEQ-002's
 * first opt-in caller, request_sequencer.c -- exists precisely to make paths
 * like these testable without a real, unportable OOM condition) ──────────── */

static void *always_fails_malloc(size_t size) { (void)size; return NULL; }
static void *always_fails_calloc(size_t nmemb, size_t size)
{
    (void)nmemb; (void)size;
    return NULL;
}
static void *always_fails_realloc(void *ptr, size_t size)
{
    (void)ptr; (void)size;
    return NULL;
}

/* rcp_loan_pool_acquire()'s `if (!loan || !release_ctx)` cleanup, `!loan`
 * side: force the loan struct's own rcp_calloc() to fail. The pool already
 * holds a reusable entry so the earlier `data` rcp_calloc() this same hook
 * would otherwise also fail is never reached (the reuse path skips it). */
static void test_acquire_returns_null_when_loan_struct_allocation_fails(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *seed = rcp_loan_pool_acquire(pool, 8);
    rcp_alloc_hooks_t hooks = {0};
    rcp_loan_t *loan;

    rcp_loan_return(seed); /* pool now holds one reusable entry */

    hooks.calloc_fn = always_fails_calloc;
    rcp_alloc_set_hooks(&hooks);

    loan = rcp_loan_pool_acquire(pool, 4); /* reuses seed's buffer; loan's own calloc fails */
    TEST_ASSERT_NULL(loan);

    rcp_alloc_reset_hooks();
    rcp_loan_release(seed);
    rcp_loan_pool_destroy(pool);
}

/* Same cleanup path, `!release_ctx` side: loan's own rcp_calloc() succeeds,
 * release_ctx's rcp_malloc() fails -- the already-allocated loan must be
 * freed too, not leaked (ASan-checked). */
static void test_acquire_returns_null_when_release_ctx_allocation_fails(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *seed = rcp_loan_pool_acquire(pool, 8);
    rcp_alloc_hooks_t hooks = {0};
    rcp_loan_t *loan;

    rcp_loan_return(seed);

    hooks.malloc_fn = always_fails_malloc;
    rcp_alloc_set_hooks(&hooks);

    loan = rcp_loan_pool_acquire(pool, 4);
    TEST_ASSERT_NULL(loan);

    rcp_alloc_reset_hooks();
    rcp_loan_release(seed);
    rcp_loan_pool_destroy(pool);
}

/* pool_append()'s own growth-allocation failure: rcp_loan_return() on an
 * empty pool (entries_cap == 0) always tries to grow it, so failing
 * rcp_realloc() here forces loan_release_to_pool()'s "pool bookkeeping
 * allocation failed, free the buffer outright instead of leaking it"
 * fallback (src/loan.c) -- the buffer must still be freed correctly (not
 * leaked, not double-freed), just not returned to the pool for reuse. */
static void test_return_frees_buffer_outright_when_pool_growth_allocation_fails(void)
{
    rcp_loan_pool_t *pool = rcp_loan_pool_new();
    rcp_loan_t *loan = rcp_loan_pool_acquire(pool, 8);
    rcp_alloc_hooks_t hooks = {0};
    rcp_loan_t *reacquired;

    hooks.realloc_fn = always_fails_realloc;
    rcp_alloc_set_hooks(&hooks);

    rcp_loan_return(loan); /* pool_append()'s growth realloc fails; buffer freed, not pooled */

    rcp_alloc_reset_hooks();

    /* Must not crash or double-free (ASan-checked in CI) and the pool must
     * still be fully usable afterwards -- pool_append() failing must not
     * corrupt entries_len/entries_cap bookkeeping for later real appends. */
    reacquired = rcp_loan_pool_acquire(pool, 8);
    TEST_ASSERT_NOT_NULL(reacquired);

    rcp_loan_release(loan);
    rcp_loan_release(reacquired);
    rcp_loan_pool_destroy(pool);
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
    RUN_TEST(test_acquire_returns_null_when_loan_struct_allocation_fails);
    RUN_TEST(test_acquire_returns_null_when_release_ctx_allocation_fails);
    RUN_TEST(test_return_frees_buffer_outright_when_pool_growth_allocation_fails);

    return UNITY_END();
}
