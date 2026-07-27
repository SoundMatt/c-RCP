//cfusa:test REQ-RL-001
//cfusa:test REQ-RL-002
//cfusa:test REQ-RL-003
//cfusa:test REQ-RL-004
//cfusa:test REQ-RL-005
//cfusa:test REQ-RL-006
//cfusa:test REQ-RL-007
//cfusa:test REQ-RL-008
#include "unity.h"

#include <rcp/mock.h>
#include <rcp/ratelimit.h>
#include <rcp/rcp.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

/* ── Basic send ───────────────────────────────────────────────────────────── */

static void test_send_forwards_command_when_bucket_has_tokens(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_controller_t *rl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.rate  = 1000;
    cfg.burst = 10;
    rl = rcp_ratelimit_controller_new(inner, cfg);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(rl, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_response_free(&resp);
    rcp_controller_release(rl);
    rcp_controller_release(inner);
}

static void test_zone_returns_inner_zone(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_CENTRAL);
    rcp_controller_t *rl = rcp_ratelimit_controller_new(inner, rcp_ratelimit_default_config());

    TEST_ASSERT_EQUAL(RCP_ZONE_CENTRAL, rcp_controller_zone(rl));

    rcp_controller_release(rl);
    rcp_controller_release(inner);
}

/* ── Token exhaustion ─────────────────────────────────────────────────────── */

static void test_send_returns_busy_when_bucket_exhausted(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_controller_t *rl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.rate            = 0.001; /* nearly zero refill */
    cfg.burst           = 2;
    cfg.exempt_critical = false;
    rl = rcp_ratelimit_controller_new(inner, cfg);

    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.priority = RCP_PRIORITY_NORMAL;

    /* Drain the 2 burst tokens. */
    (void)rcp_controller_send(rl, &ctx, &cmd, &resp); rcp_response_free(&resp);
    (void)rcp_controller_send(rl, &ctx, &cmd, &resp); rcp_response_free(&resp);

    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_controller_send(rl, &ctx, &cmd, &resp));

    rcp_controller_release(rl);
    rcp_controller_release(inner);
}

/* ── Critical exemption ───────────────────────────────────────────────────── */

static void test_critical_bypasses_bucket_when_exempt(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_controller_t *rl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.rate            = 0.001;
    cfg.burst           = 1;
    cfg.exempt_critical = true;
    rl = rcp_ratelimit_controller_new(inner, cfg);

    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.priority = RCP_PRIORITY_NORMAL;
    (void)rcp_controller_send(rl, &ctx, &cmd, &resp); rcp_response_free(&resp); /* exhaust the 1 token */

    cmd.priority = RCP_PRIORITY_CRITICAL;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(rl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(rl);
    rcp_controller_release(inner);
}

static void test_critical_does_not_bypass_bucket_when_not_exempt(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_controller_t *rl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.rate            = 0.001;
    cfg.burst           = 1;
    cfg.exempt_critical = false;
    rl = rcp_ratelimit_controller_new(inner, cfg);

    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.priority = RCP_PRIORITY_NORMAL;
    (void)rcp_controller_send(rl, &ctx, &cmd, &resp); rcp_response_free(&resp);

    cmd.priority = RCP_PRIORITY_CRITICAL;
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_controller_send(rl, &ctx, &cmd, &resp));

    rcp_controller_release(rl);
    rcp_controller_release(inner);
}

/* ── Zone mismatch / closed ───────────────────────────────────────────────── */

static void test_send_returns_zone_mismatch_on_wrong_zone(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *rl = rcp_ratelimit_controller_new(inner, rcp_ratelimit_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_REAR_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_ZONE_MISMATCH, rcp_controller_send(rl, &ctx, &cmd, &resp));

    rcp_controller_release(rl);
    rcp_controller_release(inner);
}

static void test_close_stops_sends(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *rl = rcp_ratelimit_controller_new(inner, rcp_ratelimit_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(rl));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(rl, &ctx, &cmd, &resp));

    rcp_controller_release(rl);
    rcp_controller_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_forwards_command_when_bucket_has_tokens);
    RUN_TEST(test_zone_returns_inner_zone);
    RUN_TEST(test_send_returns_busy_when_bucket_exhausted);
    RUN_TEST(test_critical_bypasses_bucket_when_exempt);
    RUN_TEST(test_critical_does_not_bypass_bucket_when_not_exempt);
    RUN_TEST(test_send_returns_zone_mismatch_on_wrong_zone);
    RUN_TEST(test_close_stops_sends);

    return UNITY_END();
}
