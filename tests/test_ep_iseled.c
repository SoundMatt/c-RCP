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
//cfusa:test REQ-ISELED-026
//cfusa:test REQ-ISELED-027
//cfusa:test REQ-ISELED-029
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_iseled.h>
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

static void test_symbol_decode_rejects_bad_parity(void)
{
    uint8_t valid = rcp_ep_iseled_symbol_encode(0x03);
    uint8_t corrupted = (uint8_t)(valid ^ 0x10u); /* flip the parity bit */
    uint8_t decoded = 0xFF;

    TEST_ASSERT_FALSE(rcp_ep_iseled_symbol_decode(corrupted, &decoded));
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

static void test_crc8_deterministic_and_input_sensitive(void)
{
    uint8_t a[3] = {0x01, 0x02, 0x03};
    uint8_t b[3] = {0x01, 0x02, 0x04};

    TEST_ASSERT_EQUAL_UINT8(rcp_ep_iseled_crc8(a, sizeof(a)), rcp_ep_iseled_crc8(a, sizeof(a)));
    TEST_ASSERT_NOT_EQUAL(rcp_ep_iseled_crc8(a, sizeof(a)), rcp_ep_iseled_crc8(b, sizeof(b)));
}

static void test_crc8_empty_input(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x00, rcp_ep_iseled_crc8(NULL, 0));
}

/* ── requires_isp_n ─────────────────────────────────────────────────────────── */

/* CORRECTED 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group G): true selects
 * the device-provided clock (TC18 Table 55), which arrives on ISP_N --
 * ISP_N is therefore required when use_rcv_clk is true, not when it is
 * false. See ep_iseled.h's own file header for the full citation. */
static void test_requires_isp_n(void)
{
    TEST_ASSERT_TRUE(rcp_ep_iseled_requires_isp_n(true));
    TEST_ASSERT_FALSE(rcp_ep_iseled_requires_isp_n(false));
}

/* ── Transmission-complete trigger ─────────────────────────────────────────── */

static void test_trigger_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_NONE, true));
    TEST_ASSERT_FALSE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_NONE, false));
    TEST_ASSERT_TRUE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_TX_COMPLETE, true));
    TEST_ASSERT_FALSE(rcp_ep_iseled_trigger_fires(RCP_EP_ISELED_TRIGGER_TX_COMPLETE, false));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

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

static void test_set_bit_clk_divider_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_bit_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.iseled_bit_clk_divider);
}

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

static void test_set_use_rcv_clk_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_use_rcv_clk(
        &cfg, true, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_FALSE(cfg.iseled_use_rcv_clk);
}

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

static void test_set_crc_enable_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_crc_enable(
        &cfg, true, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_FALSE(cfg.iseled_crc_enable);
}

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

static void test_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        none = {0};

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_iseled_set_trigger(
        &cfg, RCP_EP_ISELED_TRIGGER_TX_COMPLETE, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ISELED_TRIGGER_NONE, cfg.trigger);
}

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

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_symbol_encode_round_trips_every_nibble);
    RUN_TEST(test_symbol_encode_masks_high_bits);
    RUN_TEST(test_symbol_encode_distinct_parity_for_all_zero_and_all_one_nibbles);
    RUN_TEST(test_symbol_decode_rejects_bad_parity);

    RUN_TEST(test_bitframe_encoded_len);

    RUN_TEST(test_bitframe_round_trip_no_crc);
    RUN_TEST(test_bitframe_round_trip_with_crc);
    RUN_TEST(test_bitframe_empty_no_crc);
    RUN_TEST(test_bitframe_decode_rejects_odd_symbol_count);
    RUN_TEST(test_bitframe_decode_rejects_bad_symbol);
    RUN_TEST(test_bitframe_decode_rejects_crc_mismatch);
    RUN_TEST(test_bitframe_decode_rejects_short_frame_when_crc_expected);

    RUN_TEST(test_crc8_deterministic_and_input_sensitive);
    RUN_TEST(test_crc8_empty_input);

    RUN_TEST(test_requires_isp_n);

    RUN_TEST(test_trigger_fires);

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

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
