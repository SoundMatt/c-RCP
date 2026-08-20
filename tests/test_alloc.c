/* SPDX-License-Identifier: MPL-2.0 */
#include "unity.h"

#include <rcp/alloc.h>
#include <rcp/e2e.h>

#include <stdlib.h>

/* setUp()/tearDown() must themselves tolerate running while a previous
 * test left the table locked (they must not silently no-op via the very
 * mechanism under test) -- unlock unconditionally before resetting, or
 * a locked table from one test would poison every test that runs after
 * it in this binary. rcp_alloc_unlock_hooks() is itself a safe no-op
 * when not locked. */
void setUp(void) { rcp_alloc_unlock_hooks(); rcp_alloc_reset_hooks(); }
void tearDown(void) { rcp_alloc_unlock_hooks(); rcp_alloc_reset_hooks(); } /* never leak a hook or a lock across tests */

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

/* ── Locking [c-RCP-23b], issue #600 (SEOOC_BOUNDARY.md §2 AoU-8,
 * FREEDOM_FROM_INTERFERENCE.md §2) ────────────────────────────────────────── */

static void *hooks_a_malloc(size_t size) { g_malloc_calls++; return malloc(size); }
static void *hooks_b_malloc(size_t size) { (void)size; return NULL; /* sentinel: never actually called */ }

//cfusa:test REQ-ALLOC-007
//cfusa:test REQ-ALLOC-010
static void test_lock_rejects_a_subsequent_set_hooks_call(void)
{
    rcp_alloc_hooks_t hooks_a = {0};
    rcp_alloc_hooks_t hooks_b = {0};
    bool               applied;
    void              *p;

    g_malloc_calls = 0;
    hooks_a.malloc_fn = hooks_a_malloc;
    TEST_ASSERT_TRUE(rcp_alloc_set_hooks(&hooks_a));

    TEST_ASSERT_TRUE(rcp_alloc_lock_hooks());

    hooks_b.malloc_fn = hooks_b_malloc;
    applied = rcp_alloc_set_hooks(&hooks_b);
    TEST_ASSERT_FALSE(applied);

    /* Hooks A must still be the actually-active hooks -- verified by
     * allocating and confirming A's own counting behavior fired, not
     * just trusting the false return value. */
    p = rcp_malloc(4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(1, g_malloc_calls);
    rcp_alloc_unlock_hooks();
    rcp_free(p);
}

//cfusa:test REQ-ALLOC-011
static void test_lock_rejects_reset_hooks_too(void)
{
    rcp_alloc_hooks_t hooks_a = {0};
    bool               applied;
    void              *p;

    g_malloc_calls = 0;
    hooks_a.malloc_fn = hooks_a_malloc;
    TEST_ASSERT_TRUE(rcp_alloc_set_hooks(&hooks_a));

    TEST_ASSERT_TRUE(rcp_alloc_lock_hooks());

    applied = rcp_alloc_reset_hooks();
    TEST_ASSERT_FALSE(applied);

    /* Hooks A must still be active -- reset() was rejected, not merely
     * reported as rejected. */
    p = rcp_malloc(4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(1, g_malloc_calls);
    rcp_alloc_unlock_hooks();
    rcp_free(p);
}

//cfusa:test REQ-ALLOC-007
static void test_lock_hooks_is_idempotent(void)
{
    TEST_ASSERT_TRUE(rcp_alloc_lock_hooks());   /* first call: newly acquired */
    TEST_ASSERT_FALSE(rcp_alloc_lock_hooks());  /* second call: already locked */
    rcp_alloc_unlock_hooks();
}

//cfusa:test REQ-ALLOC-008
static void test_unlock_hooks_releases_the_lock(void)
{
    rcp_alloc_hooks_t hooks_b = {0};
    bool               applied;
    void              *p;

    TEST_ASSERT_TRUE(rcp_alloc_lock_hooks());
    TEST_ASSERT_TRUE(rcp_alloc_unlock_hooks());

    g_malloc_calls = 0;
    hooks_b.malloc_fn = hooks_a_malloc;
    applied = rcp_alloc_set_hooks(&hooks_b);
    TEST_ASSERT_TRUE(applied);

    p = rcp_malloc(4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(1, g_malloc_calls);
    rcp_free(p);
}

//cfusa:test REQ-ALLOC-008
static void test_unlock_hooks_on_already_unlocked_table_is_a_no_op(void)
{
    TEST_ASSERT_FALSE(rcp_alloc_hooks_locked());
    TEST_ASSERT_FALSE(rcp_alloc_unlock_hooks()); /* nothing to release */
    TEST_ASSERT_FALSE(rcp_alloc_hooks_locked());
}

//cfusa:test REQ-ALLOC-009
static void test_hooks_locked_reflects_state_across_lock_unlock_sequence(void)
{
    TEST_ASSERT_FALSE(rcp_alloc_hooks_locked());

    TEST_ASSERT_TRUE(rcp_alloc_lock_hooks());
    TEST_ASSERT_TRUE(rcp_alloc_hooks_locked());

    TEST_ASSERT_FALSE(rcp_alloc_lock_hooks()); /* already locked */
    TEST_ASSERT_TRUE(rcp_alloc_hooks_locked()); /* still locked */

    TEST_ASSERT_TRUE(rcp_alloc_unlock_hooks());
    TEST_ASSERT_FALSE(rcp_alloc_hooks_locked());

    TEST_ASSERT_FALSE(rcp_alloc_unlock_hooks()); /* already unlocked */
    TEST_ASSERT_FALSE(rcp_alloc_hooks_locked());
}

/* ── End-to-end proof: e2e.c's own ASIL-B safe-point path (rcp_e2e_wrap())
 * keeps using the LOCKED hooks even though nothing in e2e.c itself
 * changed -- the actual thing AoU-8/issue #600 needed closed, not just
 * a unit test of alloc.c in isolation. Frame-construction idiom copied
 * from test_e2e.c's own make_test_acf_frame(). */

static void make_test_acf_frame(uint8_t *out, size_t out_len)
{
    size_t i;
    TEST_ASSERT_TRUE(out_len >= 3);
    out[0] = 0x1Cu; /* acf_msg_type=ABB, length MSB=0 */
    out[1] = 0x00u; /* placeholder acf_msg_length, low 8 bits */
    out[2] = (uint8_t)(out_len - 8u);
    for (i = 3; i < out_len; i++) out[i] = (uint8_t)(i & 0xFFu);
}

//cfusa:test REQ-ALLOC-007
//cfusa:test REQ-ALLOC-010
static void test_e2e_wrap_still_uses_locked_hooks_with_no_change_to_e2e_c(void)
{
    rcp_alloc_hooks_t hooks_locked_in = {0};
    rcp_alloc_hooks_t attempted_override = {0};
    uint8_t            acf_frame[8];
    rcp_bytes_t        wrapped;
    bool               applied;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    g_malloc_calls = 0;
    hooks_locked_in.malloc_fn = hooks_a_malloc;
    TEST_ASSERT_TRUE(rcp_alloc_set_hooks(&hooks_locked_in));
    TEST_ASSERT_TRUE(rcp_alloc_lock_hooks());

    /* An arbitrary QM-rated caller attempts to redirect the allocator
     * e2e.c's own safe-point path depends on -- rejected. */
    attempted_override.malloc_fn = hooks_b_malloc;
    applied = rcp_alloc_set_hooks(&attempted_override);
    TEST_ASSERT_FALSE(applied);

    /* rcp_e2e_wrap() itself is untouched by this issue -- it still just
     * calls rcp_malloc(). Prove the LOCKED hooks (hooks_locked_in), not
     * the attempted override, are what it actually observes. */
    wrapped = rcp_e2e_wrap(0x05u, 0x00u, false, 1u, 1u, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_TRUE(g_malloc_calls > 0); /* hooks_a_malloc, not hooks_b_malloc, actually ran */

    rcp_alloc_unlock_hooks();
    rcp_bytes_free(&wrapped);
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

    RUN_TEST(test_lock_rejects_a_subsequent_set_hooks_call);
    RUN_TEST(test_lock_rejects_reset_hooks_too);
    RUN_TEST(test_lock_hooks_is_idempotent);
    RUN_TEST(test_unlock_hooks_releases_the_lock);
    RUN_TEST(test_unlock_hooks_on_already_unlocked_table_is_a_no_op);
    RUN_TEST(test_hooks_locked_reflects_state_across_lock_unlock_sequence);
    RUN_TEST(test_e2e_wrap_still_uses_locked_hooks_with_no_change_to_e2e_c);

    return UNITY_END();
}
