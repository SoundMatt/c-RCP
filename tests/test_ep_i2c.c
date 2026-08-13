/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-I2C-001
//cfusa:test REQ-I2C-002
//cfusa:test REQ-I2C-003
//cfusa:test REQ-I2C-004
//cfusa:test REQ-I2C-005
//cfusa:test REQ-I2C-006
//cfusa:test REQ-I2C-007
//cfusa:test REQ-I2C-008
//cfusa:test REQ-I2C-009
//cfusa:test REQ-I2C-010
//cfusa:test REQ-I2C-011
//cfusa:test REQ-I2C-012
//cfusa:test REQ-I2C-013
//cfusa:test REQ-I2C-014
//cfusa:test REQ-I2C-015
//cfusa:test REQ-I2C-016
//cfusa:test REQ-I2C-017
//cfusa:test REQ-I2C-018
//cfusa:test REQ-I2C-019
//cfusa:test REQ-I2C-021
//cfusa:test REQ-I2C-022
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_i2c.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── i2c_mode ───────────────────────────────────────────────────────────────── */

static void test_mode_valid_bounds(void)
{
    uint8_t v;

    for (v = 0; v <= 4; v++) {
        TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_i2c_mode_valid(5));
    TEST_ASSERT_FALSE(rcp_ep_i2c_mode_valid(255));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_STANDARD, cfg.i2c_mode);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.clock_divider);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.trail);
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_i2c_functional_cfg_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream(void)
{
    rcp_lifecycle_writer_ctx_t none          = {0};
    rcp_lifecycle_writer_ctx_t via_ep0       = {0};
    rcp_lifecycle_writer_ctx_t via_stream    = {0};
    rcp_lifecycle_writer_ctx_t via_discovery = {0};

    via_ep0.via_root_client_ep0        = true;
    via_stream.via_owning_stream       = true;
    via_discovery.via_discovery_stream = true;

    /* REQ-LIFECYCLE-030/036: HW_CONFIGURED functional-config write access
     * now requires the root client via EP0, the endpoint's own owning
     * stream, or the discovery stream -- no longer any writer
     * unconditionally. */
    TEST_ASSERT_FALSE(rcp_ep_i2c_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t via_ep0 = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_i2c_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

static void test_set_mode_rejects_invalid_mode(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     authorized = {0};

    authorized.via_root_client_ep0 = true;
    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_i2c_set_mode(
        &cfg, (rcp_ep_i2c_mode_t)99, RCP_LIFECYCLE_HW_CONFIGURED, authorized));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_STANDARD, cfg.i2c_mode);
}

static void test_set_mode_rejects_unauthorized(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_i2c_set_mode(
        &cfg, RCP_EP_I2C_MODE_FAST, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_STANDARD, cfg.i2c_mode);
}

static void test_set_mode_applies_when_valid_and_authorized(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_i2c_set_mode(
        &cfg, RCP_EP_I2C_MODE_HIGH_SPEED, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_HIGH_SPEED, cfg.i2c_mode);
}

/* ── The EP_func register block ────────────────────────────────────────────── */

static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    uint8_t                     out[RCP_EP_I2C_EP_FUNC_LEN];

    rcp_ep_i2c_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status         = 0x1234;
    cfg.clock_divider     = 0x55;
    cfg.i2c_mode           = (uint8_t)RCP_EP_I2C_MODE_ULTRA_FAST;
    cfg.trail              = 0x77;

    rcp_ep_i2c_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_EP_FUNC_LEN, out[RCP_EP_I2C_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_I2C_REG_RESERVED_01]);
    TEST_ASSERT_TRUE((out[RCP_EP_I2C_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    /* base_clk always renders 0 -- no real clock source modelled. */
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_I2C_REG_BASE_CLK]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_I2C_REG_BASE_CLK + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_I2C_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_I2C_REG_EP_STATUS + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x55, out[RCP_EP_I2C_REG_CLOCK_DIVIDER]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_ULTRA_FAST, out[RCP_EP_I2C_REG_MODE]);
    TEST_ASSERT_EQUAL_UINT8(0x77, out[RCP_EP_I2C_REG_TRAIL]);

    TEST_ASSERT_EQUAL_UINT16(0x000Bu, RCP_EP_I2C_EP_FUNC_LEN);
}

static void test_apply_reconfig_writes_clock_divider(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_i2c_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_I2C_REG_CLOCK_DIVIDER;
    payload[2] = 0x42;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_OK,
        rcp_ep_i2c_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0x42, cfg.clock_divider);
}

static void test_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    uint8_t                     payload[2 + 4];

    rcp_ep_i2c_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_I2C_REG_EP_STATUS;
    payload[2] = 0xAB; payload[3] = 0xCD; /* ep_status */
    payload[4] = 0x03;                    /* clock_divider */
    payload[5] = (uint8_t)RCP_EP_I2C_MODE_FAST; /* i2c_mode */

    TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_OK,
        rcp_ep_i2c_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0x03, cfg.clock_divider);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_FAST, cfg.i2c_mode);
}

static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    uint8_t                     payload[2 + 4];

    rcp_ep_i2c_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00), the reserved octet (0x01), and both octets of
     * base_clk (0x04-0x05) -- all read-only. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;
    payload[4] = 0xFF;
    payload[5] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_OK,
        rcp_ep_i2c_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_I2C_EP_FUNC_LEN];

        rcp_ep_i2c_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_EP_FUNC_LEN, out[RCP_EP_I2C_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_I2C_REG_RESERVED_01]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_I2C_REG_BASE_CLK]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_I2C_REG_BASE_CLK + 1]);
    }
}

static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_i2c_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x0B; /* == RCP_EP_I2C_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_i2c_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.trail);
}

static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    uint8_t                     addr_only[2] = {0x00, 0x08};

    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_ERR_SHORT,
        rcp_ep_i2c_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_ERR_SHORT,
        rcp_ep_i2c_apply_reconfig(&cfg, NULL, 0));
}

static void test_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_i2c_encode_reconfig_request(0x03, 0x0006, data, sizeof(data), 7);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(0x03, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL(RCP_ACF_OP_WRITE, hdr.op);
    TEST_ASSERT_EQUAL_UINT8(0x7u, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(7, hdr.transaction_num);
    TEST_ASSERT_EQUAL_UINT32(4, payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x06, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, payload[3]);

    rcp_bytes_free(&frame);
}

static void test_encode_reconfig_request_rejects_empty_data(void)
{
    rcp_bytes_t frame = rcp_ep_i2c_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_reconfig_strerror_never_null(void)
{
    rcp_ep_i2c_reconfig_errc_t codes[] = {
        RCP_EP_I2C_RECONFIG_OK, RCP_EP_I2C_RECONFIG_ERR_SHORT,
        RCP_EP_I2C_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_i2c_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_i2c_reconfig_strerror((rcp_ep_i2c_reconfig_errc_t)99));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_i2c_errc_t codes[] = {
        RCP_EP_I2C_OK,         RCP_EP_I2C_ERR_SHORT_FRAME, RCP_EP_I2C_ERR_BAD_MSG_TYPE,
        RCP_EP_I2C_ERR_WRONG_BUS, RCP_EP_I2C_ERR_WRONG_OP,   RCP_EP_I2C_ERR_BAD_EVT,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_i2c_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_i2c_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_i2c_strerror((rcp_ep_i2c_errc_t)999));
}

/* ── Transfer direction ────────────────────────────────────────────────────── */

/* TC18 v0.5.1_RC §12.9.1 "Handling of requests" states the op field's two
 * senses:
 *
 *   A response with pay load data read from the EP is given, if requested
 *   by op=0 (read request) after the request has been executed.
 *   A response with err=0 and no payload is given after successful
 *   execution of a request with op=1 (write request) ...
 *
 * and §11.3.2 "Write Response" / §11.3.3 "Read Response" restate the same
 * split on the response side:
 *
 *   With evt[3:0] < 0x9 and op = 1 the response is a write response and
 *   confirms successful execution of a write request. It does not have a
 *   byte_msg_payload.
 *   With evt[3:0] < 0x9 and op = 0 the response is a read response and
 *   confirms successful execution of a read request. It has a
 *   byte_msg_payload ...
 *
 * The I2C endpoint is *not* pinned to either sense. §13.7.7.3 "I²C EP
 * request handling" says only:
 *
 *   The byte msg payload is the I2C payload including the address. The
 *   I2C endpoint does not know whether there is a 7- or 10-bit address,
 *   since the endpoint is just transparent.
 *
 * and its Figure 29, "i2c request format", leaves the byte message info's
 * op cell BLANK -- the only header values that figure fills in are pad --
 * while spelling the I2C-bus-level direction bit out inside the payload
 * instead: its first payload octet reads "1 1 1 1 0 A10 A9 RW", the
 * 10-bit-address prefix with the R/W bit, followed by "A8..A1" and "I2C
 * data". So the R/W bit is a payload bit that the (transparent) endpoint
 * clocks onto the bus, and op is the separate RCP-level question of what
 * response comes back -- which for a half-duplex bus is genuinely either.
 *
 * This is deliberately NOT the same defect ep_lin.c/ep_spi.c carried
 * (fixed in v0.103.0): those two are unconditionally response-bearing and
 * their sections pin op=0 for them explicitly (§13.7.10.1's "a reply is
 * sent if op = 0" for LIN, Figure 23's literal "op=0 ... read_size =
 * 0x0A" for SPI), so a constant op is right for them and was simply the
 * wrong constant. Nothing pins a constant for I2C, and §13.7.4's GPIO
 * wording confirms the general model has all three shapes -- "A read
 * request without a byte_msg_payload (pure read)", "A read request with a
 * byte_msg_payload as well as a write request ..." -- so a
 * payload-bearing read request is an ordinary thing, which is exactly
 * what an I2C read (address out, data back) is.
 *
 * These two tests verify the literal wire bit rather than re-encoded
 * output: acf.h maps RCP_ACF_OP_READ onto wire op=0 and RCP_ACF_OP_WRITE
 * onto wire op=1. Before v0.104.0 this module hard-coded the write sense
 * and rejected the read sense as malformed, so an I2C read could be
 * neither sent nor received. */
static void test_transfer_request_read_direction_carries_op_read_and_read_size(void)
{
    /* 0xA3: a 7-bit address with the payload's own R/W bit set, i.e. an
     * I2C-bus-level read. This module never parses it (see the file
     * header) -- it is here to show the two direction bits are set
     * independently and both land. */
    uint8_t                     tx[1] = {0xA3};
    rcp_bytes_t                 frame = rcp_ep_i2c_encode_transfer_request(
        6, RCP_EP_I2C_DIR_READ, tx, sizeof(tx), 10, 7);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ACF_OP_READ, hdr.op);
    /* "if op = 0 this is read_size, else segment_num" -- so the count of
     * octets to clock back is only expressible in this direction. */
    TEST_ASSERT_EQUAL_UINT16(10u, hdr.read_size_or_segment_num);
    /* The payload's own R/W bit is untouched by this module. */
    TEST_ASSERT_EQUAL_UINT8(0xA3, payload[0]);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_write_direction_carries_op_write_and_no_read_size(void)
{
    /* 0xA2: the same address with the payload's R/W bit clear. */
    uint8_t                     tx[3] = {0xA2, 0x10, 0x20};
    rcp_bytes_t                 frame = rcp_ep_i2c_encode_transfer_request(
        6, RCP_EP_I2C_DIR_WRITE, tx, sizeof(tx), 0, 7);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ACF_OP_WRITE, hdr.op);
    /* In this sense the slot is a segment_num, not a read_size; this
     * module does not fragment, so it stays 0. */
    TEST_ASSERT_EQUAL_UINT16(0u, hdr.read_size_or_segment_num);
    TEST_ASSERT_EQUAL_UINT8(0xA2, payload[0]);

    rcp_bytes_free(&frame);
}

static void test_dir_valid_bounds(void)
{
    TEST_ASSERT_TRUE(rcp_ep_i2c_dir_valid(RCP_EP_I2C_DIR_WRITE));
    TEST_ASSERT_TRUE(rcp_ep_i2c_dir_valid(RCP_EP_I2C_DIR_READ));
    TEST_ASSERT_FALSE(rcp_ep_i2c_dir_valid((rcp_ep_i2c_dir_t)2));
    TEST_ASSERT_FALSE(rcp_ep_i2c_dir_valid((rcp_ep_i2c_dir_t)255));
}

static void test_transfer_request_rejects_invalid_encode_inputs(void)
{
    uint8_t     tx[1] = {0xA2};
    rcp_bytes_t frame;

    /* Not a defined direction. */
    frame = rcp_ep_i2c_encode_transfer_request(6, (rcp_ep_i2c_dir_t)2, tx, sizeof(tx), 0, 0);
    TEST_ASSERT_NULL(frame.data);

    /* read_size is a 12-bit wire field; 0x1000 does not fit. */
    frame = rcp_ep_i2c_encode_transfer_request(6, RCP_EP_I2C_DIR_READ, tx, sizeof(tx), 0x1000, 0);
    TEST_ASSERT_NULL(frame.data);
    frame = rcp_ep_i2c_encode_transfer_request(6, RCP_EP_I2C_DIR_READ, tx, sizeof(tx),
                                                RCP_EP_I2C_MAX_READ_SIZE, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_bytes_free(&frame);

    /* A write request's header slot is a segment_num, so it cannot carry a
     * request for octets back. */
    frame = rcp_ep_i2c_encode_transfer_request(6, RCP_EP_I2C_DIR_WRITE, tx, sizeof(tx), 4, 0);
    TEST_ASSERT_NULL(frame.data);
}

/* ── Transfer request round trip ───────────────────────────────────────────── */

static void test_transfer_request_round_trip_carries_address_bytes(void)
{
    /* First byte models a raw target-device address byte; this module
     * never parses or strips it -- see the file header. */
    uint8_t     tx[4] = {0xA2, 0x10, 0x20, 0x30};
    rcp_bytes_t frame = rcp_ep_i2c_encode_transfer_request(6, RCP_EP_I2C_DIR_WRITE, tx,
                                                            sizeof(tx), 0, 7);
    rcp_ep_i2c_dir_t dir = RCP_EP_I2C_DIR_READ;
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint16_t    read_size = 99;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 6, &dir, &out_tx, &out_tx_len,
                                            &read_size, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_WRITE, dir);
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT16(0u, read_size);
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_round_trip_read_direction(void)
{
    uint8_t     tx[2] = {0xF2, 0xA3}; /* 10-bit address prefix + low octet */
    rcp_bytes_t frame = rcp_ep_i2c_encode_transfer_request(6, RCP_EP_I2C_DIR_READ, tx,
                                                            sizeof(tx), 5, 8);
    rcp_ep_i2c_dir_t dir = RCP_EP_I2C_DIR_WRITE;
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint16_t    read_size = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 6, &dir, &out_tx, &out_tx_len,
                                            &read_size, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_READ, dir);
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT16(5u, read_size);
    TEST_ASSERT_EQUAL_UINT8(8, txn);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_round_trip_empty_payload(void)
{
    rcp_bytes_t frame = rcp_ep_i2c_encode_transfer_request(1, RCP_EP_I2C_DIR_WRITE, NULL, 0, 0, 1);
    rcp_ep_i2c_dir_t dir;
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 1;
    uint16_t    read_size;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 1, &dir, &out_tx, &out_tx_len,
                                            &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame = rcp_ep_i2c_encode_transfer_request(4, RCP_EP_I2C_DIR_WRITE, tx,
                                                            sizeof(tx), 0, 0);
    rcp_ep_i2c_dir_t dir;
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint16_t    read_size;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_WRONG_BUS,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 5, &dir, &out_tx, &out_tx_len,
                                            &read_size, &txn));

    rcp_bytes_free(&frame);
}

/* TC18 §13.5 Table 30: evt[2:0] = 000b is the only legal value for a
 * plain I2C transfer request; every other value (here, 0b011, a reserved
 * value in I2C's endpoint-type row) shall be rejected. */
static void test_transfer_request_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_i2c_dir_t             dir;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint16_t                     read_size;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = 0x3;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_BAD_EVT,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4, &dir, &out_tx, &out_tx_len,
                                            &read_size, &txn));

    rcp_bytes_free(&frame);
}

/* The former test_transfer_request_rejects_wrong_op() asserted that a
 * read-direction (op=0) frame is not an I2C transfer request. It is one --
 * see the direction tests above -- so its replacement asserts the
 * opposite: a hand-built op=0 frame decodes, and reports the read
 * direction along with the read_size that only that direction can carry. */
static void test_transfer_request_accepts_hand_built_read_direction_frame(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_i2c_dir_t            dir = RCP_EP_I2C_DIR_WRITE;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint16_t                     read_size = 0;
    uint8_t                      txn;

    hdr.byte_bus_id              = 4;
    hdr.op                       = RCP_ACF_OP_READ;
    hdr.read_size_or_segment_num = 12;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4, &dir, &out_tx, &out_tx_len,
                                            &read_size, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_READ, dir);
    TEST_ASSERT_EQUAL_UINT16(12u, read_size);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    rcp_ep_i2c_dir_t     dir;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint16_t              read_size;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_BAD_MSG_TYPE,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4, &dir, &out_tx, &out_tx_len,
                                            &read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_short_frame(void)
{
    uint8_t          too_short[3] = {0};
    rcp_ep_i2c_dir_t dir;
    const uint8_t   *out_tx;
    size_t           out_tx_len;
    uint16_t         read_size;
    uint8_t          txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_SHORT_FRAME,
        rcp_ep_i2c_decode_transfer_request(too_short, sizeof(too_short), 4, &dir, &out_tx,
                                            &out_tx_len, &read_size, &txn));
}

/* ── Response round trip ───────────────────────────────────────────────────── */

static void test_response_round_trip_untimed(void)
{
    uint8_t     rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_READ, rx, sizeof(rx), 11,
                                                    false, 0);
    rcp_ep_i2c_dir_t dir = RCP_EP_I2C_DIR_WRITE;
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_response(frame.data, frame.len, 2, &dir, &out_rx, &out_rx_len, &timed,
                                    &ts, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_READ, dir);
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0, ts);
    TEST_ASSERT_EQUAL_UINT8(11, txn);

    rcp_bytes_free(&frame);
}

static void test_response_round_trip_timed(void)
{
    uint8_t     rx[2] = {0x11, 0x22};
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_READ, rx, sizeof(rx), 200,
                                                    true, 0x0102030405060708ull);
    rcp_ep_i2c_dir_t dir = RCP_EP_I2C_DIR_WRITE;
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_response(frame.data, frame.len, 2, &dir, &out_rx, &out_rx_len, &timed,
                                    &ts, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_READ, dir);
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);
    TEST_ASSERT_EQUAL_UINT8(200, txn);

    rcp_bytes_free(&frame);
}

/* TC18 v0.5.1_RC §11.3.2 "Write Response": "With evt[3:0] < 0x9 and op = 1
 * the response is a write response and confirms successful execution of a
 * write request. It does not have a byte_msg_payload." Before v0.104.0
 * this module encoded every response with op=0, so the response to an I2C
 * write transaction classified as a read response on the wire. */
static void test_write_response_carries_op_write_and_no_payload(void)
{
    uint8_t                     rx[1] = {0xFF};
    rcp_bytes_t                 frame = rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_WRITE, NULL,
                                                                    0, 11, false, 0);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;
    rcp_ep_i2c_dir_t            dir = RCP_EP_I2C_DIR_READ;
    const uint8_t              *out_rx;
    size_t                      out_rx_len;
    bool                        timed;
    uint64_t                    ts;
    uint8_t                     txn;
    rcp_bytes_t                 bad;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ACF_OP_WRITE, hdr.op);
    TEST_ASSERT_EQUAL_UINT32(0u, payload_len);

    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_response(frame.data, frame.len, 2, &dir, &out_rx, &out_rx_len, &timed,
                                    &ts, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_WRITE, dir);
    TEST_ASSERT_EQUAL_UINT32(0u, out_rx_len);

    rcp_bytes_free(&frame);

    /* "It does not have a byte_msg_payload" -- so one cannot be attached. */
    bad = rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_WRITE, rx, sizeof(rx), 11, false, 0);
    TEST_ASSERT_NULL(bad.data);

    bad = rcp_ep_i2c_encode_response(2, (rcp_ep_i2c_dir_t)2, NULL, 0, 11, false, 0);
    TEST_ASSERT_NULL(bad.data);
}

static void test_response_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_READ, NULL, 0, 0, false, 0);
    rcp_ep_i2c_dir_t dir;
    const uint8_t *out_rx;
    size_t      out_rx_len;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_WRONG_BUS,
        rcp_ep_i2c_decode_response(frame.data, frame.len, 3, &dir, &out_rx, &out_rx_len, &timed,
                                    &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_short_frame(void)
{
    uint8_t  too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    rcp_ep_i2c_dir_t dir;
    const uint8_t *out_rx;
    size_t   out_rx_len;
    bool     timed;
    uint64_t ts;
    uint8_t  txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_SHORT_FRAME,
        rcp_ep_i2c_decode_response(too_short, sizeof(too_short), 2, &dir, &out_rx, &out_rx_len,
                                    &timed, &ts, &txn));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_mode_valid_bounds);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_set_mode_rejects_invalid_mode);
    RUN_TEST(test_set_mode_rejects_unauthorized);
    RUN_TEST(test_set_mode_applies_when_valid_and_authorized);

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_apply_reconfig_writes_clock_divider);
    RUN_TEST(test_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_reconfig_strerror_never_null);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_dir_valid_bounds);
    RUN_TEST(test_transfer_request_read_direction_carries_op_read_and_read_size);
    RUN_TEST(test_transfer_request_write_direction_carries_op_write_and_no_read_size);
    RUN_TEST(test_transfer_request_rejects_invalid_encode_inputs);

    RUN_TEST(test_transfer_request_round_trip_carries_address_bytes);
    RUN_TEST(test_transfer_request_round_trip_read_direction);
    RUN_TEST(test_transfer_request_round_trip_empty_payload);
    RUN_TEST(test_transfer_request_rejects_wrong_bus);
    RUN_TEST(test_transfer_request_rejects_nonzero_evt);
    RUN_TEST(test_transfer_request_accepts_hand_built_read_direction_frame);
    RUN_TEST(test_transfer_request_rejects_bad_msg_type);
    RUN_TEST(test_transfer_request_rejects_short_frame);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_write_response_carries_op_write_and_no_payload);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
