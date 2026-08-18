/* SPDX-License-Identifier: MPL-2.0 */
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
//cfusa:test REQ-REC-012
//cfusa:test REQ-REC-013
//cfusa:test REQ-REC-014
//cfusa:test REQ-REC-015
//cfusa:test REQ-REC-016
#include "unity.h"

#include "../src/mem_bounded.h"

#include <rcp/clock.h>
#include <rcp/recorder.h>

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
    uint8_t mac[6] = {0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
    rcp_avtp_addr_t a;
    a.stream_id   = rcp_stream_id_make(mac, unique_id);
    a.byte_bus_id = byte_bus_id;
    return a;
}

/* ── capture() ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-REC-001
//cfusa:test REQ-REC-012
//cfusa:test REQ-REC-013
static void test_capture_appends_an_entry(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x0E, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC};

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 100, make_addr(1, 0), true, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT(1, rcp_recorder_size(r));

    rcp_recorder_destroy(r);
}

//cfusa:test REQ-REC-002
static void test_multiple_captures_produce_sequential_entries(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x01};
    rcp_recorder_entry_t out[3];

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 10, make_addr(1, 0), true, frame, 1));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 20, make_addr(2, 0), false, frame, 1));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 30, make_addr(3, 0), true, frame, 1));

    TEST_ASSERT_EQUAL_UINT(3, rcp_recorder_entries(r, out, 3));
    TEST_ASSERT_EQUAL_UINT64(10, out[0].timestamp_ms);
    TEST_ASSERT_EQUAL_UINT64(20, out[1].timestamp_ms);
    TEST_ASSERT_EQUAL_UINT64(30, out[2].timestamp_ms);

    rcp_recorder_destroy(r);
}

/* REQ-REC-006 (byte-for-byte copy correctness) and REQ-REC-015 (the
 * stored copy is immune to later mutation of the caller's buffer) were
 * split from one bundled requirement by the c-RCP-18 requirement-atomicity
 * audit (issue #533): a shallow-copy bug (aliasing `frame` instead of
 * duplicating it) would fail only the immunity test below, while a
 * truncated/off-by-one copy would fail only the correctness test here --
 * distinct failure modes, so each gets its own id and its own assertion,
 * checked at a different point relative to the caller's mutation. */

//cfusa:test REQ-REC-006
static void test_capture_copies_frame_bytes_correctly(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[4];
    rcp_recorder_entry_t out;

    rcp_memcpy_bounded(frame, sizeof(frame), "\x01\x02\x03\x04", 4);
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, make_addr(1, 0), true, frame, 4));

    /* Checked immediately, before any mutation of the caller's buffer --
     * this isolates "the copy is byte-for-byte correct" from
     * REQ-REC-015's separate mutation-immunity contract below. */
    rcp_recorder_entries(r, &out, 1);
    TEST_ASSERT_EQUAL_UINT(4, out.frame.len);
    TEST_ASSERT_EQUAL_UINT8(0x01, out.frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, out.frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out.frame.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0x04, out.frame.data[3]);

    rcp_recorder_destroy(r);
}

//cfusa:test REQ-REC-015
static void test_capture_stored_entry_immune_to_caller_mutation(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[4];
    rcp_recorder_entry_t out;

    rcp_memcpy_bounded(frame, sizeof(frame), "\x01\x02\x03\x04", 4);
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, make_addr(1, 0), true, frame, 4));

    /* Mutating the caller's own buffer after capture() must not affect
     * the stored entry -- it copied the bytes, not a reference. */
    memset(frame, 0xFF, sizeof(frame));

    rcp_recorder_entries(r, &out, 1);
    TEST_ASSERT_EQUAL_UINT8(0x01, out.frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, out.frame.data[3]);

    rcp_recorder_destroy(r);
}

//cfusa:test REQ-REC-007
static void test_capture_records_addr_and_inbound(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x00};
    rcp_avtp_addr_t addr = make_addr(7, 4);
    rcp_recorder_entry_t out;

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, addr, false, frame, 1));

    rcp_recorder_entries(r, &out, 1);
    TEST_ASSERT_TRUE(rcp_avtp_addr_equal(addr, out.addr));
    TEST_ASSERT_FALSE(out.inbound);

    rcp_recorder_destroy(r);
}

/* ── entries() cap handling ───────────────────────────────────────────────── */

/* REQ-REC-009 (writes at most cap entries into out) and REQ-REC-016
 * (always returns the true total, regardless of cap) were split from one
 * bundled requirement by the c-RCP-18 requirement-atomicity audit (issue
 * #533): a bug that writes past index cap-1 (buffer overflow) would fail
 * only the bound-check test below, while a bug that returns cap instead
 * of the true total when cap < total would fail only the count test --
 * distinct failure modes, each with its own id and its own assertion. */

//cfusa:test REQ-REC-009
static void test_entries_writes_at_most_cap_entries(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x00};
    rcp_recorder_entry_t out[2];
    rcp_recorder_entry_t sentinel;
    int i;

    /* Sentinel-fill out[] so a write past index cap-1 is directly
     * observable as a changed byte, not just an ASan catch. */
    memset(out, 0xAB, sizeof(out));
    memset(&sentinel, 0xAB, sizeof(sentinel));

    for (i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(rcp_recorder_capture(r, (uint64_t)i, make_addr((uint16_t)i, 0), true, frame, 1));
    }

    (void)rcp_recorder_entries(r, out, 1);

    TEST_ASSERT_EQUAL_UINT64(0, out[0].timestamp_ms);
    /* out[1] must remain the untouched sentinel -- entries() wrote at
     * most cap (1) entries, even though 5 were available to copy. */
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out[1], sizeof(sentinel));

    rcp_recorder_destroy(r);
}

//cfusa:test REQ-REC-016
static void test_entries_always_returns_true_total_count(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x00};
    rcp_recorder_entry_t out[1];
    int i;

    for (i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(rcp_recorder_capture(r, (uint64_t)i, make_addr((uint16_t)i, 0), true, frame, 1));
    }

    TEST_ASSERT_EQUAL_UINT(5, rcp_recorder_entries(r, out, 1));
    TEST_ASSERT_EQUAL_UINT(5, rcp_recorder_entries(r, out, 0));
    TEST_ASSERT_EQUAL_UINT(5, rcp_recorder_size(r));

    rcp_recorder_destroy(r);
}

/* ── write_binary() ───────────────────────────────────────────────────────── */

//cfusa:test REQ-REC-003
static void test_write_binary_creates_a_non_empty_file(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0xDE, 0xAD, 0xBE, 0xEF};
    const char *path = "test_recorder_output.bin";
    FILE *f;
    long size;

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, make_addr(1, 0), true, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_recorder_write_binary(r, path));

    f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    remove(path);

    TEST_ASSERT_TRUE(size > 0);

    rcp_recorder_destroy(r);
}

//cfusa:test REQ-REC-010
static void test_write_binary_returns_busy_when_path_unopenable(void)
{
    rcp_recorder_t *r = rcp_recorder_new();

    /* A path inside a nonexistent directory can never be opened for
     * writing on any supported platform. */
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY,
                       rcp_recorder_write_binary(r, "no_such_directory/out.bin"));

    rcp_recorder_destroy(r);
}

/* ── Playback ─────────────────────────────────────────────────────────────── */

typedef struct {
    int      count;
    uint64_t last_ts;
} playback_ctx_t;

static void playback_deliver(const rcp_recorder_entry_t *entry, void *user_data)
{
    playback_ctx_t *ctx = (playback_ctx_t *)user_data;
    ctx->count++;
    ctx->last_ts = entry->timestamp_ms;
}

//cfusa:test REQ-REC-014
static void test_playback_default_config_values(void)
{
    rcp_playback_config_t cfg = rcp_playback_default_config();

    /* This project's Unity build has double-precision asserts disabled
     * (embedded-target convention); compare directly instead. */
    TEST_ASSERT_TRUE(cfg.speed_factor == 1.0);
}

//cfusa:test REQ-REC-004
static void test_playback_delivers_every_entry_in_order(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x00};
    playback_ctx_t ctx = {0, 0};

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 0, make_addr(1, 0), true, frame, 1));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 0, make_addr(2, 0), true, frame, 1));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 0, make_addr(3, 0), true, frame, 1));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_playback_run_all(r, playback_deliver, &ctx, rcp_playback_default_config()));
    TEST_ASSERT_EQUAL(3, ctx.count);

    rcp_recorder_destroy(r);
}

//cfusa:test REQ-REC-005
static void test_speed_factor_zero_disables_delays(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x00};
    rcp_playback_config_t cfg;
    playback_ctx_t ctx = {0, 0};
    uint64_t start, elapsed;

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 0, make_addr(1, 0), true, frame, 1));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 5000, make_addr(2, 0), true, frame, 1));

    cfg.speed_factor = 0.0;
    start = rcp_monotonic_ms();
    TEST_ASSERT_EQUAL(RCP_OK, rcp_playback_run_all(r, playback_deliver, &ctx, cfg));
    elapsed = rcp_monotonic_ms() - start;

    TEST_ASSERT_EQUAL(2, ctx.count);
    TEST_ASSERT_TRUE(elapsed < 1000); /* would be ~5s at speed_factor 1.0 */

    rcp_recorder_destroy(r);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

typedef struct {
    rcp_recorder_t *r;
    int               idx;
} worker_args_t;

#if defined(_WIN32)
static DWORD WINAPI capture_worker(void *arg)
#else
static void *capture_worker(void *arg)
#endif
{
    worker_args_t *a = (worker_args_t *)arg;
    uint8_t frame[] = {0x00};
    int i;

    for (i = 0; i < 500; i++) {
        (void)rcp_recorder_capture(a->r, (uint64_t)i, make_addr((uint16_t)(a->idx * 1000 + i), 0), true, frame, 1);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

//cfusa:test REQ-REC-008
static void test_record_tolerates_concurrent_captures(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    worker_args_t args[4];
    test_thread_t threads[4];
    int i;

    for (i = 0; i < 4; i++) {
        args[i].r   = r;
        args[i].idx = i;
        threads[i] = test_thread_spawn(capture_worker, &args[i]);
    }
    for (i = 0; i < 4; i++) test_thread_join(threads[i]);

    TEST_ASSERT_EQUAL_UINT(4 * 500, rcp_recorder_size(r));

    rcp_recorder_destroy(r);
}

/* ── destroy() ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-REC-011
static void test_destroy_frees_every_captured_entry(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[64];
    int i;

    memset(frame, 0xAB, sizeof(frame));
    for (i = 0; i < 16; i++) {
        TEST_ASSERT_TRUE(rcp_recorder_capture(r, (uint64_t)i, make_addr((uint16_t)i, 0), true, frame, sizeof(frame)));
    }

    rcp_recorder_destroy(r); /* must not leak (ASan-checked in CI) */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_capture_appends_an_entry);
    RUN_TEST(test_multiple_captures_produce_sequential_entries);
    RUN_TEST(test_capture_copies_frame_bytes_correctly);
    RUN_TEST(test_capture_stored_entry_immune_to_caller_mutation);
    RUN_TEST(test_capture_records_addr_and_inbound);
    RUN_TEST(test_entries_writes_at_most_cap_entries);
    RUN_TEST(test_entries_always_returns_true_total_count);
    RUN_TEST(test_write_binary_creates_a_non_empty_file);
    RUN_TEST(test_write_binary_returns_busy_when_path_unopenable);
    RUN_TEST(test_playback_default_config_values);
    RUN_TEST(test_playback_delivers_every_entry_in_order);
    RUN_TEST(test_speed_factor_zero_disables_delays);
    RUN_TEST(test_record_tolerates_concurrent_captures);
    RUN_TEST(test_destroy_frees_every_captured_entry);

    return UNITY_END();
}
