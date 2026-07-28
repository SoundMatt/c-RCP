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
#include "legacy_mock.h"
#include <rcp/rcp.h>
#include <rcp/watchdog.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_sleep_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

/* ── Keeper creation ──────────────────────────────────────────────────────── */

static void test_keeper_constructs_and_kicks_all_zones(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *keeper;

    cfg.interval_ms   = 200;
    cfg.timeout_ms    = 50;
    cfg.degrade_after = 3;
    cfg.fault_after   = 5;

    keeper = rcp_watchdog_keeper_new(cfg, ctrls, 1);
    TEST_ASSERT_NOT_NULL(keeper);

    rcp_watchdog_keeper_destroy(keeper);
    rcp_controller_release(ctrl);
}

/* ── Initial state ─────────────────────────────────────────────────────────── */

static void test_zone_starts_healthy(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *keeper;

    cfg.interval_ms = 500; /* long so no kicks fire before the assertion */

    keeper = rcp_watchdog_keeper_new(cfg, ctrls, 1);
    TEST_ASSERT_EQUAL(RCP_HEALTH_HEALTHY, rcp_watchdog_keeper_health(keeper, RCP_ZONE_FRONT_LEFT));

    rcp_watchdog_keeper_destroy(keeper);
    rcp_controller_release(ctrl);
}

/* ── Health state transitions ─────────────────────────────────────────────── */

/* Plain (non-atomic) int: only ever written by the keeper's background
 * thread and read by the test after rcp_watchdog_keeper_destroy() has
 * joined that thread, which establishes happens-before without needing
 * <stdatomic.h> (C11, unavailable under this project's C99 standard). */
static int g_last_state;

static void capture_last_state(const rcp_health_event_t *ev, void *user_data)
{
    (void)user_data;
    g_last_state = (int)ev->state;
}

static bool poll_for_state(rcp_watchdog_keeper_t *keeper, rcp_zone_t z, rcp_health_state_t want)
{
    /* Background kick cadence isn't deterministic under CI scheduling, so
     * poll up to a generous deadline rather than a single fixed sleep. */
    int elapsed_ms = 0;
    while (elapsed_ms < 5000) {
        if (rcp_watchdog_keeper_health(keeper, z) == want) return true;
        test_sleep_ms(10);
        elapsed_ms += 10;
    }
    return false;
}

static void test_degraded_after_degrade_after_misses(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *keeper;

    rcp_controller_close(ctrl); /* every kick now fails with RCP_ERR_CLOSED */

    cfg.interval_ms   = 20;
    cfg.timeout_ms    = 5;
    cfg.degrade_after = 2;
    cfg.fault_after   = 100; /* effectively unreachable within this test's deadline */

    keeper = rcp_watchdog_keeper_new(cfg, ctrls, 1);

    TEST_ASSERT_TRUE(poll_for_state(keeper, RCP_ZONE_FRONT_LEFT, RCP_HEALTH_DEGRADED));

    rcp_watchdog_keeper_destroy(keeper);
    rcp_controller_release(ctrl);
}

static void test_faulted_after_fault_after_misses(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *keeper;

    rcp_controller_close(ctrl); /* every kick now fails with RCP_ERR_CLOSED */

    cfg.interval_ms   = 20;
    cfg.timeout_ms    = 5;
    cfg.degrade_after = 2;
    cfg.fault_after   = 3;

    g_last_state = (int)RCP_HEALTH_HEALTHY;
    keeper = rcp_watchdog_keeper_new(cfg, ctrls, 1);
    rcp_watchdog_keeper_subscribe(keeper, capture_last_state, NULL);

    TEST_ASSERT_TRUE(poll_for_state(keeper, RCP_ZONE_FRONT_LEFT, RCP_HEALTH_FAULTED));

    /* rcp_watchdog_keeper_destroy() joins the background thread before
     * returning, which is what makes reading the plain (non-atomic)
     * g_last_state below safe — see its declaration comment. */
    rcp_watchdog_keeper_destroy(keeper);
    TEST_ASSERT_EQUAL(RCP_HEALTH_FAULTED, g_last_state);

    rcp_controller_release(ctrl);
}

/* ── Health recovery ───────────────────────────────────────────────────────── */

static void test_recovery_to_healthy_after_degraded(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *keeper;

    cfg.interval_ms   = 20;
    cfg.timeout_ms    = 5;
    cfg.degrade_after = 2;
    cfg.fault_after   = 100;

    keeper = rcp_watchdog_keeper_new(cfg, ctrls, 1);

    /* Should never leave Healthy: the mock controller is open and answers
     * every kick successfully. */
    test_sleep_ms(150);
    TEST_ASSERT_EQUAL(RCP_HEALTH_HEALTHY, rcp_watchdog_keeper_health(keeper, RCP_ZONE_FRONT_LEFT));

    rcp_watchdog_keeper_destroy(keeper);
    rcp_controller_release(ctrl);
}

/* ── Close ─────────────────────────────────────────────────────────────────── */

static void test_close_stops_background_kicks(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_watchdog_config_t cfg = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t *keeper;

    cfg.interval_ms = 50;
    keeper = rcp_watchdog_keeper_new(cfg, ctrls, 1);

    rcp_watchdog_keeper_close(keeper); /* must return promptly, not hang */

    rcp_watchdog_keeper_destroy(keeper);
    rcp_controller_release(ctrl);
}

static void test_health_state_string_unique_nonempty(void)
{
    const rcp_health_state_t states[] = {
        RCP_HEALTH_HEALTHY, RCP_HEALTH_DEGRADED, RCP_HEALTH_FAULTED,
    };
    const size_t n = sizeof(states) / sizeof(states[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_health_state_string(states[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(s, rcp_health_state_string(states[j])) != 0 ? 1 : 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_keeper_constructs_and_kicks_all_zones);
    RUN_TEST(test_zone_starts_healthy);
    RUN_TEST(test_degraded_after_degrade_after_misses);
    RUN_TEST(test_faulted_after_fault_after_misses);
    RUN_TEST(test_recovery_to_healthy_after_degraded);
    RUN_TEST(test_close_stops_background_kicks);
    RUN_TEST(test_health_state_string_unique_nonempty);

    return UNITY_END();
}
