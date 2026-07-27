/* Benchmark tests for the mock transport.
 *
 * Unity has no built-in benchmarking macro (unlike Catch2's BENCHMARK), so
 * each "benchmark" times N iterations of the operation and prints mean
 * latency; the pass/fail assertion is always just correctness (this file
 * is a regular ctest case, not a separate opt-in benchmark binary).
 *
 * Run with: ctest -R rcp_bench --output-on-failure -V
 */
/* Must be included before any other header (including unity.h, which pulls
 * in <stdio.h> etc.): bench_util.h's _POSIX_C_SOURCE define needs to land
 * before glibc's feature-test macros lock in on first system-header use. */
#include "bench_util.h"

#include "unity.h"

#include <rcp/mock.h>
#include <rcp/rcp.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE bench_thread_t;
#define BENCH_THREAD_CALL WINAPI
typedef DWORD bench_thread_ret_t;
static bench_thread_t bench_thread_spawn(bench_thread_ret_t(BENCH_THREAD_CALL *fn)(LPVOID), void *arg)
{
    return CreateThread(NULL, 0, fn, arg, 0, NULL);
}
static void bench_thread_join(bench_thread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
#include <pthread.h>
typedef pthread_t bench_thread_t;
#define BENCH_THREAD_CALL
typedef void *bench_thread_ret_t;
static bench_thread_t bench_thread_spawn(bench_thread_ret_t(BENCH_THREAD_CALL *fn)(void *), void *arg)
{
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void bench_thread_join(bench_thread_t t) { pthread_join(t, NULL); }
#endif

void setUp(void) {}
void tearDown(void) {}

#define BENCH_ITERS 10000

static void report(const char *label, uint64_t total_ns, int iters)
{
    printf("  [bench] %-40s %8.3f us/op  (%d iters)\n",
           label, (double)total_ns / (double)iters / 1000.0, iters);
}

/* ── Sanity (always asserted) ──────────────────────────────────────────────── */

static void test_bench_baseline_send_succeeds(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
}

/* ── Benchmarks ────────────────────────────────────────────────────────────── */

static void test_bench_send_round_trip(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    uint64_t start;
    int i;

    cmd.zone = RCP_ZONE_FRONT_LEFT;

    start = bench_now_ns();
    for (i = 0; i < BENCH_ITERS; i++) {
        rcp_response_t out = {0};
        TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
        rcp_response_free(&out);
    }
    report("send round-trip", bench_now_ns() - start, BENCH_ITERS);

    rcp_controller_release(ctrl);
}

static void test_bench_send_round_trip_with_payload(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    uint8_t payload[64];
    rcp_command_t cmd = {0};
    uint64_t start;
    int i;

    memset(payload, 0xAB, sizeof(payload));
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    start = bench_now_ns();
    for (i = 0; i < BENCH_ITERS; i++) {
        rcp_response_t out = {0};
        TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
        rcp_response_free(&out);
    }
    report("send 64-byte payload", bench_now_ns() - start, BENCH_ITERS);

    rcp_controller_release(ctrl);
}

#define CONCURRENT_THREADS 8
#define CONCURRENT_ITERS   200

static bench_thread_ret_t BENCH_THREAD_CALL concurrent_send_worker(
#if defined(_WIN32)
    LPVOID arg
#else
    void *arg
#endif
)
{
    rcp_controller_t *ctrl = (rcp_controller_t *)arg;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    int i;

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    for (i = 0; i < CONCURRENT_ITERS; i++) {
        rcp_response_t out = {0};
        (void)rcp_controller_send(ctrl, &ctx, &cmd, &out);
        rcp_response_free(&out);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

//cfusa:req REQ-CTRL-018
static void test_bench_send_concurrent(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    bench_thread_t threads[CONCURRENT_THREADS];
    uint64_t start;
    int i;

    start = bench_now_ns();
    for (i = 0; i < CONCURRENT_THREADS; i++) {
        threads[i] = bench_thread_spawn(concurrent_send_worker, ctrl);
    }
    for (i = 0; i < CONCURRENT_THREADS; i++) bench_thread_join(threads[i]);
    report("8-thread concurrent send", bench_now_ns() - start, CONCURRENT_THREADS * CONCURRENT_ITERS);

    rcp_controller_release(ctrl);
    TEST_PASS();
}

static void test_bench_publish_fan_out(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *channels[8];
    uint8_t payload[32] = {0};
    uint64_t start;
    int i;

    for (i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &channels[i]));
    }

    start = bench_now_ns();
    for (i = 0; i < BENCH_ITERS; i++) {
        rcp_mock_controller_publish(ctrl, payload, sizeof(payload));
    }
    report("publish to 8 subscribers", bench_now_ns() - start, BENCH_ITERS);

    /* Drain so channel buffers don't matter for timing; then release. */
    for (i = 0; i < 8; i++) {
        rcp_status_t st;
        while (rcp_status_channel_try_recv(channels[i], &st)) rcp_status_free(&st);
        rcp_status_channel_release(channels[i]);
    }
    rcp_controller_close(ctrl);
    rcp_controller_release(ctrl);
}

static void test_bench_registry_lookup(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    uint64_t start;
    int i;

    start = bench_now_ns();
    for (i = 0; i < BENCH_ITERS; i++) {
        rcp_controller_t *out = NULL;
        TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &out));
        rcp_controller_release(out);
    }
    report("registry lookup", bench_now_ns() - start, BENCH_ITERS);

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bench_baseline_send_succeeds);
    RUN_TEST(test_bench_send_round_trip);
    RUN_TEST(test_bench_send_round_trip_with_payload);
    RUN_TEST(test_bench_send_concurrent);
    RUN_TEST(test_bench_publish_fan_out);
    RUN_TEST(test_bench_registry_lookup);

    return UNITY_END();
}
