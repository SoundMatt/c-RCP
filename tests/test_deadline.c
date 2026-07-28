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
#include "legacy_mock.h"
#include <rcp/rcp.h>

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
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.deadline_ms = 100;
    mon = rcp_deadline_monitor_new(cfg, ctrls, 1);
    TEST_ASSERT_NOT_NULL(mon);

    rcp_deadline_monitor_destroy(mon);
    rcp_controller_release(ctrl);
}

/* ── Dead zone detected ───────────────────────────────────────────────────── */

/* Only ever written by the monitor's background watch thread; read by the
 * test after rcp_deadline_monitor_destroy() has joined that thread, which
 * establishes happens-before without needing <stdatomic.h> (C11,
 * unavailable under this project's C99 standard). */
static int g_dead_count;

static void count_dead(const rcp_liveness_event_t *ev, void *user_data)
{
    (void)user_data;
    if (!ev->alive) g_dead_count++;
}

static void test_dead_event_fires_and_is_not_repeated(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.deadline_ms = 30;

    g_dead_count = 0;
    mon = rcp_deadline_monitor_new(cfg, ctrls, 1);
    rcp_deadline_monitor_subscribe(mon, count_dead, NULL);

    /* No status published across several deadline cycles: the dead event
     * must fire exactly once (REQ-DL-002), not re-fire on every subsequent
     * cycle (REQ-DL-003). */
    test_sleep_ms(200);

    rcp_deadline_monitor_destroy(mon);
    TEST_ASSERT_EQUAL(1, g_dead_count);

    rcp_controller_release(ctrl);
}

/* ── Live zone detected ───────────────────────────────────────────────────── */

static int g_alive_count;

static void count_alive(const rcp_liveness_event_t *ev, void *user_data)
{
    (void)user_data;
    if (ev->alive) g_alive_count++;
}

static void test_alive_event_on_first_status(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.deadline_ms = 200;

    g_alive_count = 0;
    mon = rcp_deadline_monitor_new(cfg, ctrls, 1);
    rcp_deadline_monitor_subscribe(mon, count_alive, NULL);

    /* Let the monitor start its watch thread then publish a status. */
    test_sleep_ms(10);
    rcp_mock_controller_publish(ctrl, NULL, 0);

    test_sleep_ms(50);

    rcp_deadline_monitor_destroy(mon);
    TEST_ASSERT_TRUE(g_alive_count > 0);

    rcp_controller_release(ctrl);
}

/* ── alive() query ────────────────────────────────────────────────────────── */

static void test_alive_returns_false_before_first_status(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.deadline_ms = 5000; /* long timeout: no dead event will fire either */
    mon = rcp_deadline_monitor_new(cfg, ctrls, 1);

    TEST_ASSERT_FALSE(rcp_deadline_monitor_alive(mon, RCP_ZONE_FRONT_LEFT));

    rcp_deadline_monitor_destroy(mon);
    rcp_controller_release(ctrl);
}

/* ── Close ────────────────────────────────────────────────────────────────── */

static void test_close_stops_watch_threads(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_deadline_config_t cfg = rcp_deadline_default_config();
    rcp_deadline_monitor_t *mon;

    cfg.deadline_ms = 100;
    mon = rcp_deadline_monitor_new(cfg, ctrls, 1);

    rcp_deadline_monitor_close(mon); /* must return promptly, not hang */

    rcp_deadline_monitor_destroy(mon);
    rcp_controller_release(ctrl);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_monitor_constructs_without_error);
    RUN_TEST(test_dead_event_fires_and_is_not_repeated);
    RUN_TEST(test_alive_event_on_first_status);
    RUN_TEST(test_alive_returns_false_before_first_status);
    RUN_TEST(test_close_stops_watch_threads);

    return UNITY_END();
}
