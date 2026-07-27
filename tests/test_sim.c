//cfusa:test REQ-SIM-001
//cfusa:test REQ-SIM-002
//cfusa:test REQ-SIM-003
//cfusa:test REQ-SIM-004
//cfusa:test REQ-SIM-005
//cfusa:test REQ-SIM-006
//cfusa:test REQ-SIM-007
//cfusa:test REQ-SIM-008
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/rcp.h>
#include <rcp/sim.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Basic send ───────────────────────────────────────────────────────────── */

static void test_send_returns_ok_by_default(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.latency_model    = RCP_SIM_LATENCY_CONSTANT;
    cfg.base_latency_ms  = 0;
    cfg.status_interval_ms  = 0; /* disable background threads for this test */
    cfg.watchdog_timeout_ms = 0;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
}

static void test_zone_returns_configured_zone(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *ctrl;

    cfg.status_interval_ms  = 0;
    cfg.watchdog_timeout_ms = 0;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
}

/* ── Latency ──────────────────────────────────────────────────────────────── */

static void test_applies_constant_latency(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    uint64_t start;
    uint64_t elapsed;

    cfg.latency_model       = RCP_SIM_LATENCY_CONSTANT;
    cfg.base_latency_ms     = 10;
    cfg.status_interval_ms  = 0;
    cfg.watchdog_timeout_ms = 0;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    start = rcp_monotonic_ms();
    (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
    elapsed = rcp_monotonic_ms() - start;

    TEST_ASSERT_TRUE(elapsed >= 10);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
}

static void test_applies_jitter_latency_at_least_base(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    uint64_t start;
    uint64_t elapsed;

    cfg.latency_model       = RCP_SIM_LATENCY_JITTER;
    cfg.base_latency_ms     = 5;
    cfg.jitter_ms           = 5;
    cfg.status_interval_ms  = 0;
    cfg.watchdog_timeout_ms = 0;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    start = rcp_monotonic_ms();
    (void)rcp_controller_send(ctrl, &ctx, &cmd, &resp);
    elapsed = rcp_monotonic_ms() - start;

    /* Jitter only ever adds on top of base_latency_ms, never subtracts. */
    TEST_ASSERT_TRUE(elapsed >= 5);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
}

/* ── Fault injection ──────────────────────────────────────────────────────── */

static void test_fault_causes_send_to_fail(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.status_interval_ms  = 0;
    cfg.watchdog_timeout_ms = 0;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    rcp_sim_controller_fault(ctrl, RCP_ERR_CLOSED);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
}

static void test_recover_restores_normal_operation(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.status_interval_ms  = 0;
    cfg.watchdog_timeout_ms = 0;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    rcp_sim_controller_fault(ctrl, RCP_ERR_CLOSED);
    rcp_sim_controller_recover(ctrl);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
}

/* ── Subscribe / Status ───────────────────────────────────────────────────── */

static void test_publishes_status_on_interval(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    rcp_status_t st;
    int elapsed_ms;
    bool got;

    cfg.status_interval_ms  = 20;
    cfg.watchdog_timeout_ms = 0;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    got = false;
    elapsed_ms = 0;
    while (!got && elapsed_ms < 2000) {
        got = rcp_status_channel_try_recv(ch, &st);
        if (!got) {
            uint64_t start = rcp_monotonic_ms();
            while (rcp_monotonic_ms() - start < 10) { /* busy-wait */ }
            elapsed_ms += 10;
        }
    }
    TEST_ASSERT_TRUE(got);
    if (got) rcp_status_free(&st);

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
}

/* ── Close ────────────────────────────────────────────────────────────────── */

static void test_close_stops_background_threads(void)
{
    rcp_sim_config_t cfg = rcp_sim_default_config(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;

    cfg.status_interval_ms  = 50;
    cfg.watchdog_timeout_ms = 50;
    ctrl = rcp_sim_controller_new(cfg, NULL, NULL);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl)); /* must return promptly, not hang */

    rcp_controller_release(ctrl);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_returns_ok_by_default);
    RUN_TEST(test_zone_returns_configured_zone);
    RUN_TEST(test_applies_constant_latency);
    RUN_TEST(test_applies_jitter_latency_at_least_base);
    RUN_TEST(test_fault_causes_send_to_fail);
    RUN_TEST(test_recover_restores_normal_operation);
    RUN_TEST(test_publishes_status_on_interval);
    RUN_TEST(test_close_stops_background_threads);

    return UNITY_END();
}
