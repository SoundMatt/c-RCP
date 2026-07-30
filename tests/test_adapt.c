/* SPDX-License-Identifier: MPL-2.0 */
/* RELAY conformance tests (RELAY spec §18.2, §5.1, §10.3, §15.7.5, §19.4).
 *
 * Verifies that c-RCP satisfies the mandatory RELAY-conformance
 * requirements this milestone adds: the Message envelope, the Caller
 * adapter (Adapt/send/call/subscribe/close) now bound to an
 * rcp_avtp_transport_t and one TC18 endpoint kind, the per-op
 * rcp_message_to_request()/rcp_response_to_message() field mapping (see
 * adapt.h's file header table), and the RCP_SPEC_VERSION export.
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
#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/discovery.h>
#include <rcp/ep_can.h>
#include <rcp/ep_gpio.h>
#include <rcp/ep_i2c.h>
#include <rcp/ep_mdio.h>
#include <rcp/ep_spi.h>
#include <rcp/ep_wakeup.h>
#include <rcp/power.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/shmem.h>
#include <relay/relay.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_stream_id_t make_stream(uint16_t unique_id)
{
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    return rcp_stream_id_make(mac, unique_id);
}

/* ── §19.4: SpecVersion ────────────────────────────────────────────────────── */

static void test_rcp_spec_version_equals_relay_spec_version(void)
{
    TEST_ASSERT_EQUAL_STRING(RELAY_SPEC_VERSION, RCP_SPEC_VERSION);
    TEST_ASSERT_EQUAL_STRING("2.0", RCP_SPEC_VERSION);
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

    TEST_ASSERT_TRUE(relay_message_set_id(&m, "ep-gpio-0"));
    TEST_ASSERT_EQUAL_STRING("ep-gpio-0", m.id);

    TEST_ASSERT_TRUE(relay_message_set_id(&m, "ep-pwm-1"));
    TEST_ASSERT_EQUAL_STRING("ep-pwm-1", m.id);

    relay_message_free(&m);
}

static void test_message_meta_set_upserts_and_get_looks_up(void)
{
    relay_message_t m;
    relay_message_init(&m);

    TEST_ASSERT_TRUE(relay_message_set_meta(&m, "rcp.adapt.op", "gpio_read"));
    TEST_ASSERT_EQUAL_UINT(1, m.meta_len);
    TEST_ASSERT_EQUAL_STRING("gpio_read", relay_message_get_meta(&m, "rcp.adapt.op"));

    /* Upsert: same key, new value, must not append a duplicate entry. */
    TEST_ASSERT_TRUE(relay_message_set_meta(&m, "rcp.adapt.op", "gpio_write"));
    TEST_ASSERT_EQUAL_UINT(1, m.meta_len);
    TEST_ASSERT_EQUAL_STRING("gpio_write", relay_message_get_meta(&m, "rcp.adapt.op"));

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

/* ── rcp_adapt_op_t <-> kind/string ─────────────────────────────────────────── */

static void test_op_kind_families(void)
{
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_GPIO, rcp_adapt_op_kind(RCP_ADAPT_OP_GPIO_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_GPIO, rcp_adapt_op_kind(RCP_ADAPT_OP_GPIO_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_UART, rcp_adapt_op_kind(RCP_ADAPT_OP_UART_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_UART, rcp_adapt_op_kind(RCP_ADAPT_OP_UART_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_WAKEUP, rcp_adapt_op_kind(RCP_ADAPT_OP_WAKEUP_SLEEPCMD));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_WAKEUP, rcp_adapt_op_kind(RCP_ADAPT_OP_WAKEUP_WAKEUP));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_DISCOVERY, rcp_adapt_op_kind(RCP_ADAPT_OP_DISCOVERY));
}

static void test_op_string_round_trips_every_op(void)
{
    static const rcp_adapt_op_t ALL_OPS[] = {
        RCP_ADAPT_OP_GPIO_READ,       RCP_ADAPT_OP_GPIO_WRITE,   RCP_ADAPT_OP_SPI_TRANSFER,
        RCP_ADAPT_OP_I2C_TRANSFER,    RCP_ADAPT_OP_UART_WRITE,   RCP_ADAPT_OP_UART_READ,
        RCP_ADAPT_OP_ADC_READ,        RCP_ADAPT_OP_PWM_OUT_READ, RCP_ADAPT_OP_PWM_OUT_WRITE,
        RCP_ADAPT_OP_PWM_IN_READ,     RCP_ADAPT_OP_LIN_COMMAND,  RCP_ADAPT_OP_CAN_FRAME,
        RCP_ADAPT_OP_ISELED_COMMAND,  RCP_ADAPT_OP_MDIO_READ,    RCP_ADAPT_OP_MDIO_WRITE,
        RCP_ADAPT_OP_WAKEUP_SLEEPCMD, RCP_ADAPT_OP_WAKEUP_WAKEUP, RCP_ADAPT_OP_DISCOVERY,
    };
    size_t i;
    for (i = 0; i < sizeof(ALL_OPS) / sizeof(ALL_OPS[0]); i++) {
        rcp_adapt_op_t parsed;
        const char *name = rcp_adapt_op_string(ALL_OPS[i]);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(rcp_adapt_op_from_string(name, &parsed));
        TEST_ASSERT_EQUAL(ALL_OPS[i], parsed);
    }
}

static void test_op_from_string_rejects_unknown_and_null(void)
{
    rcp_adapt_op_t parsed;
    TEST_ASSERT_FALSE(rcp_adapt_op_from_string("no-such-op", &parsed));
    TEST_ASSERT_FALSE(rcp_adapt_op_from_string(NULL, &parsed));
}

static void test_adapt_strerror_never_null(void)
{
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_OK));
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_ERR_ENCODE));
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_ERR_DECODE));
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_ERR_NOT_SUPPORTED));
}

/* ── §15.7.5: rcp_message_to_request()/rcp_response_to_message() field mapping ── */

static void test_gpio_read_request_round_trips(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t out_txn = 0;

    relay_message_init(&msg);

    req = rcp_message_to_request(RCP_ADAPT_OP_GPIO_READ, 7, make_stream(1), &msg, 42, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                       rcp_ep_gpio_decode_read_request(req.data, req.len, 7, &out_txn));
    TEST_ASSERT_EQUAL_UINT8(42, out_txn);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_gpio_write_request_maps_payload_and_evt_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN] = {0x00, 0x00, 0x00, 0x05};
    uint32_t out_bitmask = 0;
    uint8_t out_evt = 0xFF;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(payload, sizeof(payload));
    relay_message_set_meta(&msg, "rcp.gpio.evt", "1"); /* OR */

    req = rcp_message_to_request(RCP_ADAPT_OP_GPIO_WRITE, 7, make_stream(1), &msg, 9, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                       rcp_ep_gpio_decode_write_request(req.data, req.len, 7, &out_bitmask,
                                                         &out_evt, &out_txn));
    TEST_ASSERT_EQUAL_UINT32(5, out_bitmask);
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_GPIO_WRITE_OR, out_evt);
    TEST_ASSERT_EQUAL_UINT8(9, out_txn);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_gpio_write_request_rejects_wrong_payload_length(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;
    uint8_t payload[2] = {0, 0};

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(payload, sizeof(payload));

    req = rcp_message_to_request(RCP_ADAPT_OP_GPIO_WRITE, 7, make_stream(1), &msg, 9, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_gpio_response_maps_bitmask_into_payload(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(7, 0xAABBCCDD, 3, true, 123456);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_GPIO_READ, 7, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, msg.protocol);
    /* RELAY spec v2.0 §15.7.5: a response Message's ID is the responding
     * endpoint's ByteBusID as a decimal string -- see issue #107. */
    TEST_ASSERT_EQUAL_STRING("7", msg.id);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_GPIO_PAYLOAD_LEN, msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, msg.payload.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, msg.payload.data[3]);
    TEST_ASSERT_EQUAL_STRING("true", relay_message_get_meta(&msg, "rcp.timed"));
    TEST_ASSERT_EQUAL_STRING("123456", relay_message_get_meta(&msg, "rcp.timestamp"));
    TEST_ASSERT_EQUAL_STRING("3", relay_message_get_meta(&msg, "rcp.transaction_num"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_response_to_message_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(7, 1, 1, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_GPIO_READ, 8 /* wrong bus */, frame.data, frame.len,
                                   &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);
    TEST_ASSERT_NULL(msg.id);
    TEST_ASSERT_NULL(msg.payload.data);

    rcp_bytes_free(&frame);
}

static void test_spi_transfer_request_maps_channel_meta_and_payload(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0x11, 0x22, 0x33};
    uint8_t out_channel = 0xFF;
    const uint8_t *out_tx = NULL;
    size_t out_tx_len = 0;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));
    relay_message_set_meta(&msg, "rcp.spi.channel", "3");

    req = rcp_message_to_request(RCP_ADAPT_OP_SPI_TRANSFER, 2, make_stream(1), &msg, 5, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
                       rcp_ep_spi_decode_transfer_request(req.data, req.len, 2, &out_channel,
                                                           &out_tx, &out_tx_len, &out_txn));
    TEST_ASSERT_EQUAL_UINT8(3, out_channel);
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_i2c_transfer_request_has_no_channel_selector(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0xAA};

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));

    req = rcp_message_to_request(RCP_ADAPT_OP_I2C_TRANSFER, 1, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_uart_read_request_requires_read_size_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg); /* no rcp.uart.read_size set */

    req = rcp_message_to_request(RCP_ADAPT_OP_UART_READ, 1, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_can_frame_request_rejects_xl_formats_as_out_of_scope(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.can.frame_format", "4"); /* RCP_EP_CAN_FRAME_XL_CLASSICAL_PL */
    relay_message_set_meta(&msg, "rcp.can.arbitration_id", "0");

    req = rcp_message_to_request(RCP_ADAPT_OP_CAN_FRAME, 3, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_can_frame_request_accepts_classical_format(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0x01, 0x02};

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));
    relay_message_set_meta(&msg, "rcp.can.frame_format", "0"); /* RCP_EP_CAN_FRAME_CBFF */
    relay_message_set_meta(&msg, "rcp.can.arbitration_id", "100");

    req = rcp_message_to_request(RCP_ADAPT_OP_CAN_FRAME, 3, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_mdio_write_request_maps_addr_meta_and_packed_words(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t words_payload[4];
    rcp_ep_mdio_addr_t out_addr;
    const uint8_t *out_words = NULL;
    size_t out_word_count = 0;
    uint8_t out_txn = 0;

    rcp_ep_mdio_word_encode(0x1234, &words_payload[0]);
    rcp_ep_mdio_word_encode(0x5678, &words_payload[2]);

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(words_payload, sizeof(words_payload));
    relay_message_set_meta(&msg, "rcp.mdio.clause", "0");
    relay_message_set_meta(&msg, "rcp.mdio.prtad", "1");
    relay_message_set_meta(&msg, "rcp.mdio.devad", "0");
    relay_message_set_meta(&msg, "rcp.mdio.regad", "5");

    req = rcp_message_to_request(RCP_ADAPT_OP_MDIO_WRITE, 4, make_stream(1), &msg, 7, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK,
                       rcp_ep_mdio_decode_write_request(req.data, req.len, 4, &out_addr,
                                                         &out_words, &out_word_count, &out_txn));
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_CLAUSE_22, out_addr.clause);
    TEST_ASSERT_EQUAL_UINT8(1, out_addr.prtad);
    TEST_ASSERT_EQUAL_UINT16(5, out_addr.regad);
    TEST_ASSERT_EQUAL_UINT(2, out_word_count);
    TEST_ASSERT_EQUAL_UINT16(0x1234, rcp_ep_mdio_unpack_word_at(out_words, 0));
    TEST_ASSERT_EQUAL_UINT16(0x5678, rcp_ep_mdio_unpack_word_at(out_words, 1));

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_wakeup_sleepcmd_round_trips_without_timed_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    rcp_bytes_t resp_frame;
    relay_message_t resp;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.wakeup.target_mode", "1"); /* RCP_PWRMODE_STANDBY */

    req = rcp_message_to_request(RCP_ADAPT_OP_WAKEUP_SLEEPCMD, 0, make_stream(1), &msg, 6, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    rcp_bytes_free(&req);
    relay_message_free(&msg);

    resp_frame = rcp_ep_wakeup_encode_sleepcmd_response(0, RCP_PWRMODE_ENTRY_OK, 6);
    TEST_ASSERT_NOT_NULL(resp_frame.data);

    err = RCP_ADAPT_ERR_DECODE;
    resp = rcp_response_to_message(RCP_ADAPT_OP_WAKEUP_SLEEPCMD, 0, resp_frame.data,
                                    resp_frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_STRING("0", relay_message_get_meta(&resp, "rcp.wakeup.result"));
    TEST_ASSERT_NULL(relay_message_get_meta(&resp, "rcp.timed")); /* no GBB variant exists */

    relay_message_free(&resp);
    rcp_bytes_free(&resp_frame);
}

static void test_discovery_response_maps_fields_and_server_stream_id(void)
{
    rcp_regmap_general_t map;
    rcp_bytes_t frame;
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;
    char expected_id[17];

    rcp_regmap_general_init(&map);
    map.vendor_id    = 0x1234;
    map.device_id    = 0x5678;
    map.svr_ep_count = 3;

    frame = rcp_discovery_encode_response(&map, (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, 9,
                                           make_stream(0xBEEF));
    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_DISCOVERY, 0, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_STRING("4660", relay_message_get_meta(&msg, "rcp.discovery.vendor_id"));
    TEST_ASSERT_EQUAL_STRING("22136", relay_message_get_meta(&msg, "rcp.discovery.device_id"));
    TEST_ASSERT_EQUAL_STRING("3", relay_message_get_meta(&msg, "rcp.discovery.svr_ep_count"));

    snprintf(expected_id, sizeof(expected_id), "%016llx",
             (unsigned long long)rcp_stream_id_to_u64(make_stream(0xBEEF)));
    TEST_ASSERT_EQUAL_STRING(expected_id, msg.id);

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

/* ── §10.3: rcp_adapt() wraps an rcp_avtp_transport_t as a relay Caller ─────── */

static void test_adapt_returns_non_null(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_NOT_NULL(caller);

    rcp_relay_caller_release(caller);
    /* rcp_adapt() retains its own reference to t (adapt.c's
     * rcp_relay_caller_release() only drops that one); the reference
     * this test obtained from rcp_avtp_loopback_transport_new() is
     * still this test's own to release. */
    rcp_avtp_transport_release(t);
}

static void test_adapt_protocol_returns_rcp(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, rcp_relay_caller_protocol(caller));

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_adapt_rejects_op_outside_bound_kind(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    relay_context_t ctx = relay_context_with_timeout_ms(200);
    relay_message_t msg;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "i2c_transfer"); /* wrong kind */

    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, rcp_relay_caller_send(caller, &ctx, &msg));

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_adapt_send_transmits_a_valid_gpio_write_request(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    rcp_context_t sctx = rcp_context_with_timeout_ms(1000);
    relay_message_t msg;
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN] = {0, 0, 0, 0x2A};
    uint8_t recv_buf[512];
    size_t recv_len = 0;
    rcp_avtp_ntscf_header_t hdr;
    const uint8_t *acf = NULL;
    size_t acf_len = 0;
    uint32_t out_bitmask = 0;
    uint8_t out_evt = 0xFF;
    uint8_t out_txn = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "gpio_write");
    msg.payload = relay_bytes_dup(payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, rcp_relay_caller_send(caller, &ctx, &msg));

    TEST_ASSERT_EQUAL(RCP_OK,
                       rcp_avtp_transport_recv(server_t, &sctx, recv_buf, sizeof(recv_buf),
                                                &recv_len));
    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(recv_buf, recv_len, &hdr, &acf, &acf_len));
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                       rcp_ep_gpio_decode_write_request(acf, acf_len, 9, &out_bitmask, &out_evt,
                                                         &out_txn));
    TEST_ASSERT_EQUAL_UINT32(0x2A, out_bitmask);

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    /* caller only holds its own retained reference to client_t (see
     * test_adapt_returns_non_null()'s comment); this test's own
     * reference from rcp_shmem_avtp_pair_new() is separate. */
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

static void test_adapt_call_gpio_read_returns_mapped_response(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t req, resp = {0};
    rcp_bytes_t resp_acf, resp_frame;
    rcp_avtp_ntscf_header_t hdr;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    /* Pre-queue the server's own reply on the server->client leg -- see
     * this file's own note on why the shmem pair's two independent FIFOs
     * make this safe to do before the client's call() even sends its
     * request. */
    resp_acf = rcp_ep_gpio_encode_response(9, 0x000000FF, 1, false, 0);
    TEST_ASSERT_NOT_NULL(resp_acf.data);
    hdr.sv = 1;
    hdr.version = 0;
    hdr.ntscf_data_length = 0;
    hdr.sequence_num = 1;
    hdr.stream_id = make_stream(2); /* the server's own stream_id */
    resp_frame = rcp_avtp_encode_ntscf(&hdr, resp_acf.data, resp_acf.len);
    TEST_ASSERT_NOT_NULL(resp_frame.data);
    rcp_bytes_free(&resp_acf);
    TEST_ASSERT_EQUAL(RCP_OK,
                       rcp_avtp_transport_send(server_t, resp_frame.data, resp_frame.len));
    rcp_bytes_free(&resp_frame);

    relay_message_init(&req);
    relay_message_set_meta(&req, "rcp.adapt.op", "gpio_read");

    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, rcp_relay_caller_call(caller, &ctx, &req, &resp));
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, resp.protocol);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_GPIO_PAYLOAD_LEN, resp.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0xFF, resp.payload.data[3]);

    relay_message_free(&req);
    relay_message_free(&resp);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

static void test_adapt_subscribe_is_not_supported(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    relay_subscriber_options_t opts = relay_subscriber_options_default();
    relay_message_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_NOT_SUPPORTED,
                       rcp_relay_caller_subscribe(caller, &opts, &ch));
    TEST_ASSERT_NULL(ch);

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_adapt_close_is_idempotent(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_close(caller));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_close(caller));

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_caller_retain_returns_same_pointer_and_keeps_it_alive(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    rcp_relay_caller_t *retained = rcp_relay_caller_retain(caller);

    TEST_ASSERT_TRUE(retained == caller);

    /* One release just drops the retain()'d share; the caller must still
     * be usable afterwards. */
    rcp_relay_caller_release(retained);
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, rcp_relay_caller_protocol(caller));

    TEST_ASSERT_NULL(rcp_relay_caller_retain(NULL));

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

/* ── §5.2 error wrapping ───────────────────────────────────────────────────── */

static void test_send_closed_error_is_relay_equivalent(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t msg;
    relay_errc_t relay_ec;
    int ec;

    rcp_avtp_transport_close(t);

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "gpio_read");

    ec = rcp_relay_caller_send(caller, &ctx, &msg);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_CLOSED, ec);
    TEST_ASSERT_TRUE(rcp_errc_to_relay_errc(ec, &relay_ec));
    TEST_ASSERT_EQUAL(RELAY_ERRC_CLOSED, relay_ec);

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_call_timeout_error_is_relay_equivalent(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_deadline_ms(1); /* already expired */
    relay_message_t req, resp = {0};
    relay_errc_t relay_ec;
    int ec;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    relay_message_init(&req);
    relay_message_set_meta(&req, "rcp.adapt.op", "gpio_read");

    ec = rcp_relay_caller_call(caller, &ctx, &req, &resp);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_TIMEOUT, ec);
    TEST_ASSERT_TRUE(rcp_errc_to_relay_errc(ec, &relay_ec));
    TEST_ASSERT_EQUAL(RELAY_ERRC_TIMEOUT, relay_ec);

    relay_message_free(&req);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

static void test_ok_and_adapt_specific_errors_have_no_relay_equivalent(void)
{
    relay_errc_t relay_ec;

    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_OK, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_ERR_ENCODE, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_ERR_DECODE, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_ERR_NOT_SUPPORTED, &relay_ec));
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
    RUN_TEST(test_channel_is_closed_reflects_close_state);

    RUN_TEST(test_subscriber_options_defaults);

    RUN_TEST(test_op_kind_families);
    RUN_TEST(test_op_string_round_trips_every_op);
    RUN_TEST(test_op_from_string_rejects_unknown_and_null);
    RUN_TEST(test_adapt_strerror_never_null);

    RUN_TEST(test_gpio_read_request_round_trips);
    RUN_TEST(test_gpio_write_request_maps_payload_and_evt_meta);
    RUN_TEST(test_gpio_write_request_rejects_wrong_payload_length);
    RUN_TEST(test_gpio_response_maps_bitmask_into_payload);
    RUN_TEST(test_response_to_message_rejects_wrong_bus);
    RUN_TEST(test_spi_transfer_request_maps_channel_meta_and_payload);
    RUN_TEST(test_i2c_transfer_request_has_no_channel_selector);
    RUN_TEST(test_uart_read_request_requires_read_size_meta);
    RUN_TEST(test_can_frame_request_rejects_xl_formats_as_out_of_scope);
    RUN_TEST(test_can_frame_request_accepts_classical_format);
    RUN_TEST(test_mdio_write_request_maps_addr_meta_and_packed_words);
    RUN_TEST(test_wakeup_sleepcmd_round_trips_without_timed_meta);
    RUN_TEST(test_discovery_response_maps_fields_and_server_stream_id);

    RUN_TEST(test_adapt_returns_non_null);
    RUN_TEST(test_adapt_protocol_returns_rcp);
    RUN_TEST(test_adapt_rejects_op_outside_bound_kind);
    RUN_TEST(test_adapt_send_transmits_a_valid_gpio_write_request);
    RUN_TEST(test_adapt_call_gpio_read_returns_mapped_response);
    RUN_TEST(test_adapt_subscribe_is_not_supported);
    RUN_TEST(test_adapt_close_is_idempotent);
    RUN_TEST(test_caller_retain_returns_same_pointer_and_keeps_it_alive);

    RUN_TEST(test_send_closed_error_is_relay_equivalent);
    RUN_TEST(test_call_timeout_error_is_relay_equivalent);
    RUN_TEST(test_ok_and_adapt_specific_errors_have_no_relay_equivalent);

    return UNITY_END();
}
