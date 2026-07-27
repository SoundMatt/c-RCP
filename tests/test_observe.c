//cfusa:test REQ-OBS-001
//cfusa:test REQ-OBS-002
//cfusa:test REQ-OBS-003
//cfusa:test REQ-OBS-004
//cfusa:test REQ-OBS-005
//cfusa:test REQ-OBS-006
//cfusa:test REQ-OBS-007
//cfusa:test REQ-OBS-008
//cfusa:test REQ-OBS-009
//cfusa:test REQ-OBS-010
//cfusa:test REQ-OBS-011
//cfusa:test REQ-OBS-012
//cfusa:test REQ-OBS-013
#include "unity.h"

#include <rcp/mock.h>
#include <rcp/observe.h>
#include <rcp/rcp.h>

#include <string.h>

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

/* ── CountingSink: records counter deltas by name and the last span seen ─── */

typedef struct {
    char   name[32];
    double value;
} counter_entry_t;

typedef struct {
    counter_entry_t counters[8];
    size_t           counters_len;
    rcp_span_t       last_span;
} counting_sink_t;

static void counting_record_span(const rcp_span_t *span, void *ctx)
{
    counting_sink_t *cs = (counting_sink_t *)ctx;
    cs->last_span = *span;
}

static void counting_record_gauge(const rcp_metric_t *metric, void *ctx) { (void)metric; (void)ctx; }

static void counting_record_counter(const char *name, rcp_zone_t zone, double delta, void *ctx)
{
    counting_sink_t *cs = (counting_sink_t *)ctx;
    size_t i;
    (void)zone;

    for (i = 0; i < cs->counters_len; i++) {
        if (strcmp(cs->counters[i].name, name) == 0) {
            cs->counters[i].value += delta;
            return;
        }
    }
    strncpy(cs->counters[cs->counters_len].name, name, sizeof(cs->counters[0].name) - 1);
    cs->counters[cs->counters_len].value = delta;
    cs->counters_len++;
}

static const rcp_metrics_sink_vtable_t counting_sink_vtable = {
    counting_record_span, counting_record_gauge, counting_record_counter,
};

static double counting_sink_counter(const counting_sink_t *cs, const char *name)
{
    size_t i;
    for (i = 0; i < cs->counters_len; i++) {
        if (strcmp(cs->counters[i].name, name) == 0) return cs->counters[i].value;
    }
    return 0.0;
}

/* ── Span recording ───────────────────────────────────────────────────────── */

static void test_span_recorded_on_successful_send(void)
{
    rcp_in_memory_sink_t *sink = rcp_in_memory_sink_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_observe_controller_new(inner, rcp_in_memory_sink_as_sink(sink));
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_span_t spans[4];

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    TEST_ASSERT_EQUAL_UINT(1, rcp_in_memory_sink_span_count(sink));
    TEST_ASSERT_EQUAL_UINT(1, rcp_in_memory_sink_spans(sink, spans, 4));
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, spans[0].zone);
    TEST_ASSERT_EQUAL(RCP_CMD_GET, spans[0].cmd_type);
    TEST_ASSERT_EQUAL(RCP_OK, spans[0].result);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_in_memory_sink_destroy(sink);
}

static void test_multiple_sends_accumulate_spans(void)
{
    rcp_in_memory_sink_t *sink = rcp_in_memory_sink_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_observe_controller_new(inner, rcp_in_memory_sink_as_sink(sink));
    rcp_context_t ctx = rcp_context_background();
    int i;

    for (i = 0; i < 5; i++) {
        rcp_command_t cmd = {0};
        rcp_response_t resp = {0};
        cmd.zone = RCP_ZONE_FRONT_LEFT;
        cmd.type = RCP_CMD_SET;
        (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
        rcp_response_free(&resp);
    }

    TEST_ASSERT_EQUAL_UINT(5, rcp_in_memory_sink_span_count(sink));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_in_memory_sink_destroy(sink);
}

static void test_span_duration_is_non_negative(void)
{
    rcp_in_memory_sink_t *sink = rcp_in_memory_sink_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_CENTRAL);
    rcp_controller_t *ctrl = rcp_observe_controller_new(inner, rcp_in_memory_sink_as_sink(sink));
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_span_t spans[1];

    cmd.zone = RCP_ZONE_CENTRAL;
    cmd.type = RCP_CMD_WATCHDOG;
    (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
    rcp_response_free(&resp);

    TEST_ASSERT_EQUAL_UINT(1, rcp_in_memory_sink_span_count(sink));
    rcp_in_memory_sink_spans(sink, spans, 1);
    TEST_ASSERT_TRUE(spans[0].end_ms >= spans[0].start_ms);

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_in_memory_sink_destroy(sink);
}

static void test_span_duration_ms_is_end_minus_start(void)
{
    rcp_span_t span;

    memset(&span, 0, sizeof(span));
    span.start_ms = 100;
    span.end_ms   = 150;
    TEST_ASSERT_EQUAL_UINT64(50, rcp_span_duration_ms(&span));

    span.start_ms = span.end_ms = 42;
    TEST_ASSERT_EQUAL_UINT64(0, rcp_span_duration_ms(&span));
}

static void test_record_gauge_accepts_metric_without_crashing(void)
{
    rcp_in_memory_sink_t *sink = rcp_in_memory_sink_new();
    rcp_metrics_sink_t noop = rcp_noop_metrics_sink();
    rcp_metrics_sink_t in_mem = rcp_in_memory_sink_as_sink(sink);
    rcp_metric_t m;

    m.name  = "rcp.queue_depth";
    m.value = 3.0;
    m.zone  = RCP_ZONE_FRONT_LEFT;

    noop.vt->record_gauge(&m, noop.ctx);
    in_mem.vt->record_gauge(&m, in_mem.ctx);

    rcp_in_memory_sink_destroy(sink);
}

static void test_noop_sink_does_not_crash(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_observe_controller_new(inner, rcp_noop_metrics_sink());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_commands_total_counter_increments_per_send(void)
{
    counting_sink_t cs;
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_metrics_sink_t sink;
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    int i;

    memset(&cs, 0, sizeof(cs));
    sink.vt  = &counting_sink_vtable;
    sink.ctx = &cs;
    ctrl = rcp_observe_controller_new(inner, sink);

    for (i = 0; i < 3; i++) {
        rcp_command_t cmd = {0};
        rcp_response_t resp = {0};
        cmd.zone = RCP_ZONE_FRONT_LEFT;
        cmd.type = RCP_CMD_SET;
        TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
        rcp_response_free(&resp);
    }

    TEST_ASSERT_TRUE(counting_sink_counter(&cs, "rcp.commands.total") == 3.0);
    TEST_ASSERT_TRUE(counting_sink_counter(&cs, "rcp.commands.errors") == 0.0);

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_span_captures_error_and_error_counter_increments(void)
{
    counting_sink_t cs;
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_metrics_sink_t sink;
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    memset(&cs, 0, sizeof(cs));
    rcp_controller_close(inner); /* a closed controller fails every send with RCP_ERR_CLOSED */
    sink.vt  = &counting_sink_vtable;
    sink.ctx = &cs;
    ctrl = rcp_observe_controller_new(inner, sink);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, cs.last_span.result); /* span records the error (REQ-OBS-004) */
    TEST_ASSERT_TRUE(counting_sink_counter(&cs, "rcp.commands.total") == 1.0);
    TEST_ASSERT_TRUE(counting_sink_counter(&cs, "rcp.commands.errors") == 1.0); /* REQ-OBS-008 */

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

#define KTHREADS 8
#define KPER_THREAD 500

static rcp_controller_t *g_ctrl;

#if defined(_WIN32)
static DWORD WINAPI send_worker(void *arg)
#else
static void *send_worker(void *arg)
#endif
{
    rcp_context_t ctx = rcp_context_background();
    int i;
    (void)arg;

    for (i = 0; i < KPER_THREAD; i++) {
        rcp_command_t cmd = {0};
        rcp_response_t resp = {0};
        cmd.zone = RCP_ZONE_CENTRAL;
        cmd.type = RCP_CMD_GET;
        (void)rcp_controller_send(g_ctrl, &ctx, &cmd, &resp);
        rcp_response_free(&resp);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_in_memory_sink_thread_safe_under_concurrent_spans(void)
{
    rcp_in_memory_sink_t *sink = rcp_in_memory_sink_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_CENTRAL);
    test_thread_t threads[KTHREADS];
    int i;

    g_ctrl = rcp_observe_controller_new(inner, rcp_in_memory_sink_as_sink(sink));

    for (i = 0; i < KTHREADS; i++) threads[i] = test_thread_spawn(send_worker, NULL);
    for (i = 0; i < KTHREADS; i++) test_thread_join(threads[i]);

    TEST_ASSERT_EQUAL_UINT(KTHREADS * KPER_THREAD, rcp_in_memory_sink_span_count(sink));

    rcp_controller_release(g_ctrl);
    rcp_controller_release(inner);
    rcp_in_memory_sink_destroy(sink);
}

static void test_zone_delegates_to_inner(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_REAR_LEFT);
    rcp_controller_t *ctrl = rcp_observe_controller_new(inner, rcp_noop_metrics_sink());

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_LEFT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_subscribe_delegates_to_inner(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_observe_controller_new(inner, rcp_noop_metrics_sink());
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_close_delegates_to_inner(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_observe_controller_new(inner, rcp_noop_metrics_sink());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(inner, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_span_recorded_on_successful_send);
    RUN_TEST(test_multiple_sends_accumulate_spans);
    RUN_TEST(test_span_duration_is_non_negative);
    RUN_TEST(test_span_duration_ms_is_end_minus_start);
    RUN_TEST(test_record_gauge_accepts_metric_without_crashing);
    RUN_TEST(test_noop_sink_does_not_crash);
    RUN_TEST(test_commands_total_counter_increments_per_send);
    RUN_TEST(test_span_captures_error_and_error_counter_increments);
    RUN_TEST(test_in_memory_sink_thread_safe_under_concurrent_spans);
    RUN_TEST(test_zone_delegates_to_inner);
    RUN_TEST(test_subscribe_delegates_to_inner);
    RUN_TEST(test_close_delegates_to_inner);

    return UNITY_END();
}
