//cfusa:test REQ-WDG-001
//cfusa:test REQ-WDG-002
//cfusa:test REQ-WDG-003
//cfusa:test REQ-WDG-004
//cfusa:test REQ-WDG-005
//cfusa:test REQ-WDG-006
//cfusa:test REQ-WDG-007
//cfusa:test REQ-WDG-008
//cfusa:test REQ-WDG-009
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/watchdog.h>

void setUp(void) {}
void tearDown(void) {}

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

/* ── Keeper creation ──────────────────────────────────────────────────────── */

static void test_keeper_constructs_with_zero_streams(void)
{
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *k  = rcp_watchdog_keeper_new(cfg, NULL, 0);

    TEST_ASSERT_NOT_NULL(k);
    rcp_watchdog_keeper_destroy(k);
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

    RUN_TEST(test_keeper_constructs_with_zero_streams);
    RUN_TEST(test_initial_status_established_synchronously);
    RUN_TEST(test_kick_unknown_stream_returns_false);
    RUN_TEST(test_status_unknown_stream_all_false);
    RUN_TEST(test_overflow_after_timeout_without_kick);
    RUN_TEST(test_disabled_watchdog_never_overflows);
    RUN_TEST(test_kick_resets_timer_prevents_overflow);
    RUN_TEST(test_notify_only_when_safestate_disabled);
    RUN_TEST(test_safestate_only_when_info_disabled);
    RUN_TEST(test_subscribe_fires_on_overflow);
    RUN_TEST(test_close_stops_background_thread);

    return UNITY_END();
}
