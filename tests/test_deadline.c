/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-DL-001
//cfusa:test REQ-DL-002
//cfusa:test REQ-DL-003
//cfusa:test REQ-DL-004
//cfusa:test REQ-DL-005
//cfusa:test REQ-DL-006
//cfusa:test REQ-DL-007
//cfusa:test REQ-DL-008
#include "unity.h"

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

/* ── Monitor creation ─────────────────────────────────────────────────────── */

static void test_monitor_constructs_without_error(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 100}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, streams, 1);

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
     * deadline elapses. */
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

//cfusa:test REQ-DL-002
//cfusa:test REQ-DL-003
static void test_dead_event_fires_and_is_not_repeated(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 30}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.poll_interval_ms = 5;

    g_dead_count = 0;
    mon = rcp_deadline_monitor_new(cfg, streams, 1);
    rcp_deadline_monitor_subscribe(mon, count_dead, NULL);

    /* No heartbeat across several deadline cycles: the dead event must
     * fire exactly once (REQ-DL-002), not re-fire on every subsequent
     * cycle (REQ-DL-003). */
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

//cfusa:test REQ-DL-001
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

static void test_notify_overflow_unknown_stream_returns_false(void)
{
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, NULL, 0);

    TEST_ASSERT_FALSE(rcp_deadline_monitor_notify_overflow(mon, 99));

    rcp_deadline_monitor_destroy(mon);
}

/* ── alive() query ────────────────────────────────────────────────────────── */

//cfusa:test REQ-DL-005
static void test_alive_returns_false_before_first_heartbeat(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 5000}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, streams, 1);

    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, 1));
    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, 99)); /* unregistered */

    rcp_deadline_monitor_destroy(mon);
}

/* ── Close ────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-DL-007
//cfusa:test REQ-DL-008
static void test_close_stops_background_thread(void)
{
    rcp_deadline_stream_cfg_t streams[] = {{1, 100}};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon = rcp_deadline_monitor_new(cfg, streams, 1);

    rcp_deadline_monitor_close(mon); /* must return promptly, not hang */
    rcp_deadline_monitor_close(mon); /* idempotent */

    rcp_deadline_monitor_destroy(mon);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_monitor_constructs_without_error);
    RUN_TEST(test_zero_deadline_ms_uses_config_default);
    RUN_TEST(test_dead_event_fires_and_is_not_repeated);
    RUN_TEST(test_alive_event_on_first_heartbeat);
    RUN_TEST(test_heartbeat_unknown_stream_returns_false);
    RUN_TEST(test_notify_overflow_immediately_declares_dead);
    RUN_TEST(test_notify_overflow_unknown_stream_returns_false);
    RUN_TEST(test_alive_returns_false_before_first_heartbeat);
    RUN_TEST(test_close_stops_background_thread);

    return UNITY_END();
}
