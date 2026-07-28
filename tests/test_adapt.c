/* RELAY conformance tests (RELAY spec §18.2, §5.1, §10.3, §14, §19.4).
 *
 * Verifies that c-RCP satisfies the mandatory RELAY-conformance
 * requirements this milestone adds: the Message envelope, the Caller
 * adapter (Adapt/send/call/subscribe/close), ToMessage/FromMessage field
 * mapping, and the RCP_SPEC_VERSION export.
 */
//cfusa:test REQ-RELAY-001
//cfusa:test REQ-RELAY-002
//cfusa:test REQ-RELAY-003
//cfusa:test REQ-RELAY-004
//cfusa:test REQ-RELAY-005
//cfusa:test REQ-RELAY-006
//cfusa:test REQ-RELAY-007
//cfusa:test REQ-RELAY-008
//cfusa:test REQ-RELAY-009
//cfusa:test REQ-RELAY-010
//cfusa:test REQ-RELAY-011
//cfusa:test REQ-RELAY-012
//cfusa:test REQ-RELAY-013
//cfusa:test REQ-RELAY-015
//cfusa:test REQ-RELAY-016
//cfusa:test REQ-RELAY-017
#include "unity.h"

#include <rcp/adapt.h>
#include <rcp/clock.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <relay/relay.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── §19.4: SpecVersion ────────────────────────────────────────────────────── */

static void test_rcp_spec_version_equals_relay_spec_version(void)
{
    TEST_ASSERT_EQUAL_STRING(RELAY_SPEC_VERSION, RCP_SPEC_VERSION);
    TEST_ASSERT_EQUAL_STRING("1.12", RCP_SPEC_VERSION);
}

/* ── §3: Protocol enum ─────────────────────────────────────────────────────── */

static void test_protocol_enum_values_match_spec(void)
{
    TEST_ASSERT_EQUAL(1, RELAY_PROTOCOL_CAN);
    TEST_ASSERT_EQUAL(2, RELAY_PROTOCOL_DDS);
    TEST_ASSERT_EQUAL(3, RELAY_PROTOCOL_LIN);
    TEST_ASSERT_EQUAL(4, RELAY_PROTOCOL_MQTT);
    TEST_ASSERT_EQUAL(5, RELAY_PROTOCOL_RCP);
    TEST_ASSERT_EQUAL(6, RELAY_PROTOCOL_SOMEIP);
}

static void test_protocol_string_unique_nonempty(void)
{
    TEST_ASSERT_EQUAL_STRING("RCP", relay_protocol_string(RELAY_PROTOCOL_RCP));
    TEST_ASSERT_EQUAL_STRING("CAN", relay_protocol_string(RELAY_PROTOCOL_CAN));
}

/* ── §4/§18.2: relay_message_t lifecycle ───────────────────────────────────── */

static void test_message_init_then_free_is_safe(void)
{
    relay_message_t m;
    relay_message_init(&m);
    TEST_ASSERT_NULL(m.id);
    TEST_ASSERT_EQUAL_UINT(0, m.meta_len);
    relay_message_free(&m); /* must not crash on an already-empty message */
}

static void test_message_set_id_replaces_prior_value(void)
{
    relay_message_t m;
    relay_message_init(&m);

    TEST_ASSERT_TRUE(relay_message_set_id(&m, "FrontLeft"));
    TEST_ASSERT_EQUAL_STRING("FrontLeft", m.id);

    TEST_ASSERT_TRUE(relay_message_set_id(&m, "RearRight"));
    TEST_ASSERT_EQUAL_STRING("RearRight", m.id);

    relay_message_free(&m);
}

static void test_message_meta_set_upserts_and_get_looks_up(void)
{
    relay_message_t m;
    relay_message_init(&m);

    TEST_ASSERT_TRUE(relay_message_set_meta(&m, "rcp.priority", "high"));
    TEST_ASSERT_EQUAL_UINT(1, m.meta_len);
    TEST_ASSERT_EQUAL_STRING("high", relay_message_get_meta(&m, "rcp.priority"));

    /* Upsert: same key, new value, must not append a duplicate entry. */
    TEST_ASSERT_TRUE(relay_message_set_meta(&m, "rcp.priority", "critical"));
    TEST_ASSERT_EQUAL_UINT(1, m.meta_len);
    TEST_ASSERT_EQUAL_STRING("critical", relay_message_get_meta(&m, "rcp.priority"));

    TEST_ASSERT_NULL(relay_message_get_meta(&m, "no-such-key"));

    relay_message_free(&m);
}

/* ── §18.2: Channel<T>-equivalent push/recv/close semantics ───────────────── */

static void test_channel_push_returns_false_when_full(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(2);
    relay_message_t a, b, c;

    relay_message_init(&a);
    relay_message_init(&b);
    relay_message_init(&c);

    TEST_ASSERT_TRUE(relay_message_channel_push(ch, &a));
    TEST_ASSERT_TRUE(relay_message_channel_push(ch, &b));
    TEST_ASSERT_FALSE(relay_message_channel_push(ch, &c)); /* full */

    relay_message_channel_release(ch);
}

static void test_channel_push_returns_false_after_close(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(8);
    relay_message_t m;
    relay_message_init(&m);

    relay_message_channel_close(ch);
    TEST_ASSERT_FALSE(relay_message_channel_push(ch, &m));

    relay_message_channel_release(ch);
}

static void test_channel_recv_returns_false_after_close_with_empty_queue(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(8);
    relay_message_t out;

    relay_message_channel_close(ch);
    TEST_ASSERT_FALSE(relay_message_channel_recv(ch, &out));

    relay_message_channel_release(ch);
}

static void test_channel_is_closed_reflects_close_state(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(8);

    TEST_ASSERT_FALSE(relay_message_channel_is_closed(ch));
    relay_message_channel_close(ch);
    TEST_ASSERT_TRUE(relay_message_channel_is_closed(ch));

    relay_message_channel_release(ch);
}

/* ── §14: SubscriberOptions defaults ───────────────────────────────────────── */

static void test_subscriber_options_defaults(void)
{
    relay_subscriber_options_t opts = relay_subscriber_options_default();
    TEST_ASSERT_EQUAL_UINT(64, opts.channel_depth);
    TEST_ASSERT_EQUAL(RELAY_BACKPRESSURE_DROP_NEWEST, opts.back_pressure);
    TEST_ASSERT_NULL(opts.topic_name);
}

/* ── §15.7.5: status_to_message golden vector (RELAY spec/vectors/rcp-status.json) ──
 *
 * Pins rcp_status_to_message() to the canonical reference vector, the same
 * one cpp-RCP's own test suite pins against. Mandatory fields (protocol,
 * id, payload, seq, meta["rcp.healthy"]) must match losslessly; timestamp
 * is "ignored on receive" per §15.7 and is not pinned.
 */
static void test_status_to_message_matches_golden_vector(void)
{
    /* value: zone=1 (FrontLeft), seq=3, healthy=true, payload="AQ==" (byte 0x01) */
    uint8_t payload_byte = 0x01;
    rcp_status_t s = {0};
    relay_message_t msg;

    s.zone         = RCP_ZONE_FRONT_LEFT;
    s.seq          = 3;
    s.healthy      = true;
    s.payload.data = &payload_byte;
    s.payload.len  = 1;

    msg = rcp_status_to_message(&s);

    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, msg.protocol);
    TEST_ASSERT_EQUAL_STRING("FrontLeft", msg.id);
    TEST_ASSERT_EQUAL_UINT64(3, msg.seq);
    TEST_ASSERT_EQUAL_UINT(1, msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0x01, msg.payload.data[0]);
    TEST_ASSERT_EQUAL_STRING("true", relay_message_get_meta(&msg, "rcp.healthy"));

    relay_message_free(&msg);
}

static void test_response_to_message_maps_status_to_meta(void)
{
    rcp_response_t r = {0};
    relay_message_t msg;

    r.zone   = RCP_ZONE_REAR_RIGHT;
    r.status = RCP_RESPONSE_ERROR;

    msg = rcp_response_to_message(&r);

    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, msg.protocol);
    TEST_ASSERT_EQUAL_STRING("RearRight", msg.id);
    TEST_ASSERT_EQUAL_STRING("1", relay_message_get_meta(&msg, "rcp.status")); /* RCP_RESPONSE_ERROR == 1 */

    relay_message_free(&msg);
}

static void test_message_to_command_maps_id_and_meta(void)
{
    relay_message_t msg;
    rcp_command_t cmd;

    relay_message_init(&msg);
    relay_message_set_id(&msg, "RearLeft");
    relay_message_set_meta(&msg, "rcp.cmd_type", "set");
    relay_message_set_meta(&msg, "rcp.priority", "critical");

    cmd = rcp_message_to_command(&msg);

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_LEFT, cmd.zone);
    TEST_ASSERT_EQUAL(RCP_CMD_SET, cmd.type);
    TEST_ASSERT_EQUAL(RCP_PRIORITY_CRITICAL, cmd.priority);

    relay_message_free(&msg);
}

static void test_message_to_command_defaults_when_meta_absent(void)
{
    relay_message_t msg;
    rcp_command_t cmd;

    relay_message_init(&msg);
    relay_message_set_id(&msg, "Central");

    cmd = rcp_message_to_command(&msg);

    TEST_ASSERT_EQUAL(RCP_ZONE_CENTRAL, cmd.zone);
    TEST_ASSERT_EQUAL(RCP_CMD_NOOP, cmd.type);
    TEST_ASSERT_EQUAL(RCP_PRIORITY_NORMAL, cmd.priority);

    relay_message_free(&msg);
}

/* ── §10.3: rcp_adapt() wraps a controller as a relay Caller ──────────────── */

static void test_adapt_returns_non_null(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);

    TEST_ASSERT_NOT_NULL(caller);

    rcp_relay_caller_release(caller);
}

static void test_adapt_protocol_returns_rcp(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);

    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, rcp_relay_caller_protocol(caller));

    rcp_relay_caller_release(caller);
}

static void echo_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    (void)user_data;
    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_OK;
}

static void test_adapt_call_sends_command_and_returns_response(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, echo_handler, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t req, resp = {0};
    int ec;

    relay_message_init(&req);
    req.protocol = RELAY_PROTOCOL_RCP;
    relay_message_set_id(&req, "FrontLeft");
    relay_message_set_meta(&req, "rcp.cmd_type", "get");

    ec = rcp_relay_caller_call(caller, &ctx, &req, &resp);

    TEST_ASSERT_EQUAL(RCP_OK, ec);
    TEST_ASSERT_EQUAL_STRING("FrontLeft", resp.id);
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, resp.protocol);

    relay_message_free(&req);
    relay_message_free(&resp);
    rcp_relay_caller_release(caller);
}

static void test_adapt_send_succeeds(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, echo_handler, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t msg;

    relay_message_init(&msg);
    relay_message_set_id(&msg, "FrontLeft");
    relay_message_set_meta(&msg, "rcp.cmd_type", "set");

    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_send(caller, &ctx, &msg));

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
}

static bool wait_recv(relay_message_channel_t *ch, relay_message_t *out, unsigned timeout_ms)
{
    uint64_t deadline = rcp_monotonic_ms() + timeout_ms;
    while (rcp_monotonic_ms() < deadline) {
        if (relay_message_channel_try_recv(ch, out)) return true;
    }
    return false;
}

static void test_adapt_subscribe_returns_valid_channel(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);
    relay_subscriber_options_t opts = relay_subscriber_options_default();
    relay_message_channel_t *ch = NULL;
    relay_message_t msg;
    uint8_t payload[] = {0x01, 0x02};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_subscribe(caller, &opts, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_mock_controller_publish(ctrl, payload, sizeof(payload));

    TEST_ASSERT_TRUE(wait_recv(ch, &msg, 500));
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, msg.protocol);
    TEST_ASSERT_EQUAL_STRING("FrontLeft", msg.id);
    TEST_ASSERT_NOT_NULL(relay_message_get_meta(&msg, "rcp.healthy"));

    relay_message_free(&msg);
    relay_message_channel_release(ch);
    rcp_relay_caller_release(caller);
}

static void test_adapt_close_is_idempotent(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_close(caller));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_close(caller));

    rcp_relay_caller_release(caller);
}

static void test_caller_retain_returns_same_pointer_and_keeps_it_alive(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);
    rcp_relay_caller_t *retained = rcp_relay_caller_retain(caller);

    TEST_ASSERT_TRUE(retained == caller);

    /* One release just drops the retain()'d share; the caller must still
     * be usable afterwards. */
    rcp_relay_caller_release(retained);
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, rcp_relay_caller_protocol(caller));

    TEST_ASSERT_NULL(rcp_relay_caller_retain(NULL));

    rcp_relay_caller_release(caller);
}

/* ── §5.2 error wrapping ───────────────────────────────────────────────────── */

static void test_send_closed_error_is_relay_equivalent(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t msg;
    relay_errc_t relay_ec;
    int ec;

    rcp_controller_close(ctrl);

    relay_message_init(&msg);
    relay_message_set_id(&msg, "FrontLeft");

    ec = rcp_relay_caller_send(caller, &ctx, &msg);
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, ec);
    TEST_ASSERT_TRUE(rcp_errc_to_relay_errc(ec, &relay_ec));
    TEST_ASSERT_EQUAL(RELAY_ERRC_CLOSED, relay_ec);

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
}

static void test_call_timeout_error_is_relay_equivalent(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_relay_caller_t *caller = rcp_adapt(ctrl);
    relay_context_t ctx = relay_context_with_deadline_ms(1); /* already expired */
    relay_message_t req, resp = {0};
    relay_errc_t relay_ec;
    int ec;

    relay_message_init(&req);
    relay_message_set_id(&req, "FrontLeft");

    ec = rcp_relay_caller_call(caller, &ctx, &req, &resp);
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, ec);
    TEST_ASSERT_TRUE(rcp_errc_to_relay_errc(ec, &relay_ec));
    TEST_ASSERT_EQUAL(RELAY_ERRC_TIMEOUT, relay_ec);

    relay_message_free(&req);
    rcp_relay_caller_release(caller);
}

static void test_ok_and_rcp_specific_errors_have_no_relay_equivalent(void)
{
    relay_errc_t relay_ec;

    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_OK, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ERR_ZONE_MISMATCH, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ERR_NOT_FOUND, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ERR_FORBIDDEN, &relay_ec));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rcp_spec_version_equals_relay_spec_version);
    RUN_TEST(test_protocol_enum_values_match_spec);
    RUN_TEST(test_protocol_string_unique_nonempty);

    RUN_TEST(test_message_init_then_free_is_safe);
    RUN_TEST(test_message_set_id_replaces_prior_value);
    RUN_TEST(test_message_meta_set_upserts_and_get_looks_up);

    RUN_TEST(test_channel_push_returns_false_when_full);
    RUN_TEST(test_channel_push_returns_false_after_close);
    RUN_TEST(test_channel_recv_returns_false_after_close_with_empty_queue);
    RUN_TEST(test_channel_is_closed_reflects_close_state);

    RUN_TEST(test_subscriber_options_defaults);

    RUN_TEST(test_status_to_message_matches_golden_vector);
    RUN_TEST(test_response_to_message_maps_status_to_meta);
    RUN_TEST(test_message_to_command_maps_id_and_meta);
    RUN_TEST(test_message_to_command_defaults_when_meta_absent);

    RUN_TEST(test_adapt_returns_non_null);
    RUN_TEST(test_adapt_protocol_returns_rcp);
    RUN_TEST(test_adapt_call_sends_command_and_returns_response);
    RUN_TEST(test_adapt_send_succeeds);
    RUN_TEST(test_adapt_subscribe_returns_valid_channel);
    RUN_TEST(test_adapt_close_is_idempotent);
    RUN_TEST(test_caller_retain_returns_same_pointer_and_keeps_it_alive);

    RUN_TEST(test_send_closed_error_is_relay_equivalent);
    RUN_TEST(test_call_timeout_error_is_relay_equivalent);
    RUN_TEST(test_ok_and_rcp_specific_errors_have_no_relay_equivalent);

    return UNITY_END();
}
