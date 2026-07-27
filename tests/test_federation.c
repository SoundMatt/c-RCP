//cfusa:test REQ-FED-001
//cfusa:test REQ-FED-002
//cfusa:test REQ-FED-003
//cfusa:test REQ-FED-004
//cfusa:test REQ-FED-005
//cfusa:test REQ-FED-006
//cfusa:test REQ-FED-007
//cfusa:test REQ-FED-008
//cfusa:test REQ-FED-009
//cfusa:test REQ-FED-010
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/federation.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

static void test_local_controller_preferred_over_lease(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *local_ctrl = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *remote_ctrl = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, local_ctrl));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_federation_registry_add_lease(
        reg, RCP_ZONE_FRONT_LEFT, "hpc-b", rcp_monotonic_ms() + 60000, remote_ctrl));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl));
    TEST_ASSERT_TRUE(ctrl == local_ctrl); /* local wins */

    rcp_controller_release(ctrl);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(local_ctrl);
    rcp_controller_release(remote_ctrl);
}

static void test_remote_lease_used_when_no_local(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *remote_ctrl = make_mock(RCP_ZONE_REAR_LEFT);
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_federation_registry_add_lease(
        reg, RCP_ZONE_REAR_LEFT, "hpc-b", rcp_monotonic_ms() + 60000, remote_ctrl));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_REAR_LEFT, &ctrl));
    TEST_ASSERT_TRUE(ctrl == remote_ctrl);

    rcp_controller_release(ctrl);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(remote_ctrl);
}

static void test_expired_lease_returns_not_found(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *remote_ctrl = make_mock(RCP_ZONE_CENTRAL);
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_federation_registry_add_lease(
        reg, RCP_ZONE_CENTRAL, "hpc-b", 1 /* already expired */, remote_ctrl));

    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_lookup(reg, RCP_ZONE_CENTRAL, &ctrl));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(remote_ctrl);
}

static void test_revoke_lease_removes_lease(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *remote_ctrl = make_mock(RCP_ZONE_FRONT_RIGHT);
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_federation_registry_add_lease(
        reg, RCP_ZONE_FRONT_RIGHT, "hpc-b", rcp_monotonic_ms() + 60000, remote_ctrl));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_federation_registry_revoke_lease(reg, RCP_ZONE_FRONT_RIGHT));

    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_lookup(reg, RCP_ZONE_FRONT_RIGHT, &ctrl));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(remote_ctrl);
}

static void test_local_id_is_preserved(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-main");

    TEST_ASSERT_EQUAL_STRING("hpc-main", rcp_federation_registry_local_id(reg));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_register_ctrl_rejects_duplicate_zone(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *a = make_mock(RCP_ZONE_CENTRAL);
    rcp_controller_t *b = make_mock(RCP_ZONE_CENTRAL);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, a));
    TEST_ASSERT_EQUAL(RCP_ERR_ALREADY_EXISTS, rcp_registry_register(reg, b));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_controller_release(a);
    rcp_controller_release(b);
}

static void test_close_closes_local_and_remote_controllers(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *local_ctrl = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *remote_ctrl = make_mock(RCP_ZONE_REAR_LEFT);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, local_ctrl));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_federation_registry_add_lease(
        reg, RCP_ZONE_REAR_LEFT, "hpc-b", rcp_monotonic_ms() + 60000, remote_ctrl));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(local_ctrl, &ctx, &cmd, &resp));
    cmd.zone = RCP_ZONE_REAR_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(remote_ctrl, &ctx, &cmd, &resp));

    rcp_registry_destroy(reg);
    rcp_controller_release(local_ctrl);
    rcp_controller_release(remote_ctrl);
}

static void test_closed_registry_returns_closed_on_lookup(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl));

    rcp_registry_destroy(reg);
}

static void test_controllers_lists_registered_local_controllers(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *a = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *b = make_mock(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *out[4] = {0};
    size_t n;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, a));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, b));

    n = rcp_registry_controllers(reg, out, 4);
    TEST_ASSERT_EQUAL_UINT(2, n);

    rcp_controller_release(out[0]);
    rcp_controller_release(out[1]);
    rcp_registry_destroy(reg);
    rcp_controller_release(a);
    rcp_controller_release(b);
}

static void test_deregister_removes_and_closes_local_controller(void)
{
    rcp_registry_t *reg = rcp_federation_registry_new("hpc-a");
    rcp_controller_t *ctrl = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *out[1] = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, ctrl));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL_UINT(0, rcp_registry_controllers(reg, out, 1));

    rcp_registry_destroy(reg);
    rcp_controller_release(ctrl);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_local_controller_preferred_over_lease);
    RUN_TEST(test_remote_lease_used_when_no_local);
    RUN_TEST(test_expired_lease_returns_not_found);
    RUN_TEST(test_revoke_lease_removes_lease);
    RUN_TEST(test_local_id_is_preserved);
    RUN_TEST(test_register_ctrl_rejects_duplicate_zone);
    RUN_TEST(test_close_closes_local_and_remote_controllers);
    RUN_TEST(test_closed_registry_returns_closed_on_lookup);
    RUN_TEST(test_controllers_lists_registered_local_controllers);
    RUN_TEST(test_deregister_removes_and_closes_local_controller);

    return UNITY_END();
}
