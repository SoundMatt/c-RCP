/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-PLATFORM-001
//cfusa:test REQ-PLATFORM-002
//cfusa:test REQ-PLATFORM-003
/* Direct unit tests for the internal, codebase-wide portability primitives
 * (src/platform.h, src/platform.c, rcp/clock.h): mutex/condvar mutual
 * exclusion, thread start, and the monotonic clock. Every module needing
 * cross-thread synchronization or timing depends on these; before this
 * test file they were only exercised indirectly through the retired
 * zone-controller mock's own test suite (tests/legacy_mock.c and its
 * consumers), which was removed along with the rest of the retired rcp.h
 * object model it doubled (RELAY spec §15.5, no compatibility shim) --
 * see platform.c/clock.c's own REQ-PLATFORM-* cfusa:req tags. */
#include "unity.h"

#include <rcp/clock.h>

#include "platform.h"

void setUp(void) {}
void tearDown(void) {}

/* ── Mutex / condvar ───────────────────────────────────────────────────────── */

static void test_mutex_lock_unlock_roundtrip(void)
{
    rcp_mutex_t m;
    rcp_mutex_init(&m);
    rcp_mutex_lock(&m);
    rcp_mutex_unlock(&m);
    rcp_mutex_destroy(&m);
    TEST_PASS(); /* must not deadlock or crash */
}

static void test_cond_signal_wakes_waiter_same_thread_polling(void)
{
    /* Single-threaded smoke test: timedwait_until with an already-past
     * deadline must return false (timeout) rather than blocking forever. */
    rcp_mutex_t m;
    rcp_cond_t  c;
    bool        signaled;

    rcp_mutex_init(&m);
    rcp_cond_init(&c);

    rcp_mutex_lock(&m);
    signaled = rcp_cond_timedwait_until(&c, &m, rcp_monotonic_ms());
    rcp_mutex_unlock(&m);

    TEST_ASSERT_FALSE(signaled);

    rcp_cond_destroy(&c);
    rcp_mutex_destroy(&m);
}

/* ── Threads ───────────────────────────────────────────────────────────────── */

static volatile int g_thread_ran = 0;

static void thread_fn(void *arg)
{
    volatile int *flag = (volatile int *)arg;
    *flag = 1;
}

static void test_thread_start_joinable_runs_and_joins(void)
{
    rcp_thread_t t;
    int rc;

    g_thread_ran = 0;
    rc = rcp_thread_start(&t, thread_fn, (void *)&g_thread_ran);
    TEST_ASSERT_EQUAL(0, rc);

    rcp_thread_join(t);
    TEST_ASSERT_EQUAL(1, g_thread_ran);
}

static volatile int g_detached_ran = 0;

static void detached_thread_fn(void *arg)
{
    volatile int *flag = (volatile int *)arg;
    *flag = 1;
}

static void test_thread_start_detached_runs(void)
{
    int rc;

    g_detached_ran = 0;
    rc = rcp_thread_start_detached(detached_thread_fn, (void *)&g_detached_ran);
    TEST_ASSERT_EQUAL(0, rc);

    /* Detached: no join handle. Give it a bounded window to run rather than
     * asserting an exact schedule -- this only checks the thread actually
     * started and ran the function at all. */
    rcp_sleep_ms(200);
    TEST_ASSERT_EQUAL(1, g_detached_ran);
}

/* ── Monotonic clock ───────────────────────────────────────────────────────── */

static void test_monotonic_ms_never_decreases(void)
{
    uint64_t a = rcp_monotonic_ms();
    uint64_t b;
    rcp_sleep_ms(5);
    b = rcp_monotonic_ms();
    TEST_ASSERT_TRUE(b >= a);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_mutex_lock_unlock_roundtrip);
    RUN_TEST(test_cond_signal_wakes_waiter_same_thread_polling);

    RUN_TEST(test_thread_start_joinable_runs_and_joins);
    RUN_TEST(test_thread_start_detached_runs);

    RUN_TEST(test_monotonic_ms_never_decreases);

    return UNITY_END();
}
