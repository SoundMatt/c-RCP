//cfusa:test REQ-CANEP-001
//cfusa:test REQ-CANEP-002
//cfusa:test REQ-CANEP-003
//cfusa:test REQ-CANEP-004
//cfusa:test REQ-CANEP-005
//cfusa:test REQ-CANEP-006
//cfusa:test REQ-CANEP-007
//cfusa:test REQ-CANEP-008
//cfusa:test REQ-CANEP-009
//cfusa:test REQ-CANEP-010
//cfusa:test REQ-CANEP-011
//cfusa:test REQ-CANEP-012
//cfusa:test REQ-CANEP-013
//cfusa:test REQ-CANEP-014
//cfusa:test REQ-CANEP-015
//cfusa:test REQ-CANEP-016
//cfusa:test REQ-CANEP-017
//cfusa:test REQ-CANEP-018
//cfusa:test REQ-CANEP-019
//cfusa:test REQ-CANEP-020
//cfusa:test REQ-CANEP-021
//cfusa:test REQ-CANEP-022
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/ep_can.h>
#include <rcp/fragment.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── frame_format_valid ────────────────────────────────────────────────────── */

static void test_frame_format_valid_bounds(void)
{
    uint8_t v;

    for (v = 0; v <= 5; v++) {
        TEST_ASSERT_TRUE(rcp_ep_can_frame_format_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_valid(6));
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_valid(7));
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_valid(255));
}

/* ── frame_format_is_xl ────────────────────────────────────────────────────── */

static void test_frame_format_is_xl(void)
{
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_CBFF));
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_CEFF));
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_FBFF));
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_FEFF));
    TEST_ASSERT_TRUE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_XL_CLASSICAL_PL));
    TEST_ASSERT_TRUE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_XL_NEW_PL));
}

/* ── frame_format_id_width ─────────────────────────────────────────────────── */

static void test_frame_format_id_width(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ID_WIDTH_BASE_11,
                       rcp_ep_can_frame_format_id_width(RCP_EP_CAN_FRAME_CBFF));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ID_WIDTH_EXTENDED_29,
                       rcp_ep_can_frame_format_id_width(RCP_EP_CAN_FRAME_CEFF));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ID_WIDTH_BASE_11,
                       rcp_ep_can_frame_format_id_width(RCP_EP_CAN_FRAME_FBFF));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ID_WIDTH_EXTENDED_29,
                       rcp_ep_can_frame_format_id_width(RCP_EP_CAN_FRAME_FEFF));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ID_WIDTH_BASE_11,
                       rcp_ep_can_frame_format_id_width(RCP_EP_CAN_FRAME_XL_CLASSICAL_PL));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ID_WIDTH_BASE_11,
                       rcp_ep_can_frame_format_id_width(RCP_EP_CAN_FRAME_XL_NEW_PL));
    /* Invalid format value fails safe to the narrower BASE_11 width. */
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ID_WIDTH_BASE_11,
                       rcp_ep_can_frame_format_id_width((rcp_ep_can_frame_format_t)7));
}

/* ── arbitration_id_valid ──────────────────────────────────────────────────── */

static void test_arbitration_id_valid_base_11(void)
{
    TEST_ASSERT_TRUE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_CBFF, 0));
    TEST_ASSERT_TRUE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_CBFF, 0x7FFu));
    TEST_ASSERT_FALSE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_CBFF, 0x800u));
}

static void test_arbitration_id_valid_extended_29(void)
{
    TEST_ASSERT_TRUE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_CEFF, 0x1FFFFFFFu));
    TEST_ASSERT_FALSE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_CEFF, 0x20000000u));
}

static void test_arbitration_id_valid_xl_uses_base_11(void)
{
    TEST_ASSERT_TRUE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_XL_NEW_PL, 0x7FFu));
    TEST_ASSERT_FALSE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_XL_NEW_PL, 0x800u));
}

static void test_arbitration_id_valid_false_for_invalid_format(void)
{
    TEST_ASSERT_FALSE(rcp_ep_can_arbitration_id_valid((rcp_ep_can_frame_format_t)6, 0));
    TEST_ASSERT_FALSE(rcp_ep_can_arbitration_id_valid((rcp_ep_can_frame_format_t)7, 0));
}

/* ── frame_format_max_data_len ─────────────────────────────────────────────── */

static void test_frame_format_max_data_len(void)
{
    TEST_ASSERT_EQUAL_UINT32(RCP_EP_CAN_CLASSICAL_MAX_DATA_LEN,
                              rcp_ep_can_frame_format_max_data_len(RCP_EP_CAN_FRAME_CBFF));
    TEST_ASSERT_EQUAL_UINT32(RCP_EP_CAN_CLASSICAL_MAX_DATA_LEN,
                              rcp_ep_can_frame_format_max_data_len(RCP_EP_CAN_FRAME_CEFF));
    TEST_ASSERT_EQUAL_UINT32(RCP_EP_CAN_FD_MAX_DATA_LEN,
                              rcp_ep_can_frame_format_max_data_len(RCP_EP_CAN_FRAME_FBFF));
    TEST_ASSERT_EQUAL_UINT32(RCP_EP_CAN_FD_MAX_DATA_LEN,
                              rcp_ep_can_frame_format_max_data_len(RCP_EP_CAN_FRAME_FEFF));
    TEST_ASSERT_EQUAL_UINT32(
        RCP_EP_CAN_XL_MAX_DATA_LEN,
        rcp_ep_can_frame_format_max_data_len(RCP_EP_CAN_FRAME_XL_CLASSICAL_PL));
    TEST_ASSERT_EQUAL_UINT32(RCP_EP_CAN_XL_MAX_DATA_LEN,
                              rcp_ep_can_frame_format_max_data_len(RCP_EP_CAN_FRAME_XL_NEW_PL));
    TEST_ASSERT_EQUAL_UINT32(0, rcp_ep_can_frame_format_max_data_len((rcp_ep_can_frame_format_t)7));
}

/* ── xl_filter_index_valid ─────────────────────────────────────────────────── */

static void test_xl_filter_index_valid_bounds(void)
{
    uint8_t i;

    for (i = 0; i < RCP_EP_CAN_XL_MAX_FILTERS; i++) {
        TEST_ASSERT_TRUE(rcp_ep_can_xl_filter_index_valid(i));
    }
    TEST_ASSERT_FALSE(rcp_ep_can_xl_filter_index_valid(RCP_EP_CAN_XL_MAX_FILTERS));
    TEST_ASSERT_FALSE(rcp_ep_can_xl_filter_index_valid(255));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_can_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_can_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.arbitration_timing.prescaler);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.fd_data_timing.prescaler);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.xl_data_timing.prescaler);
    TEST_ASSERT_FALSE(cfg.delay_comp_enable);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.delay_comp_offset);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.exec_delay_clk_divider);
    TEST_ASSERT_FALSE(cfg.xl_filters[0].enable);
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_can_functional_cfg_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_can_functional_cfg_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
}

static void test_set_arbitration_timing_rejects_unauthorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};
    rcp_ep_can_bit_timing_t     timing = {0};

    rcp_ep_can_functional_cfg_init(&cfg);
    timing.prescaler = 5;

    TEST_ASSERT_FALSE(rcp_ep_can_set_arbitration_timing(
        &cfg, timing, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.arbitration_timing.prescaler);
}

static void test_set_arbitration_timing_applies_when_authorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    rcp_ep_can_bit_timing_t     timing = {0};

    rcp_ep_can_functional_cfg_init(&cfg);
    timing.prescaler        = 5;
    timing.prop_seg         = 3;
    timing.phase_seg1       = 4;
    timing.phase_seg2       = 2;
    timing.sync_jump_width  = 1;

    TEST_ASSERT_TRUE(rcp_ep_can_set_arbitration_timing(
        &cfg, timing, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(5, cfg.arbitration_timing.prescaler);
    TEST_ASSERT_EQUAL_UINT16(3, cfg.arbitration_timing.prop_seg);
}

static void test_set_fd_data_timing_applies_when_authorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    rcp_ep_can_bit_timing_t     timing = {0};

    rcp_ep_can_functional_cfg_init(&cfg);
    timing.prescaler = 9;

    TEST_ASSERT_TRUE(rcp_ep_can_set_fd_data_timing(
        &cfg, timing, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(9, cfg.fd_data_timing.prescaler);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.arbitration_timing.prescaler);
}

static void test_set_xl_data_timing_applies_when_authorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    rcp_ep_can_bit_timing_t     timing = {0};

    rcp_ep_can_functional_cfg_init(&cfg);
    timing.prescaler = 13;

    TEST_ASSERT_TRUE(rcp_ep_can_set_xl_data_timing(
        &cfg, timing, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(13, cfg.xl_data_timing.prescaler);
}

static void test_set_delay_compensation_applies_when_authorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_can_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_can_set_delay_compensation(
        &cfg, true, 7, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_TRUE(cfg.delay_comp_enable);
    TEST_ASSERT_EQUAL_UINT8(7, cfg.delay_comp_offset);
}

static void test_set_delay_compensation_rejects_unauthorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_can_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_can_set_delay_compensation(
        &cfg, true, 7, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_FALSE(cfg.delay_comp_enable);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.delay_comp_offset);
}

static void test_set_exec_delay_clk_divider_applies_when_authorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_can_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_can_set_exec_delay_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(42, cfg.exec_delay_clk_divider);
}

static void test_set_exec_delay_clk_divider_rejects_unauthorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_can_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_can_set_exec_delay_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.exec_delay_clk_divider);
}

static void test_set_xl_filter_applies_when_authorized(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    rcp_ep_can_xl_filter_t      filter = {0};

    rcp_ep_can_functional_cfg_init(&cfg);
    filter.id     = 0x123;
    filter.mask   = 0x7FF;
    filter.enable = true;

    TEST_ASSERT_TRUE(rcp_ep_can_set_xl_filter(
        &cfg, 2, filter, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(0x123, cfg.xl_filters[2].id);
    TEST_ASSERT_TRUE(cfg.xl_filters[2].enable);
    TEST_ASSERT_FALSE(cfg.xl_filters[0].enable);
}

static void test_set_xl_filter_rejects_bad_index(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    rcp_ep_can_xl_filter_t      filter = {0};

    rcp_ep_can_functional_cfg_init(&cfg);
    filter.enable = true;

    TEST_ASSERT_FALSE(rcp_ep_can_set_xl_filter(
        &cfg, RCP_EP_CAN_XL_MAX_FILTERS, filter, RCP_LIFECYCLE_HW_CONFIGURED, writer));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_can_errc_t codes[] = {
        RCP_EP_CAN_OK,           RCP_EP_CAN_ERR_SHORT_FRAME,
        RCP_EP_CAN_ERR_BAD_MSG_TYPE, RCP_EP_CAN_ERR_WRONG_BUS,
        RCP_EP_CAN_ERR_WRONG_OP, RCP_EP_CAN_ERR_BAD_FRAME_FORMAT,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_can_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_can_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_can_strerror((rcp_ep_can_errc_t)999));
}

/* ── Frame request round trip: Classical ───────────────────────────────────── */

static void test_frame_request_round_trip_classical(void)
{
    uint8_t     tx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(
        6, RCP_EP_CAN_FRAME_CBFF, 0x123, NULL, tx, sizeof(tx), 9);
    rcp_ep_can_frame_format_t format;
    uint32_t                  id = 0;
    rcp_ep_can_xl_header_t    xl_hdr;
    const uint8_t             *out_tx = NULL;
    size_t                     out_tx_len = 0;
    uint8_t                    txn = 0;

    memset(&xl_hdr, 0xAA, sizeof(xl_hdr));

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 6, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_CBFF, format);
    TEST_ASSERT_EQUAL_UINT32(0x123, id);
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8(9, txn);

    rcp_bytes_free(&frame);
}

/* ── Frame request round trip: CAN XL (extra header fields) ─────────────────── */

static void test_frame_request_round_trip_xl(void)
{
    uint8_t                tx[2048];
    rcp_ep_can_xl_header_t xl_hdr_in = {0};
    rcp_bytes_t            frame;
    rcp_ep_can_frame_format_t format;
    uint32_t                  id = 0;
    rcp_ep_can_xl_header_t    xl_hdr_out;
    const uint8_t             *out_tx = NULL;
    size_t                     out_tx_len = 0;
    uint8_t                    txn = 0;
    size_t                     i;

    for (i = 0; i < sizeof(tx); i++) tx[i] = (uint8_t)(i & 0xFFu);
    xl_hdr_in.sdt  = 0x42;
    xl_hdr_in.vcid = 0x07;
    xl_hdr_in.af   = 0xDEADBEEFu;

    frame = rcp_ep_can_encode_frame_request(3, RCP_EP_CAN_FRAME_XL_NEW_PL, 0x7FF, &xl_hdr_in, tx,
                                             sizeof(tx), 1);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT32((size_t)(4 + 6 + sizeof(tx)) + RCP_ACF_ABB_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 3, &format, &id, &xl_hdr_out,
                                         &out_tx, &out_tx_len, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_XL_NEW_PL, format);
    TEST_ASSERT_EQUAL_UINT32(0x7FF, id);
    TEST_ASSERT_EQUAL_UINT8(0x42, xl_hdr_out.sdt);
    TEST_ASSERT_EQUAL_UINT8(0x07, xl_hdr_out.vcid);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, xl_hdr_out.af);
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));

    rcp_bytes_free(&frame);
}

static void test_frame_request_encode_rejects_bad_frame_format(void)
{
    rcp_bytes_t frame =
        rcp_ep_can_encode_frame_request(1, (rcp_ep_can_frame_format_t)7, 0, NULL, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_frame_request_encode_rejects_bad_arbitration_id(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(1, RCP_EP_CAN_FRAME_CBFF, 0x800u, NULL,
                                                          NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_frame_request_encode_rejects_data_too_long(void)
{
    uint8_t tx[9] = {0};
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(1, RCP_EP_CAN_FRAME_CBFF, 0, NULL, tx,
                                                          sizeof(tx), 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_frame_request_encode_rejects_missing_xl_header(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(
        1, RCP_EP_CAN_FRAME_XL_CLASSICAL_PL, 0, NULL, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_frame_request_encode_rejects_unexpected_xl_header(void)
{
    rcp_ep_can_xl_header_t xl_hdr = {0};
    rcp_bytes_t            frame =
        rcp_ep_can_encode_frame_request(1, RCP_EP_CAN_FRAME_CBFF, 0, &xl_hdr, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_frame_request_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame =
        rcp_ep_can_encode_frame_request(4, RCP_EP_CAN_FRAME_CBFF, 1, NULL, NULL, 0, 0);
    rcp_ep_can_frame_format_t format;
    uint32_t                  id;
    rcp_ep_can_xl_header_t    xl_hdr;
    const uint8_t             *out_tx;
    size_t                     out_tx_len;
    uint8_t                    txn;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_WRONG_BUS,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 5, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_frame_request_decode_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_can_frame_format_t   format;
    uint32_t                    id;
    rcp_ep_can_xl_header_t      xl_hdr;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_WRONG_OP,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_frame_request_decode_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    rcp_ep_can_frame_format_t format;
    uint32_t                  id;
    rcp_ep_can_xl_header_t    xl_hdr;
    const uint8_t             *out_tx;
    size_t                     out_tx_len;
    uint8_t                    txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_MSG_TYPE,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_frame_request_decode_rejects_short_frame(void)
{
    uint8_t too_short[3] = {0};
    rcp_ep_can_frame_format_t format;
    uint32_t                  id;
    rcp_ep_can_xl_header_t    xl_hdr;
    const uint8_t             *out_tx;
    size_t                     out_tx_len;
    uint8_t                    txn;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_SHORT_FRAME,
        rcp_ep_can_decode_frame_request(too_short, sizeof(too_short), 4, &format, &id, &xl_hdr,
                                         &out_tx, &out_tx_len, &txn));
}

static void test_frame_request_decode_rejects_bad_frame_format(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_can_frame_format_t   format;
    uint32_t                    id;
    rcp_ep_can_xl_header_t      xl_hdr;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;
    uint8_t                      payload[4] = {0};

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = 7; /* reserved -- no defined frame format */
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_FRAME_FORMAT,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_frame_request_decode_rejects_short_xl_prefix(void)
{
    /* frame_format = XL_NEW_PL (evt=5) but payload is only the 4-byte
     * arbitration-id prefix, short of the 6-byte SDT/VCID/AF prefix an XL
     * frame requires. */
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_can_frame_format_t   format;
    uint32_t                    id;
    rcp_ep_can_xl_header_t      xl_hdr;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;
    uint8_t                      payload[4] = {0};

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = (uint8_t)RCP_EP_CAN_FRAME_XL_NEW_PL;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_SHORT_FRAME,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

/* ── Response round trip ───────────────────────────────────────────────────── */

static void test_frame_response_round_trip_untimed(void)
{
    uint8_t     rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t frame = rcp_ep_can_encode_frame_response(
        2, RCP_EP_CAN_FRAME_FBFF, 0x55, NULL, rx, sizeof(rx), 11, false, 0);
    rcp_ep_can_frame_format_t format;
    uint32_t                  id = 0;
    rcp_ep_can_xl_header_t    xl_hdr;
    const uint8_t             *out_rx = NULL;
    size_t                     out_rx_len = 0;
    bool                       timed = true;
    uint64_t                   ts = 1;
    uint8_t                    txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
        rcp_ep_can_decode_frame_response(frame.data, frame.len, 2, &format, &id, &xl_hdr, &out_rx,
                                          &out_rx_len, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_FBFF, format);
    TEST_ASSERT_EQUAL_UINT32(0x55, id);
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0, ts);
    TEST_ASSERT_EQUAL_UINT8(11, txn);

    rcp_bytes_free(&frame);
}

static void test_frame_response_round_trip_timed_xl(void)
{
    uint8_t                rx[3] = {0x11, 0x22, 0x33};
    rcp_ep_can_xl_header_t xl_hdr_in = {0};
    rcp_bytes_t            frame;
    rcp_ep_can_frame_format_t format;
    uint32_t                  id = 0;
    rcp_ep_can_xl_header_t    xl_hdr_out;
    const uint8_t             *out_rx = NULL;
    size_t                     out_rx_len = 0;
    bool                       timed = false;
    uint64_t                   ts = 0;
    uint8_t                    txn = 0;

    xl_hdr_in.sdt  = 1;
    xl_hdr_in.vcid = 2;
    xl_hdr_in.af   = 0x0A0B0C0Du;

    frame = rcp_ep_can_encode_frame_response(2, RCP_EP_CAN_FRAME_XL_CLASSICAL_PL, 0x10, &xl_hdr_in,
                                              rx, sizeof(rx), 200, true, 0x0102030405060708ull);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
        rcp_ep_can_decode_frame_response(frame.data, frame.len, 2, &format, &id, &xl_hdr_out,
                                          &out_rx, &out_rx_len, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_XL_CLASSICAL_PL, format);
    TEST_ASSERT_EQUAL_UINT32(0x10, id);
    TEST_ASSERT_EQUAL_UINT8(1, xl_hdr_out.sdt);
    TEST_ASSERT_EQUAL_UINT8(2, xl_hdr_out.vcid);
    TEST_ASSERT_EQUAL_UINT32(0x0A0B0C0Du, xl_hdr_out.af);
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);
    TEST_ASSERT_EQUAL_UINT8(200, txn);

    rcp_bytes_free(&frame);
}

static void test_frame_response_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_response(2, RCP_EP_CAN_FRAME_CBFF, 0, NULL, NULL,
                                                           0, 0, false, 0);
    rcp_ep_can_frame_format_t format;
    uint32_t                  id;
    rcp_ep_can_xl_header_t    xl_hdr;
    const uint8_t             *out_rx;
    size_t                     out_rx_len;
    bool                       timed;
    uint64_t                   ts;
    uint8_t                    txn;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_WRONG_BUS,
        rcp_ep_can_decode_frame_response(frame.data, frame.len, 3, &format, &id, &xl_hdr, &out_rx,
                                          &out_rx_len, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_frame_response_decode_rejects_short_frame(void)
{
    uint8_t  too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    rcp_ep_can_frame_format_t format;
    uint32_t                  id;
    rcp_ep_can_xl_header_t    xl_hdr;
    const uint8_t             *out_rx;
    size_t                     out_rx_len;
    bool                       timed;
    uint64_t                   ts;
    uint8_t                    txn;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_SHORT_FRAME,
        rcp_ep_can_decode_frame_response(too_short, sizeof(too_short), 2, &format, &id, &xl_hdr,
                                          &out_rx, &out_rx_len, &timed, &ts, &txn));
}

static void test_frame_response_encode_rejects_bad_frame_format(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_response(
        1, (rcp_ep_can_frame_format_t)6, 0, NULL, NULL, 0, 0, false, 0);

    TEST_ASSERT_NULL(frame.data);
}

/* ── Fragmented response (Phase 20, fragment.h) ────────────────────────────── */

static void test_fragment_count_one_when_fits_in_one_fragment(void)
{
    /* A 3-byte classical response's combined (4-byte prefix + 3-byte
     * data) payload is 7 octets; with a generous 100-octet cap this needs
     * exactly one (unfragmented) frame -- count is 1, not 0 (0 means
     * "not representable", not "no fragmentation needed"). */
    size_t count = rcp_ep_can_frame_response_fragment_count(
        RCP_EP_CAN_FRAME_CBFF, 0x10, NULL, 3, 100);
    TEST_ASSERT_EQUAL_UINT(1, count);
}

static void test_fragment_count_zero_for_bad_preconditions(void)
{
    /* Invalid frame_format -> encode_preconditions_ok() fails -> 0. */
    size_t count = rcp_ep_can_frame_response_fragment_count(
        (rcp_ep_can_frame_format_t)7, 0x10, NULL, 3, 100);
    TEST_ASSERT_EQUAL_UINT(0, count);
}

/* Closes the single-AVTPDU-worst-case test deferred at milestone 72
 * (v0.72.0): a full RCP_EP_CAN_XL_MAX_DATA_LEN (2048)-byte CAN XL captured
 * frame, whose combined prefix-then-data ACF payload (2058 octets, see
 * ep_can.h's file header) does not fit within a single
 * RCP_AVTP_NTSCF_MAX_PAYLOAD (2047)-byte NTSCF AVTPDU -- exactly the
 * scenario ROADMAP.md's Phase 20 go-decision names as the concrete driver
 * for this milestone. Encodes it fragmented at a max_fragment_payload
 * comfortably under that NTSCF ceiling, then reassembles it back via
 * fragment.h and this module's own reassembled-response decode helper. */
static void test_fragment_worst_case_can_xl_response_round_trip(void)
{
    uint8_t                     rx[RCP_EP_CAN_XL_MAX_DATA_LEN];
    rcp_ep_can_xl_header_t      xl_hdr_in = {0};
    rcp_ep_can_xl_header_t      xl_hdr_out;
    size_t                      i;
    size_t                      max_fragment_payload = 1024;
    size_t                      count;
    rcp_bytes_t                 frames[4];
    rcp_fragment_reassembler_t  reasm;
    size_t                      combined_len = 4u + 6u + RCP_EP_CAN_XL_MAX_DATA_LEN;

    for (i = 0; i < sizeof(rx); i++) rx[i] = (uint8_t)(i * 3 + 7);
    xl_hdr_in.sdt  = 0x5;
    xl_hdr_in.vcid = 0x9;
    xl_hdr_in.af   = 0xDEADBEEFu;

    count = rcp_ep_can_frame_response_fragment_count(
        RCP_EP_CAN_FRAME_XL_NEW_PL, 0x123, &xl_hdr_in, sizeof(rx), max_fragment_payload);
    TEST_ASSERT_EQUAL_UINT(3, count); /* 2058 / 1024 -> ceil = 3 */
    TEST_ASSERT_TRUE(count <= (sizeof(frames) / sizeof(frames[0])));

    count = rcp_ep_can_encode_frame_response_fragmented(
        7, RCP_EP_CAN_FRAME_XL_NEW_PL, 0x123, &xl_hdr_in, rx, sizeof(rx), 55, false, 0,
        max_fragment_payload, frames);
    TEST_ASSERT_EQUAL_UINT(3, count);

    /* Every individual fragment must fit comfortably under NTSCF's own
     * single-AVTPDU payload ceiling -- the whole point of fragmenting. */
    for (i = 0; i < count; i++) {
        TEST_ASSERT_NOT_NULL(frames[i].data);
        TEST_ASSERT_TRUE(frames[i].len < RCP_AVTP_NTSCF_MAX_PAYLOAD);
    }

    rcp_fragment_reassembler_init(&reasm, combined_len);
    for (i = 0; i < count; i++) {
        rcp_ep_can_frame_format_t   fmt;
        bool                         ms;
        uint8_t                      segnum;
        const uint8_t                *payload;
        size_t                        payload_len;
        bool                          timed;
        uint64_t                      ts;
        uint8_t                       txn;
        rcp_fragment_reasm_result_t   rc;

        TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
            rcp_ep_can_decode_frame_response_fragment(frames[i].data, frames[i].len, 7, &fmt,
                                                        &ms, &segnum, &payload, &payload_len,
                                                        &timed, &ts, &txn));
        TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_XL_NEW_PL, fmt);
        TEST_ASSERT_EQUAL_UINT8(55, txn);
        TEST_ASSERT_FALSE(timed);

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
        TEST_ASSERT_EQUAL_UINT(combined_len, reassembled_len);

        {
            uint32_t       out_id = 0;
            const uint8_t *out_rx = NULL;
            size_t         out_rx_len = 0;

            TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
                rcp_ep_can_decode_reassembled_frame_response(
                    reassembled, reassembled_len, RCP_EP_CAN_FRAME_XL_NEW_PL, &out_id,
                    &xl_hdr_out, &out_rx, &out_rx_len));

            TEST_ASSERT_EQUAL_UINT32(0x123, out_id);
            TEST_ASSERT_EQUAL_UINT8(0x5, xl_hdr_out.sdt);
            TEST_ASSERT_EQUAL_UINT8(0x9, xl_hdr_out.vcid);
            TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, xl_hdr_out.af);
            TEST_ASSERT_EQUAL_UINT(sizeof(rx), out_rx_len);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
        }
    }

    rcp_fragment_reassembler_destroy(&reasm);
    for (i = 0; i < count; i++) rcp_bytes_free(&frames[i]);
}

static void test_fragment_response_unfragmented_matches_single_frame_path(void)
{
    /* When the combined payload already fits in one fragment, the
     * fragmented encoder must produce exactly what the plain,
     * unfragmented encoder would have -- fragmentation is a strict
     * superset of the single-frame wire format, not a parallel one. */
    uint8_t      rx[3] = {0xAA, 0xBB, 0xCC};
    rcp_bytes_t  plain;
    rcp_bytes_t  fragmented[1];
    size_t       count;

    plain = rcp_ep_can_encode_frame_response(4, RCP_EP_CAN_FRAME_CBFF, 0x42, NULL, rx,
                                              sizeof(rx), 9, false, 0);
    TEST_ASSERT_NOT_NULL(plain.data);

    count = rcp_ep_can_encode_frame_response_fragmented(
        4, RCP_EP_CAN_FRAME_CBFF, 0x42, NULL, rx, sizeof(rx), 9, false, 0, 1024, fragmented);
    TEST_ASSERT_EQUAL_UINT(1, count);

    TEST_ASSERT_EQUAL_UINT(plain.len, fragmented[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plain.data, fragmented[0].data, plain.len);

    rcp_bytes_free(&plain);
    rcp_bytes_free(&fragmented[0]);
}

static void test_fragment_encode_rejects_bad_preconditions(void)
{
    rcp_bytes_t frames[4];
    size_t      count = rcp_ep_can_encode_frame_response_fragmented(
        4, (rcp_ep_can_frame_format_t)7, 0x42, NULL, NULL, 0, 9, false, 0, 1024, frames);
    TEST_ASSERT_EQUAL_UINT(0, count);
}

static void test_fragment_decode_fragment_rejects_wrong_bus(void)
{
    uint8_t     rx[3] = {1, 2, 3};
    rcp_bytes_t frames[1];
    size_t      count = rcp_ep_can_encode_frame_response_fragmented(
        4, RCP_EP_CAN_FRAME_CBFF, 0x42, NULL, rx, sizeof(rx), 9, false, 0, 1024, frames);
    rcp_ep_can_frame_format_t fmt;
    bool                       ms;
    uint8_t                    segnum;
    const uint8_t              *payload;
    size_t                      payload_len;
    bool                        timed;
    uint64_t                    ts;
    uint8_t                     txn;

    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_WRONG_BUS,
        rcp_ep_can_decode_frame_response_fragment(frames[0].data, frames[0].len, 99, &fmt, &ms,
                                                    &segnum, &payload, &payload_len, &timed, &ts,
                                                    &txn));

    rcp_bytes_free(&frames[0]);
}

static void test_reassembled_decode_rejects_short_frame(void)
{
    uint8_t                 too_short[3] = {0};
    uint32_t                 id = 0;
    rcp_ep_can_xl_header_t   xl_hdr;
    const uint8_t             *out_rx = NULL;
    size_t                     out_rx_len = 0;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_SHORT_FRAME,
        rcp_ep_can_decode_reassembled_frame_response(too_short, sizeof(too_short),
                                                       RCP_EP_CAN_FRAME_CBFF, &id, &xl_hdr,
                                                       &out_rx, &out_rx_len));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_frame_format_valid_bounds);
    RUN_TEST(test_frame_format_is_xl);
    RUN_TEST(test_frame_format_id_width);

    RUN_TEST(test_arbitration_id_valid_base_11);
    RUN_TEST(test_arbitration_id_valid_extended_29);
    RUN_TEST(test_arbitration_id_valid_xl_uses_base_11);
    RUN_TEST(test_arbitration_id_valid_false_for_invalid_format);

    RUN_TEST(test_frame_format_max_data_len);

    RUN_TEST(test_xl_filter_index_valid_bounds);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_true_hw_configured_any_writer);

    RUN_TEST(test_set_arbitration_timing_rejects_unauthorized);
    RUN_TEST(test_set_arbitration_timing_applies_when_authorized);
    RUN_TEST(test_set_fd_data_timing_applies_when_authorized);
    RUN_TEST(test_set_xl_data_timing_applies_when_authorized);
    RUN_TEST(test_set_delay_compensation_applies_when_authorized);
    RUN_TEST(test_set_delay_compensation_rejects_unauthorized);
    RUN_TEST(test_set_exec_delay_clk_divider_applies_when_authorized);
    RUN_TEST(test_set_exec_delay_clk_divider_rejects_unauthorized);
    RUN_TEST(test_set_xl_filter_applies_when_authorized);
    RUN_TEST(test_set_xl_filter_rejects_bad_index);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_frame_request_round_trip_classical);
    RUN_TEST(test_frame_request_round_trip_xl);
    RUN_TEST(test_frame_request_encode_rejects_bad_frame_format);
    RUN_TEST(test_frame_request_encode_rejects_bad_arbitration_id);
    RUN_TEST(test_frame_request_encode_rejects_data_too_long);
    RUN_TEST(test_frame_request_encode_rejects_missing_xl_header);
    RUN_TEST(test_frame_request_encode_rejects_unexpected_xl_header);
    RUN_TEST(test_frame_request_decode_rejects_wrong_bus);
    RUN_TEST(test_frame_request_decode_rejects_wrong_op);
    RUN_TEST(test_frame_request_decode_rejects_bad_msg_type);
    RUN_TEST(test_frame_request_decode_rejects_short_frame);
    RUN_TEST(test_frame_request_decode_rejects_bad_frame_format);
    RUN_TEST(test_frame_request_decode_rejects_short_xl_prefix);

    RUN_TEST(test_frame_response_round_trip_untimed);
    RUN_TEST(test_frame_response_round_trip_timed_xl);
    RUN_TEST(test_frame_response_decode_rejects_wrong_bus);
    RUN_TEST(test_frame_response_decode_rejects_short_frame);
    RUN_TEST(test_frame_response_encode_rejects_bad_frame_format);

    RUN_TEST(test_fragment_count_one_when_fits_in_one_fragment);
    RUN_TEST(test_fragment_count_zero_for_bad_preconditions);
    RUN_TEST(test_fragment_worst_case_can_xl_response_round_trip);
    RUN_TEST(test_fragment_response_unfragmented_matches_single_frame_path);
    RUN_TEST(test_fragment_encode_rejects_bad_preconditions);
    RUN_TEST(test_fragment_decode_fragment_rejects_wrong_bus);
    RUN_TEST(test_reassembled_decode_rejects_short_frame);

    return UNITY_END();
}
