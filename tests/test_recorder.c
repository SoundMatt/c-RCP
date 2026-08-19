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

#include <rcp/alloc.h>
#include <rcp/clock.h>
#include <rcp/recorder.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MC/DC gap closure for rcp_recorder_write_binary()'s per-field fault
 * path (CERT-C ERR33-C: every fwrite() return must be checked, not
 * assumed to succeed) needs a way to make a REAL fwrite() call fail at
 * a specific field, which is POSIX-only (RLIMIT_FSIZE/SIGXFSZ have no
 * Windows equivalent) -- see the block of helpers and tests below,
 * guarded accordingly, matching this file's existing #ifdef _WIN32
 * precedent for platform-specific test machinery. */
#if !defined(_WIN32)
#include <signal.h>
#include <sys/resource.h>
#endif

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

/* REQ-REC-001's `if (frame_len > 0 && !e.frame.data) return false;`
 * (recorder.c) needs BOTH conditions independently demonstrated:
 * frame_len==0 (short-circuits, never returns false here -- exercised
 * below by test_capture_and_write_binary_handle_zero_length_frame) and
 * frame_len>0 with a genuine rcp_bytes_dup() allocation failure (this
 * test) -- distinct from every existing capture test, which only ever
 * exercises frame_len>0 with allocation success. */
static void *always_fails_malloc(size_t size)
{
    (void)size;
    return NULL;
}

//cfusa:test REQ-REC-001
static void test_capture_fails_when_frame_dup_allocation_fails(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0xAA, 0xBB};
    rcp_alloc_hooks_t hooks = {0};

    hooks.malloc_fn = always_fails_malloc;
    rcp_alloc_set_hooks(&hooks);

    TEST_ASSERT_FALSE(rcp_recorder_capture(r, 1, make_addr(1, 0), true, frame, sizeof(frame)));

    rcp_alloc_reset_hooks();

    /* The failed capture must not have appended a corrupt/half-built
     * entry. */
    TEST_ASSERT_EQUAL_UINT(0, rcp_recorder_size(r));

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

//cfusa:test REQ-REC-001
//cfusa:test REQ-REC-003
static void test_capture_and_write_binary_handle_zero_length_frame(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    const char *path = "test_recorder_zero_frame.bin";
    FILE *f;
    long size;

    /* frame_len==0: REQ-REC-001's `frame_len > 0 && !e.frame.data`
     * short-circuits on the first condition and must NOT return false
     * here (unlike test_capture_fails_when_frame_dup_allocation_fails,
     * where frame_len>0 and the allocation itself fails). */
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, make_addr(1, 0), true, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, rcp_recorder_size(r));

    /* write_bytes()'s `len == 0 || fwrite(...) == len` (recorder.c)
     * must take the len==0 short-circuit branch and still report
     * overall success, writing exactly the fixed 23-byte header
     * (ts=8 + stream_id=8 + byte_bus=2 + inbound=1 + flen=4) with zero
     * frame bytes appended. */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_recorder_write_binary(r, path));

    f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    remove(path);
    TEST_ASSERT_EQUAL(23, size);

    rcp_recorder_destroy(r);
}

#if !defined(_WIN32)
/* ── write_binary() partial-write fault injection (POSIX only) ───────────────
 *
 * rcp_recorder_write_binary()'s per-entry write is
 *
 *     ok = write_field(f, &ts, ...)
 *       && write_field(f, &stream_id, ...)
 *       && write_field(f, &byte_bus, ...)
 *       && write_field(f, &inbound, ...)
 *       && write_field(f, &flen, ...)
 *       && write_bytes(f, e->frame.data, flen);
 *
 * a genuine six-condition short-circuit && chain (CERT-C ERR33-C:
 * every fwrite() return must be checked). Demonstrating each
 * condition's MC/DC independence means forcing a REAL fwrite()
 * failure at that SPECIFIC field -- not just "eventually, at
 * fclose()" -- which needs two real mechanisms working together:
 *
 *  1. RLIMIT_FSIZE (POSIX) makes any write that would grow a regular
 *     file past the limit fail with EFBIG instead of succeeding, with
 *     SIGXFSZ ignored so the process gets the error back instead of
 *     being killed by the default action.
 *
 *  2. Landing the failure on the exact field under test: libc fully
 *     buffers a freshly opened disk stream, so small fwrite() calls
 *     (ts/stream_id/byte_bus/inbound/flen -- 8+8+2+1+4 = 23 bytes
 *     total) merely copy into that userspace buffer and report
 *     success regardless of RLIMIT_FSIZE *until* the buffer is
 *     completely full and libc must actually flush to the OS.
 *     probe_stdio_flush_boundary() below empirically discovers that
 *     exact byte threshold at test run time (it is a libc/filesystem
 *     implementation detail -- 4096 on the platform this was
 *     validated on -- not a portable constant to hard-code), and a
 *     "padding" entry is captured first with a frame_len chosen so
 *     the buffer is left holding *exactly* enough bytes that the next
 *     entry's field-under-test is the one whose flush fails. That
 *     demonstrates the target field false while every field before it
 *     in the SAME evaluation instance genuinely succeeded, and every
 *     field after it is never reached at all (real short-circuiting,
 *     not simulated).
 */

/* Empirically finds the number of bytes that can be fwrite()'d to a
 * freshly opened, fully-buffered disk stream before libc must
 * actually flush to the OS -- i.e. the largest cumulative total that
 * still succeeds even under an RLIMIT_FSIZE far too small to permit
 * any real write to complete. */
static size_t probe_stdio_flush_boundary(void)
{
    const char *scratch = "test_recorder_probe.bin";
    struct rlimit rl_tiny, rl_orig;
    FILE *f;
    uint8_t byte = 0x55;
    size_t total = 0;

    /* Must restore the real limit before returning -- this runs before
     * every field-fail test, so leaving RLIMIT_FSIZE stuck at 1 byte
     * would corrupt process-wide state for the rest of the test binary
     * (including, e.g., this file's own LLVM profiling counters at
     * exit), not just this one probe. */
    getrlimit(RLIMIT_FSIZE, &rl_orig);
    rl_tiny.rlim_cur = 1;
    rl_tiny.rlim_max = RLIM_INFINITY;
    signal(SIGXFSZ, SIG_IGN);
    setrlimit(RLIMIT_FSIZE, &rl_tiny);

    f = fopen(scratch, "wb");
    TEST_ASSERT_NOT_NULL(f);
    for (;;) {
        if (fwrite(&byte, 1, 1, f) != 1) break;
        total++;
    }
    fclose(f);
    remove(scratch);

    setrlimit(RLIMIT_FSIZE, &rl_orig);
    return total;
}

/* Field sizes and write order, matching rcp_recorder_write_binary()'s
 * write_field() call sequence exactly: ts(8), stream_id(8),
 * byte_bus(2), inbound(1), flen(4). */
static const size_t k_header_field_sizes[5] = {8, 8, 2, 1, 4};
#define K_HEADER_TOTAL_SIZE 23 /* 8+8+2+1+4 */

static size_t cumulative_offset_before_field(int field_idx)
{
    size_t cum = 0;
    int i;
    for (i = 0; i < field_idx; i++) cum += k_header_field_sizes[i];
    return cum;
}

/* Captures a "padding" entry sized so the stdio buffer is left exactly
 * full up to the byte just before header field `field_idx` of a
 * SECOND entry, then a tiny RLIMIT_FSIZE is installed, write_binary()
 * is run, and the limit is restored. Returns write_binary()'s result. */
static int write_binary_failing_at_field(int field_idx)
{
    size_t boundary = probe_stdio_flush_boundary();
    size_t target_total = boundary - cumulative_offset_before_field(field_idx);
    size_t pad_frame_len = target_total - K_HEADER_TOTAL_SIZE;
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t *pad_frame = (uint8_t *)malloc(pad_frame_len);
    uint8_t small_frame[] = {0x01};
    struct rlimit rl_tiny, rl_orig;
    const char *path = "test_recorder_field_fail.bin";
    int rc;

    TEST_ASSERT_NOT_NULL(pad_frame);
    memset(pad_frame, 0x11, pad_frame_len);

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, make_addr(1, 0), true, pad_frame, pad_frame_len));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 2, make_addr(2, 0), true, small_frame, sizeof(small_frame)));
    free(pad_frame);

    getrlimit(RLIMIT_FSIZE, &rl_orig);
    rl_tiny.rlim_cur = 1;
    rl_tiny.rlim_max = RLIM_INFINITY;
    signal(SIGXFSZ, SIG_IGN);
    setrlimit(RLIMIT_FSIZE, &rl_tiny);

    rc = rcp_recorder_write_binary(r, path);

    setrlimit(RLIMIT_FSIZE, &rl_orig); /* never leak the rlimit across tests */
    remove(path);
    rcp_recorder_destroy(r);
    return rc;
}

//cfusa:test REQ-REC-003
static void test_write_binary_fails_when_timestamp_write_fails(void)
{
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, write_binary_failing_at_field(0));
}

//cfusa:test REQ-REC-003
static void test_write_binary_fails_when_stream_id_write_fails(void)
{
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, write_binary_failing_at_field(1));
}

//cfusa:test REQ-REC-003
static void test_write_binary_fails_when_byte_bus_write_fails(void)
{
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, write_binary_failing_at_field(2));
}

//cfusa:test REQ-REC-003
static void test_write_binary_fails_when_inbound_write_fails(void)
{
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, write_binary_failing_at_field(3));
}

//cfusa:test REQ-REC-003
static void test_write_binary_fails_when_flen_write_fails(void)
{
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, write_binary_failing_at_field(4));
}

/* Closes the sixth (frame-bytes) condition of the same && chain, plus
 * write_bytes()'s own `len == 0 || fwrite(...) == len` false-branch
 * for its second operand, plus rcp_recorder_write_binary()'s `for (i
 * = 0; i < r->len && ok; i++)` loop condition's `ok` term: a
 * megabyte-scale frame is large enough that libc's fwrite() bypasses
 * the small stdio buffer entirely and issues a direct write() syscall
 * for it, so RLIMIT_FSIZE fails THIS specific call (not merely
 * fclose()) while every field written before it for the same entry
 * genuinely succeeded. A second entry is captured (never reached,
 * since ok becomes false) purely so the loop re-checks `i < r->len`
 * as still true while `ok` is false -- demonstrating `ok` is what
 * actually stops the loop, not just `i < r->len` running out. */
//cfusa:test REQ-REC-003
//cfusa:test REQ-REC-010
static void test_write_binary_fails_when_frame_data_write_fails(void)
{
    size_t huge_len = 1u << 20; /* 1 MiB: comfortably larger than any
                                    realistic stdio buffer. */
    uint8_t *huge_frame = (uint8_t *)malloc(huge_len);
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t small_frame[] = {0x02};
    struct rlimit rl_tiny, rl_orig;
    const char *path = "test_recorder_huge_frame_fail.bin";
    int rc;

    TEST_ASSERT_NOT_NULL(huge_frame);
    memset(huge_frame, 0x22, huge_len);

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, make_addr(1, 0), true, huge_frame, huge_len));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 2, make_addr(2, 0), true, small_frame, sizeof(small_frame)));
    free(huge_frame);

    getrlimit(RLIMIT_FSIZE, &rl_orig);
    rl_tiny.rlim_cur = 1;
    rl_tiny.rlim_max = RLIM_INFINITY;
    signal(SIGXFSZ, SIG_IGN);
    setrlimit(RLIMIT_FSIZE, &rl_tiny);

    rc = rcp_recorder_write_binary(r, path);

    setrlimit(RLIMIT_FSIZE, &rl_orig);
    remove(path);
    rcp_recorder_destroy(r);

    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rc);
}
#endif /* !defined(_WIN32) */

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

/* rcp_playback_run_all()'s `if (snapshot[i].timestamp_ms > prev_ts &&
 * cfg.speed_factor > 0.0)` (recorder.c) needs both conditions
 * independently demonstrated. Every existing playback test holds one
 * side fixed at the value that keeps the whole && false:
 * test_playback_delivers_every_entry_in_order uses identical
 * timestamps (0,0,0), so "timestamp_ms > prev_ts" is always false;
 * test_speed_factor_zero_disables_delays uses speed_factor==0.0, so
 * "speed_factor > 0.0" is always false. Neither demonstrates the ||
 * ...err, &&, branch actually being TAKEN. This test uses a genuinely
 * increasing timestamp (so the first condition is true for the second
 * entry) together with the default positive speed_factor (so the
 * second condition is true too) -- paired against
 * test_playback_delivers_every_entry_in_order it demonstrates the
 * timestamp condition's independence (speed_factor held true in both,
 * timestamp condition flips outcome); paired against
 * test_speed_factor_zero_disables_delays it demonstrates the
 * speed_factor condition's independence (timestamp condition held
 * true in both, speed_factor flips outcome). The 1ms gap keeps
 * delay_ms <= 1, so `if (delay_ms > 1) rcp_sleep_ms(...)` is never
 * taken -- this test reaches the target branch without ever actually
 * sleeping. */
//cfusa:test REQ-REC-004
static void test_playback_evaluates_delay_condition_when_time_advances(void)
{
    rcp_recorder_t *r = rcp_recorder_new();
    uint8_t frame[] = {0x00};
    playback_ctx_t ctx = {0, 0};

    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 0, make_addr(1, 0), true, frame, 1));
    TEST_ASSERT_TRUE(rcp_recorder_capture(r, 1, make_addr(2, 0), true, frame, 1));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_playback_run_all(r, playback_deliver, &ctx, rcp_playback_default_config()));
    TEST_ASSERT_EQUAL(2, ctx.count);
    TEST_ASSERT_EQUAL_UINT64(1, ctx.last_ts);

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
    RUN_TEST(test_capture_fails_when_frame_dup_allocation_fails);
    RUN_TEST(test_entries_writes_at_most_cap_entries);
    RUN_TEST(test_entries_always_returns_true_total_count);
    RUN_TEST(test_write_binary_creates_a_non_empty_file);
    RUN_TEST(test_write_binary_returns_busy_when_path_unopenable);
    RUN_TEST(test_capture_and_write_binary_handle_zero_length_frame);
#if !defined(_WIN32)
    RUN_TEST(test_write_binary_fails_when_timestamp_write_fails);
    RUN_TEST(test_write_binary_fails_when_stream_id_write_fails);
    RUN_TEST(test_write_binary_fails_when_byte_bus_write_fails);
    RUN_TEST(test_write_binary_fails_when_inbound_write_fails);
    RUN_TEST(test_write_binary_fails_when_flen_write_fails);
    RUN_TEST(test_write_binary_fails_when_frame_data_write_fails);
#endif
    RUN_TEST(test_playback_default_config_values);
    RUN_TEST(test_playback_delivers_every_entry_in_order);
    RUN_TEST(test_playback_evaluates_delay_condition_when_time_advances);
    RUN_TEST(test_speed_factor_zero_disables_delays);
    RUN_TEST(test_record_tolerates_concurrent_captures);
    RUN_TEST(test_destroy_frees_every_captured_entry);

    return UNITY_END();
}
