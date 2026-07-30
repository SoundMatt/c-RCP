/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ADMIN-001
//cfusa:test REQ-ADMIN-002
//cfusa:test REQ-ADMIN-003
//cfusa:test REQ-ADMIN-004
//cfusa:test REQ-ADMIN-005
//cfusa:test REQ-ADMIN-006
//cfusa:test REQ-ADMIN-007
//cfusa:test REQ-ADMIN-008
#include "unity.h"

#include <rcp/admin.h>

#include <stdio.h>
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
    uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    rcp_avtp_addr_t a;
    a.stream_id   = rcp_stream_id_make(mac, unique_id);
    a.byte_bus_id = byte_bus_id;
    return a;
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

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_register_and_deregister_report_membership_changes);
    RUN_TEST(test_endpoints_returns_a_snapshot_of_registered_endpoints);
    RUN_TEST(test_subscribe_and_emit_deliver_the_correct_event);
    RUN_TEST(test_record_counter_accumulates_each_delta_exactly_once);
    RUN_TEST(test_distinct_name_labels_tracked_separately);
    RUN_TEST(test_metrics_text_renders_prometheus_format);
    RUN_TEST(test_metrics_text_with_long_name_and_labels_does_not_read_out_of_bounds);
    RUN_TEST(test_admin_server_tolerates_concurrent_mutation);

    return UNITY_END();
}
