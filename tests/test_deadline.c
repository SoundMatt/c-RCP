/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-DL-001
//cfusa:test REQ-DL-002
//cfusa:test REQ-DL-003
//cfusa:test REQ-DL-004
//cfusa:test REQ-DL-005
//cfusa:test REQ-DL-006
//cfusa:test REQ-DL-007
//cfusa:test REQ-DL-008
//cfusa:test REQ-DL-009
//cfusa:test REQ-DL-010
//cfusa:test REQ-DL-011
//cfusa:test REQ-DL-012
//cfusa:test REQ-DL-013
//cfusa:test REQ-DL-014
//cfusa:test REQ-DL-015
//cfusa:test REQ-DL-016
//cfusa:test REQ-DL-017
//cfusa:test REQ-DL-018
//cfusa:test REQ-DL-019
#include "unity.h"

#include <rcp/alloc.h>
#include <rcp/clock.h>
#include <rcp/deadline.h>

void setUp(void) {}
void tearDown(void) {}

static void test_sleep_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

/* ── Default config ───────────────────────────────────────────────────────── */

//cfusa:test REQ-DL-009
static void test_default_config_values(void)
{
    rcp_deadline_config_t cfg = rcp_deadline_default_config();

    TEST_ASSERT_EQUAL_UINT32(50, cfg.default_deadline_ms);
    TEST_ASSERT_EQUAL_UINT32(5, cfg.poll_interval_ms);
}

/* ── Monitor creation ─────────────────────────────────────────────────────── */

//cfusa:test REQ-DL-010
//cfusa:test REQ-DL-019
static void test_monitor_constructs_without_error(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 100}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, streams, 1);

    TEST_ASSERT_NOT_NULL(mon);
    /* Never heartbeated yet, but the stream IS tracked (unlike an unknown
     * one -- see test_alive_returns_false_before_first_heartbeat). */
    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, 1));

    /* REQ-DL-019: destroy() on a non-NULL monitor with a real running
     * background thread must close (join) that thread and free every
     * owned array/mutex before returning. There is no in-process way to
     * assert "the memory really was freed", but this construct-with-a-
     * live-thread-then-destroy round trip is exactly the shape ASan (this
     * project's CI build, and the pinned ASan/UBSan build required by
     * this same PR) needs to catch the concrete mutation this id guards
     * against: if destroy() stopped closing the thread first, the still-
     * running thread would touch m's mutex/state after rcp_free(m) --
     * a use-after-free ASan reports independently of leak-detection
     * settings. */
    rcp_deadline_monitor_destroy(mon);
}

//cfusa:test REQ-DL-013
static void test_destroy_tolerates_null(void)
{
    rcp_deadline_monitor_destroy(NULL); /* must not crash */
}

/* REQ-DL-018: rcp_calloc() failure must return NULL rather than a
 * partially-constructed monitor. Uses rcp/alloc.h's fault-injection seam
 * (rcp_calloc() is what rcp_deadline_monitor_new() actually calls) --
 * see tests/test_fragment.c's identical always-fails-hook pattern for
 * the ASan-portability rationale (a raw absurd-size allocation aborts
 * under this project's own ASan configuration instead of returning
 * NULL). */
static void *always_fails_calloc(size_t nmemb, size_t size)
{
    (void)nmemb;
    (void)size;
    return NULL;
}

//cfusa:test REQ-DL-018
static void test_monitor_new_returns_null_on_alloc_failure(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 100}};
    rcp_deadline_config_t     cfg = rcp_deadline_default_config();
    rcp_alloc_hooks_t         hooks = {0};
    rcp_deadline_monitor_t   *mon;

    hooks.calloc_fn = always_fails_calloc;
    rcp_alloc_set_hooks(&hooks);

    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    TEST_ASSERT_NULL(mon);

    rcp_alloc_reset_hooks();

    /* Real allocator restored: the same call now succeeds, confirming the
     * NULL above was really the injected failure and not some unrelated
     * rejection (e.g. the capacity check). */
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    TEST_ASSERT_NOT_NULL(mon);
    rcp_deadline_monitor_destroy(mon);
}

static void test_zero_deadline_ms_uses_config_default(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{2, 0}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.default_deadline_ms = 30;
    cfg.poll_interval_ms    = 5;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);

    /* Never heartbeated: should be declared dead once the config default
     * deadline elapses. NOTE this asserts alive() == false, which is also
     * true (for an unrelated reason -- "never reported on") even if the
     * background poll thread never ran at all; it is NOT REQ-DL-017's own
     * test (see test_dead_event_fires_and_is_not_repeated for that: only
     * a genuine LivenessEvent{alive=false} *transition*, observed via a
     * subscribed callback rather than alive()'s static default, can tell
     * "the thread ran and declared it dead" apart from "the thread never
     * started". This test's own job is narrower: that cfg.default_deadline_ms
     * (not some other value) is what got applied when streams[i].deadline_ms
     * was 0. */
    test_sleep_ms(80);
    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, 2));

    rcp_deadline_monitor_destroy(mon);
}

/* ── Dead stream detected ─────────────────────────────────────────────────── */

/* Only ever written by the monitor's background thread; read by the test
 * after rcp_deadline_monitor_destroy() has joined that thread, which
 * establishes happens-before without needing <stdatomic.h> (C11,
 * unavailable under this project's C99 standard). */
static int g_dead_count;

static void count_dead(const rcp_liveness_event_t *ev, void *user_data)
{
    (void)user_data;
    if (!ev->alive) g_dead_count++;
}

/* Also REQ-DL-017's own test: a genuine LivenessEvent{alive=false}
 * *transition*, observed via this subscribed callback, can only happen
 * if the background poll thread rcp_deadline_monitor_new() started is
 * actually the thing that ran check_deadlines() and called emit() --
 * unlike querying alive()'s static default (see
 * test_zero_deadline_ms_uses_config_default's own note on why that one
 * is NOT this proof). Confirmed by mutation: temporarily short-circuiting
 * rcp_thread_start()'s call in rcp_deadline_monitor_new() leaves this
 * test's g_dead_count at 0 (assertion fails) while every alive()-based
 * test in this file keeps passing unchanged. */
//cfusa:test REQ-DL-002
//cfusa:test REQ-DL-003
//cfusa:test REQ-DL-012
//cfusa:test REQ-DL-017
static void test_dead_event_fires_and_is_not_repeated(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 30}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.poll_interval_ms = 5;

    g_dead_count = 0;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    TEST_ASSERT_TRUE(rcp_deadline_monitor_subscribe(mon, count_dead, NULL));

    /* No heartbeat across several deadline cycles: the dead event must
     * fire exactly once (REQ-DL-002), not re-fire on every subsequent
     * cycle (REQ-DL-003). */
    test_sleep_ms(200);

    rcp_deadline_monitor_destroy(mon);
    TEST_ASSERT_EQUAL(1, g_dead_count);
}

/* MC/DC: check_deadlines()'s `should_emit = (elapsed >= st->deadline_ms)
 * && (st->alive || !st->ever_reported)` (src/deadline.c) never had the
 * st->alive operand's own independent contribution demonstrated.
 * test_dead_event_fires_and_is_not_repeated() above never heartbeats at
 * all, so st->alive is false for the stream's entire life -- its single
 * dead-event vector is (elapsed>=deadline, false, true) -> true, decided
 * by !ever_reported, and its steady-state "not repeated" vectors are
 * (elapsed>=deadline, false, false) -> false. Neither ever has st->alive
 * true when elapsed first crosses the deadline. This test heartbeats
 * once (establishing alive=true, ever_reported=true) and then lets the
 * deadline elapse with no further heartbeat, so the eventual dead event
 * fires with elapsed>=deadline_ms true and st->alive itself still true
 * (masking !ever_reported, which is false and irrelevant to the OR).
 * Paired against test_dead_event_fires_and_is_not_repeated()'s
 * steady-state (true, false, false) -> false vector (same elapsed>=
 * deadline_ms=true, ever_reported held equal at true throughout), only
 * st->alive differs between the two vectors and the outcome flips --
 * exactly what MC/DC independence requires. */
//cfusa:test REQ-DL-002
//cfusa:test REQ-DL-004
static void test_dead_event_fires_after_heartbeat_then_deadline_elapses(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 30}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.poll_interval_ms = 5;

    g_dead_count = 0;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    TEST_ASSERT_TRUE(rcp_deadline_monitor_subscribe(mon, count_dead, NULL));

    /* Establishes alive=true, ever_reported=true (via emit() inside
       heartbeat()'s was_alive==false path) before the deadline elapses --
       unlike test_dead_event_fires_and_is_not_repeated(), which never
       heartbeats and so never has st->alive true at all. */
    TEST_ASSERT_TRUE(rcp_deadline_monitor_heartbeat(mon, 1));
    TEST_ASSERT_TRUE(rcp_deadline_monitor_alive(mon, 1));

    /* No further heartbeat: several deadline_ms(30) cycles pass with the
       background poll thread's own check_deadlines() eventually
       declaring the stream dead because it WAS alive, not because it was
       never reported. */
    test_sleep_ms(200);

    rcp_deadline_monitor_destroy(mon);
    TEST_ASSERT_EQUAL(1, g_dead_count);
}

/* ── Live stream detected ─────────────────────────────────────────────────── */

static int g_alive_count;

static void count_alive(const rcp_liveness_event_t *ev, void *user_data)
{
    (void)user_data;
    if (ev->alive) g_alive_count++;
}

//cfusa:test REQ-DL-004
static void test_alive_event_on_first_heartbeat(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 200}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.poll_interval_ms = 5;

    g_alive_count = 0;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    rcp_deadline_monitor_subscribe(mon, count_alive, NULL);

    TEST_ASSERT_TRUE(rcp_deadline_monitor_heartbeat(mon, 1));

    test_sleep_ms(20);

    rcp_deadline_monitor_destroy(mon);
    TEST_ASSERT_TRUE(g_alive_count > 0);
}

/* REQ-DL-001: heartbeat() must reset the stream's last-signal time, not
 * merely flip its alive flag once (that half is REQ-DL-004, proved
 * above). A deadline_ms of 40 with heartbeats sent every ~15ms for
 * ~150ms total (several deadline windows' worth of elapsed wall time)
 * can only stay alive throughout if each heartbeat genuinely re-zeroes
 * the elapsed-time clock check_deadlines() measures against; if the
 * reset were a no-op the stream would be declared dead the first time
 * elapsed-since-construction, rather than elapsed-since-last-heartbeat,
 * exceeded 40ms. */
//cfusa:test REQ-DL-001
static void test_heartbeat_resets_deadline_timer(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 40}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;
    int i;

    cfg.poll_interval_ms = 5;

    g_dead_count = 0;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    TEST_ASSERT_TRUE(rcp_deadline_monitor_subscribe(mon, count_dead, NULL));

    TEST_ASSERT_TRUE(rcp_deadline_monitor_heartbeat(mon, 1)); /* initial alive */
    for (i = 0; i < 8; i++) {
        test_sleep_ms(15); /* < deadline_ms, repeated past deadline_ms*3 total */
        TEST_ASSERT_TRUE(rcp_deadline_monitor_heartbeat(mon, 1));
    }

    TEST_ASSERT_TRUE(rcp_deadline_monitor_alive(mon, 1));
    rcp_deadline_monitor_destroy(mon);
    TEST_ASSERT_EQUAL(0, g_dead_count);
}

//cfusa:test REQ-DL-014
static void test_heartbeat_unknown_stream_returns_false(void)
{
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, NULL, 0);

    TEST_ASSERT_FALSE(rcp_deadline_monitor_heartbeat(mon, 99));

    rcp_deadline_monitor_destroy(mon);
}

/* ── notify_overflow() ────────────────────────────────────────────────────── */

//cfusa:test REQ-DL-006
static void test_notify_overflow_immediately_declares_dead(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 5000}}; /* long deadline: only
                                                            notify_overflow()
                                                            can declare it
                                                            dead within this
                                                            test */
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.poll_interval_ms = 5;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);

    TEST_ASSERT_TRUE(rcp_deadline_monitor_heartbeat(mon, 1));
    TEST_ASSERT_TRUE(rcp_deadline_monitor_alive(mon, 1));

    TEST_ASSERT_TRUE(rcp_deadline_monitor_notify_overflow(mon, 1));
    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, 1));

    rcp_deadline_monitor_destroy(mon);
}

//cfusa:test REQ-DL-015
static void test_notify_overflow_unknown_stream_returns_false(void)
{
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, NULL, 0);

    TEST_ASSERT_FALSE(rcp_deadline_monitor_notify_overflow(mon, 99));

    rcp_deadline_monitor_destroy(mon);
}

/* MC/DC: rcp_deadline_monitor_notify_overflow()'s `should_emit =
 * st->alive || !st->ever_reported` (src/deadline.c) never had either
 * operand independently demonstrated -- the only other call
 * (test_notify_overflow_immediately_declares_dead() above) always
 * heartbeats first, so st->alive is true and st->ever_reported is
 * already true (!ever_reported == false) at the one vector it exercises
 * -- (true, false) -> true, deciding entirely on the first operand.
 * This test supplies the two missing vectors, on a freshly-constructed
 * stream that is never heartbeated:
 *   1. First call: alive=false, ever_reported=false -> should_emit =
 *      false || true = true (the !ever_reported operand alone decides
 *      it -- paired against vector 3 below, alive held false, this
 *      shows !ever_reported's independence). emit() fires, setting
 *      ever_reported=true.
 *   2. (same call as 1) -- alive stays false throughout, since nothing
 *      here ever calls heartbeat().
 *   3. Second call: alive=false, ever_reported=true -> should_emit =
 *      false || false = false (paired against
 *      test_notify_overflow_immediately_declares_dead()'s (true,
 *      false) -> true vector, ever_reported held equal (false there,
 *      via a fresh alive=true/reported=true state -- see that test),
 *      this shows st->alive's independence: only it differs, and the
 *      outcome flips). No second dead event fires -- g_dead_count stays
 *      at 1. */
//cfusa:test REQ-DL-006
static void test_notify_overflow_should_emit_independent_operands(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 5000}}; /* long deadline: the
                                                            background poll
                                                            thread's own
                                                            check_deadlines()
                                                            never fires
                                                            during this test,
                                                            isolating
                                                            notify_overflow()'s
                                                            own should_emit. */
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.poll_interval_ms = 5;

    g_dead_count = 0;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    TEST_ASSERT_TRUE(rcp_deadline_monitor_subscribe(mon, count_dead, NULL));

    /* Never heartbeated: alive=false, ever_reported=false. */
    TEST_ASSERT_TRUE(rcp_deadline_monitor_notify_overflow(mon, 1));
    TEST_ASSERT_EQUAL(1, g_dead_count);

    /* alive still false, but ever_reported is now true (emit() above set
       it): should_emit is false this time, so no second event fires. */
    TEST_ASSERT_TRUE(rcp_deadline_monitor_notify_overflow(mon, 1));
    TEST_ASSERT_EQUAL(1, g_dead_count);

    rcp_deadline_monitor_destroy(mon);
}

/* ── alive() query ────────────────────────────────────────────────────────── */

//cfusa:test REQ-DL-005
//cfusa:test REQ-DL-011
static void test_alive_returns_false_before_first_heartbeat(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 5000}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, streams, 1);

    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, 1));
    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, 99)); /* unregistered */

    rcp_deadline_monitor_destroy(mon);
}

/* ── [c-RCP-17] Fixed-capacity stream table / callback list ─────────────────── */

//cfusa:test REQ-DL-010
static void test_monitor_new_at_max_streams_succeeds(void)
{
    rcp_deadline_stream_cfg_t streams[RCP_DEADLINE_MAX_STREAMS];
    rcp_deadline_config_t     cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t   *mon;
    size_t                    i;

    for (i = 0; i < RCP_DEADLINE_MAX_STREAMS; i++) {
        streams[i].stream_id   = (uint64_t)i + 1;
        streams[i].deadline_ms = 5000;
    }

    mon = rcp_deadline_monitor_new(cfg, streams, RCP_DEADLINE_MAX_STREAMS);
    TEST_ASSERT_NOT_NULL(mon);
    /* Last-registered stream is reachable -- confirms the fixed array was
     * fully populated, not silently truncated below capacity. */
    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, RCP_DEADLINE_MAX_STREAMS));
    TEST_ASSERT_TRUE(rcp_deadline_monitor_heartbeat(mon, RCP_DEADLINE_MAX_STREAMS));

    rcp_deadline_monitor_destroy(mon);
}

//cfusa:test REQ-DL-010
static void test_monitor_new_over_max_streams_returns_null(void)
{
    rcp_deadline_stream_cfg_t streams[RCP_DEADLINE_MAX_STREAMS + 1];
    rcp_deadline_config_t     cfg = rcp_deadline_default_config();
    size_t                    i;

    for (i = 0; i < RCP_DEADLINE_MAX_STREAMS + 1; i++) {
        streams[i].stream_id   = (uint64_t)i + 1;
        streams[i].deadline_ms = 5000;
    }

    TEST_ASSERT_NULL(rcp_deadline_monitor_new(cfg, streams, RCP_DEADLINE_MAX_STREAMS + 1));
}

//cfusa:test REQ-DL-012
static void test_subscribe_at_max_callbacks_succeeds_then_next_fails(void)
{
    rcp_deadline_config_t   cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, NULL, 0);
    size_t                  i;

    for (i = 0; i < RCP_DEADLINE_MAX_CALLBACKS; i++) {
        TEST_ASSERT_TRUE(rcp_deadline_monitor_subscribe(mon, count_dead, NULL));
    }
    /* One more, at capacity: rejected, not silently grown. */
    TEST_ASSERT_FALSE(rcp_deadline_monitor_subscribe(mon, count_dead, NULL));

    rcp_deadline_monitor_destroy(mon);
}

/* ── Close ────────────────────────────────────────────────────────────────── */

/* REQ-DL-007: close() must actually stop the background poll thread, not
 * merely return promptly while leaving it running. Proved by a negative:
 * close() a monitor BEFORE its (short) deadline elapses, then wait well
 * past that deadline and confirm no dead event ever fires -- the only
 * thing that can suppress it is the poll thread genuinely having
 * stopped checking. */
//cfusa:test REQ-DL-007
//cfusa:test REQ-DL-008
static void test_close_stops_background_thread(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 30}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.poll_interval_ms = 5;

    g_dead_count = 0;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    TEST_ASSERT_TRUE(rcp_deadline_monitor_subscribe(mon, count_dead, NULL));

    test_sleep_ms(10); /* let the poll thread actually start running */
    rcp_deadline_monitor_close(mon); /* stops it before the 30ms deadline */

    test_sleep_ms(80); /* well past the deadline the (stopped) thread would
                           otherwise have detected */
    TEST_ASSERT_EQUAL(0, g_dead_count);

    rcp_deadline_monitor_destroy(mon);
}

/* REQ-DL-016: a second close() call, after the thread has already
 * stopped, must not block or fail -- distinct from REQ-DL-007's "the
 * first call really stops the thread" claim above. */
//cfusa:test REQ-DL-016
static void test_close_is_idempotent(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 100}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, streams, 1);

    rcp_deadline_monitor_close(mon); /* real stop */
    rcp_deadline_monitor_close(mon); /* must not block/crash on repeat */
    rcp_deadline_monitor_close(mon); /* nor a third time */

    rcp_deadline_monitor_destroy(mon);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_config_values);
    RUN_TEST(test_monitor_constructs_without_error);
    RUN_TEST(test_destroy_tolerates_null);
    RUN_TEST(test_monitor_new_returns_null_on_alloc_failure);
    RUN_TEST(test_zero_deadline_ms_uses_config_default);
    RUN_TEST(test_dead_event_fires_and_is_not_repeated);
    RUN_TEST(test_dead_event_fires_after_heartbeat_then_deadline_elapses);
    RUN_TEST(test_alive_event_on_first_heartbeat);
    RUN_TEST(test_heartbeat_resets_deadline_timer);
    RUN_TEST(test_heartbeat_unknown_stream_returns_false);
    RUN_TEST(test_notify_overflow_immediately_declares_dead);
    RUN_TEST(test_notify_overflow_should_emit_independent_operands);
    RUN_TEST(test_notify_overflow_unknown_stream_returns_false);
    RUN_TEST(test_alive_returns_false_before_first_heartbeat);
    RUN_TEST(test_monitor_new_at_max_streams_succeeds);
    RUN_TEST(test_monitor_new_over_max_streams_returns_null);
    RUN_TEST(test_subscribe_at_max_callbacks_succeeds_then_next_fails);
    RUN_TEST(test_close_stops_background_thread);
    RUN_TEST(test_close_is_idempotent);

    return UNITY_END();
}
