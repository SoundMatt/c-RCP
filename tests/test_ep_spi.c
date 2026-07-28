//cfusa:test REQ-SPI-001
//cfusa:test REQ-SPI-002
//cfusa:test REQ-SPI-003
//cfusa:test REQ-SPI-004
//cfusa:test REQ-SPI-005
//cfusa:test REQ-SPI-006
//cfusa:test REQ-SPI-007
//cfusa:test REQ-SPI-008
//cfusa:test REQ-SPI-009
//cfusa:test REQ-SPI-010
//cfusa:test REQ-SPI-011
//cfusa:test REQ-SPI-012
//cfusa:test REQ-SPI-013
//cfusa:test REQ-SPI-014
//cfusa:test REQ-SPI-015
//cfusa:test REQ-SPI-016
//cfusa:test REQ-SPI-017
//cfusa:test REQ-SPI-018
//cfusa:test REQ-SPI-019
//cfusa:test REQ-SPI-020
//cfusa:test REQ-SPI-021
//cfusa:test REQ-SPI-022
//cfusa:test REQ-SPI-023
//cfusa:test REQ-SPI-024
//cfusa:test REQ-SPI-025
//cfusa:test REQ-SPI-026
//cfusa:test REQ-SPI-027
//cfusa:test REQ-SPI-028
//cfusa:test REQ-SPI-029
//cfusa:test REQ-SPI-030
//cfusa:test REQ-SPI-031
//cfusa:test REQ-SPI-032
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_spi.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Channel addressing ────────────────────────────────────────────────────── */

static void test_channel_valid_bounds(void)
{
    TEST_ASSERT_TRUE(rcp_ep_spi_channel_valid(0));
    TEST_ASSERT_TRUE(rcp_ep_spi_channel_valid(5));
    TEST_ASSERT_FALSE(rcp_ep_spi_channel_valid(6));
    TEST_ASSERT_FALSE(rcp_ep_spi_channel_valid(255));
}

/* ── Clock mode ─────────────────────────────────────────────────────────────── */

static void test_mode_valid(void)
{
    uint8_t v;

    for (v = 0; v <= 3; v++) {
        TEST_ASSERT_TRUE(rcp_ep_spi_mode_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_valid(4));
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_valid(255));
}

static void test_mode_cpol(void)
{
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_0));
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_1));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_2));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_3));
}

static void test_mode_cpha(void)
{
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_0));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_1));
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_2));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_3));
}

/* ── Per-channel trigger signals ────────────────────────────────────────────── */

static void test_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_NONE, RCP_EP_SPI_EVENT_TRANSFER_DONE));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_NONE, RCP_EP_SPI_EVENT_CS_ASSERT));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_NONE, RCP_EP_SPI_EVENT_CS_DEASSERT));
}

static void test_trigger_transfer_done(void)
{
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_TRANSFER_DONE, RCP_EP_SPI_EVENT_TRANSFER_DONE));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_TRANSFER_DONE, RCP_EP_SPI_EVENT_CS_ASSERT));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_TRANSFER_DONE, RCP_EP_SPI_EVENT_CS_DEASSERT));
}

static void test_trigger_cs_assert(void)
{
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_ASSERT, RCP_EP_SPI_EVENT_CS_ASSERT));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_ASSERT, RCP_EP_SPI_EVENT_TRANSFER_DONE));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_ASSERT, RCP_EP_SPI_EVENT_CS_DEASSERT));
}

static void test_trigger_cs_deassert(void)
{
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_DEASSERT, RCP_EP_SPI_EVENT_CS_DEASSERT));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_DEASSERT, RCP_EP_SPI_EVENT_TRANSFER_DONE));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_DEASSERT, RCP_EP_SPI_EVENT_CS_ASSERT));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    size_t i;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);

    for (i = 0; i < RCP_EP_SPI_MAX_CHANNELS; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_0, cfg.channels[i].mode);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_BIT_ORDER_MSB_FIRST, cfg.channels[i].bit_order);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_CS_ACTIVE_LOW, cfg.channels[i].cs_polarity);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_TRIGGER_NONE, cfg.channels[i].trigger);
        TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[i].clock_divider);
        TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[i].inter_byte_delay_ns);
        TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[i].inter_transfer_delay_ns);
    }
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_spi_functional_cfg_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_spi_functional_cfg_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t via_ep0 = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_spi_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_spi_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_spi_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

static void test_set_channel_mode_rejects_invalid_channel_or_unauthorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     authorized = {0};
    rcp_lifecycle_writer_ctx_t     none = {0};

    authorized.via_root_client_ep0 = true;
    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_mode(
        &cfg, 6, RCP_EP_SPI_MODE_3, RCP_LIFECYCLE_HW_CONFIGURED, authorized));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_0, cfg.channels[0].mode);

    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_mode(
        &cfg, 0, RCP_EP_SPI_MODE_3, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_0, cfg.channels[0].mode);
}

static void test_set_channel_mode_applies_when_authorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_spi_set_channel_mode(
        &cfg, 3, RCP_EP_SPI_MODE_2, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_2, cfg.channels[3].mode);
}

static void test_set_channel_bit_order_rejects_invalid_channel_or_unauthorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_bit_order(
        &cfg, 6, RCP_EP_SPI_BIT_ORDER_LSB_FIRST, RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_bit_order(
        &cfg, 0, RCP_EP_SPI_BIT_ORDER_LSB_FIRST, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_BIT_ORDER_MSB_FIRST, cfg.channels[0].bit_order);
}

static void test_set_channel_bit_order_applies_when_authorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_spi_set_channel_bit_order(
        &cfg, 1, RCP_EP_SPI_BIT_ORDER_LSB_FIRST, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_BIT_ORDER_LSB_FIRST, cfg.channels[1].bit_order);
}

static void test_set_channel_cs_polarity_rejects_invalid_channel_or_unauthorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_cs_polarity(
        &cfg, 6, RCP_EP_SPI_CS_ACTIVE_HIGH, RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_cs_polarity(
        &cfg, 0, RCP_EP_SPI_CS_ACTIVE_HIGH, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_CS_ACTIVE_LOW, cfg.channels[0].cs_polarity);
}

static void test_set_channel_cs_polarity_applies_when_authorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_spi_set_channel_cs_polarity(
        &cfg, 2, RCP_EP_SPI_CS_ACTIVE_HIGH, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_CS_ACTIVE_HIGH, cfg.channels[2].cs_polarity);
}

static void test_set_channel_clock_divider_rejects_invalid_channel_or_unauthorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_clock_divider(
        &cfg, 6, 128, RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_clock_divider(
        &cfg, 0, 128, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[0].clock_divider);
}

static void test_set_channel_clock_divider_applies_when_authorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_spi_set_channel_clock_divider(
        &cfg, 4, 256, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(256, cfg.channels[4].clock_divider);
}

static void test_set_channel_timing_rejects_invalid_channel_or_unauthorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_timing(
        &cfg, 6, 100, 200, RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_timing(
        &cfg, 0, 100, 200, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[0].inter_byte_delay_ns);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[0].inter_transfer_delay_ns);
}

static void test_set_channel_timing_applies_when_authorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_spi_set_channel_timing(
        &cfg, 5, 50, 500, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(50, cfg.channels[5].inter_byte_delay_ns);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.channels[5].inter_transfer_delay_ns);
}

static void test_set_channel_trigger_rejects_invalid_channel_or_unauthorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_trigger(
        &cfg, 6, RCP_EP_SPI_TRIGGER_TRANSFER_DONE, RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_FALSE(rcp_ep_spi_set_channel_trigger(
        &cfg, 0, RCP_EP_SPI_TRIGGER_TRANSFER_DONE, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_TRIGGER_NONE, cfg.channels[0].trigger);
}

static void test_set_channel_trigger_applies_when_authorized(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_spi_set_channel_trigger(
        &cfg, 0, RCP_EP_SPI_TRIGGER_CS_ASSERT, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_TRIGGER_CS_ASSERT, cfg.channels[0].trigger);
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_spi_errc_t codes[] = {
        RCP_EP_SPI_OK, RCP_EP_SPI_ERR_SHORT_FRAME, RCP_EP_SPI_ERR_BAD_MSG_TYPE,
        RCP_EP_SPI_ERR_WRONG_BUS, RCP_EP_SPI_ERR_WRONG_OP, RCP_EP_SPI_ERR_BAD_CHANNEL,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_spi_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_spi_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_spi_strerror((rcp_ep_spi_errc_t)999));
}

/* ── Transfer request round trip ───────────────────────────────────────────── */

static void test_transfer_request_round_trip(void)
{
    uint8_t     tx[3] = {0x01, 0x02, 0x03};
    rcp_bytes_t frame = rcp_ep_spi_encode_transfer_request(4, 2, tx, sizeof(tx), 9);
    uint8_t     channel = 0xFF;
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT8(2, channel);
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8(9, txn);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_round_trip_empty_payload(void)
{
    rcp_bytes_t frame = rcp_ep_spi_encode_transfer_request(4, 0, NULL, 0, 1);
    uint8_t     channel = 0xFF;
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT8(0, channel);
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame = rcp_ep_spi_encode_transfer_request(4, 1, tx, sizeof(tx), 0);
    uint8_t     channel;
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_WRONG_BUS,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 5, &channel, &out_tx,
                                            &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     channel;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ; /* not a transfer request */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_WRONG_OP,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_bad_channel(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     channel;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = 7; /* not a valid channel selector */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_BAD_CHANNEL,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    uint8_t              channel;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_BAD_MSG_TYPE,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0};
    uint8_t        channel;
    const uint8_t  *out_tx;
    size_t          out_tx_len;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_SHORT_FRAME,
        rcp_ep_spi_decode_transfer_request(too_short, sizeof(too_short), 4, &channel, &out_tx,
                                            &out_tx_len, &txn));
}

/* ── Response round trip ───────────────────────────────────────────────────── */

static void test_response_round_trip_untimed(void)
{
    uint8_t     rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t frame = rcp_ep_spi_encode_response(2, 5, rx, sizeof(rx), 11, false, 0);
    uint8_t     channel = 0xFF;
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
        rcp_ep_spi_decode_response(frame.data, frame.len, 2, &channel, &out_rx, &out_rx_len,
                                    &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT8(5, channel);
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
    rcp_bytes_t frame = rcp_ep_spi_encode_response(2, 3, rx, sizeof(rx), 200, true,
                                                    0x0102030405060708ull);
    uint8_t     channel = 0xFF;
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
        rcp_ep_spi_decode_response(frame.data, frame.len, 2, &channel, &out_rx, &out_rx_len,
                                    &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT8(3, channel);
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);
    TEST_ASSERT_EQUAL_UINT8(200, txn);

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_spi_encode_response(2, 0, NULL, 0, 0, false, 0);
    uint8_t     channel;
    const uint8_t *out_rx;
    size_t      out_rx_len;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_WRONG_BUS,
        rcp_ep_spi_decode_response(frame.data, frame.len, 3, &channel, &out_rx, &out_rx_len,
                                    &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_bad_channel(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     channel;
    const uint8_t               *out_rx;
    size_t                       out_rx_len;
    bool                         timed;
    uint64_t                     ts;
    uint8_t                      txn;

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.evt         = 6; /* not a valid channel selector */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_BAD_CHANNEL,
        rcp_ep_spi_decode_response(frame.data, frame.len, 2, &channel, &out_rx, &out_rx_len,
                                    &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_short_frame(void)
{
    uint8_t  too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    uint8_t  channel;
    const uint8_t *out_rx;
    size_t   out_rx_len;
    bool     timed;
    uint64_t ts;
    uint8_t  txn;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_SHORT_FRAME,
        rcp_ep_spi_decode_response(too_short, sizeof(too_short), 2, &channel, &out_rx,
                                    &out_rx_len, &timed, &ts, &txn));
}

/* ── Compound-wait truncation rule ─────────────────────────────────────────── */

static void test_compound_wait_status_equal_compares_only_first_4_bytes(void)
{
    uint8_t status[RCP_EP_SPI_STATUS_MAX_LEN] = {1, 2, 3, 4, 0xAA, 0xBB, 0xCC};
    uint8_t target[RCP_EP_SPI_STATUS_MAX_LEN] = {1, 2, 3, 4, 0xDE, 0xAD, 0xBE};

    /* Bytes 4.. differ between status and target, but only the first 4 are
     * compared -- see the file header's compound-wait truncation note. */
    TEST_ASSERT_TRUE(rcp_ep_spi_compound_wait_status_equal(status, sizeof(status), target,
                                                            sizeof(target)));

    status[3] = 0xFF; /* now differs within the first 4 bytes */
    TEST_ASSERT_FALSE(rcp_ep_spi_compound_wait_status_equal(status, sizeof(status), target,
                                                             sizeof(target)));
}

static void test_compound_wait_status_equal_false_when_too_short(void)
{
    uint8_t buf4[4] = {1, 2, 3, 4};
    uint8_t buf3[3] = {1, 2, 3};

    TEST_ASSERT_FALSE(rcp_ep_spi_compound_wait_status_equal(buf3, sizeof(buf3), buf4, sizeof(buf4)));
    TEST_ASSERT_FALSE(rcp_ep_spi_compound_wait_status_equal(buf4, sizeof(buf4), buf3, sizeof(buf3)));
    TEST_ASSERT_FALSE(rcp_ep_spi_compound_wait_status_equal(NULL, 0, buf4, sizeof(buf4)));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_channel_valid_bounds);

    RUN_TEST(test_mode_valid);
    RUN_TEST(test_mode_cpol);
    RUN_TEST(test_mode_cpha);

    RUN_TEST(test_trigger_none_never_fires);
    RUN_TEST(test_trigger_transfer_done);
    RUN_TEST(test_trigger_cs_assert);
    RUN_TEST(test_trigger_cs_deassert);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_true_hw_configured_any_writer);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_set_channel_mode_rejects_invalid_channel_or_unauthorized);
    RUN_TEST(test_set_channel_mode_applies_when_authorized);
    RUN_TEST(test_set_channel_bit_order_rejects_invalid_channel_or_unauthorized);
    RUN_TEST(test_set_channel_bit_order_applies_when_authorized);
    RUN_TEST(test_set_channel_cs_polarity_rejects_invalid_channel_or_unauthorized);
    RUN_TEST(test_set_channel_cs_polarity_applies_when_authorized);
    RUN_TEST(test_set_channel_clock_divider_rejects_invalid_channel_or_unauthorized);
    RUN_TEST(test_set_channel_clock_divider_applies_when_authorized);
    RUN_TEST(test_set_channel_timing_rejects_invalid_channel_or_unauthorized);
    RUN_TEST(test_set_channel_timing_applies_when_authorized);
    RUN_TEST(test_set_channel_trigger_rejects_invalid_channel_or_unauthorized);
    RUN_TEST(test_set_channel_trigger_applies_when_authorized);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_transfer_request_round_trip);
    RUN_TEST(test_transfer_request_round_trip_empty_payload);
    RUN_TEST(test_transfer_request_rejects_wrong_bus);
    RUN_TEST(test_transfer_request_rejects_wrong_op);
    RUN_TEST(test_transfer_request_rejects_bad_channel);
    RUN_TEST(test_transfer_request_rejects_bad_msg_type);
    RUN_TEST(test_transfer_request_rejects_short_frame);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_bad_channel);
    RUN_TEST(test_response_decode_rejects_short_frame);

    RUN_TEST(test_compound_wait_status_equal_compares_only_first_4_bytes);
    RUN_TEST(test_compound_wait_status_equal_false_when_too_short);

    return UNITY_END();
}
