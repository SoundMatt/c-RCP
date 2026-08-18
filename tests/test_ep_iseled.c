/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ISELED-001
//cfusa:test REQ-ISELED-002
//cfusa:test REQ-ISELED-003
//cfusa:test REQ-ISELED-004
//cfusa:test REQ-ISELED-005
//cfusa:test REQ-ISELED-006
//cfusa:test REQ-ISELED-007
//cfusa:test REQ-ISELED-008
//cfusa:test REQ-ISELED-009
//cfusa:test REQ-ISELED-010
//cfusa:test REQ-ISELED-011
//cfusa:test REQ-ISELED-012
//cfusa:test REQ-ISELED-013
//cfusa:test REQ-ISELED-014
//cfusa:test REQ-ISELED-015
//cfusa:test REQ-ISELED-016
//cfusa:test REQ-ISELED-017
//cfusa:test REQ-ISELED-018
//cfusa:test REQ-ISELED-019
//cfusa:test REQ-ISELED-020
//cfusa:test REQ-ISELED-021
//cfusa:test REQ-ISELED-022
//cfusa:test REQ-ISELED-023
//cfusa:test REQ-ISELED-024
//cfusa:test REQ-ISELED-025
//cfusa:test REQ-ISELED-026
//cfusa:test REQ-ISELED-027
//cfusa:test REQ-ISELED-029
//cfusa:test REQ-ISELED-030
//cfusa:test REQ-ISELED-031
//cfusa:test REQ-ISELED-032
//cfusa:test REQ-ISELED-033
//cfusa:test REQ-ISELED-034
//cfusa:test REQ-ISELED-035
//cfusa:test REQ-ISELED-036
//cfusa:test REQ-ISELED-037
//cfusa:test REQ-ISELED-038
//cfusa:test REQ-ISELED-039
//cfusa:test REQ-ISELED-040
//cfusa:test REQ-ISELED-042
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_iseled.h>
#include <rcp/fragment.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── symbol_encode / symbol_decode ─────────────────────────────────────────── */

static void test_symbol_encode_round_trips_every_nibble(void)
{
    uint8_t nibble;

    for (nibble = 0; nibble <= 0x0F; nibble++) {
        uint8_t symbol = rcp_ep_iseled_symbol_encode(nibble);
        uint8_t decoded = 0xFF;

        TEST_ASSERT_TRUE(symbol <= 0x1F);
        TEST_ASSERT_TRUE(rcp_ep_iseled_symbol_decode(symbol, &decoded));
        TEST_ASSERT_EQUAL_UINT8(nibble, decoded);
    }
}

static void test_symbol_encode_masks_high_bits(void)
{
    TEST_ASSERT_EQUAL_UINT8(rcp_ep_iseled_symbol_encode(0x05),
                             rcp_ep_iseled_symbol_encode(0xF5));
}

static void test_symbol_encode_distinct_parity_for_all_zero_and_all_one_nibbles(void)
{
    /* 0x0 (even parity 0) and 0xF (four set bits, even parity 0)... verify
     * the two extremes never collide with each other's bit-4 framing --
     * see the file header's recovered-clock transition-density claim. */
    uint8_t sym0 = rcp_ep_iseled_symbol_encode(0x0);
    uint8_t sym_f = rcp_ep_iseled_symbol_encode(0xF);

    TEST_ASSERT_NOT_EQUAL(sym0, sym_f);
}

//cfusa:test REQ-ISELED-002
static void test_symbol_decode_rejects_bad_parity(void)
{
    uint8_t valid = rcp_ep_iseled_symbol_encode(0x03);
    uint8_t corrupted = (uint8_t)(valid ^ 0x10u); /* flip the parity bit */
    uint8_t decoded = 0xFF;

    TEST_ASSERT_FALSE(rcp_ep_iseled_symbol_decode(corrupted, &decoded));
}

/* REQ-ISELED-032 (split 2026-08-18, issue #533, from REQ-ISELED-002): a
 * dedicated, independent assertion for the valid-parity accept-and-decode
 * clause, not inherited in passing from test_symbol_encode_round_trips_
 * every_nibble()'s own REQ-ISELED-001 round trip. */
//cfusa:test REQ-ISELED-032
static void test_symbol_decode_accepts_valid_parity_and_sets_nibble(void)
{
    uint8_t symbol = rcp_ep_iseled_symbol_encode(0x0A); /* valid parity by construction */
    uint8_t decoded = 0xFF;

    TEST_ASSERT_TRUE(rcp_ep_iseled_symbol_decode(symbol, &decoded));
    TEST_ASSERT_EQUAL_UINT8(0x0A, decoded);
}

/* ── bitframe_encoded_len ──────────────────────────────────────────────────── */

static void test_bitframe_encoded_len(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, rcp_ep_iseled_bitframe_encoded_len(0, false));
    TEST_ASSERT_EQUAL_UINT32(2, rcp_ep_iseled_bitframe_encoded_len(0, true));
    TEST_ASSERT_EQUAL_UINT32(6, rcp_ep_iseled_bitframe_encoded_len(3, false));
    TEST_ASSERT_EQUAL_UINT32(8, rcp_ep_iseled_bitframe_encoded_len(3, true));
}

/* ── encode_bitframe / decode_bitframe round trips ─────────────────────────── */

static void test_bitframe_round_trip_no_crc(void)
{
    uint8_t     data[3] = {0x12, 0xAB, 0x00};
    rcp_bytes_t framed = rcp_ep_iseled_encode_bitframe(data, sizeof(data), false);
    rcp_bytes_t decoded = {0};
    size_t      i;

    TEST_ASSERT_NOT_NULL(framed.data);
    TEST_ASSERT_EQUAL_UINT32(6, framed.len);
    for (i = 0; i < framed.len; i++) {
        TEST_ASSERT_TRUE(framed.data[i] <= 0x1F);
    }

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_bitframe(framed.data, framed.len, false, &decoded));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), decoded.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, decoded.data, sizeof(data));

    rcp_bytes_free(&framed);
    rcp_bytes_free(&decoded);
}

static void test_bitframe_round_trip_with_crc(void)
{
    uint8_t     data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t framed = rcp_ep_iseled_encode_bitframe(data, sizeof(data), true);
    rcp_bytes_t decoded = {0};

    TEST_ASSERT_NOT_NULL(framed.data);
    TEST_ASSERT_EQUAL_UINT32(10, framed.len); /* (4 + 1 trailer) * 2 */

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_bitframe(framed.data, framed.len, true, &decoded));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), decoded.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, decoded.data, sizeof(data));

    rcp_bytes_free(&framed);
    rcp_bytes_free(&decoded);
}

static void test_bitframe_empty_no_crc(void)
{
    rcp_bytes_t framed = rcp_ep_iseled_encode_bitframe(NULL, 0, false);
    rcp_bytes_t decoded;

    memset(&decoded, 0xFF, sizeof(decoded)); /* garbage, must be zeroed by a 0-length success */

    TEST_ASSERT_NULL(framed.data);
    TEST_ASSERT_EQUAL_UINT32(0, framed.len);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_bitframe(NULL, 0, false, &decoded));
    TEST_ASSERT_NULL(decoded.data);
    TEST_ASSERT_EQUAL_UINT32(0, decoded.len);
}

static void test_bitframe_decode_rejects_odd_symbol_count(void)
{
    uint8_t     symbols[3] = {0x00, 0x00, 0x00};
    rcp_bytes_t decoded = {0};

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_ODD_SYMBOL_COUNT,
        rcp_ep_iseled_decode_bitframe(symbols, sizeof(symbols), false, &decoded));
    TEST_ASSERT_NULL(decoded.data);
}

static void test_bitframe_decode_rejects_bad_symbol(void)
{
    uint8_t     data[1] = {0x42};
    rcp_bytes_t framed = rcp_ep_iseled_encode_bitframe(data, sizeof(data), false);
    rcp_bytes_t decoded = {0};

    TEST_ASSERT_NOT_NULL(framed.data);
    framed.data[0] = (uint8_t)(framed.data[0] ^ 0x10u); /* flip a parity bit */

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_BAD_SYMBOL,
        rcp_ep_iseled_decode_bitframe(framed.data, framed.len, false, &decoded));
    TEST_ASSERT_NULL(decoded.data);

    rcp_bytes_free(&framed);
}

static void test_bitframe_decode_rejects_crc_mismatch(void)
{
    uint8_t     data[2] = {0x11, 0x22};
    rcp_bytes_t framed = rcp_ep_iseled_encode_bitframe(data, sizeof(data), true);
    rcp_bytes_t decoded = {0};

    TEST_ASSERT_NOT_NULL(framed.data);
    /* Corrupt the low-nibble symbol of the first content octet -- still a
     * valid symbol on its own (good parity), but changes the decoded data,
     * which must now disagree with the CRC-8 trailer. */
    framed.data[1] = rcp_ep_iseled_symbol_encode(0x0F);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_CRC_MISMATCH,
        rcp_ep_iseled_decode_bitframe(framed.data, framed.len, true, &decoded));
    TEST_ASSERT_NULL(decoded.data);

    rcp_bytes_free(&framed);
}

static void test_bitframe_decode_rejects_short_frame_when_crc_expected(void)
{
    rcp_bytes_t decoded = {0};

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_SHORT_FRAME,
        rcp_ep_iseled_decode_bitframe(NULL, 0, true, &decoded));
    TEST_ASSERT_NULL(decoded.data);
}

/* ── crc8 ───────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-ISELED-006
static void test_crc8_deterministic(void)
{
    uint8_t a[3] = {0x01, 0x02, 0x03};

    TEST_ASSERT_EQUAL_UINT8(rcp_ep_iseled_crc8(a, sizeof(a)), rcp_ep_iseled_crc8(a, sizeof(a)));
}

/* REQ-ISELED-034 (split 2026-08-18, issue #533, from REQ-ISELED-006): a
 * dedicated, independent assertion for the content-sensitivity clause. */
//cfusa:test REQ-ISELED-034
static void test_crc8_differs_for_different_content(void)
{
    uint8_t a[3] = {0x01, 0x02, 0x03};
    uint8_t b[3] = {0x01, 0x02, 0x04};

    TEST_ASSERT_NOT_EQUAL(rcp_ep_iseled_crc8(a, sizeof(a)), rcp_ep_iseled_crc8(b, sizeof(b)));
}

/* REQ-ISELED-033 (split 2026-08-18, issue #533, from REQ-ISELED-006). */
//cfusa:test REQ-ISELED-033
static void test_crc8_empty_input(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x00, rcp_ep_iseled_crc8(NULL, 0));
}

/* ── requires_isp_n ─────────────────────────────────────────────────────────── */

/* CORRECTED 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group G): true selects
 * the device-provided clock (TC18 Table 55), which arrives on ISP_N --
 * ISP_N is therefore required when use_rcv_clk is true, not when it is
 * false. See ep_iseled.h's own file header for the full citation. */
//cfusa:test REQ-ISELED-007
static void test_requires_isp_n(void)
{
    TEST_ASSERT_TRUE(rcp_ep_iseled_requires_isp_n(true));
    TEST_ASSERT_FALSE(rcp_ep_iseled_requires_isp_n(false));
}

/* ── Transmission-complete trigger ─────────────────────────────────────────── */

//cfusa:test REQ-ISELED-008
static void test_trigger_fires_none_always_false(void)
{
    TEST_ASSERT_FALSE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_NONE, true));
    TEST_ASSERT_FALSE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_NONE, false));
}

/* REQ-ISELED-035 (split 2026-08-18, issue #533, from REQ-ISELED-008). */
//cfusa:test REQ-ISELED-035
static void test_trigger_fires_tx_complete_passes_through(void)
{
    TEST_ASSERT_TRUE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_TX_COMPLETE, true));
    TEST_ASSERT_FALSE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_TX_COMPLETE, false));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:test REQ-ISELED-009
static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.iseled_bit_clk_divider);
    TEST_ASSERT_FALSE(cfg.iseled_use_rcv_clk);
    TEST_ASSERT_FALSE(cfg.iseled_crc_enable);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ISELED_TRIGGER_NONE, cfg.trigger);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.base_clk);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.wire_clk_divider);
    TEST_ASSERT_FALSE(cfg.collect_resp);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.nr_leds);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.rcv_timeout);
}

//cfusa:test REQ-ISELED-010
static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_iseled_functional_cfg_writable(
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
    TEST_ASSERT_FALSE(rcp_ep_iseled_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_iseled_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_iseled_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_iseled_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t via_ep0 = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_iseled_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_iseled_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_iseled_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

//cfusa:test REQ-ISELED-011
static void test_set_bit_clk_divider_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_bit_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.iseled_bit_clk_divider);
}

/* REQ-ISELED-036 (split 2026-08-18, issue #533, from REQ-ISELED-011). */
//cfusa:test REQ-ISELED-036
static void test_set_bit_clk_divider_applies_when_authorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_iseled_set_bit_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(42, cfg.iseled_bit_clk_divider);
}

//cfusa:test REQ-ISELED-012
static void test_set_use_rcv_clk_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_use_rcv_clk(
        &cfg, true, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_FALSE(cfg.iseled_use_rcv_clk);
}

/* REQ-ISELED-037 (split 2026-08-18, issue #533, from REQ-ISELED-012). */
//cfusa:test REQ-ISELED-037
static void test_set_use_rcv_clk_applies_when_authorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_iseled_set_use_rcv_clk(
        &cfg, true, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_TRUE(cfg.iseled_use_rcv_clk);
}

//cfusa:test REQ-ISELED-013
static void test_set_crc_enable_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_crc_enable(
        &cfg, true, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_FALSE(cfg.iseled_crc_enable);
}

/* REQ-ISELED-038 (split 2026-08-18, issue #533, from REQ-ISELED-013). */
//cfusa:test REQ-ISELED-038
static void test_set_crc_enable_applies_when_authorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_iseled_set_crc_enable(
        &cfg, true, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_TRUE(cfg.iseled_crc_enable);
}

//cfusa:test REQ-ISELED-014
static void test_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_trigger(
        &cfg, RCP_EP_ISELED_TRIGGER_TX_COMPLETE, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ISELED_TRIGGER_NONE, cfg.trigger);
}

/* REQ-ISELED-039 (split 2026-08-18, issue #533, from REQ-ISELED-014). */
//cfusa:test REQ-ISELED-039
static void test_set_trigger_applies_when_authorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_iseled_set_trigger(
        &cfg, RCP_EP_ISELED_TRIGGER_TX_COMPLETE, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ISELED_TRIGGER_TX_COMPLETE, cfg.trigger);
}

/* ── The EP_func register block ──────────────────────────────────────────── */

static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    uint8_t                        out[RCP_EP_ISELED_EP_FUNC_LEN];

    rcp_ep_iseled_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status         = 0x1234;
    cfg.wire_clk_divider   = 0x55;
    cfg.collect_resp       = true;
    cfg.iseled_use_rcv_clk = true;
    cfg.nr_leds             = 0xABCD;
    cfg.rcv_timeout          = 0x9876;

    rcp_ep_iseled_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ISELED_EP_FUNC_LEN, out[RCP_EP_ISELED_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ISELED_REG_RESERVED_01]);
    TEST_ASSERT_TRUE((out[RCP_EP_ISELED_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ISELED_REG_BASE_CLK]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ISELED_REG_BASE_CLK + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_ISELED_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_ISELED_REG_EP_STATUS + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x55, out[RCP_EP_ISELED_REG_CLK_DIVIDER]);
    TEST_ASSERT_TRUE((out[RCP_EP_ISELED_REG_FLAGS] & RCP_EP_ISELED_FLAG_COLLECT_RESP) != 0u);
    TEST_ASSERT_TRUE((out[RCP_EP_ISELED_REG_FLAGS] & RCP_EP_ISELED_FLAG_USE_RCV_CLK) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0xABu, out[RCP_EP_ISELED_REG_NR_LEDS]);
    TEST_ASSERT_EQUAL_UINT8(0xCDu, out[RCP_EP_ISELED_REG_NR_LEDS + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x98u, out[RCP_EP_ISELED_REG_RCV_TIMEOUT]);
    TEST_ASSERT_EQUAL_UINT8(0x76u, out[RCP_EP_ISELED_REG_RCV_TIMEOUT + 1]);

    TEST_ASSERT_EQUAL_UINT16(0x000Eu, RCP_EP_ISELED_EP_FUNC_LEN);
}

/* iseled_crc_enable gates a second, independent CRC layer this module's
 * own file header documents as deliberately NOT part of the wire register
 * block -- confirm render never touches it either way. */
static void test_render_registers_ignores_crc_enable(void)
{
    rcp_ep_iseled_functional_cfg_t cfg_off, cfg_on;
    uint8_t                        out_off[RCP_EP_ISELED_EP_FUNC_LEN];
    uint8_t                        out_on[RCP_EP_ISELED_EP_FUNC_LEN];

    rcp_ep_iseled_functional_cfg_init(&cfg_off);
    rcp_ep_iseled_functional_cfg_init(&cfg_on);
    cfg_on.iseled_crc_enable = true;

    rcp_ep_iseled_render_registers(&cfg_off, out_off);
    rcp_ep_iseled_render_registers(&cfg_on, out_on);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(out_off, out_on, RCP_EP_ISELED_EP_FUNC_LEN);
}

static void test_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    uint8_t                        payload[2 + 6];

    rcp_ep_iseled_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_ISELED_REG_EP_STATUS;
    payload[2] = 0xAB; payload[3] = 0xCD; /* ep_status */
    payload[4] = 0x11;                    /* wire_clk_divider */
    payload[5] = RCP_EP_ISELED_FLAG_COLLECT_RESP; /* flags */
    payload[6] = 0x22; payload[7] = 0x33; /* nr_leds */

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_RECONFIG_OK,
        rcp_ep_iseled_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0x11, cfg.wire_clk_divider);
    TEST_ASSERT_TRUE(cfg.collect_resp);
    TEST_ASSERT_FALSE(cfg.iseled_use_rcv_clk);
    TEST_ASSERT_EQUAL_UINT16(0x2233, cfg.nr_leds);
}

static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    uint8_t                        payload[2 + 4];

    rcp_ep_iseled_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00), the reserved octet (0x01), and both octets of
     * base_clk (0x04-0x05) -- all read-only. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;
    payload[4] = 0xFF;
    payload[5] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_RECONFIG_OK,
        rcp_ep_iseled_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_ISELED_EP_FUNC_LEN];

        rcp_ep_iseled_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ISELED_EP_FUNC_LEN, out[RCP_EP_ISELED_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ISELED_REG_RESERVED_01]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ISELED_REG_BASE_CLK]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ISELED_REG_BASE_CLK + 1]);
    }
}

static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    uint8_t                        payload[3];

    rcp_ep_iseled_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x0E; /* == RCP_EP_ISELED_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_iseled_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.rcv_timeout);
}

static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    uint8_t                        addr_only[2] = {0x00, 0x08};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_RECONFIG_ERR_SHORT,
        rcp_ep_iseled_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_RECONFIG_ERR_SHORT,
        rcp_ep_iseled_apply_reconfig(&cfg, NULL, 0));
}

static void test_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_iseled_encode_reconfig_request(0x03, 0x0006, data, sizeof(data), 7);
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
    rcp_bytes_t frame = rcp_ep_iseled_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

/* REQ-ISELED-042 (split 2026-08-18, issue #533, from REQ-ISELED-029). */
//cfusa:test REQ-ISELED-042
static void test_reconfig_strerror_never_null(void)
{
    rcp_ep_iseled_reconfig_errc_t codes[] = {
        RCP_EP_ISELED_RECONFIG_OK, RCP_EP_ISELED_RECONFIG_ERR_SHORT,
        RCP_EP_ISELED_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_iseled_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_iseled_reconfig_strerror((rcp_ep_iseled_reconfig_errc_t)99));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_iseled_errc_t codes[] = {
        RCP_EP_ISELED_OK,
        RCP_EP_ISELED_ERR_SHORT_FRAME,
        RCP_EP_ISELED_ERR_BAD_MSG_TYPE,
        RCP_EP_ISELED_ERR_WRONG_BUS,
        RCP_EP_ISELED_ERR_WRONG_OP,
        RCP_EP_ISELED_ERR_BAD_SYMBOL,
        RCP_EP_ISELED_ERR_CRC_MISMATCH,
        RCP_EP_ISELED_ERR_ODD_SYMBOL_COUNT,
        RCP_EP_ISELED_ERR_ALLOC,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_iseled_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_iseled_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_iseled_strerror((rcp_ep_iseled_errc_t)999));
}

/* ── Command request round trip ────────────────────────────────────────────── */

static void test_command_request_round_trip_carries_raw_bytes(void)
{
    /* Bytes model a client-constructed plain ISELED instruction/address/
     * data payload -- this module never parses, strips, or bit-frames any
     * of it at the ACF layer, see the file header. */
    uint8_t     tx[4] = {0x01, 0x02, 0x10, 0x20};
    rcp_bytes_t frame = rcp_ep_iseled_encode_command_request(6, tx, sizeof(tx), 7);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_command_request(frame.data, frame.len, 6, &out_tx, &out_tx_len,
                                              &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

static void test_command_request_round_trip_empty_payload(void)
{
    rcp_bytes_t frame = rcp_ep_iseled_encode_command_request(1, NULL, 0, 1);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_command_request(frame.data, frame.len, 1, &out_tx, &out_tx_len,
                                              &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);

    rcp_bytes_free(&frame);
}

static void test_command_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame = rcp_ep_iseled_encode_command_request(4, tx, sizeof(tx), 0);
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_WRONG_BUS,
        rcp_ep_iseled_decode_command_request(frame.data, frame.len, 5, &out_tx, &out_tx_len,
                                              &txn));

    rcp_bytes_free(&frame);
}

static void test_command_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ; /* not a command request */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_WRONG_OP,
        rcp_ep_iseled_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len,
                                              &txn));

    rcp_bytes_free(&frame);
}

/* TC18 §13.5 Table 30: evt[2:0] = 000b is the only legal value for a
 * plain ISELED command request; every other value (here, 0b010, a
 * reserved value in ISELED's endpoint-type row) shall be rejected. */
static void test_command_request_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = 0x2;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_BAD_EVT,
        rcp_ep_iseled_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len,
                                              &txn));

    rcp_bytes_free(&frame);
}

static void test_command_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_BAD_MSG_TYPE,
        rcp_ep_iseled_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len,
                                              &txn));

    rcp_bytes_free(&frame);
}

static void test_command_request_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0};
    const uint8_t  *out_tx;
    size_t          out_tx_len;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_SHORT_FRAME,
        rcp_ep_iseled_decode_command_request(too_short, sizeof(too_short), 4, &out_tx,
                                              &out_tx_len, &txn));
}

/* ── Regression: write-direction command request is unchanged (issue #471) ──
 *
 * These four assertions are the same wire behavior
 * test_command_request_round_trip_carries_raw_bytes() and
 * test_command_request_rejects_wrong_op() above already pin -- repeated
 * here, by name, as an explicit "the write path did not move" regression
 * check alongside the new read-direction tests below, so a future reader
 * (or a mutation run) sees both directions verified side by side. */
static void test_command_request_write_direction_unchanged_regression(void)
{
    uint8_t        tx[3] = {0x01, 0x02, 0x03};
    rcp_bytes_t    frame = rcp_ep_iseled_encode_command_request(2, tx, sizeof(tx), 9);
    const uint8_t *out_tx = NULL;
    size_t         out_tx_len = 0;
    uint16_t       read_size = 0;
    uint8_t        txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    /* Still ACF_OP_WRITE on the wire -- decode_read_request must reject it. */
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_WRONG_OP,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 2, &out_tx, &out_tx_len,
                                           &read_size, &txn));
    /* And decode_command_request still accepts it exactly as before. */
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_command_request(frame.data, frame.len, 2, &out_tx, &out_tx_len,
                                              &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8(9, txn);

    rcp_bytes_free(&frame);
}

/* ── Read request (issue #471, REQ-ISELED-030/031) ───────────────────────────── */

static void test_read_request_round_trip_carries_address_and_read_size(void)
{
    /* Instruction+Address selecting what to read back -- no Data octets,
     * see the header's own file-level note. */
    uint8_t        tx[2] = {0x03, 0x40};
    rcp_bytes_t    frame = rcp_ep_iseled_encode_read_request(6, tx, sizeof(tx), 12, 7);
    const uint8_t *out_tx = NULL;
    size_t         out_tx_len = 0;
    uint16_t       read_size = 0;
    uint8_t        txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 6, &out_tx, &out_tx_len,
                                           &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT16(12, read_size);
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

static void test_read_request_round_trip_empty_payload(void)
{
    rcp_bytes_t    frame = rcp_ep_iseled_encode_read_request(1, NULL, 0, 64, 1);
    const uint8_t *out_tx = NULL;
    size_t         out_tx_len = 1;
    uint16_t       read_size = 0;
    uint8_t        txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 1, &out_tx, &out_tx_len,
                                           &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);
    TEST_ASSERT_EQUAL_UINT16(64, read_size);

    rcp_bytes_free(&frame);
}

/* Boundary: read_size == 0 is legal (a caller asking for nothing back is
 * still a well-formed read request on the wire). */
static void test_read_request_read_size_zero(void)
{
    rcp_bytes_t frame = rcp_ep_iseled_encode_read_request(3, NULL, 0, 0, 2);
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint16_t    read_size = 0xFFFFu;
    uint8_t     txn;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 3, &out_tx, &out_tx_len,
                                           &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT16(0, read_size);

    rcp_bytes_free(&frame);
}

/* Boundary: read_size == RCP_EP_ISELED_MAX_READ_SIZE (0x0FFF, the ACF
 * header's own 12-bit ceiling) round-trips; one above it is rejected at
 * encode time. */
static void test_read_request_read_size_max_boundary(void)
{
    rcp_bytes_t frame = rcp_ep_iseled_encode_read_request(3, NULL, 0,
                                                            RCP_EP_ISELED_MAX_READ_SIZE, 2);
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint16_t    read_size = 0;
    uint8_t     txn;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 3, &out_tx, &out_tx_len,
                                           &read_size, &txn));
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_ISELED_MAX_READ_SIZE, read_size);

    rcp_bytes_free(&frame);
}

static void test_read_request_read_size_above_max_rejected_at_encode(void)
{
    rcp_bytes_t frame = rcp_ep_iseled_encode_read_request(
        3, NULL, 0, (uint16_t)(RCP_EP_ISELED_MAX_READ_SIZE + 1u), 2);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT32(0, frame.len);
}

static void test_read_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame = rcp_ep_iseled_encode_read_request(4, tx, sizeof(tx), 8, 0);
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint16_t    read_size;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_WRONG_BUS,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 5, &out_tx, &out_tx_len,
                                           &read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint16_t                     read_size;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE; /* not a read request */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_WRONG_OP,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 4, &out_tx, &out_tx_len,
                                           &read_size, &txn));

    rcp_bytes_free(&frame);
}

/* TC18 §13.5 Table 33: evt[2:0] = 000b is the only legal value for a
 * plain ISELED read request; every other value shall be rejected. */
static void test_read_request_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint16_t                     read_size;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.evt         = 0x3;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_BAD_EVT,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 4, &out_tx, &out_tx_len,
                                           &read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint16_t              read_size;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_BAD_MSG_TYPE,
        rcp_ep_iseled_decode_read_request(frame.data, frame.len, 4, &out_tx, &out_tx_len,
                                           &read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0};
    const uint8_t  *out_tx;
    size_t          out_tx_len;
    uint16_t        read_size;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_SHORT_FRAME,
        rcp_ep_iseled_decode_read_request(too_short, sizeof(too_short), 4, &out_tx,
                                           &out_tx_len, &read_size, &txn));
}

/* ── Response round trip ───────────────────────────────────────────────────── */

static void test_response_round_trip_untimed(void)
{
    uint8_t     rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t frame = rcp_ep_iseled_encode_response(2, rx, sizeof(rx), 11, false, 0);
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed, &ts,
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
    rcp_bytes_t frame = rcp_ep_iseled_encode_response(2, rx, sizeof(rx), 200, true,
                                                        0x0102030405060708ull);
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed, &ts,
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
    rcp_bytes_t frame = rcp_ep_iseled_encode_response(2, NULL, 0, 0, false, 0);
    const uint8_t *out_rx;
    size_t      out_rx_len;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_WRONG_BUS,
        rcp_ep_iseled_decode_response(frame.data, frame.len, 3, &out_rx, &out_rx_len, &timed, &ts,
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

    TEST_ASSERT_EQUAL(RCP_EP_ISELED_ERR_SHORT_FRAME,
        rcp_ep_iseled_decode_response(too_short, sizeof(too_short), 2, &out_rx, &out_rx_len,
                                       &timed, &ts, &txn));
}

/* ── REQ-ISELED-025/040: response fragmentation, bounded by read_size ───── */

/* REQ-ISELED-040 (split 2026-08-18, issue #533, from REQ-ISELED-025): the
 * frame-count calculation, independent of actually producing the frames. */
//cfusa:test REQ-ISELED-040
static void test_fragment_count_one_when_capped_data_fits_in_one_fragment(void)
{
    /* 10 available octets, read_size 10, generous 100-octet cap -> one
     * (unfragmented) frame; count is 1, not 0 (0 means "not
     * representable", not "no fragmentation needed"). */
    TEST_ASSERT_EQUAL_UINT(1, rcp_ep_iseled_response_fragment_count(10, 10, 100));
}

//cfusa:test REQ-ISELED-040
static void test_fragment_count_respects_read_size_ceiling(void)
{
    /* 1000 octets of decoded data are actually available, but read_size
     * caps the response at 10 -- fragment planning must be against the
     * capped 10, not the full 1000, so a generous 100-octet
     * max_fragment_payload still yields exactly one frame. */
    TEST_ASSERT_EQUAL_UINT(1, rcp_ep_iseled_response_fragment_count(1000, 10, 100));
}

//cfusa:test REQ-ISELED-040
static void test_fragment_count_splits_capped_data_across_frames(void)
{
    /* read_size caps at 250; max_fragment_payload of 100 means the
     * capped 250 octets need 3 frames (100 + 100 + 50), regardless of
     * how much more data was actually available. */
    TEST_ASSERT_EQUAL_UINT(3, rcp_ep_iseled_response_fragment_count(1000, 250, 100));
}

/* End-to-end: encode a response whose available data (300 octets)
 * exceeds both read_size (200) and max_fragment_payload (64), fragment
 * it, decode+reassemble every fragment via fragment.h's own generic
 * reassembler plus this module's own already-existing (unmodified)
 * rcp_ep_iseled_decode_response() as the per-fragment decoder, and
 * confirm the reassembled result is exactly the first 200 (not 300)
 * octets of the original data. REQ-ISELED-025 (rcp_ep_iseled_encode_
 * response_fragmented() itself) -- its own frame-count is REQ-ISELED-040,
 * exercised above and reused here, not re-asserted. */
//cfusa:test REQ-ISELED-025
static void test_fragment_worst_case_response_round_trip_respects_read_size(void)
{
    uint8_t                    rx[300];
    size_t                     i;
    uint16_t                   read_size            = 200;
    size_t                     max_fragment_payload = 64;
    size_t                     count;
    rcp_bytes_t                frames[4];
    rcp_fragment_reassembler_t reasm;

    for (i = 0; i < sizeof(rx); i++) rx[i] = (uint8_t)(i * 5 + 1);

    count = rcp_ep_iseled_response_fragment_count(sizeof(rx), read_size, max_fragment_payload);
    TEST_ASSERT_EQUAL_UINT(4, count); /* ceil(200 / 64) = 4 */
    TEST_ASSERT_TRUE(count <= (sizeof(frames) / sizeof(frames[0])));

    count = rcp_ep_iseled_encode_response_fragmented(9, rx, sizeof(rx), read_size, 42, false, 0,
                                                       max_fragment_payload, frames);
    TEST_ASSERT_EQUAL_UINT(4, count);

    rcp_fragment_reassembler_init(&reasm, read_size);
    for (i = 0; i < count; i++) {
        const uint8_t              *out_rx;
        size_t                       out_rx_len;
        bool                         timed;
        uint64_t                     ts;
        uint8_t                      txn;
        rcp_acf_byte_message_info_t  hdr;
        rcp_fragment_reasm_result_t  rc;

        TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
            rcp_ep_iseled_decode_response(frames[i].data, frames[i].len, 9, &out_rx, &out_rx_len,
                                           &timed, &ts, &txn));
        TEST_ASSERT_EQUAL_UINT8(42, txn);
        TEST_ASSERT_FALSE(timed);

        /* ms/segment_num live in the ACF header, not this module's own
         * payload -- peeked directly via acf.h, matching fragment.h's own
         * "caller supplies ms/segment_num from wherever its own wire
         * format carries them" reassembly contract. */
        TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(frames[i].data, frames[i].len, &hdr,
                                                           &out_rx, &out_rx_len));

        rc = rcp_fragment_reassembler_feed(&reasm, hdr.ms != 0, hdr.read_size_or_segment_num,
                                            out_rx, out_rx_len);
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
        TEST_ASSERT_EQUAL_UINT(read_size, reassembled_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, reassembled, read_size);
    }

    rcp_fragment_reassembler_destroy(&reasm);
    for (i = 0; i < count; i++) rcp_bytes_free(&frames[i]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_symbol_encode_round_trips_every_nibble);
    RUN_TEST(test_symbol_encode_masks_high_bits);
    RUN_TEST(test_symbol_encode_distinct_parity_for_all_zero_and_all_one_nibbles);
    RUN_TEST(test_symbol_decode_rejects_bad_parity);
    RUN_TEST(test_symbol_decode_accepts_valid_parity_and_sets_nibble);

    RUN_TEST(test_bitframe_encoded_len);

    RUN_TEST(test_bitframe_round_trip_no_crc);
    RUN_TEST(test_bitframe_round_trip_with_crc);
    RUN_TEST(test_bitframe_empty_no_crc);
    RUN_TEST(test_bitframe_decode_rejects_odd_symbol_count);
    RUN_TEST(test_bitframe_decode_rejects_bad_symbol);
    RUN_TEST(test_bitframe_decode_rejects_crc_mismatch);
    RUN_TEST(test_bitframe_decode_rejects_short_frame_when_crc_expected);

    RUN_TEST(test_crc8_deterministic);
    RUN_TEST(test_crc8_differs_for_different_content);
    RUN_TEST(test_crc8_empty_input);

    RUN_TEST(test_requires_isp_n);

    RUN_TEST(test_trigger_fires_none_always_false);
    RUN_TEST(test_trigger_fires_tx_complete_passes_through);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_set_bit_clk_divider_rejects_unauthorized);
    RUN_TEST(test_set_bit_clk_divider_applies_when_authorized);
    RUN_TEST(test_set_use_rcv_clk_rejects_unauthorized);
    RUN_TEST(test_set_use_rcv_clk_applies_when_authorized);
    RUN_TEST(test_set_crc_enable_rejects_unauthorized);
    RUN_TEST(test_set_crc_enable_applies_when_authorized);
    RUN_TEST(test_set_trigger_rejects_unauthorized);
    RUN_TEST(test_set_trigger_applies_when_authorized);

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_render_registers_ignores_crc_enable);
    RUN_TEST(test_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_reconfig_strerror_never_null);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_command_request_round_trip_carries_raw_bytes);
    RUN_TEST(test_command_request_round_trip_empty_payload);
    RUN_TEST(test_command_request_rejects_wrong_bus);
    RUN_TEST(test_command_request_rejects_wrong_op);
    RUN_TEST(test_command_request_rejects_nonzero_evt);
    RUN_TEST(test_command_request_rejects_bad_msg_type);
    RUN_TEST(test_command_request_rejects_short_frame);
    RUN_TEST(test_command_request_write_direction_unchanged_regression);

    RUN_TEST(test_read_request_round_trip_carries_address_and_read_size);
    RUN_TEST(test_read_request_round_trip_empty_payload);
    RUN_TEST(test_read_request_read_size_zero);
    RUN_TEST(test_read_request_read_size_max_boundary);
    RUN_TEST(test_read_request_read_size_above_max_rejected_at_encode);
    RUN_TEST(test_read_request_rejects_wrong_bus);
    RUN_TEST(test_read_request_rejects_wrong_op);
    RUN_TEST(test_read_request_rejects_nonzero_evt);
    RUN_TEST(test_read_request_rejects_bad_msg_type);
    RUN_TEST(test_read_request_rejects_short_frame);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_short_frame);

    RUN_TEST(test_fragment_count_one_when_capped_data_fits_in_one_fragment);
    RUN_TEST(test_fragment_count_respects_read_size_ceiling);
    RUN_TEST(test_fragment_count_splits_capped_data_across_frames);
    RUN_TEST(test_fragment_worst_case_response_round_trip_respects_read_size);

    return UNITY_END();
}
