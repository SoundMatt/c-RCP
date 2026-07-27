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
    rcp_status_t st = {0};
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

//cfusa:req REQ-UDP-015
static void test_udp_controller_zone_returns_configured_zone(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_controller_t *ctrl;
    uint16_t port;

    srv = rcp_udp_zone_server_new(RCP_ZONE_REAR_RIGHT, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    port = rcp_udp_zone_server_port(srv);

    ctrl = rcp_udp_controller_new(RCP_ZONE_REAR_RIGHT, "127.0.0.1", port);
    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

//cfusa:req REQ-UDP-016
static void test_addr_string_returns_bound_address(void)
{
    rcp_udp_zone_server_t *srv;
    char buf[64];
    size_t n;

    srv = rcp_udp_zone_server_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }

    n = rcp_udp_zone_server_addr_string(srv, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(buf, "127.0.0.1:"));

    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

//cfusa:req REQ-UDP-017
static void test_set_healthy_affects_published_status(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_controller_t *ctrl;
    rcp_context_t ctx;
    rcp_status_channel_t *ch = NULL;
    rcp_status_t st = {0};
    uint8_t payload[] = {0x02};
    uint16_t port;

    srv = rcp_udp_zone_server_new(RCP_ZONE_CENTRAL, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    port = rcp_udp_zone_server_port(srv);
    rcp_udp_zone_server_set_healthy(srv, false);

    ctrl = rcp_udp_controller_new(RCP_ZONE_CENTRAL, "127.0.0.1", port);
    ctx = rcp_context_background();
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));

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
    TEST_ASSERT_FALSE(st.healthy);

    rcp_status_free(&st);
    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

/* Exercises subs_remove()/zserv_subs_remove()/same_addr(): closing a
 * subscription's status channel directly (without closing the whole
 * controller) makes the background watcher thread notice, remove the
 * channel from the controller's own subscriber list, and send a real
 * Unsubscribe control frame that the zone server's serve loop processes
 * to remove the matching address from its own subscriber list. There is
 * no public API to directly observe either internal list, so this pins
 * the externally-visible contract: the round trip completes without
 * hanging or crashing and the channel ends up closed. */
//cfusa:req REQ-UDP-018
static void test_closing_channel_triggers_unsubscribe_round_trip(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_controller_t *ctrl;
    rcp_context_t ctx;
    rcp_status_channel_t *ch = NULL;
    rcp_status_t st = {0};
    uint8_t payload[] = {0x03};
    uint16_t port;
    uint64_t deadline;

    srv = rcp_udp_zone_server_new(RCP_ZONE_FRONT_RIGHT, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    port = rcp_udp_zone_server_port(srv);

    ctrl = rcp_udp_controller_new(RCP_ZONE_FRONT_RIGHT, "127.0.0.1", port);
    ctx = rcp_context_background();
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));

    /* Confirm the subscription is genuinely live before tearing it down. */
    deadline = rcp_monotonic_ms() + 2000;
    while (rcp_monotonic_ms() < deadline) {
        rcp_udp_zone_server_publish(srv, payload, sizeof(payload));
        if (rcp_status_channel_try_recv(ch, &st)) {
            rcp_status_free(&st);
            break;
        }
        test_sleep_ms(20);
    }

    /* Close the channel directly (not the controller) -- this is the path
     * that makes the watcher thread take the subs_remove()/Unsubscribe
     * branch rather than the plain-close branch. */
    rcp_status_channel_close(ch);

    deadline = rcp_monotonic_ms() + 1000;
    while (!rcp_status_channel_is_closed(ch) && rcp_monotonic_ms() < deadline) {
        test_sleep_ms(10);
    }
    TEST_ASSERT_TRUE(rcp_status_channel_is_closed(ch));

    /* Give the server's serve loop a moment to process the resulting
     * Unsubscribe datagram before tearing everything down. */
    test_sleep_ms(100);

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

//cfusa:req REQ-REG-002
//cfusa:req REQ-REG-003
//cfusa:req REQ-REG-004
//cfusa:req REQ-REG-006
static void test_registry_register_lookup_controllers_deregister_close(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_registry_t *reg;
    rcp_controller_t *ctrl;
    rcp_controller_t *looked_up = NULL;
    rcp_controller_t *listed[2] = {0};
    uint16_t port;

    srv = rcp_udp_zone_server_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    port = rcp_udp_zone_server_port(srv);

    reg  = rcp_udp_registry_new();
    ctrl = rcp_udp_controller_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", port);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_register(reg, ctrl));
    TEST_ASSERT_EQUAL(RCP_ERR_ALREADY_EXISTS, rcp_registry_register(reg, ctrl));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &looked_up));
    TEST_ASSERT_NOT_NULL(looked_up);
    rcp_controller_release(looked_up);

    TEST_ASSERT_EQUAL_UINT(1, rcp_registry_controllers(reg, listed, 2));
    rcp_controller_release(listed[0]);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_FOUND, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));

    rcp_registry_destroy(reg);
    rcp_controller_release(ctrl);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

//cfusa:req REQ-UDP-019
static void test_registry_dial_connects_and_registers(void)
{
    rcp_udp_zone_server_t *srv;
    rcp_registry_t *reg;
    rcp_controller_t *looked_up = NULL;
    uint16_t port;

    srv = rcp_udp_zone_server_new(RCP_ZONE_REAR_LEFT, "127.0.0.1", 0);
    if (!rcp_udp_zone_server_ok(srv)) {
        rcp_udp_zone_server_destroy(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    port = rcp_udp_zone_server_port(srv);
    reg  = rcp_udp_registry_new();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_udp_registry_dial(reg, RCP_ZONE_REAR_LEFT, "127.0.0.1", port));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_REAR_LEFT, &looked_up));
    TEST_ASSERT_NOT_NULL(looked_up);

    rcp_controller_release(looked_up);
    rcp_registry_destroy(reg);
    rcp_udp_zone_server_close(srv);
    rcp_udp_zone_server_destroy(srv);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_udp_send_roundtrip_over_loopback);
    RUN_TEST(test_udp_send_zone_mismatch);
    RUN_TEST(test_udp_publish_subscribe_over_loopback);
    RUN_TEST(test_udp_controller_zone_returns_configured_zone);
    RUN_TEST(test_addr_string_returns_bound_address);
    RUN_TEST(test_set_healthy_affects_published_status);
    RUN_TEST(test_closing_channel_triggers_unsubscribe_round_trip);
    RUN_TEST(test_registry_register_lookup_controllers_deregister_close);
    RUN_TEST(test_registry_dial_connects_and_registers);

    return UNITY_END();
}
