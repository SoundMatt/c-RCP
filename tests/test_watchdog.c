/* SPDX-License-Identifier: MPL-2.0 */
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/watchdog.h>

void setUp(void) {}
void tearDown(void) {}

/* MC/DC note (not a test): the static wd_result_equal() helper in
 * src/watchdog.c -- `a.overflowed == b.overflowed &&
 * a.enter_safe_state == b.enter_safe_state && a.notify == b.notify` --
 * has its second and third operands' independence undemonstrated, and
 * that independence is structurally unreachable through
 * evaluate_stream(), its one and only caller (grep confirms no other
 * call site exists anywhere in this repository).
 *
 * evaluate_stream() always calls wd_result_equal(result,
 * st->last_result), comparing a stream's freshly-computed
 * rcp_e2e_wd_result_t against that *same* stream's own previous one.
 * rcp_e2e_wd_evaluate() (src/e2e.c) derives all three result fields
 * deterministically from one boolean and two per-stream config
 * constants that never change after rcp_watchdog_keeper_new() (there is
 * no reconfigure API -- see include/rcp/watchdog.h):
 *
 *     overflowed       = rx_wd_enable && elapsed >= rx_wd_timeout_ms
 *     enter_safe_state = overflowed && rx_wd_safestate_enable   (const)
 *     notify           = overflowed && rx_wd_info_enable         (const)
 *
 * Because rx_wd_safestate_enable/rx_wd_info_enable are the same
 * constants in both the "a" and "b" results being compared (same
 * stream, same cfg), `a.overflowed == b.overflowed` being true
 * *guarantees* `a.enter_safe_state == b.enter_safe_state` and
 * `a.notify == b.notify` are true too -- there is no way for
 * `overflowed` to agree while either derived field disagrees. So
 * whenever the `&&` chain reaches its second or third operand (which
 * only happens when the first has already been true), that operand is
 * always true as well: it can never be the operand whose value flips
 * the overall result to false. No sequence of kick()/timeout/config
 * calls through the public rcp_watchdog_keeper_t API (the only way
 * anything reaches wd_result_equal()) can produce the missing vector,
 * because doing so would require overflowed to stay equal while a
 * *constant* derived from it changes -- a contradiction, not a gap in
 * test effort. No fake/whitebox test is added to force it.
 *
 * Re-confirmed under issue #604 (c-RCP-23b), which re-derived this same
 * proof from source with fresh eyes rather than trusting this comment
 * (or PR #584, which first reached this conclusion) at face value, then
 * went a step further with empirical mutation testing: with this file's
 * tests otherwise unchanged, each of (a) deleting the enter_safe_state
 * clause, (b) deleting the notify clause, and (c) inverting the
 * enter_safe_state comparison's polarity (== -> !=) in src/watchdog.c's
 * wd_result_equal() is an *undetected* mutant -- the full 67-test suite
 * still passes 67/67 against every one of the three. That is direct
 * empirical confirmation, not just analytical argument, that no
 * black-box test reachable through rcp_watchdog_keeper_t's public API
 * can ever supply the missing MC/DC vectors. See AUDIT_PACK.md's MC/DC
 * section for the full writeup. */

static void test_sleep_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

static rcp_watchdog_stream_cfg_t make_cfg(uint64_t stream_id, bool enable, uint32_t timeout_ms,
                                           bool safestate, bool info)
{
    rcp_watchdog_stream_cfg_t c;
    c.stream_id             = stream_id;
    c.rx_wd_enable           = enable;
    c.rx_wd_timeout_ms       = timeout_ms;
    c.rx_wd_safestate_enable = safestate;
    c.rx_wd_info_enable      = info;
    return c;
}

/* ── Default config ───────────────────────────────────────────────────────── */

//cfusa:test REQ-WDG-011
static void test_default_config_values(void)
{
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();

    TEST_ASSERT_EQUAL_UINT64(10, cfg.poll_interval_ms);
}

/* ── Keeper creation ──────────────────────────────────────────────────────── */

//cfusa:test REQ-WDG-012
static void test_keeper_constructs_with_zero_streams(void)
{
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k  = rcp_watchdog_keeper_new(cfg, NULL, 0);

    TEST_ASSERT_NOT_NULL(k);
    rcp_watchdog_keeper_destroy(k);
}

//cfusa:test REQ-WDG-012
static void test_keeper_destroy_tolerates_null(void)
{
    rcp_watchdog_keeper_destroy(NULL); /* must not crash */
}

//cfusa:test REQ-WDG-009
static void test_initial_status_established_synchronously(void)
{
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(1, true, 5000, true, true)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;
    rcp_e2e_wd_result_t status;

    cfg.poll_interval_ms = 1000; /* long enough that the background thread
                                     hasn't run its first cycle yet */
    k = rcp_watchdog_keeper_new(cfg, streams, 1);

    status = rcp_watchdog_keeper_status(k, 1);
    TEST_ASSERT_FALSE(status.overflowed);

    rcp_watchdog_keeper_destroy(k);
}

/* ── kick() ────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-WDG-003
static void test_kick_unknown_stream_returns_false(void)
{
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k  = rcp_watchdog_keeper_new(cfg, NULL, 0);

    TEST_ASSERT_FALSE(rcp_watchdog_keeper_kick(k, 42));

    rcp_watchdog_keeper_destroy(k);
}

//cfusa:test REQ-WDG-005
static void test_status_unknown_stream_all_false(void)
{
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k  = rcp_watchdog_keeper_new(cfg, NULL, 0);
    rcp_e2e_wd_result_t status = rcp_watchdog_keeper_status(k, 42);

    TEST_ASSERT_FALSE(status.overflowed);
    TEST_ASSERT_FALSE(status.enter_safe_state);
    TEST_ASSERT_FALSE(status.notify);

    rcp_watchdog_keeper_destroy(k);
}

/* ── Overflow behavior ────────────────────────────────────────────────────── */

static bool poll_for_overflow(rcp_watchdog_keeper_t *k, uint64_t stream_id, bool want)
{
    int elapsed_ms = 0;
    while (elapsed_ms < 5000) {
        if (rcp_watchdog_keeper_status(k, stream_id).overflowed == want) return true;
        test_sleep_ms(10);
        elapsed_ms += 10;
    }
    return false;
}

//cfusa:test REQ-WDG-001
//cfusa:test REQ-WDG-002
static void test_overflow_after_timeout_without_kick(void)
{
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(7, true, 20, true, true)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, streams, 1);

    TEST_ASSERT_TRUE(poll_for_overflow(k, 7, true));

    rcp_watchdog_keeper_destroy(k);
}

/* ── FTTI cross-check (c-RCP-22 Gap 5) ────────────────────────────────────── */

/*
 * .fusa-hara.json's H-001 (and H-003, which shares the same underlying
 * rcp_e2e_wd_evaluate() mechanism) records ftti_ms = 100 -- a claim about
 * how quickly the watchdog actually detects-and-reacts to a lost request,
 * asserted as HARA data but never previously cross-checked against a real
 * measured reaction time anywhere in this repo (the pre-existing
 * poll_for_overflow() above only bounds detection at a generous 5000ms,
 * roughly 50x the recorded FTTI -- sufficient to prove eventual detection,
 * not that the recorded FTTI value is honest). This test configures a real
 * rcp_watchdog_keeper_t with H-001's exact recorded ftti_ms as its
 * rx_wd_timeout_ms, measures actual wall-clock elapsed time to overflow
 * detection under real timing (test_sleep_ms's own busy-wait, not a mocked
 * clock), and asserts detection lands within a bounded window bracketing
 * that FTTI: not before it (REQ-E2E-025 requires elapsed >= timeout), and
 * not more than a fixed slack budget after it. The slack budget covers
 * fine poll_interval_ms granularity plus CI scheduling/ASan-instrumentation
 * jitter -- generous enough not to flake, but two orders of magnitude
 * tighter than the pre-existing 5000ms "eventually" bound, so this
 * actually constitutes a check of the recorded FTTI value, not just of
 * eventual detection.
 */
//cfusa:test REQ-E2E-025
//cfusa:test REQ-WDG-004
static void test_overflow_detected_within_recorded_ftti(void)
{
    const uint32_t ftti_ms    = 100; /* .fusa-hara.json hazards[] H-001.ftti_ms */
    const uint32_t slack_ms   = 300; /* poll granularity + CI/ASan jitter budget */
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(101, true, ftti_ms, true, true)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;
    uint64_t start, elapsed;

    cfg.poll_interval_ms = 2; /* fine-grained so poll granularity doesn't dominate */
    k = rcp_watchdog_keeper_new(cfg, streams, 1);

    start = rcp_monotonic_ms();
    TEST_ASSERT_TRUE(poll_for_overflow(k, 101, true));
    elapsed = rcp_monotonic_ms() - start;

    TEST_ASSERT_TRUE(elapsed >= ftti_ms);
    TEST_ASSERT_TRUE(elapsed <= (uint64_t)ftti_ms + slack_ms);

    rcp_watchdog_keeper_destroy(k);
}

//cfusa:test REQ-WDG-008
static void test_disabled_watchdog_never_overflows(void)
{
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(9, false, 10, true, true)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, streams, 1);

    test_sleep_ms(150);
    TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, 9).overflowed);

    rcp_watchdog_keeper_destroy(k);
}

static void test_kick_resets_timer_prevents_overflow(void)
{
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(3, true, 40, true, true)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;
    int i;

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, streams, 1);

    for (i = 0; i < 10; i++) {
        test_sleep_ms(10);
        TEST_ASSERT_TRUE(rcp_watchdog_keeper_kick(k, 3));
        TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, 3).overflowed);
    }

    rcp_watchdog_keeper_destroy(k);
}

/* ── notify/enter_safe_state independence ─────────────────────────────────── */

static void test_notify_only_when_safestate_disabled(void)
{
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(11, true, 20, false, true)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;
    rcp_e2e_wd_result_t status;

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, streams, 1);

    TEST_ASSERT_TRUE(poll_for_overflow(k, 11, true));
    status = rcp_watchdog_keeper_status(k, 11);
    TEST_ASSERT_TRUE(status.notify);
    TEST_ASSERT_FALSE(status.enter_safe_state);

    rcp_watchdog_keeper_destroy(k);
}

static void test_safestate_only_when_info_disabled(void)
{
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(12, true, 20, true, false)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;
    rcp_e2e_wd_result_t status;

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, streams, 1);

    TEST_ASSERT_TRUE(poll_for_overflow(k, 12, true));
    status = rcp_watchdog_keeper_status(k, 12);
    TEST_ASSERT_TRUE(status.enter_safe_state);
    TEST_ASSERT_FALSE(status.notify);

    rcp_watchdog_keeper_destroy(k);
}

/* ── Subscription ─────────────────────────────────────────────────────────── */

/* Only ever written by the keeper's background thread; read by the test
 * after rcp_watchdog_keeper_destroy() has joined that thread, which
 * establishes happens-before without needing <stdatomic.h> (C11,
 * unavailable under this project's C99 standard). */
static int g_event_count;
static bool g_last_overflowed;

static void count_events(const rcp_watchdog_event_t *ev, void *user_data)
{
    (void)user_data;
    g_event_count++;
    g_last_overflowed = ev->result.overflowed;
}

//cfusa:test REQ-WDG-006
static void test_subscribe_fires_on_overflow(void)
{
    rcp_watchdog_stream_cfg_t streams[] = {make_cfg(15, true, 20, true, true)};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;

    cfg.poll_interval_ms = 5;
    g_event_count = 0;
    g_last_overflowed = false;

    k = rcp_watchdog_keeper_new(cfg, streams, 1);
    TEST_ASSERT_TRUE(rcp_watchdog_keeper_subscribe(k, count_events, NULL));

    TEST_ASSERT_TRUE(poll_for_overflow(k, 15, true));

    rcp_watchdog_keeper_destroy(k);
    TEST_ASSERT_TRUE(g_event_count > 0);
    TEST_ASSERT_TRUE(g_last_overflowed);
}

/* ── [c-RCP-17] Fixed-capacity stream table / callback list ─────────────────── */

//cfusa:test REQ-WDG-009
static void test_keeper_new_at_max_streams_succeeds(void)
{
    rcp_watchdog_stream_cfg_t streams[RCP_WATCHDOG_MAX_STREAMS];
    rcp_watchdog_config_t     cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t    *k;
    size_t                    i;

    for (i = 0; i < RCP_WATCHDOG_MAX_STREAMS; i++) {
        streams[i] = make_cfg((uint64_t)i + 1, false, 1000, true, true);
    }

    k = rcp_watchdog_keeper_new(cfg, streams, RCP_WATCHDOG_MAX_STREAMS);
    TEST_ASSERT_NOT_NULL(k);
    /* Last-registered stream is reachable -- confirms the fixed array was
     * fully populated, not silently truncated below capacity. */
    TEST_ASSERT_TRUE(rcp_watchdog_keeper_kick(k, RCP_WATCHDOG_MAX_STREAMS));
    rcp_watchdog_keeper_destroy(k);
}

//cfusa:test REQ-WDG-009
static void test_keeper_new_over_max_streams_returns_null(void)
{
    rcp_watchdog_stream_cfg_t streams[RCP_WATCHDOG_MAX_STREAMS + 1];
    rcp_watchdog_config_t     cfg = rcp_watchdog_default_config();
    size_t                    i;

    for (i = 0; i < RCP_WATCHDOG_MAX_STREAMS + 1; i++) {
        streams[i] = make_cfg((uint64_t)i + 1, false, 1000, true, true);
    }

    TEST_ASSERT_NULL(rcp_watchdog_keeper_new(cfg, streams, RCP_WATCHDOG_MAX_STREAMS + 1));
}

//cfusa:test REQ-WDG-006
static void test_subscribe_at_max_callbacks_succeeds_then_next_fails(void)
{
    rcp_watchdog_config_t  cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k   = rcp_watchdog_keeper_new(cfg, NULL, 0);
    size_t                 i;

    for (i = 0; i < RCP_WATCHDOG_MAX_CALLBACKS; i++) {
        TEST_ASSERT_TRUE(rcp_watchdog_keeper_subscribe(k, count_events, NULL));
    }
    /* One more, at capacity: rejected, not silently grown. */
    TEST_ASSERT_FALSE(rcp_watchdog_keeper_subscribe(k, count_events, NULL));

    rcp_watchdog_keeper_destroy(k);
}

/* ── Close ─────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-WDG-007
static void test_close_stops_background_thread(void)
{
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k;

    cfg.poll_interval_ms = 50;
    k = rcp_watchdog_keeper_new(cfg, NULL, 0);

    rcp_watchdog_keeper_close(k); /* must return promptly, not hang */
    rcp_watchdog_keeper_close(k); /* idempotent */

    rcp_watchdog_keeper_destroy(k);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_config_values);
    RUN_TEST(test_keeper_constructs_with_zero_streams);
    RUN_TEST(test_keeper_destroy_tolerates_null);
    RUN_TEST(test_initial_status_established_synchronously);
    RUN_TEST(test_kick_unknown_stream_returns_false);
    RUN_TEST(test_status_unknown_stream_all_false);
    RUN_TEST(test_overflow_after_timeout_without_kick);
    RUN_TEST(test_overflow_detected_within_recorded_ftti);
    RUN_TEST(test_disabled_watchdog_never_overflows);
    RUN_TEST(test_kick_resets_timer_prevents_overflow);
    RUN_TEST(test_notify_only_when_safestate_disabled);
    RUN_TEST(test_safestate_only_when_info_disabled);
    RUN_TEST(test_subscribe_fires_on_overflow);
    RUN_TEST(test_keeper_new_at_max_streams_succeeds);
    RUN_TEST(test_keeper_new_over_max_streams_returns_null);
    RUN_TEST(test_subscribe_at_max_callbacks_succeeds_then_next_fails);
    RUN_TEST(test_close_stops_background_thread);

    return UNITY_END();
}
