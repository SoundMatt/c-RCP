/* SPDX-License-Identifier: MPL-2.0 */
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
//cfusa:test REQ-OBS-014
//cfusa:test REQ-OBS-015
//cfusa:test REQ-OBS-016
//cfusa:test REQ-OBS-017
//cfusa:test REQ-OBS-018
//cfusa:test REQ-OBS-019
//cfusa:test REQ-OBS-020
//cfusa:test REQ-OBS-021
//cfusa:test REQ-OBS-022
#include "unity.h"

#include <rcp/observe.h>

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

static rcp_avtp_addr_t make_addr(uint16_t unique_id, uint8_t byte_bus_id)
{
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    rcp_avtp_addr_t a;
    a.stream_id   = rcp_stream_id_make(mac, unique_id);
    a.byte_bus_id = byte_bus_id;
    return a;
}

/* ── record() ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-OBS-001
//cfusa:test REQ-OBS-009
//cfusa:test REQ-OBS-010
//cfusa:test REQ-OBS-011
//cfusa:test REQ-OBS-015
//cfusa:test REQ-OBS-016
//cfusa:test REQ-OBS-017
//cfusa:test REQ-OBS-018
//cfusa:test REQ-OBS-022
static void test_record_produces_a_span_with_every_field(void)
{
    rcp_in_memory_sink_t *mem = rcp_in_memory_sink_new();
    rcp_metrics_sink_t sink = rcp_in_memory_sink_as_sink(mem);
    rcp_avtp_addr_t addr = make_addr(1, 3);
    rcp_span_t out;

    rcp_observe_record(sink, "rcp.request", addr, 0x0F, 1000, 1010, RCP_OK);

    TEST_ASSERT_EQUAL_UINT(1, rcp_in_memory_sink_span_count(mem));
    TEST_ASSERT_EQUAL_UINT(1, rcp_in_memory_sink_spans(mem, &out, 1));
    TEST_ASSERT_EQUAL_STRING("rcp.request", out.name);
    TEST_ASSERT_TRUE(rcp_avtp_addr_equal(addr, out.addr));
    TEST_ASSERT_EQUAL_UINT8(0x0F, out.request_type);
    TEST_ASSERT_EQUAL_UINT64(1000, out.start_ms);
    TEST_ASSERT_EQUAL_UINT64(1010, out.end_ms);

    rcp_in_memory_sink_destroy(mem);
}

//cfusa:test REQ-OBS-002
static void test_multiple_records_accumulate_spans_in_order(void)
{
    rcp_in_memory_sink_t *mem = rcp_in_memory_sink_new();
    rcp_metrics_sink_t sink = rcp_in_memory_sink_as_sink(mem);
    rcp_span_t out[3];

    rcp_observe_record(sink, "a", make_addr(1, 0), 0x00, 0, 5, RCP_OK);
    rcp_observe_record(sink, "b", make_addr(2, 0), 0x00, 5, 12, RCP_OK);
    rcp_observe_record(sink, "c", make_addr(3, 0), 0x00, 12, 20, RCP_OK);

    TEST_ASSERT_EQUAL_UINT(3, rcp_in_memory_sink_spans(mem, out, 3));
    TEST_ASSERT_EQUAL_STRING("a", out[0].name);
    TEST_ASSERT_EQUAL_STRING("b", out[1].name);
    TEST_ASSERT_EQUAL_STRING("c", out[2].name);

    rcp_in_memory_sink_destroy(mem);
}

//cfusa:test REQ-OBS-003
//cfusa:test REQ-OBS-013
static void test_span_duration_is_end_minus_start(void)
{
    rcp_span_t span;
    span.start_ms = 100;
    span.end_ms   = 175;

    TEST_ASSERT_EQUAL_UINT64(75, rcp_span_duration_ms(&span));
}

//cfusa:test REQ-OBS-004
static void test_span_captures_the_result_code(void)
{
    rcp_in_memory_sink_t *mem = rcp_in_memory_sink_new();
    rcp_metrics_sink_t sink = rcp_in_memory_sink_as_sink(mem);
    rcp_span_t out;

    rcp_observe_record(sink, "rcp.request", make_addr(1, 0), 0x00, 0, 1, RCP_ERR_TIMEOUT);

    rcp_in_memory_sink_spans(mem, &out, 1);
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, out.result);

    rcp_in_memory_sink_destroy(mem);
}

/* rcp_in_memory_sink_spans()'s own contract (include/rcp/observe.h): it
 * copies min(recorded_count, cap) spans but returns the TRUE recorded
 * count, which may exceed cap -- signalling truncation to the caller.
 * Nothing above exercises cap < recorded_count; this does. */
//cfusa:test REQ-OBS-018
static void test_spans_reports_true_count_when_truncated_by_cap(void)
{
    rcp_in_memory_sink_t *mem = rcp_in_memory_sink_new();
    rcp_metrics_sink_t sink = rcp_in_memory_sink_as_sink(mem);
    rcp_span_t out[2];
    size_t reported;

    rcp_observe_record(sink, "a", make_addr(1, 0), 0x00, 0, 5, RCP_OK);
    rcp_observe_record(sink, "b", make_addr(2, 0), 0x00, 5, 12, RCP_OK);
    rcp_observe_record(sink, "c", make_addr(3, 0), 0x00, 12, 20, RCP_OK);

    reported = rcp_in_memory_sink_spans(mem, out, 2); /* cap smaller than the 3 recorded */
    TEST_ASSERT_EQUAL_UINT(3, reported);              /* true count, not the cap */
    TEST_ASSERT_EQUAL_STRING("a", out[0].name);        /* only the first cap(2) copied */
    TEST_ASSERT_EQUAL_STRING("b", out[1].name);

    rcp_in_memory_sink_destroy(mem);
}

/* ── Noop sink ────────────────────────────────────────────────────────────── */

/* REQ-OBS-014: rcp_noop_metrics_sink() returns a sink whose three
 * callbacks all discard their arguments -- exercised end to end through
 * rcp_observe_record(), which is the only caller-visible way most code
 * ever reaches this sink at all. */
//cfusa:test REQ-OBS-014
static void test_noop_sink_does_not_crash(void)
{
    rcp_metrics_sink_t sink = rcp_noop_metrics_sink();

    rcp_observe_record(sink, "rcp.request", make_addr(1, 0), 0x00, 0, 1, RCP_OK);
    rcp_observe_record(sink, "rcp.request", make_addr(1, 0), 0x00, 0, 1, RCP_ERR_TIMEOUT);
}

/* REQ-OBS-005/REQ-OBS-020/REQ-OBS-021 each own one noop-sink callback's
 * "safe to call with any valid arguments, no observable side effect"
 * contract. Every callback here ignores ctx entirely ((void)ctx in
 * src/observe.c) -- rather than pass ctx=NULL, the way
 * rcp_noop_metrics_sink() itself always does, each test below points
 * ctx at its own canary struct and memcmp()s it before/after the call.
 * That is a real, distinct assertion for "no observable side effect"
 * (a bug that started writing through ctx would flip this from pass to
 * fail), not merely "did not crash" restated -- and it is independent
 * per callback: breaking one callback's no-side-effect contract cannot
 * make a different callback's canary test fail. */
typedef struct { unsigned char bytes[64]; } side_effect_canary_t;

static side_effect_canary_t make_canary(void)
{
    side_effect_canary_t c;
    size_t i;
    for (i = 0; i < sizeof(c.bytes); i++) c.bytes[i] = (unsigned char)(0xA5 ^ i);
    return c;
}

//cfusa:test REQ-OBS-005
static void test_noop_record_span_is_safe_and_side_effect_free(void)
{
    rcp_metrics_sink_t   sink   = rcp_noop_metrics_sink();
    side_effect_canary_t canary = make_canary();
    side_effect_canary_t before = canary;
    rcp_span_t           span;

    span.name         = "rcp.request";
    span.addr         = make_addr(0xFFFF, 0xFF);
    span.request_type = 0xFF;
    span.start_ms     = 0;
    span.end_ms       = UINT64_MAX;
    span.result       = RCP_ERR_TIMEOUT;

    sink.vt->record_span(&span, &canary); /* must not crash on edge-case values */

    TEST_ASSERT_EQUAL_MEMORY(&before, &canary, sizeof(canary));
}

//cfusa:test REQ-OBS-020
static void test_noop_record_gauge_is_safe_and_side_effect_free(void)
{
    rcp_metrics_sink_t   sink   = rcp_noop_metrics_sink();
    side_effect_canary_t canary = make_canary();
    side_effect_canary_t before = canary;
    rcp_metric_t         metric;

    metric.name  = "rcp.something";
    metric.value = 3.5;
    metric.addr  = make_addr(1, 0);

    sink.vt->record_gauge(&metric, &canary);

    TEST_ASSERT_EQUAL_MEMORY(&before, &canary, sizeof(canary));
}

//cfusa:test REQ-OBS-021
static void test_noop_record_counter_is_safe_and_side_effect_free(void)
{
    rcp_metrics_sink_t   sink   = rcp_noop_metrics_sink();
    side_effect_canary_t canary = make_canary();
    side_effect_canary_t before = canary;

    sink.vt->record_counter("rcp.other", make_addr(1, 0), 1.0, &canary);

    TEST_ASSERT_EQUAL_MEMORY(&before, &canary, sizeof(canary));
}

/* ── Counters ─────────────────────────────────────────────────────────────── */

typedef struct {
    int total_calls;
    int error_calls;
} counting_ctx_t;

static void counting_record_span(const rcp_span_t *span, void *ctx) { (void)span; (void)ctx; }
static void counting_record_gauge(const rcp_metric_t *metric, void *ctx) { (void)metric; (void)ctx; }
static void counting_record_counter(const char *name, rcp_avtp_addr_t addr, double delta, void *ctx)
{
    counting_ctx_t *c = (counting_ctx_t *)ctx;
    (void)addr; (void)delta;
    if (strcmp(name, "rcp.requests.total") == 0) c->total_calls++;
    if (strcmp(name, "rcp.requests.errors") == 0) c->error_calls++;
}

static const rcp_metrics_sink_vtable_t counting_vtable = {
    counting_record_span,
    counting_record_gauge,
    counting_record_counter,
};

//cfusa:test REQ-OBS-007
//cfusa:test REQ-OBS-008
static void test_total_counter_fires_every_time_errors_only_on_failure(void)
{
    counting_ctx_t ctx = {0, 0};
    rcp_metrics_sink_t sink;
    sink.vt  = &counting_vtable;
    sink.ctx = &ctx;

    rcp_observe_record(sink, "rcp.request", make_addr(1, 0), 0x00, 0, 1, RCP_OK);
    rcp_observe_record(sink, "rcp.request", make_addr(1, 0), 0x00, 0, 1, RCP_ERR_TIMEOUT);
    rcp_observe_record(sink, "rcp.request", make_addr(1, 0), 0x00, 0, 1, RCP_OK);

    TEST_ASSERT_EQUAL(3, ctx.total_calls);
    TEST_ASSERT_EQUAL(1, ctx.error_calls);
}

/* ── Gauge callback ───────────────────────────────────────────────────────── */

//cfusa:test REQ-OBS-012
static void test_in_memory_gauge_and_counter_are_no_ops(void)
{
    rcp_in_memory_sink_t *mem = rcp_in_memory_sink_new();
    rcp_metrics_sink_t sink = rcp_in_memory_sink_as_sink(mem);
    rcp_metric_t metric;
    metric.name  = "rcp.something";
    metric.value = 3.5;
    metric.addr  = make_addr(1, 0);

    sink.vt->record_gauge(&metric, sink.ctx);
    sink.vt->record_counter("rcp.other", make_addr(1, 0), 1.0, sink.ctx);

    /* Neither call should have affected the span log. */
    TEST_ASSERT_EQUAL_UINT(0, rcp_in_memory_sink_span_count(mem));

    rcp_in_memory_sink_destroy(mem);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

static rcp_in_memory_sink_t *g_mem;

#if defined(_WIN32)
static DWORD WINAPI record_worker(void *arg)
#else
static void *record_worker(void *arg)
#endif
{
    rcp_metrics_sink_t sink = rcp_in_memory_sink_as_sink(g_mem);
    int i;
    (void)arg;

    for (i = 0; i < 2000; i++) {
        rcp_observe_record(sink, "rcp.request", make_addr((uint16_t)i, 0), 0x00, 0, 1, RCP_OK);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

//cfusa:test REQ-OBS-006
static void test_in_memory_sink_is_thread_safe_under_concurrent_spans(void)
{
    test_thread_t threads[8];
    int i;

    g_mem = rcp_in_memory_sink_new();

    for (i = 0; i < 8; i++) threads[i] = test_thread_spawn(record_worker, NULL);
    for (i = 0; i < 8; i++) test_thread_join(threads[i]);

    TEST_ASSERT_EQUAL_UINT(8 * 2000, rcp_in_memory_sink_span_count(g_mem));

    rcp_in_memory_sink_destroy(g_mem);
}

/* ── Destroy ──────────────────────────────────────────────────────────────── */

/* REQ-OBS-019's own clause, split off from REQ-OBS-022's non-NULL
 * cleanup-ordering clause below: rcp_in_memory_sink_destroy(NULL) must
 * be a safe no-op. Nothing above this batch ever called destroy() with
 * a NULL argument -- every existing call site in this file passes a
 * live sink -- so this is real, previously-missing coverage of the
 * NULL branch, not a restatement of an assertion made elsewhere. */
//cfusa:test REQ-OBS-019
static void test_in_memory_sink_destroy_null_is_safe_noop(void)
{
    rcp_in_memory_sink_destroy(NULL); /* must not crash; ASan would flag any NULL deref */
    TEST_PASS();
}

/* REQ-OBS-022: for non-NULL s, destroy() frees s's mutex and
 * recorded-spans array before freeing s itself. This file's own
 * convention (see test_authz.c's test_retain_and_release_tolerate_null()
 * neighbor for the same reasoning) is to never inspect freed memory
 * directly, so the real check here is indirect: force the spans array
 * to actually grow past its initial cap==0 (in_memory_record_span()'s
 * own growth path) so destroy() has real heap state -- mutex and array
 * both allocated, not an all-zero freshly-`rcp_calloc`'d struct -- to
 * release, and let AddressSanitizer's heap-corruption/use-after-free
 * detection (not just LeakSanitizer, which CI's macOS runs disable via
 * ASAN_OPTIONS=detect_leaks=0) catch any wrong free ordering. */
//cfusa:test REQ-OBS-022
static void test_in_memory_sink_destroy_frees_mutex_and_spans_array(void)
{
    rcp_in_memory_sink_t *mem = rcp_in_memory_sink_new();
    rcp_metrics_sink_t sink = rcp_in_memory_sink_as_sink(mem);

    rcp_observe_record(sink, "rcp.request", make_addr(1, 0), 0x00, 0, 1, RCP_OK);
    TEST_ASSERT_EQUAL_UINT(1, rcp_in_memory_sink_span_count(mem));

    rcp_in_memory_sink_destroy(mem); /* mutex destroyed, spans array freed, s freed -- in that order */
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_record_produces_a_span_with_every_field);
    RUN_TEST(test_multiple_records_accumulate_spans_in_order);
    RUN_TEST(test_span_duration_is_end_minus_start);
    RUN_TEST(test_span_captures_the_result_code);
    RUN_TEST(test_spans_reports_true_count_when_truncated_by_cap);
    RUN_TEST(test_noop_sink_does_not_crash);
    RUN_TEST(test_noop_record_span_is_safe_and_side_effect_free);
    RUN_TEST(test_noop_record_gauge_is_safe_and_side_effect_free);
    RUN_TEST(test_noop_record_counter_is_safe_and_side_effect_free);
    RUN_TEST(test_total_counter_fires_every_time_errors_only_on_failure);
    RUN_TEST(test_in_memory_gauge_and_counter_are_no_ops);
    RUN_TEST(test_in_memory_sink_is_thread_safe_under_concurrent_spans);
    RUN_TEST(test_in_memory_sink_destroy_null_is_safe_noop);
    RUN_TEST(test_in_memory_sink_destroy_frees_mutex_and_spans_array);

    return UNITY_END();
}
