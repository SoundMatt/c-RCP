/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-GPIO-001
//cfusa:test REQ-GPIO-002
//cfusa:test REQ-GPIO-003
//cfusa:test REQ-GPIO-004
//cfusa:test REQ-GPIO-005
//cfusa:test REQ-GPIO-006
//cfusa:test REQ-GPIO-007
//cfusa:test REQ-GPIO-008
//cfusa:test REQ-GPIO-009
//cfusa:test REQ-GPIO-010
//cfusa:test REQ-GPIO-011
//cfusa:test REQ-GPIO-012
//cfusa:test REQ-GPIO-013
//cfusa:test REQ-GPIO-014
//cfusa:test REQ-GPIO-015
//cfusa:test REQ-GPIO-016
//cfusa:test REQ-GPIO-017
//cfusa:test REQ-GPIO-018
//cfusa:test REQ-GPIO-019
//cfusa:test REQ-GPIO-020
//cfusa:test REQ-GPIO-021
//cfusa:test REQ-GPIO-022
//cfusa:test REQ-GPIO-023
//cfusa:test REQ-GPIO-024
//cfusa:test REQ-GPIO-025
//cfusa:test REQ-GPIO-026
//cfusa:test REQ-GPIO-027
//cfusa:test REQ-GPIO-028
//cfusa:test REQ-GPIO-029
//cfusa:test REQ-GPIO-030
//cfusa:test REQ-GPIO-031
//cfusa:test REQ-GPIO-032
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_gpio.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Pin addressing ────────────────────────────────────────────────────────── */

static void test_pin_index_valid_bounds(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_index_valid(0));
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_index_valid(31));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_index_valid(32));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_index_valid(255));
}

static void test_pin_mask(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, rcp_ep_gpio_pin_mask(0));
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, rcp_ep_gpio_pin_mask(31));
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_ep_gpio_pin_mask(32));
}

static void test_pin_get(void)
{
    uint32_t bitmask = (1u << 3) | (1u << 17);

    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_get(bitmask, 3));
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_get(bitmask, 17));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_get(bitmask, 4));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_get(bitmask, 32)); /* invalid index -> false, not error */
}

/* ── evt[2:0] write semantics ──────────────────────────────────────────────── */

static void test_write_semantics_valid(void)
{
    uint8_t v;

    for (v = 0; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_gpio_write_semantics_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_gpio_write_semantics_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_gpio_write_semantics_valid(255));
}

static void test_apply_write_replace(void)
{
    TEST_ASSERT_EQUAL_UINT32(0xABCDu,
        rcp_ep_gpio_apply_write(0x1234u, 0xABCDu, RCP_EP_GPIO_WRITE_REPLACE));
}

static void test_apply_write_or(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x0Fu,
        rcp_ep_gpio_apply_write(0x0Au, 0x05u, RCP_EP_GPIO_WRITE_OR));
}

static void test_apply_write_and(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x0Au,
        rcp_ep_gpio_apply_write(0x0Fu, 0x0Au, RCP_EP_GPIO_WRITE_AND));
}

static void test_apply_write_xor(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x0Fu,
        rcp_ep_gpio_apply_write(0x0Au, 0x05u, RCP_EP_GPIO_WRITE_XOR));
}

static void test_apply_write_add_ordinary(void)
{
    TEST_ASSERT_EQUAL_UINT32(30u, rcp_ep_gpio_apply_write(10u, 20u, RCP_EP_GPIO_WRITE_ADD));
}

static void test_apply_write_add_saturates_at_upper_boundary(void)
{
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu,
        rcp_ep_gpio_apply_write(0xFFFFFFF0u, 0x100u, RCP_EP_GPIO_WRITE_ADD));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu,
        rcp_ep_gpio_apply_write(0xFFFFFFFFu, 0xFFFFFFFFu, RCP_EP_GPIO_WRITE_ADD));
}

static void test_apply_write_sub_ordinary(void)
{
    TEST_ASSERT_EQUAL_UINT32(10u, rcp_ep_gpio_apply_write(30u, 20u, RCP_EP_GPIO_WRITE_SUB));
}

static void test_apply_write_sub_saturates_at_lower_boundary(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_ep_gpio_apply_write(5u, 20u, RCP_EP_GPIO_WRITE_SUB));
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_ep_gpio_apply_write(0u, 1u, RCP_EP_GPIO_WRITE_SUB));
}

static void test_apply_write_reserved4_is_noop(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x1234u,
        rcp_ep_gpio_apply_write(0x1234u, 0xFFFFu, RCP_EP_GPIO_WRITE_RESERVED4));
}

/* Regression test for issue #104: raw wire evt[2:0] values 4/5/6 must map
 * to Reserved/ADD/SUB respectively (not the previous off-by-one ADD/SUB/
 * Reserved mapping). Exercises rcp_ep_gpio_apply_write() with the raw
 * wire-value enum casts a decoder would actually produce, not just the
 * named constants, so a future accidental re-shuffle of the enum values
 * themselves (not just their names) would still be caught. */
static void test_apply_write_wire_value_4_is_reserved_noop(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x1234u,
        rcp_ep_gpio_apply_write(0x1234u, 0xFFFFu, (rcp_ep_gpio_write_semantics_t)4u));
}

static void test_apply_write_wire_value_5_is_add(void)
{
    TEST_ASSERT_EQUAL_UINT32(30u,
        rcp_ep_gpio_apply_write(10u, 20u, (rcp_ep_gpio_write_semantics_t)5u));
}

static void test_apply_write_wire_value_6_is_sub(void)
{
    TEST_ASSERT_EQUAL_UINT32(10u,
        rcp_ep_gpio_apply_write(30u, 20u, (rcp_ep_gpio_write_semantics_t)6u));
}

static void test_apply_reconfig_toggles_only_flagged_pins(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];

    memset(pins, 0, sizeof(pins));
    pins[0] = RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP;
    pins[1] = RCP_REGMAP_PIN_PROP_INPUT;
    pins[2] = RCP_REGMAP_PIN_PROP_OUTPUT;

    /* Flag pins 0 and 1 only; pin 2 must be left untouched. */
    rcp_ep_gpio_apply_reconfig(pins, (1u << 0) | (1u << 1));

    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_INPUT | RCP_REGMAP_PIN_PROP_PULL_UP, pins[0]);
    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_OUTPUT, pins[1]);
    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_OUTPUT, pins[2]); /* untouched */
}

/* ── Per-pin trigger signals ────────────────────────────────────────────────── */

static void test_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, true, true));
}

static void test_trigger_any_change(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, false, true));
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, true, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, false, false));
}

static void test_trigger_rising(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, true, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, false, false));
}

static void test_trigger_falling(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, true, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, false, false));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    size_t i;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);

    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, cfg.pins[i].pin_property);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_NONE, cfg.pins[i].trigger);
    }
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t via_ep0 = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

static void test_set_pin_property_rejects_invalid_pin_or_unauthorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      authorized = {0};
    rcp_lifecycle_writer_ctx_t      none = {0};

    authorized.via_root_client_ep0 = true;
    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_property(
        &cfg, 32, RCP_REGMAP_PIN_PROP_OUTPUT, RCP_LIFECYCLE_HW_CONFIGURED, authorized));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.pins[0].pin_property);

    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_property(
        &cfg, 0, RCP_REGMAP_PIN_PROP_OUTPUT, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.pins[0].pin_property);
}

static void test_set_pin_property_applies_when_authorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};

    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_gpio_set_pin_property(
        &cfg, 5, RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP,
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP,
                             cfg.pins[5].pin_property);
}

static void test_set_pin_trigger_rejects_invalid_pin_or_unauthorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      none = {0};

    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_trigger(
        &cfg, 32, RCP_EP_GPIO_TRIGGER_RISING, RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_trigger(
        &cfg, 0, RCP_EP_GPIO_TRIGGER_RISING, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_NONE, cfg.pins[0].trigger);
}

static void test_set_pin_trigger_applies_when_authorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};

    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_gpio_set_pin_trigger(
        &cfg, 9, RCP_EP_GPIO_TRIGGER_FALLING, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_FALLING, cfg.pins[9].trigger);
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_gpio_errc_t codes[] = {
        RCP_EP_GPIO_OK, RCP_EP_GPIO_ERR_SHORT_FRAME, RCP_EP_GPIO_ERR_BAD_MSG_TYPE,
        RCP_EP_GPIO_ERR_WRONG_BUS, RCP_EP_GPIO_ERR_WRONG_OP, RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_gpio_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_gpio_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_gpio_strerror((rcp_ep_gpio_errc_t)999));
}

/* ── Read request round trip ───────────────────────────────────────────────── */

static void test_read_request_round_trip(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_read_request(3, 42);
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK, rcp_ep_gpio_decode_read_request(frame.data, frame.len, 3, &txn));
    TEST_ASSERT_EQUAL_UINT8(42, txn);

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_read_request(3, 42);
    uint8_t     txn = 0;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_BUS,
                       rcp_ep_gpio_decode_read_request(frame.data, frame.len, 4, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     txn = 0;

    hdr.byte_bus_id = 3;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_OP,
                       rcp_ep_gpio_decode_read_request(frame.data, frame.len, 3, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    uint8_t              txn = 0;

    gbb_hdr.info.byte_bus_id = 3;
    gbb_hdr.info.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_MSG_TYPE,
                       rcp_ep_gpio_decode_read_request(frame.data, frame.len, 3, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_short_frame(void)
{
    uint8_t too_short[3] = {0};
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_SHORT_FRAME,
                       rcp_ep_gpio_decode_read_request(too_short, sizeof(too_short), 3, &txn));
}

/* ── Write request round trip ──────────────────────────────────────────────── */

static void test_write_request_round_trip(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_write_request(5, 0xDEADBEEFu, RCP_EP_GPIO_WRITE_XOR, 7);
    uint32_t    bitmask = 0;
    uint8_t     evt = 0xFF;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, bitmask);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_WRITE_XOR, evt);
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

static void test_write_request_evt_masked_to_low_3_bits(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_write_request(5, 0, RCP_EP_GPIO_WRITE_RECONFIG, 0);
    uint32_t    bitmask;
    uint8_t     evt;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_WRITE_RECONFIG, evt);

    rcp_bytes_free(&frame);
}

static void test_write_request_rejects_wrong_bus_op_and_bad_payload_len(void)
{
    rcp_bytes_t                 frame;
    uint32_t                    bitmask;
    uint8_t                     evt;
    uint8_t                     txn;
    rcp_acf_byte_message_info_t hdr = {0};

    frame = rcp_ep_gpio_encode_write_request(5, 1, RCP_EP_GPIO_WRITE_REPLACE, 0);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_BUS,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 6, &bitmask, &evt, &txn));
    rcp_bytes_free(&frame);

    hdr.byte_bus_id = 5;
    hdr.op          = RCP_ACF_OP_READ; /* not a write */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_OP,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    rcp_bytes_free(&frame);

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id = 5;
    hdr.op          = RCP_ACF_OP_WRITE;
    {
        uint8_t short_payload[2] = {0, 0};
        frame = rcp_acf_encode_abb(&hdr, short_payload, sizeof(short_payload));
    }
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    rcp_bytes_free(&frame);
}

/* ── Response round trip ───────────────────────────────────────────────────── */

static void test_response_round_trip_untimed(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(2, 0x0000FF00u, 11, false, 0);
    uint32_t    bitmask = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 2, &bitmask, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(0x0000FF00u, bitmask);
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0, ts);
    TEST_ASSERT_EQUAL_UINT8(11, txn);

    rcp_bytes_free(&frame);
}

static void test_response_round_trip_timed(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(2, 0x11223344u, 200, true, 0x0102030405060708ull);
    uint32_t    bitmask = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 2, &bitmask, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, bitmask);
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);
    TEST_ASSERT_EQUAL_UINT8(200, txn);

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(2, 0, 0, false, 0);
    uint32_t    bitmask;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_BUS,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 3, &bitmask, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_bad_payload_len(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint32_t                    bitmask;
    bool                        timed;
    uint64_t                    ts;
    uint8_t                     txn;
    uint8_t                     short_payload[2] = {0, 0};

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_abb(&hdr, short_payload, sizeof(short_payload));

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 2, &bitmask, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_short_frame(void)
{
    uint8_t  too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    uint32_t bitmask;
    bool     timed;
    uint64_t ts;
    uint8_t  txn;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_SHORT_FRAME,
        rcp_ep_gpio_decode_response(too_short, sizeof(too_short), 2, &bitmask, &timed, &ts, &txn));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pin_index_valid_bounds);
    RUN_TEST(test_pin_mask);
    RUN_TEST(test_pin_get);

    RUN_TEST(test_write_semantics_valid);
    RUN_TEST(test_apply_write_replace);
    RUN_TEST(test_apply_write_or);
    RUN_TEST(test_apply_write_and);
    RUN_TEST(test_apply_write_xor);
    RUN_TEST(test_apply_write_add_ordinary);
    RUN_TEST(test_apply_write_add_saturates_at_upper_boundary);
    RUN_TEST(test_apply_write_sub_ordinary);
    RUN_TEST(test_apply_write_sub_saturates_at_lower_boundary);
    RUN_TEST(test_apply_write_reserved4_is_noop);
    RUN_TEST(test_apply_write_wire_value_4_is_reserved_noop);
    RUN_TEST(test_apply_write_wire_value_5_is_add);
    RUN_TEST(test_apply_write_wire_value_6_is_sub);
    RUN_TEST(test_apply_reconfig_toggles_only_flagged_pins);

    RUN_TEST(test_trigger_none_never_fires);
    RUN_TEST(test_trigger_any_change);
    RUN_TEST(test_trigger_rising);
    RUN_TEST(test_trigger_falling);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_true_hw_configured_any_writer);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_set_pin_property_rejects_invalid_pin_or_unauthorized);
    RUN_TEST(test_set_pin_property_applies_when_authorized);
    RUN_TEST(test_set_pin_trigger_rejects_invalid_pin_or_unauthorized);
    RUN_TEST(test_set_pin_trigger_applies_when_authorized);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_read_request_round_trip);
    RUN_TEST(test_read_request_rejects_wrong_bus);
    RUN_TEST(test_read_request_rejects_wrong_op);
    RUN_TEST(test_read_request_rejects_bad_msg_type);
    RUN_TEST(test_read_request_rejects_short_frame);

    RUN_TEST(test_write_request_round_trip);
    RUN_TEST(test_write_request_evt_masked_to_low_3_bits);
    RUN_TEST(test_write_request_rejects_wrong_bus_op_and_bad_payload_len);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_bad_payload_len);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
