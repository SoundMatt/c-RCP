/* restbridge protocol-bridge stub conformance tests.
 *
 * The REST controller is a compile-time interface stub: until a concrete
 * HTTP client backend is linked, send()/subscribe() return
 * RCP_ERR_NOT_SUPPORTED, zone() reports the configured zone, and close()
 * succeeds. These tests pin that contract so callers get a well-defined
 * error rather than undefined behaviour.
 */
//cfusa:test REQ-REST-001
//cfusa:test REQ-REST-002
//cfusa:test REQ-REST-003
//cfusa:test REQ-REST-004
#include "unity.h"

#include <rcp/restbridge.h>
#include <rcp/rcp.h>

void setUp(void) {}
void tearDown(void) {}

static void test_send_returns_not_supported_when_stub(void)
{
    rcp_controller_t *ctrl = rcp_rest_controller_new(RCP_ZONE_FRONT_LEFT, rcp_rest_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_SET;
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
}

static void test_zone_returns_configured_zone(void)
{
    rcp_controller_t *ctrl = rcp_rest_controller_new(RCP_ZONE_REAR_RIGHT, rcp_rest_default_config());

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
}

static void test_subscribe_returns_not_supported_when_stub(void)
{
    rcp_controller_t *ctrl = rcp_rest_controller_new(RCP_ZONE_CENTRAL, rcp_rest_default_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_controller_subscribe(ctrl, &ctx, &ch));

    rcp_controller_release(ctrl);
}

static void test_close_returns_no_error(void)
{
    rcp_controller_t *ctrl = rcp_rest_controller_new(RCP_ZONE_FRONT_RIGHT, rcp_rest_default_config());

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    rcp_controller_release(ctrl);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_returns_not_supported_when_stub);
    RUN_TEST(test_zone_returns_configured_zone);
    RUN_TEST(test_subscribe_returns_not_supported_when_stub);
    RUN_TEST(test_close_returns_no_error);

    return UNITY_END();
}
