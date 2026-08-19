/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ADMIN-001
//cfusa:test REQ-ADMIN-002
//cfusa:test REQ-ADMIN-003
//cfusa:test REQ-ADMIN-004
//cfusa:test REQ-ADMIN-005
//cfusa:test REQ-ADMIN-006
//cfusa:test REQ-ADMIN-007
//cfusa:test REQ-ADMIN-008
//cfusa:test REQ-ADMIN-009
//cfusa:test REQ-ADMIN-010
//cfusa:test REQ-ADMIN-011
//cfusa:test REQ-ADMIN-012
#include "unity.h"

#include <rcp/admin.h>
#include <rcp/alloc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
void tearDown(void) { rcp_alloc_reset_hooks(); } /* never leak a fault-injection hook across tests */

static rcp_avtp_addr_t make_addr(uint16_t unique_id, uint8_t byte_bus_id)
{
    uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    rcp_avtp_addr_t a;
    a.stream_id   = rcp_stream_id_make(mac, unique_id);
    a.byte_bus_id = byte_bus_id;
    return a;
}

/* ── Construction / destruction ───────────────────────────────────────────── */

//cfusa:test REQ-ADMIN-009
static void test_new_returns_a_server_with_no_endpoints_registered(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_endpoint_info_t out[4];

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL_size_t(0, rcp_admin_server_endpoints(srv, out, 4));

    rcp_admin_server_destroy(srv);
}

/* REQ-ADMIN-011's own clause, split out of what was REQ-ADMIN-009: forces
 * rcp_admin_server_new()'s internal rcp_calloc() to fail via this project's
 * fault-injection hook (alloc.h), the same technique test_loan.c's
 * test_acquire_returns_null_when_loan_struct_allocation_fails() uses --
 * distinct from test_new_returns_a_server_with_no_endpoints_registered()
 * above, which never exercises this branch at all. */
static void *admin_test_failing_calloc(size_t nmemb, size_t size)
{
    (void)nmemb; (void)size;
    return NULL;
}

//cfusa:test REQ-ADMIN-011
static void test_new_returns_null_when_allocation_fails(void)
{
    rcp_alloc_hooks_t hooks = {0};

    hooks.calloc_fn = admin_test_failing_calloc;
    rcp_alloc_set_hooks(&hooks);

    TEST_ASSERT_NULL(rcp_admin_server_new());

    rcp_alloc_reset_hooks();
}

//cfusa:test REQ-ADMIN-010
static void test_destroy_tolerates_null(void)
{
    rcp_admin_server_destroy(NULL); /* must not crash */
}

/* REQ-ADMIN-012's own clause, split out of what was REQ-ADMIN-010: proves
 * destroy() actually frees srv (not just "doesn't crash", which
 * test_destroy_tolerates_null above already covers for the NULL case) by
 * installing a counting rcp_free() hook and checking it fires exactly once,
 * with srv's own pointer -- every other test in this file also calls
 * rcp_admin_server_destroy() at teardown, but none of them assert anything
 * about that call, so this is the first test that would actually fail if
 * destroy() stopped freeing srv. */
static int   g_admin_free_calls;
static void *g_admin_last_freed;

static void admin_test_counting_free(void *ptr)
{
    g_admin_free_calls++;
    g_admin_last_freed = ptr;
    free(ptr);
}

//cfusa:test REQ-ADMIN-012
static void test_destroy_frees_srv_when_non_null(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_alloc_hooks_t   hooks = {0};

    g_admin_free_calls = 0;
    g_admin_last_freed = NULL;
    hooks.free_fn = admin_test_counting_free;
    rcp_alloc_set_hooks(&hooks);

    rcp_admin_server_destroy(srv);

    TEST_ASSERT_EQUAL_INT(1, g_admin_free_calls);
    TEST_ASSERT_EQUAL_PTR(srv, g_admin_last_freed);

    rcp_alloc_reset_hooks();
}

/* ── Endpoint registration ────────────────────────────────────────────────── */

//cfusa:test REQ-ADMIN-002
static void test_register_and_deregister_report_membership_changes(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_avtp_addr_t addr = make_addr(1, 0);

    TEST_ASSERT_TRUE(rcp_admin_server_register_endpoint(srv, addr));
    TEST_ASSERT_FALSE(rcp_admin_server_register_endpoint(srv, addr)); /* already registered */

    TEST_ASSERT_TRUE(rcp_admin_server_deregister_endpoint(srv, addr));
    TEST_ASSERT_FALSE(rcp_admin_server_deregister_endpoint(srv, addr)); /* already gone */

    rcp_admin_server_destroy(srv);
}

//cfusa:test REQ-ADMIN-001
static void test_endpoints_returns_a_snapshot_of_registered_endpoints(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_endpoint_info_t out[2];
    size_t n;

    TEST_ASSERT_TRUE(rcp_admin_server_register_endpoint(srv, make_addr(1, 0)));
    TEST_ASSERT_TRUE(rcp_admin_server_register_endpoint(srv, make_addr(2, 0)));

    n = rcp_admin_server_endpoints(srv, out, 2);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_TRUE(out[0].registered);
    TEST_ASSERT_TRUE(out[1].registered);
    TEST_ASSERT_EQUAL_STRING("", out[0].extra);

    rcp_admin_server_destroy(srv);
}

/* MC/DC: rcp_admin_server_endpoints()'s `for (i = 0; i < n && i < cap;
 * i++)` (src/admin.c) never had `i < cap` independently demonstrated --
 * every other call site in this file passes a `cap` at least as large
 * as the registered count, so the loop always runs its full course and
 * `i < n` alone ever decides when it stops. This registers two
 * endpoints but caps the output array at 1, so the *second* iteration's
 * `i < cap` (false, i=1 == cap=1) is what actually ends the loop -- not
 * `i < n` (which would still be true, i=1 < n=2). The full count (2) is
 * still the return value: cap only bounds how much of out[] gets
 * written, not what rcp_admin_server_endpoints() reports as the true
 * registered total (mirroring snprintf()'s own "would-have-written"
 * convention, see rcp_admin_server_metrics_text() a few lines down). */
//cfusa:test REQ-ADMIN-001
static void test_endpoints_return_value_is_full_count_even_when_cap_truncates_output(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_endpoint_info_t out[1];
    size_t n;

    TEST_ASSERT_TRUE(rcp_admin_server_register_endpoint(srv, make_addr(1, 0)));
    TEST_ASSERT_TRUE(rcp_admin_server_register_endpoint(srv, make_addr(2, 0)));

    n = rcp_admin_server_endpoints(srv, out, 1);
    TEST_ASSERT_EQUAL_UINT(2, n); /* full count, not clamped to cap */
    TEST_ASSERT_TRUE(out[0].registered); /* only the one slot cap allowed got written */

    rcp_admin_server_destroy(srv);
}

/* ── Subscription / emit ──────────────────────────────────────────────────── */

static int g_event_count;
static rcp_admin_event_t g_last_event;

static void count_events(const rcp_admin_event_t *ev, void *user_data)
{
    (void)user_data;
    g_event_count++;
    g_last_event = *ev;
}

//cfusa:test REQ-ADMIN-003
//cfusa:test REQ-ADMIN-004
static void test_subscribe_and_emit_deliver_the_correct_event(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_admin_event_t ev;
    rcp_avtp_addr_t addr = make_addr(9, 2);

    g_event_count = 0;
    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, count_events, NULL));

    ev.type  = RCP_ADMIN_EVT_ENDPOINT_REGISTERED;
    ev.addr  = addr;
    ev.ts_ms = 12345;
    rcp_admin_server_emit(srv, ev);

    TEST_ASSERT_EQUAL(1, g_event_count);
    TEST_ASSERT_EQUAL(RCP_ADMIN_EVT_ENDPOINT_REGISTERED, g_last_event.type);
    TEST_ASSERT_TRUE(rcp_avtp_addr_equal(addr, g_last_event.addr));
    TEST_ASSERT_EQUAL_UINT64(12345, g_last_event.ts_ms);

    rcp_admin_server_destroy(srv);
}

/* REQ-ADMIN-004's own "every subscriber... in registration order" clause:
 * the single-subscriber test above cannot distinguish "invokes the one
 * subscriber" from "invokes every subscriber in order" -- three
 * subscribers, each identified by its own user_data, log the order they
 * were actually called in. */
static int g_call_order[8];
static int g_call_order_len;

static void log_call_order(const rcp_admin_event_t *ev, void *user_data)
{
    (void)ev;
    g_call_order[g_call_order_len++] = (int)(intptr_t)user_data;
}

static void test_emit_invokes_every_subscriber_in_registration_order(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_admin_event_t   ev;

    g_call_order_len = 0;
    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, log_call_order, (void *)(intptr_t)1));
    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, log_call_order, (void *)(intptr_t)2));
    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, log_call_order, (void *)(intptr_t)3));

    ev.type  = RCP_ADMIN_EVT_ENDPOINT_REGISTERED;
    ev.addr  = make_addr(9, 2);
    ev.ts_ms = 1;
    rcp_admin_server_emit(srv, ev);

    TEST_ASSERT_EQUAL_INT(3, g_call_order_len);
    TEST_ASSERT_EQUAL_INT(1, g_call_order[0]);
    TEST_ASSERT_EQUAL_INT(2, g_call_order[1]);
    TEST_ASSERT_EQUAL_INT(3, g_call_order[2]);

    rcp_admin_server_destroy(srv);
}

/* ── Counters ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-ADMIN-005
static void test_record_counter_accumulates_each_delta_exactly_once(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();

    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.requests.total", "", 1.0));
    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.requests.total", "", 2.0));
    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.requests.total", "", 1.5));

    {
        char buf[256];
        rcp_admin_server_metrics_text(srv, buf, sizeof(buf));
        TEST_ASSERT_NOT_NULL(strstr(buf, "rcp.requests.total 4.5"));
    }

    rcp_admin_server_destroy(srv);
}

//cfusa:test REQ-ADMIN-007
static void test_distinct_name_labels_tracked_separately(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    char buf[512];

    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.requests.total", "endpoint=\"a\"", 1.0));
    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.requests.total", "endpoint=\"b\"", 5.0));

    rcp_admin_server_metrics_text(srv, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rcp.requests.total{endpoint=\"a\"} 1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rcp.requests.total{endpoint=\"b\"} 5"));

    rcp_admin_server_destroy(srv);
}

//cfusa:test REQ-ADMIN-006
static void test_metrics_text_renders_prometheus_format(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    char buf[256];
    size_t needed;

    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.requests.total", "", 3.0));

    needed = rcp_admin_server_metrics_text(srv, NULL, 0);
    TEST_ASSERT_TRUE(needed > 0);

    rcp_admin_server_metrics_text(srv, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "# TYPE rcp.requests.total counter"));

    rcp_admin_server_destroy(srv);
}

/* MC/DC: rcp_admin_server_metrics_text()'s `if (out && cap > 0)` (src/
 * admin.c) never had `cap > 0` independently demonstrated -- every
 * existing call in this file either passes a real, non-NULL buffer with
 * a real capacity (`cap > 0` true) or passes `out == NULL, cap == 0`
 * together (short-circuiting on the first operand, so the second is
 * never even reached). Holding `out` non-NULL constant and passing
 * `cap == 0` is what's missing: the copy-out must be skipped without
 * touching `out[0]` (which would be an out-of-bounds write into a
 * caller buffer the caller declared zero-capacity), while `total`
 * (the would-have-been-written length) is still computed and
 * returned exactly as the `out == NULL` path does. */
//cfusa:test REQ-ADMIN-006
static void test_metrics_text_with_non_null_buf_but_zero_cap_writes_nothing(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    char buf[256];
    size_t total;

    memset(buf, 'Z', sizeof(buf)); /* sentinel: must stay untouched */

    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.requests.total", "", 3.0));

    total = rcp_admin_server_metrics_text(srv, buf, 0);
    TEST_ASSERT_TRUE(total > 0);           /* same "would-have-written" length as out==NULL */
    TEST_ASSERT_EQUAL_UINT8('Z', (uint8_t)buf[0]); /* out non-NULL but cap==0: nothing written */

    rcp_admin_server_destroy(srv);
}

//cfusa:test REQ-ADMIN-006
static void test_metrics_text_with_long_name_and_labels_does_not_read_out_of_bounds(void)
{
    /* A 63-char name (embedded twice) plus a 127-char labels string plus a
       large-magnitude value formats to well over the 256-byte stack line
       buffer inside rcp_admin_server_metrics_text; this must be handled
       without reading past that buffer (run under ASan to catch a
       regression — this is what NEW-C-01 found missing). */
    rcp_admin_server_t *srv = rcp_admin_server_new();
    char long_name[64];
    char long_labels[128];
    char buf[8192];
    size_t needed;

    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    memset(long_labels, 'b', sizeof(long_labels) - 1);
    long_labels[sizeof(long_labels) - 1] = '\0';

    /* Unformatted, this line would be ~297 bytes -- more than the 256-byte
       stack buffer inside rcp_admin_server_metrics_text -- so each line is
       expected to be safely truncated, not fully preserved. The property
       under test is memory safety (no OOB read, verified by the ASan/UBSan
       CI job) and internally-consistent length accounting, not verbatim
       content preservation. */
    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, long_name, long_labels, 1e300));

    needed = rcp_admin_server_metrics_text(srv, NULL, 0);
    TEST_ASSERT_TRUE(needed > 0);

    rcp_admin_server_metrics_text(srv, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(needed, (unsigned)strlen(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "# TYPE "));
    TEST_ASSERT_NOT_NULL(strstr(buf, long_name));

    rcp_admin_server_destroy(srv);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

typedef struct {
    rcp_admin_server_t *srv;
    int                   idx;
} worker_args_t;

#if defined(_WIN32)
static DWORD WINAPI mutate_worker(void *arg)
#else
static void *mutate_worker(void *arg)
#endif
{
    worker_args_t *a = (worker_args_t *)arg;
    int i;

    for (i = 0; i < 500; i++) {
        rcp_avtp_addr_t addr = make_addr((uint16_t)(a->idx * 1000 + i), 0);
        (void)rcp_admin_server_register_endpoint(a->srv, addr);
        (void)rcp_admin_server_record_counter(a->srv, "rcp.requests.total", "", 1.0);
        (void)rcp_admin_server_deregister_endpoint(a->srv, addr);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

//cfusa:test REQ-ADMIN-008
static void test_admin_server_tolerates_concurrent_mutation(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    worker_args_t args[4];
    test_thread_t threads[4];
    int i;

    for (i = 0; i < 4; i++) {
        args[i].srv = srv;
        args[i].idx = i;
        threads[i] = test_thread_spawn(mutate_worker, &args[i]);
    }
    for (i = 0; i < 4; i++) test_thread_join(threads[i]);

    rcp_admin_server_destroy(srv); /* must not crash (ASan/TSan-checked in CI) */
}

/* ── [c-RCP-17] Fixed-capacity endpoint set / subscriber list / counter table ── */

//cfusa:test REQ-ADMIN-002
static void test_register_endpoint_at_max_succeeds_then_next_fails(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    rcp_endpoint_info_t out[RCP_ADMIN_MAX_ENDPOINTS];
    rcp_avtp_addr_t     overflow_addr;
    size_t              i;
    size_t              n;

    for (i = 0; i < RCP_ADMIN_MAX_ENDPOINTS; i++) {
        TEST_ASSERT_TRUE(rcp_admin_server_register_endpoint(srv, make_addr((uint16_t)(i + 1), 0)));
    }
    /* Last-registered endpoint is reachable -- confirms the fixed array was
     * fully populated, not silently truncated below capacity. */
    n = rcp_admin_server_endpoints(srv, out, RCP_ADMIN_MAX_ENDPOINTS);
    TEST_ASSERT_EQUAL_size_t(RCP_ADMIN_MAX_ENDPOINTS, n);

    /* One more, at capacity: rejected, not silently grown. */
    overflow_addr = make_addr((uint16_t)(RCP_ADMIN_MAX_ENDPOINTS + 1), 0);
    TEST_ASSERT_FALSE(rcp_admin_server_register_endpoint(srv, overflow_addr));

    rcp_admin_server_destroy(srv);
}

//cfusa:test REQ-ADMIN-003
static void test_subscribe_at_max_succeeds_then_next_fails(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    size_t               i;

    for (i = 0; i < RCP_ADMIN_MAX_SUBSCRIBERS; i++) {
        TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, count_events, NULL));
    }
    /* One more, at capacity: rejected, not silently grown. */
    TEST_ASSERT_FALSE(rcp_admin_server_subscribe(srv, count_events, NULL));

    rcp_admin_server_destroy(srv);
}

//cfusa:test REQ-ADMIN-005
static void test_record_counter_at_max_succeeds_then_next_new_one_fails(void)
{
    rcp_admin_server_t *srv = rcp_admin_server_new();
    char                 name[32];
    size_t               i;

    for (i = 0; i < RCP_ADMIN_MAX_COUNTERS; i++) {
        snprintf(name, sizeof(name), "counter_%zu", i);
        TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, name, "", 1.0));
    }
    /* A repeat delta against an already-tracked counter still succeeds at
     * capacity -- only a genuinely new (name, labels) pair is rejected. */
    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "counter_0", "", 1.0));

    /* One more, genuinely new, at capacity: rejected, not silently grown. */
    TEST_ASSERT_FALSE(rcp_admin_server_record_counter(srv, "one_too_many", "", 1.0));

    rcp_admin_server_destroy(srv);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_new_returns_a_server_with_no_endpoints_registered);
    RUN_TEST(test_new_returns_null_when_allocation_fails);
    RUN_TEST(test_destroy_tolerates_null);
    RUN_TEST(test_destroy_frees_srv_when_non_null);
    RUN_TEST(test_register_and_deregister_report_membership_changes);
    RUN_TEST(test_endpoints_returns_a_snapshot_of_registered_endpoints);
    RUN_TEST(test_endpoints_return_value_is_full_count_even_when_cap_truncates_output);
    RUN_TEST(test_subscribe_and_emit_deliver_the_correct_event);
    RUN_TEST(test_emit_invokes_every_subscriber_in_registration_order);
    RUN_TEST(test_record_counter_accumulates_each_delta_exactly_once);
    RUN_TEST(test_distinct_name_labels_tracked_separately);
    RUN_TEST(test_metrics_text_renders_prometheus_format);
    RUN_TEST(test_metrics_text_with_non_null_buf_but_zero_cap_writes_nothing);
    RUN_TEST(test_metrics_text_with_long_name_and_labels_does_not_read_out_of_bounds);
    RUN_TEST(test_admin_server_tolerates_concurrent_mutation);
    RUN_TEST(test_register_endpoint_at_max_succeeds_then_next_fails);
    RUN_TEST(test_subscribe_at_max_succeeds_then_next_fails);
    RUN_TEST(test_record_counter_at_max_succeeds_then_next_new_one_fails);

    return UNITY_END();
}
