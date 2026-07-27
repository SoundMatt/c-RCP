//cfusa:test REQ-SHMEM-001
//cfusa:test REQ-SHMEM-002
//cfusa:test REQ-SHMEM-003
//cfusa:test REQ-SHMEM-004
//cfusa:test REQ-SHMEM-005
//cfusa:test REQ-SHMEM-006
//cfusa:test REQ-SHMEM-007
//cfusa:test REQ-SHMEM-008
//cfusa:test REQ-SHMEM-009
//cfusa:test REQ-SHMEM-010
//cfusa:test REQ-SHMEM-011
//cfusa:test REQ-SHMEM-012
//cfusa:test REQ-SHMEM-013
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/rcp.h>
#include <rcp/shmem.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── ZoneServer + Controller ──────────────────────────────────────────────── */

static void test_send_returns_ok_via_default_handler(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_shmem_controller_new(server);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_shmem_zone_server_release(server);
}

static void custom_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    static const uint8_t payload[] = {0xAB};
    (void)user_data;
    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_OK;
    out->payload    = rcp_bytes_dup(payload, sizeof(payload));
}

static void test_dispatches_to_custom_handler(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    rcp_shmem_zone_server_set_handler(server, custom_handler, NULL);
    ctrl = rcp_shmem_controller_new(server);

    cmd.id   = 7;
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL_UINT(1, resp.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0xAB, resp.payload.data[0]);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_shmem_zone_server_release(server);
}

static void test_send_zone_mismatch(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_shmem_controller_new(server);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_REAR_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_ZONE_MISMATCH, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_shmem_zone_server_release(server);
}

static void test_send_after_close_is_closed(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_shmem_controller_new(server);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    rcp_controller_close(ctrl);
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_shmem_zone_server_release(server);
}

/* ── Publish / subscribe ──────────────────────────────────────────────────── */

static void test_publish_delivers_to_subscribers(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    uint8_t payload[] = {0x01, 0x02};
    rcp_status_t st;

    rcp_shmem_zone_server_set_healthy(server, true);
    ctrl = rcp_shmem_controller_new(server);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_shmem_zone_server_publish(server, payload, sizeof(payload));
    TEST_ASSERT_TRUE(rcp_status_channel_recv(ch, &st));
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, st.zone);
    TEST_ASSERT_TRUE(st.healthy);
    TEST_ASSERT_EQUAL_UINT(2, st.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, st.payload.data, sizeof(payload));

    rcp_status_free(&st);
    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_shmem_zone_server_release(server);
}

/* ── Payload copy isolation ───────────────────────────────────────────────── */

static uint8_t g_received_payload[3];
static size_t  g_received_len;

static void capture_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    (void)user_data;
    g_received_len = cmd->payload.len;
    if (cmd->payload.len > 0) memcpy(g_received_payload, cmd->payload.data, cmd->payload.len);
    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_OK;
}

static void test_controller_copies_payload(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    uint8_t original[] = {1, 2, 3};
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    rcp_shmem_zone_server_set_handler(server, capture_handler, NULL);
    ctrl = rcp_shmem_controller_new(server);

    g_received_len = 0;
    cmd.zone         = RCP_ZONE_FRONT_LEFT;
    cmd.payload.data = original;
    cmd.payload.len  = sizeof(original);

    rcp_controller_send(ctrl, &ctx, &cmd, &resp);
    original[0] = 0xFF; /* mutate after send returns */

    TEST_ASSERT_EQUAL_UINT(3, g_received_len);
    TEST_ASSERT_EQUAL_UINT8(1, g_received_payload[0]);

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_shmem_zone_server_release(server);
}

/* ── Registry ─────────────────────────────────────────────────────────────── */

static void test_registry_lookup_finds_registered_server(void)
{
    rcp_registry_t *reg = rcp_shmem_registry_new();
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl_out = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_shmem_registry_add_server(reg, server));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl_out));
    TEST_ASSERT_NOT_NULL(ctrl_out);

    rcp_controller_release(ctrl_out);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    rcp_shmem_zone_server_release(server);
}

static void test_registry_close_releases_all_controllers(void)
{
    rcp_registry_t *reg = rcp_shmem_registry_new();
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl_out = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_shmem_registry_add_server(reg, server));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl_out));

    rcp_registry_destroy(reg);
    rcp_shmem_zone_server_release(server);
}

static void test_zone_server_close_marks_unhealthy(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);

    TEST_ASSERT_TRUE(rcp_shmem_zone_server_ok(server));
    rcp_shmem_zone_server_close(server);
    TEST_ASSERT_FALSE(rcp_shmem_zone_server_ok(server));

    rcp_shmem_zone_server_release(server);
}

static void test_subscription_watcher_removes_sub_on_controller_close(void)
{
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_shmem_controller_new(server);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;
    uint64_t deadline;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    /* The watcher thread polls every ~1ms and, on seeing the controller
     * closed, removes this channel from the server and closes it -- wait
     * for that to happen rather than asserting immediately. */
    deadline = rcp_monotonic_ms() + 500;
    while (!rcp_status_channel_is_closed(ch) && rcp_monotonic_ms() < deadline) {
        /* busy-wait */
    }
    TEST_ASSERT_TRUE(rcp_status_channel_is_closed(ch));

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_shmem_zone_server_release(server);
}

static void test_registry_controllers_lists_registered_servers(void)
{
    rcp_registry_t *reg = rcp_shmem_registry_new();
    rcp_shmem_zone_server_t *a = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_shmem_zone_server_t *b = rcp_shmem_zone_server_new(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *out[4] = {0};
    size_t n;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_shmem_registry_add_server(reg, a));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_shmem_registry_add_server(reg, b));

    n = rcp_registry_controllers(reg, out, 4);
    TEST_ASSERT_EQUAL_UINT(2, n);

    rcp_controller_release(out[0]);
    rcp_controller_release(out[1]);
    rcp_registry_destroy(reg);
    rcp_shmem_zone_server_release(a);
    rcp_shmem_zone_server_release(b);
}

static void test_registry_deregister_removes_and_closes_controller(void)
{
    rcp_registry_t *reg = rcp_shmem_registry_new();
    rcp_shmem_zone_server_t *server = rcp_shmem_zone_server_new(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *out[1] = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_shmem_registry_add_server(reg, server));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL_UINT(0, rcp_registry_controllers(reg, out, 1));

    rcp_registry_destroy(reg);
    rcp_shmem_zone_server_release(server);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_returns_ok_via_default_handler);
    RUN_TEST(test_dispatches_to_custom_handler);
    RUN_TEST(test_send_zone_mismatch);
    RUN_TEST(test_send_after_close_is_closed);
    RUN_TEST(test_publish_delivers_to_subscribers);
    RUN_TEST(test_controller_copies_payload);
    RUN_TEST(test_registry_lookup_finds_registered_server);
    RUN_TEST(test_registry_close_releases_all_controllers);
    RUN_TEST(test_zone_server_close_marks_unhealthy);
    RUN_TEST(test_subscription_watcher_removes_sub_on_controller_close);
    RUN_TEST(test_registry_controllers_lists_registered_servers);
    RUN_TEST(test_registry_deregister_removes_and_closes_controller);

    return UNITY_END();
}
