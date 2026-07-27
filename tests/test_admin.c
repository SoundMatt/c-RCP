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
#include <rcp/mock.h>
#include <rcp/rcp.h>

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

void setUp(void) {}
void tearDown(void) {}

static void test_zones_lists_registered_controllers(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_admin_server_t *srv = rcp_admin_server_new(reg);
    rcp_zone_info_t zones[8];
    size_t n;
    size_t i;

    n = rcp_admin_server_zones(srv, zones, 8);
    TEST_ASSERT_EQUAL_UINT(5, n); /* mock registry pre-populates all 5 */
    for (i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(zones[i].registered);
    }

    rcp_admin_server_destroy(srv);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

typedef struct {
    rcp_admin_event_t events[8];
    size_t              count;
} event_log_t;

static void log_event(const rcp_admin_event_t *ev, void *user_data)
{
    event_log_t *log = (event_log_t *)user_data;
    if (log->count < 8) log->events[log->count] = *ev;
    log->count++;
}

static void test_subscribe_receives_emitted_events(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_admin_server_t *srv = rcp_admin_server_new(reg);
    event_log_t log = {{{0}}, 0};
    rcp_admin_event_t ev1;
    rcp_admin_event_t ev2;

    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, log_event, &log));

    ev1.type = RCP_ADMIN_EVT_ZONE_REGISTERED;
    ev1.zone = RCP_ZONE_FRONT_LEFT;
    ev1.ts_ms = 0;
    rcp_admin_server_emit(srv, ev1);

    ev2.type = RCP_ADMIN_EVT_STATUS_UPDATE;
    ev2.zone = RCP_ZONE_REAR_RIGHT;
    ev2.ts_ms = 0;
    rcp_admin_server_emit(srv, ev2);

    TEST_ASSERT_EQUAL_UINT(2, log.count);
    TEST_ASSERT_EQUAL(RCP_ADMIN_EVT_ZONE_REGISTERED, log.events[0].type);
    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, log.events[1].zone);

    rcp_admin_server_destroy(srv);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_metrics_text_contains_counter_lines(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_admin_server_t *srv = rcp_admin_server_new(reg);
    char buf[512];
    size_t len;

    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.commands.total", "zone=\"FrontLeft\"", 10.0));
    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.commands.total", "zone=\"FrontLeft\"", 5.0));

    len = rcp_admin_server_metrics_text(srv, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len < sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rcp.commands.total"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "15"));

    rcp_admin_server_destroy(srv);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_distinct_labels_tracked_separately(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_admin_server_t *srv = rcp_admin_server_new(reg);
    char buf[512];

    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.commands.total", "zone=\"FrontLeft\"", 10.0));
    TEST_ASSERT_TRUE(rcp_admin_server_record_counter(srv, "rcp.commands.total", "zone=\"RearRight\"", 3.0));

    rcp_admin_server_metrics_text(srv, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "FrontLeft"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "RearRight"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "10"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "3"));

    rcp_admin_server_destroy(srv);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_multiple_subscribers_all_receive_events(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_admin_server_t *srv = rcp_admin_server_new(reg);
    event_log_t log_a = {{{0}}, 0};
    event_log_t log_b = {{{0}}, 0};
    rcp_admin_event_t ev;

    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, log_event, &log_a));
    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, log_event, &log_b));

    ev.type = RCP_ADMIN_EVT_ZONE_DEREGISTERED;
    ev.zone = RCP_ZONE_CENTRAL;
    ev.ts_ms = 0;
    rcp_admin_server_emit(srv, ev);

    TEST_ASSERT_EQUAL_UINT(1, log_a.count);
    TEST_ASSERT_EQUAL_UINT(1, log_b.count);

    rcp_admin_server_destroy(srv);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_event_delivers_correct_type_and_zone(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_admin_server_t *srv = rcp_admin_server_new(reg);
    event_log_t log = {{{0}}, 0};
    rcp_admin_event_t ev;

    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(srv, log_event, &log));

    ev.type = RCP_ADMIN_EVT_STATUS_UPDATE;
    ev.zone = RCP_ZONE_REAR_LEFT;
    ev.ts_ms = 0;
    rcp_admin_server_emit(srv, ev);

    TEST_ASSERT_EQUAL_UINT(1, log.count);
    TEST_ASSERT_EQUAL(RCP_ADMIN_EVT_STATUS_UPDATE, log.events[0].type);
    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_LEFT, log.events[0].zone);

    rcp_admin_server_destroy(srv);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

#define KTHREADS 8
#define KPER_THREAD 1000

static rcp_admin_server_t *g_srv;
static volatile int g_events;

static void count_event(const rcp_admin_event_t *ev, void *user_data)
{
    (void)ev; (void)user_data;
    test_atomic_add(&g_events, 1);
}

#if defined(_WIN32)
static DWORD WINAPI admin_worker(void *arg)
#else
static void *admin_worker(void *arg)
#endif
{
    int i;
    (void)arg;
    for (i = 0; i < KPER_THREAD; i++) {
        rcp_admin_event_t ev;
        rcp_admin_server_record_counter(g_srv, "rcp.commands.total", "zone=\"FrontLeft\"", 1.0);
        ev.type = RCP_ADMIN_EVT_STATUS_UPDATE;
        ev.zone = RCP_ZONE_FRONT_LEFT;
        ev.ts_ms = 0;
        rcp_admin_server_emit(g_srv, ev);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_concurrent_record_counter_and_emit_are_thread_safe(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    test_thread_t threads[KTHREADS];
    int i;
    char buf[512];
    char expect[32];

    g_srv = rcp_admin_server_new(reg);
    g_events = 0;
    TEST_ASSERT_TRUE(rcp_admin_server_subscribe(g_srv, count_event, NULL));

    for (i = 0; i < KTHREADS; i++) threads[i] = test_thread_spawn(admin_worker, NULL);
    for (i = 0; i < KTHREADS; i++) test_thread_join(threads[i]);

    /* record_counter accumulates every delta exactly once (REQ-ADMIN-004) and
     * the server tolerates concurrent mutation without data races (REQ-ADMIN-005). */
    TEST_ASSERT_EQUAL(KTHREADS * KPER_THREAD, g_events);
    rcp_admin_server_metrics_text(g_srv, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rcp.commands.total"));
    snprintf(expect, sizeof(expect), "%d", KTHREADS * KPER_THREAD);
    TEST_ASSERT_NOT_NULL(strstr(buf, expect));

    rcp_admin_server_destroy(g_srv);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_zones_lists_registered_controllers);
    RUN_TEST(test_subscribe_receives_emitted_events);
    RUN_TEST(test_metrics_text_contains_counter_lines);
    RUN_TEST(test_distinct_labels_tracked_separately);
    RUN_TEST(test_multiple_subscribers_all_receive_events);
    RUN_TEST(test_event_delivers_correct_type_and_zone);
    RUN_TEST(test_concurrent_record_counter_and_emit_are_thread_safe);

    return UNITY_END();
}
