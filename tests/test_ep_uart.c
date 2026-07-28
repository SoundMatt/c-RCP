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
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_uart.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/server.h>

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
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_server_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_uart_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_server_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_server_writer_ctx_t none = {0};
    rcp_server_writer_ctx_t via_ep0 = {0};
    rcp_server_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_uart_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_uart_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

static void test_set_baud_rate_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_baud_rate(
        &cfg, 115200, RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.baud_rate);
}

static void test_set_baud_rate_applies_when_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      writer = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_baud_rate(
        &cfg, 115200, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(115200, cfg.baud_rate);
}

static void test_set_frame_format_rejects_invalid_nr_bits(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      writer = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_frame_format(
        &cfg, 0, RCP_EP_UART_PARITY_EVEN, RCP_EP_UART_STOP_BITS_TWO,
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_UART_NR_BITS_MAX, cfg.uart_nr_bits);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_PARITY_NONE, cfg.parity);
}

static void test_set_frame_format_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_frame_format(
        &cfg, 7, RCP_EP_UART_PARITY_ODD, RCP_EP_UART_STOP_BITS_ONE,
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_UART_NR_BITS_MAX, cfg.uart_nr_bits);
}

static void test_set_frame_format_applies_when_valid_and_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      writer = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_frame_format(
        &cfg, 7, RCP_EP_UART_PARITY_EVEN, RCP_EP_UART_STOP_BITS_TWO,
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(7, cfg.uart_nr_bits);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_PARITY_EVEN, cfg.parity);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_TWO, cfg.stop_bits);
}

static void test_set_rx_buffer_size_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_rx_buffer_size(
        &cfg, 256, RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_rx_buffer_size);
}

static void test_set_rx_buffer_size_applies_when_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      writer = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_rx_buffer_size(
        &cfg, 256, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT16(256, cfg.ep_rx_buffer_size);
}

static void test_set_timeout_rejects_unauthorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      none = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_uart_set_timeout(
        &cfg, 50, RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.uart_timeout_ms);
}

static void test_set_timeout_applies_when_authorized(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_server_writer_ctx_t      writer = {0};

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_timeout(
        &cfg, 50, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(50, cfg.uart_timeout_ms);
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
    uint8_t     read_size = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_request(frame.data, frame.len, 6, &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT8(64, read_size);
    TEST_ASSERT_EQUAL_UINT8(3, txn);

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_payload_with_unknown_cmd(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload[1] = {0x01};
    rcp_bytes_t                 frame;
    uint8_t                     read_size;
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
    uint8_t                      read_size;
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
    uint8_t     requested_read_size = 0;
    uint8_t     req_txn = 0;
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_request(read_req.data, read_req.len, 2, &requested_read_size,
                                         &req_txn));
    TEST_ASSERT_EQUAL_UINT8(32, requested_read_size);

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
    RUN_TEST(test_functional_cfg_writable_true_hw_configured_any_writer);
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

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_write_request_round_trip);
    RUN_TEST(test_write_request_rejects_wrong_bus_op_short_frame_bad_type);
    RUN_TEST(test_write_response_round_trip_untimed_and_timed);
    RUN_TEST(test_write_response_decode_rejects_wrong_bus_and_short_frame);

    RUN_TEST(test_read_request_round_trip);
    RUN_TEST(test_read_request_rejects_payload_with_unknown_cmd);
    RUN_TEST(test_read_request_rejects_wrong_bus_op_short_frame);
    RUN_TEST(test_read_response_round_trip_full_length);
    RUN_TEST(test_read_response_round_trip_short_read_single_avtpdu);
    RUN_TEST(test_read_response_decode_rejects_wrong_bus_and_short_frame);

    return UNITY_END();
}
