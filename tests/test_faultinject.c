//cfusa:test REQ-FI-001
//cfusa:test REQ-FI-002
//cfusa:test REQ-FI-003
//cfusa:test REQ-FI-004
//cfusa:test REQ-FI-005
//cfusa:test REQ-FI-006
//cfusa:test REQ-FI-007
//cfusa:test REQ-FI-008
//cfusa:test REQ-FI-009
//cfusa:test REQ-FI-010
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/faultinject.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

/* ── No rules ─────────────────────────────────────────────────────────────── */

static void test_send_passes_through_when_no_rules_active(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(fi, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_response_free(&resp);
    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

/* ── Drop ─────────────────────────────────────────────────────────────────── */

static void test_drop_rule_causes_send_to_return_error(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_fi_rule_t rule = {0};

    rule.type  = RCP_FI_DROP;
    rule.count = -1;
    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_NOT_EQUAL(RCP_OK, rcp_controller_send(fi, &ctx, &cmd, &resp));

    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

/* ── Error ────────────────────────────────────────────────────────────────── */

static void test_error_rule_returns_response_error(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_fi_rule_t rule = {0};

    rule.type  = RCP_FI_ERROR;
    rule.count = -1;
    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    (void)rcp_controller_send(fi, &ctx, &cmd, &resp);
    TEST_ASSERT_EQUAL(RCP_RESPONSE_ERROR, resp.status);

    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

/* ── Slow ─────────────────────────────────────────────────────────────────── */

static void test_slow_rule_adds_latency(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_fi_rule_t rule = {0};
    uint64_t start;
    uint64_t elapsed;

    rule.type       = RCP_FI_SLOW;
    rule.latency_ms = 20;
    rule.count      = 1;
    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    start = rcp_monotonic_ms();
    (void)rcp_controller_send(fi, &ctx, &cmd, &resp);
    elapsed = rcp_monotonic_ms() - start;
    TEST_ASSERT_TRUE(elapsed >= 20);

    rcp_response_free(&resp);
    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

/* ── Count-based expiry ───────────────────────────────────────────────────── */

static void test_rule_expires_after_count_sends(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_fi_rule_t rule = {0};

    rule.type  = RCP_FI_DROP;
    rule.count = 2; /* fires twice then expires */
    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_NOT_EQUAL(RCP_OK, rcp_controller_send(fi, &ctx, &cmd, &resp)); /* drop 1 */
    TEST_ASSERT_NOT_EQUAL(RCP_OK, rcp_controller_send(fi, &ctx, &cmd, &resp)); /* drop 2 */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(fi, &ctx, &cmd, &resp));     /* passes */

    rcp_response_free(&resp);
    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

/* ── clear_rules ──────────────────────────────────────────────────────────── */

static void test_clear_rules_removes_all_active_rules(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_fi_rule_t rule = {0};

    rule.type  = RCP_FI_DROP;
    rule.count = -1;
    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));
    rcp_faultinject_clear_rules(fi);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(fi, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

/* ── Timeout ──────────────────────────────────────────────────────────────── */

static void test_timeout_rule_returns_err_timeout(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_fi_rule_t rule = {0};

    rule.type  = RCP_FI_TIMEOUT;
    rule.count = -1;
    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_controller_send(fi, &ctx, &cmd, &resp));

    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

/* ── Zone passthrough ─────────────────────────────────────────────────────── */

static void test_zone_returns_inner_zone(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_CENTRAL);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);

    TEST_ASSERT_EQUAL(RCP_ZONE_CENTRAL, rcp_controller_zone(fi));

    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

static void test_subscribe_delegates_to_inner(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(fi, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_status_channel_release(ch);
    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

static void test_close_delegates_to_inner_and_rejects_further_sends(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *fi = rcp_faultinject_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(fi));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(fi, &ctx, &cmd, &resp));
    /* Confirm close() really reached the inner controller too. */
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(inner, &ctx, &cmd, &resp));

    rcp_controller_release(fi);
    rcp_controller_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_passes_through_when_no_rules_active);
    RUN_TEST(test_drop_rule_causes_send_to_return_error);
    RUN_TEST(test_error_rule_returns_response_error);
    RUN_TEST(test_slow_rule_adds_latency);
    RUN_TEST(test_rule_expires_after_count_sends);
    RUN_TEST(test_clear_rules_removes_all_active_rules);
    RUN_TEST(test_timeout_rule_returns_err_timeout);
    RUN_TEST(test_zone_returns_inner_zone);
    RUN_TEST(test_subscribe_delegates_to_inner);
    RUN_TEST(test_close_delegates_to_inner_and_rejects_further_sends);

    return UNITY_END();
}
