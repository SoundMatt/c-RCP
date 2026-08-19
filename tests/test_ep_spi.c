/* SPDX-License-Identifier: MPL-2.0 */
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
//cfusa:test REQ-SPI-027
//cfusa:test REQ-SPI-028
//cfusa:test REQ-SPI-029
//cfusa:test REQ-SPI-030
//cfusa:test REQ-SPI-040
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

    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);

    for (i = 0; i < RCP_EP_SPI_MAX_CHANNELS; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_0, cfg.channels[i].mode);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_BIT_ORDER_MSB_FIRST, cfg.channels[i].bit_order);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_CS_ACTIVE_LOW, cfg.channels[i].cs_polarity);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_TRIGGER_NONE, cfg.channels[i].trigger);
        TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[i].clock_divider);
        TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[i].inter_byte_delay_ns);
        TEST_ASSERT_EQUAL_UINT32(0, cfg.channels[i].inter_transfer_delay_ns);
        TEST_ASSERT_EQUAL_UINT16(0, cfg.channels[i].baud_rate_kbps);
        TEST_ASSERT_FALSE(cfg.channels[i].use_common_cs);
        TEST_ASSERT_EQUAL_UINT8(0, cfg.channels[i].cs_clk_leadtime);
        TEST_ASSERT_EQUAL_UINT8(0, cfg.channels[i].clk_cs_trailtime);
        TEST_ASSERT_EQUAL_UINT8(0, cfg.channels[i].bits_max);
        TEST_ASSERT_EQUAL_UINT8(0, cfg.channels[i].pause_min);
        TEST_ASSERT_FALSE(cfg.channels[i].deassert_cs_pause);
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
    TEST_ASSERT_FALSE(rcp_ep_spi_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_spi_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_spi_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_spi_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
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
    writer.via_owning_stream = true;

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
    writer.via_owning_stream = true;

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
    writer.via_owning_stream = true;

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
    writer.via_owning_stream = true;

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
    writer.via_owning_stream = true;

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
    writer.via_owning_stream = true;

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_spi_set_channel_trigger(
        &cfg, 0, RCP_EP_SPI_TRIGGER_CS_ASSERT, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_TRIGGER_CS_ASSERT, cfg.channels[0].trigger);
}

/* ── The EP_func register block ────────────────────────────────────────────── */

//cfusa:test REQ-SPI-038
static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     out[RCP_EP_SPI_EP_FUNC_LEN];

    rcp_ep_spi_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status         = 0x1234;
    cfg.channels[0].mode           = (uint8_t)RCP_EP_SPI_MODE_3; /* cpol=1, cpha=1 */
    cfg.channels[0].cs_polarity    = (uint8_t)RCP_EP_SPI_CS_ACTIVE_HIGH;
    cfg.channels[0].use_common_cs  = true;
    cfg.channels[0].baud_rate_kbps = 0x5566;
    cfg.channels[0].cs_clk_leadtime  = 3;
    cfg.channels[0].clk_cs_trailtime = 4;
    cfg.channels[0].bits_max         = 5;
    cfg.channels[0].pause_min        = 6;
    cfg.channels[0].deassert_cs_pause = true;

    rcp_ep_spi_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_EP_FUNC_LEN, out[RCP_EP_SPI_REG_EP_LEN]);
    /* TC18 0.5.1_RC5: spi_nr_cs is a 4-bit "(count - 1)" field, upper
     * nibble reserved -- RCP_EP_SPI_MAX_CHANNELS (6) renders as 0x05, not
     * a plain 6, per the file header's own "FIXED 2026-08-11" note. */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(RCP_EP_SPI_MAX_CHANNELS - 1u), out[RCP_EP_SPI_REG_NR_CS]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[RCP_EP_SPI_REG_NR_CS] & 0xF0u); /* reserved nibble */
    TEST_ASSERT_TRUE((out[RCP_EP_SPI_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_SPI_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_SPI_REG_EP_STATUS + 1]);

    /* Channel 0's own block starts at 0x0006. */
    TEST_ASSERT_EQUAL_UINT8(0x55u, out[0x0006]);
    TEST_ASSERT_EQUAL_UINT8(0x66u, out[0x0007]);
    TEST_ASSERT_EQUAL_UINT8(
        RCP_EP_SPI_CFG_BIT_CLK_POLARITY | RCP_EP_SPI_CFG_BIT_CLK_PHASE |
            RCP_EP_SPI_CFG_BIT_CS_POLARITY | RCP_EP_SPI_CFG_BIT_USE_CS |
            RCP_EP_SPI_CFG_BIT_DEASSERT_CS_PAUSE,
        out[0x0008]);
    TEST_ASSERT_EQUAL_UINT8(3, out[0x0009]);
    TEST_ASSERT_EQUAL_UINT8(4, out[0x000A]);
    TEST_ASSERT_EQUAL_UINT8(5, out[0x000B]);
    TEST_ASSERT_EQUAL_UINT8(6, out[0x000C]);
    TEST_ASSERT_EQUAL_UINT8(0, out[0x000D]); /* channel 0's reserved octet */

    /* Channel 1's own block starts at 0x0006 + 8 = 0x000E, per Table 39's
     * own explicit spi_baud_rate1 address. */
    TEST_ASSERT_EQUAL_UINT16(0x000Eu,
        (uint16_t)(RCP_EP_SPI_REG_CHANNEL_BASE + 1u * RCP_EP_SPI_REG_CHANNEL_SPAN));

    TEST_ASSERT_EQUAL_UINT16(0x0036u, RCP_EP_SPI_EP_FUNC_LEN);
}

//cfusa:test REQ-SPI-039
static void test_apply_reconfig_writes_baud_rate(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     payload[4];

    rcp_ep_spi_functional_cfg_init(&cfg);

    payload[0] = 0x00; /* address hi */
    payload[1] = 0x0E; /* address lo -- channel 1's baud_rate */
    payload[2] = 0x12;
    payload[3] = 0x34;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0x1234, cfg.channels[1].baud_rate_kbps);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.channels[0].baud_rate_kbps); /* untouched */
}

/* TC18 0.5.1_RC5, ticket NXP_100 (see the file header's own "FIXED
 * 2026-08-11" note): spi_deassert_cs_pauseN is bit 4 of a channel's own
 * +0x02 cfg octet -- proves it round-trips through the parse path
 * (rcp_ep_spi_apply_reconfig(), not just render), and that the other
 * three cfg bits are unaffected by setting or clearing it. */
static void test_apply_reconfig_writes_deassert_cs_pause_bit(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_spi_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x08; /* address = channel 0's own cfg octet (0x0006+0x02) */
    payload[2] = RCP_EP_SPI_CFG_BIT_DEASSERT_CS_PAUSE | RCP_EP_SPI_CFG_BIT_CLK_PHASE;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_TRUE(cfg.channels[0].deassert_cs_pause);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_1, cfg.channels[0].mode); /* cpha only */
    TEST_ASSERT_FALSE(cfg.channels[0].use_common_cs);
    TEST_ASSERT_FALSE(cfg.channels[1].deassert_cs_pause); /* untouched */

    payload[2] = 0x00;
    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_FALSE(cfg.channels[0].deassert_cs_pause);
}

/* MC/DC: mode_from_bits()'s cascading if-chain
 *   if (!cpol && !cpha) return MODE_0;
 *   if (!cpol &&  cpha) return MODE_1;
 *   if ( cpol && !cpha) return MODE_2;
 *   return MODE_3;
 * is only ever driven through the real parse path (parse_registers(),
 * called from rcp_ep_spi_apply_reconfig()) with cpol=0 -- every existing
 * apply_reconfig test leaves every channel's cpol bit clear, so MODE_2
 * and MODE_3 are only ever reached by *directly* assigning
 * cfg.channels[i].mode in render-only tests (bypassing mode_from_bits
 * entirely). cpol=1 was therefore never observed through this decision
 * chain at all -- proves both remaining modes derive correctly from the
 * cpol/cpha wire bits rather than only round-tripping whatever render()
 * puts there. */
static void test_apply_reconfig_derives_mode_2_and_3_from_cpol_cpha_bits(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_spi_functional_cfg_init(&cfg);

    /* cpol=1, cpha=0 -> MODE_2. */
    payload[0] = 0x00;
    payload[1] = 0x08; /* channel 0's own cfg octet */
    payload[2] = RCP_EP_SPI_CFG_BIT_CLK_POLARITY;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_2, cfg.channels[0].mode);

    /* cpol=1, cpha=1 -> MODE_3. */
    payload[2] = RCP_EP_SPI_CFG_BIT_CLK_POLARITY | RCP_EP_SPI_CFG_BIT_CLK_PHASE;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_MODE_3, cfg.channels[0].mode);
}

static void test_apply_reconfig_writes_multi_channel_span(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     payload[2 + 16];

    rcp_ep_spi_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x06; /* address = channel 0's block base */
    /* Channel 0's 8-octet block, then channel 1's 8-octet block. */
    payload[2] = 0xAA; payload[3] = 0xBB; /* baud_rate0 */
    payload[4] = 0x00;                    /* cfg byte -- all bits clear */
    payload[5] = 1; payload[6] = 2; payload[7] = 3; payload[8] = 4; payload[9] = 0xFF; /* reserved, ignored */
    payload[10] = 0xCC; payload[11] = 0xDD; /* baud_rate1 */
    payload[12] = 0x00;
    payload[13] = 5; payload[14] = 6; payload[15] = 7; payload[16] = 8; payload[17] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xAABB, cfg.channels[0].baud_rate_kbps);
    TEST_ASSERT_EQUAL_UINT8(1, cfg.channels[0].cs_clk_leadtime);
    TEST_ASSERT_EQUAL_UINT8(2, cfg.channels[0].clk_cs_trailtime);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.channels[0].bits_max);
    TEST_ASSERT_EQUAL_UINT8(4, cfg.channels[0].pause_min);
    TEST_ASSERT_EQUAL_UINT16(0xCCDD, cfg.channels[1].baud_rate_kbps);
    TEST_ASSERT_EQUAL_UINT8(5, cfg.channels[1].cs_clk_leadtime);
    TEST_ASSERT_EQUAL_UINT8(6, cfg.channels[1].clk_cs_trailtime);
    TEST_ASSERT_EQUAL_UINT8(7, cfg.channels[1].bits_max);
    TEST_ASSERT_EQUAL_UINT8(8, cfg.channels[1].pause_min);
}

static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     payload[2 + 2];

    rcp_ep_spi_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00) and NR_CS (0x01) -- both read-only. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));

    /* cfg has no separate EP_LEN/NR_CS storage -- render_registers() must
     * still report the true constants, proving the write above never took
     * effect. */
    {
        uint8_t out[RCP_EP_SPI_EP_FUNC_LEN];

        rcp_ep_spi_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_SPI_EP_FUNC_LEN, out[RCP_EP_SPI_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(RCP_EP_SPI_MAX_CHANNELS - 1u), out[RCP_EP_SPI_REG_NR_CS]);
    }
}

static void test_apply_reconfig_ignores_channel_reserved_octet(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_spi_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x0D; /* channel 0's own reserved octet */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_SPI_EP_FUNC_LEN];

        rcp_ep_spi_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8(0, out[0x000D]);
    }
}

static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_spi_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x36; /* == RCP_EP_SPI_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_spi_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.channels[5].baud_rate_kbps);
}

static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    uint8_t                     addr_only[2] = {0x00, 0x06};

    rcp_ep_spi_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_ERR_SHORT,
        rcp_ep_spi_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_ERR_SHORT,
        rcp_ep_spi_apply_reconfig(&cfg, NULL, 0));
}

//cfusa:test REQ-SPI-042
static void test_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_spi_encode_reconfig_request(0x03, 0x0006, data, sizeof(data), 7);
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
    rcp_bytes_t frame = rcp_ep_spi_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

/* MC/DC: independently demonstrates the `data == NULL` half of
 * `data_len == 0 || data == NULL` -- a nonzero data_len paired with a NULL
 * data pointer, so the rejection can only be attributed to the second
 * operand, not the first (which the empty-data test above already
 * covers). */
static void test_encode_reconfig_request_rejects_null_data_with_nonzero_len(void)
{
    rcp_bytes_t frame = rcp_ep_spi_encode_reconfig_request(0x00, 0, NULL, 5, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-SPI-043
static void test_reconfig_strerror_never_null(void)
{
    rcp_ep_spi_reconfig_errc_t codes[] = {
        RCP_EP_SPI_RECONFIG_OK, RCP_EP_SPI_RECONFIG_ERR_SHORT,
        RCP_EP_SPI_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_spi_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_spi_reconfig_strerror((rcp_ep_spi_reconfig_errc_t)99));
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

/* TC18 v0.5.1_RC §12.9.1 "Handling of requests" states the op field's
 * two senses:
 *
 *   A response with pay load data read from the EP is given, if requested
 *   by op=0 (read request) after the request has been executed.
 *   A response with err=0 and no payload is given after successful
 *   execution of a request with op=1 (write request) ...
 *
 * and §13.7.3.3's own worked SPI example, Figure 23 -- captioned "SPI
 * request (example to write 20 bytes and get a response with 10 on SPI
 * channel 3)" -- shows that request's byte message info as evt = 0101b,
 * op=0, read_size = 0x0A. So an SPI transfer request, which sends PICO
 * bytes and expects POCI bytes back, carries op=0. Verified here against
 * the literal wire bit rather than against re-encoded output: acf.h maps
 * RCP_ACF_OP_READ onto wire op=0. This module previously encoded op=1 and
 * rejected op=0 -- exactly inverted. */
//cfusa:test REQ-SPI-026
static void test_transfer_request_uses_read_direction_op(void)
{
    uint8_t                     tx[1] = {0x55};
    rcp_bytes_t                 frame = rcp_ep_spi_encode_transfer_request(4, 3, tx, sizeof(tx),
                                                                            0, 3);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;

    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ACF_OP_READ, hdr.op);
    /* Figure 23's evt field selects the SPI channel: channel 3 there. */
    TEST_ASSERT_EQUAL_UINT8(3u, (uint8_t)(hdr.evt & 0x7u));

    rcp_bytes_free(&frame);
}

/* §13.7.3.3's own worked example, Figure 23: "write 20 bytes and get a
 * response with 10 on SPI channel 3" -- evt = 0101b, op=0, read_size =
 * 0x0A. Verifies read_size itself now round-trips through the ACF
 * header's read_size_or_segment_num field. */
//cfusa:test REQ-SPI-041
//cfusa:test REQ-SPI-044
static void test_transfer_request_round_trip(void)
{
    uint8_t     tx[3] = {0x01, 0x02, 0x03};
    rcp_bytes_t frame = rcp_ep_spi_encode_transfer_request(4, 2, tx, sizeof(tx), 0x0Au, 9);
    uint8_t     channel = 0xFF;
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint16_t    out_read_size = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &out_read_size, &txn));
    TEST_ASSERT_EQUAL_UINT8(2, channel);
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT16(0x0Au, out_read_size);
    TEST_ASSERT_EQUAL_UINT8(9, txn);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_round_trip_empty_payload(void)
{
    rcp_bytes_t frame = rcp_ep_spi_encode_transfer_request(4, 0, NULL, 0, 0, 1);
    uint8_t     channel = 0xFF;
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 1;
    uint16_t    out_read_size = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &out_read_size, &txn));
    TEST_ASSERT_EQUAL_UINT8(0, channel);
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame = rcp_ep_spi_encode_transfer_request(4, 1, tx, sizeof(tx), 0, 0);
    uint8_t     channel;
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint16_t    out_read_size;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_WRONG_BUS,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 5, &channel, &out_tx,
                                            &out_tx_len, &out_read_size, &txn));

    rcp_bytes_free(&frame);
}

/* The mirror of test_transfer_request_uses_read_direction_op(): a frame
 * carrying the write direction (§12.9.1's op=1, "no payload data
 * response") is not an SPI transfer request. */
static void test_transfer_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     channel;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint16_t                     out_read_size;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE; /* not a transfer request */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_WRONG_OP,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &out_read_size, &txn));

    rcp_bytes_free(&frame);
}

/* TC18 v0.5.1_RC Table 30 (§13.5), the SPI rows: evt[2:0] "000b to 101b"
 * selects channel 0..5, "110b" is reserved and to be rejected with
 * UNSUPPORTED_CMD, and "111b" is the configuration escape hatch -- so
 * neither 6 nor 7 is a channel selector. (op is the read direction here
 * because that is what a transfer request carries -- see
 * test_transfer_request_uses_read_direction_op(); the channel check is
 * what this test is about.) */
static void test_transfer_request_rejects_bad_channel(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     channel;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint16_t                     out_read_size;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.evt         = 7; /* not a valid channel selector */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_BAD_CHANNEL,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &out_read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    uint8_t              channel;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint16_t              out_read_size;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_BAD_MSG_TYPE,
        rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 4, &channel, &out_tx,
                                            &out_tx_len, &out_read_size, &txn));

    rcp_bytes_free(&frame);
}

static void test_transfer_request_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0};
    uint8_t        channel;
    const uint8_t  *out_tx;
    size_t          out_tx_len;
    uint16_t        out_read_size;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_SHORT_FRAME,
        rcp_ep_spi_decode_transfer_request(too_short, sizeof(too_short), 4, &channel, &out_tx,
                                            &out_tx_len, &out_read_size, &txn));
}

/* FIXED 2026-08-12 (issue #201, REQ-SPI-036): TC18 §13.7.3.3's own
 * transfer-length rule -- zero-fill when read_size exceeds the payload,
 * full payload on PICO otherwise -- verified directly against
 * rcp_ep_spi_transfer_length(), one case per direction plus the
 * exactly-equal boundary. */
//cfusa:test REQ-SPI-036
static void test_spi_transfer_length_zero_fills_when_read_size_exceeds_payload(void)
{
    TEST_ASSERT_EQUAL_UINT(10u, rcp_ep_spi_transfer_length(3u, 10u));
}

static void test_spi_transfer_length_presents_full_payload_when_read_size_is_smaller(void)
{
    TEST_ASSERT_EQUAL_UINT(10u, rcp_ep_spi_transfer_length(10u, 3u));
}

static void test_spi_transfer_length_equal_case(void)
{
    TEST_ASSERT_EQUAL_UINT(5u, rcp_ep_spi_transfer_length(5u, 5u));
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

/* rcp_ep_spi_compound_wait_status_equal() was removed in v0.111.0: it
 * hardcoded a fixed 4-byte comparison length that isn't an SPI-specific
 * rule (see ep_spi.h's file header). Compound-wait comparisons against
 * SPI now go through acf.h's rcp_acf_compound_wait_evt_valid()/_match()
 * directly, exercised generically (including this endpoint type's own
 * 4-of-20-byte length-capping example from the specification) in
 * test_acf.c. */

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
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
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

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_apply_reconfig_writes_baud_rate);
    RUN_TEST(test_apply_reconfig_writes_deassert_cs_pause_bit);
    RUN_TEST(test_apply_reconfig_derives_mode_2_and_3_from_cpol_cpha_bits);
    RUN_TEST(test_apply_reconfig_writes_multi_channel_span);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_ignores_channel_reserved_octet);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_encode_reconfig_request_rejects_null_data_with_nonzero_len);
    RUN_TEST(test_reconfig_strerror_never_null);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_transfer_request_uses_read_direction_op);
    RUN_TEST(test_transfer_request_round_trip);
    RUN_TEST(test_transfer_request_round_trip_empty_payload);
    RUN_TEST(test_transfer_request_rejects_wrong_bus);
    RUN_TEST(test_transfer_request_rejects_wrong_op);
    RUN_TEST(test_transfer_request_rejects_bad_channel);
    RUN_TEST(test_transfer_request_rejects_bad_msg_type);
    RUN_TEST(test_transfer_request_rejects_short_frame);
    RUN_TEST(test_spi_transfer_length_zero_fills_when_read_size_exceeds_payload);
    RUN_TEST(test_spi_transfer_length_presents_full_payload_when_read_size_is_smaller);
    RUN_TEST(test_spi_transfer_length_equal_case);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_bad_channel);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
