//cfusa:test REQ-CTRL-001
//cfusa:test REQ-CTRL-002
//cfusa:test REQ-CTRL-003
//cfusa:test REQ-CTRL-004
//cfusa:test REQ-CTRL-005
//cfusa:test REQ-CTRL-006
//cfusa:test REQ-CTRL-007
//cfusa:test REQ-CTRL-008
//cfusa:test REQ-CTRL-009
//cfusa:test REQ-CTRL-010
//cfusa:test REQ-CTRL-011
//cfusa:test REQ-CTRL-012
//cfusa:test REQ-CTRL-013
//cfusa:test REQ-CTRL-014
//cfusa:test REQ-CTRL-015
//cfusa:test REQ-CTRL-016
//cfusa:test REQ-CTRL-017
//cfusa:test REQ-CTRL-018
//cfusa:test REQ-CTRL-019
//cfusa:test REQ-CTRL-020
//cfusa:test REQ-CTRL-021
//cfusa:test REQ-CTRL-022
//cfusa:test REQ-CTRL-023
//cfusa:test REQ-CTRL-024
//cfusa:test REQ-CTRL-025
//cfusa:test REQ-CTRL-026
//cfusa:test REQ-CTRL-027
//cfusa:test REQ-REG-001
//cfusa:test REQ-REG-002
//cfusa:test REQ-REG-003
//cfusa:test REQ-REG-004
//cfusa:test REQ-REG-005
//cfusa:test REQ-REG-006
//cfusa:test REQ-REG-007
//cfusa:test REQ-REG-008
//cfusa:test REQ-REG-009
//cfusa:test REQ-REG-010
//cfusa:test REQ-REG-011
//cfusa:test REQ-REG-012
//cfusa:test REQ-REG-013
//cfusa:test REQ-RESP-001
//cfusa:test REQ-RESP-002
//cfusa:test REQ-STAT-001
//cfusa:test REQ-STAT-002
//cfusa:test REQ-STAT-003
//cfusa:test REQ-STAT-004
//cfusa:test REQ-ERR-011
#include "unity.h"

#include <rcp/mock.h>
#include <rcp/rcp.h>

#include <string.h>

/* Minimal cross-platform thread spawn for the two concurrency tests below
 * (REQ-CTRL-018/019). Test-only: the library itself never exposes threading
 * primitives in its public API, so this is self-contained rather than
 * reaching into the library's internal platform shim. */
#if defined(_WIN32)
#include <windows.h>
typedef HANDLE test_thread_t;
#define TEST_THREAD_CALL WINAPI
typedef DWORD test_thread_ret_t;
static test_thread_t test_thread_spawn(test_thread_ret_t(TEST_THREAD_CALL *fn)(LPVOID), void *arg)
{
    return CreateThread(NULL, 0, fn, arg, 0, NULL);
}
static void test_thread_join(test_thread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
static int test_atomic_add(volatile LONG *v, LONG n) { return (int)InterlockedExchangeAdd(v, n) + (int)n; }
#else
#include <pthread.h>
typedef pthread_t test_thread_t;
#define TEST_THREAD_CALL
typedef void *test_thread_ret_t;
static test_thread_t test_thread_spawn(test_thread_ret_t(TEST_THREAD_CALL *fn)(void *), void *arg)
{
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void test_thread_join(test_thread_t t) { pthread_join(t, NULL); }
static int test_atomic_add(volatile int *v, int n) { return __atomic_add_fetch(v, n, __ATOMIC_ACQ_REL); }
#endif

void setUp(void) {}
void tearDown(void) {}

/* ── Registry pre-population ──────────────────────────────────────────────── */

static void test_new_registry_prepopulates_zones(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    const rcp_zone_t zones[] = {
        RCP_ZONE_FRONT_LEFT, RCP_ZONE_FRONT_RIGHT,
        RCP_ZONE_REAR_LEFT,  RCP_ZONE_REAR_RIGHT,
        RCP_ZONE_CENTRAL,
    };
    size_t i;

    for (i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
        rcp_controller_t *ctrl = NULL;
        TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, zones[i], &ctrl));
        TEST_ASSERT_EQUAL(zones[i], rcp_controller_zone(ctrl));
        rcp_controller_release(ctrl);
    }
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_register_duplicate_zone_already_exists(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *dup = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);

    TEST_ASSERT_EQUAL(RCP_ERR_ALREADY_EXISTS, rcp_registry_register(reg, dup));

    rcp_controller_release(dup);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_deregister_removes_and_closes(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_lookup_absent_zone_not_found(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_registry_close_idempotent(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));
    rcp_registry_destroy(reg);
}

static void test_registry_controllers_returns_all(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *ctrls[8] = {0};
    size_t n = rcp_registry_controllers(reg, ctrls, 8);
    size_t i;

    TEST_ASSERT_EQUAL_UINT(5, n);
    for (i = 0; i < n; i++) rcp_controller_release(ctrls[i]);

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_register_after_close_is_closed(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *extra;

    rcp_registry_close(reg);
    extra = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_registry_register(reg, extra));

    rcp_controller_release(extra);
    rcp_registry_destroy(reg);
}

static void test_deregister_never_registered_not_found(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_registered_controller_immediately_lookupable(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *nc = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *got = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, nc));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &got));
    TEST_ASSERT_EQUAL_PTR(nc, got);

    rcp_controller_release(got);
    rcp_controller_release(nc);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_registry_close_closes_all_controllers(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *ctrl = NULL;
    rcp_response_t out = {0};
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl));
    rcp_registry_close(reg);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(ctrl, &ctx, &cmd, &out));

    rcp_controller_release(ctrl);
    rcp_registry_destroy(reg);
}

static void test_lookup_on_closed_registry_is_closed(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_controller_t *ctrl = NULL;

    rcp_registry_close(reg);
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl));

    rcp_registry_destroy(reg);
}

static void test_deregister_already_deregistered_not_found(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

/* ── Controller::send ──────────────────────────────────────────────────────── */

static void test_send_no_handler_returns_ok(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 1;
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, out.status);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static bool g_handler_called;

static void handler_error_verbatim(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    (void)user_data;
    g_handler_called = true;
    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_ERROR;
}

static void test_send_dispatches_to_handler_verbatim(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, handler_error_verbatim, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    g_handler_called = false;
    cmd.id = 42;
    cmd.zone = RCP_ZONE_FRONT_LEFT;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL(RCP_RESPONSE_ERROR, out.status);
    TEST_ASSERT_EQUAL_UINT32(42, out.command_id);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static void test_send_after_close_is_closed(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    rcp_controller_close(ctrl);
    cmd.id = 1;
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(ctrl, &ctx, &cmd, &out));

    rcp_controller_release(ctrl);
}

static bool g_must_not_be_called;

static void handler_marks_called(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    (void)cmd; (void)out; (void)user_data;
    g_must_not_be_called = true;
}

static void test_send_done_context_returns_timeout(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, handler_marks_called, NULL);
    rcp_context_t past = rcp_context_with_deadline_ms(rcp_monotonic_ms() - 1000);
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    g_must_not_be_called = false;
    cmd.id = 1;
    cmd.zone = RCP_ZONE_FRONT_LEFT;

    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_controller_send(ctrl, &past, &cmd, &out));
    TEST_ASSERT_FALSE(g_must_not_be_called);

    rcp_controller_release(ctrl);
}

static void test_controller_close_idempotent(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));
    rcp_controller_release(ctrl);
}

static void test_subscribe_channel_closes_on_controller_close(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    rcp_controller_close(ctrl);

    /* The watcher thread and close() both close the channel synchronously
     * from close()'s perspective (close() closes subs directly before
     * returning), so no extra wait is required here. */
    TEST_ASSERT_TRUE(rcp_status_channel_is_closed(ch));

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
}

static void test_subscribe_after_close_is_closed(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    rcp_controller_close(ctrl);
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_subscribe(ctrl, &ctx, &ch));

    rcp_controller_release(ctrl);
}

static void test_controller_zone_returns_declared_zone(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_REAR_RIGHT, NULL, NULL);
    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(ctrl));
    rcp_controller_release(ctrl);
}

static void test_status_seq_strictly_increasing(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_CENTRAL, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    uint32_t last = 0;
    int i;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    rcp_mock_controller_publish(ctrl, NULL, 0);
    rcp_mock_controller_publish(ctrl, NULL, 0);
    rcp_mock_controller_publish(ctrl, NULL, 0);

    for (i = 0; i < 3; i++) {
        rcp_status_t st;
        TEST_ASSERT_TRUE(rcp_status_channel_recv(ch, &st));
        TEST_ASSERT_TRUE(st.seq > last);
        last = st.seq;
        rcp_status_free(&st);
    }

    rcp_status_channel_release(ch);
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

static void test_subscribe_channel_closes_on_context_expiry(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_with_timeout_ms(20);
    rcp_status_channel_t *ch = NULL;
    uint64_t start;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));

    /* Poll instead of a fixed sleep: wait up to 500ms for the watcher
     * thread to notice the expired context and close the channel. */
    start = rcp_monotonic_ms();
    while (!rcp_status_channel_is_closed(ch) && rcp_monotonic_ms() - start < 500) {
        /* busy-wait */
    }
    TEST_ASSERT_TRUE(rcp_status_channel_is_closed(ch));

    rcp_status_channel_release(ch);
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

static void test_multiple_subscribers_each_receive_published_status(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *channels[3] = {0};
    const uint8_t payload[] = {0x01};
    size_t i;

    for (i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &channels[i]));
    }
    rcp_mock_controller_publish(ctrl, payload, sizeof(payload));

    for (i = 0; i < 3; i++) {
        rcp_status_t st;
        TEST_ASSERT_TRUE(rcp_status_channel_recv(channels[i], &st));
        TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, st.zone);
        TEST_ASSERT_EQUAL_UINT(1, st.payload.len);
        TEST_ASSERT_EQUAL_UINT8(0x01, st.payload.data[0]);
        rcp_status_free(&st);
        rcp_status_channel_release(channels[i]);
    }

    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

static void test_send_noop_returns_ok(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 1; cmd.zone = RCP_ZONE_FRONT_LEFT; cmd.type = RCP_CMD_NOOP;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, out.status);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static void test_send_watchdog_returns_ok(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 1; cmd.zone = RCP_ZONE_FRONT_LEFT; cmd.type = RCP_CMD_WATCHDOG;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, out.status);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static void test_send_reset_returns_ok(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 1; cmd.zone = RCP_ZONE_FRONT_LEFT; cmd.type = RCP_CMD_RESET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, out.status);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static void test_publish_on_closed_does_not_crash(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_close(ctrl);
    rcp_mock_controller_publish(ctrl, NULL, 0); /* must not crash */
    rcp_controller_release(ctrl);
    TEST_PASS();
}

#define CONCURRENCY_THREADS 8
#define CONCURRENCY_ITERS   100

typedef struct {
    rcp_controller_t *ctrl;
    int               thread_index;
    volatile int     *ok_counter;
} send_worker_args_t;

static test_thread_ret_t TEST_THREAD_CALL send_worker(
#if defined(_WIN32)
    LPVOID arg
#else
    void *arg
#endif
)
{
    send_worker_args_t *a = (send_worker_args_t *)arg;
    rcp_context_t ctx = rcp_context_background();
    int i;

    for (i = 0; i < CONCURRENCY_ITERS; i++) {
        rcp_response_t out = {0};
        rcp_command_t cmd = {0};
        cmd.id = (uint32_t)(a->thread_index * CONCURRENCY_ITERS + i);
        cmd.zone = RCP_ZONE_FRONT_LEFT;
        cmd.type = RCP_CMD_GET;
        if (rcp_controller_send(a->ctrl, &ctx, &cmd, &out) == RCP_OK) {
            test_atomic_add(a->ok_counter, 1);
        }
        rcp_response_free(&out);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_concurrent_send_data_race_free(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    test_thread_t threads[CONCURRENCY_THREADS];
    send_worker_args_t args[CONCURRENCY_THREADS];
    volatile int ok_counter = 0;
    int i;

    for (i = 0; i < CONCURRENCY_THREADS; i++) {
        args[i].ctrl = ctrl;
        args[i].thread_index = i;
        args[i].ok_counter = &ok_counter;
        threads[i] = test_thread_spawn(send_worker, &args[i]);
    }
    for (i = 0; i < CONCURRENCY_THREADS; i++) test_thread_join(threads[i]);

    TEST_ASSERT_EQUAL_INT(CONCURRENCY_THREADS * CONCURRENCY_ITERS, ok_counter);

    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

typedef struct {
    rcp_controller_t   *ctrl;
    volatile int        stop;
} publish_worker_args_t;

static test_thread_ret_t TEST_THREAD_CALL publish_worker(
#if defined(_WIN32)
    LPVOID arg
#else
    void *arg
#endif
)
{
    publish_worker_args_t *a = (publish_worker_args_t *)arg;
    const uint8_t payload[] = {0xAB};
    while (!a->stop) {
        rcp_mock_controller_publish(a->ctrl, payload, sizeof(payload));
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_concurrent_publish_and_subscribe_data_race_free(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    publish_worker_args_t args;
    test_thread_t t;
    uint64_t start;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));

    args.ctrl = ctrl;
    args.stop = 0;
    t = test_thread_spawn(publish_worker, &args);

    start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < 20) {
        rcp_status_t st;
        if (rcp_status_channel_try_recv(ch, &st)) rcp_status_free(&st);
    }
    args.stop = 1;
    test_thread_join(t);

    rcp_status_channel_release(ch);
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
    TEST_PASS();
}

static void test_status_zone_correct(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_REAR_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    rcp_status_t st;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    rcp_mock_controller_publish(ctrl, NULL, 0);
    TEST_ASSERT_TRUE(rcp_status_channel_recv(ch, &st));
    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_LEFT, st.zone);

    rcp_status_free(&st);
    rcp_status_channel_release(ch);
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

static void test_status_payload_matches_published(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_status_t st;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    rcp_mock_controller_publish(ctrl, payload, sizeof(payload));
    TEST_ASSERT_TRUE(rcp_status_channel_recv(ch, &st));
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), st.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, st.payload.data, sizeof(payload));

    rcp_status_free(&st);
    rcp_status_channel_release(ch);
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

static void test_status_healthy_true_while_open(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    rcp_status_t st;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    rcp_mock_controller_publish(ctrl, NULL, 0);
    TEST_ASSERT_TRUE(rcp_status_channel_recv(ch, &st));
    TEST_ASSERT_TRUE(st.healthy);

    rcp_status_free(&st);
    rcp_status_channel_release(ch);
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

static void test_send_empty_payload_does_not_crash(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 1; cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static void test_send_zone_mismatch(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 1; cmd.zone = RCP_ZONE_FRONT_RIGHT;
    TEST_ASSERT_EQUAL(RCP_ERR_ZONE_MISMATCH, rcp_controller_send(ctrl, &ctx, &cmd, &out));

    rcp_controller_release(ctrl);
}

static uint8_t g_seen_payload[3];
static size_t  g_seen_payload_len;

static void handler_capture_payload(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    (void)user_data;
    g_seen_payload_len = cmd->payload.len;
    if (cmd->payload.len > 0) memcpy(g_seen_payload, cmd->payload.data, cmd->payload.len);
    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_OK;
}

static void test_payload_copied_before_handler(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, handler_capture_payload, NULL);
    rcp_context_t ctx = rcp_context_background();
    uint8_t payload[] = {1, 2, 3};
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    g_seen_payload_len = 0;
    cmd.id = 1;
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    rcp_controller_send(ctrl, &ctx, &cmd, &out);
    payload[0] = 0xFF; /* mutate after send returns */

    TEST_ASSERT_EQUAL_UINT(3, g_seen_payload_len);
    TEST_ASSERT_EQUAL_UINT8(1, g_seen_payload[0]);
    TEST_ASSERT_EQUAL_UINT8(2, g_seen_payload[1]);
    TEST_ASSERT_EQUAL_UINT8(3, g_seen_payload[2]);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static void test_publish_copies_payload_before_delivery(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    uint8_t payload[] = {0xAA, 0xBB};
    rcp_status_t st;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    rcp_mock_controller_publish(ctrl, payload, sizeof(payload));
    payload[0] = 0x00;

    TEST_ASSERT_TRUE(rcp_status_channel_recv(ch, &st));
    TEST_ASSERT_EQUAL_UINT8(0xAA, st.payload.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, st.payload.data[1]);

    rcp_status_free(&st);
    rcp_status_channel_release(ch);
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

/* ── Response fields ───────────────────────────────────────────────────────── */

static void test_response_command_id_echoes(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 0xDEADBEEF;
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, out.command_id);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

static void test_response_zone_identifies_processor(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_REAR_RIGHT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.id = 1;
    cmd.zone = RCP_ZONE_REAR_RIGHT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, out.zone);

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_new_registry_prepopulates_zones);
    RUN_TEST(test_register_duplicate_zone_already_exists);
    RUN_TEST(test_deregister_removes_and_closes);
    RUN_TEST(test_lookup_absent_zone_not_found);
    RUN_TEST(test_registry_close_idempotent);
    RUN_TEST(test_registry_controllers_returns_all);
    RUN_TEST(test_register_after_close_is_closed);
    RUN_TEST(test_deregister_never_registered_not_found);
    RUN_TEST(test_registered_controller_immediately_lookupable);
    RUN_TEST(test_registry_close_closes_all_controllers);
    RUN_TEST(test_lookup_on_closed_registry_is_closed);
    RUN_TEST(test_deregister_already_deregistered_not_found);

    RUN_TEST(test_send_no_handler_returns_ok);
    RUN_TEST(test_send_dispatches_to_handler_verbatim);
    RUN_TEST(test_send_after_close_is_closed);
    RUN_TEST(test_send_done_context_returns_timeout);
    RUN_TEST(test_controller_close_idempotent);
    RUN_TEST(test_subscribe_channel_closes_on_controller_close);
    RUN_TEST(test_subscribe_after_close_is_closed);
    RUN_TEST(test_controller_zone_returns_declared_zone);
    RUN_TEST(test_status_seq_strictly_increasing);
    RUN_TEST(test_subscribe_channel_closes_on_context_expiry);
    RUN_TEST(test_multiple_subscribers_each_receive_published_status);
    RUN_TEST(test_send_noop_returns_ok);
    RUN_TEST(test_send_watchdog_returns_ok);
    RUN_TEST(test_send_reset_returns_ok);
    RUN_TEST(test_publish_on_closed_does_not_crash);
    RUN_TEST(test_concurrent_send_data_race_free);
    RUN_TEST(test_concurrent_publish_and_subscribe_data_race_free);
    RUN_TEST(test_status_zone_correct);
    RUN_TEST(test_status_payload_matches_published);
    RUN_TEST(test_status_healthy_true_while_open);
    RUN_TEST(test_send_empty_payload_does_not_crash);
    RUN_TEST(test_send_zone_mismatch);
    RUN_TEST(test_payload_copied_before_handler);
    RUN_TEST(test_publish_copies_payload_before_delivery);

    RUN_TEST(test_response_command_id_echoes);
    RUN_TEST(test_response_zone_identifies_processor);

    return UNITY_END();
}
