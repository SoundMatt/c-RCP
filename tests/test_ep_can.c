/* SPDX-License-Identifier: MPL-2.0 */
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
//cfusa:test REQ-CANEP-023
//cfusa:test REQ-CANEP-024
//cfusa:test REQ-CANEP-025
//cfusa:test REQ-CANEP-026
//cfusa:test REQ-CANEP-027
//cfusa:test REQ-CANEP-030
/* [c-RCP-18-tracker] issue #533 REQ-CANEP-* atomicity audit (2026-08-18):
 * ids split out of the ones above now also carry their own per-test
 * cfusa "test" tag directly above the specific test function that
 * proves them, per CONTRIBUTING.md's "Writing a requirement"; this
 * file-header block exists only so this file, taken as a whole, still
 * satisfies cfusa's own file-level annotation gate. */
//cfusa:test REQ-CANEP-033
//cfusa:test REQ-CANEP-034
//cfusa:test REQ-CANEP-035
//cfusa:test REQ-CANEP-036
//cfusa:test REQ-CANEP-038
//cfusa:test REQ-CANEP-039
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/alloc.h>
#include <rcp/avtp.h>
#include <rcp/ep_can.h>
#include <rcp/fragment.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) { rcp_alloc_reset_hooks(); } /* never leak a fault-injection hook across tests */

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
    TEST_ASSERT_FALSE(rcp_ep_can_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_can_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_can_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_can_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
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
    writer.via_owning_stream = true;
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
    writer.via_owning_stream = true;
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
    writer.via_owning_stream = true;
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
    writer.via_owning_stream = true;

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
    writer.via_owning_stream = true;

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
    writer.via_owning_stream = true;
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
        RCP_EP_CAN_ERR_BAD_EVT,  RCP_EP_CAN_ERR_BAD_ARBITRATION_ID,
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

//cfusa:test REQ-CANEP-033
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

//cfusa:test REQ-CANEP-033
static void test_frame_request_round_trip_xl(void)
{
    /* NOT RCP_EP_CAN_XL_MAX_DATA_LEN (2048): since the TC18 conformance
     * fix (see CHANGELOG.md), acf_msg_length is a real 9-bit quadlet
     * count (RCP_ACF_ABB_MAX_PAYLOAD, acf.h), and a full 2048-byte CAN XL
     * new-payload frame -- plus this module's own 4-byte arbitration-id
     * and 6-byte SDT/VCID/AF prefix -- no longer fits in a single
     * unfragmented ACF_ABB message. rcp_ep_can_encode_frame_request() has
     * no fragmented counterpart (only
     * rcp_ep_can_encode_frame_response_fragmented() exists, for the
     * response direction -- see
     * test_fragment_worst_case_can_xl_response_round_trip below for that
     * path exercised at the real 2048-byte worst case, unaffected by this
     * bound since its max_fragment_payload of 1024 is well under it);
     * a fragmented *request* path for the worst-case CAN XL write is
     * tracked as a follow-up, not implemented by this fix. 246 bytes
     * (well within RCP_ACF_ABB_MAX_PAYLOAD, and chosen so
     * 4+6+246=256 lands exactly on a quadlet boundary with no pad) is
     * enough to exercise this round trip's actual behavior. */
    uint8_t                tx[246];
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

//cfusa:test REQ-CANEP-016
static void test_frame_request_encode_rejects_bad_frame_format(void)
{
    rcp_bytes_t frame =
        rcp_ep_can_encode_frame_request(1, (rcp_ep_can_frame_format_t)7, 0, NULL, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-CANEP-016
static void test_frame_request_encode_rejects_bad_arbitration_id(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(1, RCP_EP_CAN_FRAME_CBFF, 0x800u, NULL,
                                                          NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-CANEP-016
static void test_frame_request_encode_rejects_data_too_long(void)
{
    uint8_t tx[9] = {0};
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(1, RCP_EP_CAN_FRAME_CBFF, 0, NULL, tx,
                                                          sizeof(tx), 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-CANEP-016
static void test_frame_request_encode_rejects_missing_xl_header(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(
        1, RCP_EP_CAN_FRAME_XL_CLASSICAL_PL, 0, NULL, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-CANEP-016
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

/* TC18 §13.7.11.3 Figure 39: FrameFormat is the payload's leading
 * quadlet's top 3 bits, not evt[2:0] -- a request with evt[2:0] = 7
 * (reserved in CAN's Table 30 Row-2) is now rejected for its evt value,
 * before frame_format is even inspected. */
static void test_frame_request_decode_rejects_bad_evt(void)
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
    hdr.evt         = 7; /* reserved in CAN's Table 30 Row-2 */
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_EVT,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

/* Payload's leading quadlet's top 3 bits are 0b111 (7), Table 54's own
 * second reserved code -- evt[2:0] is a plain 0b000, so this exercises
 * frame_format validation specifically, not the evt check above. */
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
    uint8_t                      payload[4] = {0xE0, 0, 0, 0}; /* top 3 bits = 111b = 7 */

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_FRAME_FORMAT,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

/* An 11-bit-width format (CBFF) whose leading quadlet carries a
 * arbitration_id value outside the low 11 bits -- TC18 §13.7.11.3's own
 * "shall be right aligned" rule, violated here on purpose. */
static void test_frame_request_decode_rejects_bad_arbitration_id(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_can_frame_format_t   format;
    uint32_t                    id;
    rcp_ep_can_xl_header_t      xl_hdr;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;
    /* frame_format = CBFF (000b) in the top 3 bits; bit 3 (the first bit
     * of the 29-bit id field's own upper, must-be-zero-for-base-11 range)
     * set to 1 -- id = 0x08000000, far outside CBFF's 0x7FF ceiling. */
    uint8_t                      payload[4] = {0x08, 0, 0, 0};

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_ARBITRATION_ID,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

/* Same defect, response side (rcp_ep_can_decode_frame_response() -- issue
 * #520 category 2): its own arbitration-id validation is a separate call
 * site from the request decoder's above, previously unexercised. */
static void test_frame_response_decode_rejects_bad_arbitration_id(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_can_frame_format_t   format;
    uint32_t                    id;
    rcp_ep_can_xl_header_t      xl_hdr;
    const uint8_t               *out_rx;
    size_t                       out_rx_len;
    bool                         timed = true;
    uint64_t                     ts = 1;
    uint8_t                      txn;
    uint8_t                      payload[4] = {0x08, 0, 0, 0}; /* same CBFF/oversized-id trick */

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.rsp         = 1;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_ARBITRATION_ID,
        rcp_ep_can_decode_frame_response(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_rx,
                                          &out_rx_len, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

/* frame_format = XL_NEW_PL (leading quadlet's top 3 bits = 101b = 5) but
 * payload is only the 4-byte leading quadlet, short of the 6-byte
 * SDT/VCID/AF prefix an XL frame requires. */
static void test_frame_request_decode_rejects_short_xl_prefix(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_can_frame_format_t   format;
    uint32_t                    id;
    rcp_ep_can_xl_header_t      xl_hdr;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;
    /* top 3 bits = 101b (XL_NEW_PL = 5), remaining 29 bits (id) = 0. */
    uint8_t                      payload[4] = {(uint8_t)(RCP_EP_CAN_FRAME_XL_NEW_PL << 5), 0, 0, 0};

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_SHORT_FRAME,
        rcp_ep_can_decode_frame_request(frame.data, frame.len, 4, &format, &id, &xl_hdr, &out_tx,
                                         &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

/* Golden vector, independently hand-computed against TC18 §13.7.11.3
 * Figure 39 -- not re-derived from rcp_ep_can_encode_frame_request()
 * itself, which is what this test exists to check. frame_format = CEFF
 * (1) in the top 3 bits (0b001), arbitration_id = 0x1ABCDEF (extended-29,
 * fits in the low 29 bits) -- combined leading quadlet =
 * (1 << 29) | 0x1ABCDEF = 0x21ABCDEF. */
//cfusa:test REQ-CANEP-039
static void test_frame_request_golden_leading_quadlet_bit_packing(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_request(
        4, RCP_EP_CAN_FRAME_CEFF, 0x01ABCDEFu, NULL, NULL, 0, 3);
    const uint8_t *payload = frame.data + RCP_ACF_ABB_HEADER_LEN;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN + 4, frame.len);
    TEST_ASSERT_EQUAL_HEX8(0x21u, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0xABu, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCDu, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, payload[3]);

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

//cfusa:test REQ-CANEP-034
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

/* Issue #521: rcp_ep_can_encode_frame_response_fragmented()'s own
 * fragment-plan array is a fixed RCP_EP_CAN_MAX_FRAGMENT_SEGMENTS-entry
 * stack array, not heap-allocated -- a max_fragment_payload small
 * enough to need more segments than that must be rejected (0) rather
 * than silently truncated or overrun. Worst-case combined payload
 * (RCP_EP_CAN_XL_MAX_ENCODED_LEN = 2058 octets) at max_fragment_payload
 * = 8 needs ceil(2058/8) = 258 segments, one more than the 256-entry
 * ceiling. */
//cfusa:test REQ-CANEP-034
static void test_fragment_count_zero_when_segment_count_exceeds_max_fragment_segments(void)
{
    rcp_ep_can_xl_header_t xl_hdr = {0};
    size_t                 count  = rcp_ep_can_frame_response_fragment_count(
        RCP_EP_CAN_FRAME_XL_NEW_PL, 0x123, &xl_hdr, RCP_EP_CAN_XL_MAX_DATA_LEN, 8);

    TEST_ASSERT_EQUAL_UINT(0, count);
}

//cfusa:test REQ-CANEP-023
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
//cfusa:test REQ-CANEP-024
//cfusa:test REQ-CANEP-036
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
        bool                         ms;
        uint8_t                      segnum;
        const uint8_t                *payload;
        size_t                        payload_len;
        bool                          timed;
        uint64_t                      ts;
        uint8_t                       txn;
        rcp_fragment_reasm_result_t   rc;

        TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
            rcp_ep_can_decode_frame_response_fragment(frames[i].data, frames[i].len, 7,
                                                        &ms, &segnum, &payload, &payload_len,
                                                        &timed, &ts, &txn));
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
            rcp_ep_can_frame_format_t out_format;
            uint32_t       out_id = 0;
            const uint8_t *out_rx = NULL;
            size_t         out_rx_len = 0;

            TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
                rcp_ep_can_decode_reassembled_frame_response(
                    reassembled, reassembled_len, &out_format, &out_id,
                    &xl_hdr_out, &out_rx, &out_rx_len));

            TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_XL_NEW_PL, out_format);
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

/* A timed fragmented response (issue #520 category 2):
 * rcp_ep_can_encode_frame_response_fragmented()'s `timed` branch (each
 * segment encoded as ACF_GBB, not ACF_ABB) and
 * rcp_ep_can_decode_frame_response_fragment()'s matching GBB decode path
 * were both entirely unexercised -- the worst-case round-trip test above
 * only ever passes timed=false. Small classical (CBFF) payload,
 * deliberately tiny max_fragment_payload to force multiple real
 * fragments cheaply rather than reusing the XL worst-case's own size. */
//cfusa:test REQ-CANEP-024
static void test_fragment_timed_response_round_trip(void)
{
    uint8_t      rx[8]; /* RCP_EP_CAN_CLASSICAL_MAX_DATA_LEN -- CBFF's own data-length ceiling */
    size_t       i;
    size_t       max_fragment_payload = 5; /* combined = 4 + 8 = 12 -> ceil(12/5) = 3 fragments */
    size_t       count;
    rcp_bytes_t  frames[4];
    rcp_fragment_reassembler_t reasm;
    size_t       combined_len = 4u + sizeof(rx);

    for (i = 0; i < sizeof(rx); i++) rx[i] = (uint8_t)(i + 1);

    count = rcp_ep_can_frame_response_fragment_count(RCP_EP_CAN_FRAME_CBFF, 0x10, NULL,
                                                       sizeof(rx), max_fragment_payload);
    TEST_ASSERT_EQUAL_UINT(3, count);

    count = rcp_ep_can_encode_frame_response_fragmented(
        7, RCP_EP_CAN_FRAME_CBFF, 0x10, NULL, rx, sizeof(rx), 9, true, 99999,
        max_fragment_payload, frames);
    TEST_ASSERT_EQUAL_UINT(3, count);

    rcp_fragment_reassembler_init(&reasm, combined_len);
    for (i = 0; i < count; i++) {
        bool                         ms;
        uint8_t                      segnum;
        const uint8_t                *payload;
        size_t                        payload_len;
        bool                          timed = false;
        uint64_t                      ts = 0;
        uint8_t                       txn = 0;
        rcp_fragment_reasm_result_t   rc;

        TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
            rcp_ep_can_decode_frame_response_fragment(frames[i].data, frames[i].len, 7, &ms,
                                                        &segnum, &payload, &payload_len, &timed,
                                                        &ts, &txn));
        TEST_ASSERT_EQUAL_UINT8(9, txn);
        TEST_ASSERT_TRUE(timed);
        TEST_ASSERT_EQUAL_UINT64(99999, ts);

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
    }

    rcp_fragment_reassembler_destroy(&reasm);
    for (i = 0; i < count; i++) rcp_bytes_free(&frames[i]);
}

/* ── rcp_ep_can_encode_frame_response_fragmented()'s allocation-failure
 * cleanup path (issue #520 category 2, updated for issue #521). As of
 * issue #521, build_payload()'s combined buffer and the segs plan array
 * are both fixed-capacity stack arrays, not heap-allocated (see
 * RCP_EP_CAN_MAX_FRAGMENT_SEGMENTS's own doc comment, ep_can.h) -- the
 * only remaining rcp_malloc() calls in this path are one per segment
 * inside rcp_acf_encode_gbb()/_abb(), so a call-counting fault-injection
 * hook (alloc.h's own harness, same pattern as shmem.c's
 * counting_calloc) can fail exactly the Nth segment's own encode. */

static int g_can_malloc_call_count;
static int g_can_malloc_fail_at_call;

static void *counting_malloc(size_t size)
{
    g_can_malloc_call_count++;
    if (g_can_malloc_call_count == g_can_malloc_fail_at_call) return NULL;
    return malloc(size);
}

static void reset_counting_malloc(int fail_at_call)
{
    rcp_alloc_hooks_t hooks = {0};
    g_can_malloc_call_count    = 0;
    g_can_malloc_fail_at_call  = fail_at_call;
    hooks.malloc_fn = counting_malloc;
    rcp_alloc_set_hooks(&hooks);
}

/* Fails the *second* segment's own rcp_acf_encode_gbb()/rcp_malloc() call
 * (not the first), so the failure path's own "free every frame already
 * encoded before this one" loop runs with real content (i == 1, not the
 * vacuous i == 0 case), exercising that loop's own body, not just its
 * zero-iteration form. */
//cfusa:test REQ-CANEP-024
static void test_fragmented_encode_frees_prior_frames_when_a_later_segment_fails(void)
{
    uint8_t     rx[8];
    size_t      i;
    rcp_bytes_t frames[4];
    size_t      count;

    for (i = 0; i < sizeof(rx); i++) rx[i] = (uint8_t)i;

    /* segment[0] encode = call 1, segment[1] encode = call 2 -- issue
     * #521 removed the two heap allocations (combined, segs) that used
     * to precede these. */
    reset_counting_malloc(2);

    count = rcp_ep_can_encode_frame_response_fragmented(
        7, RCP_EP_CAN_FRAME_CBFF, 0x10, NULL, rx, sizeof(rx), 1, true, 42, 5, frames);
    TEST_ASSERT_EQUAL_UINT(0, count);
}

//cfusa:test REQ-CANEP-035
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

/* ── Fragmented request (issue #611, fragment.h) ─────────────────────────────
 *
 * The request-side counterpart of "Fragmented response" above -- same
 * pure-encode-and-manually-reassemble shape (no mock.c/dispatch
 * involvement; that composition is exercised separately in
 * tests/test_tc18_gaps_e2e.c), just for rcp_ep_can_frame_request_fragment_
 * count()/rcp_ep_can_encode_frame_request_fragmented() and a request's own
 * tx_data/no-timed-variant shape. */

//cfusa:test REQ-CANEP-041
static void test_fragment_request_count_zero_for_bad_preconditions(void)
{
    /* Invalid frame_format -> encode_preconditions_ok() fails -> 0, same
     * as rcp_ep_can_frame_response_fragment_count()'s own identical
     * branch (REQ-CANEP-023). */
    size_t count = rcp_ep_can_frame_request_fragment_count(
        (rcp_ep_can_frame_format_t)7, 0x10, NULL, 3, 100);
    TEST_ASSERT_EQUAL_UINT(0, count);
}

/* Same RCP_EP_CAN_MAX_FRAGMENT_SEGMENTS ceiling
 * rcp_ep_can_frame_response_fragment_count()'s own identical test
 * (test_fragment_count_zero_when_segment_count_exceeds_max_fragment_segments)
 * already proves for the response side -- worst-case combined payload
 * (RCP_EP_CAN_XL_MAX_ENCODED_LEN = 2058 octets) at max_fragment_payload =
 * 8 needs ceil(2058/8) = 258 segments, one more than the 256-entry
 * ceiling. */
//cfusa:test REQ-CANEP-041
static void test_fragment_request_count_zero_when_segment_count_exceeds_max_fragment_segments(void)
{
    rcp_ep_can_xl_header_t xl_hdr = {0};
    size_t                 count  = rcp_ep_can_frame_request_fragment_count(
        RCP_EP_CAN_FRAME_XL_NEW_PL, 0x123, &xl_hdr, RCP_EP_CAN_XL_MAX_DATA_LEN, 8);

    TEST_ASSERT_EQUAL_UINT(0, count);
}

/* The literal worst-case scenario issue #611 names: a full
 * RCP_EP_CAN_XL_MAX_DATA_LEN (2048)-byte CAN XL write request, whose
 * combined prefix-then-data payload (RCP_EP_CAN_XL_MAX_ENCODED_LEN, 2058
 * octets) does not fit within a single ACF message
 * (RCP_ACF_ABB_MAX_PAYLOAD, 2036 octets) -- the request-side mirror of
 * test_fragment_worst_case_can_xl_response_round_trip() above, adapted for
 * a request's own tx_data naming and ACF_ABB-only (never GBB) encoding.
 * Each fragment is decoded with acf.c's own rcp_acf_decode_abb() directly
 * (this module deliberately has no rcp_ep_can_decode_frame_request_
 * fragment() of its own -- see ep_can.h's "Fragmented request" section:
 * a request fragment's ms/segment_num/payload are plain ACF_ABB fields,
 * nothing CAN-specific to peel off first), reassembled via fragment.h
 * directly, then decoded back with rcp_ep_can_decode_reassembled_frame_
 * response() -- reused verbatim for the request direction, since it
 * inspects only the reassembled bytes themselves, never op/rsp. */
//cfusa:test REQ-CANEP-041
//cfusa:test REQ-CANEP-042
static void test_fragment_worst_case_can_xl_request_round_trip(void)
{
    uint8_t                     tx[RCP_EP_CAN_XL_MAX_DATA_LEN];
    rcp_ep_can_xl_header_t      xl_hdr_in = {0};
    rcp_ep_can_xl_header_t      xl_hdr_out;
    size_t                      i;
    size_t                      max_fragment_payload = RCP_ACF_ABB_MAX_PAYLOAD;
    size_t                      count;
    rcp_bytes_t                 frames[4];
    rcp_fragment_reassembler_t  reasm;
    size_t                      combined_len = 4u + 6u + RCP_EP_CAN_XL_MAX_DATA_LEN;

    for (i = 0; i < sizeof(tx); i++) tx[i] = (uint8_t)(i * 3 + 7);
    xl_hdr_in.sdt  = 0x5;
    xl_hdr_in.vcid = 0x9;
    xl_hdr_in.af   = 0xDEADBEEFu;

    count = rcp_ep_can_frame_request_fragment_count(
        RCP_EP_CAN_FRAME_XL_NEW_PL, 0x123, &xl_hdr_in, sizeof(tx), max_fragment_payload);
    TEST_ASSERT_EQUAL_UINT(2, count); /* ceil(2058 / 2036) = 2 */
    TEST_ASSERT_TRUE(count <= (sizeof(frames) / sizeof(frames[0])));

    count = rcp_ep_can_encode_frame_request_fragmented(
        7, RCP_EP_CAN_FRAME_XL_NEW_PL, 0x123, &xl_hdr_in, tx, sizeof(tx), 55,
        max_fragment_payload, frames);
    TEST_ASSERT_EQUAL_UINT(2, count);

    for (i = 0; i < count; i++) TEST_ASSERT_NOT_NULL(frames[i].data);

    rcp_fragment_reassembler_init(&reasm, combined_len);
    for (i = 0; i < count; i++) {
        rcp_acf_byte_message_info_t hdr;
        const uint8_t                *payload;
        size_t                         payload_len;
        rcp_fragment_reasm_result_t   rc;

        TEST_ASSERT_EQUAL(RCP_ACF_OK,
            rcp_acf_decode_abb(frames[i].data, frames[i].len, &hdr, &payload, &payload_len));
        TEST_ASSERT_EQUAL_UINT8(7, hdr.byte_bus_id);
        TEST_ASSERT_EQUAL(RCP_ACF_OP_WRITE, (rcp_acf_op_t)hdr.op);
        TEST_ASSERT_EQUAL_UINT8(55, hdr.transaction_num);

        rc = rcp_fragment_reassembler_feed(&reasm, hdr.ms != 0u, hdr.read_size_or_segment_num,
                                            payload, payload_len);
        if (i + 1 < count) {
            TEST_ASSERT_TRUE(hdr.ms != 0u);
            TEST_ASSERT_EQUAL_UINT16((uint16_t)i, hdr.read_size_or_segment_num);
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);
        } else {
            TEST_ASSERT_TRUE(hdr.ms == 0u);
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);
        }
    }

    {
        const uint8_t *reassembled;
        size_t         reassembled_len;

        rcp_fragment_reassembler_get(&reasm, &reassembled, &reassembled_len);
        TEST_ASSERT_EQUAL_UINT(combined_len, reassembled_len);

        {
            rcp_ep_can_frame_format_t out_format;
            uint32_t       out_id = 0;
            const uint8_t *out_tx = NULL;
            size_t         out_tx_len = 0;

            TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
                rcp_ep_can_decode_reassembled_frame_response(
                    reassembled, reassembled_len, &out_format, &out_id,
                    &xl_hdr_out, &out_tx, &out_tx_len));

            TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_XL_NEW_PL, out_format);
            TEST_ASSERT_EQUAL_UINT32(0x123, out_id);
            TEST_ASSERT_EQUAL_UINT8(0x5, xl_hdr_out.sdt);
            TEST_ASSERT_EQUAL_UINT8(0x9, xl_hdr_out.vcid);
            TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, xl_hdr_out.af);
            TEST_ASSERT_EQUAL_UINT(sizeof(tx), out_tx_len);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
        }
    }

    rcp_fragment_reassembler_destroy(&reasm);
    for (i = 0; i < count; i++) rcp_bytes_free(&frames[i]);
}

//cfusa:test REQ-CANEP-042
static void test_fragment_request_unfragmented_matches_single_frame_path(void)
{
    /* When the combined payload already fits in one fragment, the
     * fragmented encoder must produce exactly what the plain,
     * unfragmented encoder would have -- same contract
     * test_fragment_response_unfragmented_matches_single_frame_path()
     * above already proves for the response side (REQ-CANEP-035),
     * mirrored here for the request side under REQ-CANEP-042 (this
     * requirement was not split the way the response side's was --
     * see REQ-CANEP-042's own .fusa-reqs.json entry). */
    uint8_t      tx[3] = {0xAA, 0xBB, 0xCC};
    rcp_bytes_t  plain;
    rcp_bytes_t  fragmented[1];
    size_t       count;

    plain = rcp_ep_can_encode_frame_request(4, RCP_EP_CAN_FRAME_CBFF, 0x42, NULL, tx,
                                             sizeof(tx), 9);
    TEST_ASSERT_NOT_NULL(plain.data);

    count = rcp_ep_can_encode_frame_request_fragmented(
        4, RCP_EP_CAN_FRAME_CBFF, 0x42, NULL, tx, sizeof(tx), 9, 1024, fragmented);
    TEST_ASSERT_EQUAL_UINT(1, count);

    TEST_ASSERT_EQUAL_UINT(plain.len, fragmented[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plain.data, fragmented[0].data, plain.len);

    rcp_bytes_free(&plain);
    rcp_bytes_free(&fragmented[0]);
}

//cfusa:test REQ-CANEP-042
static void test_fragment_request_encode_rejects_bad_preconditions(void)
{
    rcp_bytes_t frames[4];
    size_t      count = rcp_ep_can_encode_frame_request_fragmented(
        4, (rcp_ep_can_frame_format_t)7, 0x42, NULL, NULL, 0, 9, 1024, frames);
    TEST_ASSERT_EQUAL_UINT(0, count);
}

static void test_fragment_decode_fragment_rejects_wrong_bus(void)
{
    uint8_t     rx[3] = {1, 2, 3};
    rcp_bytes_t frames[1];
    size_t      count = rcp_ep_can_encode_frame_response_fragmented(
        4, RCP_EP_CAN_FRAME_CBFF, 0x42, NULL, rx, sizeof(rx), 9, false, 0, 1024, frames);
    bool                       ms;
    uint8_t                    segnum;
    const uint8_t              *payload;
    size_t                      payload_len;
    bool                        timed;
    uint64_t                    ts;
    uint8_t                     txn;

    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_WRONG_BUS,
        rcp_ep_can_decode_frame_response_fragment(frames[0].data, frames[0].len, 99, &ms,
                                                    &segnum, &payload, &payload_len, &timed, &ts,
                                                    &txn));

    rcp_bytes_free(&frames[0]);
}

//cfusa:test REQ-CANEP-027
static void test_reassembled_decode_rejects_short_frame(void)
{
    uint8_t                    too_short[3] = {0};
    rcp_ep_can_frame_format_t  format;
    uint32_t                   id = 0;
    rcp_ep_can_xl_header_t     xl_hdr;
    const uint8_t              *out_rx = NULL;
    size_t                      out_rx_len = 0;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_SHORT_FRAME,
        rcp_ep_can_decode_reassembled_frame_response(too_short, sizeof(too_short),
                                                       &format, &id, &xl_hdr,
                                                       &out_rx, &out_rx_len));
}

/* Third (and last) rcp_ep_can_arbitration_id_valid() call site (issue
 * #520 category 2) -- a separate check from both the plain-response and
 * request decoders' own, operating directly on a post-fragmentation
 * reassembled buffer rather than an ACF-wrapped frame. Same
 * CBFF/oversized-id trick as the other two sites. */
//cfusa:test REQ-CANEP-027
static void test_reassembled_decode_rejects_bad_arbitration_id(void)
{
    uint8_t                    reassembled[4] = {0x08, 0, 0, 0};
    rcp_ep_can_frame_format_t  format;
    uint32_t                   id = 0;
    rcp_ep_can_xl_header_t     xl_hdr;
    const uint8_t              *out_rx = NULL;
    size_t                      out_rx_len = 0;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_ARBITRATION_ID,
        rcp_ep_can_decode_reassembled_frame_response(reassembled, sizeof(reassembled),
                                                       &format, &id, &xl_hdr,
                                                       &out_rx, &out_rx_len));
}

/* [c-RCP-18-tracker] issue #533 REQ-CANEP-* atomicity audit: before this
 * batch, RCP_EP_CAN_ERR_BAD_FRAME_FORMAT had no dedicated test of its
 * own for this specific function -- SHORT_FRAME and BAD_ARBITRATION_ID
 * both did (above), but the third leg of REQ-CANEP-027's own
 * reject-taxonomy was only ever exercised incidentally via other
 * functions' own tests. Top 3 bits = 110b (6), TC18 Table 57's first
 * reserved FrameFormat code. */
//cfusa:test REQ-CANEP-027
static void test_reassembled_decode_rejects_bad_frame_format(void)
{
    uint8_t                    reserved_code[4] = {0xC0u, 0, 0, 0};
    rcp_ep_can_frame_format_t  format;
    uint32_t                   id = 0;
    rcp_ep_can_xl_header_t     xl_hdr;
    const uint8_t              *out_rx = NULL;
    size_t                      out_rx_len = 0;

    TEST_ASSERT_EQUAL(RCP_EP_CAN_ERR_BAD_FRAME_FORMAT,
        rcp_ep_can_decode_reassembled_frame_response(reserved_code, sizeof(reserved_code),
                                                       &format, &id, &xl_hdr,
                                                       &out_rx, &out_rx_len));
}

/* [c-RCP-18-tracker] issue #533 REQ-CANEP-* atomicity audit: REQ-CANEP-036
 * split out of REQ-CANEP-027's own prior text as this function's own
 * successful-populate contract. Before this batch, the only coverage of
 * this specific outcome was folded into the multi-fragment round-trip
 * tests above (test_fragment_worst_case_can_xl_response_round_trip/
 * test_fragment_timed_response_round_trip), which exercise this
 * function only as the last step of a full fragment/reassemble
 * pipeline -- a shared assertion that happens to touch this function's
 * own contract in passing, not a focused proof of it. This test calls
 * rcp_ep_can_decode_reassembled_frame_response() directly against a
 * combined payload built without ever going through fragment.h, proving
 * this function's own contract independent of REQ-CANEP-024/-035's own
 * fragmentation logic. */
//cfusa:test REQ-CANEP-036
static void test_reassembled_decode_round_trip_recovers_fields(void)
{
    uint8_t                     rx[3] = {0x11, 0x22, 0x33};
    rcp_ep_can_xl_header_t      xl_hdr_in = {0};
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *combined_payload = NULL;
    size_t                       combined_len = 0;
    rcp_ep_can_frame_format_t    out_format;
    uint32_t                     out_id = 0;
    rcp_ep_can_xl_header_t       out_xl_hdr;
    const uint8_t                *out_rx = NULL;
    size_t                        out_rx_len = 0;

    xl_hdr_in.sdt  = 0x5u;
    xl_hdr_in.vcid = 0x9u;
    xl_hdr_in.af   = 0xDEADBEEFu;

    /* An unfragmented ACF_ABB response's own payload IS already the
     * "combined prefix-then-data payload" rcp_ep_can_decode_reassembled_
     * frame_response() expects -- no fragment.h reassembly required to
     * exercise this function's own contract. */
    frame = rcp_ep_can_encode_frame_response(9, RCP_EP_CAN_FRAME_XL_NEW_PL, 0x321u, &xl_hdr_in,
                                              rx, sizeof(rx), 3u, false, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
        rcp_acf_decode_abb(frame.data, frame.len, &hdr, &combined_payload, &combined_len));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
        rcp_ep_can_decode_reassembled_frame_response(combined_payload, combined_len, &out_format,
                                                       &out_id, &out_xl_hdr, &out_rx,
                                                       &out_rx_len));
    TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_XL_NEW_PL, out_format);
    TEST_ASSERT_EQUAL_UINT32(0x321u, out_id);
    TEST_ASSERT_EQUAL_UINT8(0x5u, out_xl_hdr.sdt);
    TEST_ASSERT_EQUAL_UINT8(0x9u, out_xl_hdr.vcid);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, out_xl_hdr.af);
    TEST_ASSERT_EQUAL_UINT(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));

    rcp_bytes_free(&frame);
}

/* ── REQ-CANEP-030: CAN XL physical-layer provisioning ───────────────────── */

//cfusa:test REQ-CANEP-038
static void test_xl_pl_non_xl_frame_always_matches(void)
{
    TEST_ASSERT_TRUE(rcp_ep_can_xl_frame_matches_provisioned_pl(true, RCP_EP_CAN_FRAME_CBFF));
    TEST_ASSERT_TRUE(rcp_ep_can_xl_frame_matches_provisioned_pl(false, RCP_EP_CAN_FRAME_CBFF));
    TEST_ASSERT_TRUE(rcp_ep_can_xl_frame_matches_provisioned_pl(true, RCP_EP_CAN_FRAME_CEFF));
    TEST_ASSERT_TRUE(rcp_ep_can_xl_frame_matches_provisioned_pl(false, RCP_EP_CAN_FRAME_FBFF));
    TEST_ASSERT_TRUE(rcp_ep_can_xl_frame_matches_provisioned_pl(true, RCP_EP_CAN_FRAME_FEFF));
}

//cfusa:test REQ-CANEP-038
static void test_xl_pl_new_pl_provisioned_matches_only_new_pl_frame(void)
{
    TEST_ASSERT_TRUE(rcp_ep_can_xl_frame_matches_provisioned_pl(true, RCP_EP_CAN_FRAME_XL_NEW_PL));
    TEST_ASSERT_FALSE(
        rcp_ep_can_xl_frame_matches_provisioned_pl(true, RCP_EP_CAN_FRAME_XL_CLASSICAL_PL));
}

//cfusa:test REQ-CANEP-038
static void test_xl_pl_classical_pl_provisioned_matches_only_classical_pl_frame(void)
{
    TEST_ASSERT_TRUE(
        rcp_ep_can_xl_frame_matches_provisioned_pl(false, RCP_EP_CAN_FRAME_XL_CLASSICAL_PL));
    TEST_ASSERT_FALSE(
        rcp_ep_can_xl_frame_matches_provisioned_pl(false, RCP_EP_CAN_FRAME_XL_NEW_PL));
}

//cfusa:test REQ-CANEP-030
static void test_xl_pl_set_provisioned_requires_authorization(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t  none = {0};
    rcp_lifecycle_writer_ctx_t  owning;

    memset(&owning, 0, sizeof(owning));
    owning.via_owning_stream = true;

    rcp_ep_can_functional_cfg_init(&cfg);
    TEST_ASSERT_FALSE(cfg.xl_new_pl_provisioned);

    TEST_ASSERT_FALSE(rcp_ep_can_set_xl_new_pl_provisioned(&cfg, true,
                                                            RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_FALSE(cfg.xl_new_pl_provisioned);

    TEST_ASSERT_TRUE(rcp_ep_can_set_xl_new_pl_provisioned(&cfg, true,
                                                           RCP_LIFECYCLE_HW_CONFIGURED, owning));
    TEST_ASSERT_TRUE(cfg.xl_new_pl_provisioned);
}

/* ── rcp_ep_can_apply_reconfig()/rcp_ep_can_reconfig_strerror() ─────────────
 * (issue #520 category 2/1: this whole escape-hatch pair had zero test
 * coverage before this batch -- neither the reconfig write's own three
 * outcomes nor its dispatch-table strerror() had ever been exercised.) */

static void test_apply_reconfig_rejects_short_payload(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    uint8_t                     payload[RCP_EP_CAN_RECONFIG_ADDR_LEN]; /* addr only, no data */

    rcp_ep_can_functional_cfg_init(&cfg);
    memset(payload, 0, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_ERR_SHORT,
                       rcp_ep_can_apply_reconfig(&cfg, payload, sizeof(payload)));
}

static void test_apply_reconfig_rejects_span_past_end_of_block(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    /* start_address = RCP_EP_CAN_EP_FUNC_LEN - 1 (last valid octet), plus
     * 2 data octets -- the span extends 1 octet past the block. */
    uint8_t payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + 2] = {0, 0, 0xAA, 0xBB};

    rcp_ep_can_functional_cfg_init(&cfg);
    payload[0] = (uint8_t)((RCP_EP_CAN_EP_FUNC_LEN - 1) >> 8);
    payload[1] = (uint8_t)(RCP_EP_CAN_EP_FUNC_LEN - 1);

    TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_ERR_OUT_OF_RANGE,
                       rcp_ep_can_apply_reconfig(&cfg, payload, sizeof(payload)));
}

static void test_apply_reconfig_applies_in_range_write(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    uint8_t                     payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + 1];
    uint8_t                     before[RCP_EP_CAN_EP_FUNC_LEN];
    uint8_t                     after[RCP_EP_CAN_EP_FUNC_LEN];

    rcp_ep_can_functional_cfg_init(&cfg);
    rcp_ep_can_render_registers(&cfg, before);

    payload[0] = 0;
    payload[1] = 0;
    payload[2] = 0xFF; /* one octet write at address 0 (can_ep_len, read-only -- ignored) */

    TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_OK,
                       rcp_ep_can_apply_reconfig(&cfg, payload, sizeof(payload)));

    /* can_ep_len (address 0) is documented read-only -- applying a write
     * there must leave the rendered block unchanged, confirming the
     * read-only-octet skip this function's own header comment describes,
     * not merely that RCONFIG_OK was returned. */
    rcp_ep_can_render_registers(&cfg, after);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, after, sizeof(before));
}

static void test_reconfig_strerror_covers_every_code(void)
{
    TEST_ASSERT_EQUAL_STRING("rcp/ep_can: CAN configuration write applied",
                              rcp_ep_can_reconfig_strerror(RCP_EP_CAN_RECONFIG_OK));
    TEST_ASSERT_EQUAL_STRING("rcp/ep_can: CAN configuration write has no address and data",
                              rcp_ep_can_reconfig_strerror(RCP_EP_CAN_RECONFIG_ERR_SHORT));
    TEST_ASSERT_EQUAL_STRING(
        "rcp/ep_can: CAN configuration write extends past the EP_func block",
        rcp_ep_can_reconfig_strerror(RCP_EP_CAN_RECONFIG_ERR_OUT_OF_RANGE));
}

static void test_reconfig_strerror_rejects_out_of_range_code(void)
{
    TEST_ASSERT_EQUAL_STRING("rcp/ep_can: CAN unknown configuration-write error",
                              rcp_ep_can_reconfig_strerror((rcp_ep_can_reconfig_errc_t)99));
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
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);

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
    RUN_TEST(test_frame_request_decode_rejects_bad_evt);
    RUN_TEST(test_frame_request_decode_rejects_bad_frame_format);
    RUN_TEST(test_frame_request_decode_rejects_bad_arbitration_id);
    RUN_TEST(test_frame_response_decode_rejects_bad_arbitration_id);
    RUN_TEST(test_frame_request_golden_leading_quadlet_bit_packing);
    RUN_TEST(test_frame_request_decode_rejects_short_xl_prefix);

    RUN_TEST(test_frame_response_round_trip_untimed);
    RUN_TEST(test_frame_response_round_trip_timed_xl);
    RUN_TEST(test_frame_response_decode_rejects_wrong_bus);
    RUN_TEST(test_frame_response_decode_rejects_short_frame);
    RUN_TEST(test_frame_response_encode_rejects_bad_frame_format);

    RUN_TEST(test_fragment_count_one_when_fits_in_one_fragment);
    RUN_TEST(test_fragment_count_zero_for_bad_preconditions);
    RUN_TEST(test_fragment_count_zero_when_segment_count_exceeds_max_fragment_segments);
    RUN_TEST(test_fragment_worst_case_can_xl_response_round_trip);
    RUN_TEST(test_fragment_timed_response_round_trip);
    RUN_TEST(test_fragmented_encode_frees_prior_frames_when_a_later_segment_fails);
    RUN_TEST(test_fragment_response_unfragmented_matches_single_frame_path);
    RUN_TEST(test_fragment_encode_rejects_bad_preconditions);
    RUN_TEST(test_fragment_request_count_zero_for_bad_preconditions);
    RUN_TEST(test_fragment_request_count_zero_when_segment_count_exceeds_max_fragment_segments);
    RUN_TEST(test_fragment_worst_case_can_xl_request_round_trip);
    RUN_TEST(test_fragment_request_unfragmented_matches_single_frame_path);
    RUN_TEST(test_fragment_request_encode_rejects_bad_preconditions);
    RUN_TEST(test_fragment_decode_fragment_rejects_wrong_bus);
    RUN_TEST(test_reassembled_decode_rejects_short_frame);
    RUN_TEST(test_reassembled_decode_rejects_bad_arbitration_id);
    RUN_TEST(test_reassembled_decode_rejects_bad_frame_format);
    RUN_TEST(test_reassembled_decode_round_trip_recovers_fields);

    RUN_TEST(test_xl_pl_non_xl_frame_always_matches);
    RUN_TEST(test_xl_pl_new_pl_provisioned_matches_only_new_pl_frame);
    RUN_TEST(test_xl_pl_classical_pl_provisioned_matches_only_classical_pl_frame);
    RUN_TEST(test_xl_pl_set_provisioned_requires_authorization);
    RUN_TEST(test_apply_reconfig_rejects_short_payload);
    RUN_TEST(test_apply_reconfig_rejects_span_past_end_of_block);
    RUN_TEST(test_apply_reconfig_applies_in_range_write);
    RUN_TEST(test_reconfig_strerror_covers_every_code);
    RUN_TEST(test_reconfig_strerror_rejects_out_of_range_code);

    return UNITY_END();
}
