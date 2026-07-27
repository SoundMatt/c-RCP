//cfusa:test REQ-PROXY-001
//cfusa:test REQ-PROXY-002
//cfusa:test REQ-PROXY-003
//cfusa:test REQ-PROXY-004
//cfusa:test REQ-PROXY-005
//cfusa:test REQ-PROXY-006
//cfusa:test REQ-PROXY-007
#include "unity.h"

#include <rcp/mock.h>
#include <rcp/proxy.h>
#include <rcp/rcp.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

/* ── ProxyController ──────────────────────────────────────────────────────── */

static void test_forwards_command_within_budget(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pc = rcp_proxy_controller_new(inner, rcp_proxy_default_config()); /* default 50ms budget -- not exceeded */
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(pc, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_response_free(&resp);
    rcp_controller_release(pc);
    rcp_controller_release(inner);
}

static void test_zero_latency_budget_makes_the_hop_time_out(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_proxy_config_t cfg = rcp_proxy_default_config();
    rcp_controller_t *pc;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.latency_budget_ms = 0; /* deadline = now -> already due */
    pc = rcp_proxy_controller_new(inner, cfg);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    /* The proxy derives a now+budget deadline and passes it down; the
     * upstream observes the budget has elapsed and returns RCP_ERR_TIMEOUT
     * instead of forwarding. */
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_controller_send(pc, &ctx, &cmd, &resp));

    rcp_controller_release(pc);
    rcp_controller_release(inner);
}

static void test_zone_matches_upstream(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *pc = rcp_proxy_controller_new(inner, rcp_proxy_default_config());

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(pc));

    rcp_controller_release(pc);
    rcp_controller_release(inner);
}

/* ── ProxyRegistry ────────────────────────────────────────────────────────── */

static void test_registry_lookup_and_send(void)
{
    rcp_registry_t *reg = rcp_proxy_registry_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_REAR_LEFT);
    rcp_controller_t *ctrl = NULL;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_proxy_registry_add_route(reg, inner, rcp_proxy_default_config()));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_REAR_LEFT, &ctrl));

    cmd.zone = RCP_ZONE_REAR_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(inner);
}

static void test_lookup_unknown_zone_returns_not_found(void)
{
    rcp_registry_t *reg = rcp_proxy_registry_new();
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_lookup(reg, RCP_ZONE_CENTRAL, &ctrl));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_deregister_closes_the_upstream_controller(void)
{
    rcp_registry_t *reg = rcp_proxy_registry_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_REAR_LEFT);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_proxy_registry_add_route(reg, inner, rcp_proxy_default_config()));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_REAR_LEFT));

    /* The upstream controller wrapped by the route is now closed. */
    cmd.zone = RCP_ZONE_REAR_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(inner, &ctx, &cmd, &resp));

    /* And the route is gone. */
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_lookup(reg, RCP_ZONE_REAR_LEFT, &ctrl));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(inner);
}

static void test_registry_close_is_idempotent(void)
{
    rcp_registry_t *reg = rcp_proxy_registry_new();
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_RIGHT);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_proxy_registry_add_route(reg, inner, rcp_proxy_default_config()));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg)); /* second close is a no-op, still no error */

    rcp_registry_destroy(reg);
    rcp_controller_release(inner);
}

static void test_duplicate_route_returns_already_exists(void)
{
    rcp_registry_t *reg = rcp_proxy_registry_new();
    rcp_controller_t *a = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *b = make_mock(RCP_ZONE_FRONT_LEFT);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_proxy_registry_add_route(reg, a, rcp_proxy_default_config()));
    TEST_ASSERT_EQUAL(RCP_ERR_ALREADY_EXISTS, rcp_proxy_registry_add_route(reg, b, rcp_proxy_default_config()));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(a);
    rcp_controller_release(b);
}

static void test_subscribe_delegates_to_upstream(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *pc = rcp_proxy_controller_new(inner, rcp_proxy_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(pc, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_status_channel_release(ch);
    rcp_controller_release(pc);
    rcp_controller_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_forwards_command_within_budget);
    RUN_TEST(test_zero_latency_budget_makes_the_hop_time_out);
    RUN_TEST(test_zone_matches_upstream);
    RUN_TEST(test_registry_lookup_and_send);
    RUN_TEST(test_lookup_unknown_zone_returns_not_found);
    RUN_TEST(test_deregister_closes_the_upstream_controller);
    RUN_TEST(test_registry_close_is_idempotent);
    RUN_TEST(test_duplicate_route_returns_already_exists);
    RUN_TEST(test_subscribe_delegates_to_upstream);

    return UNITY_END();
}
