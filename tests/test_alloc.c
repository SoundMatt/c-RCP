/* SPDX-License-Identifier: MPL-2.0 */
#include "unity.h"

#include <rcp/alloc.h>

#include <stdlib.h>

void setUp(void) { rcp_alloc_reset_hooks(); }
void tearDown(void) { rcp_alloc_reset_hooks(); } /* never leak a hook across tests */

/* ── Default passthrough (no hooks installed) ────────────────────────────── */

//cfusa:test REQ-ALLOC-003
static void test_malloc_default_behaves_like_libc_malloc(void)
{
    void *p = rcp_malloc(16);
    TEST_ASSERT_NOT_NULL(p);
    rcp_free(p);
}

//cfusa:test REQ-ALLOC-004
static void test_calloc_default_zero_initializes(void)
{
    uint8_t *p = (uint8_t *)rcp_calloc(8, 1);
    size_t   i;

    TEST_ASSERT_NOT_NULL(p);
    for (i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    rcp_free(p);
}

//cfusa:test REQ-ALLOC-006
static void test_realloc_default_behaves_like_libc_realloc(void)
{
    uint8_t *p = (uint8_t *)rcp_malloc(4);
    uint8_t *grown;

    TEST_ASSERT_NOT_NULL(p);
    p[0] = 0xAA;
    grown = (uint8_t *)rcp_realloc(p, 64);
    TEST_ASSERT_NOT_NULL(grown);
    TEST_ASSERT_EQUAL_UINT8(0xAA, grown[0]); /* original content preserved */
    rcp_free(grown);
}

//cfusa:test REQ-ALLOC-005
static void test_free_null_is_a_safe_no_op(void)
{
    rcp_free(NULL); /* must not crash */
    TEST_PASS();
}

/* ── Hook installation ───────────────────────────────────────────────────── */

static int  g_malloc_calls;
static int  g_calloc_calls;
static int  g_realloc_calls;
static int  g_free_calls;

static void *counting_malloc(size_t size) { g_malloc_calls++; return malloc(size); }
static void *counting_calloc(size_t nmemb, size_t size) { g_calloc_calls++; return calloc(nmemb, size); }
static void *counting_realloc(void *ptr, size_t size) { g_realloc_calls++; return realloc(ptr, size); }
static void  counting_dealloc(void *ptr) { g_free_calls++; free(ptr); }

//cfusa:test REQ-ALLOC-001
//cfusa:test REQ-ALLOC-003
//cfusa:test REQ-ALLOC-004
//cfusa:test REQ-ALLOC-005
//cfusa:test REQ-ALLOC-006
static void test_set_hooks_routes_every_call_through_the_installed_hook(void)
{
    rcp_alloc_hooks_t hooks = {0};
    void              *p_malloc;
    void              *p_calloc;
    void              *p_realloc;

    g_malloc_calls  = 0;
    g_calloc_calls  = 0;
    g_realloc_calls = 0;
    g_free_calls    = 0;
    hooks.malloc_fn  = counting_malloc;
    hooks.calloc_fn  = counting_calloc;
    hooks.realloc_fn = counting_realloc;
    hooks.free_fn    = counting_dealloc;
    rcp_alloc_set_hooks(&hooks);

    p_malloc = rcp_malloc(4);
    TEST_ASSERT_NOT_NULL(p_malloc);
    TEST_ASSERT_EQUAL_INT(1, g_malloc_calls);

    p_realloc = rcp_realloc(p_malloc, 64);
    TEST_ASSERT_NOT_NULL(p_realloc);
    TEST_ASSERT_EQUAL_INT(1, g_realloc_calls);
    rcp_free(p_realloc);
    TEST_ASSERT_EQUAL_INT(1, g_free_calls);

    p_calloc = rcp_calloc(2, 2);
    TEST_ASSERT_NOT_NULL(p_calloc);
    TEST_ASSERT_EQUAL_INT(1, g_calloc_calls);
    rcp_free(p_calloc);
    TEST_ASSERT_EQUAL_INT(2, g_free_calls);
}

/* A hooks value with only SOME members set falls back to libc for the
 * others -- a caller intercepting malloc() alone need not reimplement
 * calloc()/free() just to install one hook. */
//cfusa:test REQ-ALLOC-001
//cfusa:test REQ-ALLOC-003
//cfusa:test REQ-ALLOC-004
//cfusa:test REQ-ALLOC-005
static void test_set_hooks_partial_falls_back_to_libc_for_unset_members(void)
{
    rcp_alloc_hooks_t hooks = {0};
    void              *p_malloc;
    void              *p_calloc;

    g_malloc_calls = 0;
    hooks.malloc_fn = counting_malloc; /* calloc_fn/free_fn left NULL */
    rcp_alloc_set_hooks(&hooks);

    p_malloc = rcp_malloc(4);
    TEST_ASSERT_NOT_NULL(p_malloc);
    TEST_ASSERT_EQUAL_INT(1, g_malloc_calls);
    rcp_free(p_malloc); /* falls back to the libc default -- no counting hook installed for this call */

    p_calloc = rcp_calloc(1, 4); /* falls back to libc calloc() */
    TEST_ASSERT_NOT_NULL(p_calloc);
    rcp_free(p_calloc);
}

/* rcp_alloc_set_hooks(NULL) is documented as equivalent to
 * rcp_alloc_reset_hooks(). */
//cfusa:test REQ-ALLOC-001
static void test_set_hooks_null_resets_to_default(void)
{
    rcp_alloc_hooks_t hooks = {0};
    void              *p;

    g_malloc_calls = 0;
    hooks.malloc_fn = counting_malloc;
    rcp_alloc_set_hooks(&hooks);
    rcp_alloc_set_hooks(NULL);

    p = rcp_malloc(4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(0, g_malloc_calls); /* hook never re-invoked */
    rcp_free(p);
}

/* ── Fault injection: the whole point of this module ─────────────────────── */

static void *failing_malloc(size_t size) { (void)size; return NULL; }
static void *failing_calloc(size_t nmemb, size_t size) { (void)nmemb; (void)size; return NULL; }
static void *failing_realloc(void *ptr, size_t size) { (void)ptr; (void)size; return NULL; }

//cfusa:test REQ-ALLOC-003
static void test_installed_malloc_hook_can_simulate_allocation_failure(void)
{
    rcp_alloc_hooks_t hooks = {0};

    hooks.malloc_fn = failing_malloc;
    rcp_alloc_set_hooks(&hooks);

    TEST_ASSERT_NULL(rcp_malloc(16));
}

//cfusa:test REQ-ALLOC-004
static void test_installed_calloc_hook_can_simulate_allocation_failure(void)
{
    rcp_alloc_hooks_t hooks = {0};

    hooks.calloc_fn = failing_calloc;
    rcp_alloc_set_hooks(&hooks);

    TEST_ASSERT_NULL(rcp_calloc(4, 4));
}

/* This is REQ-FRAG-016's own fault-injection technique -- see
 * test_reassembler_alloc_failure_is_distinct_and_preserves_state() in
 * test_fragment.c. */
//cfusa:test REQ-ALLOC-006
static void test_installed_realloc_hook_can_simulate_allocation_failure(void)
{
    rcp_alloc_hooks_t hooks = {0};

    hooks.realloc_fn = failing_realloc;
    rcp_alloc_set_hooks(&hooks);

    TEST_ASSERT_NULL(rcp_realloc((void *)&hooks, 16)); /* ptr never dereferenced on failure */
}

/* ── Reset ────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-ALLOC-002
static void test_reset_hooks_restores_the_libc_passthrough(void)
{
    rcp_alloc_hooks_t hooks = {0};
    void              *p;

    hooks.malloc_fn = failing_malloc;
    rcp_alloc_set_hooks(&hooks);
    TEST_ASSERT_NULL(rcp_malloc(16));

    rcp_alloc_reset_hooks();
    p = rcp_malloc(16);
    TEST_ASSERT_NOT_NULL(p);
    rcp_free(p);
}

//cfusa:test REQ-ALLOC-002
static void test_reset_hooks_is_a_safe_no_op_when_nothing_was_installed(void)
{
    void *p;

    rcp_alloc_reset_hooks();
    rcp_alloc_reset_hooks();
    p = rcp_malloc(1);
    TEST_ASSERT_NOT_NULL(p);
    rcp_free(p);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_malloc_default_behaves_like_libc_malloc);
    RUN_TEST(test_calloc_default_zero_initializes);
    RUN_TEST(test_realloc_default_behaves_like_libc_realloc);
    RUN_TEST(test_free_null_is_a_safe_no_op);

    RUN_TEST(test_set_hooks_routes_every_call_through_the_installed_hook);
    RUN_TEST(test_set_hooks_partial_falls_back_to_libc_for_unset_members);
    RUN_TEST(test_set_hooks_null_resets_to_default);

    RUN_TEST(test_installed_malloc_hook_can_simulate_allocation_failure);
    RUN_TEST(test_installed_calloc_hook_can_simulate_allocation_failure);
    RUN_TEST(test_installed_realloc_hook_can_simulate_allocation_failure);

    RUN_TEST(test_reset_hooks_restores_the_libc_passthrough);
    RUN_TEST(test_reset_hooks_is_a_safe_no_op_when_nothing_was_installed);

    return UNITY_END();
}
