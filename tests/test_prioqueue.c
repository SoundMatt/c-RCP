//cfusa:test REQ-PQ-001
//cfusa:test REQ-PQ-002
//cfusa:test REQ-PQ-003
//cfusa:test REQ-PQ-004
//cfusa:test REQ-PQ-005
//cfusa:test REQ-PQ-006
//cfusa:test REQ-PQ-007
//cfusa:test REQ-PQ-008
//cfusa:test REQ-PQ-009
#include "unity.h"

#include <rcp/clock.h>
#include "legacy_mock.h"
#include <rcp/prioqueue.h>
#include <rcp/rcp.h>

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE test_thread_t;
static test_thread_t test_thread_spawn(DWORD(WINAPI *fn)(void *), void *arg)
{
    return CreateThread(NULL, 0, fn, arg, 0, NULL);
}
static void test_thread_join(test_thread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
static int test_atomic_add(volatile int *v, int n)
{
    return (int)InterlockedExchangeAdd((volatile LONG *)v, (LONG)n) + n;
}
#else
#include <pthread.h>
typedef pthread_t test_thread_t;
static test_thread_t test_thread_spawn(void *(*fn)(void *), void *arg)
{
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void test_thread_join(test_thread_t t) { pthread_join(t, NULL); }
static int test_atomic_add(volatile int *v, int n) { return __atomic_add_fetch(v, n, __ATOMIC_ACQ_REL); }
#endif

static void test_sleep_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

/* ── Basic send ───────────────────────────────────────────────────────────── */

static void test_send_forwards_command_to_inner_and_returns_ok(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pq = rcp_prioqueue_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(pq, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_response_free(&resp);
    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

static void test_zone_returns_inner_zone(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *pq = rcp_prioqueue_controller_new(inner);

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(pq));

    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

/* ── Priority ordering ────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t *pq;
    rcp_priority_t      priority;
} send_args_t;

#if defined(_WIN32)
static DWORD WINAPI send_thread(void *arg)
#else
static void *send_thread(void *arg)
#endif
{
    send_args_t *a = (send_args_t *)arg;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.priority = a->priority;
    (void)rcp_controller_send(a->pq, &ctx, &cmd, &resp);
    rcp_response_free(&resp);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_prioritises_critical_high_normal_no_crash(void)
{
    /* Full ordering inspection would require dispatch-order visibility the
     * mock doesn't expose (matching cpp-RCP's own test_prioqueue.cpp
     * comment) -- this verifies concurrent sends of every priority
     * complete without error or deadlock. */
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pq = rcp_prioqueue_controller_new(inner);
    send_args_t args[3];
    test_thread_t threads[3];
    rcp_priority_t priorities[3] = {RCP_PRIORITY_NORMAL, RCP_PRIORITY_HIGH, RCP_PRIORITY_CRITICAL};
    int i;

    for (i = 0; i < 3; i++) {
        args[i].pq       = pq;
        args[i].priority = priorities[i];
        threads[i] = test_thread_spawn(send_thread, &args[i]);
    }
    for (i = 0; i < 3; i++) test_thread_join(threads[i]);

    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

typedef struct {
    volatile int first;
} slow_first_state_t;

static void slow_first_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    slow_first_state_t *st = (slow_first_state_t *)user_data;
    (void)cmd;
    /* The very first invocation holds the dispatch thread inside the
     * handler for a while, giving the other concurrently-sent entries
     * below time to actually pile up in the heap before it's popped again
     * -- forcing heap_pop() to invoke heap_sift_down()/heap_swap() over
     * more than one pending entry, rather than the degenerate
     * single-element case the fast (default) mock handler produces when
     * the dispatch thread drains each entry before the next is enqueued. */
    if (test_atomic_add(&st->first, 1) == 1) {
        test_sleep_ms(150);
    }
    out->status = RCP_RESPONSE_OK;
}

static void test_heap_reorders_multiple_pending_entries(void)
{
    slow_first_state_t state;
    rcp_controller_t *inner;
    rcp_controller_t *pq;
    send_args_t first_args;
    test_thread_t first_thread;
    /* Deadline already in the past (rcp_monotonic_ms() is far past 1ms of
     * uptime by the time any test runs) -- pq_ctrl_send() always pushes
     * the entry onto the heap unconditionally before it ever consults the
     * context, so each of these calls deterministically enqueues, then
     * returns ~immediately with RCP_ERR_TIMEOUT once the wait loop notices
     * the deadline has already elapsed. This builds a known 3-element heap
     * sequentially from a single thread -- no race on insertion order --
     * while the dispatch thread is still blocked processing the first
     * (slow) entry below, guaranteeing heap_pop() has more than one entry
     * to reorder via heap_sift_down()/heap_swap() when it resumes. */
    rcp_context_t expired = rcp_context_with_deadline_ms(1);
    rcp_priority_t priorities[3] = {RCP_PRIORITY_HIGH, RCP_PRIORITY_NORMAL, RCP_PRIORITY_CRITICAL};
    int i;

    state.first = 0;
    inner = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, slow_first_handler, &state);
    pq    = rcp_prioqueue_controller_new(inner);

    first_args.pq       = pq;
    first_args.priority = RCP_PRIORITY_NORMAL;
    first_thread = test_thread_spawn(send_thread, &first_args);
    test_sleep_ms(20); /* let the dispatch thread pick this one up and block in the handler */

    for (i = 0; i < 3; i++) {
        rcp_command_t cmd = {0};
        rcp_response_t resp = {0};
        cmd.zone     = RCP_ZONE_FRONT_LEFT;
        cmd.priority = priorities[i];
        (void)rcp_controller_send(pq, &expired, &cmd, &resp);
        rcp_response_free(&resp);
    }

    test_thread_join(first_thread);

    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

/* ── Context deadline ─────────────────────────────────────────────────────── */

static void test_send_returns_timeout_when_context_already_expired(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pq = rcp_prioqueue_controller_new(inner);
    /* A deadline already in the past. */
    rcp_context_t ctx = rcp_context_with_deadline_ms(1);
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_controller_send(pq, &ctx, &cmd, &resp));

    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

/* ── Zone mismatch ────────────────────────────────────────────────────────── */

static void test_send_returns_zone_mismatch_on_wrong_zone(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pq = rcp_prioqueue_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_REAR_LEFT; /* mismatched */
    TEST_ASSERT_EQUAL(RCP_ERR_ZONE_MISMATCH, rcp_controller_send(pq, &ctx, &cmd, &resp));

    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

/* ── Subscribe ────────────────────────────────────────────────────────────── */

static void test_subscribe_delegates_to_inner(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pq = rcp_prioqueue_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(pq, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_status_channel_close(ch);
    rcp_status_channel_release(ch);
    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

/* ── Close ────────────────────────────────────────────────────────────────── */

static void test_close_stops_accepting_work(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pq = rcp_prioqueue_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(pq));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(pq, &ctx, &cmd, &resp));

    rcp_controller_release(pq);
    rcp_controller_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_forwards_command_to_inner_and_returns_ok);
    RUN_TEST(test_zone_returns_inner_zone);
    RUN_TEST(test_prioritises_critical_high_normal_no_crash);
    RUN_TEST(test_heap_reorders_multiple_pending_entries);
    RUN_TEST(test_send_returns_timeout_when_context_already_expired);
    RUN_TEST(test_send_returns_zone_mismatch_on_wrong_zone);
    RUN_TEST(test_subscribe_delegates_to_inner);
    RUN_TEST(test_close_stops_accepting_work);

    return UNITY_END();
}
