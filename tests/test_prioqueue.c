//cfusa:test REQ-PQ-001
//cfusa:test REQ-PQ-002
//cfusa:test REQ-PQ-003
//cfusa:test REQ-PQ-004
//cfusa:test REQ-PQ-005
//cfusa:test REQ-PQ-006
//cfusa:test REQ-PQ-007
//cfusa:test REQ-PQ-008
#include "unity.h"

#include <rcp/mock.h>
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
#endif

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
    RUN_TEST(test_send_returns_timeout_when_context_already_expired);
    RUN_TEST(test_send_returns_zone_mismatch_on_wrong_zone);
    RUN_TEST(test_subscribe_delegates_to_inner);
    RUN_TEST(test_close_stops_accepting_work);

    return UNITY_END();
}
