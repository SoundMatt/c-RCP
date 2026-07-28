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
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_i2c.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/server.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── i2c_mode ───────────────────────────────────────────────────────────────── */

static void test_mode_valid_bounds(void)
{
    uint8_t v;

    for (v = 0; v <= 3; v++) {
        TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_i2c_mode_valid(4));
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
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_server_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_i2c_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_server_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_server_writer_ctx_t none = {0};
    rcp_server_writer_ctx_t via_ep0 = {0};
    rcp_server_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_i2c_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_i2c_functional_cfg_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

static void test_set_mode_rejects_invalid_mode(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    rcp_server_writer_ctx_t     authorized = {0};

    authorized.via_root_client_ep0 = true;
    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_i2c_set_mode(
        &cfg, (rcp_ep_i2c_mode_t)99, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, authorized));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_STANDARD, cfg.i2c_mode);
}

static void test_set_mode_rejects_unauthorized(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    rcp_server_writer_ctx_t     none = {0};

    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_i2c_set_mode(
        &cfg, RCP_EP_I2C_MODE_FAST, RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_STANDARD, cfg.i2c_mode);
}

static void test_set_mode_applies_when_valid_and_authorized(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;
    rcp_server_writer_ctx_t     writer = {0};

    rcp_ep_i2c_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_i2c_set_mode(
        &cfg, RCP_EP_I2C_MODE_HIGH_SPEED, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_HIGH_SPEED, cfg.i2c_mode);
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_i2c_errc_t codes[] = {
        RCP_EP_I2C_OK, RCP_EP_I2C_ERR_SHORT_FRAME, RCP_EP_I2C_ERR_BAD_MSG_TYPE,
        RCP_EP_I2C_ERR_WRONG_BUS, RCP_EP_I2C_ERR_WRONG_OP,
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

/* ── Transfer request round trip ───────────────────────────────────────────── */

static void test_transfer_request_round_trip_carries_address_bytes(void)
{
    /* First byte models a raw target-device address byte; this module
     * never parses or strips it -- see the file header. */
    uint8_t     tx[4] = {0xA2, 0x10, 0x20, 0x30};
    rcp_bytes_t frame = rcp_ep_i2c_encode_transfer_request(6, tx, sizeof(tx), 7);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 6, &out_tx, &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_round_trip_empty_payload(void)
{
    rcp_bytes_t frame = rcp_ep_i2c_encode_transfer_request(1, NULL, 0, 1);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 1, &out_tx, &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame = rcp_ep_i2c_encode_transfer_request(4, tx, sizeof(tx), 0);
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_WRONG_BUS,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 5, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ; /* not a transfer request */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_WRONG_OP,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_BAD_MSG_TYPE,
        rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0};
    const uint8_t  *out_tx;
    size_t          out_tx_len;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_SHORT_FRAME,
        rcp_ep_i2c_decode_transfer_request(too_short, sizeof(too_short), 4, &out_tx, &out_tx_len,
                                            &txn));
}

/* ── Response round trip ───────────────────────────────────────────────────── */

static void test_response_round_trip_untimed(void)
{
    uint8_t     rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, rx, sizeof(rx), 11, false, 0);
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed, &ts,
                                    &txn));
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
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, rx, sizeof(rx), 200, true,
                                                    0x0102030405060708ull);
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
        rcp_ep_i2c_decode_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed, &ts,
                                    &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);
    TEST_ASSERT_EQUAL_UINT8(200, txn);

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, NULL, 0, 0, false, 0);
    const uint8_t *out_rx;
    size_t      out_rx_len;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_WRONG_BUS,
        rcp_ep_i2c_decode_response(frame.data, frame.len, 3, &out_rx, &out_rx_len, &timed, &ts,
                                    &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_short_frame(void)
{
    uint8_t  too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    const uint8_t *out_rx;
    size_t   out_rx_len;
    bool     timed;
    uint64_t ts;
    uint8_t  txn;

    TEST_ASSERT_EQUAL(RCP_EP_I2C_ERR_SHORT_FRAME,
        rcp_ep_i2c_decode_response(too_short, sizeof(too_short), 2, &out_rx, &out_rx_len, &timed,
                                    &ts, &txn));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_mode_valid_bounds);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_true_hw_configured_any_writer);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_set_mode_rejects_invalid_mode);
    RUN_TEST(test_set_mode_rejects_unauthorized);
    RUN_TEST(test_set_mode_applies_when_valid_and_authorized);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_transfer_request_round_trip_carries_address_bytes);
    RUN_TEST(test_transfer_request_round_trip_empty_payload);
    RUN_TEST(test_transfer_request_rejects_wrong_bus);
    RUN_TEST(test_transfer_request_rejects_wrong_op);
    RUN_TEST(test_transfer_request_rejects_bad_msg_type);
    RUN_TEST(test_transfer_request_rejects_short_frame);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
