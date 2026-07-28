//cfusa:test REQ-ZG-001
//cfusa:test REQ-ZG-002
//cfusa:test REQ-ZG-003
//cfusa:test REQ-ZG-004
//cfusa:test REQ-ZG-005
//cfusa:test REQ-ZG-006
#include "unity.h"

#include "legacy_mock.h"
#include <rcp/rcp.h>
#include <rcp/zonegroup.h>

void setUp(void) {}
void tearDown(void) {}

static void test_zone_group_all_has_5_zones(void)
{
    rcp_zone_group_t g = rcp_zone_group_all();
    TEST_ASSERT_EQUAL_UINT(5, g.len);
}

static void test_zone_group_rear_has_2_zones(void)
{
    rcp_zone_group_t g = rcp_zone_group_rear();
    TEST_ASSERT_EQUAL_UINT(2, g.len);
}

static void test_send_group_dispatches_to_all_zones(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_zone_group_t g = rcp_zone_group_all();
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_group_response_t resp;

    cmd.type = RCP_CMD_GET;
    resp = rcp_zonegroup_send(reg, &ctx, &g, &cmd);

    TEST_ASSERT_TRUE(rcp_group_response_ok(&resp));
    TEST_ASSERT_EQUAL_UINT(5, resp.results_len);

    rcp_group_response_free(&resp);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static bool result_zone_present(const rcp_group_response_t *r, rcp_zone_t z)
{
    size_t i;
    for (i = 0; i < r->results_len; i++) {
        if (r->results[i].zone == z) return true;
    }
    return false;
}

static void test_send_group_returns_results_per_zone(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_zone_group_t g = rcp_zone_group_front();
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_group_response_t resp;
    rcp_zone_t errs[8];

    cmd.type = RCP_CMD_SET;
    resp = rcp_zonegroup_send(reg, &ctx, &g, &cmd);

    TEST_ASSERT_TRUE(result_zone_present(&resp, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_TRUE(result_zone_present(&resp, RCP_ZONE_FRONT_RIGHT));
    TEST_ASSERT_EQUAL_UINT(0, rcp_group_response_errors(&resp, errs, 8));

    rcp_group_response_free(&resp);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_missing_zone_returns_error_in_result(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_zone_group_t g = rcp_zone_group_empty();
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_group_response_t resp;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    rcp_zone_group_add(&g, RCP_ZONE_FRONT_LEFT);

    cmd.type = RCP_CMD_GET;
    resp = rcp_zonegroup_send(reg, &ctx, &g, &cmd);

    TEST_ASSERT_FALSE(rcp_group_response_ok(&resp));

    rcp_group_response_free(&resp);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_errors_lists_every_failing_zone(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_zone_group_t g = rcp_zone_group_front();
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_group_response_t resp;
    rcp_zone_t errs[8];
    size_t n;
    bool has_fl = false;
    bool has_fr = false;
    size_t i;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_RIGHT));

    cmd.type = RCP_CMD_GET;
    resp = rcp_zonegroup_send(reg, &ctx, &g, &cmd);

    TEST_ASSERT_FALSE(rcp_group_response_ok(&resp));
    n = rcp_group_response_errors(&resp, errs, 8);
    TEST_ASSERT_EQUAL_UINT(2, n);
    for (i = 0; i < n; i++) {
        if (errs[i] == RCP_ZONE_FRONT_LEFT)  has_fl = true;
        if (errs[i] == RCP_ZONE_FRONT_RIGHT) has_fr = true;
    }
    TEST_ASSERT_TRUE(has_fl);
    TEST_ASSERT_TRUE(has_fr);

    rcp_group_response_free(&resp);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_zone_group_is_a_copyable_value_type(void)
{
    rcp_zone_group_t a = rcp_zone_group_empty();
    rcp_zone_group_t b;
    rcp_zone_group_t c;

    rcp_zone_group_add(&a, RCP_ZONE_FRONT_LEFT);
    rcp_zone_group_add(&a, RCP_ZONE_FRONT_RIGHT);

    b = a; /* copy */
    rcp_zone_group_add(&b, RCP_ZONE_CENTRAL); /* mutate the copy only */

    TEST_ASSERT_EQUAL_UINT(2, a.len); /* original is unaffected -> value semantics */
    TEST_ASSERT_EQUAL_UINT(3, b.len);

    c = a; /* copy assign */
    TEST_ASSERT_EQUAL_UINT(2, c.len);
}

static void test_send_group_honours_the_caller_context_deadline(void)
{
    rcp_registry_t *reg = rcp_mock_registry_new();
    rcp_zone_group_t g = rcp_zone_group_all();
    rcp_context_t ctx = rcp_context_with_deadline_ms(1); /* already expired */
    rcp_command_t cmd = {0};
    rcp_group_response_t resp;
    size_t i;

    cmd.type = RCP_CMD_GET;
    resp = rcp_zonegroup_send(reg, &ctx, &g, &cmd);

    TEST_ASSERT_FALSE(rcp_group_response_ok(&resp));
    TEST_ASSERT_EQUAL_UINT(5, resp.results_len);
    for (i = 0; i < resp.results_len; i++) {
        TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, resp.results[i].error);
    }

    rcp_group_response_free(&resp);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_zone_group_all_has_5_zones);
    RUN_TEST(test_zone_group_rear_has_2_zones);
    RUN_TEST(test_send_group_dispatches_to_all_zones);
    RUN_TEST(test_send_group_returns_results_per_zone);
    RUN_TEST(test_missing_zone_returns_error_in_result);
    RUN_TEST(test_errors_lists_every_failing_zone);
    RUN_TEST(test_zone_group_is_a_copyable_value_type);
    RUN_TEST(test_send_group_honours_the_caller_context_deadline);

    return UNITY_END();
}
