//cfusa:test REQ-REC-001
//cfusa:test REQ-REC-002
//cfusa:test REQ-REC-003
//cfusa:test REQ-REC-004
//cfusa:test REQ-REC-005
//cfusa:test REQ-REC-006
//cfusa:test REQ-REC-007
//cfusa:test REQ-REC-008
//cfusa:test REQ-REC-009
//cfusa:test REQ-REC-010
//cfusa:test REQ-REC-011
#include "unity.h"

#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <rcp/record.h>

#include <stdio.h>

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

static void test_recording_controller_captures_entries(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_record_entry_t entries[1];

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    TEST_ASSERT_EQUAL_UINT(1, rcp_record_size(rec));
    TEST_ASSERT_EQUAL_UINT(1, rcp_record_entries(rec, entries, 1));
    TEST_ASSERT_EQUAL(RCP_CMD_GET, entries[0].cmd.type);
    TEST_ASSERT_EQUAL(RCP_OK, entries[0].error);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

static void test_multiple_sends_produce_sequential_entries(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);
    rcp_context_t ctx = rcp_context_background();
    int i;

    for (i = 0; i < 3; i++) {
        rcp_command_t cmd = {0};
        rcp_response_t resp = {0};
        cmd.zone = RCP_ZONE_FRONT_LEFT;
        cmd.type = RCP_CMD_SET;
        (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
        rcp_response_free(&resp);
    }

    TEST_ASSERT_EQUAL_UINT(3, rcp_record_size(rec));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

static void test_write_binary_creates_file(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    const char *path = "rcp_test_record.bin";
    FILE *f;
    long size;

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
    rcp_response_free(&resp);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_record_write_binary(rec, path));

    f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    remove(path);
    TEST_ASSERT_TRUE(size > 0);

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

static void test_entry_timestamps_are_monotonically_non_decreasing(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);
    rcp_context_t ctx = rcp_context_background();
    rcp_record_entry_t entries[20];
    size_t n;
    size_t i;
    int j;

    for (j = 0; j < 20; j++) {
        rcp_command_t cmd = {0};
        rcp_response_t resp = {0};
        cmd.zone = RCP_ZONE_FRONT_LEFT;
        cmd.type = RCP_CMD_SET;
        (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
        rcp_response_free(&resp);
    }

    n = rcp_record_entries(rec, entries, 20);
    TEST_ASSERT_EQUAL_UINT(20, n);
    for (i = 1; i < n; i++) {
        TEST_ASSERT_TRUE(entries[i].timestamp_ms >= entries[i - 1].timestamp_ms);
    }

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

static void test_forwards_inner_send_result_unchanged(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_controller_t *ctrl;
    rcp_record_entry_t entries[1];

    rcp_controller_close(inner); /* closed inner returns RCP_ERR_CLOSED */
    ctrl = rcp_record_controller_new(inner, rec);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(ctrl, &ctx, &cmd, &resp)); /* result passed through verbatim */
    TEST_ASSERT_EQUAL_UINT(1, rcp_record_size(rec));
    rcp_record_entries(rec, entries, 1);
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, entries[0].error); /* and captured in the log */

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
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

static void test_record_tolerates_concurrent_appends(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_CENTRAL);
    test_thread_t threads[KTHREADS];
    int i;

    g_ctrl = rcp_record_controller_new(inner, rec);

    for (i = 0; i < KTHREADS; i++) threads[i] = test_thread_spawn(send_worker, NULL);
    for (i = 0; i < KTHREADS; i++) test_thread_join(threads[i]);

    TEST_ASSERT_EQUAL_UINT(KTHREADS * KPER_THREAD, rcp_record_size(rec));

    rcp_controller_release(g_ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

/* ── Playback ─────────────────────────────────────────────────────────────── */

static void test_playback_replays_entries_against_target(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_controller_t *target;
    rcp_playback_config_t cfg = rcp_playback_default_config();

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
    rcp_response_free(&resp);

    TEST_ASSERT_EQUAL_UINT(1, rcp_record_size(rec));

    target = make_mock(RCP_ZONE_FRONT_LEFT);
    cfg.speed_factor = 0.0; /* no delays */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_playback_run_all(target, rec, &ctx, cfg));

    rcp_controller_release(target);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

static void test_zone_delegates_to_inner(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_REAR_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_LEFT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

static void test_subscribe_delegates_to_inner(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

static void test_close_delegates_to_inner(void)
{
    rcp_record_t *rec = rcp_record_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_record_controller_new(inner, rec);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(inner, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_record_destroy(rec);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_recording_controller_captures_entries);
    RUN_TEST(test_multiple_sends_produce_sequential_entries);
    RUN_TEST(test_write_binary_creates_file);
    RUN_TEST(test_entry_timestamps_are_monotonically_non_decreasing);
    RUN_TEST(test_forwards_inner_send_result_unchanged);
    RUN_TEST(test_record_tolerates_concurrent_appends);
    RUN_TEST(test_playback_replays_entries_against_target);
    RUN_TEST(test_zone_delegates_to_inner);
    RUN_TEST(test_subscribe_delegates_to_inner);
    RUN_TEST(test_close_delegates_to_inner);

    return UNITY_END();
}
