//cfusa:test REQ-TSN-001
//cfusa:test REQ-TSN-002
//cfusa:test REQ-TSN-003
//cfusa:test REQ-TSN-004
//cfusa:test REQ-TSN-005
//cfusa:test REQ-TSN-006
#include "unity.h"

#include "legacy_mock.h"
#include <rcp/rcp.h>
#include <rcp/tsn.h>

void setUp(void) {}
void tearDown(void) {}

static void test_pcp_map_maps_normal_high_critical(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(m.normal,   rcp_tsn_pcp_for(&m, RCP_PRIORITY_NORMAL));
    TEST_ASSERT_EQUAL_UINT8(m.high,     rcp_tsn_pcp_for(&m, RCP_PRIORITY_HIGH));
    TEST_ASSERT_EQUAL_UINT8(m.critical, rcp_tsn_pcp_for(&m, RCP_PRIORITY_CRITICAL));
}

static void test_critical_maps_to_highest_pcp(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(7, rcp_tsn_pcp_for(&m, RCP_PRIORITY_CRITICAL));
    TEST_ASSERT_TRUE(rcp_tsn_pcp_for(&m, RCP_PRIORITY_CRITICAL) > rcp_tsn_pcp_for(&m, RCP_PRIORITY_HIGH));
}

static void test_high_maps_to_mid_range_pcp(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();
    uint8_t high = rcp_tsn_pcp_for(&m, RCP_PRIORITY_HIGH);

    TEST_ASSERT_EQUAL_UINT8(5, high);
    TEST_ASSERT_TRUE(high > rcp_tsn_pcp_for(&m, RCP_PRIORITY_NORMAL));
    TEST_ASSERT_TRUE(high < rcp_tsn_pcp_for(&m, RCP_PRIORITY_CRITICAL));
}

static void test_normal_maps_to_low_pcp(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(2, rcp_tsn_pcp_for(&m, RCP_PRIORITY_NORMAL));
    TEST_ASSERT_TRUE(rcp_tsn_pcp_for(&m, RCP_PRIORITY_NORMAL) < rcp_tsn_pcp_for(&m, RCP_PRIORITY_HIGH));
}

static void capture_priority_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    rcp_priority_t *seen = (rcp_priority_t *)user_data;
    *seen = cmd->priority;
    out->command_id = cmd->id;
    out->zone       = RCP_ZONE_FRONT_LEFT;
    out->status     = RCP_RESPONSE_OK;
}

static void test_send_applies_priority_class_then_delegates_to_inner(void)
{
    rcp_priority_t seen = RCP_PRIORITY_NORMAL;
    rcp_controller_t *inner = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, capture_priority_handler, &seen);
    /* fd = -1 -> SO_PRIORITY is skipped, send still delegates. */
    rcp_controller_t *ctrl = rcp_tsn_controller_new(inner, -1, rcp_tsn_default_config());
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_context_t ctx = rcp_context_background();

    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.type     = RCP_CMD_SET;
    cmd.priority = RCP_PRIORITY_CRITICAL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);
    TEST_ASSERT_EQUAL(RCP_PRIORITY_CRITICAL, seen); /* forwarded unchanged to the inner controller */

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_subscribe_delegates_to_inner(void)
{
    rcp_controller_t *inner = rcp_mock_controller_new(RCP_ZONE_CENTRAL, NULL, NULL);
    rcp_controller_t *ctrl  = rcp_tsn_controller_new(inner, -1, rcp_tsn_default_config());
    rcp_status_channel_t *ch = NULL;
    rcp_context_t ctx = rcp_context_background();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL(RCP_ZONE_CENTRAL, rcp_controller_zone(ctrl));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pcp_map_maps_normal_high_critical);
    RUN_TEST(test_critical_maps_to_highest_pcp);
    RUN_TEST(test_high_maps_to_mid_range_pcp);
    RUN_TEST(test_normal_maps_to_low_pcp);
    RUN_TEST(test_send_applies_priority_class_then_delegates_to_inner);
    RUN_TEST(test_subscribe_delegates_to_inner);

    return UNITY_END();
}
