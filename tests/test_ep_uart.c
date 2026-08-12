/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-UART-001
//cfusa:test REQ-UART-002
//cfusa:test REQ-UART-003
//cfusa:test REQ-UART-004
//cfusa:test REQ-UART-005
//cfusa:test REQ-UART-006
//cfusa:test REQ-UART-007
//cfusa:test REQ-UART-008
//cfusa:test REQ-UART-009
//cfusa:test REQ-UART-010
//cfusa:test REQ-UART-011
//cfusa:test REQ-UART-012
//cfusa:test REQ-UART-013
//cfusa:test REQ-UART-014
//cfusa:test REQ-UART-015
//cfusa:test REQ-UART-016
//cfusa:test REQ-UART-017
//cfusa:test REQ-UART-018
//cfusa:test REQ-UART-019
//cfusa:test REQ-UART-020
//cfusa:test REQ-UART-021
//cfusa:test REQ-UART-022
//cfusa:test REQ-UART-023
//cfusa:test REQ-UART-024
//cfusa:test REQ-UART-025
//cfusa:test REQ-UART-026
//cfusa:test REQ-UART-027
//cfusa:test REQ-UART-028
//cfusa:test REQ-UART-029
//cfusa:test REQ-UART-030
//cfusa:test REQ-UART-031
//cfusa:test REQ-UART-034
//cfusa:test REQ-UART-036
//cfusa:test REQ-UART-039
//cfusa:test REQ-UART-040
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_uart.h>
#include <rcp/fragment.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Word format / bit-padding ─────────────────────────────────────────────── */

static void test_nr_bits_valid_bounds(void)
{
    uint8_t v;

    TEST_ASSERT_FALSE(rcp_ep_uart_nr_bits_valid(0));
    for (v = 1; v <= 8; v++) {
        TEST_ASSERT_TRUE(rcp_ep_uart_nr_bits_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_uart_nr_bits_valid(9));
    TEST_ASSERT_FALSE(rcp_ep_uart_nr_bits_valid(255));
}

static void test_bit_pad_mask_values(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, rcp_ep_uart_bit_pad_mask(1));
    TEST_ASSERT_EQUAL_HEX8(0x1F, rcp_ep_uart_bit_pad_mask(5));
    TEST_ASSERT_EQUAL_HEX8(0x7F, rcp_ep_uart_bit_pad_mask(7));
    TEST_ASSERT_EQUAL_HEX8(0xFF, rcp_ep_uart_bit_pad_mask(8));
    TEST_ASSERT_EQUAL_HEX8(0x00, rcp_ep_uart_bit_pad_mask(0));
    TEST_ASSERT_EQUAL_HEX8(0x00, rcp_ep_uart_bit_pad_mask(9));
}

static void test_apply_bit_padding_masks_every_byte(void)
{
    uint8_t buf[3] = {0xFF, 0xFF, 0xFF};

    rcp_ep_uart_apply_bit_padding(buf, sizeof(buf), 7);
    TEST_ASSERT_EQUAL_HEX8(0x7F, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, buf[2]);
}

static void test_apply_bit_padding_no_op_for_8_bits(void)
{
    uint8_t buf[2] = {0xAB, 0xCD};

    rcp_ep_uart_apply_bit_padding(buf, sizeof(buf), 8);
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[1]);
}

static void test_apply_bit_padding_invalid_nr_bits_zeroes_buffer(void)
{
    uint8_t buf[2] = {0xAB, 0xCD};

    rcp_ep_uart_apply_bit_padding(buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes_except_nr_bits(void)
{
    rcp_ep_uart_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.baud_rate);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_PARITY_NONE, cfg.parity);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_ONE, cfg.stop_bits);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_rx_buffer_size);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.uart_timeout_ms);
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_UART_NR_BITS_MAX, cfg.uart_nr_bits);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.baud_rate_kbps);
    TEST_ASSERT_FALSE(cfg.rts_enable);
    TEST_ASSERT_FALSE(cfg.cts_enable);
    TEST_ASSERT_FALSE(cfg.half_duplex);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.wire_timeout_bit_times);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.trail);
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_uart_functional_cfg_writable(
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
    TEST_ASSERT_FALSE(rcp_ep_uart_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t via_ep0 = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_uart_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

static void test_set_baud_rate_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_baud_rate(
        &cfg, 115200, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.baud_rate);
}

static void test_set_baud_rate_applies_when_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_baud_rate(
        &cfg, 115200, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(115200, cfg.baud_rate);
}

static void test_set_frame_format_rejects_invalid_nr_bits(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_frame_format(
        &cfg, 0, RCP_EP_UART_PARITY_EVEN, RCP_EP_UART_STOP_BITS_TWO,
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_UART_NR_BITS_MAX, cfg.uart_nr_bits);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_PARITY_NONE, cfg.parity);
}

static void test_set_frame_format_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_frame_format(
        &cfg, 7, RCP_EP_UART_PARITY_ODD, RCP_EP_UART_STOP_BITS_ONE,
        RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_UART_NR_BITS_MAX, cfg.uart_nr_bits);
}

static void test_set_frame_format_applies_when_valid_and_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_frame_format(
        &cfg, 7, RCP_EP_UART_PARITY_EVEN, RCP_EP_UART_STOP_BITS_TWO,
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(7, cfg.uart_nr_bits);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_PARITY_EVEN, cfg.parity);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_TWO, cfg.stop_bits);
}

static void test_set_rx_buffer_size_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_rx_buffer_size(
        &cfg, 256, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_rx_buffer_size);
}

static void test_set_rx_buffer_size_applies_when_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_rx_buffer_size(
        &cfg, 256, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT16(256, cfg.ep_rx_buffer_size);
}

static void test_set_timeout_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_timeout(
        &cfg, 50, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.uart_timeout_ms);
}

static void test_set_timeout_applies_when_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_timeout(
        &cfg, 50, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(50, cfg.uart_timeout_ms);
}

/* ── The EP_func register block ────────────────────────────────────────────── */

static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    uint8_t                      out[RCP_EP_UART_EP_FUNC_LEN];

    rcp_ep_uart_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status         = 0x1234;
    cfg.baud_rate_kbps    = 0x5566;
    cfg.uart_nr_bits      = 7;
    cfg.parity            = (uint8_t)RCP_EP_UART_PARITY_EVEN;
    cfg.rts_enable        = true;
    cfg.cts_enable        = true;
    cfg.half_duplex       = true;
    cfg.stop_bits         = (uint8_t)RCP_EP_UART_STOP_BITS_TWO;
    cfg.wire_timeout_bit_times = 9;
    cfg.trail                  = 10;

    rcp_ep_uart_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_EP_FUNC_LEN, out[RCP_EP_UART_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_UART_REG_RESERVED_01]);
    TEST_ASSERT_TRUE((out[RCP_EP_UART_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_UART_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_UART_REG_EP_STATUS + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x55u, out[RCP_EP_UART_REG_BAUD_RATE]);
    TEST_ASSERT_EQUAL_UINT8(0x66u, out[RCP_EP_UART_REG_BAUD_RATE + 1]);
    TEST_ASSERT_EQUAL_UINT8(7, out[RCP_EP_UART_REG_NR_BITS]);
    TEST_ASSERT_EQUAL_UINT8(
        RCP_EP_UART_FLAG_PARITY_ENABLE | RCP_EP_UART_FLAG_PARITY_POL |
            RCP_EP_UART_FLAG_RTS_ENABLE | RCP_EP_UART_FLAG_CTS_ENABLE |
            RCP_EP_UART_FLAG_HALF_DUPLEX,
        out[RCP_EP_UART_REG_FLAGS]);
    TEST_ASSERT_EQUAL_UINT8(4, out[RCP_EP_UART_REG_STOP_BITS]); /* TWO -> half units 4 */
    TEST_ASSERT_EQUAL_UINT8(9, out[RCP_EP_UART_REG_TIMEOUT]);
    TEST_ASSERT_EQUAL_UINT8(10, out[RCP_EP_UART_REG_TRAIL]);

    TEST_ASSERT_EQUAL_UINT16(0x000Du, RCP_EP_UART_EP_FUNC_LEN);
}

static void test_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    uint8_t                      payload[2 + 7];

    rcp_ep_uart_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_UART_REG_BAUD_RATE;
    payload[2] = 0xAB; payload[3] = 0xCD; /* baud_rate */
    payload[4] = 6;                       /* nr_bits */
    payload[5] = RCP_EP_UART_FLAG_PARITY_ENABLE | RCP_EP_UART_FLAG_RTS_ENABLE; /* odd parity, RTS */
    payload[6] = 4;                       /* stop_bits half units -> TWO */
    payload[7] = 11;                      /* timeout */
    payload[8] = 12;                      /* trail */

    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_OK,
        rcp_ep_uart_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.baud_rate_kbps);
    TEST_ASSERT_EQUAL_UINT8(6, cfg.uart_nr_bits);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_PARITY_ODD, cfg.parity);
    TEST_ASSERT_TRUE(cfg.rts_enable);
    TEST_ASSERT_FALSE(cfg.cts_enable);
    TEST_ASSERT_FALSE(cfg.half_duplex);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_TWO, cfg.stop_bits);
    TEST_ASSERT_EQUAL_UINT8(11, cfg.wire_timeout_bit_times);
    TEST_ASSERT_EQUAL_UINT8(12, cfg.trail);
}

static void test_apply_reconfig_stop_bits_half_unit_rounding(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    uint8_t                      payload_low[3]  = {0x00, (uint8_t)RCP_EP_UART_REG_STOP_BITS, 2};
    uint8_t                      payload_mid[3]  = {0x00, (uint8_t)RCP_EP_UART_REG_STOP_BITS, 3};
    uint8_t                      payload_high[3] = {0x00, (uint8_t)RCP_EP_UART_REG_STOP_BITS, 4};

    rcp_ep_uart_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_OK,
        rcp_ep_uart_apply_reconfig(&cfg, payload_low, sizeof(payload_low)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_ONE, cfg.stop_bits);

    rcp_ep_uart_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_OK,
        rcp_ep_uart_apply_reconfig(&cfg, payload_mid, sizeof(payload_mid)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_TWO, cfg.stop_bits);

    rcp_ep_uart_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_OK,
        rcp_ep_uart_apply_reconfig(&cfg, payload_high, sizeof(payload_high)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_TWO, cfg.stop_bits);
}

static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    uint8_t                      payload[2 + 2];

    rcp_ep_uart_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_OK,
        rcp_ep_uart_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_UART_EP_FUNC_LEN];

        rcp_ep_uart_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_EP_FUNC_LEN, out[RCP_EP_UART_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_UART_REG_RESERVED_01]);
    }
}

static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    uint8_t                      payload[3];

    rcp_ep_uart_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x0D; /* == RCP_EP_UART_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_uart_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.trail);
}

static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    uint8_t                      addr_only[2] = {0x00, 0x06};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_ERR_SHORT,
        rcp_ep_uart_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_UART_RECONFIG_ERR_SHORT,
        rcp_ep_uart_apply_reconfig(&cfg, NULL, 0));
}

static void test_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_uart_encode_reconfig_request(0x03, 0x0006, data, sizeof(data), 7);
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
    rcp_bytes_t frame = rcp_ep_uart_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_reconfig_strerror_never_null(void)
{
    rcp_ep_uart_reconfig_errc_t codes[] = {
        RCP_EP_UART_RECONFIG_OK, RCP_EP_UART_RECONFIG_ERR_SHORT,
        RCP_EP_UART_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_uart_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_uart_reconfig_strerror((rcp_ep_uart_reconfig_errc_t)99));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_uart_errc_t codes[] = {
        RCP_EP_UART_OK, RCP_EP_UART_ERR_SHORT_FRAME, RCP_EP_UART_ERR_BAD_MSG_TYPE,
        RCP_EP_UART_ERR_WRONG_BUS, RCP_EP_UART_ERR_WRONG_OP, RCP_EP_UART_ERR_UNKNOWN_CMD,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_uart_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_uart_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_uart_strerror((rcp_ep_uart_errc_t)999));
}

/* ── TX: write request/response ────────────────────────────────────────────── */

static void test_write_request_round_trip(void)
{
    uint8_t     tx[3] = {0x01, 0x02, 0x03};
    rcp_bytes_t frame = rcp_ep_uart_encode_write_request(4, tx, sizeof(tx), 9);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_write_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8(9, txn);

    rcp_bytes_free(&frame);
}

static void test_write_request_rejects_wrong_bus_op_short_frame_bad_type(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t wrong_bus_frame = rcp_ep_uart_encode_write_request(4, tx, sizeof(tx), 0);
    rcp_acf_byte_message_info_t wrong_op_hdr = {0};
    rcp_acf_gbb_header_t        bad_type_hdr = {0};
    rcp_bytes_t                 wrong_op_frame;
    rcp_bytes_t                 bad_type_frame;
    uint8_t                     too_short[3] = {0};
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_WRONG_BUS,
        rcp_ep_uart_decode_write_request(wrong_bus_frame.data, wrong_bus_frame.len, 5, &out_tx,
                                          &out_tx_len, &txn));
    rcp_bytes_free(&wrong_bus_frame);

    wrong_op_hdr.byte_bus_id = 4;
    wrong_op_hdr.op          = RCP_ACF_OP_READ;
    wrong_op_frame = rcp_acf_encode_abb(&wrong_op_hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_WRONG_OP,
        rcp_ep_uart_decode_write_request(wrong_op_frame.data, wrong_op_frame.len, 4, &out_tx,
                                          &out_tx_len, &txn));
    rcp_bytes_free(&wrong_op_frame);

    bad_type_hdr.info.byte_bus_id = 4;
    bad_type_hdr.info.op          = RCP_ACF_OP_WRITE;
    bad_type_frame = rcp_acf_encode_gbb(&bad_type_hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_BAD_MSG_TYPE,
        rcp_ep_uart_decode_write_request(bad_type_frame.data, bad_type_frame.len, 4, &out_tx,
                                          &out_tx_len, &txn));
    rcp_bytes_free(&bad_type_frame);

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_SHORT_FRAME,
        rcp_ep_uart_decode_write_request(too_short, sizeof(too_short), 4, &out_tx, &out_tx_len,
                                          &txn));
}

/* TC18 §13.5 Table 30: evt[2:0] = 000b is the only legal value for a
 * plain UART write request; every other value (here, 0b110, a reserved
 * value in UART's endpoint-type row) shall be rejected. */
static void test_write_request_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = 0x6;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_BAD_EVT,
        rcp_ep_uart_decode_write_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_write_response_round_trip_untimed_and_timed(void)
{
    uint8_t     accepted[2] = {0x55, 0x66};
    rcp_bytes_t untimed = rcp_ep_uart_encode_write_response(3, accepted, sizeof(accepted), 4,
                                                             false, 0);
    rcp_bytes_t timed_frame = rcp_ep_uart_encode_write_response(3, accepted, sizeof(accepted), 4,
                                                                  true, 0xAABBCCDDull);
    const uint8_t *out_accepted = NULL;
    size_t      out_accepted_len = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_write_response(untimed.data, untimed.len, 3, &out_accepted,
                                           &out_accepted_len, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(accepted, out_accepted, sizeof(accepted));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0, ts);
    rcp_bytes_free(&untimed);

    timed  = false;
    ts     = 0;
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_write_response(timed_frame.data, timed_frame.len, 3, &out_accepted,
                                           &out_accepted_len, &timed, &ts, &txn));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0xAABBCCDDull, ts);
    TEST_ASSERT_EQUAL_UINT8(4, txn);
    rcp_bytes_free(&timed_frame);
}

static void test_write_response_decode_rejects_wrong_bus_and_short_frame(void)
{
    rcp_bytes_t frame = rcp_ep_uart_encode_write_response(3, NULL, 0, 0, false, 0);
    uint8_t     too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    const uint8_t *out_accepted;
    size_t      out_accepted_len;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_WRONG_BUS,
        rcp_ep_uart_decode_write_response(frame.data, frame.len, 9, &out_accepted,
                                           &out_accepted_len, &timed, &ts, &txn));
    rcp_bytes_free(&frame);

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_SHORT_FRAME,
        rcp_ep_uart_decode_write_response(too_short, sizeof(too_short), 3, &out_accepted,
                                           &out_accepted_len, &timed, &ts, &txn));
}

/* ── RX: read request/response ─────────────────────────────────────────────── */

static void test_read_request_round_trip(void)
{
    rcp_bytes_t frame = rcp_ep_uart_encode_read_request(6, 64, 3);
    uint16_t    read_size = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_request(frame.data, frame.len, 6, &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT16(64, read_size);
    TEST_ASSERT_EQUAL_UINT8(3, txn);

    rcp_bytes_free(&frame);
}

/* FIXED 2026-08-12 (issue #201, REQ-UART-034): a read_size above 255 --
 * previously inexpressible, since the parameter was narrowed to uint8_t
 * -- now round-trips through the full 12-bit ACF header field. */
static void test_read_request_round_trip_above_255(void)
{
    rcp_bytes_t frame = rcp_ep_uart_encode_read_request(6, 4000u, 3);
    uint16_t    read_size = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_request(frame.data, frame.len, 6, &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT16(4000u, read_size);
    TEST_ASSERT_EQUAL_UINT8(3, txn);

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_payload_with_unknown_cmd(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload[1] = {0x01};
    rcp_bytes_t                 frame;
    uint16_t                    read_size;
    uint8_t                     txn;

    hdr.byte_bus_id = 6;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.read_size_or_segment_num = 8;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_UNKNOWN_CMD,
        rcp_ep_uart_decode_read_request(frame.data, frame.len, 6, &read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_bus_op_short_frame(void)
{
    rcp_bytes_t                  wrong_bus = rcp_ep_uart_encode_read_request(6, 8, 0);
    rcp_acf_byte_message_info_t  wrong_op_hdr = {0};
    rcp_bytes_t                  wrong_op_frame;
    uint8_t                      too_short[3] = {0};
    uint16_t                     read_size;
    uint8_t                      txn;

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_WRONG_BUS,
        rcp_ep_uart_decode_read_request(wrong_bus.data, wrong_bus.len, 7, &read_size, &txn));
    rcp_bytes_free(&wrong_bus);

    wrong_op_hdr.byte_bus_id = 6;
    wrong_op_hdr.op          = RCP_ACF_OP_WRITE;
    wrong_op_frame = rcp_acf_encode_abb(&wrong_op_hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_WRONG_OP,
        rcp_ep_uart_decode_read_request(wrong_op_frame.data, wrong_op_frame.len, 6, &read_size,
                                         &txn));
    rcp_bytes_free(&wrong_op_frame);

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_SHORT_FRAME,
        rcp_ep_uart_decode_read_request(too_short, sizeof(too_short), 6, &read_size, &txn));
}

/* TC18 §13.5 Table 30: evt[2:0] = 000b is the only legal value for a
 * plain UART read request; every other value (here, 0b101, a reserved
 * value in UART's endpoint-type row) shall be rejected. */
static void test_read_request_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint16_t                    read_size;
    uint8_t                     txn;

    hdr.byte_bus_id = 6;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.evt         = 0x5;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_BAD_EVT,
        rcp_ep_uart_decode_read_request(frame.data, frame.len, 6, &read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_response_round_trip_full_length(void)
{
    uint8_t     rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t frame = rcp_ep_uart_encode_read_response(2, rx, sizeof(rx), 11, false, 0);
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed,
                                          &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT8(11, txn);

    rcp_bytes_free(&frame);
}

/* Worst-case single-AVTPDU short read: the read request asks for
 * read_size bytes but the uart_timeout_ms race completes first with
 * fewer bytes actually captured -- still just one ordinary ACF message,
 * no segment_num-based reassembly (deferred to Phase 20). See the file
 * header. */
static void test_read_response_round_trip_short_read_single_avtpdu(void)
{
    rcp_bytes_t read_req = rcp_ep_uart_encode_read_request(2, 32, 5);
    uint8_t     rx[3] = {0x01, 0x02, 0x03}; /* far fewer than the requested 32 */
    rcp_bytes_t frame = rcp_ep_uart_encode_read_response(2, rx, sizeof(rx), 5, true,
                                                           0x1122334455667788ull);
    uint16_t    requested_read_size = 0;
    uint8_t     req_txn = 0;
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_request(read_req.data, read_req.len, 2, &requested_read_size,
                                         &req_txn));
    TEST_ASSERT_EQUAL_UINT16(32, requested_read_size);

    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed,
                                          &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_TRUE(out_rx_len < requested_read_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x1122334455667788ull, ts);
    TEST_ASSERT_EQUAL_UINT8(5, txn);

    rcp_bytes_free(&read_req);
    rcp_bytes_free(&frame);
}

static void test_read_response_decode_rejects_wrong_bus_and_short_frame(void)
{
    rcp_bytes_t frame = rcp_ep_uart_encode_read_response(2, NULL, 0, 0, false, 0);
    uint8_t     too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    const uint8_t *out_rx;
    size_t      out_rx_len;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_WRONG_BUS,
        rcp_ep_uart_decode_read_response(frame.data, frame.len, 3, &out_rx, &out_rx_len, &timed,
                                          &ts, &txn));
    rcp_bytes_free(&frame);

    TEST_ASSERT_EQUAL(RCP_EP_UART_ERR_SHORT_FRAME,
        rcp_ep_uart_decode_read_response(too_short, sizeof(too_short), 2, &out_rx, &out_rx_len,
                                          &timed, &ts, &txn));
}

/* ── Fragmented read response (Phase 20, fragment.h) ───────────────────────── */

static void test_fragment_count_one_when_unfragmented(void)
{
    TEST_ASSERT_EQUAL_UINT(1, rcp_ep_uart_read_response_fragment_count(10, 100));
    TEST_ASSERT_EQUAL_UINT(1, rcp_ep_uart_read_response_fragment_count(0, 0));
}

static void test_fragment_unfragmented_matches_single_frame_path(void)
{
    uint8_t     rx[3] = {0x11, 0x22, 0x33};
    rcp_bytes_t plain;
    rcp_bytes_t fragmented[1];
    size_t      count;

    plain = rcp_ep_uart_encode_read_response(6, rx, sizeof(rx), 12, false, 0);
    TEST_ASSERT_NOT_NULL(plain.data);

    count = rcp_ep_uart_encode_read_response_fragmented(6, rx, sizeof(rx), 12, false, 0, 255,
                                                          fragmented);
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_UINT(plain.len, fragmented[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plain.data, fragmented[0].data, plain.len);

    rcp_bytes_free(&plain);
    rcp_bytes_free(&fragmented[0]);
}

/* Closes the deferred single-AVTPDU-worst-case test noted at milestone 66
 * (v0.66.0): exercises fragment.h's ms/segment_num mechanism against this
 * endpoint's own wire codec end-to-end, using a deliberately small
 * max_fragment_payload -- see the file header for why this endpoint's own
 * one-octet read_size means genuine UART traffic never actually needs
 * more than one fragment in practice; this test proves the mechanism
 * composes correctly regardless. */
static void test_fragment_deliberately_small_cap_round_trip(void)
{
    uint8_t                     rx[20];
    size_t                      i;
    size_t                      max_fragment_payload = 6;
    size_t                      count;
    rcp_bytes_t                 frames[4];
    rcp_fragment_reassembler_t  reasm;

    for (i = 0; i < sizeof(rx); i++) rx[i] = (uint8_t)(100 + i);

    count = rcp_ep_uart_read_response_fragment_count(sizeof(rx), max_fragment_payload);
    TEST_ASSERT_EQUAL_UINT(4, count); /* ceil(20/6) */
    TEST_ASSERT_TRUE(count <= (sizeof(frames) / sizeof(frames[0])));

    count = rcp_ep_uart_encode_read_response_fragmented(3, rx, sizeof(rx), 66, true,
                                                          0x0102030405060708ull,
                                                          max_fragment_payload, frames);
    TEST_ASSERT_EQUAL_UINT(4, count);

    rcp_fragment_reassembler_init(&reasm, sizeof(rx));
    for (i = 0; i < count; i++) {
        bool                         ms;
        uint8_t                      segnum;
        const uint8_t                *payload;
        size_t                        payload_len;
        bool                          timed;
        uint64_t                      ts;
        uint8_t                       txn;
        rcp_fragment_reasm_result_t   rc;

        TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
            rcp_ep_uart_decode_read_response_fragment(frames[i].data, frames[i].len, 3, &ms,
                                                        &segnum, &payload, &payload_len,
                                                        &timed, &ts, &txn));
        TEST_ASSERT_EQUAL_UINT8(66, txn);
        TEST_ASSERT_TRUE(timed);
        TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);

        rc = rcp_fragment_reassembler_feed(&reasm, ms, segnum, payload, payload_len);
        if (i + 1 < count) {
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);
        } else {
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);
        }
    }

    {
        const uint8_t *reassembled;
        size_t         reassembled_len;

        rcp_fragment_reassembler_get(&reasm, &reassembled, &reassembled_len);
        TEST_ASSERT_EQUAL_UINT(sizeof(rx), reassembled_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, reassembled, sizeof(rx));
    }

    rcp_fragment_reassembler_destroy(&reasm);
    for (i = 0; i < count; i++) rcp_bytes_free(&frames[i]);
}

static void test_fragment_encode_disabled_when_zero_cap_and_oversized(void)
{
    uint8_t     rx[4] = {1, 2, 3, 4};
    rcp_bytes_t frames[4];
    size_t      count = rcp_ep_uart_encode_read_response_fragmented(3, rx, sizeof(rx), 1, false,
                                                                       0, 0, frames);
    TEST_ASSERT_EQUAL_UINT(0, count);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_nr_bits_valid_bounds);
    RUN_TEST(test_bit_pad_mask_values);
    RUN_TEST(test_apply_bit_padding_masks_every_byte);
    RUN_TEST(test_apply_bit_padding_no_op_for_8_bits);
    RUN_TEST(test_apply_bit_padding_invalid_nr_bits_zeroes_buffer);

    RUN_TEST(test_functional_cfg_init_zeroes_except_nr_bits);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_set_baud_rate_rejects_unauthorized);
    RUN_TEST(test_set_baud_rate_applies_when_authorized);
    RUN_TEST(test_set_frame_format_rejects_invalid_nr_bits);
    RUN_TEST(test_set_frame_format_rejects_unauthorized);
    RUN_TEST(test_set_frame_format_applies_when_valid_and_authorized);
    RUN_TEST(test_set_rx_buffer_size_rejects_unauthorized);
    RUN_TEST(test_set_rx_buffer_size_applies_when_authorized);
    RUN_TEST(test_set_timeout_rejects_unauthorized);
    RUN_TEST(test_set_timeout_applies_when_authorized);

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_apply_reconfig_stop_bits_half_unit_rounding);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_reconfig_strerror_never_null);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_write_request_round_trip);
    RUN_TEST(test_write_request_rejects_wrong_bus_op_short_frame_bad_type);
    RUN_TEST(test_write_request_rejects_nonzero_evt);
    RUN_TEST(test_write_response_round_trip_untimed_and_timed);
    RUN_TEST(test_write_response_decode_rejects_wrong_bus_and_short_frame);

    RUN_TEST(test_read_request_round_trip);
    RUN_TEST(test_read_request_round_trip_above_255);
    RUN_TEST(test_read_request_rejects_payload_with_unknown_cmd);
    RUN_TEST(test_read_request_rejects_wrong_bus_op_short_frame);
    RUN_TEST(test_read_request_rejects_nonzero_evt);
    RUN_TEST(test_read_response_round_trip_full_length);
    RUN_TEST(test_read_response_round_trip_short_read_single_avtpdu);
    RUN_TEST(test_read_response_decode_rejects_wrong_bus_and_short_frame);

    RUN_TEST(test_fragment_count_one_when_unfragmented);
    RUN_TEST(test_fragment_unfragmented_matches_single_frame_path);
    RUN_TEST(test_fragment_deliberately_small_cap_round_trip);
    RUN_TEST(test_fragment_encode_disabled_when_zero_cap_and_oversized);

    return UNITY_END();
}
