//cfusa:test REQ-RED-001
//cfusa:test REQ-RED-002
//cfusa:test REQ-RED-003
//cfusa:test REQ-RED-004
//cfusa:test REQ-RED-005
//cfusa:test REQ-RED-006
//cfusa:test REQ-RED-007
//cfusa:test REQ-RED-008
#include "unity.h"

#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <rcp/redundancy.h>

#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

/* ── FailController: always fails send()/subscribe() with a fixed error ──── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t         zone;
    int                err;
} fail_controller_t;

static rcp_zone_t fail_ctrl_zone(rcp_controller_t *self) { return ((fail_controller_t *)self)->zone; }

static int fail_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                           const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)ctx; (void)cmd; (void)out;
    return ((fail_controller_t *)self)->err;
}

static int fail_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)ctx; (void)out;
    return ((fail_controller_t *)self)->err;
}

static int fail_ctrl_close(rcp_controller_t *self) { (void)self; return RCP_OK; }
static void fail_ctrl_destroy(rcp_controller_t *self) { free(self); }

static const rcp_controller_vtable_t fail_controller_vtable = {
    fail_ctrl_zone, fail_ctrl_send, fail_ctrl_subscribe, fail_ctrl_close, fail_ctrl_destroy, NULL, NULL,
};

static rcp_controller_t *fail_controller_new(rcp_zone_t z, int err)
{
    fail_controller_t *fc = (fail_controller_t *)calloc(1, sizeof(*fc));
    fc->base.vt       = &fail_controller_vtable;
    fc->base.refcount = 1;
    fc->zone = z;
    fc->err  = err;
    return &fc->base;
}

/* ── Basic failover ───────────────────────────────────────────────────────── */

static void test_primary_succeeds_standby_unused(void)
{
    rcp_controller_t *primary = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *standby = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_redundancy_controller_new(primary, standby, rcp_redundancy_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_TRUE(rcp_redundancy_controller_is_primary_active(ctrl));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_TRUE(rcp_redundancy_controller_is_primary_active(ctrl));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

static void test_manual_promote_switches_to_standby(void)
{
    rcp_controller_t *primary = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *standby = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_redundancy_controller_new(primary, standby, rcp_redundancy_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    rcp_redundancy_controller_promote(ctrl);
    TEST_ASSERT_FALSE(rcp_redundancy_controller_is_primary_active(ctrl));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

static void test_double_promote_returns_to_primary(void)
{
    rcp_controller_t *primary = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *standby = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_redundancy_controller_new(primary, standby, rcp_redundancy_default_config());

    rcp_redundancy_controller_promote(ctrl);
    rcp_redundancy_controller_promote(ctrl);
    TEST_ASSERT_TRUE(rcp_redundancy_controller_is_primary_active(ctrl));

    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

static void test_zone_returns_zone_of_active_controller(void)
{
    rcp_controller_t *primary = make_mock(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *standby = make_mock(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *ctrl = rcp_redundancy_controller_new(primary, standby, rcp_redundancy_default_config());

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

static void test_auto_promotes_standby_on_closed(void)
{
    rcp_controller_t *primary = fail_controller_new(RCP_ZONE_FRONT_LEFT, RCP_ERR_CLOSED);
    rcp_controller_t *standby = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_redundancy_controller_new(primary, standby, rcp_redundancy_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_TRUE(rcp_redundancy_controller_is_primary_active(ctrl));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp)); /* succeeds via standby */
    TEST_ASSERT_FALSE(rcp_redundancy_controller_is_primary_active(ctrl));    /* standby is now active */

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

static void test_auto_promotes_standby_on_timeout(void)
{
    rcp_controller_t *primary = fail_controller_new(RCP_ZONE_FRONT_LEFT, RCP_ERR_TIMEOUT);
    rcp_controller_t *standby = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_redundancy_controller_new(primary, standby, rcp_redundancy_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_FALSE(rcp_redundancy_controller_is_primary_active(ctrl));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

static void test_auto_promote_false_disables_automatic_failover(void)
{
    rcp_controller_t *primary = fail_controller_new(RCP_ZONE_FRONT_LEFT, RCP_ERR_CLOSED);
    rcp_controller_t *standby = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_redundancy_config_t cfg = rcp_redundancy_default_config();
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cfg.auto_promote = false;
    ctrl = rcp_redundancy_controller_new(primary, standby, cfg);

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(ctrl, &ctx, &cmd, &resp)); /* error surfaced, no failover */
    TEST_ASSERT_TRUE(rcp_redundancy_controller_is_primary_active(ctrl));             /* still on primary */

    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

static void test_close_closes_both_controllers(void)
{
    rcp_controller_t *primary = make_mock(RCP_ZONE_CENTRAL);
    rcp_controller_t *standby = make_mock(RCP_ZONE_CENTRAL);
    rcp_controller_t *ctrl = rcp_redundancy_controller_new(primary, standby, rcp_redundancy_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    cmd.zone = RCP_ZONE_CENTRAL;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(primary, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(standby, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_controller_release(primary);
    rcp_controller_release(standby);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_primary_succeeds_standby_unused);
    RUN_TEST(test_manual_promote_switches_to_standby);
    RUN_TEST(test_double_promote_returns_to_primary);
    RUN_TEST(test_zone_returns_zone_of_active_controller);
    RUN_TEST(test_auto_promotes_standby_on_closed);
    RUN_TEST(test_auto_promotes_standby_on_timeout);
    RUN_TEST(test_auto_promote_false_disables_automatic_failover);
    RUN_TEST(test_close_closes_both_controllers);

    return UNITY_END();
}
