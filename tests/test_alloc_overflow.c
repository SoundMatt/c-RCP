/* SPDX-License-Identifier: MPL-2.0 */
/* Direct unit tests for src/alloc_overflow.h (CFUSA-CY005 / CERT-C INT30-C
 * defense-in-depth guard shared by every growable-array allocation call
 * site in this codebase -- issue #523). Internal, not-installed header,
 * same testing convention as test_platform.c for src/platform.h: reaches
 * into src/ directly since there is no public rcp header re-export. */
#include "unity.h"

#include "alloc_overflow.h"

#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

/* ── rcp_alloc_size_would_overflow ────────────────────────────────────────── */

static void test_would_overflow_false_for_ordinary_sizes(void)
{
    TEST_ASSERT_FALSE(rcp_alloc_size_would_overflow(0, 1));
    TEST_ASSERT_FALSE(rcp_alloc_size_would_overflow(1, 1));
    TEST_ASSERT_FALSE(rcp_alloc_size_would_overflow(1000, 64));
    TEST_ASSERT_FALSE(rcp_alloc_size_would_overflow(SIZE_MAX / 8, 8));
}

static void test_would_overflow_true_at_exact_boundary(void)
{
    /* (SIZE_MAX / 8 + 1) * 8 overflows by exactly one unit past SIZE_MAX. */
    TEST_ASSERT_TRUE(rcp_alloc_size_would_overflow((SIZE_MAX / 8) + 1, 8));
}

static void test_would_overflow_true_for_huge_n(void)
{
    TEST_ASSERT_TRUE(rcp_alloc_size_would_overflow(SIZE_MAX, 2));
    TEST_ASSERT_TRUE(rcp_alloc_size_would_overflow(SIZE_MAX, SIZE_MAX));
}

static void test_would_overflow_false_for_elem_size_one(void)
{
    /* elem_size == 1 can never overflow for any representable n. */
    TEST_ASSERT_FALSE(rcp_alloc_size_would_overflow(SIZE_MAX, 1));
}

/* ── rcp_alloc_checked_size ───────────────────────────────────────────────── */

static void test_checked_size_returns_product_when_safe(void)
{
    TEST_ASSERT_EQUAL_UINT(0, rcp_alloc_checked_size(0, 16));
    TEST_ASSERT_EQUAL_UINT(160, rcp_alloc_checked_size(10, 16));
    TEST_ASSERT_EQUAL_UINT(SIZE_MAX, rcp_alloc_checked_size(SIZE_MAX, 1));
}

static void test_checked_size_returns_zero_on_overflow(void)
{
    TEST_ASSERT_EQUAL_UINT(0, rcp_alloc_checked_size((SIZE_MAX / 8) + 1, 8));
    TEST_ASSERT_EQUAL_UINT(0, rcp_alloc_checked_size(SIZE_MAX, SIZE_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_would_overflow_false_for_ordinary_sizes);
    RUN_TEST(test_would_overflow_true_at_exact_boundary);
    RUN_TEST(test_would_overflow_true_for_huge_n);
    RUN_TEST(test_would_overflow_false_for_elem_size_one);
    RUN_TEST(test_checked_size_returns_product_when_safe);
    RUN_TEST(test_checked_size_returns_zero_on_overflow);
    return UNITY_END();
}
