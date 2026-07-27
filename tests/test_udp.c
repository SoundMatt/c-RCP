/* Loopback integration tests for the UDP transport. Skips (TEST_IGNORE)
 * rather than failing on platforms where rcp_udp_*_ok() reports the
 * transport isn't available (currently: Windows, which only has the stub
 * implementation — see ROADMAP.md). */
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/rcp.h>
#include <rcp/udp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_sleep_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

static void echo_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    (void)user_data;
    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_OK;
    if (cmd->payload.len > 0) {
        out->payload = rcp_bytes_dup(cmd->payload.data, cmd->payload.len);
    }
}

//cfusa:req REQ-UDP-001
static void test_udp_send_roundtrip_over_loopback(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_controller_t *ctrl;
    rcp_context_t ctx;
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};
    uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    uint16_t port;

    srv = rcp_udp_zone_server_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    rcp_udp_zone_server_set_handler(srv, echo_handler, NULL);
    port = rcp_udp_zone_server_port(srv);

    ctrl = rcp_udp_controller_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", port);
    TEST_ASSERT_TRUE(rcp_udp_controller_ok(ctrl));

    /* Note: the UDP controller reassigns its own internal sequence id
     * before sending (mirrors cpp-RCP's udp::Controller::send(), which does
     * the same via ++next_id_) so it can disambiguate concurrent in-flight
     * requests over a connectionless transport — the caller's cmd.id here
     * is deliberately NOT what comes back as out.command_id. This is the
     * first send() on a freshly constructed controller, so the assigned id
     * is 1. */
    cmd.id           = 7;
    cmd.zone         = RCP_ZONE_FRONT_LEFT;
    cmd.type         = RCP_CMD_SET;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out.command_id);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, out.zone);
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, out.status);
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), out.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.payload.data, sizeof(payload));

    rcp_response_free(&out);
    rcp_controller_release(ctrl);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

//cfusa:req REQ-CTRL-025
static void test_udp_send_zone_mismatch(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_controller_t *ctrl;
    rcp_context_t ctx;
    rcp_command_t cmd = {0};
    rcp_response_t out = {0};
    uint16_t port;

    srv = rcp_udp_zone_server_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    port = rcp_udp_zone_server_port(srv);

    ctrl = rcp_udp_controller_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", port);
    TEST_ASSERT_TRUE(rcp_udp_controller_ok(ctrl));

    cmd.zone = RCP_ZONE_FRONT_RIGHT; /* controller is bound to FrontLeft */
    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_ERR_ZONE_MISMATCH, rcp_controller_send(ctrl, &ctx, &cmd, &out));

    rcp_controller_release(ctrl);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

//cfusa:req REQ-CTRL-006
static void test_udp_publish_subscribe_over_loopback(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_controller_t *ctrl;
    rcp_context_t ctx;
    rcp_status_channel_t *ch = NULL;
    rcp_status_t st;
    uint8_t payload[] = {0x01};
    uint16_t port;

    srv = rcp_udp_zone_server_new(RCP_ZONE_CENTRAL, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    port = rcp_udp_zone_server_port(srv);

    ctrl = rcp_udp_controller_new(RCP_ZONE_CENTRAL, "127.0.0.1", port);
    TEST_ASSERT_TRUE(rcp_udp_controller_ok(ctrl));

    ctx = rcp_context_background();
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));

    /* Retry-publish until the subscriber shows up in the received status:
     * UDP gives no delivery guarantee, and the server may not have
     * processed our Subscribe control frame yet by the time we publish
     * once, so re-publish periodically rather than relying on a single
     * fixed sleep. */
    {
        uint64_t deadline = rcp_monotonic_ms() + 2000;
        bool got = false;
        while (rcp_monotonic_ms() < deadline && !got) {
            rcp_udp_zone_server_publish(srv, payload, sizeof(payload));
            if (rcp_status_channel_try_recv(ch, &st)) {
                got = true;
                break;
            }
            test_sleep_ms(20);
        }
        TEST_ASSERT_TRUE(got);
    }
    TEST_ASSERT_EQUAL(RCP_ZONE_CENTRAL, st.zone);
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), st.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0x01, st.payload.data[0]);

    rcp_status_free(&st);
    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_udp_send_roundtrip_over_loopback);
    RUN_TEST(test_udp_send_zone_mismatch);
    RUN_TEST(test_udp_publish_subscribe_over_loopback);

    return UNITY_END();
}
