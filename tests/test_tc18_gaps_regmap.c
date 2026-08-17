/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-CFG-011
//cfusa:test REQ-CFG-012
//cfusa:test REQ-DISC-029
//cfusa:test REQ-RMAP-023
//cfusa:test REQ-RMAP-024
//cfusa:test REQ-RMAP-025
//cfusa:test REQ-RMAP-026
//cfusa:test REQ-RMAP-027
//cfusa:test REQ-RMAP-028
//cfusa:test REQ-RMAP-029
//cfusa:test REQ-RMAP-030
//cfusa:test REQ-RMAP-031
//cfusa:test REQ-RMAP-032
//cfusa:test REQ-RMAP-033
//cfusa:test REQ-RMAP-034
//cfusa:test REQ-RMAP-035
//cfusa:test REQ-RMAP-036
//cfusa:test REQ-RMAP-037
//cfusa:test REQ-RMAP-038
//cfusa:test REQ-RMAP-039
//cfusa:test REQ-RMAP-040
//cfusa:test REQ-RMAP-041
//cfusa:test REQ-RMAP-042
//cfusa:test REQ-RMAP-043
//cfusa:test REQ-RMAP-044
//cfusa:test REQ-RMAP-045
//cfusa:test REQ-RMAP-046
//cfusa:test REQ-RMAP-047
//cfusa:test REQ-RMAP-048
//cfusa:test REQ-RMAP-049
//cfusa:test REQ-RMAP-050
//cfusa:test REQ-RMAP-051
//cfusa:test REQ-RMAP-052
//cfusa:test REQ-RMAP-053
//cfusa:test REQ-RMAP-054
//cfusa:test REQ-RMAP-055
//cfusa:test REQ-RMAP-056
//cfusa:test REQ-RMAP-057
//cfusa:test REQ-RMAP-058
//cfusa:test REQ-RMAP-059
//cfusa:test REQ-WAKEUP-020
//cfusa:test REQ-RMAP-060
//cfusa:test REQ-RMAP-061
//cfusa:test REQ-RMAP-062
//cfusa:test REQ-RMAP-063
//cfusa:test REQ-RMAP-064
//cfusa:test REQ-RMAP-065
//cfusa:test REQ-RMAP-066
//cfusa:test REQ-RMAP-067
//cfusa:test REQ-RMAP-068
//cfusa:test REQ-RMAP-069
//cfusa:test REQ-RMAP-071
//cfusa:test REQ-RMAP-072
//cfusa:test REQ-RMAP-073
//cfusa:test REQ-RMAP-074
//cfusa:test REQ-RMAP-075
//cfusa:test REQ-RMAP-076
//cfusa:test REQ-RMAP-077
//cfusa:test REQ-RMAP-078
//cfusa:test REQ-RMAP-079
//cfusa:test REQ-RMAP-080
//cfusa:test REQ-RMAP-081

/*
 * test_tc18_gaps_regmap.c -- spec-literal conformance-and-deviation suite
 * for the register-map, HW-pin-mapping, stream-configuration, EP_ID-map,
 * configuration-request and discovery clauses of the OPEN Alliance TC18
 * Remote Control Protocol newly catalogued in .fusa-reqs.json (TC18
 * §12.3, §12.7.1, §12.7.5 Table 18, §12.7.6 Tables 19-21, §12.7.7
 * Table 22, §12.7.8 Table 23, §12.7.9 Table 24, §12.7.11-§12.7.14 and
 * §13.7.1.2 Table 33).
 *
 * Requirements catalogued "implemented" are asserted literally, at the
 * addresses/widths/encodings their TC18 citation gives. Requirements
 * catalogued "partial" or "not-implemented" get a *deviation-pinning*
 * test instead: the assertion states the current, real, observable
 * behaviour of this codebase, and the comment above it names the TC18
 * clause that is not met and what a conforming implementation would do
 * instead. Such a test is expected to fail on the day the gap is closed
 * -- that is the point: closing a catalogued gap must be a deliberate,
 * visible edit here and not a silent drift.
 */

#include "unity.h"

#include <rcp/rcp.h>
#include <rcp/avtp.h>
#include <rcp/acf.h>
#include <rcp/errors.h>
#include <rcp/lifecycle.h>
#include <rcp/regmap.h>
#include <rcp/respqueue.h>
#include <rcp/discovery.h>
#include <rcp/config.h>
#include <rcp/mock.h>
#include <rcp/request_sequencer.h>
#include <rcp/ep_gpio.h>
#include <rcp/ep_pwm.h>
#include <rcp/ep_spi.h>
#include <rcp/ep_i2c.h>
#include <rcp/e2e.h>
#include <rcp/fragment.h>
#include <rcp/deadline.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t SERVER_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x11};
static const uint8_t CLIENT_A_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0xA1};
static const uint8_t CLIENT_B_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0xB2};

/* Writable/unauthorized writer contexts, reused throughout. */
static const rcp_lifecycle_writer_ctx_t ROOT_WRITER      = {true, true};
static const rcp_lifecycle_writer_ctx_t PLAIN_WRITER     = {false, false};
/* The discovery claimant -- the only writer HW_GENERIC's HW_UNCONFIGURED
 * branch admits as of the REQ-LIFECYCLE-026/035 fix (see lifecycle.c's
 * own doc comment). */
static const rcp_lifecycle_writer_ctx_t DISCOVERY_WRITER = {false, false, false, true};

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Every rcp_regmap_general_t field set to a distinctive nonzero value, so
 * a register that is genuinely readable at its TC18 absolute address is
 * distinguishable from zero-fill. */
static rcp_regmap_general_t populated_map(void)
{
    rcp_regmap_general_t map;

    rcp_regmap_general_init(&map);
    map.magic                     = 0xC0FFEE01u;
    map.svr_version               = 0x00010501u;
    map.vendor_id                 = 0x1234u;
    map.device_id                 = 0x5678u;
    map.svr_ep_count              = 0x0009u;
    map.svr_req_stream_max        = 0xABu;
    map.svr_responder_streams_max = 0xCDu;
    map.svr_sequencers_max        = 0x07u;
    map.svr_configuration_lock    = 0x01u; /* nonzero -- distinguishable from the unlocked default */
    map.svr_responder_mem_size    = 0x1122u;
    map.svr_req_mem_size          = 0x3344u;
    map.svr_implemented_options   = 0x1Fu; /* all five REQ-RMAP-030 bits set */
    map.svr_io_pin_count          = 0x0020u;
    map.svr_root_client_index     = 0x0002u;
    map.svr_hw_cfg_ptr            = 0x0060u;
    map.svr_request_stream_cfg_capacity  = 0x08u;
    map.svr_response_stream_cfg_capacity = 0x04u;
    map.svr_request_stream_cfg_ptr       = 0x0070u;
    map.svr_response_stream_cfg_ptr      = 0x0090u;
    map.svr_ep_generic_cfg_ptr           = 0x00A0u;
    map.svr_ep_generic_cfg_capacity      = 0x0140u; /* bytes, not entries */
    map.svr_ep_bytebus_id_map_ptr        = 0x00B0u;
    map.svr_ep_bytebus_id_map_capacity   = 0x10u; /* entries */
    map.svr_ep_functional_cfg_ptr        = 0x00C0u;
    map.svr_sequencer_state_ptr          = 0x00D0u;
    map.svr_network_interface_cfg_ptr      = 0x0050u;
    map.svr_network_interface_cfg_capacity = 0x0004u;
    map.svr_physical_layer_cfg_ptr         = 0x0058u;
    map.svr_physical_layer_cfg_capacity    = 0x0002u;
    map.svr_time_synch_cfg_ptr             = 0x0060u;
    map.svr_time_synch_cfg_capacity        = 0x0002u;
    map.svr_security_cfg_ptr               = 0x0070u;
    map.svr_security_cfg_capacity          = 0x0008u;
    map.svr_device_specific_cfg_ptr        = 0x0080u; /* REQ-RMAP-039 (issue #429) */
    map.svr_device_specific_cfg_capacity   = 0x0006u;

    return map;
}

/* The whole Table 18 general register map, via the REQ-RMAP-024 wire
 * codec: encodes a read response of read_size octets and decodes it back
 * into *out (which the caller must have already defined, e.g. via
 * rcp_regmap_general_init() -- a short response leaves the remaining
 * fields of *out exactly as the caller left them, matching
 * rcp_regmap_general_decode_read_response()'s own doc comment). */
static rcp_regmap_general_errc_t read_general_full(const rcp_regmap_general_t *map,
                                                     uint8_t read_size,
                                                     rcp_regmap_general_t *out)
{
    rcp_bytes_t                frame = rcp_regmap_general_encode_read_response(map, read_size, 7);
    rcp_regmap_general_errc_t  rc;

    TEST_ASSERT_NOT_NULL(frame.data);
    rc = rcp_regmap_general_decode_read_response(frame.data, frame.len, out);
    rcp_bytes_free(&frame);
    return rc;
}

/* This TU's own big-endian read helpers, for spot-checking
 * rcp_regmap_general_render()'s raw output directly -- matching every
 * production TU's own house convention of not sharing a byte-order util
 * across modules. */
static uint16_t get_test_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void put_test_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_test_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* ── §13.7.1.2: effective register-write payload length ───────────────────── */

static void test_reg_write_len_matches_the_formula(void)
{
    /* REQ-RMAP-069 (TC18 §13.7.1.2, corrected in spec revision 0.5.1_RC5):
     * "Effective number of bytes to be written to register map =
     * (acf_msg_length - 3) x 4 - pad - 2" -- the trailing "- 2" subtracts
     * the 2-octet register start address that leads the byte payload
     * (RC5's own Figure 22), a term the pre-fix formula omitted entirely.
     * acf_msg_length=3 (the fixed region alone, no data) with no pad
     * yields 0 data octets regardless. acf_msg_length=5 (2 quadlets = 8
     * octets of data region) with pad=2 yields 8-2-2=4 data octets. */
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(3u, 0u));
    TEST_ASSERT_EQUAL_UINT(4u, rcp_acf_reg_write_len(5u, 2u));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_acf_reg_write_len(4u, 0u));

    /* Fail-safe: a malformed/adversarial frame (acf_msg_length too small
     * to hold the fixed region, or pad+2 exceeding what remains) never
     * underflows to a huge size_t -- it reads as 0 effective octets. */
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(2u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(0u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(4u, 5u));

    /* New boundary case: pad alone would fit under the OLD formula
     * (pad(3) <= total_octets(4), which used to return 4-3=1), but
     * pad+2 does not (3+2=5 > 4) -- proves the "-2" term changes real
     * results, not just edge cases that were already 0 either way. */
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(4u, 3u));
}

/* ── §12.7.1: the generic evt[2:0] == 111b configuration request ──────────── */

/* TC18 §12.7.1 / Figure 18 defines ONE generic configuration request --
 * evt[2:0] = 111b, payload = a 16-bit relative start address within the
 * addressed endpoint's EP_func section followed by configuration data --
 * usable against EVERY endpoint type. c-RCP implements that shape for
 * PWM_OUT (asserted first, spec-literally: clk_divider at relative
 * 0x0008, signal_flags at 0x0009). FIXED 2026-08-11 (c-RCP-AUDIT-06,
 * issue #256 Group G, REQ-GPIO-013): GPIO now implements the identical
 * shape (asserted second: clk_divider at relative 0x0008, matching
 * PWM_OUT's own offset for the same register) -- its former
 * rcp_ep_gpio_apply_reconfig() (a pin-direction-toggle bitmask, no
 * relative start address at all) is retired from this role and renamed
 * rcp_ep_gpio_toggle_pin_direction() (see ep_gpio.h's own file header).
 * FIXED 2026-08-11 (issue #256 Group I, REQ-SPI-035): SPI now implements
 * the identical shape too (asserted third: channel 1's baud_rate at
 * relative 0x000E, per Table 39's own explicit per-channel addressing --
 * unlike PWM_OUT/GPIO, SPI's evt[2:0] ALSO carries channel selection
 * 000b-101b for its normal transfer requests, per Table 30's own SPI row;
 * that channel-selection design was already correct and is untouched --
 * only the previously entirely-missing 111b reconfig path is new here).
 * FIXED 2026-08-11 (issue #256 Group I, REQ-I2C-019): I2C now implements
 * the identical shape too (asserted fourth: i2c_clock_divider at relative
 * 0x0008 -- Table 46's own printed address for this register, 0x0006,
 * collides with i2c_base_clk's own printed address and was corrected via
 * cross-table pattern matching against PWM_OUT's/GPIO's/SPI's own common
 * prefixes; see ep_i2c.h's file header). This test was renamed from
 * `..._is_pwm_out_gpio_and_spi` to avoid an ever-growing name as each
 * endpoint type gets its own fix -- see the (now shrinking) remaining
 * list below instead. Deviation, narrowed but not closed: UART/LIN/CAN/
 * ADC/ISELED/MDIO/wakeup still have no reconfig entry point of any kind.
 * A conforming implementation would decode the same address+data payload
 * for all of them -- REQ-CFG-011 tracks the remaining 7. */
static void test_generic_config_request_implemented_endpoints(void)
{
    rcp_ep_pwm_out_functional_cfg_t pwm_cfg;
    rcp_ep_gpio_functional_cfg_t    gpio_cfg;
    rcp_ep_spi_functional_cfg_t     spi_cfg;
    rcp_ep_i2c_functional_cfg_t     i2c_cfg;
    const uint8_t                   pwm_write[4]  = {0x00, 0x08, 0x33, 0x05};
    const uint8_t                   gpio_write[3] = {0x00, 0x08, 0x77};
    const uint8_t                   spi_write[4]  = {0x00, 0x0E, 0x12, 0x34};
    const uint8_t                   i2c_write[3]  = {0x00, 0x08, 0x09};

    rcp_ep_pwm_out_functional_cfg_init(&pwm_cfg);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_RECONFIG_OK,
                      rcp_ep_pwm_out_apply_reconfig(&pwm_cfg, pwm_write, sizeof(pwm_write)));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0008u, RCP_EP_PWM_OUT_REG_CLK_DIVIDER);
    TEST_ASSERT_EQUAL_HEX8(0x33, pwm_cfg.clk_divider);
    TEST_ASSERT_EQUAL_HEX8(0x05, pwm_cfg.signal_flags);

    rcp_ep_gpio_functional_cfg_init(&gpio_cfg);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0008u, RCP_EP_GPIO_REG_CLK_DIVIDER);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_OK,
                      rcp_ep_gpio_apply_reconfig(&gpio_cfg, gpio_write, sizeof(gpio_write)));
    TEST_ASSERT_EQUAL_HEX8(0x77, gpio_cfg.clk_divider);

    rcp_ep_spi_functional_cfg_init(&spi_cfg);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)0x000Eu,
        (uint16_t)(RCP_EP_SPI_REG_CHANNEL_BASE + 1u * RCP_EP_SPI_REG_CHANNEL_SPAN));
    TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_OK,
                      rcp_ep_spi_apply_reconfig(&spi_cfg, spi_write, sizeof(spi_write)));
    TEST_ASSERT_EQUAL_HEX16(0x1234, spi_cfg.channels[1].baud_rate_kbps);

    rcp_ep_i2c_functional_cfg_init(&i2c_cfg);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0008u, RCP_EP_I2C_REG_CLOCK_DIVIDER);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_OK,
                      rcp_ep_i2c_apply_reconfig(&i2c_cfg, i2c_write, sizeof(i2c_write)));
    TEST_ASSERT_EQUAL_HEX8(0x09, i2c_cfg.clock_divider);
}

/* TC18 §12.7.1 requires EVERY endpoint to publish EP_LEN at EP_func
 * relative address 0x0000 and to ignore, in its entirety, a write whose
 * start_address + length exceeds it. PWM_OUT satisfies this literally
 * (asserted here: EP_LEN lives at 0x0000, reports 0x0F, and a write of 2
 * octets at 0x000E is refused whole). FIXED 2026-08-11 (issue #256 Group
 * G, REQ-GPIO-013): GPIO now does too (EP_LEN at 0x0000 reports 0x29, a
 * write past it is refused whole). FIXED 2026-08-11 (issue #256 Group I,
 * REQ-SPI-035): SPI now does too (EP_LEN at 0x0000 reports 0x36, a write
 * at the block's own last valid offset is refused whole -- note SPI's
 * block additionally publishes spi_nr_cs at 0x0001, also read-only and
 * also exercised here). FIXED 2026-08-11 (issue #256 Group I,
 * REQ-I2C-019): I2C now does too (EP_LEN at 0x0000 reports 0x0B, a write
 * at the block's own last valid offset is refused whole). Deviation,
 * narrowed but not closed: `grep -rn EP_LEN src` now matches ep_pwm.c,
 * ep_gpio.c, ep_spi.c, and ep_i2c.c -- UART/LIN/CAN/ADC/ISELED/MDIO/
 * wakeup still define no EP_LEN register or overrun rule, because none
 * has an addressed EP_func write path at all (see the test above;
 * REQ-CFG-012 tracks the remaining 7). */
static void test_ep_len_overrun_rule_implemented_endpoints(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    uint8_t                         block[RCP_EP_PWM_OUT_EP_FUNC_LEN];
    const uint8_t                   overrun[4] = {0x00, 0x0E, 0xAA, 0xBB};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    cfg.skew = 0x42u;

    TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0000u, RCP_EP_PWM_OUT_REG_EP_LEN);
    rcp_ep_pwm_out_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_PWM_OUT_EP_FUNC_LEN, block[RCP_EP_PWM_OUT_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_HEX8(0x0F, block[RCP_EP_PWM_OUT_REG_EP_LEN]);

    /* 0x000E + 2 > 0x000F: the whole write is ignored, skew untouched. */
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE,
                      rcp_ep_pwm_out_apply_reconfig(&cfg, overrun, sizeof(overrun)));
    TEST_ASSERT_EQUAL_HEX8(0x42, cfg.skew);

    {
        rcp_ep_gpio_functional_cfg_t gpio_cfg;
        uint8_t                      gpio_block[RCP_EP_GPIO_EP_FUNC_LEN];
        /* 0x0028 (the last real register, gpio_debounce_IO31) + 2 octets
         * overruns 0x0029: the whole write is ignored. */
        const uint8_t                gpio_overrun[4] = {0x00, 0x28, 0xAA, 0xBB};

        rcp_ep_gpio_functional_cfg_init(&gpio_cfg);
        gpio_cfg.debounce[31] = 0x42u;

        TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0000u, RCP_EP_GPIO_REG_EP_LEN);
        rcp_ep_gpio_render_registers(&gpio_cfg, gpio_block);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_GPIO_EP_FUNC_LEN, gpio_block[RCP_EP_GPIO_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_HEX8(0x29, gpio_block[RCP_EP_GPIO_REG_EP_LEN]);

        TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_ERR_OUT_OF_RANGE,
                          rcp_ep_gpio_apply_reconfig(&gpio_cfg, gpio_overrun, sizeof(gpio_overrun)));
        TEST_ASSERT_EQUAL_HEX8(0x42, gpio_cfg.debounce[31]);
    }

    {
        rcp_ep_spi_functional_cfg_t spi_cfg;
        uint8_t                     spi_block[RCP_EP_SPI_EP_FUNC_LEN];
        /* 0x0036 == RCP_EP_SPI_EP_FUNC_LEN itself -- one past the last
         * valid offset, so even a single-octet write there overruns. */
        const uint8_t                spi_overrun[3] = {0x00, 0x36, 0xAA};

        rcp_ep_spi_functional_cfg_init(&spi_cfg);
        spi_cfg.channels[5].pause_min = 0x42u;

        TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0000u, RCP_EP_SPI_REG_EP_LEN);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0001u, RCP_EP_SPI_REG_NR_CS);
        rcp_ep_spi_render_registers(&spi_cfg, spi_block);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_SPI_EP_FUNC_LEN, spi_block[RCP_EP_SPI_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_HEX8(0x36, spi_block[RCP_EP_SPI_REG_EP_LEN]);
        /* TC18 0.5.1_RC5: spi_nr_cs is a 4-bit "(count - 1)" field, not a
         * plain count -- see ep_spi.h's own "FIXED 2026-08-11" note. */
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(RCP_EP_SPI_MAX_CHANNELS - 1u), spi_block[RCP_EP_SPI_REG_NR_CS]);

        TEST_ASSERT_EQUAL(RCP_EP_SPI_RECONFIG_ERR_OUT_OF_RANGE,
                          rcp_ep_spi_apply_reconfig(&spi_cfg, spi_overrun, sizeof(spi_overrun)));
        TEST_ASSERT_EQUAL_HEX8(0x42, spi_cfg.channels[5].pause_min);
    }

    {
        rcp_ep_i2c_functional_cfg_t i2c_cfg;
        uint8_t                     i2c_block[RCP_EP_I2C_EP_FUNC_LEN];
        /* 0x000B == RCP_EP_I2C_EP_FUNC_LEN itself -- one past the last
         * valid offset, so even a single-octet write there overruns. */
        const uint8_t                i2c_overrun[3] = {0x00, 0x0B, 0xAA};

        rcp_ep_i2c_functional_cfg_init(&i2c_cfg);
        i2c_cfg.trail = 0x42u;

        TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0000u, RCP_EP_I2C_REG_EP_LEN);
        rcp_ep_i2c_render_registers(&i2c_cfg, i2c_block);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_I2C_EP_FUNC_LEN, i2c_block[RCP_EP_I2C_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_HEX8(0x0B, i2c_block[RCP_EP_I2C_REG_EP_LEN]);

        TEST_ASSERT_EQUAL(RCP_EP_I2C_RECONFIG_ERR_OUT_OF_RANGE,
                          rcp_ep_i2c_apply_reconfig(&i2c_cfg, i2c_overrun, sizeof(i2c_overrun)));
        TEST_ASSERT_EQUAL_HEX8(0x42, i2c_cfg.trail);
    }
}

/* ── §12.3: discovery-stream occupancy ─────────────────────────────────────── */

/* TC18 §12.3 / Figure 16: a discovery request arriving while the
 * discovery stream is already claimed is answered with a stream-occupied
 * error. FIXED (REQ-DISC-029): rcp_discovery_claim_note_request() now
 * returns bool -- true when the claim was open and granted, false when
 * refused because it was already held by an unlapsed claimant (Figure
 * 16's own two "Discovery request received" transitions carve out no
 * exception for requester identity, so this applies uniformly whether a
 * different client or the current claimant itself re-requests). STILL
 * OPEN: DISCOVERY_STREAM_OCCUPIED is a Figure-16-diagram-only label with
 * no corresponding numbered code in TC18 §12.9.6 Table 27
 * (rcp_wire_error_t stops at RCP_ERROR_CHAIN_ERROR, 17) -- unlike
 * LOCKED_CONFIG_ACCESS (which cleanly maps onto RCP_ERROR_LOCKED_MEM_ACCESS),
 * no numbered code here has an obviously matching meaning, so this
 * codebase does not invent one; a caller has a real bool signal to act
 * on, but which wire error code (if any) to send is left genuinely
 * unresolved. */
static void test_discovery_claim_refusal_now_returns_a_real_signal(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t       a = rcp_stream_id_make(CLIENT_A_MAC, 1);
    rcp_stream_id_t       b = rcp_stream_id_make(CLIENT_B_MAC, 2);

    rcp_discovery_claim_init(&claim, RCP_DISCOVERY_DEFAULT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT32(20u, claim.timeout_ms);

    TEST_ASSERT_TRUE(rcp_discovery_claim_note_request(&claim, a, 100u));
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 105u));

    /* B's request is refused -- the caller now has a real signal to act on. */
    TEST_ASSERT_FALSE(rcp_discovery_claim_note_request(&claim, b, 105u));
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 106u));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, b, 106u));

    /* A re-requesting while still the claimant is ALSO refused (Figure
     * 16's own text carves out no requester-identity exception) -- and
     * still does not itself refresh the deadline, matching
     * rcp_discovery_claim_note_config_write()'s own separate mechanism. */
    TEST_ASSERT_FALSE(rcp_discovery_claim_note_request(&claim, a, 110u));

    /* 17 is still the last assigned wire error; 18 -- where a
     * DISCOVERY_STREAM_OCCUPIED code would sit -- is still not assigned. */
    TEST_ASSERT_EQUAL_INT(17, (int)RCP_ERROR_CHAIN_ERROR);
    TEST_ASSERT_EQUAL_STRING(rcp_wire_error_string((rcp_wire_error_t)19),
                             rcp_wire_error_string((rcp_wire_error_t)18));
}

/* ── §12.7.5 Table 18: the RC Server general (static) register map ────────── */

/* REQ-RMAP-023 (TC18 §12.3.1.1/§12.3.1.2/§12.4.1): the lifecycle state
 * must be exposed as the svr_lifecycle_state register-map entry.
 * rcp_regmap_general_t now carries that field (content modeling only --
 * this is NOT REQ-RMAP-024's own separate wire-reachability concern,
 * which stays open; see test_general_map_wire_reach_stops_after_0x000d()
 * below, unaffected by this field since it isn't wired into the
 * discovery slice). Proves the field's own default and that
 * rcp_mock_server_transition() -- the one caller composing
 * rcp_lifecycle_transition() with a regmap.h general block today --
 * keeps it in sync with the authoritative rcp_lifecycle_state_t on every
 * transition, success or failure. */
static void test_lifecycle_state_register_field_tracks_the_authoritative_state(void)
{
    rcp_mock_server_t                          *srv;
    rcp_lifecycle_endpoint_plausibility_t       eps[1] = {{true, true, true, true}};
    rcp_lifecycle_request_stream_plausibility_t rs[1]  = {{true, true, 0}};
    rcp_lifecycle_plausibility_snapshot_t       snap;
    rcp_lifecycle_writer_ctx_t                  writer = ROOT_WRITER;

    snap.endpoints             = eps;
    snap.endpoint_count        = 1u;
    snap.request_streams       = rs;
    snap.request_stream_count  = 1u;
    snap.response_stream_count = 1u; /* REQ-RMAP-049: unused by this test's own HW_CONFIGURED-only
                                         transition (check_hw_cfg, not check_rcp_cfg), set for hygiene */

    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)RCP_LIFECYCLE_HW_UNCONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0x55, (uint8_t)RCP_LIFECYCLE_HW_CONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0xAA, (uint8_t)RCP_LIFECYCLE_RCP_CONFIGURED);

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    /* A freshly-constructed server's regmap field already matches its
     * lifecycle state's own default -- both are HW_UNCONFIGURED (0) by
     * construction, not by an extra sync call. */
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_LIFECYCLE_HW_UNCONFIGURED,
                           rcp_mock_server_regmap(srv)->svr_lifecycle_state);

    /* A rejected transition (HW_UNCONFIGURED -> RCP_CONFIGURED skips a
     * state -- not a modeled edge regardless of writer or idleness)
     * leaves both fields exactly where they already were -- the sync
     * call is unconditional but never introduces drift on a failure
     * path. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, writer, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, rcp_mock_server_state(srv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_LIFECYCLE_HW_UNCONFIGURED,
                           rcp_mock_server_regmap(srv)->svr_lifecycle_state);

    /* A successful transition updates both. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &snap, writer, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, rcp_mock_server_state(srv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_LIFECYCLE_HW_CONFIGURED,
                           rcp_mock_server_regmap(srv)->svr_lifecycle_state);

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-024 CLOSED: TC18 §12.7 requires EP0 to be a fully addressable
 * register space. rcp_regmap_general_encode_read_response()/
 * _decode_read_response() (regmap.h) are the real wire codec that closes
 * this -- the same ACF_ABB read mechanism rcp_discovery_encode_response()
 * already used for its own narrower 14-octet slice, generalized to serve
 * every Table 18 field at its own documented address. Proves the FULL
 * extent round-trips correctly against a map in which every field holds a
 * distinctive nonzero value, and that the two fields with no genuine
 * Table 18 address (svr_lifecycle_state, svr_root_client_index) are
 * deliberately left untouched by the decode, not silently zeroed or
 * mis-addressed -- see rcp_regmap_general_render()'s own doc comment. */
/* Direct byte-offset spot-check of rcp_regmap_general_render()'s own
 * output -- distinct from the round-trip test below, and deliberately so:
 * a round trip through the decoder alone would not catch a render bug
 * that writes a real field's value into the wrong slot (e.g. the
 * unnamed, deliberately-excluded 0x002B gap, or over svr_lifecycle_
 * state's own would-be slot at the 0x000D..0x000E boundary) if the
 * decoder itself never reads that slot either -- only inspecting the
 * raw rendered bytes at their exact TC18-cited addresses proves the
 * layout, not just that decode(encode(x)) happens to equal x. */
static void test_general_map_render_matches_table_18_byte_offsets(void)
{
    rcp_regmap_general_t map = populated_map();
    uint8_t              img[RCP_REGMAP_GENERAL_LEN];

    rcp_regmap_general_render(&map, img);

    TEST_ASSERT_EQUAL_HEX32(map.magic, get_test_u32(&img[0x0000]));
    TEST_ASSERT_EQUAL_HEX32(map.svr_version, get_test_u32(&img[0x0004]));
    TEST_ASSERT_EQUAL_HEX16(map.vendor_id, get_test_u16(&img[0x0008]));
    TEST_ASSERT_EQUAL_HEX16(map.device_id, get_test_u16(&img[0x000A]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_count, get_test_u16(&img[0x000C]));
    /* svr_lifecycle_state has no Table 18 slot -- 0x000E is
     * svr_req_stream_max's own address, immediately after svr_ep_count's
     * 16 bits, with no gap for it. */
    TEST_ASSERT_EQUAL_HEX8(map.svr_req_stream_max, img[0x000E]);
    TEST_ASSERT_EQUAL_HEX8(map.svr_responder_streams_max, img[0x000F]);
    TEST_ASSERT_EQUAL_HEX16(map.svr_responder_mem_size, get_test_u16(&img[0x0010]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_req_mem_size, get_test_u16(&img[0x0012]));
    TEST_ASSERT_EQUAL_HEX8(map.svr_sequencers_max, img[0x0014]);
    TEST_ASSERT_EQUAL_HEX8(map.svr_configuration_lock, img[0x0015]);
    TEST_ASSERT_EQUAL_HEX8(map.svr_implemented_options, img[0x0016]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, img[0x0017]);
    TEST_ASSERT_EQUAL_HEX16(map.svr_io_pin_count, get_test_u16(&img[0x0018]));
    /* svr_root_client_index has no Table 18 slot either -- 0x001A is
     * svr_hw_cfg_ptr's own address, immediately after svr_io_pin_count's
     * 16 bits, with no gap for it. */
    TEST_ASSERT_EQUAL_HEX16(map.svr_hw_cfg_ptr, get_test_u16(&img[0x001A]));
    TEST_ASSERT_EQUAL_HEX8(map.svr_request_stream_cfg_capacity, img[0x001C]);
    TEST_ASSERT_EQUAL_HEX8(map.svr_response_stream_cfg_capacity, img[0x001D]);
    TEST_ASSERT_EQUAL_HEX16(map.svr_request_stream_cfg_ptr, get_test_u16(&img[0x001E]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_response_stream_cfg_ptr, get_test_u16(&img[0x0020]));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, get_test_u16(&img[0x0022]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_generic_cfg_ptr, get_test_u16(&img[0x0024]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_generic_cfg_capacity, get_test_u16(&img[0x0026]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_bytebus_id_map_ptr, get_test_u16(&img[0x0028]));
    TEST_ASSERT_EQUAL_HEX8(map.svr_ep_bytebus_id_map_capacity, img[0x002A]);
    /* The inferred, unconfirmed alignment gap -- see
     * rcp_regmap_general_render()'s own doc comment. */
    TEST_ASSERT_EQUAL_HEX8(0x00u, img[0x002B]);
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_functional_cfg_ptr, get_test_u16(&img[0x002C]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_sequencer_state_ptr, get_test_u16(&img[0x002E]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_network_interface_cfg_ptr, get_test_u16(&img[0x0030]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_network_interface_cfg_capacity, get_test_u16(&img[0x0032]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_physical_layer_cfg_ptr, get_test_u16(&img[0x0034]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_physical_layer_cfg_capacity, get_test_u16(&img[0x0036]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_time_synch_cfg_ptr, get_test_u16(&img[0x0038]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_time_synch_cfg_capacity, get_test_u16(&img[0x003A]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_security_cfg_ptr, get_test_u16(&img[0x003C]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_security_cfg_capacity, get_test_u16(&img[0x003E]));
    /* REQ-RMAP-039 (issue #429): Table 20's own real last pair, not
     * svr_security_cfg_capacity above. */
    TEST_ASSERT_EQUAL_HEX16(map.svr_device_specific_cfg_ptr, get_test_u16(&img[0x0040]));
    TEST_ASSERT_EQUAL_HEX16(map.svr_device_specific_cfg_capacity, get_test_u16(&img[0x0042]));
}

static void test_general_map_wire_reach_now_covers_full_table_18(void)
{
    rcp_regmap_general_t      map = populated_map();
    rcp_regmap_general_t      out;
    rcp_regmap_general_errc_t rc;

    rcp_regmap_general_init(&out);
    out.svr_root_client_index = 0x0009u; /* poisoned -- decode must not touch it */
    out.svr_lifecycle_state   = (uint8_t)RCP_LIFECYCLE_HW_CONFIGURED; /* likewise */

    rc = read_general_full(&map, (uint8_t)RCP_REGMAP_GENERAL_LEN, &out);
    TEST_ASSERT_EQUAL(RCP_REGMAP_GENERAL_OK, rc);

    TEST_ASSERT_EQUAL_HEX32(map.magic, out.magic);
    TEST_ASSERT_EQUAL_HEX32(map.svr_version, out.svr_version);
    TEST_ASSERT_EQUAL_HEX16(map.vendor_id, out.vendor_id);
    TEST_ASSERT_EQUAL_HEX16(map.device_id, out.device_id);
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_count, out.svr_ep_count);
    TEST_ASSERT_EQUAL_UINT8(map.svr_req_stream_max, out.svr_req_stream_max);
    TEST_ASSERT_EQUAL_UINT8(map.svr_responder_streams_max, out.svr_responder_streams_max);
    TEST_ASSERT_EQUAL_HEX16(map.svr_responder_mem_size, out.svr_responder_mem_size);
    TEST_ASSERT_EQUAL_HEX16(map.svr_req_mem_size, out.svr_req_mem_size);
    TEST_ASSERT_EQUAL_UINT8(map.svr_sequencers_max, out.svr_sequencers_max);
    TEST_ASSERT_EQUAL_UINT8(map.svr_configuration_lock, out.svr_configuration_lock);
    TEST_ASSERT_EQUAL_UINT8(map.svr_implemented_options, out.svr_implemented_options);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out.reserved_0x17);
    TEST_ASSERT_EQUAL_HEX16(map.svr_io_pin_count, out.svr_io_pin_count);
    TEST_ASSERT_EQUAL_HEX16(map.svr_hw_cfg_ptr, out.svr_hw_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT8(map.svr_request_stream_cfg_capacity, out.svr_request_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT8(map.svr_response_stream_cfg_capacity, out.svr_response_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_HEX16(map.svr_request_stream_cfg_ptr, out.svr_request_stream_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_response_stream_cfg_ptr, out.svr_response_stream_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, out.reserved_0x22);
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_generic_cfg_ptr, out.svr_ep_generic_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_generic_cfg_capacity, out.svr_ep_generic_cfg_capacity);
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_bytebus_id_map_ptr, out.svr_ep_bytebus_id_map_ptr);
    TEST_ASSERT_EQUAL_UINT8(map.svr_ep_bytebus_id_map_capacity, out.svr_ep_bytebus_id_map_capacity);
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_functional_cfg_ptr, out.svr_ep_functional_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_sequencer_state_ptr, out.svr_sequencer_state_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_network_interface_cfg_ptr, out.svr_network_interface_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_network_interface_cfg_capacity, out.svr_network_interface_cfg_capacity);
    TEST_ASSERT_EQUAL_HEX16(map.svr_physical_layer_cfg_ptr, out.svr_physical_layer_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_physical_layer_cfg_capacity, out.svr_physical_layer_cfg_capacity);
    TEST_ASSERT_EQUAL_HEX16(map.svr_time_synch_cfg_ptr, out.svr_time_synch_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_time_synch_cfg_capacity, out.svr_time_synch_cfg_capacity);
    TEST_ASSERT_EQUAL_HEX16(map.svr_security_cfg_ptr, out.svr_security_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_security_cfg_capacity, out.svr_security_cfg_capacity);
    /* REQ-RMAP-039 (issue #429): a full-table (RCP_REGMAP_GENERAL_LEN)
     * read now genuinely reaches Table 20's own real last pair -- these
     * two fields used to be entirely unreachable over the wire. */
    TEST_ASSERT_EQUAL_HEX16(map.svr_device_specific_cfg_ptr, out.svr_device_specific_cfg_ptr);
    TEST_ASSERT_EQUAL_HEX16(map.svr_device_specific_cfg_capacity,
                             out.svr_device_specific_cfg_capacity);

    /* Deliberately unpopulated fields survive the decode untouched --
     * proof the exclusion is real, not a coincidental zero. */
    TEST_ASSERT_EQUAL_HEX16(0x0009u, out.svr_root_client_index);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_LIFECYCLE_HW_CONFIGURED, out.svr_lifecycle_state);
}

/* A read_size shorter than RCP_REGMAP_GENERAL_LEN carries only a prefix
 * of the map -- fields beyond what fits are left exactly as the caller's
 * *out already had them, matching rcp_regmap_general_decode_read_response()'s
 * own doc comment (the same "short response, partial population" contract
 * every register-block apply_reconfig() in this codebase already
 * follows). Also proves discovery's own narrower 14-octet slice
 * (RCP_DISCOVERY_GENERAL_SLICE_LEN) still equals the read_size boundary
 * this test uses for its own short-response case, so the two codecs stay
 * mutually consistent. */
static void test_general_map_short_read_size_leaves_the_remainder_untouched(void)
{
    rcp_regmap_general_t      map = populated_map();
    rcp_regmap_general_t      out;
    rcp_regmap_general_errc_t rc;

    TEST_ASSERT_EQUAL_UINT((size_t)14u, RCP_DISCOVERY_GENERAL_SLICE_LEN);

    rcp_regmap_general_init(&out);
    out.svr_req_stream_max = 0x5Au; /* poisoned -- past the 14-octet slice */

    rc = read_general_full(&map, (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, &out);
    TEST_ASSERT_EQUAL(RCP_REGMAP_GENERAL_OK, rc);

    /* The 5-field discovery-identity prefix IS carried by a 14-octet
     * response and IS updated. */
    TEST_ASSERT_EQUAL_HEX32(map.magic, out.magic);
    TEST_ASSERT_EQUAL_HEX16(map.svr_ep_count, out.svr_ep_count);
    /* svr_req_stream_max (0x000E) falls one octet past the 14-octet
     * (0x0000..0x000D) slice -- left exactly as poisoned. */
    TEST_ASSERT_EQUAL_UINT8(0x5Au, out.svr_req_stream_max);
}

//cfusa:test REQ-RMAP-024
static void test_general_map_strerror_never_null_and_distinct(void)
{
    rcp_regmap_general_errc_t e;

    for (e = RCP_REGMAP_GENERAL_OK; e <= RCP_REGMAP_GENERAL_ERR_WRONG_OP;
         e = (rcp_regmap_general_errc_t)((int)e + 1)) {
        TEST_ASSERT_NOT_NULL(rcp_regmap_general_strerror(e));
    }
    TEST_ASSERT_NOT_EQUAL(0, strcmp(rcp_regmap_general_strerror(RCP_REGMAP_GENERAL_ERR_WRONG_BUS),
                                     rcp_regmap_general_strerror(RCP_REGMAP_GENERAL_ERR_WRONG_OP)));
}

//cfusa:test REQ-RMAP-024
static void test_general_map_read_response_decode_rejects_malformed_frames(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_regmap_general_t        out;
    uint8_t                     short_frame[2] = {0, 0};

    TEST_ASSERT_EQUAL(RCP_REGMAP_GENERAL_ERR_SHORT_FRAME,
                      rcp_regmap_general_decode_read_response(short_frame, sizeof(short_frame), &out));

    /* Wrong bus: byte_bus_id != EP0. */
    hdr.byte_bus_id     = 3;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.rsp             = 1;
    hdr.transaction_num = 1;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_REGMAP_GENERAL_ERR_WRONG_BUS,
                      rcp_regmap_general_decode_read_response(frame.data, frame.len, &out));
    rcp_bytes_free(&frame);

    /* Wrong op: a WRITE frame is not a read response. */
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_REGMAP_GENERAL_ERR_WRONG_OP,
                      rcp_regmap_general_decode_read_response(frame.data, frame.len, &out));
    rcp_bytes_free(&frame);
}

/* REQ-RMAP-025 CLOSED: TC18 §12.7.5 Table 18, access type R -- a remote
 * write to the general static part must not take effect. lifecycle.h's
 * RCP_LIFECYCLE_FIELD_READ_ONLY (proven unconditionally unwritable in
 * every state by every writer, test_lifecycle.c) is now actually
 * consulted by a real wire-dispatch decode path:
 * rcp_regmap_general_decode_write_request() (regmap.h) recognizes any
 * ACF_ABB WRITE addressed to EP0 and reports RCP_ERROR_LOCKED_MEM_ACCESS
 * every time -- proven directly below, reusing (not duplicating)
 * rcp_lifecycle_field_write_error()'s own already-tested primitive. The
 * three other field kinds stay writable in at least one state (asserted
 * for contrast, unchanged from this test's own prior form) -- READ_ONLY
 * is the one kind for which no state/writer combination ever succeeds. */
static void test_general_static_part_write_attempts_are_now_rejected_over_the_wire(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                  frame;
    rcp_wire_error_t              err;
    uint8_t                       tn;
    rcp_regmap_general_errc_t     rc;
    uint8_t                       payload[2] = {0xBE, 0xEF};

    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_HW_GENERIC, DISCOVERY_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_READ_ONLY, ROOT_WRITER));

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 9;

    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_regmap_general_decode_write_request(frame.data, frame.len, &err, &tn);
    TEST_ASSERT_EQUAL(RCP_REGMAP_GENERAL_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
    TEST_ASSERT_EQUAL_UINT8(9u, tn);

    rcp_bytes_free(&frame);
}

/* rcp_mock_server_regmap() itself still hands back a directly mutable
 * in-process pointer -- a deliberate, separate design choice for this
 * test double's own convenience API (setting up fixture state), not a
 * remaining conformance gap: no code path routes a REAL wire write
 * through that pointer -- the wire path is
 * rcp_regmap_general_decode_write_request() above, which never applies
 * a write at all. Kept as its own test, distinct from the wire-rejection
 * one above, so the two concerns are never conflated. */
static void test_mock_server_regmap_pointer_is_still_directly_mutable_in_process(void)
{
    rcp_mock_server_t    *srv;
    rcp_regmap_general_t *map;

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    map = rcp_mock_server_regmap(srv);
    TEST_ASSERT_NOT_NULL(map);
    map->vendor_id = 0xBEEFu;
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, rcp_mock_server_regmap(srv)->vendor_id);
    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-026 (TC18 §12.7.5 Table 18): svr_req_stream_max is an 8-bit R
 * register at absolute address 0x000E and svr_responder_streams_max an
 * 8-bit R register at 0x000F. rcp_regmap_general_t now carries both at
 * the correct width -- uint8_t, so a value neither register could hold
 * on the wire (e.g. 256) can no longer be constructed in the first
 * place, and svr_responder_streams_max exists at all. Now wire-reachable
 * (REQ-RMAP-024 CLOSED, proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). */
static void test_req_stream_max_and_responder_streams_max_are_now_correctly_sized(void)
{
    rcp_regmap_general_t map = populated_map();

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_req_stream_max));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_responder_streams_max));

    map.svr_req_stream_max = 0xFFu; /* the widest value an 8-bit
                                        register can hold, accepted */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, map.svr_req_stream_max);

    map.svr_responder_streams_max = 0xFFu;
    TEST_ASSERT_EQUAL_UINT8(0xFFu, map.svr_responder_streams_max);

    /* The slice ends at 0x000D, so 0x000E (svr_req_stream_max's own
     * address) is the first still-unreachable one -- REQ-RMAP-024's own
     * boundary, generically covered by
     * test_general_map_wire_reach_stops_after_0x000d() too. */
    TEST_ASSERT_EQUAL_UINT((size_t)0x0Eu, RCP_DISCOVERY_GENERAL_SLICE_LEN);
}

/* REQ-RMAP-027 (TC18 §12.7.5 Table 18): two distinct 16-bit capacity
 * registers, both counted in 32-bit words: svr_responder_mem_size at
 * 0x0010 and svr_req_mem_size at 0x0012. rcp_regmap_general_t now
 * carries both, distinctly addressed and separately settable, replacing
 * the former undifferentiated 32-bit svr_memory_capacity that conflated
 * them into one unaddressed field. Now wire-reachable (REQ-RMAP-024
 * CLOSED, proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). */
static void test_responder_and_req_mem_size_are_now_distinctly_addressed(void)
{
    rcp_regmap_general_t map = populated_map();

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_responder_mem_size));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_req_mem_size));

    /* Distinctly set and distinctly readable in-process -- the two
     * limits can now be told apart, unlike the former single field. */
    TEST_ASSERT_EQUAL_HEX16(0x1122u, map.svr_responder_mem_size);
    TEST_ASSERT_EQUAL_HEX16(0x3344u, map.svr_req_mem_size);

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_HEX16(0u, map.svr_responder_mem_size);
    TEST_ASSERT_EQUAL_HEX16(0u, map.svr_req_mem_size);
}

/* REQ-RMAP-028 (TC18 §12.7.5 Table 18): svr_sequencers_max is an 8-bit R
 * register at 0x0014 whose value 0 means "sequencer operation not
 * supported"; 1..n gives the number of available sequencer state
 * registers. rcp_regmap_general_t now carries this field at the correct
 * width, and mock.c's rcp_mock_server_set_sequencer_count() keeps it
 * synced with the actual rcp_sequencer_table_t.count it allocates --
 * request_sequencer.h's own rcp_sequencer_table_unsupported()
 * (table->count == 0) already implements the "0 means unsupported"
 * rule this register describes, so once synced, the register correctly
 * reflects that same rule by construction. Still open (REQ-RMAP-024,
 * same as every other Group 1 item): the address falls past the
 * discovery slice's 0x000D ceiling, already covered generically by
 * test_general_map_wire_reach_stops_after_0x000d(). */
static void test_sequencers_max_is_now_correctly_sized_and_synced_from_the_table(void)
{
    rcp_mock_server_t *srv;

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(((rcp_regmap_general_t *)0)->svr_sequencers_max));

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);

    /* A freshly-constructed server has no sequencers: the register
     * already reads 0 ("not supported"), matching
     * rcp_sequencer_table_unsupported()'s own verdict on the table it
     * was never given a count for. */
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_mock_server_regmap(srv)->svr_sequencers_max);
    TEST_ASSERT_TRUE(rcp_sequencer_table_unsupported(rcp_mock_server_sequencers(srv)));

    /* Setting a nonzero count updates both the table and the register in
     * lockstep. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 7u));
    TEST_ASSERT_EQUAL_UINT8(7u, rcp_mock_server_regmap(srv)->svr_sequencers_max);
    TEST_ASSERT_FALSE(rcp_sequencer_table_unsupported(rcp_mock_server_sequencers(srv)));

    /* Setting the count back to 0 re-establishes "not supported" in both
     * places -- the exact deviation the old pin demonstrated (7 and
     * "unsupported" being indistinguishable) is now impossible. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_mock_server_regmap(srv)->svr_sequencers_max);
    TEST_ASSERT_TRUE(rcp_sequencer_table_unsupported(rcp_mock_server_sequencers(srv)));

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-029 (TC18 §12.7.5 Table 18): svr_configuration_lock is an
 * 8-bit R register at 0x0015. rcp_regmap_general_t now carries this
 * field (content modeling only), zero-initializing to 0x00 ("permits
 * write access to R/W+ parameters" -- the correct unlocked default).
 * Proves the field exists, defaults correctly, and round-trips a
 * nonzero (locked) value in-process. Still open (REQ-RMAP-024, same as
 * every other Group 1 item): the address falls past the discovery
 * slice's 0x000D ceiling. */
static void test_configuration_lock_register_now_exists_and_defaults_unlocked(void)
{
    rcp_regmap_general_t map;

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_configuration_lock));
    TEST_ASSERT_EQUAL_HEX8(0x00u, map.svr_configuration_lock); /* unlocked default */

    map.svr_configuration_lock = 0x01u; /* any nonzero value means locked */
    TEST_ASSERT_EQUAL_HEX8(0x01u, map.svr_configuration_lock);
}

/* rcp_lifecycle_field_writable() still takes no lock input, so its
 * verdict for an R/W+ (FUNCTIONAL_W_STAR) field depends only on state
 * and writer authorization, never on the new svr_configuration_lock
 * byte -- asserted here by two differently-authorized writer contexts
 * (root-client-via-EP0 vs. owning-stream) producing the identical TRUE
 * answer in HW_CONFIGURED, where a real lock register would still have
 * to refuse regardless of which authorized path granted access. (As of
 * REQ-LIFECYCLE-030/036, an *unauthorized* writer is now correctly
 * refused here too -- that is the authorization gate working as
 * intended, not a lock register; see PLAIN_WRITER's own FALSE assertion
 * below, kept to document the distinction rather than silently
 * dropped.) Deliberately still open: REQ-RMAP-055 (issue #200 Group 3)
 * is this codebase's own already-identified "shared plumbing, implement
 * once" home for the W+ lockable-access-type primitive this register
 * drives -- see regmap.h's own svr_configuration_lock field comment for
 * the full reasoning. Not re-tested for wire-reachability here (already
 * covered generically by
 * test_general_map_wire_reach_stops_after_0x000d()). */
static void test_configuration_lock_not_yet_consulted_by_field_writable(void)
{
    const rcp_lifecycle_writer_ctx_t owning_stream_writer = {false, true, false, false};

    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  owning_stream_writer));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                   PLAIN_WRITER));
}

/* REQ-RMAP-030 (TC18 §12.7.5 Table 18): svr_implemented_options is an
 * 8-bit R register at 0x0016 with one bit per optional feature (a
 * compound&wait, b trigger requests, c chained requests, d time
 * synch/timed, e enhanced cancellation) -- verified directly against
 * the primary-source PDF (Table 18, page 51 of
 * OA_TC18_specification_v_0.5.1_RC.pdf). rcp_regmap_general_t's field
 * is now 8 bit and carries exactly these five independent bits,
 * replacing a prior 32-bit, six-bit, three-forced-pair design
 * (REQ-RMAP-004..008, now retired -- see test_regmap.c's own
 * retirement note) whose citation for the pairing rule (§12.9.1.1) was
 * found, on the same primary-source check, to say nothing about this
 * register at all: that section is entirely about handling multiple
 * ACF-type requests packed into one AVTPDU frame. The prior design also
 * had no bit at all for trigger or chained requests, even though this
 * codebase implements both -- this test proves that gap is closed too.
 * Still open (REQ-RMAP-024, same as every other Group 1 item): the
 * address falls past the discovery slice's 0x000D ceiling, already
 * covered generically by test_general_map_wire_reach_stops_after_0x000d(). */
static void test_implemented_options_now_matches_table_18_exactly(void)
{
    rcp_regmap_general_t map;

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_implemented_options));

    TEST_ASSERT_EQUAL_HEX8(0x01u, RCP_REGMAP_OPT_COMPOUND_WAIT); /* a */
    TEST_ASSERT_EQUAL_HEX8(0x02u, RCP_REGMAP_OPT_TRIGGER);       /* b -- previously unrepresentable */
    TEST_ASSERT_EQUAL_HEX8(0x04u, RCP_REGMAP_OPT_CHAINED);       /* c -- previously unrepresentable */
    TEST_ASSERT_EQUAL_HEX8(0x08u, RCP_REGMAP_OPT_TIME_SYNC);     /* d */
    TEST_ASSERT_EQUAL_HEX8(0x10u, RCP_REGMAP_OPT_ENH_CANCEL);    /* e */

    /* Every bit is independently settable -- no pairing invariant to
     * satisfy, unlike the retired design. */
    map.svr_implemented_options = RCP_REGMAP_OPT_TRIGGER;
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_OPT_TRIGGER, map.svr_implemented_options);

    map.svr_implemented_options = (uint8_t)(RCP_REGMAP_OPT_COMPOUND_WAIT | RCP_REGMAP_OPT_TRIGGER |
                                             RCP_REGMAP_OPT_CHAINED | RCP_REGMAP_OPT_TIME_SYNC |
                                             RCP_REGMAP_OPT_ENH_CANCEL);
    TEST_ASSERT_EQUAL_HEX8(0x1Fu, map.svr_implemented_options); /* all five, matching Table 18's own count */
}

/* REQ-RMAP-031 (TC18 §12.7.5 Table 18): the 8-bit register at 0x0017 is
 * reserved and must read 0x00. rcp_regmap_general_t now explicitly
 * models this octet (reserved_0x17) rather than leaving it implicitly
 * absent -- it zero-inits for free via rcp_regmap_general_init()'s own
 * memset, and no setter exists anywhere in this codebase to construct a
 * nonzero value. Wire-reachability (REQ-RMAP-024) is CLOSED -- proven
 * generically by test_general_map_wire_reach_now_covers_full_table_18()'s
 * own `TEST_ASSERT_EQUAL_UINT8(0x00u, out.reserved_0x17)` assertion, not
 * re-tested here. */
static void test_reserved_octet_at_0x17_is_now_explicitly_modeled(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.reserved_0x17));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT8(0x00u, map.reserved_0x17);
}

/* REQ-RMAP-035 (TC18 §12.7.5 Table 18): the 16-bit register at 0x0022
 * is reserved and must read 0x00. rcp_regmap_general_t now explicitly
 * models this span (reserved_0x22) rather than leaving it implicitly
 * absent -- it zero-inits for free via rcp_regmap_general_init()'s own
 * memset, and no setter exists anywhere in this codebase to construct
 * a nonzero value. Wire-reachability (REQ-RMAP-024) is CLOSED -- proven
 * generically by test_general_map_wire_reach_now_covers_full_table_18()'s
 * own `TEST_ASSERT_EQUAL_HEX16(0x0000u, out.reserved_0x22)` assertion,
 * not re-tested here. */
static void test_reserved_register_at_0x22_is_now_explicitly_modeled(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.reserved_0x22));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.reserved_0x22);
}

/* REQ-RMAP-032 (TC18 §12.7.5 Table 18): svr_io_pin_count is a 16-bit R
 * register at 0x0018, the §12.7.6-authoritative extent of the HW_config
 * table (Table 19). rcp_regmap_general_t now declares this field, and
 * it is now wire-reachable (REQ-RMAP-024 CLOSED, proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). Still open: nothing in this codebase yet allocates or bounds a
 * real HW_config table against it (Group 2's own scope, issue #200
 * items -040 through -045) -- content modeling, wire-reachable, but no
 * real HW_config table to point at yet. */
static void test_io_pin_count_is_now_explicitly_modeled(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_io_pin_count));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_io_pin_count);
    map.svr_io_pin_count = 0x0020u;
    TEST_ASSERT_EQUAL_UINT16(0x0020u, map.svr_io_pin_count);
}

/* REQ-RMAP-033 (TC18 §12.7.5 Table 18): svr_hw_cfg_ptr is a 16-bit R
 * pointer at 0x001A, the address of the HW_config register map
 * (§12.7.6). rcp_regmap_general_t now declares this field at its
 * correct 16-bit width, with no spurious capacity member -- HW_config's
 * extent comes from svr_io_pin_count (REQ-RMAP-032) instead, so a
 * bundled capacity would have been a second, contradictory source of
 * truth for the table's length. Now wire-reachable (REQ-RMAP-024
 * CLOSED, proven generically, not re-tested here). Still open: this
 * codebase has no real HW_config table storage anywhere yet (Group 2's
 * own separate scope, REQ-RMAP-040 through -045) for this pointer to
 * meaningfully address. */
static void test_hw_cfg_ptr_is_now_correctly_shaped(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_hw_cfg_ptr));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_hw_cfg_ptr);
    map.svr_hw_cfg_ptr = 0x0060u;
    TEST_ASSERT_EQUAL_UINT16(0x0060u, map.svr_hw_cfg_ptr);
}

/* REQ-RMAP-034 (TC18 §12.7.5 Table 18): FOUR separate, non-adjacent
 * registers exist for the stream-configuration sub-tables:
 * svr_request_stream_cfg_capacity (8 bit, 0x001C),
 * svr_response_stream_cfg_capacity (8 bit, 0x001D),
 * svr_request_stream_cfg_ptr (16 bit, 0x001E) and
 * svr_response_stream_cfg_ptr (16 bit, 0x0020). rcp_regmap_general_t
 * now declares these as four correctly-sized scalar fields, replacing
 * the former request_stream_cfg/response_queue_cfg pair
 * (rcp_regmap_table_ref_t, the shared pointer/capacity type most
 * sub-table refs below still use) -- the same class of shape mismatch
 * REQ-RMAP-033 fixed for svr_hw_cfg_ptr, here doubled across both
 * stream directions. A capacity value TC18's real 8-bit registers
 * could never hold (e.g. 0x0100) is now impossible to construct in
 * the first place, rather than merely untrue-but-representable. Now
 * wire-reachable (REQ-RMAP-024 CLOSED, proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). Still open: this codebase has no real request/response-stream
 * config table storage anywhere yet for these pointers to meaningfully
 * address (Group 3/Group 4's own separate scope). */
static void test_stream_cfg_registers_are_now_correctly_sized(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_request_stream_cfg_capacity));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_response_stream_cfg_capacity));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_request_stream_cfg_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_response_stream_cfg_ptr));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT8(0x00u, map.svr_request_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT8(0x00u, map.svr_response_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_request_stream_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_response_stream_cfg_ptr);

    map.svr_request_stream_cfg_capacity  = 0x08u;
    map.svr_response_stream_cfg_capacity = 0x04u;
    map.svr_request_stream_cfg_ptr       = 0x0070u;
    map.svr_response_stream_cfg_ptr      = 0x0090u;
    TEST_ASSERT_EQUAL_UINT8(0x08u, map.svr_request_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT8(0x04u, map.svr_response_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0x0070u, map.svr_request_stream_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0090u, map.svr_response_stream_cfg_ptr);
}

/* REQ-RMAP-036 (TC18 §12.7.5 Table 18): svr_ep_generic_cfg_ptr (16
 * bit, 0x0024) points to the EP_config register map (§13.2);
 * svr_ep_generic_cfg_capacity (16 bit, 0x0026) is "the LENGTH OF THE
 * EP CONFIG REGISTER SECTION IN BYTES" -- verified directly against
 * the primary-source PDF (Table 18, page 52). rcp_regmap_general_t
 * now declares both as correctly-sized, correctly-UNITED scalar
 * fields, replacing the former ep_generic_cfg field
 * (rcp_regmap_table_ref_t, the shared pointer/capacity type most
 * remaining sub-table refs still use) whose capacity member was
 * documented as an ENTRY COUNT -- the exact opposite unit from what
 * 0x0026 actually is. This was a genuine semantic contradiction, not
 * just a width mismatch (the same class of bug REQ-RMAP-033/-034 fixed
 * was purely about width/address; here the OLD shared field's own
 * documented MEANING was backwards for this specific register). Now
 * wire-reachable (REQ-RMAP-024 CLOSED, proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). Still open: this codebase has no real EP_config table storage
 * anywhere yet for this pointer to meaningfully address. */
static void test_ep_generic_cfg_ptr_and_capacity_are_now_correctly_shaped(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_ep_generic_cfg_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_ep_generic_cfg_capacity));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_ep_generic_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_ep_generic_cfg_capacity);

    map.svr_ep_generic_cfg_ptr      = 0x00A0u;
    map.svr_ep_generic_cfg_capacity = 0x0140u; /* bytes, e.g. 320 octets -- not
                                                   an entry count */
    TEST_ASSERT_EQUAL_UINT16(0x00A0u, map.svr_ep_generic_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0140u, map.svr_ep_generic_cfg_capacity);
}

/* REQ-RMAP-037 (TC18 §12.7.5 Table 18): svr_ep_bytebus_id_map_ptr (16
 * bit, 0x0028) points to the EP - byte_bus_id mapping table (§12.7.8);
 * svr_ep_bytebus_id_map_capacity (8 bit, 0x002A) is the table's max
 * entry count -- verified directly against the primary-source PDF
 * (Table 18, page 52) during REQ-RMAP-036's own batch. Unlike -036,
 * this is purely the width/address class of fix REQ-RMAP-033/-034
 * already established: the capacity really is an entry count here,
 * matching this codebase's own rcp_regmap_table_ref_t.capacity
 * convention -- no semantic contradiction to resolve. rcp_regmap_
 * general_t now declares both as correctly-sized scalar fields,
 * replacing the former ep_id_bus_map field (rcp_regmap_table_ref_t).
 * Now wire-reachable (REQ-RMAP-024 CLOSED, proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). Still open: this codebase has no real EP-byte_bus_id mapping
 * table storage anywhere yet for this pointer to meaningfully address. */
static void test_ep_bytebus_id_map_ptr_and_capacity_are_now_correctly_shaped(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_ep_bytebus_id_map_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_ep_bytebus_id_map_capacity));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_ep_bytebus_id_map_ptr);
    TEST_ASSERT_EQUAL_UINT8(0x00u, map.svr_ep_bytebus_id_map_capacity);

    map.svr_ep_bytebus_id_map_ptr      = 0x00B0u;
    map.svr_ep_bytebus_id_map_capacity = 0x10u;
    TEST_ASSERT_EQUAL_UINT16(0x00B0u, map.svr_ep_bytebus_id_map_ptr);
    TEST_ASSERT_EQUAL_UINT8(0x10u, map.svr_ep_bytebus_id_map_capacity);
}

/* REQ-RMAP-038 (TC18 §12.7.5 Table 18): svr_ep_functional_cfg_ptr (16
 * bit, 0x002C) points to the EP_FUNC_config register map (§13.7.1.2
 * Server); svr_sequencer_state_ptr (16 bit, 0x002E) points to the
 * Sequencer_config register map (§12.7.10) -- both LONE pointers, TC18
 * defines no adjacent capacity register for either, verified directly
 * against the primary-source PDF (Table 18, page 52) during
 * REQ-RMAP-036's own batch. rcp_regmap_general_t now declares both as
 * bare, correctly-sized scalar fields, replacing the former ep_
 * functional_cfg/sequencer_state fields (rcp_regmap_table_ref_t,
 * the shared pointer/capacity type -- now unused by any field in this
 * struct, since every one of the seven original sub-table refs has
 * been retyped across REQ-RMAP-033/-034/-036/-037/-038) whose spurious
 * capacity members had no TC18 basis for either register -- the same
 * class of fix REQ-RMAP-033 already established for svr_hw_cfg_ptr.
 * Now wire-reachable (REQ-RMAP-024 CLOSED, proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). Still open: this codebase has no real EP_FUNC_config or
 * Sequencer_config table storage anywhere yet for either pointer to
 * meaningfully address. */
static void test_functional_cfg_and_sequencer_state_ptrs_are_now_correctly_shaped(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_ep_functional_cfg_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_sequencer_state_ptr));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_ep_functional_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_sequencer_state_ptr);

    map.svr_ep_functional_cfg_ptr = 0x00C0u;
    map.svr_sequencer_state_ptr   = 0x00D0u;
    TEST_ASSERT_EQUAL_UINT16(0x00C0u, map.svr_ep_functional_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x00D0u, map.svr_sequencer_state_ptr);
}

/* TC18 §12.7.5 Table 18 (continued) and §12.7.11-§12.7.14 define four
 * further pointer/capacity register pairs -- network interface, physical
 * layer, time synch, security -- where a zero pointer is the defined
 * encoding for "subsystem not supported" (confirmed for physical layer,
 * time synch, and security; TC18's own table gives no such note for
 * network interface -- flagged, not assumed by analogy) and a section
 * spans pointer..pointer+capacity. rcp_regmap_general_t now declares
 * all four pairs (REQ-RMAP-039) -- NOT Group 1's last item; a real ninth
 * pair, svr_device_specific_cfg_ptr/_capacity, sits immediately after
 * svr_security_cfg_capacity (issue #429, see
 * test_general_map_render_matches_table_18_byte_offsets() and
 * test_general_map_wire_reach_now_covers_full_table_18() for that
 * pair's own coverage). Group 1 IS otherwise complete as of this test:
 * every one of the general map's original seven
 * rcp_regmap_table_ref_t sub-table refs was already retyped
 * (svr_hw_cfg_ptr, REQ-RMAP-033; the stream-config quartet,
 * REQ-RMAP-034; svr_ep_generic_cfg_ptr/_capacity, REQ-RMAP-036;
 * svr_ep_bytebus_id_map_ptr/_capacity, REQ-RMAP-037;
 * svr_ep_functional_cfg_ptr and svr_sequencer_state_ptr, REQ-RMAP-038),
 * and these eight fields are the first genuinely NEW Group 1 fields
 * since REQ-RMAP-033. TC18's own "Absolute address" column is blank
 * for all eight on the primary-source PDF's own continuation page (a
 * genuine spec gap, not an extraction failure -- see
 * svr_network_interface_cfg_ptr's own comment in regmap.h for the full
 * explanation), so this test uses each field's INFERRED address
 * (0x0030-0x003F) rather than a directly-read one -- still the best
 * available answer, and honestly documented as such throughout. Now
 * wire-reachable at those inferred addresses (REQ-RMAP-024 CLOSED,
 * proven generically by
 * test_general_map_wire_reach_now_covers_full_table_18(), not re-tested
 * here). */
static void test_four_optional_subsystem_pointer_pairs_are_now_present(void)
{
    rcp_regmap_general_t map;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_network_interface_cfg_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_network_interface_cfg_capacity));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_physical_layer_cfg_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_physical_layer_cfg_capacity));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_time_synch_cfg_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_time_synch_cfg_capacity));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_security_cfg_ptr));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_security_cfg_capacity));

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_network_interface_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_network_interface_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_physical_layer_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_physical_layer_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_time_synch_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_time_synch_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_security_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_security_cfg_capacity);

    /* 0x0000 is TC18's own defined encoding for "not supported" on
     * three of the four pointers (network interface has no such note)
     * -- the correct, safe default this codebase's own established
     * "fail-open to the honest answer" convention already uses
     * elsewhere (e.g. svr_configuration_lock's 0x00 == unlocked). */
    map.svr_physical_layer_cfg_ptr = 0x0000u; /* still "not supported" */
    TEST_ASSERT_EQUAL_UINT16(0x0000u, map.svr_physical_layer_cfg_ptr);

    map.svr_network_interface_cfg_ptr      = 0x0050u;
    map.svr_network_interface_cfg_capacity = 0x0004u;
    map.svr_time_synch_cfg_ptr             = 0x0060u;
    map.svr_time_synch_cfg_capacity        = 0x0002u;
    map.svr_security_cfg_ptr               = 0x0070u;
    map.svr_security_cfg_capacity          = 0x0008u;
    TEST_ASSERT_EQUAL_UINT16(0x0050u, map.svr_network_interface_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0004u, map.svr_network_interface_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0x0060u, map.svr_time_synch_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0002u, map.svr_time_synch_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0x0070u, map.svr_security_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0x0008u, map.svr_security_cfg_capacity);
    TEST_ASSERT_EQUAL_HEX16(RCP_REGMAP_NO_ROOT_CLIENT, map.svr_root_client_index);
}

/* ── §12.7.6 Tables 19-21: HW pin mapping ──────────────────────────────────── */

/* REQ-RMAP-040 CLOSED (storage half): TC18 §12.7.6 requires an actual
 * HW_config table holding, per used physical IO-pin, its endpoint-signal
 * assignment and pin properties. rcp_mock_server_t now carries real
 * storage (rcp_mock_server_set_hw_pin_map()/_hw_pin_map(), mock.h/
 * mock.c), and rcp_config_apply_to_mock() no longer discards the parsed
 * manifest data -- asserted here: the manifest carries one pin, the
 * server's own table holds it after applying, field for field. The wire
 * ACF_ABB request/response mechanism itself is NOT part of this fix --
 * see regmap.h's own file-header note on rcp_regmap_hw_pin_map_render()
 * for the genuine, still-unresolved addressing question that keeps
 * REQ-RMAP-040/041 at `partial`, not `implemented`. */
static void test_hw_config_table_now_has_real_server_side_storage(void)
{
    static const char json[] =
        "{\"hw_pin_map\":[{\"hw_ep_nr\":4,\"hw_ep_pin_nr\":3,"
        "\"pin_property\":[\"output\",\"pull_up\"]}]}";
    rcp_config_manifest_t                 m;
    rcp_mock_server_t                    *srv;
    const rcp_regmap_hw_pin_map_entry_t  *stored;
    size_t                                stored_len;

    TEST_ASSERT_EQUAL_INT(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, m.hw_pin_map_len);
    TEST_ASSERT_EQUAL_HEX8(4, m.hw_pin_map[0].hw_ep_nr);
    TEST_ASSERT_EQUAL_HEX8(3, m.hw_pin_map[0].hw_ep_pin_nr);

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);

    /* A freshly-constructed server's table starts empty -- not merely
     * absent, an explicit, observable zero length. */
    stored = rcp_mock_server_hw_pin_map(srv, &stored_len);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_EQUAL_UINT((size_t)0u, stored_len);

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_apply_to_mock(&m, srv));

    /* The parsed pin now DOES reach the server -- stored, not discarded. */
    stored = rcp_mock_server_hw_pin_map(srv, &stored_len);
    TEST_ASSERT_EQUAL_UINT((size_t)1u, stored_len);
    TEST_ASSERT_EQUAL_HEX8(4, stored[0].hw_ep_nr);
    TEST_ASSERT_EQUAL_HEX8(3, stored[0].hw_ep_pin_nr);

    rcp_mock_server_destroy(srv);
    rcp_config_manifest_free(&m);
}

/* rcp_mock_server_set_hw_pin_map() rejects a table larger than
 * RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES (regmap.h) outright, leaving srv's
 * own existing table untouched -- proven by first populating one real
 * entry, then attempting (and failing) an oversized replacement, then
 * confirming the original single entry survived. */
static void test_hw_pin_map_rejects_oversized_table_leaving_existing_data_intact(void)
{
    rcp_mock_server_t             *srv;
    rcp_regmap_hw_pin_map_entry_t  one[1]     = {{4, 3, 0x0Cu}};
    rcp_regmap_hw_pin_map_entry_t  oversized[RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES + 1];
    const rcp_regmap_hw_pin_map_entry_t *stored;
    size_t                          stored_len;

    memset(oversized, 0, sizeof(oversized));

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);

    TEST_ASSERT_TRUE(rcp_mock_server_set_hw_pin_map(srv, one, 1));
    TEST_ASSERT_FALSE(rcp_mock_server_set_hw_pin_map(srv, oversized,
                                                      RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES + 1));

    stored = rcp_mock_server_hw_pin_map(srv, &stored_len);
    TEST_ASSERT_EQUAL_UINT((size_t)1u, stored_len);
    TEST_ASSERT_EQUAL_HEX8(4, stored[0].hw_ep_nr);

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-032: svr_io_pin_count (Table 20, wire-readable) is this
 * server's own report of how many HW pins it has -- previously never
 * set anywhere at all (rcp_regmap_general_init() leaves it at its
 * zero-init default forever), so a wire reader's own view of Table 20
 * silently disagreed with the real, larger hw_pin_map this same server
 * actually enforces writes against. rcp_mock_server_set_hw_pin_map()
 * now syncs it to the real table's own length on every call, including
 * shrinking back down on a later, smaller replacement (not just a
 * one-time high-water mark). */
static void test_set_hw_pin_map_syncs_svr_io_pin_count(void)
{
    rcp_mock_server_t             *srv;
    rcp_regmap_hw_pin_map_entry_t  three[3] = {{1, 1, 0x0Cu}, {2, 2, 0x0Cu}, {3, 3, 0x0Cu}};
    rcp_regmap_hw_pin_map_entry_t  one[1]   = {{4, 4, 0x0Cu}};

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL_UINT16(0, rcp_mock_server_regmap(srv)->svr_io_pin_count);

    TEST_ASSERT_TRUE(rcp_mock_server_set_hw_pin_map(srv, three, 3));
    TEST_ASSERT_EQUAL_UINT16(3, rcp_mock_server_regmap(srv)->svr_io_pin_count);

    /* Shrinks back down too, not just a one-time high-water mark. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_hw_pin_map(srv, one, 1));
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->svr_io_pin_count);

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-034: svr_request_stream_cfg_capacity/svr_response_stream_cfg_
 * capacity (Table 20, wire-readable entry counts, not byte lengths) are
 * this server's own report of how many request/response streams it has
 * configured -- previously never set anywhere, so a wire reader's own
 * view of Table 20 silently disagreed with the real tables this same
 * server actually enforces writes against, the same class of gap
 * REQ-RMAP-032 (hw_pin_map/svr_io_pin_count, above) already fixed.
 * rcp_mock_server_set_response_queue_cfg() is new this batch -- no
 * backing storage for response-queue-cfg existed in rcp_mock_server_t at
 * all before now, so its own capacity register had no real table to sync
 * from either. */
static void test_set_request_stream_cfg_syncs_svr_request_stream_cfg_capacity(void)
{
    rcp_mock_server_t              *srv;
    rcp_regmap_request_stream_cfg_t two[2];
    rcp_regmap_request_stream_cfg_t one[1];

    rcp_regmap_request_stream_cfg_init(&two[0]);
    rcp_regmap_request_stream_cfg_init(&two[1]);
    rcp_regmap_request_stream_cfg_init(&one[0]);

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL_UINT8(0, rcp_mock_server_regmap(srv)->svr_request_stream_cfg_capacity);

    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, two, 2));
    TEST_ASSERT_EQUAL_UINT8(2, rcp_mock_server_regmap(srv)->svr_request_stream_cfg_capacity);

    /* Shrinks back down too, not just a one-time high-water mark. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, one, 1));
    TEST_ASSERT_EQUAL_UINT8(1, rcp_mock_server_regmap(srv)->svr_request_stream_cfg_capacity);

    rcp_mock_server_destroy(srv);
}

static void test_set_response_queue_cfg_syncs_svr_response_stream_cfg_capacity(void)
{
    rcp_mock_server_t               *srv;
    rcp_regmap_response_queue_cfg_t  two[2];
    rcp_regmap_response_queue_cfg_t  one[1];

    rcp_regmap_response_queue_cfg_init(&two[0]);
    rcp_regmap_response_queue_cfg_init(&two[1]);
    rcp_regmap_response_queue_cfg_init(&one[0]);

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL_UINT8(0, rcp_mock_server_regmap(srv)->svr_response_stream_cfg_capacity);

    TEST_ASSERT_TRUE(rcp_mock_server_set_response_queue_cfg(srv, two, 2));
    TEST_ASSERT_EQUAL_UINT8(2, rcp_mock_server_regmap(srv)->svr_response_stream_cfg_capacity);

    /* Shrinks back down too, not just a one-time high-water mark. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_response_queue_cfg(srv, one, 1));
    TEST_ASSERT_EQUAL_UINT8(1, rcp_mock_server_regmap(srv)->svr_response_stream_cfg_capacity);

    /* Rejects an oversized table, leaving the existing table (and its
     * capacity register) untouched. */
    TEST_ASSERT_FALSE(rcp_mock_server_set_response_queue_cfg(
        srv, two, RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES + 1));
    TEST_ASSERT_EQUAL_UINT8(1, rcp_mock_server_regmap(srv)->svr_response_stream_cfg_capacity);

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-037: svr_ep_bytebus_id_map_capacity (Table 20, wire-readable
 * entry count) is this server's own report of how many EP_ID_config rows
 * it has -- previously never set anywhere, same class of gap as
 * REQ-RMAP-032/034 above. */
static void test_set_ep_id_map_syncs_svr_ep_bytebus_id_map_capacity(void)
{
    rcp_mock_server_t            *srv;
    rcp_regmap_ep_id_map_entry_t  two[2] = {{0u, 1u, 1u}, {0u, 2u, 1u}};
    rcp_regmap_ep_id_map_entry_t  one[1] = {{0u, 3u, 1u}};

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL_UINT8(0, rcp_mock_server_regmap(srv)->svr_ep_bytebus_id_map_capacity);

    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, two, 2));
    TEST_ASSERT_EQUAL_UINT8(2, rcp_mock_server_regmap(srv)->svr_ep_bytebus_id_map_capacity);

    /* Shrinks back down too, not just a one-time high-water mark. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, one, 1));
    TEST_ASSERT_EQUAL_UINT8(1, rcp_mock_server_regmap(srv)->svr_ep_bytebus_id_map_capacity);

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-036: svr_ep_generic_cfg_capacity (Table 20) is the LENGTH OF
 * THE EP CONFIG REGISTER SECTION IN BYTES (regmap.h's own field doc
 * comment), not an entry count -- previously never set anywhere,
 * staying 0 regardless of how many endpoints were registered. Confirms
 * the byte-stride conversion (12 bytes/endpoint, matching
 * src/regmap.c's own ep_generic_cfg_len computation) and that it tracks
 * both registration and removal, not just a one-time high-water mark. */
static void test_endpoint_registration_syncs_svr_ep_generic_cfg_capacity(void)
{
    rcp_mock_server_t *srv;

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL_UINT16(0, rcp_mock_server_regmap(srv)->svr_ep_generic_cfg_capacity);

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1u, 1u, true, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT16(12, rcp_mock_server_regmap(srv)->svr_ep_generic_cfg_capacity);

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 2u, 1u, true, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT16(24, rcp_mock_server_regmap(srv)->svr_ep_generic_cfg_capacity);

    TEST_ASSERT_TRUE(rcp_mock_server_remove_endpoint(srv, 1u));
    TEST_ASSERT_EQUAL_UINT16(12, rcp_mock_server_regmap(srv)->svr_ep_generic_cfg_capacity);

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-040/041 CLOSED (wire-dispatch half, issue #301): direct
 * verification of the current RC5 baseline PDF (page 61) shows Table
 * 18's own address column is headed "Absolute address", confirming
 * every "_ptr" field's own value is an absolute address in the SAME
 * EP0-scoped space Table 18 itself lives in -- see regmap.h's own
 * "HW_config server-side storage + wire codec" file-header section for
 * the full finding. rcp_regmap_hw_pin_map_apply_reconfig() is the
 * inverse of rcp_regmap_hw_pin_map_render(); rcp_regmap_ep0_decode_write_request()
 * is the address-routed dispatcher unifying Table 18's own already-correct
 * read-only rejection with HW_config's own newly-writable range. */
static void test_hw_pin_map_apply_reconfig_patches_addressed_octets_only(void)
{
    rcp_regmap_hw_pin_map_entry_t rows[2] = {
        {1, 2, 0x03u},
        {4, 5, 0x06u},
    };
    uint8_t patch_row1_type[1] = {0x99u};

    /* Patch only row 1's own hw_pin_type octet (relative address 5 = 3*1+2). */
    TEST_ASSERT_EQUAL(RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK,
                       rcp_regmap_hw_pin_map_apply_reconfig(rows, 2, 5u,
                                                             patch_row1_type, 1u));
    TEST_ASSERT_EQUAL_UINT8(1, rows[0].hw_ep_nr); /* row 0 untouched */
    TEST_ASSERT_EQUAL_UINT8(2, rows[0].hw_ep_pin_nr);
    TEST_ASSERT_EQUAL_UINT8(0x03u, rows[0].hw_pin_type);
    TEST_ASSERT_EQUAL_UINT8(4, rows[1].hw_ep_nr);     /* row 1's other octets untouched */
    TEST_ASSERT_EQUAL_UINT8(5, rows[1].hw_ep_pin_nr);
    TEST_ASSERT_EQUAL_UINT8(0x99u, rows[1].hw_pin_type); /* patched */
}

static void test_hw_pin_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched(void)
{
    rcp_regmap_hw_pin_map_entry_t rows[1] = {{7, 8, 0x0Au}};
    uint8_t data[2] = {0x11u, 0x22u};

    /* count=1 -> block_len=3; address 2 + data_len 2 = 4 > 3. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_OUT_OF_RANGE,
                       rcp_regmap_hw_pin_map_apply_reconfig(rows, 1, 2u, data, 2u));
    TEST_ASSERT_EQUAL_UINT8(7, rows[0].hw_ep_nr); /* entirely unchanged */
    TEST_ASSERT_EQUAL_UINT8(8, rows[0].hw_ep_pin_nr);
    TEST_ASSERT_EQUAL_UINT8(0x0Au, rows[0].hw_pin_type);

    TEST_ASSERT_EQUAL(RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_SHORT,
                       rcp_regmap_hw_pin_map_apply_reconfig(rows, 1, 0u, data, 0u));
}

/* REQ-RMAP-039: rcp_regmap_optional_subsystem_cfg_apply_reconfig()'s own
 * two failure modes -- direct unit-level coverage of the bounds check,
 * independent of the dispatcher-level test elsewhere in this file
 * (which only ever writes in-range). Same "leaves storage untouched on
 * rejection" shape every sibling apply_reconfig() already guarantees. */
static void test_optional_subsystem_cfg_apply_reconfig_rejects_out_of_range_and_short(void)
{
    rcp_regmap_optional_subsystem_cfg_t cfg = {{0xAAu, 0xBBu, 0xCCu, 0xDDu}, 4u};
    uint8_t                             data[2] = {0x11u, 0x22u};

    /* len=4; relative 3 + data_len 2 = 5 > 4. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_OUT_OF_RANGE,
                       rcp_regmap_optional_subsystem_cfg_apply_reconfig(&cfg, 3u, data, 2u));
    TEST_ASSERT_EQUAL_UINT8(0xAAu, cfg.data[0]); /* entirely unchanged */
    TEST_ASSERT_EQUAL_UINT8(0xBBu, cfg.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCCu, cfg.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0xDDu, cfg.data[3]);

    TEST_ASSERT_EQUAL(RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_SHORT,
                       rcp_regmap_optional_subsystem_cfg_apply_reconfig(&cfg, 0u, data, 0u));

    /* An in-range write does apply, and only touches the addressed
     * octet. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_OK,
                       rcp_regmap_optional_subsystem_cfg_apply_reconfig(&cfg, 1u, data, 2u));
    TEST_ASSERT_EQUAL_UINT8(0xAAu, cfg.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x11u, cfg.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0x22u, cfg.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0xDDu, cfg.data[3]);

    TEST_ASSERT_NOT_NULL(rcp_regmap_optional_subsystem_cfg_reconfig_strerror(
        RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_OK));
}

/* REQ-WAKEUP-020 (issue #336): TC18 §13.7.2.1 fixes the WakeUp
 * endpoint's own ep_id to 1 -- rcp_regmap_ep0_decode_write_request()
 * now enforces this at write time (not just diagnoses it after the
 * fact, rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id()'s own
 * pre-existing job), via the caller-supplied ep_id_map_ep_types/
 * fixed_ep_id_target_ep_type/fixed_ep_id_required_ep_id parameters --
 * regmap.c has no dependency on ep_wakeup.h, so this test stands in
 * for its own concrete constants (target_ep_type=9, required_ep_id=1,
 * arbitrary values distinct from every other row's own ep_type in this
 * test, matching the real caller's own responsibility to supply
 * ep_wakeup.h's actual RCP_EP_WAKEUP_EP_TYPE/_ENDPOINT_NUM). */
static void test_ep0_dispatcher_enforces_fixed_ep_id_for_configured_ep_type(void)
{
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_bytes_t                  frame;
    rcp_regmap_general_t         map;
    rcp_regmap_ep_id_map_entry_t ep_id_map[2] = {
        {1u, 1u, 10u},  /* row 0: WakeUp-typed, already correctly ep_id=1 */
        {1u, 5u, 20u},  /* row 1: a different, unconstrained ep_type */
    };
    uint8_t                    ep_types[2]  = {9u, 3u}; /* row 0 is the target ep_type (9);
                                                            row 1 is not */
    rcp_lifecycle_state_t      state        = RCP_LIFECYCLE_HW_UNCONFIGURED; /* R/W+ writable
                                                                                 unconditionally */
    rcp_lifecycle_writer_ctx_t writer       = PLAIN_WRITER;
    rcp_wire_error_t           err;
    uint8_t                    tn = 0;
    rcp_regmap_ep0_errc_t      rc;

    rcp_regmap_general_init(&map);
    map.svr_ep_bytebus_id_map_ptr = 0x0200u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 31;

    /* 1) A write that would set row 0's own ep_id (relative offset 1
     * within its own 4-octet row) to 7, violating the fixed-ep_id
     * invariant for its own configured ep_type -- denied, table left
     * entirely unchanged. */
    {
        uint8_t payload[3] = {0x02u, 0x01u, 0x07u}; /* addr=0x0201 (row 0's own ep_id octet) */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, ep_id_map, 2u, NULL, 0u, NULL, 0u, NULL, 0u,
                                                   NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, ep_types,
                                                   9u, 1u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, err);
        TEST_ASSERT_EQUAL_UINT16(1u, ep_id_map[0].ep_id); /* unchanged */
        rcp_bytes_free(&frame);
    }

    /* 2) The identical write to row 1 -- NOT the configured ep_type, so
     * it applies normally: no invariant to violate. */
    {
        uint8_t payload[3] = {0x02u, 0x05u, 0x07u}; /* addr=0x0205 (row 1's own ep_id octet) */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, ep_id_map, 2u, NULL, 0u, NULL, 0u, NULL, 0u,
                                                   NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, ep_types,
                                                   9u, 1u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT16(7u, ep_id_map[1].ep_id); /* applied */
        rcp_bytes_free(&frame);
    }

    /* 3) A write that sets row 0's own ep_id to 1 -- the SAME value the
     * invariant requires -- applies normally, proving this isn't an
     * unconditional deny for the target ep_type. */
    {
        uint8_t payload[3] = {0x02u, 0x01u, 0x01u};

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, ep_id_map, 2u, NULL, 0u, NULL, 0u, NULL, 0u,
                                                   NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, ep_types,
                                                   9u, 1u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT16(1u, ep_id_map[0].ep_id);
        rcp_bytes_free(&frame);
    }

    /* 4) ep_id_map_ep_types itself NULL -- no enforcement configured,
     * the SAME violating write from case 1 now applies unchecked
     * (matches this dispatcher's own established NULL-means-absent
     * convention). */
    {
        uint8_t payload[3] = {0x02u, 0x01u, 0x07u};

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, ep_id_map, 2u, NULL, 0u, NULL, 0u, NULL, 0u,
                                                   NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 9u,
                                                   1u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT16(7u, ep_id_map[0].ep_id); /* applied, unchecked */
        rcp_bytes_free(&frame);
    }
}

/* REQ-RMAP-052/054 CLOSED (write-dispatch half, issue #301 batch 2):
 * same finding as HW_config's own, applied to EP_ID_config --
 * svr_ep_bytebus_id_map_ptr's own value is an absolute address in the
 * same EP0-scoped space Table 18 itself lives in. New
 * rcp_regmap_ep_id_map_apply_reconfig() is the parse-side inverse of
 * rcp_regmap_ep_id_map_render(); rcp_regmap_ep0_decode_write_request()
 * now routes to it the identical way it already routes to HW_config. */
/* REQ-RMAP-061/065 CLOSED (write-dispatch half, issue #301 batch 3):
 * same finding as HW_config's and EP_ID_config's own, applied to
 * response-queue-config -- svr_response_stream_cfg_ptr's own value is
 * an absolute address in the same EP0-scoped space Table 18 itself
 * lives in. Unlike the other two, no render function existed for this
 * table at all before this batch; rcp_regmap_response_queue_cfg_render()
 * is the first one. */
static void test_ep0_dispatcher_routes_all_five_pointed_to_tables_and_unknown_addresses(void)
{
    rcp_acf_byte_message_info_t     hdr = {0};
    rcp_bytes_t                     frame;
    rcp_regmap_general_t            map;
    rcp_regmap_hw_pin_map_entry_t   hw_pin_map[2] = {
        {1, 2, 0x03u},
        {4, 5, 0x06u},
    };
    rcp_regmap_ep_id_map_entry_t    ep_id_map[2] = {
        {10u, 20u, 1u},
        {30u, 40u, 1u},
    };
    rcp_regmap_response_queue_cfg_t response_queue_cfg[2];
    rcp_regmap_request_stream_cfg_t request_stream_cfg[2];
    rcp_regmap_ep_generic_cfg_t     ep_generic_cfg[2];
    rcp_lifecycle_state_t           state  = RCP_LIFECYCLE_HW_UNCONFIGURED; /* maximally
                                                                                permissive for
                                                                                every table's
                                                                                own kind:
                                                                                W-star/W-plus
                                                                                are writable
                                                                                unconditionally
                                                                                here, and
                                                                                HW_GENERIC
                                                                                (HW_config's own
                                                                                narrower rule,
                                                                                issue #308) is
                                                                                writable here
                                                                                too, given the
                                                                                right writer */
    rcp_lifecycle_writer_ctx_t      writer = DISCOVERY_WRITER; /* the ONLY writer HW_GENERIC
                                                                    accepts (no root
                                                                    client/owning stream
                                                                    can exist this early);
                                                                    W-star/W-plus's own
                                                                    "unconditionally" rule
                                                                    accepts this too */
    rcp_wire_error_t                err;
    uint8_t                         tn = 0;
    rcp_regmap_ep0_errc_t           rc;

    rcp_regmap_response_queue_cfg_init(&response_queue_cfg[0]);
    response_queue_cfg[0].stream_uid = 111u;
    rcp_regmap_response_queue_cfg_init(&response_queue_cfg[1]);
    response_queue_cfg[1].stream_uid = 222u;

    rcp_regmap_request_stream_cfg_init(&request_stream_cfg[0]);
    request_stream_cfg[0].rx_stream_id = 1111u;
    rcp_regmap_request_stream_cfg_init(&request_stream_cfg[1]);
    request_stream_cfg[1].rx_stream_id = 2222u;

    rcp_regmap_ep_generic_cfg_init(&ep_generic_cfg[0]);
    ep_generic_cfg[0].ep_type = 0x03u;
    rcp_regmap_ep_generic_cfg_init(&ep_generic_cfg[1]);
    ep_generic_cfg[1].ep_type = 0x06u;

    rcp_regmap_general_init(&map);
    map.svr_hw_cfg_ptr             = 0x0100u; /* arbitrary, past Table 18's own 0x40 extent */
    map.svr_ep_bytebus_id_map_ptr  = 0x0200u; /* arbitrary, clear of HW_config's own range */
    map.svr_response_stream_cfg_ptr = 0x0300u; /* arbitrary, clear of both above */
    map.svr_request_stream_cfg_ptr  = 0x0400u; /* arbitrary, clear of all three above */
    map.svr_ep_generic_cfg_ptr      = 0x0500u; /* arbitrary, clear of all four above (issue #311 batch 5) */
    /* map.svr_configuration_lock stays 0 (rcp_regmap_general_init()'s own
     * zero-init default) -- unlocked, so W+ writes below are authorized too. */

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 11;

    /* 1) An address within Table 18's own extent -- always denied,
     * regardless of what any pointed-to table's own pointer/table
     * say. */
    {
        uint8_t payload[3] = {0x00u, 0x10u, 0xFFu}; /* addr=0x0010, 1 data octet */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        TEST_ASSERT_EQUAL_UINT8(11, tn);
        rcp_bytes_free(&frame);
    }

    /* 2) An address within HW_config's own extent -- applied, and the
     * table is actually patched. Row 0's own hw_pin_type is at
     * svr_hw_cfg_ptr + 2. */
    {
        uint8_t payload[3];

        put_test_u16(payload, (uint16_t)(map.svr_hw_cfg_ptr + 2u));
        payload[2] = 0x77u;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT8(0x77u, hw_pin_map[0].hw_pin_type);
        TEST_ASSERT_EQUAL_UINT8(1, hw_pin_map[0].hw_ep_nr); /* untouched */
        rcp_bytes_free(&frame);
    }

    /* 3) A write into HW_config's own range but extending past its
     * current extent (count=2 -> 6 octets; address+len = 5+2 = 7). */
    {
        uint8_t payload[4];

        put_test_u16(payload, (uint16_t)(map.svr_hw_cfg_ptr + 5u));
        payload[2] = 0xAAu;
        payload[3] = 0xBBu;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, err);
        rcp_bytes_free(&frame);
    }

    /* 4) An address within EP_ID_config's own extent -- applied. Row 1's
     * own byte_bus_id/Ctrl word (2 octets) is at svr_ep_bytebus_id_map_ptr
     * + 6 (row 1 begins at +4; the word is that row's own octets 2-3).
     * Table 25/26 (issue #421) packs it -- BBID in bits[15:5], Ctrl in
     * bits[4:0] -- so the wire value written here is 99 already shifted
     * (99 << 5 = 0x0C60), not a flat 99; this test's own point is the
     * dispatcher's routing/patching, not the packing itself (that is
     * covered directly by test_ep_id_map_render_and_apply_reconfig_pack_
     * bbid_ctrl_per_table_25_26()). */
    {
        uint8_t payload[4];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 6u));
        put_test_u16(&payload[2], (uint16_t)(99u << 5));

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT16(99u, (uint16_t)ep_id_map[1].byte_bus_id);
        TEST_ASSERT_EQUAL_UINT16(30u, ep_id_map[1].ep_id); /* untouched */
        TEST_ASSERT_EQUAL_UINT8(1u, ep_id_map[1].request_stream_index); /* untouched */
        rcp_bytes_free(&frame);
    }

    /* 5) A write into EP_ID_config's own range but extending past its
     * current extent (count=2 -> 8 octets; address+len = 7+2 = 9). */
    {
        uint8_t payload[4];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 7u));
        payload[2] = 0xCCu;
        payload[3] = 0xDDu;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, err);
        rcp_bytes_free(&frame);
    }

    /* 5b) An address exactly one past EP_ID_config's own current extent
     * (count=2 -> 8 octets; ptr+8 is the first address NOT in range) --
     * must fall through toward the unknown-address case, NOT be routed
     * into EP_ID_config's own apply_reconfig() at all -- see this test
     * function's own file-header note on why this boundary case is its
     * own dedicated check, not implied by case 5 above. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 8u));

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 6) An address within response-queue-config's own extent --
     * applied. Row 1's own max_avtpdu_size (2 octets) is at
     * svr_response_stream_cfg_ptr + 12 (row 1 begins at +10;
     * max_avtpdu_size is that row's own octets 2-3). */
    {
        uint8_t payload[4];

        put_test_u16(payload, (uint16_t)(map.svr_response_stream_cfg_ptr + 12u));
        put_test_u16(&payload[2], 555u);

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT16(555u, response_queue_cfg[1].max_avtpdu_size);
        TEST_ASSERT_EQUAL_UINT16(222u, response_queue_cfg[1].stream_uid); /* untouched */
        rcp_bytes_free(&frame);
    }

    /* 7) A write into response-queue-config's own range but extending
     * past its current extent (count=2 -> 20 octets; address+len =
     * 19+2 = 21). */
    {
        uint8_t payload[4];

        put_test_u16(payload, (uint16_t)(map.svr_response_stream_cfg_ptr + 19u));
        payload[2] = 0xEEu;
        payload[3] = 0xFFu;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, err);
        rcp_bytes_free(&frame);
    }

    /* 7b) An address exactly one past response-queue-config's own
     * current extent (count=2 -> 20 octets; ptr+20 is the first
     * address NOT in range) -- same dedicated dispatcher-boundary check
     * as case 5b above, this table's own routing condition mutated
     * independently of the other two tables'. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_response_stream_cfg_ptr + 20u));

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 8) An address within request-stream-cfg's own extent -- applied.
     * Row 1's own rx_secure_channel_index (1 octet) is at
     * svr_request_stream_cfg_ptr + 36 (row 1 begins at +24; 0x000C
     * within that row is +12, so +24+12 = +36). */
    {
        uint8_t payload[3];

        put_test_u16(payload, (uint16_t)(map.svr_request_stream_cfg_ptr + 36u));
        payload[2] = 0x55u;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT8(0x55u, request_stream_cfg[1].rx_secure_channel_index);
        TEST_ASSERT_EQUAL_UINT64(2222u, request_stream_cfg[1].rx_stream_id); /* untouched */
        rcp_bytes_free(&frame);
    }

    /* 8b) request-stream-cfg's own routing boundary (count=2 -> 48
     * octets; ptr+48 is the first address NOT in range). */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_request_stream_cfg_ptr + 48u));

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 8c) An address within ep_generic_cfg's own extent (issue #311
     * batch 5) -- applied. Row 1's own ep_description (4 octets) is at
     * svr_ep_generic_cfg_ptr + 16 (row 1 begins at +12; 0x0004 within
     * that row is +4, so +12+4 = +16). ep_type stays untouched -- the
     * dispatcher-level confirmation of apply_reconfig()'s own already-
     * tested read-only behavior (issue #311 batch 4). */
    {
        uint8_t payload[6];

        put_test_u16(payload, (uint16_t)(map.svr_ep_generic_cfg_ptr + 16u));
        payload[2] = 0x11u;
        payload[3] = 0x22u;
        payload[4] = 0x33u;
        payload[5] = 0x44u;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT32(0x11223344u, ep_generic_cfg[1].ep_description);
        TEST_ASSERT_EQUAL_UINT8(0x06u, ep_generic_cfg[1].ep_type); /* untouched -- read-only */
        rcp_bytes_free(&frame);
    }

    /* 8d) ep_generic_cfg's own routing boundary (count=2 -> 24 octets;
     * ptr+24 is the first address NOT in range). */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_ep_generic_cfg_ptr + 24u));

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 9) An address that lands in none of Table 18, HW_config,
     * EP_ID_config, response-queue-config, request-stream-cfg, or
     * ep_generic_cfg. */
    {
        uint8_t payload[3] = {0x00u, 0x50u, 0x00u}; /* 0x0050: past Table 18, before svr_hw_cfg_ptr */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 10) ACF-level failures still propagate correctly. */
    {
        uint8_t payload[1] = {0x00u}; /* too short for its own leading address */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   state, writer, hw_pin_map, 2u, ep_id_map, 2u,
                                                   response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u, NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD, rc);
        rcp_bytes_free(&frame);
    }

    /* strerror() never NULL, distinct across at least two values. */
    TEST_ASSERT_NOT_NULL(rcp_regmap_ep0_strerror(RCP_REGMAP_EP0_OK));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(rcp_regmap_ep0_strerror(RCP_REGMAP_EP0_OK),
                                     rcp_regmap_ep0_strerror(RCP_REGMAP_EP0_ERR_WRONG_BUS)));
    TEST_ASSERT_NOT_NULL(rcp_regmap_hw_pin_map_reconfig_strerror(RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK));
    TEST_ASSERT_NOT_NULL(rcp_regmap_ep_id_map_reconfig_strerror(RCP_REGMAP_EP_ID_MAP_RECONFIG_OK));
    TEST_ASSERT_NOT_NULL(rcp_regmap_response_queue_cfg_reconfig_strerror(
        RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_OK));
    TEST_ASSERT_NOT_NULL(rcp_regmap_request_stream_cfg_reconfig_strerror(
        RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK));
}

/* REQ-RMAP-039 (issue #336): the four optional-subsystem sections
 * (network interface/physical layer/time synch/security, TC18
 * §12.7.11-.14) are now genuinely wire-reachable -- a client can write
 * into and read back from each one's own [ptr, ptr+len) extent, exactly
 * like the five row-typed pointed-to tables the test above already
 * covers, even though these four have no row layout of their own (see
 * regmap.h's own file-header note on rcp_regmap_optional_subsystem_cfg_t
 * for why). Also proves the per-section NULL convention: a section with
 * a NULL cfg pointer in rcp_regmap_optional_subsystem_cfg_ptrs_t is
 * skipped by both dispatchers even when its own address range would
 * otherwise match. */
static void test_ep0_dispatcher_routes_optional_subsystem_cfg_sections(void)
{
    rcp_acf_byte_message_info_t              hdr = {0};
    rcp_bytes_t                              frame;
    rcp_regmap_general_t                     map;
    rcp_regmap_optional_subsystem_cfg_t      network_cfg = {{0}, 4u}; /* len=4: a tiny
                                                                          installed section */
    rcp_regmap_optional_subsystem_cfg_t      security_cfg = {{0}, 0u}; /* len=0: NOT installed --
                                                                           matches TC18's own
                                                                           "0 means not
                                                                           supported" encoding */
    rcp_regmap_optional_subsystem_cfg_ptrs_t optional_cfg;
    rcp_lifecycle_state_t                    state  = RCP_LIFECYCLE_HW_UNCONFIGURED; /* W-star
                                                                                          writable
                                                                                          unconditionally
                                                                                          here */
    rcp_lifecycle_writer_ctx_t               writer = PLAIN_WRITER;
    rcp_wire_error_t                         err;
    uint8_t                                  tn = 0;
    uint16_t                                 addr;
    uint8_t                                  read_size;
    rcp_regmap_ep0_errc_t                    rc;

    optional_cfg.network_interface_cfg = &network_cfg;
    optional_cfg.physical_layer_cfg    = NULL; /* whole section absent -- distinct from
                                                   security_cfg's own "present but len==0" case */
    optional_cfg.time_synch_cfg        = NULL;
    optional_cfg.security_cfg          = &security_cfg;

    rcp_regmap_general_init(&map);
    map.svr_network_interface_cfg_ptr = 0x0600u; /* arbitrary, clear of every other table's own
                                                      range this file's sibling tests use */
    map.svr_security_cfg_ptr          = 0x0700u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 21;

    /* 1) A write within network_interface_cfg's own extent -- applied,
     * and the buffer is actually patched (proves apply_reconfig()'s own
     * direct-memcpy path, not just that the dispatcher recognizes the
     * address). */
    {
        uint8_t payload[3] = {0x06u, 0x02u, 0x5Au}; /* addr=0x0602 (relative 2), 1 data octet */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL,
                                                   NULL, 0u, 0u, &err, &tn, 0u, &optional_cfg, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT8(21, tn);
        TEST_ASSERT_EQUAL_UINT8(0x5Au, network_cfg.data[2]);
        rcp_bytes_free(&frame);
    }

    /* 2) A read of that same extent -- the just-applied write is
     * genuinely visible back over the wire, zero-filled past len=4. */
    {
        rcp_bytes_t                 resp;
        rcp_acf_byte_message_info_t rhdr = {0};
        const uint8_t               *rpayload;
        size_t                       rpayload_len;

        addr      = map.svr_network_interface_cfg_ptr;
        read_size = 6u; /* extends 2 octets past network_cfg's own len=4 -- proves zero-fill */

        resp = rcp_regmap_ep0_encode_read_response(addr, read_size, 22, &map, NULL, 0u, NULL, 0u,
                                                     NULL, 0u, NULL, 0u, NULL, 0u, NULL, NULL, 0u,
                                                     &err, 0u, &optional_cfg, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_NOT_NULL(resp.data);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp.data, resp.len, &rhdr, &rpayload, &rpayload_len));
        TEST_ASSERT_EQUAL(6u, rpayload_len);
        TEST_ASSERT_EQUAL_UINT8(0x5Au, rpayload[2]); /* the write from case 1, read back */
        TEST_ASSERT_EQUAL_UINT8(0x00u, rpayload[4]); /* past len=4 -- zero-filled */
        TEST_ASSERT_EQUAL_UINT8(0x00u, rpayload[5]);
        rcp_bytes_free(&resp);
    }

    /* 3) security_cfg's own address range -- present in optional_cfg but
     * len==0, so its own [ptr, ptr+0) extent is empty and never matches
     * any address; falls through to EP_NOT_FOUND, matching TC18's own
     * "0 means not supported" encoding rather than silently accepting a
     * write into a section that was never actually sized. */
    {
        uint8_t payload[3] = {0x07u, 0x00u, 0xFFu}; /* addr=0x0700, security_cfg's own ptr */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL,
                                                   NULL, 0u, 0u, &err, &tn, 0u, &optional_cfg, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 4) optional_cfg itself NULL -- every one of this dispatcher's own
     * ordinary calls (every sibling test in this file) already proves
     * this doesn't crash or misroute; one direct check here too. */
    {
        uint8_t payload[3] = {0x06u, 0x02u, 0x11u};

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL,
                                                   NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 5) FUNCTIONAL_W_STAR's own "permanently locked once RCP_CONFIGURED"
     * rule (lifecycle.h) genuinely denies the write, proving
     * optional_subsystem_cfg_write_route() actually calls
     * rcp_lifecycle_field_writable() before applying -- not just that
     * the address range matches. The buffer is left untouched. */
    {
        uint8_t payload[3] = {0x06u, 0x02u, 0x99u};

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER, NULL,
                                                   0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL,
                                                   NULL, 0u, 0u, &err, &tn, 0u, &optional_cfg, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        TEST_ASSERT_EQUAL_UINT8(0x5Au, network_cfg.data[2]); /* case 1's own write, unchanged --
                                                                  NOT overwritten by 0x99 */
        rcp_bytes_free(&frame);
    }
}

/* REQ-RMAP-039: rcp_mock_server_set_network_interface_cfg() etc. install
 * a section's own content AND keep srv->regmap's own capacity register
 * synced to it -- the same capacity-sync convention
 * rcp_mock_server_set_hw_pin_map() etc. already establish
 * (REQ-RMAP-032/034/036/037). Covers all four setters/getters; a
 * dedicated dispatcher-level test above already covers the wire codec
 * itself, so this test's own job is just the mock-server storage/sync
 * layer. */
static void test_mock_server_optional_subsystem_cfg_setters_sync_capacity(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    uint8_t             data[3] = {0x01u, 0x02u, 0x03u};
    rcp_regmap_optional_subsystem_cfg_t *got;

    TEST_ASSERT_NOT_NULL(srv);

    TEST_ASSERT_TRUE(rcp_mock_server_set_network_interface_cfg(srv, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT16(3u, rcp_mock_server_regmap(srv)->svr_network_interface_cfg_capacity);
    got = rcp_mock_server_network_interface_cfg(srv);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL(3u, got->len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, got->data, 3u);

    TEST_ASSERT_TRUE(rcp_mock_server_set_physical_layer_cfg(srv, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT16(3u, rcp_mock_server_regmap(srv)->svr_physical_layer_cfg_capacity);
    TEST_ASSERT_EQUAL(3u, rcp_mock_server_physical_layer_cfg(srv)->len);

    TEST_ASSERT_TRUE(rcp_mock_server_set_time_synch_cfg(srv, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT16(3u, rcp_mock_server_regmap(srv)->svr_time_synch_cfg_capacity);
    TEST_ASSERT_EQUAL(3u, rcp_mock_server_time_synch_cfg(srv)->len);

    TEST_ASSERT_TRUE(rcp_mock_server_set_security_cfg(srv, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT16(3u, rcp_mock_server_regmap(srv)->svr_security_cfg_capacity);
    TEST_ASSERT_EQUAL(3u, rcp_mock_server_security_cfg(srv)->len);

    /* Oversized install rejected, storage/capacity left unchanged. */
    TEST_ASSERT_FALSE(rcp_mock_server_set_network_interface_cfg(
        srv, data, RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_MAX_OCTETS + 1u));
    TEST_ASSERT_EQUAL_UINT16(3u, rcp_mock_server_regmap(srv)->svr_network_interface_cfg_capacity);

    rcp_mock_server_destroy(srv);
}

static void test_response_queue_cfg_apply_reconfig_patches_addressed_octets_only(void)
{
    rcp_regmap_response_queue_cfg_t rows[2];
    uint8_t                         patch_row1_queue_size[2];

    rcp_regmap_response_queue_cfg_init(&rows[0]);
    rows[0].stream_uid = 1u;
    rcp_regmap_response_queue_cfg_init(&rows[1]);
    rows[1].stream_uid      = 2u;
    rows[1].max_avtpdu_size = 3u;
    rows[1].queue_size      = 4u;

    put_test_u16(patch_row1_queue_size, 0x1234u);

    /* Patch only row 1's own queue_size octets (relative address 14 =
     * 10*1+4). */
    TEST_ASSERT_EQUAL(RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_OK,
                       rcp_regmap_response_queue_cfg_apply_reconfig(rows, 2, 14u,
                                                                     patch_row1_queue_size, 2u));
    TEST_ASSERT_EQUAL_UINT16(1u, rows[0].stream_uid); /* row 0 untouched */
    TEST_ASSERT_EQUAL_UINT16(2u, rows[1].stream_uid);      /* row 1's other octets untouched */
    TEST_ASSERT_EQUAL_UINT16(3u, rows[1].max_avtpdu_size);
    TEST_ASSERT_EQUAL_UINT16(0x1234u, rows[1].queue_size); /* patched */
}

static void test_response_queue_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched(void)
{
    rcp_regmap_response_queue_cfg_t rows[1];
    uint8_t                         data[2] = {0x11u, 0x22u};

    rcp_regmap_response_queue_cfg_init(&rows[0]);
    rows[0].stream_uid = 9u;

    /* count=1 -> block_len=10; address 9 + data_len 2 = 11 > 10. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_OUT_OF_RANGE,
                       rcp_regmap_response_queue_cfg_apply_reconfig(rows, 1, 9u, data, 2u));
    TEST_ASSERT_EQUAL_UINT16(9u, rows[0].stream_uid); /* entirely unchanged */

    TEST_ASSERT_EQUAL(RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_SHORT,
                       rcp_regmap_response_queue_cfg_apply_reconfig(rows, 1, 0u, data, 0u));
}

static void test_response_queue_cfg_render_saturates_oversized_flush_time_us_without_wrapping(void)
{
    rcp_regmap_response_queue_cfg_t row;
    uint8_t                         out[10];

    rcp_regmap_response_queue_cfg_init(&row);
    row.flush_time_us = 0x10000u; /* one past the wire register's own 16-bit range */

    rcp_regmap_response_queue_cfg_render(&row, 1, out);

    /* Must saturate to 0xFFFF, NOT wrap to 0 -- wrapping would silently
     * flip this field's own meaning to TC18's "flush only by count"
     * encoding. */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[8]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[9]);
}

/* REQ-RMAP-073/074/075/076/077/078 (issue #311 batch 3): the READ side
 * of ep_generic_cfg's own wire codec -- see rcp_regmap_ep_generic_cfg_render()'s
 * own doc comment (regmap.h) for why apply_reconfig() is deliberately not
 * built in this same batch (ep_type is TC18's first read-only field mixed
 * into an otherwise R/W* row anywhere in this codebase). */
static void test_ep_generic_cfg_render_matches_table_28_byte_offsets(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     out[12];

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_type             = 0x03u; /* SPI, per Table 29/30's own ep_type enum */
    row.ep_used              = true;
    row.ep_delay_time        = 20u;  /* one of the 4 allowed values -- register 10b */
    row.ep_req_storage_size  = 8u;   /* 2 words */
    row.ep_description       = 0x11223344u;
    row.ep_tx_buffer_size    = 0x5566u;
    row.ep_rx_buffer_size    = 0x7788u;

    rcp_regmap_ep_generic_cfg_render(&row, 1, out);

    TEST_ASSERT_EQUAL_UINT8(0x03u, out[0]);              /* ep_type */
    TEST_ASSERT_EQUAL_UINT8(0x21u, out[1]);              /* ep_used=1 (bit0) | delay_reg=2 (bits4:5) */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[2]);              /* ep_req_storage_size hi (2 words) */
    TEST_ASSERT_EQUAL_UINT8(0x02u, out[3]);              /* ep_req_storage_size lo */
    TEST_ASSERT_EQUAL_UINT8(0x11u, out[4]);              /* ep_description */
    TEST_ASSERT_EQUAL_UINT8(0x22u, out[5]);
    TEST_ASSERT_EQUAL_UINT8(0x33u, out[6]);
    TEST_ASSERT_EQUAL_UINT8(0x44u, out[7]);
    TEST_ASSERT_EQUAL_UINT8(0x55u, out[8]);              /* ep_tx_buffer_size */
    TEST_ASSERT_EQUAL_UINT8(0x66u, out[9]);
    TEST_ASSERT_EQUAL_UINT8(0x77u, out[10]);             /* ep_rx_buffer_size */
    TEST_ASSERT_EQUAL_UINT8(0x88u, out[11]);
}

static void test_ep_generic_cfg_render_uses_12_octet_stride_across_entries(void)
{
    rcp_regmap_ep_generic_cfg_t rows[2];
    uint8_t                     out[24];

    rcp_regmap_ep_generic_cfg_init(&rows[0]);
    rcp_regmap_ep_generic_cfg_init(&rows[1]);
    rows[0].ep_type = 0xAAu;
    rows[1].ep_type = 0xBBu;

    rcp_regmap_ep_generic_cfg_render(rows, 2, out);

    TEST_ASSERT_EQUAL_UINT8(0xAAu, out[0]);  /* EP0's ep_type at relative 0x0000 */
    TEST_ASSERT_EQUAL_UINT8(0xBBu, out[12]); /* EP1's ep_type at relative 0x000C */
}

static void test_ep_generic_cfg_render_falls_back_to_1us_for_unconfigured_delay_time(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     out[12];

    rcp_regmap_ep_generic_cfg_init(&row); /* ep_delay_time left at its own zero-init
                                              default, 0us -- NOT one of TC18's 4
                                              allowed register values */

    rcp_regmap_ep_generic_cfg_render(&row, 1, out);

    /* Falls back to register 0 (1us), not an assertion failure or an
     * arbitrary bit pattern -- every freshly-initialized, not-yet-
     * configured endpoint must render cleanly. */
    TEST_ASSERT_EQUAL_UINT8(0x00u, (uint8_t)(out[1] & 0x30u));
}

static void test_ep_generic_cfg_render_falls_back_to_1us_for_any_disallowed_delay_value(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     out[12];

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_delay_time = 999999u; /* not one of {1,10,20,50} */

    rcp_regmap_ep_generic_cfg_render(&row, 1, out);

    TEST_ASSERT_EQUAL_UINT8(0x00u, (uint8_t)(out[1] & 0x30u)); /* falls back to 0 (1us), same as unconfigured */
}

static void test_ep_generic_cfg_render_clamps_oversized_req_storage_size_without_wrapping(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     out[12];

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_req_storage_size = 262144u; /* one word (4 octets) past the register's own max */

    rcp_regmap_ep_generic_cfg_render(&row, 1, out);

    /* Clamped to the register's own max word count (0xFFFF), not wrapped
     * to a small or zero value. */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[3]);
}

static void test_ep_generic_cfg_render_clamps_non_multiple_of_4_req_storage_size(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     out[12];

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_req_storage_size = 9u; /* not an exact multiple of 4 -- floors to 2 words (8 octets) */

    rcp_regmap_ep_generic_cfg_render(&row, 1, out);

    TEST_ASSERT_EQUAL_UINT8(0x00u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, out[3]);
}

/* REQ-RMAP-081 (issue #467, investigated 2026-08-16): TC18's own prose
 * immediately below Table 31 ("The configuration parameter
 * EP_RESP_ON_ERROR also switches on the gauging of the assigned physical
 * IO pins...") names a configuration parameter Table 31 itself never
 * actually defines -- confirmed via direct extraction of the primary-
 * source PDF page image (pdftotext -layout, physical pages 81-82): Table
 * 31's own row list for EP0 ends at ep_rx_buffer_size with no
 * ep_resp_on_error row anywhere, and the two spans octet 0x0001 does
 * reserve (relative 0x0001.1:3 and 0x0001.6:7) are both marked plain
 * "reserved". A full-document search finds "EP_RESP_ON_ERROR" exactly
 * once, in this same sentence -- a dangling reference, independently
 * confirmed three times by TC18_spec_defects_report.md item 22 and its
 * own review copies, not an unwired local gap. There is no bit position
 * anywhere in the document for c-RCP to model, so this test pins that
 * rcp_regmap_ep_generic_cfg_render() invents none: both reserved spans of
 * octet 1 stay zero for every input, including inputs deliberately chosen
 * to be non-zero/extreme everywhere else, so this assertion cannot pass
 * by accident of an all-zero row. See regmap.h's own REQ-RMAP-081 comment
 * (next to rcp_regmap_ep_generic_cfg_t) for the full investigation
 * writeup and .fusa-reqs.json's REQ-RMAP-081 entry for the catalog
 * record. */
static void test_ep_generic_cfg_render_has_no_ep_resp_on_error_bit_reserved_bits_stay_zero(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     out[12];

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_type             = 0xFFu;       /* read-only; non-default so this test's own mask
                                               isn't accidentally checking ep_type instead */
    row.ep_used              = true;
    row.ep_delay_time        = 50u;        /* one of the 4 allowed values -- register 11b */
    row.ep_req_storage_size  = 0xFFFFFFFFu; /* deliberately extreme -- an all-zero row would
                                                make this assertion pass trivially */
    row.ep_description       = 0xFFFFFFFFu;
    row.ep_tx_buffer_size    = 0xFFFFu;
    row.ep_rx_buffer_size    = 0xFFFFu;

    rcp_regmap_ep_generic_cfg_render(&row, 1, out);

    /* octet 1 (relative 0x0001): bit 0 is ep_used, bits [5:4] are
     * ep_delay_time -- TC18's own two reserved spans are bits [3:1] and
     * [7:6] (mask 0xCE). Neither holds an EP_RESP_ON_ERROR bit or any
     * other meaning; this function must never set either span regardless
     * of how every other field is populated. */
    TEST_ASSERT_EQUAL_UINT8(0x00u, (uint8_t)(out[1] & 0xCEu));
}

/* REQ-RMAP-079 (issue #311 batch 4): the WRITE side of ep_generic_cfg's
 * own wire codec -- see rcp_regmap_ep_generic_cfg_apply_reconfig()'s own
 * doc comment (regmap.h) for why this is a per-field, not per-buffer,
 * implementation. */
static void test_ep_generic_cfg_apply_reconfig_patches_addressed_octets_only(void)
{
    rcp_regmap_ep_generic_cfg_t rows[2];
    uint8_t                     patch[12] = {
        0xFFu,        /* ep_type -- must have NO effect, per TC18 §13.7.1.2 */
        0x21u,        /* ep_used=1, delay_reg=2 (20us) */
        0x00u, 0x02u, /* ep_req_storage_size: 2 words = 8 octets */
        0x11u, 0x22u, 0x33u, 0x44u, /* ep_description */
        0x55u, 0x66u, /* ep_tx_buffer_size */
        0x77u, 0x88u, /* ep_rx_buffer_size */
    };
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&rows[0]);
    rcp_regmap_ep_generic_cfg_init(&rows[1]);
    rows[0].ep_type = 0xAAu; /* the row's own original ep_type, must survive */

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(rows, 2, 0u, patch, sizeof(patch));

    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(0xAAu, rows[0].ep_type); /* unchanged -- read-only */
    TEST_ASSERT_TRUE(rows[0].ep_used);
    TEST_ASSERT_EQUAL_UINT32(20u, rows[0].ep_delay_time);
    TEST_ASSERT_EQUAL_UINT32(8u, rows[0].ep_req_storage_size);
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, rows[0].ep_description);
    TEST_ASSERT_EQUAL_UINT16(0x5566u, rows[0].ep_tx_buffer_size);
    TEST_ASSERT_EQUAL_UINT16(0x7788u, rows[0].ep_rx_buffer_size);
    /* row 1 (relative 12-23) entirely untouched by this write */
    TEST_ASSERT_EQUAL_UINT8(0u, rows[1].ep_type);
    TEST_ASSERT_FALSE(rows[1].ep_used);
}

static void test_ep_generic_cfg_apply_reconfig_write_touching_only_ep_type_is_a_no_op_confirmed_normally(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     patch[1] = {0xFFu};
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_type = 0x03u;

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(&row, 1, 0u, patch, sizeof(patch));

    /* TC18 §13.7.1.2: "Writing data to read only registers has no effect
     * and request is confirmed normally" -- OK, not an error, and the
     * field itself is untouched. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(0x03u, row.ep_type);
}

/* REQ-RMAP-016/REQ-RMAP-079 (issue #466): ep_used's own row-0-only
 * override -- TC18 Table 31's ep_used row states EP0's bit is "fixed to
 * 1 as EP0 needs to be always implemented", on top of the field's
 * otherwise general R/W* status for EP1..EPn. A write targeting row 0's
 * own ep_used bit must be silently ignored, the same "no effect,
 * confirmed normally" treatment ep_type gets above -- see this
 * function's own doc comment (regmap.h). */
static void test_ep_generic_cfg_apply_reconfig_row0_ep_used_write_is_ignored_stays_true(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     patch[1] = {0x00u}; /* ep_used=0, delay_reg=0 (1us) --
                                                         targets ONLY octet 0x0001 */
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_used = true; /* EP0's own required-always-on state */

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(&row, 1, 1u, patch, sizeof(patch));

    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    /* Forced -- the incoming ep_used=0 bit has no effect on row 0. */
    TEST_ASSERT_TRUE(row.ep_used);
    /* ep_delay_time (bits 4:5 of the same octet) is NOT part of this
     * override and is still updated normally from the write. */
    TEST_ASSERT_EQUAL_UINT32(1u, row.ep_delay_time);
}

/* Same override, exercised through a real 2-row EP_GENERIC_config table
 * so the "row 0 only" scoping (not "every row", not "no rows") is
 * checked directly rather than inferred from a single-row table. */
static void test_ep_generic_cfg_apply_reconfig_ep0_ep_used_forced_true_ep1_honors_write(void)
{
    rcp_regmap_ep_generic_cfg_t rows[2];
    uint8_t                     patch[2] = {
        0x00u, /* row 0 octet 0x0001: ep_used=0 -- must be ignored */
        0x00u, /* row 1 octet 0x0001: ep_used=0 -- must be honored */
    };
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&rows[0]);
    rcp_regmap_ep_generic_cfg_init(&rows[1]);
    rows[0].ep_used = true;  /* EP0's own required-always-on state */
    rows[1].ep_used = true;  /* EP1 starts on so the write's own effect is observable */

    /* relative_start_address=1 covers row 0's own octet 0x0001 (12*0+1);
     * relative_start_address=13 covers row 1's own octet 0x0001 (12*1+1).
     * Two separate single-byte writes, mirroring how every other test in
     * this file addresses one row's octet 0x0001 at a time. */
    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(rows, 2, 1u, &patch[0], 1u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    TEST_ASSERT_TRUE(rows[0].ep_used); /* row 0: forced, write ignored */

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(rows, 2, 13u, &patch[1], 1u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    TEST_ASSERT_FALSE(rows[1].ep_used); /* row 1: honored normally */

    /* And the general case still works the other direction too --
     * writing ep_used=1 to a non-EP0 row is not a no-op regression. */
    {
        uint8_t set_patch[1] = {0x01u}; /* ep_used=1, delay_reg=0 */

        rows[1].ep_used = false;
        rc = rcp_regmap_ep_generic_cfg_apply_reconfig(rows, 2, 13u, set_patch, sizeof(set_patch));
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
        TEST_ASSERT_TRUE(rows[1].ep_used);
    }
}

static void test_ep_generic_cfg_apply_reconfig_leaves_partially_covered_field_unchanged(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     patch[1] = {0x99u}; /* only byte 0 of the 2-byte
                                                         ep_req_storage_size field
                                                         (relative 0x0002-0x0003) */
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_req_storage_size = 40u; /* pre-existing value */

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(&row, 1, 2u, patch, sizeof(patch));

    /* A write covering only HALF of a multi-octet field must leave that
     * field entirely unchanged, not apply a corrupted partial value. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(40u, row.ep_req_storage_size);
}

static void test_ep_generic_cfg_apply_reconfig_does_not_launder_an_untouched_rows_own_invalid_delay_time(void)
{
    rcp_regmap_ep_generic_cfg_t rows[2];
    uint8_t                     patch[1] = {0xBBu}; /* row 1's own ep_type -- itself a no-op */
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&rows[0]);
    rcp_regmap_ep_generic_cfg_init(&rows[1]);
    /* row 0's own ep_delay_time is already NOT one of TC18's 4 allowed
     * values -- exactly the state rcp_regmap_ep_generic_cfg_render()'s
     * own fallback exists for. This is the motivating correctness case
     * from issue #311's own batch 4 scoping: a write to a DIFFERENT row
     * must never silently "correct" this via a render()-then-reparse
     * round trip. */
    rows[0].ep_delay_time = 999999u;

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(rows, 2, 12u, patch, sizeof(patch));

    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    /* row 0 is untouched by this write (it targets row 1's own relative
     * 12) -- its own invalid ep_delay_time must survive exactly as-is,
     * NOT be silently laundered to 1us via render()'s own fallback. */
    TEST_ASSERT_EQUAL_UINT32(999999u, rows[0].ep_delay_time);
}

static void test_ep_generic_cfg_apply_reconfig_extracts_delay_time_register_value(void)
{
    /* Row 1 (EP1), NOT row 0 -- row 0's own ep_used bit is forced true by
     * issue #466's own row-0-only override (see the dedicated
     * test_ep_generic_cfg_apply_reconfig_row0_ep_used_write_is_ignored_*
     * tests above), so this general-case, non-EP0 extraction check uses
     * a row where ep_used still honors the incoming bit normally. */
    rcp_regmap_ep_generic_cfg_t rows[2];
    uint8_t                     patch[1] = {0x30u}; /* bits 4:5 = 11b = 50us */
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&rows[0]);
    rcp_regmap_ep_generic_cfg_init(&rows[1]);

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(rows, 2, 13u, patch, sizeof(patch));

    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(50u, rows[1].ep_delay_time);
    TEST_ASSERT_FALSE(rows[1].ep_used); /* bit 0 of 0x30 is 0 */
}

static void test_ep_generic_cfg_apply_reconfig_rejects_short_payload(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&row);

    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(&row, 1, 0u, NULL, 0u);

    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_SHORT, rc);
}

static void test_ep_generic_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched(void)
{
    rcp_regmap_ep_generic_cfg_t row;
    uint8_t                     patch[1] = {0xFFu};
    rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

    rcp_regmap_ep_generic_cfg_init(&row);
    row.ep_description = 0x12345678u;

    /* 1 row = 12 octets, relative address 12 is one past the extent */
    rc = rcp_regmap_ep_generic_cfg_apply_reconfig(&row, 1, 12u, patch, sizeof(patch));

    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_OUT_OF_RANGE, rc);
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, row.ep_description); /* untouched */
}

/* REQ-RMAP-047/048/049 CLOSED (write-dispatch half, issue #306): same
 * finding as issue #301's own three tables, applied to request-stream-cfg
 * -- svr_request_stream_cfg_ptr's own value is an absolute address in the
 * same EP0-scoped space Table 18 itself lives in. New
 * rcp_regmap_request_stream_cfg_apply_reconfig() is the parse-side
 * inverse of rcp_regmap_request_stream_cfg_render(); see this table's own
 * file-header note (regmap.h) for the three deliberately-excluded fields
 * and two saturating width-mismatch fixes. */
static void test_request_stream_cfg_apply_reconfig_patches_addressed_octets_only(void)
{
    rcp_regmap_request_stream_cfg_t rows[2];
    uint8_t                         patch_row1_secure_channel[1] = {0x09u};

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rows[0].rx_stream_id = 1u;
    rcp_regmap_request_stream_cfg_init(&rows[1]);
    rows[1].rx_stream_id            = 2u;
    rows[1].rx_secure_channel_index = 3u;

    /* Patch only row 1's own rx_secure_channel_index octet (relative
     * address 36 = 24*1+12). */
    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 2, 36u,
                                                                     patch_row1_secure_channel, 1u, 0u));
    TEST_ASSERT_EQUAL_UINT64(1u, rows[0].rx_stream_id); /* row 0 untouched */
    TEST_ASSERT_EQUAL_UINT64(2u, rows[1].rx_stream_id); /* row 1's other octets untouched */
    TEST_ASSERT_EQUAL_UINT8(0x09u, rows[1].rx_secure_channel_index); /* patched */
}

static void test_request_stream_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched(void)
{
    rcp_regmap_request_stream_cfg_t rows[1];
    uint8_t                         data[2] = {0x11u, 0x22u};

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rows[0].rx_stream_id = 9u;

    /* count=1 -> block_len=24; address 23 + data_len 2 = 25 > 24. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_OUT_OF_RANGE,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 1, 23u, data, 2u, 0u));
    TEST_ASSERT_EQUAL_UINT64(9u, rows[0].rx_stream_id); /* entirely unchanged */

    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_SHORT,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 1, 0u, data, 0u, 0u));
}

static void test_request_stream_cfg_render_saturates_oversized_max_request_size_without_wrapping(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);
    row.rx_stream_max_request_size = 0x10000u; /* one past the wire register's own 16-bit range */

    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);

    /* Must saturate to 0xFFFF, NOT wrap to 0 -- wrapping would silently
     * flip this field's own meaning to TC18's "no fragmentation
     * supported" encoding. */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[0x0008]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[0x0009]);
}

static void test_request_stream_cfg_render_saturates_oversized_safestate_sequencer_without_wrapping(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);
    row.rx_safestate_sequencer = 0x0100u; /* one past the wire register's own 8-bit range */

    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);

    /* Must saturate to 0xFF, NOT wrap to 0x00 -- wrapping would silently
     * alias onto sequencer index 0, an entirely different, actually-
     * existing target for a safety-relevant field. */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[0x000E]);
}

/* CORRECTED 2026-08-14 (issue #458): TC18 Table 24's own 0x000D octet's
 * REAL RC5 layout is 4 meaningful bits, not this codebase's old
 * RC1-baseline 8-independent-bit model an earlier revision of this test
 * proved (regmap.h's own "TC18 0.5.1_RC5 terminology drift" file-header
 * note has the full reconciliation) -- bit0 rx_enforce_crc
 * (rx_enforce_e2e), bit1 rx_enforce_sequence (rx_enforce_seq AND
 * rx_seq_safestate_enable), bit2 rx_enforce_watchdog (rx_wd_enable AND
 * rx_wd_safestate_enable), bit3 rx_enforce_request_filing
 * (rx_ovrflw_safestate_enable), bits [6:4] Reserved (always 0, no
 * field -- rx_safety_measure no longer has a wire position at all).
 * Bit 7 is NOT one of these -- REQ-E2E-046/REQ-RMAP-051 (issue #424):
 * it is TC18's own distinct, live rx_stream_status bit, covered by the
 * dedicated tests below, not by this one. rx_wd_info_enable set true
 * here (and never appearing in the expected byte) proves it no longer
 * leaks into bit 7. */
static void test_request_stream_cfg_render_packs_four_config_bits_at_0x000d(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);
    row.rx_enforce_e2e    = true; /* bit 0 */
    row.rx_wd_info_enable = true; /* no wire position anymore -- must NOT appear */
    /* every other bit left false */

    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[0x000D]);

    row.rx_ovrflw_safestate_enable = true; /* bit 3: single dimension, direct map */
    row.rx_safety_measure          = 1u;   /* Reserved bit 6 -- must NOT appear */

    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x09u, out[0x000D]); /* bit0 | bit3, bits [6:4] still 0 */
}

/* AND-not-OR merge rule (issue #458): bit1/bit2 each collapse TWO
 * independently-expressible internal dimensions into ONE real wire bit
 * whose own TC18 text ties "block" and "enter safe state" together
 * atomically -- render() must render true ONLY when BOTH dimensions of
 * a pair agree, never when only one is set (that would overstate a
 * safety guarantee to a real RC5 peer reading this register). */
static void test_request_stream_cfg_render_couples_sequence_bit_with_and_not_or(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);

    row.rx_enforce_seq = true; /* enable only, safestate off */
    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[0x000D]); /* NOT bit1 -- decoupled, no wire bit */

    row.rx_enforce_seq          = false;
    row.rx_seq_safestate_enable = true; /* safestate only, enable off */
    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[0x000D]); /* NOT bit1 -- still decoupled */

    row.rx_enforce_seq = true; /* both true: the real RC5-expressible coupled state */
    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x02u, out[0x000D]); /* bit1 set */
}

static void test_request_stream_cfg_render_couples_watchdog_bit_with_and_not_or(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);

    row.rx_wd_enable = true; /* enable only, safestate off */
    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[0x000D]); /* NOT bit2 -- decoupled, no wire bit */

    row.rx_wd_enable           = false;
    row.rx_wd_safestate_enable = true; /* safestate only, enable off */
    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[0x000D]); /* NOT bit2 -- still decoupled */

    row.rx_wd_enable = true; /* both true: the real RC5-expressible coupled state */
    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x04u, out[0x000D]); /* bit2 set */
}

/* apply_reconfig() write-side half of the same reconciliation: a real
 * RC5 write can only ever express the coupled state (there is no wire
 * encoding for "block but don't enter safe state"), so writing bit1/bit2
 * sets BOTH of a pair's own internal dimensions together -- and a write
 * confined to the Reserved bits [6:4] (which used to be rx_wd_safestate_
 * enable/rx_ovrflw_safestate_enable/rx_safety_measure's own OLD,
 * RC1-baseline positions) has NO effect on any struct field, exactly
 * like this table's own reserved trailing octets. */
static void test_request_stream_cfg_apply_reconfig_couples_sequence_and_watchdog_bits(void)
{
    rcp_regmap_request_stream_cfg_t rows[1];
    uint8_t                         patch_seq[1] = {0x02u};
    uint8_t                         patch_wd[1]  = {0x04u};
    uint8_t                         patch_reserved[1] = {0x70u}; /* bits [6:4] only */

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 1, 0x000Du, patch_seq,
                                                                     1u, 0u));
    TEST_ASSERT_TRUE(rows[0].rx_enforce_seq);
    TEST_ASSERT_TRUE(rows[0].rx_seq_safestate_enable);
    TEST_ASSERT_FALSE(rows[0].rx_wd_enable);
    TEST_ASSERT_FALSE(rows[0].rx_wd_safestate_enable);

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 1, 0x000Du, patch_wd, 1u,
                                                                     0u));
    TEST_ASSERT_TRUE(rows[0].rx_wd_enable);
    TEST_ASSERT_TRUE(rows[0].rx_wd_safestate_enable);
    TEST_ASSERT_FALSE(rows[0].rx_enforce_seq);
    TEST_ASSERT_FALSE(rows[0].rx_seq_safestate_enable);

    /* A write confined to the Reserved bits [6:4] leaves rx_safety_measure
     * (this field's own OLD, RC1-baseline bit-6 position) completely
     * unaffected -- it has NO wire register position anymore. */
    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rows[0].rx_safety_measure = 1u;
    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 1, 0x000Du,
                                                                     patch_reserved, 1u, 0u));
    TEST_ASSERT_EQUAL_UINT8(1u, rows[0].rx_safety_measure); /* unchanged */
    TEST_ASSERT_FALSE(rows[0].rx_enforce_seq);              /* unaffected */
    TEST_ASSERT_FALSE(rows[0].rx_wd_enable);                /* unaffected */
    TEST_ASSERT_FALSE(rows[0].rx_ovrflw_safestate_enable);  /* unaffected */
}

/* REQ-E2E-046/REQ-RMAP-051 (issue #424): TC18 Table 24's own 0x000D.7
 * (rx_stream_status) is a distinct, plain R/W LIVE status bit -- c-RCP
 * used to mis-wire this exact bit position to rx_wd_info_enable instead
 * (a plain bug, not an instance of that field's own separate, still-open
 * RC5-mapping ambiguity -- see that field's own doc comment, regmap.h).
 * It now reflects a caller-supplied, index-parallel
 * rx_stream_status_blocked[] array -- the same NULL-means-"not
 * live-known, render 0" convention this file's other index-parallel
 * array parameters (e.g. ep_types[]) already establish. */
static void test_request_stream_cfg_render_wires_rx_stream_status_bit_from_live_array(void)
{
    rcp_regmap_request_stream_cfg_t rows[2];
    uint8_t                         out[48];
    bool                            blocked[2] = {true, false};

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rcp_regmap_request_stream_cfg_init(&rows[1]);
    rows[0].rx_wd_info_enable = true; /* must NOT leak into bit 7 anymore */
    rows[1].rx_wd_info_enable = true;

    rcp_regmap_request_stream_cfg_render(rows, 2, out, 0u, blocked);
    TEST_ASSERT_EQUAL_UINT8(0x80u, out[0x000D]);       /* row 0: blocked */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[24u + 0x000D]); /* row 1: not blocked */

    /* NULL (no live status known) renders as not-blocked, same as every
     * other NULL-able index-parallel array this file already uses. */
    rcp_regmap_request_stream_cfg_render(rows, 2, out, 0u, NULL);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[0x000D]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[24u + 0x000D]);
}

/* REQ-E2E-046/REQ-RMAP-051 (issue #424): the live rx_stream_status bit is
 * now visible through an actual EP0 register READ, not only through
 * rcp_e2e_stream_status_rx_blocked()'s own direct API accessor -- a real
 * CRC fault is injected via e2e.h's own genuine
 * rcp_e2e_stream_status_note_crc_error() latch (not a synthetic bit
 * flip), the resulting aggregate is threaded into
 * rcp_regmap_ep0_encode_read_response()'s new
 * request_stream_status_blocked parameter, and the decoded response
 * payload's own wire bit 0x000D.7 is confirmed to carry it -- then
 * confirmed to clear again after rcp_e2e_stream_status_reset_crc(),
 * proving this is a genuinely live read, not a one-way latch in the test
 * itself. */
static void test_ep0_read_dispatcher_surfaces_live_rx_stream_status_from_a_real_crc_fault(void)
{
    rcp_acf_byte_message_info_t     req_hdr = {0};
    rcp_regmap_general_t            map;
    rcp_regmap_request_stream_cfg_t request_stream_cfg[1];
    rcp_e2e_stream_status_t         status;
    bool                             blocked[1];
    uint8_t                         payload[2];
    rcp_bytes_t                     req_frame, resp_frame;
    uint16_t                        decoded_addr;
    uint8_t                         decoded_read_size, decoded_tn;
    rcp_regmap_ep0_errc_t           rc;
    rcp_wire_error_t                err;
    rcp_acf_byte_message_info_t     resp_hdr;
    const uint8_t                   *resp_payload;
    size_t                           resp_payload_len;

    rcp_regmap_request_stream_cfg_init(&request_stream_cfg[0]);
    rcp_regmap_general_init(&map);
    map.svr_request_stream_cfg_ptr = 0x0400u;

    rcp_e2e_stream_status_init(&status);
    /* rx_enforce_e2e=true so a CRC_ERROR genuinely latches the stream
     * fault, matching rcp_e2e_stream_fault_on_crc_error()'s own real
     * decision rule -- not a synthetic "just set the bool" stimulus. */
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_note_crc_error(&status, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_status_rx_blocked(&status)); /* sanity-check the
                                                                     direct accessor first */
    blocked[0] = rcp_e2e_stream_status_rx_blocked(&status);

    put_test_u16(payload, (uint16_t)map.svr_request_stream_cfg_ptr);
    req_hdr.byte_bus_id              = RCP_REGMAP_EP0_INDEX;
    req_hdr.op                       = RCP_ACF_OP_READ;
    req_hdr.read_size_or_segment_num = 24u;
    req_hdr.transaction_num          = 3;
    req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(req_frame.data);

    rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len, &decoded_addr,
                                              &decoded_read_size, &decoded_tn);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    rcp_bytes_free(&req_frame);

    resp_frame = rcp_regmap_ep0_encode_read_response(
        decoded_addr, decoded_read_size, decoded_tn, &map, NULL, 0u, NULL, 0u, NULL, 0u,
        request_stream_cfg, 1u, NULL, 0u, NULL, NULL, 0u, &err, 0u, NULL, blocked);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_NOT_NULL(resp_frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                                       &resp_payload, &resp_payload_len));
    TEST_ASSERT_EQUAL_UINT8(0x80u, (uint8_t)(resp_payload[0x000D] & 0x80u));
    rcp_bytes_free(&resp_frame);

    /* Reset the fault and confirm the SAME dispatcher path now reflects
     * not-blocked too. */
    rcp_e2e_stream_status_reset_crc(&status);
    blocked[0] = rcp_e2e_stream_status_rx_blocked(&status);
    TEST_ASSERT_FALSE(blocked[0]);

    req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(req_frame.data);
    rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len, &decoded_addr,
                                              &decoded_read_size, &decoded_tn);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    rcp_bytes_free(&req_frame);

    resp_frame = rcp_regmap_ep0_encode_read_response(
        decoded_addr, decoded_read_size, decoded_tn, &map, NULL, 0u, NULL, 0u, NULL, 0u,
        request_stream_cfg, 1u, NULL, 0u, NULL, NULL, 0u, &err, 0u, NULL, blocked);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                                       &resp_payload, &resp_payload_len));
    TEST_ASSERT_EQUAL_UINT8(0x00u, (uint8_t)(resp_payload[0x000D] & 0x80u));
    rcp_bytes_free(&resp_frame);
}

/* REQ-RMAP-051 (issue #424): rx_stream_status (row-relative 0x000D bit 7)
 * is plain R/W, not R/W* -- a write confined to that one bit must still
 * be authorized once FUNCTIONAL_W_STAR's own window has closed
 * (RCP_CONFIGURED), while a write that ALSO touches any of the seven
 * genuinely R/W* enforcement bits sharing that same octet remains
 * denied, proving the carve-out cannot be used to smuggle a change to
 * those safety-relevant bits through. */
static void test_ep0_write_dispatcher_authorizes_rx_stream_status_bit_even_when_rcp_configured(void)
{
    rcp_acf_byte_message_info_t     hdr = {0};
    rcp_regmap_general_t            map;
    rcp_regmap_request_stream_cfg_t request_stream_cfg[1];
    rcp_bytes_t                     frame;
    rcp_wire_error_t                err;
    uint8_t                         tn;
    rcp_regmap_ep0_errc_t           rc;

    rcp_regmap_request_stream_cfg_init(&request_stream_cfg[0]);
    rcp_regmap_general_init(&map);
    map.svr_request_stream_cfg_ptr = 0x0400u;

    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;

    /* 1) A write confined to row-relative 0x000D whose only nonzero bit
     * is bit 7 (0x80) is AUTHORIZED even in RCP_CONFIGURED, since it
     * leaves the seven R/W* bits sharing that octet at their own current
     * (all-false, 0x00) value -- unchanged. */
    {
        uint8_t payload[3];

        put_test_u16(payload, (uint16_t)(map.svr_request_stream_cfg_ptr + 0x000Du));
        payload[2] = 0x80u;
        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);

        rc = rcp_regmap_ep0_decode_write_request(
            frame.data, frame.len, &map, RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER, NULL, 0u,
            NULL, 0u, NULL, 0u, request_stream_cfg, 1u, NULL, 0u, NULL, NULL, 0u, 0u, &err, &tn,
            0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        rcp_bytes_free(&frame);
    }

    /* 2) The identical write, but bit 0 (rx_enforce_e2e, genuinely R/W*)
     * ALSO set -- still RCP_CONFIGURED, still denied: the carve-out must
     * not let a status-bit write smuggle an enforcement-bit change
     * through. */
    {
        uint8_t payload[3];

        put_test_u16(payload, (uint16_t)(map.svr_request_stream_cfg_ptr + 0x000Du));
        payload[2] = 0x81u;
        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);

        rc = rcp_regmap_ep0_decode_write_request(
            frame.data, frame.len, &map, RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER, NULL, 0u,
            NULL, 0u, NULL, 0u, request_stream_cfg, 1u, NULL, 0u, NULL, NULL, 0u, 0u, &err, &tn,
            0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_NOT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_FALSE(request_stream_cfg[0].rx_enforce_e2e); /* unchanged */
        rcp_bytes_free(&frame);
    }

    /* 3) In HW_UNCONFIGURED, the SAME bit-0-set write from case 2 is
     * authorized (FUNCTIONAL_W_STAR's own ordinary window, no carve-out
     * needed) -- proves case 2's denial was genuinely about
     * RCP_CONFIGURED, not some unrelated bug in this new code path. */
    {
        uint8_t payload[3];

        put_test_u16(payload, (uint16_t)(map.svr_request_stream_cfg_ptr + 0x000Du));
        payload[2] = 0x81u;
        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);

        rc = rcp_regmap_ep0_decode_write_request(
            frame.data, frame.len, &map, RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER, NULL, 0u,
            NULL, 0u, NULL, 0u, request_stream_cfg, 1u, NULL, 0u, NULL, NULL, 0u, 0u, &err, &tn,
            0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_TRUE(request_stream_cfg[0].rx_enforce_e2e);
        rcp_bytes_free(&frame);
    }
}

/* REQ-RMAP-050: watchdog_ms_per_tick == 0 ("not configured") makes
 * rcp_regmap_wd_timeout_ms_to_ticks() fail unconditionally, so
 * rx_wd_timeout_ms falls back to 0x0000 exactly like the reserved
 * trailing octets -- see this table's own file-header note (regmap.h)
 * for the full fail-closed rationale. */
static void test_request_stream_cfg_render_falls_back_to_zero_when_watchdog_tick_rate_unconfigured(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);
    row.rx_wd_timeout_ms = 12345u;
    row.rx_wd_action     = 7u; /* no TC18 register at all -- excluded entirely */

    rcp_regmap_request_stream_cfg_render(&row, 1, out, 0u, NULL);

    TEST_ASSERT_EQUAL_UINT8(0u, out[0x000A]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x000B]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x0012]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x0013]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x0014]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x0015]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x0016]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x0017]);
}

/* REQ-RMAP-050, the real (configured) path: a 1000 ms timeout at 10
 * ms/tick renders as exactly 100 ticks (0x0064), matching
 * rcp_regmap_wd_timeout_ms_to_ticks()'s own already-unit-tested
 * round-down/bounds-check behavior, now actually reachable on the wire. */
static void test_request_stream_cfg_render_produces_real_ticks_when_watchdog_tick_rate_configured(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);
    row.rx_wd_timeout_ms = 1000u;

    rcp_regmap_request_stream_cfg_render(&row, 1, out, 10u, NULL);

    TEST_ASSERT_EQUAL_UINT8(0x00u, out[0x000A]);
    TEST_ASSERT_EQUAL_UINT8(0x64u, out[0x000B]);
}

/* The other half of REQ-RMAP-050's own render-side fallback: an
 * internal ms value that does not fit the register's 16-bit tick width
 * even at the configured rate (here: 700000 ms at 10 ms/tick would need
 * 70000 ticks, one past UINT16_MAX) falls back to 0x0000 the same way
 * an unconfigured rate does -- render() has no error-return mechanism,
 * so this is the only representable outcome for an input a conformant
 * caller should never have accepted in the first place. */
static void test_request_stream_cfg_render_falls_back_to_zero_when_ms_value_does_not_fit_even_configured(void)
{
    rcp_regmap_request_stream_cfg_t row;
    uint8_t                         out[24];

    rcp_regmap_request_stream_cfg_init(&row);
    row.rx_wd_timeout_ms = 700000u;

    rcp_regmap_request_stream_cfg_render(&row, 1, out, 10u, NULL);

    TEST_ASSERT_EQUAL_UINT8(0u, out[0x000A]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0x000B]);
}

/* REQ-RMAP-050's write direction: a wire-arriving ticks value (always
 * exactly 16 bits, so it can never itself violate a width constraint)
 * converts to ms using the caller-supplied rate and lands in
 * rx_wd_timeout_ms -- the round-trip inverse of the render test above. */
static void test_request_stream_cfg_apply_reconfig_converts_ticks_to_ms_when_watchdog_tick_rate_configured(void)
{
    rcp_regmap_request_stream_cfg_t rows[1];
    uint8_t                         patch[2] = {0x00u, 0x64u}; /* 100 ticks */

    rcp_regmap_request_stream_cfg_init(&rows[0]);

    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 1, 0x000Au, patch, 2u, 10u));
    TEST_ASSERT_EQUAL_UINT32(1000u, rows[0].rx_wd_timeout_ms);
}

/* When watchdog_ms_per_tick is unconfigured (0), the ticks-to-ms
 * conversion fails and rx_wd_timeout_ms is left at its own prior value
 * rather than clobbered to some meaningless derived number -- the same
 * "can't faithfully round-trip this field, so don't touch it" choice
 * render()'s own fallback makes for the opposite direction. Confirms
 * the whole apply_reconfig() call still succeeds (OK, not an error) --
 * the arriving wire value is always valid 16-bit ticks regardless of
 * whether this library can currently interpret them as milliseconds. */
static void test_request_stream_cfg_apply_reconfig_leaves_rx_wd_timeout_ms_unchanged_when_unconfigured(void)
{
    rcp_regmap_request_stream_cfg_t rows[1];
    uint8_t                         patch[2] = {0x00u, 0x64u}; /* 100 ticks */

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rows[0].rx_wd_timeout_ms = 42u;

    TEST_ASSERT_EQUAL(RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK,
                       rcp_regmap_request_stream_cfg_apply_reconfig(rows, 1, 0x000Au, patch, 2u, 0u));
    TEST_ASSERT_EQUAL_UINT32(42u, rows[0].rx_wd_timeout_ms); /* untouched, not zeroed */
}

/* ── REQ-SEQ-013: rcp_regmap_request_stream_cfg_resolve_index() ──────────── */

static void test_resolve_index_matches_by_rx_stream_id(void)
{
    rcp_regmap_request_stream_cfg_t rows[2];

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rows[0].rx_stream_id = 1111u;
    rcp_regmap_request_stream_cfg_init(&rows[1]);
    rows[1].rx_stream_id = 2222u;

    /* 1-based: rows[0] is request_stream_index 1, rows[1] is 2 -- the
     * same convention rcp_regmap_ep_id_map_entry_t.request_stream_index
     * already establishes (REQ-RMAP-052). */
    TEST_ASSERT_EQUAL_UINT8(1u, rcp_regmap_request_stream_cfg_resolve_index(rows, 2, 1111u));
    TEST_ASSERT_EQUAL_UINT8(2u, rcp_regmap_request_stream_cfg_resolve_index(rows, 2, 2222u));
}

static void test_resolve_index_no_match_returns_zero_sentinel(void)
{
    rcp_regmap_request_stream_cfg_t rows[1];

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rows[0].rx_stream_id = 1111u;

    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_request_stream_cfg_resolve_index(rows, 1, 9999u));
}

static void test_resolve_index_null_or_empty_returns_zero(void)
{
    rcp_regmap_request_stream_cfg_t rows[1];

    rcp_regmap_request_stream_cfg_init(&rows[0]);
    rows[0].rx_stream_id = 1111u;

    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_request_stream_cfg_resolve_index(NULL, 0, 1111u));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_request_stream_cfg_resolve_index(rows, 0, 1111u));
}

/* ── REQ-SEQ-013/REQ-SEQ-014: rcp_regmap_sequencer_table_render()/
 *    _apply_reconfig() ──────────────────────────────────────────────────── */

static void test_sequencer_table_render_interleaves_state_and_owner(void)
{
    uint8_t state[2] = {0x01u, 0x02u};
    uint8_t owner[2] = {0x0Au, 0x14u};
    uint8_t out[4];

    rcp_regmap_sequencer_table_render(state, owner, 2, out);
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0Au, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x14u, out[3]);
}

static void test_sequencer_table_apply_reconfig_patches_addressed_octets_only(void)
{
    uint8_t state[2] = {0x01u, 0x02u};
    uint8_t owner[2] = {0x0Au, 0x14u};
    uint8_t patch[1] = {0x99u};

    /* Patch only sequencer 1's own Seq_state octet (relative address 2). */
    TEST_ASSERT_EQUAL(RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_OK,
                       rcp_regmap_sequencer_table_apply_reconfig(state, owner, 2, 2u, patch, 1u));
    TEST_ASSERT_EQUAL_UINT8(0x01u, state[0]); /* sequencer 0 untouched */
    TEST_ASSERT_EQUAL_UINT8(0x99u, state[1]); /* patched */
    TEST_ASSERT_EQUAL_UINT8(0x14u, owner[1]); /* owner untouched */
}

static void test_sequencer_table_apply_reconfig_rejects_out_of_range_leaving_table_untouched(void)
{
    uint8_t state[1] = {0x01u};
    uint8_t owner[1] = {0x0Au};
    uint8_t patch[2] = {0x11u, 0x22u};

    /* count=1 -> block_len=2; address 1 + data_len 2 = 3 > 2. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_ERR_OUT_OF_RANGE,
                       rcp_regmap_sequencer_table_apply_reconfig(state, owner, 1, 1u, patch, 2u));
    TEST_ASSERT_EQUAL_UINT8(0x01u, state[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0Au, owner[0]);

    TEST_ASSERT_EQUAL(RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_ERR_SHORT,
                       rcp_regmap_sequencer_table_apply_reconfig(state, owner, 1, 0u, patch, 0u));
}

/* ── REQ-SEQ-013: rcp_regmap_ep0_decode_write_request()'s own ownership-
 *    aware sequencer authorization ──────────────────────────────────────── */

static void test_ep0_write_dispatcher_denies_seq_state_write_when_sequencer_unclaimed(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {1u};
    uint8_t                     sequencer_owner[1] = {RCP_SEQUENCER_OWNER_UNCLAIMED};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    put_test_u16(payload, (uint16_t)map.svr_sequencer_state_ptr); /* Seq_state, sequencer 0 */
    payload[2] = 5u;
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 3u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, err);
    TEST_ASSERT_EQUAL_UINT8(1u, sequencer_state[0]); /* unchanged */
    rcp_bytes_free(&frame);
}

static void test_ep0_write_dispatcher_permits_seq_state_write_by_the_recorded_owner(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {1u};
    uint8_t                     sequencer_owner[1] = {3u};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    put_test_u16(payload, (uint16_t)map.svr_sequencer_state_ptr);
    payload[2] = 5u;
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 3u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT8(5u, sequencer_state[0]); /* applied */
    rcp_bytes_free(&frame);
}

static void test_ep0_write_dispatcher_denies_seq_state_write_by_a_different_client(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {1u};
    uint8_t                     sequencer_owner[1] = {3u};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    put_test_u16(payload, (uint16_t)map.svr_sequencer_state_ptr);
    payload[2] = 5u;
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    /* requester_stream_index=4, but sequencer 0 is owned by client 3. */
    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 4u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, err);
    TEST_ASSERT_EQUAL_UINT8(1u, sequencer_state[0]); /* unchanged */
    rcp_bytes_free(&frame);
}

static void test_ep0_write_dispatcher_permits_claiming_an_unclaimed_sequencer(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {1u};
    uint8_t                     sequencer_owner[1] = {RCP_SEQUENCER_OWNER_UNCLAIMED};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    /* Request_stream_index octet, sequencer 0 (relative address 1). */
    put_test_u16(payload, (uint16_t)(map.svr_sequencer_state_ptr + 1u));
    payload[2] = 7u; /* claim as client 7 */
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 7u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT8(7u, sequencer_owner[0]); /* claimed */
    rcp_bytes_free(&frame);
}

static void test_ep0_write_dispatcher_denies_stealing_an_already_claimed_sequencer(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {1u};
    uint8_t                     sequencer_owner[1] = {3u};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    put_test_u16(payload, (uint16_t)(map.svr_sequencer_state_ptr + 1u));
    payload[2] = 9u; /* client 9 tries to steal sequencer 0 from client 3 */
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 9u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, err);
    TEST_ASSERT_EQUAL_UINT8(3u, sequencer_owner[0]); /* unchanged -- not stolen */
    rcp_bytes_free(&frame);
}

static void test_ep0_write_dispatcher_permits_owner_releasing_its_own_sequencer(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {1u};
    uint8_t                     sequencer_owner[1] = {3u};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    put_test_u16(payload, (uint16_t)(map.svr_sequencer_state_ptr + 1u));
    payload[2] = RCP_SEQUENCER_OWNER_UNCLAIMED; /* client 3 releases its own sequencer */
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 3u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_OWNER_UNCLAIMED, sequencer_owner[0]); /* released */
    rcp_bytes_free(&frame);
}

/* The ordinary FUNCTIONAL_W_STAR lifecycle gate still applies on top of
 * the ownership check -- an otherwise-authorized owner still can't
 * write outside a writable lifecycle state (TC18 Table 24's own legend:
 * FUNCTIONAL_W_STAR is permanently locked once RCP_CONFIGURED is
 * reached, regardless of writer identity -- see
 * rcp_lifecycle_field_writable()'s own FUNCTIONAL_W_STAR case). */
static void test_ep0_write_dispatcher_still_enforces_functional_w_star_for_sequencer_writes(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {1u};
    uint8_t                     sequencer_owner[1] = {3u};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    put_test_u16(payload, (uint16_t)map.svr_sequencer_state_ptr);
    payload[2] = 5u;
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    /* RCP_CONFIGURED locks FUNCTIONAL_W_STAR unconditionally -- even
     * ROOT_WRITER, and even though requester_stream_index (3) already
     * matches sequencer 0's own recorded owner, is denied before
     * ownership is ever consulted. */
    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 3u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_NOT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT8(1u, sequencer_state[0]); /* unchanged */
    rcp_bytes_free(&frame);
}

/* REQ-RMAP-040/041/047/048/049/052/054/061 CLOSED (write-authorization
 * half, issue #308): rcp_regmap_ep0_decode_write_request() now consults
 * lifecycle-state/writer/W+-lock authorization for every one of its own
 * four routed tables BEFORE applying a write or even consulting that
 * table's own bounds check -- see this function's own doc comment
 * (regmap.h) for the full per-table access-type classification. */
static void test_ep0_dispatcher_denies_unauthorized_writes_before_applying_or_bounds_checking(void)
{
    rcp_acf_byte_message_info_t     hdr = {0};
    rcp_bytes_t                     frame;
    rcp_regmap_general_t            map;
    rcp_regmap_hw_pin_map_entry_t   hw_pin_map[1]        = {{1, 2, 0x03u}};
    rcp_regmap_ep_id_map_entry_t    ep_id_map[1]         = {{10u, 20u, 1u}};
    rcp_regmap_response_queue_cfg_t response_queue_cfg[1];
    rcp_regmap_request_stream_cfg_t request_stream_cfg[1];
    rcp_regmap_ep_generic_cfg_t     ep_generic_cfg[1];
    rcp_wire_error_t                err;
    uint8_t                         tn = 0;
    rcp_regmap_ep0_errc_t           rc;

    rcp_regmap_response_queue_cfg_init(&response_queue_cfg[0]);
    rcp_regmap_request_stream_cfg_init(&request_stream_cfg[0]);
    rcp_regmap_ep_generic_cfg_init(&ep_generic_cfg[0]);

    rcp_regmap_general_init(&map);
    map.svr_hw_cfg_ptr              = 0x0100u;
    map.svr_ep_bytebus_id_map_ptr   = 0x0200u;
    map.svr_response_stream_cfg_ptr = 0x0300u;
    map.svr_request_stream_cfg_ptr  = 0x0400u;
    map.svr_ep_generic_cfg_ptr      = 0x0500u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op               = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 22;

    /* 1) RCP_CONFIGURED state denies HW_config (W*) even for ROOT_WRITER
     * -- state alone forbids it, matching FUNCTIONAL_W_STAR's own
     * "permanently locked once RCP_CONFIGURED" rule. The write's own
     * address+length is ALSO out of the table's own current extent
     * (count=1 -> 3 octets; this write starts at +2 with 2 octets,
     * relative+data_len = 2+2 = 4 > 3) while its own STARTING address
     * still routes into HW_config's own extent, proving authorization
     * is checked BEFORE apply_reconfig()'s own bounds check: the denial
     * is LOCKED_MEM_ACCESS, not INVALID_PARAMETER. */
    {
        uint8_t payload[4];

        put_test_u16(payload, (uint16_t)(map.svr_hw_cfg_ptr + 2u));
        payload[2] = 0xAAu;
        payload[3] = 0xBBu;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);
    }

    /* 2) RCP_CONFIGURED state denies request-stream-cfg (W*) the
     * identical way. */
    {
        uint8_t payload[3] = {0x00u, 0x00u, 0xAAu};

        put_test_u16(payload, (uint16_t)map.svr_request_stream_cfg_ptr);
        payload[2] = 0xAAu;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);
    }

    /* 3) RCP_CONFIGURED state denies EP_ID_config (W+) the same way --
     * the underlying FUNCTIONAL_W_STAR-shaped state rule w_plus() reuses
     * denies it regardless of the lock bit. */
    {
        uint8_t payload[3];

        put_test_u16(payload, (uint16_t)map.svr_ep_bytebus_id_map_ptr);
        payload[2] = 0x09u;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);
    }

    /* 4) HW_UNCONFIGURED + svr_configuration_lock != 0 denies
     * EP_ID_config (W+) with LOCKED_MEM_ACCESS (the explicit lock always
     * wins, unconditionally -- rcp_lifecycle_field_write_error_w_plus()'s
     * own documented behavior) while STILL PERMITTING HW_config (W*),
     * since the lock is a W+-only concept -- proving the two access
     * types are checked independently, not conflated. */
    {
        uint8_t payload_ep_id[3];
        uint8_t payload_hw[3];

        map.svr_configuration_lock = 1u; /* locked */

        put_test_u16(payload_ep_id, (uint16_t)map.svr_ep_bytebus_id_map_ptr);
        payload_ep_id[2] = 0x09u;
        frame = rcp_acf_encode_abb(&hdr, payload_ep_id, sizeof(payload_ep_id));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);

        put_test_u16(payload_hw, (uint16_t)map.svr_hw_cfg_ptr);
        payload_hw[2] = 0xAAu;
        frame = rcp_acf_encode_abb(&hdr, payload_hw, sizeof(payload_hw));
        TEST_ASSERT_NOT_NULL(frame.data);
        /* HW_config is RCP_LIFECYCLE_FIELD_HW_GENERIC, not FUNCTIONAL_W_STAR
         * (issue #308) -- DISCOVERY_WRITER, not ROOT_WRITER, is the only
         * writer this kind ever accepts (see this dispatcher's own doc
         * comment, regmap.h). */
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_HW_UNCONFIGURED, DISCOVERY_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err); /* HW_config (W*) unaffected by the W+ lock */
        TEST_ASSERT_EQUAL_UINT8(0xAAu, hw_pin_map[0].hw_ep_nr);
        rcp_bytes_free(&frame);

        map.svr_configuration_lock = 0u; /* restore unlocked for the cases below */
    }

    /* 5) response-queue-config (QUEUE_CFG): CORRECTED 2026-08-13
     * (issue #338, REQ-LIFECYCLE-023) -- the whole table is now
     * RCP_LIFECYCLE_FIELD_HW_GENERIC-gated (Figure 17's own
     * HW_CONFIGURED-box "HW_CONFIG or QUEUE_CFG or EP_GEN_CFG ->
     * LOCKED_CONFIG_ACCESS" transition), so RCP_CONFIGURED denies both
     * its own W*-shaped sub-range (Max_AVTPDUsize, row-relative [2,4))
     * and its own W+-shaped sub-range (STREAM_UID, row-relative [0,2))
     * identically -- the per-field W-star/W-plus distinction no longer
     * matters once the table-wide state gate alone already denies the
     * write in this state, matching HW_config's own precedent. */
    {
        uint8_t payload_wstar[4];
        uint8_t payload_wplus[4];

        put_test_u16(payload_wstar, (uint16_t)(map.svr_response_stream_cfg_ptr + 2u));
        put_test_u16(&payload_wstar[2], 0x1234u);
        frame = rcp_acf_encode_abb(&hdr, payload_wstar, sizeof(payload_wstar));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);

        put_test_u16(payload_wplus, (uint16_t)map.svr_response_stream_cfg_ptr);
        put_test_u16(&payload_wplus[2], 0x5678u);
        frame = rcp_acf_encode_abb(&hdr, payload_wplus, sizeof(payload_wplus));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);
    }

    /* 6) response-queue-config (QUEUE_CFG): CORRECTED 2026-08-13
     * (issue #338, REQ-LIFECYCLE-023) -- a write spanning BOTH a W+
     * octet (STREAM_UID's own byte 1, row-relative offset 1) and a W*
     * octet (Max_AVTPDUsize's own byte 0, row-relative offset 2), sent
     * with DISCOVERY_WRITER (the only writer the table-wide HW_GENERIC
     * gate now accepts) during HW_UNCONFIGURED with the table's own
     * INDEPENDENT R/W+ lock bit set, is still denied -- proving the
     * table-wide HW_GENERIC gate passing does not itself bypass the
     * separate, orthogonal W+ lock-bit check this table's own
     * STREAM_UID/flush_on_count/Flush_time fields still carry. (Using
     * ROOT_WRITER here, as this subtest did before this fix, would now
     * be denied earlier and differently -- RCP_ERROR_UNAUTHORIZED_ACCESS
     * from the table-wide HW_GENERIC gate itself, since ROOT_WRITER
     * lacks via_discovery_stream -- which would prove the wrong thing:
     * DISCOVERY_WRITER is needed here specifically to isolate the W+
     * lock-bit check as this subtest's own real target.) */
    {
        uint8_t payload[4];

        map.svr_configuration_lock = 1u; /* locked -- denies W+ only */

        put_test_u16(payload, (uint16_t)(map.svr_response_stream_cfg_ptr + 1u));
        payload[2] = 0x00u;
        payload[3] = 0x00u;
        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_HW_UNCONFIGURED, DISCOVERY_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err); /* denied via the touched, locked W+ octet */

        map.svr_configuration_lock = 0u; /* restore */
        rcp_bytes_free(&frame);
    }

    /* 7pre) NEW 2026-08-13 (issue #338, REQ-LIFECYCLE-023): the actual
     * behavior CHANGE this fix makes, proven directly -- a write to
     * ep_generic_cfg/response_queue_cfg that a maximally-privileged
     * DISCOVERY_WRITER would have succeeded at once the server reaches
     * RCP_LIFECYCLE_HW_CONFIGURED (per FUNCTIONAL_W_STAR's own "writable
     * in HW_CONFIGURED with authorization" rule, this codebase's
     * behavior before this fix) is now REJECTED with LOCKED_MEM_ACCESS,
     * matching Figure 17's own explicit "HW_CONFIG or QUEUE_CFG or
     * EP_GEN_CFG -> LOCKED_CONFIG_ACCESS" transition (TC18.txt
     * L2485-2488) during HW_CONFIGURED specifically -- not merely
     * RCP_CONFIGURED, which subtests 5-7 already covered and which every
     * kind (W*, W+, HW_GENERIC alike) already agreed on. HW_CONFIGURED
     * is the one state where the old (FUNCTIONAL_W_STAR-based) and new
     * (HW_GENERIC-based) rules actually disagree, so it is the only
     * state that proves the fix, not merely re-confirms unchanged
     * behavior. */
    {
        uint8_t payload_queue[4];
        uint8_t payload_ep_gen[4];

        put_test_u16(payload_queue, (uint16_t)(map.svr_response_stream_cfg_ptr + 2u));
        put_test_u16(&payload_queue[2], 0x9999u);
        frame = rcp_acf_encode_abb(&hdr, payload_queue, sizeof(payload_queue));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_HW_CONFIGURED, DISCOVERY_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);

        put_test_u16(payload_ep_gen, (uint16_t)(map.svr_ep_generic_cfg_ptr + 4u));
        put_test_u16(&payload_ep_gen[2], 0x8888u);
        frame = rcp_acf_encode_abb(&hdr, payload_ep_gen, sizeof(payload_ep_gen));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_HW_CONFIGURED, DISCOVERY_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);
    }

    /* 7) RCP_CONFIGURED state denies ep_generic_cfg (EP_GEN_CFG) the
     * identical way. CORRECTED 2026-08-13 (issue #338, REQ-LIFECYCLE-023):
     * this table is RCP_LIFECYCLE_FIELD_HW_GENERIC-gated (Figure 17's
     * own HW_CONFIGURED-box "HW_CONFIG or QUEUE_CFG or EP_GEN_CFG ->
     * LOCKED_CONFIG_ACCESS" transition -- issue #311 batch 5's own
     * original claim, that §13.2's surrounding prose names no
     * table-specific override so the generic FUNCTIONAL_W_STAR rule
     * applied instead, only checked the prose next to Table 31 itself
     * and missed this diagram-level override). HW_GENERIC denies
     * RCP_CONFIGURED unconditionally too (permanently locked from
     * HW_CONFIGURED onward), so this subtest's own assertion is
     * unaffected by the fix -- only the underlying reason changed. The
     * write targets ep_description (fully R/W*, not the read-only
     * ep_type octet), proving this is a genuine authorization denial,
     * not merely the read-only no-op case #311 batch 4 already covers. */
    {
        uint8_t payload[6];

        put_test_u16(payload, (uint16_t)(map.svr_ep_generic_cfg_ptr + 4u));
        payload[2] = 0x11u;
        payload[3] = 0x22u;
        payload[4] = 0x33u;
        payload[5] = 0x44u;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER,
                                                   hw_pin_map, 1u, ep_id_map, 1u,
                                                   response_queue_cfg, 1u, request_stream_cfg, 1u, ep_generic_cfg, 1u,
                                                   NULL, NULL, 0u, 0u,
                                                   &err, &tn, 0u, NULL, NULL, 0u, 0u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, err);
        rcp_bytes_free(&frame);
    }
}

/* REQ-RMAP-040/041/052/054/061 CLOSED (read-dispatch half, issue #301
 * batch 4): rcp_regmap_ep0_decode_read_request()/_encode_read_response()
 * are the read-side counterpart to the write dispatcher, routing an
 * incoming ACF_ABB READ request's own address across the identical four
 * extents the write dispatcher already routes, reusing each table's own
 * already-proven render() function. REQ-SEQ-014 (closed, issue #334)
 * adds a seventh extent, sequencer_state -- unlike the other six, it has
 * no dedicated render() of its own (see rcp_regmap_ep0_encode_read_
 * response()'s own doc comment for why), so its own case below passes
 * the raw sequencer-state bytes directly rather than rendering a struct
 * array first. */
static void test_ep0_read_dispatcher_routes_all_seven_extents_and_unknown_addresses(void)
{
    rcp_acf_byte_message_info_t     req_hdr = {0};
    rcp_bytes_t                     req_frame, resp_frame;
    rcp_acf_byte_message_info_t     resp_hdr;
    const uint8_t                   *resp_payload;
    size_t                          resp_payload_len;
    rcp_regmap_general_t            map;
    rcp_regmap_hw_pin_map_entry_t   hw_pin_map[2] = {
        {1, 2, 0x03u},
        {4, 5, 0x06u},
    };
    rcp_regmap_ep_id_map_entry_t    ep_id_map[2] = {
        {10u, 20u, 1u},
        {30u, 40u, 1u},
    };
    rcp_regmap_response_queue_cfg_t response_queue_cfg[2];
    rcp_regmap_request_stream_cfg_t request_stream_cfg[2];
    rcp_regmap_ep_generic_cfg_t     ep_generic_cfg[2];
    uint16_t                        decoded_addr;
    uint8_t                         decoded_read_size;
    uint8_t                         decoded_tn;
    rcp_regmap_ep0_errc_t           rc;
    rcp_wire_error_t                err;
    uint8_t                         expected[RCP_REGMAP_GENERAL_LEN]; /* was a hardcoded 64
                                                    (0x0040) -- must track RCP_REGMAP_GENERAL_LEN,
                                                    now 0x0044 (issue #429) */

    rcp_regmap_response_queue_cfg_init(&response_queue_cfg[0]);
    response_queue_cfg[0].stream_uid = 111u;
    rcp_regmap_response_queue_cfg_init(&response_queue_cfg[1]);
    response_queue_cfg[1].stream_uid = 222u;

    rcp_regmap_request_stream_cfg_init(&request_stream_cfg[0]);
    request_stream_cfg[0].rx_stream_id = 1111u;
    rcp_regmap_request_stream_cfg_init(&request_stream_cfg[1]);
    request_stream_cfg[1].rx_stream_id = 2222u;

    rcp_regmap_ep_generic_cfg_init(&ep_generic_cfg[0]);
    ep_generic_cfg[0].ep_type = 0x03u;
    rcp_regmap_ep_generic_cfg_init(&ep_generic_cfg[1]);
    ep_generic_cfg[1].ep_type = 0x06u;

    rcp_regmap_general_init(&map);
    map.magic                        = 0x12345678u;
    map.svr_hw_cfg_ptr               = 0x0100u;
    map.svr_ep_bytebus_id_map_ptr    = 0x0200u;
    map.svr_response_stream_cfg_ptr  = 0x0300u;
    map.svr_request_stream_cfg_ptr   = 0x0400u;
    map.svr_ep_generic_cfg_ptr       = 0x0500u;
    map.svr_sequencer_state_ptr      = 0x0600u;

    /* Helper macro-free pattern: build a READ request frame at (addr,
     * read_size), decode it, route it, and decode the response back --
     * repeated per case below since each targets a different extent. */

    /* 1) Table 18 itself, addr=0, exact-length read -- matches
     * rcp_regmap_general_render()'s own image. */
    {
        uint8_t payload[2];

        put_test_u16(payload, 0u);
        req_hdr.byte_bus_id              = RCP_REGMAP_EP0_INDEX;
        req_hdr.op                       = RCP_ACF_OP_READ;
        req_hdr.read_size_or_segment_num = RCP_REGMAP_GENERAL_LEN;
        req_hdr.transaction_num          = 7;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL_UINT16(0u, decoded_addr);
        TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_GENERAL_LEN, decoded_read_size);
        TEST_ASSERT_EQUAL_UINT8(7, decoded_tn);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_NOT_NULL(resp_frame.data);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        TEST_ASSERT_EQUAL(1, resp_hdr.rsp);
        rcp_regmap_general_render(&map, expected);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, resp_payload, RCP_REGMAP_GENERAL_LEN);
        rcp_bytes_free(&resp_frame);
    }

    /* 2) Table 18, read_size extending past RCP_REGMAP_GENERAL_LEN --
     * real octets then zero-fill, not garbage/spillover. */
    {
        uint8_t payload[2];
        uint8_t oversized_read_size = (uint8_t)(RCP_REGMAP_GENERAL_LEN + 8u);

        put_test_u16(payload, 0u);
        req_hdr.read_size_or_segment_num = oversized_read_size;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_NOT_NULL(resp_frame.data);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        TEST_ASSERT_EQUAL_size_t(oversized_read_size, resp_payload_len);
        rcp_regmap_general_render(&map, expected);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, resp_payload, RCP_REGMAP_GENERAL_LEN);
        {
            size_t i;
            for (i = RCP_REGMAP_GENERAL_LEN; i < oversized_read_size; i++) {
                TEST_ASSERT_EQUAL_UINT8(0u, resp_payload[i]); /* zero-fill, not spillover */
            }
        }
        rcp_bytes_free(&resp_frame);
    }

    /* 3) HW_config's own extent -- matches
     * rcp_regmap_hw_pin_map_render()'s own image. */
    {
        uint8_t payload[2];
        uint8_t expected_hw[6];

        put_test_u16(payload, (uint16_t)map.svr_hw_cfg_ptr);
        req_hdr.read_size_or_segment_num = 6u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        rcp_regmap_hw_pin_map_render(hw_pin_map, 2u, expected_hw);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_hw, resp_payload, 6u);
        rcp_bytes_free(&resp_frame);
    }

    /* 4) HW_config's own routing boundary -- one octet past its own
     * extent must NOT be routed to it (falls through to EP_NOT_FOUND,
     * same as the write dispatcher's own equivalent boundary case).
     * Written in from the start, not discovered after an undetected
     * mutation -- see batch 2/3's own established lesson. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_hw_cfg_ptr + 6u));
        req_hdr.read_size_or_segment_num = 1u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        TEST_ASSERT_NULL(resp_frame.data);
    }

    /* 5) EP_ID_config's own extent -- matches
     * rcp_regmap_ep_id_map_render()'s own image. */
    {
        uint8_t payload[2];
        uint8_t expected_ep_id[8];

        put_test_u16(payload, (uint16_t)map.svr_ep_bytebus_id_map_ptr);
        req_hdr.read_size_or_segment_num = 8u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        rcp_regmap_ep_id_map_render(ep_id_map, 2u, expected_ep_id);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_ep_id, resp_payload, 8u);
        rcp_bytes_free(&resp_frame);
    }

    /* 6) EP_ID_config's own routing boundary. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 8u));
        req_hdr.read_size_or_segment_num = 1u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        TEST_ASSERT_NULL(resp_frame.data);
    }

    /* 7) response-queue-config's own extent -- matches
     * rcp_regmap_response_queue_cfg_render()'s own image. */
    {
        uint8_t payload[2];
        uint8_t expected_rq[20];

        put_test_u16(payload, (uint16_t)map.svr_response_stream_cfg_ptr);
        req_hdr.read_size_or_segment_num = 20u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        rcp_regmap_response_queue_cfg_render(response_queue_cfg, 2u, expected_rq);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_rq, resp_payload, 20u);
        rcp_bytes_free(&resp_frame);
    }

    /* 8) response-queue-config's own routing boundary. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_response_stream_cfg_ptr + 20u));
        req_hdr.read_size_or_segment_num = 1u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        TEST_ASSERT_NULL(resp_frame.data);
    }

    /* 9) request-stream-cfg's own extent -- matches
     * rcp_regmap_request_stream_cfg_render()'s own image. Row 1's own
     * rx_ack_stream_index (1 octet) is at svr_request_stream_cfg_ptr +
     * 40 (row 1 begins at +24; 0x0010 within that row is +16, so
     * +24+16 = +40). */
    {
        uint8_t payload[2];
        uint8_t expected_rs[48]; /* count=2 rows -- render() writes 24*count octets,
                                     even though only row 0's own 24 are compared below */

        put_test_u16(payload, (uint16_t)map.svr_request_stream_cfg_ptr);
        req_hdr.read_size_or_segment_num = 24u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        rcp_regmap_request_stream_cfg_render(request_stream_cfg, 2u, expected_rs, 0u, NULL);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_rs, resp_payload, 24u);
        rcp_bytes_free(&resp_frame);
    }

    /* 9b) request-stream-cfg's own routing boundary. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_request_stream_cfg_ptr + 48u));
        req_hdr.read_size_or_segment_num = 1u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        TEST_ASSERT_NULL(resp_frame.data);
    }

    /* 9c) ep_generic_cfg's own extent (issue #311 batch 5) -- matches
     * rcp_regmap_ep_generic_cfg_render()'s own image. */
    {
        uint8_t payload[2];
        uint8_t expected_egc[24]; /* count=2 rows -- 12*count octets */

        put_test_u16(payload, (uint16_t)map.svr_ep_generic_cfg_ptr);
        req_hdr.read_size_or_segment_num = 24u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        rcp_regmap_ep_generic_cfg_render(ep_generic_cfg, 2u, expected_egc);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_egc, resp_payload, 24u);
        rcp_bytes_free(&resp_frame);
    }

    /* 9d) ep_generic_cfg's own routing boundary. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_ep_generic_cfg_ptr + 24u));
        req_hdr.read_size_or_segment_num = 1u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        TEST_ASSERT_NULL(resp_frame.data);
    }

    /* 10) sequencer_state/sequencer_owner, REQ-SEQ-013/REQ-SEQ-014's own
     * eighth extent -- goes through rcp_regmap_sequencer_table_render()
     * (2 octets per sequencer: Seq_state then Request_stream_index),
     * unlike the now-corrected earlier fix's own unconverted-copy
     * approach (see that function's own doc comment, regmap.h). */
    {
        uint8_t payload[2];
        uint8_t sequencer_state[2] = {0x01u, 0x02u};
        uint8_t sequencer_owner[2] = {0x0Au, 0x14u};
        uint8_t expected_sequencer[4] = {0x01u, 0x0Au, 0x02u, 0x14u};

        put_test_u16(payload, map.svr_sequencer_state_ptr);
        req_hdr.read_size_or_segment_num = 4u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            sequencer_state, sequencer_owner, 2u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_NOT_NULL(resp_frame.data);
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                           rcp_acf_decode_abb(resp_frame.data, resp_frame.len, &resp_hdr,
                                               &resp_payload, &resp_payload_len));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_sequencer, resp_payload, 4u);
        rcp_bytes_free(&resp_frame);
    }

    /* 10b) sequencer_state/sequencer_owner passed as NULL (server has no
     * sequencer table, rcp_sequencer_table_unsupported()) -- must fall
     * through to the unknown-extent case, not dereference a null
     * pointer. */
    {
        uint8_t payload[2];

        put_test_u16(payload, map.svr_sequencer_state_ptr);
        req_hdr.read_size_or_segment_num = 1u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 3u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        TEST_ASSERT_NULL(resp_frame.data);
    }

    /* 11) An address matching none of the seven known extents. */
    {
        uint8_t payload[2];

        put_test_u16(payload, 0x0050u); /* past Table 18, before svr_hw_cfg_ptr */
        req_hdr.read_size_or_segment_num = 1u;
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);

        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        rcp_bytes_free(&req_frame);

        resp_frame = rcp_regmap_ep0_encode_read_response(decoded_addr, decoded_read_size,
                                                            decoded_tn, &map, hw_pin_map, 2u,
                                                            ep_id_map, 2u, response_queue_cfg, 2u, request_stream_cfg, 2u, ep_generic_cfg, 2u,
                                                            NULL, NULL, 0u,
                                                            &err, 0u, NULL, NULL);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        TEST_ASSERT_NULL(resp_frame.data);
    }

    /* 11) decode_read_request()'s own ACF-level failures and wrong-op
     * detection. */
    {
        uint8_t payload[1] = {0x00u}; /* too short for its own leading address */

        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);
        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD, rc);
        rcp_bytes_free(&req_frame);
    }
    {
        uint8_t payload[3] = {0x00u, 0x00u, 0xFFu};

        req_hdr.op = RCP_ACF_OP_WRITE; /* a write frame handed to the read decoder */
        req_frame = rcp_acf_encode_abb(&req_hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(req_frame.data);
        rc = rcp_regmap_ep0_decode_read_request(req_frame.data, req_frame.len,
                                                  &decoded_addr, &decoded_read_size, &decoded_tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_ERR_WRONG_OP, rc);
        rcp_bytes_free(&req_frame);
    }
}

static void test_ep_id_map_apply_reconfig_patches_addressed_octets_only(void)
{
    rcp_regmap_ep_id_map_entry_t rows[2] = {
        {1u, 2u, 3u},
        {4u, 5u, 6u},
    };
    uint8_t patch_row1_stream_index[1] = {0x09u};

    /* Patch only row 1's own request_stream_index octet (relative
     * address 4 = 4*1+0). */
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_ID_MAP_RECONFIG_OK,
                       rcp_regmap_ep_id_map_apply_reconfig(rows, 2, 4u,
                                                            patch_row1_stream_index, 1u));
    TEST_ASSERT_EQUAL_UINT16(1u, rows[0].ep_id); /* row 0 untouched */
    TEST_ASSERT_EQUAL_UINT8(3u, rows[0].request_stream_index);
    TEST_ASSERT_EQUAL_UINT16(4u, rows[1].ep_id);        /* row 1's other octets untouched */
    TEST_ASSERT_EQUAL_UINT16(5u, (uint16_t)rows[1].byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(0x09u, rows[1].request_stream_index); /* patched */
}

static void test_ep_id_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched(void)
{
    rcp_regmap_ep_id_map_entry_t rows[1] = {{7u, 8u, 9u}};
    uint8_t data[2] = {0x11u, 0x22u};

    /* count=1 -> block_len=4; address 3 + data_len 2 = 5 > 4. */
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_OUT_OF_RANGE,
                       rcp_regmap_ep_id_map_apply_reconfig(rows, 1, 3u, data, 2u));
    TEST_ASSERT_EQUAL_UINT16(7u, rows[0].ep_id); /* entirely unchanged */
    TEST_ASSERT_EQUAL_UINT16(8u, (uint16_t)rows[0].byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(9u, rows[0].request_stream_index);

    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_SHORT,
                       rcp_regmap_ep_id_map_apply_reconfig(rows, 1, 0u, data, 0u));
}

/* REQ-RMAP-041 CLOSED (row-stride half): TC18 §12.7.6 Table 19 lays
 * HW_config out as three consecutive 8-bit R/W* registers per IO pin
 * (hw_ep_nr, hw_ep_pin_nr, hw_pin_type), so IO pin N begins at relative
 * address 3*N, and R/W* means writable only while HW_unconfigured.
 * c-RCP's row carries the three 8-bit fields (correctly named
 * hw_pin_type as of REQ-RMAP-042) and now also serializes them at
 * exactly this 3-octet stride via rcp_regmap_hw_pin_map_render()
 * (regmap.h/regmap.c) -- asserted below via a direct byte-offset
 * check across two rows. The wire ACF_ABB request/response mechanism
 * itself is still not implemented (see regmap.h's own file-header
 * note); this test proves the STRUCTURAL layout only.
 *
 * The comparison below against ep_gpio.h's OWN, DIFFERENT,
 * deliberately-runtime-adjustable pin_property field (REQ-GPIO-013, its
 * own separate tracked concern, NOT this table's hw_pin_type) is
 * retained as-is: that field's own FUNCTIONAL_W classification was
 * never meant to match HW_config's own R/W* rule in the first place,
 * since it is a different register by design -- see this session's own
 * investigation note on issue #200 for the full architecture question
 * this raises, deliberately not resolved by this batch. */
static void test_hw_config_row_stride_now_modeled_gpio_access_class_still_diverges(void)
{
    rcp_regmap_hw_pin_map_entry_t entry;
    rcp_ep_gpio_functional_cfg_t  cfg;
    rcp_regmap_hw_pin_map_entry_t rows[2] = {{0x11u, 0x22u, 0x33u}, {0x44u, 0x55u, 0x66u}};
    uint8_t                       img[6];

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(entry.hw_ep_nr));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(entry.hw_ep_pin_nr));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(entry.hw_pin_type));

    /* IO_Pin 1 at relative address 0x0000-0x0002, IO_Pin 2 immediately
     * following at 0x0003-0x0005 -- the exact TC18-cited 3*N stride. */
    rcp_regmap_hw_pin_map_render(rows, 2, img);
    TEST_ASSERT_EQUAL_HEX8(0x11u, img[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, img[1]);
    TEST_ASSERT_EQUAL_HEX8(0x33u, img[2]);
    TEST_ASSERT_EQUAL_HEX8(0x44u, img[3]);
    TEST_ASSERT_EQUAL_HEX8(0x55u, img[4]);
    TEST_ASSERT_EQUAL_HEX8(0x66u, img[5]);

    /* Table 19's own access class, R/W* == HW_unconfigured-only, is what
     * RCP_LIFECYCLE_FIELD_HW_GENERIC expresses (DISCOVERY_WRITER, not
     * ROOT_WRITER, since the REQ-LIFECYCLE-026/035 fix restricts
     * HW_UNCONFIGURED writability to the discovery claimant -- no root
     * client can exist yet this early in bring-up): */
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_HW_GENERIC, DISCOVERY_WRITER));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_HW_GENERIC, ROOT_WRITER));

    /* ...but the actual pin-property setter uses FUNCTIONAL_W instead. */
    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_property(&cfg, 0, RCP_REGMAP_PIN_PROP_OUTPUT,
                                                   RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_ep_gpio_set_pin_property(&cfg, 0, RCP_REGMAP_PIN_PROP_OUTPUT,
                                                  RCP_LIFECYCLE_HW_CONFIGURED, ROOT_WRITER));
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_OUTPUT, cfg.pins[0].pin_property);
}

/* TC18 §12.7.6 Table 20 packs hw_pin_type as four sub-fields: pull mode
 * at bits 1:0 (00b float, 01b pull down, 10b pull up), output stage at
 * bits 3:2 (00b input, 01b open drain, 10b open source, 11b push pull),
 * drive strength at bits 5:4 (00b input, 01b low, 10b medium, 11b
 * high), reserved bit 6 reading 0, and Schmitt-trigger enable at bit 7.
 * Fixed (REQ-RMAP-042): rcp_regmap_hw_pin_map_entry_t.hw_pin_type now
 * has its own dedicated RCP_REGMAP_HW_PIN_* constants at exactly these
 * positions, primary-source verified against the TC18 v0.5.1_RC PDF
 * directly -- separate from ep_gpio.h's own, differently-shaped
 * RCP_REGMAP_PIN_PROP_* (REQ-GPIO-013's own concern, untouched here). */
static void test_hw_pin_type_matches_table_20(void)
{
    uint8_t combined;

    /* Pull field, bits 1:0. */
    TEST_ASSERT_EQUAL_HEX8(0x00, RCP_REGMAP_HW_PIN_PULL_FLOAT);
    TEST_ASSERT_EQUAL_HEX8(0x01, RCP_REGMAP_HW_PIN_PULL_DOWN);
    TEST_ASSERT_EQUAL_HEX8(0x02, RCP_REGMAP_HW_PIN_PULL_UP);
    TEST_ASSERT_EQUAL_HEX8(0x03, RCP_REGMAP_HW_PIN_PULL_MASK);

    /* Output stage, bits 3:2. */
    TEST_ASSERT_EQUAL_HEX8(0x00, RCP_REGMAP_HW_PIN_STAGE_INPUT);
    TEST_ASSERT_EQUAL_HEX8(0x04, RCP_REGMAP_HW_PIN_STAGE_OPEN_DRAIN);
    TEST_ASSERT_EQUAL_HEX8(0x08, RCP_REGMAP_HW_PIN_STAGE_OPEN_SOURCE);
    TEST_ASSERT_EQUAL_HEX8(0x0C, RCP_REGMAP_HW_PIN_STAGE_PUSH_PULL);
    TEST_ASSERT_EQUAL_HEX8(0x0C, RCP_REGMAP_HW_PIN_STAGE_MASK);

    /* Drive strength, bits 5:4. */
    TEST_ASSERT_EQUAL_HEX8(0x00, RCP_REGMAP_HW_PIN_DRIVE_INPUT);
    TEST_ASSERT_EQUAL_HEX8(0x10, RCP_REGMAP_HW_PIN_DRIVE_LOW);
    TEST_ASSERT_EQUAL_HEX8(0x20, RCP_REGMAP_HW_PIN_DRIVE_MEDIUM);
    TEST_ASSERT_EQUAL_HEX8(0x30, RCP_REGMAP_HW_PIN_DRIVE_HIGH);
    TEST_ASSERT_EQUAL_HEX8(0x30, RCP_REGMAP_HW_PIN_DRIVE_MASK);

    /* Schmitt-Trigger, bit 7; bit 6 is reserved and has no macro. */
    TEST_ASSERT_EQUAL_HEX8(0x80, RCP_REGMAP_HW_PIN_SCHMITT_TRIGGER);

    /* The four fields don't overlap and don't touch the reserved bit. */
    combined = (uint8_t)(RCP_REGMAP_HW_PIN_PULL_MASK | RCP_REGMAP_HW_PIN_STAGE_MASK |
                          RCP_REGMAP_HW_PIN_DRIVE_MASK | RCP_REGMAP_HW_PIN_SCHMITT_TRIGGER);
    TEST_ASSERT_EQUAL_HEX8(0xBFu, combined); /* every bit except reserved bit 6 */
    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(combined & 0x40u));

    /* A fully-specified value round-trips through a real entry. */
    {
        rcp_regmap_hw_pin_map_entry_t entry;
        entry.hw_ep_nr     = 0u;
        entry.hw_ep_pin_nr = 0u;
        entry.hw_pin_type  = (uint8_t)(RCP_REGMAP_HW_PIN_PULL_UP |
                                        RCP_REGMAP_HW_PIN_STAGE_PUSH_PULL |
                                        RCP_REGMAP_HW_PIN_DRIVE_HIGH |
                                        RCP_REGMAP_HW_PIN_SCHMITT_TRIGGER);
        TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_PULL_UP,
                               (uint8_t)(entry.hw_pin_type & RCP_REGMAP_HW_PIN_PULL_MASK));
        TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_STAGE_PUSH_PULL,
                               (uint8_t)(entry.hw_pin_type & RCP_REGMAP_HW_PIN_STAGE_MASK));
        TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_DRIVE_HIGH,
                               (uint8_t)(entry.hw_pin_type & RCP_REGMAP_HW_PIN_DRIVE_MASK));
        TEST_ASSERT_TRUE((entry.hw_pin_type & RCP_REGMAP_HW_PIN_SCHMITT_TRIGGER) != 0);
    }
}

/* TC18 §12.7.6: ALL OUTPUTS ARE ALWAYS ALSO AN INPUT -- output and
 * input are not mutually exclusive pin states for hw_pin_type, which is
 * what makes read-back of an output pin's actual level (short/stuck-
 * driver detection) possible. Fixed (REQ-RMAP-043): hw_pin_type's own
 * output-stage field (RCP_REGMAP_HW_PIN_STAGE_*) selects ONE of four
 * drive modes -- there is no separate, exclusive INPUT flag to toggle
 * away from at all, so a push-pull/open-drain/open-source pin is
 * structurally never "not an input" the way the old RCP_REGMAP_PIN_
 * PROP_OUTPUT/_INPUT pair could represent. (ep_gpio.h's own, separate
 * pin_property field and its rcp_ep_gpio_toggle_pin_direction() helper --
 * renamed 2026-08-11 from rcp_ep_gpio_apply_reconfig(), issue #256 Group G,
 * once REQ-GPIO-013's real, unrelated wire-mechanism bug was fixed and
 * this function was found to correspond to no TC18 register at all -- are
 * a separate, still-open structural concern of their own: this module's
 * local pin_property model duplicates, and diverges from, HW_config's own
 * hw_pin_type model. See this session's investigation note on issue
 * #200.) */
static void test_hw_pin_output_stage_has_no_exclusive_input_flag(void)
{
    uint8_t pin = (uint8_t)(RCP_REGMAP_HW_PIN_STAGE_PUSH_PULL | RCP_REGMAP_HW_PIN_PULL_UP);

    /* A push-pull-configured pin's own byte carries no bit anywhere that
     * means "not readable as an input" -- the four STAGE_* values are
     * the field's only four possible states, and none of them is a
     * separate "output, therefore not input" flag. */
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_STAGE_PUSH_PULL,
                           (uint8_t)(pin & RCP_REGMAP_HW_PIN_STAGE_MASK));
    TEST_ASSERT_NOT_EQUAL_UINT8(RCP_REGMAP_HW_PIN_STAGE_INPUT,
                                (uint8_t)(pin & RCP_REGMAP_HW_PIN_STAGE_MASK));
    /* ...yet nothing about that byte is exclusive with being read as an
     * input: unlike RCP_REGMAP_PIN_PROP_OUTPUT/_INPUT, there is no
     * second, independent bit this field's own three non-input values
     * ever clear or set to represent "readability" -- the pull
     * configuration (an orthogonal field) is untouched either way. */
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_PULL_UP,
                           (uint8_t)(pin & RCP_REGMAP_HW_PIN_PULL_MASK));
}

/* TC18 §12.7.6: ALL OUTPUTS ARE ALWAYS ALSO AN INPUT -- output and input
 * are not mutually exclusive pin states, which is what makes read-back of
 * an output pin's actual level (short/stuck-driver detection) possible.
 * Deviation: c-RCP models OUTPUT and INPUT as two independent flags and
 * rcp_ep_gpio_toggle_pin_direction() TOGGLES a pin from one to the other,
 * so a pin configured as an output ceases to be readable as an input, and
 * back again. */
static void test_output_pin_loses_its_input_capability(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];

    memset(pins, 0, sizeof(pins));
    pins[0] = (uint8_t)(RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP);

    rcp_ep_gpio_toggle_pin_direction(pins, 0x00000001u);
    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_OUTPUT));
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_INPUT,
                           (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_INPUT));

    rcp_ep_gpio_toggle_pin_direction(pins, 0x00000001u);
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_OUTPUT,
                           (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_OUTPUT));
    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_INPUT));
    /* the unrelated pull-up bit survives, so this is a genuine toggle */
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_PULL_UP,
                           (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_PULL_UP));
}

/* TC18 §12.7.6 Table 21 enumerates EP_Signal_Nr for EVERY endpoint type
 * (UART 0..3, LIN 0..2, PWM_OUT 0..1, PWM_IN 0, ADC 0, DAC 0, CAN 0 RXD /
 * 1 TXD, ISELED 0..1, MDIO 0..1 besides GPIO/SPI/I2C), and restarts the
 * numbering at 0 for each type. Fixed (REQ-RMAP-044): the enum now
 * covers all eleven endpoint types. Fixed (REQ-RMAP-045):
 * rcp_regmap_named_signal_ep_signal_nr() converts this enum's own flat
 * ordinal into TC18's per-type-relative wire value. */
static void test_named_signal_index_covers_every_endpoint_type(void)
{
    /* Coverage: the enum no longer stops at I2C_SDA. */
    TEST_ASSERT_EQUAL_STRING("UART_TX",     rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_UART_TX));
    TEST_ASSERT_EQUAL_STRING("LIN_TXD",     rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_LIN_TXD));
    TEST_ASSERT_EQUAL_STRING("PWM_OUT",     rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_PWM_OUT));
    TEST_ASSERT_EQUAL_STRING("PWM_OUTN",    rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_PWM_OUTN));
    TEST_ASSERT_EQUAL_STRING("PWM_IN",      rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_PWM_IN));
    TEST_ASSERT_EQUAL_STRING("ADC_IN",      rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_ADC_IN));
    TEST_ASSERT_EQUAL_STRING("DAC_OUT",     rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_DAC_OUT));
    TEST_ASSERT_EQUAL_STRING("CAN_RXD",     rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_CAN_RXD));
    TEST_ASSERT_EQUAL_STRING("CAN_TXD",     rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_CAN_TXD));
    TEST_ASSERT_EQUAL_STRING("ISELED_ISP_P", rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_ISELED_ISP_P));
    TEST_ASSERT_EQUAL_STRING("ISELED_ISP_N", rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_ISELED_ISP_N));
    TEST_ASSERT_EQUAL_STRING("MDIO_MDC",    rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_MDIO_MDC));
    TEST_ASSERT_EQUAL_STRING("MDIO_DATA",   rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_MDIO_DATA));
    TEST_ASSERT_EQUAL_STRING("unknown",
                             rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_COUNT));

    /* Per-type EP_Signal_Nr restarts at 0 for every type, even though
     * this enum's own flat ordinal keeps climbing -- exactly the
     * counter-example the old deviation pin cited (SPI_CLK/I2C_SCL
     * would both be EP_Signal_Nr 0, not their flat ordinals 32/41). */
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_SPI_CLK));
    TEST_ASSERT_EQUAL_UINT8(8u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_SPI_CS5));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_I2C_SCL));
    TEST_ASSERT_EQUAL_UINT8(1u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_I2C_SDA));
    TEST_ASSERT_EQUAL_UINT8(31u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_GPIO31));

    /* TC18's own counter-intuitive CAN ordering (RXD=0, TXD=1) is now
     * recorded, not lost. */
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_CAN_RXD));
    TEST_ASSERT_EQUAL_UINT8(1u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_CAN_TXD));

    /* Single-signal types are always EP_Signal_Nr 0. */
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_PWM_IN));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_ADC_IN));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_DAC_OUT));

    /* Out-of-range input (including a value below the enum's own first
     * member) safely returns 0, per this function's own documented
     * contract, not garbage. */
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_COUNT));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr((rcp_regmap_named_signal_t)-1));
}

/* ── §12.7.7 Table 22: request-stream configuration ────────────────────────── */

/* TC18 §12.7.7 Table 22 defines rx_secure_channel_index (0x000C, 8 bit,
 * default 0 = MACsec uncontrolled port), rx_ack_stream_index (0x0010,
 * 8 bit, 0 = send no acknowledge) and rx_resp_stream_index (0x0011,
 * 8 bit, POWER-ON DEFAULT 1 so a discovery request can be answered before
 * any configuration is written). Fixed (REQ-RMAP-047/048/049): all three
 * are now modelled fields, and rcp_regmap_request_stream_cfg_init() sets
 * rx_resp_stream_index to 1 as its sole deliberate exception to
 * zero-initializing everything else -- content modeling only, no ACF_ABB
 * wire wrapper reaches these fields yet (same deferred-wire-dispatch
 * scope as HW_config/EP_ID_config/response-queue; see regmap.h's own
 * file-header note). */
static void test_request_stream_cfg_now_has_channel_and_stream_indices(void)
{
    rcp_regmap_request_stream_cfg_t cfg;
    const uint8_t                  *raw = (const uint8_t *)&cfg;
    size_t                          i;
    bool                            any_other_nonzero = false;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_request_stream_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.configured);
    TEST_ASSERT_EQUAL_HEX64(0u, cfg.rx_stream_id);
    TEST_ASSERT_FALSE(cfg.rx_enforce_e2e);
    TEST_ASSERT_FALSE(cfg.rx_wd_enable);
    TEST_ASSERT_EQUAL_UINT32(0u, cfg.rx_wd_timeout_ms);
    TEST_ASSERT_EQUAL_UINT((size_t)0u, cfg.rx_stream_max_request_size);

    /* REQ-RMAP-047/048: default to 0 ("uncontrolled port" / "no ack"). */
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.rx_secure_channel_index);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.rx_ack_stream_index);
    /* REQ-RMAP-049: the one deliberate non-zero power-on default. */
    TEST_ASSERT_EQUAL_UINT8(1u, cfg.rx_resp_stream_index);

    for (i = 0; i < sizeof(cfg); i++) {
        const uint8_t *field = (const uint8_t *)&cfg.rx_resp_stream_index;
        if (&raw[i] >= field && &raw[i] < field + sizeof(cfg.rx_resp_stream_index)) continue;
        if (raw[i] != 0u) any_other_nonzero = true;
    }
    /* Every octet outside rx_resp_stream_index is still 0. */
    TEST_ASSERT_FALSE(any_other_nonzero);
}

/* TC18 §12.7.7 Table 22: rx_wd_timeout_intervall is a 16-bit R/W*
 * register at relative 0x000A expressed in CLOCK TICS. c-RCP still
 * stores rx_wd_timeout_ms as a 32-bit MILLISECOND value internally
 * (rcp_e2e_wd_evaluate()'s own unit) -- asserted here by 0x10000 ms
 * still being accepted and honoured exactly as a 65536 ms threshold in
 * that internal representation, a value the wire register itself cannot
 * hold. Fixed (REQ-RMAP-050): the conversion and 16-bit bounds check
 * TC18 requires at the register-write boundary now exist as an explicit
 * caller-supplied-tick-duration function pair (rcp_regmap_wd_timeout_ms_to_ticks()/
 * _ticks_to_ms()), matching the MTU-budget design precedent
 * (rcp_respqueue_max_avtpdu_size_within_mtu()) since TC18 names no fixed
 * clock-tick rate for this register. */
static void test_watchdog_timeout_internal_unit_is_still_milliseconds(void)
{
    rcp_regmap_request_stream_cfg_t cfg;
    rcp_e2e_wd_result_t             r;

    rcp_regmap_request_stream_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT((size_t)4u, sizeof(cfg.rx_wd_timeout_ms));

    cfg.rx_wd_enable      = true;
    cfg.rx_wd_timeout_ms  = 0x00010000u; /* 65536 -- past a 16-bit register */
    TEST_ASSERT_EQUAL_UINT32(0x00010000u, cfg.rx_wd_timeout_ms);

    r = rcp_e2e_wd_evaluate(cfg.rx_wd_enable, cfg.rx_wd_timeout_ms, false, false, 65535u);
    TEST_ASSERT_FALSE(r.overflowed);
    r = rcp_e2e_wd_evaluate(cfg.rx_wd_enable, cfg.rx_wd_timeout_ms, false, false, 65536u);
    TEST_ASSERT_TRUE(r.overflowed);
}

/* REQ-RMAP-050: ms -> ticks rounds down (never grants a longer enforced
 * watchdog period than requested) and rejects anything that would not
 * fit the register's 16-bit width. */
static void test_wd_timeout_ms_to_ticks_rounds_down_and_bounds_checks(void)
{
    uint16_t ticks = 0xFFFFu;

    TEST_ASSERT_TRUE(rcp_regmap_wd_timeout_ms_to_ticks(1000u, 10u, &ticks));
    TEST_ASSERT_EQUAL_UINT16(100u, ticks);

    /* 1005 ms / 10 ms-per-tick = 100.5 ticks -> rounds down to 100, not 101. */
    ticks = 0xFFFFu;
    TEST_ASSERT_TRUE(rcp_regmap_wd_timeout_ms_to_ticks(1005u, 10u, &ticks));
    TEST_ASSERT_EQUAL_UINT16(100u, ticks);

    /* Exactly the register's own ceiling still fits. */
    ticks = 0u;
    TEST_ASSERT_TRUE(rcp_regmap_wd_timeout_ms_to_ticks(65535u, 1u, &ticks));
    TEST_ASSERT_EQUAL_UINT16(65535u, ticks);

    /* One tick past the ceiling is rejected, not silently truncated. */
    ticks = 42u;
    TEST_ASSERT_FALSE(rcp_regmap_wd_timeout_ms_to_ticks(65536u, 1u, &ticks));
    TEST_ASSERT_EQUAL_UINT16(42u, ticks); /* untouched on rejection */

    /* A zero-length tick has no meaningful register value. */
    ticks = 42u;
    TEST_ASSERT_FALSE(rcp_regmap_wd_timeout_ms_to_ticks(1000u, 0u, &ticks));
    TEST_ASSERT_EQUAL_UINT16(42u, ticks);
}

/* REQ-RMAP-050: ticks -> ms is the plain inverse, used when populating
 * rx_wd_timeout_ms from a value read off the wire; rejects a zero-length
 * tick the same way. */
static void test_wd_timeout_ticks_to_ms_round_trips(void)
{
    uint32_t ms = 0xFFFFFFFFu;

    TEST_ASSERT_TRUE(rcp_regmap_wd_timeout_ticks_to_ms(100u, 10u, &ms));
    TEST_ASSERT_EQUAL_UINT32(1000u, ms);

    ms = 999u;
    TEST_ASSERT_FALSE(rcp_regmap_wd_timeout_ticks_to_ms(100u, 0u, &ms));
    TEST_ASSERT_EQUAL_UINT32(999u, ms); /* untouched on rejection */
}

/* TC18 §12.7.7 Table 22's own legend (TC18.txt:2929-2931): "This
 * configuration table can only be changed in the life-cycle states
 * HW_UNCONFIGURED and HW_CONFIGURED. In RCP_CONFIGURED ... this is
 * read-only. (As indicated by W*)" -- every R/W* field is writable in
 * BOTH HW_UNCONFIGURED and HW_CONFIGURED and read-only only once
 * RCP_CONFIGURED is reached. Fixed: rcp_lifecycle_field_writable() now
 * matches this for all three states -- HW_UNCONFIGURED unconditionally
 * (no association/root-client concept exists that early), HW_CONFIGURED
 * now additionally subject to REQ-LIFECYCLE-030/036's authorization gate
 * (ROOT_WRITER used below rather than PLAIN_WRITER, since this test's own
 * point is the per-state W-star-vs-read-only shape, not writer authorization --
 * that is covered directly by test_functional_w_hw_configured_requires_
 * authorization_or_discovery_stream() in test_lifecycle.c). (No caller
 * yet classifies the request-stream table as FUNCTIONAL_W_STAR, so this
 * primitive being correct doesn't by itself mean Table 22 is wired up
 * end to end -- that's a separate, still-open gap.) */
static void test_table22_w_star_writable_in_both_pre_rcp_configured_states(void)
{
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  PLAIN_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                   ROOT_WRITER));
}

/* ── §12.7.8 Table 23: EP_ID_config ────────────────────────────────────────── */

/* REQ-RMAP-052 (TC18 §12.7.8 Table 23): one EP_ID_config row is a
 * TRIPLE -- Request_Stream_Index (row offset 0x0000, 8 bit, R/W+),
 * EP_Nr (0x0001, 8 bit, R/W+), BBID (0x0002, 16 bit, R/W+) -- so the
 * same byte_bus_id may legally reach different endpoints on different
 * request streams. rcp_regmap_ep_id_map_entry_t now declares
 * request_stream_index, closing this field-content gap. A
 * Request_Stream_Index of 0 is TC18's own end-of-table sentinel;
 * rcp_regmap_ep_id_map_effective_count() (REQ-RMAP-054, see the test
 * below) is the dedicated consumer that stops scanning at it --
 * rcp_regmap_ep_id_map_is_ascending() itself deliberately does NOT,
 * since it is a generic ordering diagnostic, not a sentinel-aware
 * iterator, and a Request_Stream_Index of 0 is still a real (if
 * unusually low) value for its own composite-key comparison
 * (REQ-RMAP-056, pinned by the test after next). */
static void test_ep_id_row_now_has_request_stream_index(void)
{
    rcp_regmap_ep_id_map_entry_t rows[3];

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(rows[0].ep_id));
    /* byte_bus_id is now 2 bytes (uint16_t, REQ-RMAP-053/REQ-ACF-020 --
     * was 1 byte/uint8_t when this test was first written). */
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(rows[0].byte_bus_id));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(rows[0].request_stream_index));

    rows[0].ep_id = 5u; rows[0].byte_bus_id = 1u; rows[0].request_stream_index = 1u;
    rows[1].ep_id = 6u; rows[1].byte_bus_id = 2u; rows[1].request_stream_index = 1u;
    /* TC18's end-of-table sentinel row (Request_Stream_Index == 0). */
    rows[2].ep_id = 0u; rows[2].byte_bus_id = 0u; rows[2].request_stream_index = 0u;
    TEST_ASSERT_EQUAL_UINT8(0u, rows[2].request_stream_index);

    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(rows, 2u));
    /* is_ascending() is deliberately sentinel-UNAWARE (see this test's
     * own header comment): the third row still counts as a real
     * mapping to it, and under the composite-key rule (REQ-RMAP-056)
     * its request_stream_index (0) is a decrease from row 1's (1), so
     * this is FALSE on that basis alone, independent of either row's
     * byte_bus_id. Sentinel recognition belongs to
     * rcp_regmap_ep_id_map_effective_count() instead -- see the next
     * test. */
    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(rows, 3u));
}

/* REQ-RMAP-054: TC18 §12.7.8 (Table 23 / L2976) defines a
 * Request_Stream_Index of 0 as the table's own end-of-table sentinel.
 * rcp_regmap_ep_id_map_effective_count() is the dedicated, sentinel-
 * aware consumer that stops scanning at the first such row. */
static void test_ep_id_map_effective_count_stops_at_sentinel(void)
{
    rcp_regmap_ep_id_map_entry_t rows[4];

    rows[0].ep_id = 5u; rows[0].byte_bus_id = 1u; rows[0].request_stream_index = 1u;
    rows[1].ep_id = 6u; rows[1].byte_bus_id = 2u; rows[1].request_stream_index = 1u;
    /* Sentinel: everything from here on is not a real mapping. */
    rows[2].ep_id = 0u; rows[2].byte_bus_id = 0u; rows[2].request_stream_index = 0u;
    rows[3].ep_id = 9u; rows[3].byte_bus_id = 9u; rows[3].request_stream_index = 9u;

    TEST_ASSERT_EQUAL_UINT((size_t)2u, rcp_regmap_ep_id_map_effective_count(rows, 4u));

    /* No sentinel present in the scanned range -- the whole buffer is
     * real, so the count equals the capacity given. */
    TEST_ASSERT_EQUAL_UINT((size_t)2u, rcp_regmap_ep_id_map_effective_count(rows, 2u));

    /* Sentinel is the very first row -- an empty table. */
    TEST_ASSERT_EQUAL_UINT((size_t)0u, rcp_regmap_ep_id_map_effective_count(&rows[2], 2u));

    /* capacity == 0: entries may be NULL, vacuously 0 real rows. */
    TEST_ASSERT_EQUAL_UINT((size_t)0u, rcp_regmap_ep_id_map_effective_count(NULL, 0u));
}

/* REQ-RMAP-054's other half: TC18 §12.7.8 requires the table's
 * power-on default contents to permit access to EP0 before any
 * configuration is written. rcp_regmap_ep_id_map_row_init_default()
 * supplies exactly that default row. */
static void test_ep_id_map_power_on_default_permits_ep0(void)
{
    rcp_regmap_ep_id_map_entry_t row;

    /* Poison the row first so the test cannot pass by accident on an
     * already-zeroed stack. */
    row.ep_id = 0xFFu; row.byte_bus_id = 0xFFu; row.request_stream_index = 0xFFu;

    rcp_regmap_ep_id_map_row_init_default(&row);

    TEST_ASSERT_EQUAL_UINT16((uint16_t)RCP_REGMAP_EP0_INDEX, row.ep_id);
    TEST_ASSERT_EQUAL_UINT8(0u, row.byte_bus_id);
    /* Nonzero -- REQ-RMAP-054 also defines 0 as the end-of-table
     * sentinel, so the default row itself must not look like one, or
     * rcp_regmap_ep_id_map_effective_count() would report an empty
     * table on power-on rather than one row granting EP0 access. */
    TEST_ASSERT_NOT_EQUAL_UINT8(0u, row.request_stream_index);
    TEST_ASSERT_EQUAL_UINT((size_t)1u,
                            rcp_regmap_ep_id_map_effective_count(&row, 1u));
}

/* TC18 §12.7.8 requires the table to be ascending in the COMPOSITE key
 * (Request_Stream_Index, BBID), so a table that restarts its BBID run at
 * each new stream is correctly ordered. is_ascending() below now DOES
 * consider the composite key (REQ-RMAP-056, closed as of this test's own
 * follow-up batch): a per-stream-ascending table (BBIDs 1,2 then 1,2
 * again on the next, higher stream) is correctly reported ascending, and
 * a stream index that goes backwards is correctly reported non-ascending
 * regardless of that row's own BBID. */
static void test_ep_id_ordering_considers_request_stream_index(void)
{
    rcp_regmap_ep_id_map_entry_t rows[4];

    /* stream 1: BBID 1,2 -- stream 2: BBID 1,2. Ascending per TC18's own
     * composite-key rule: the stream index increases between rows[1] and
     * rows[2], so that transition is ascending regardless of BBID. */
    rows[0].ep_id = 10u; rows[0].byte_bus_id = 1u; rows[0].request_stream_index = 1u;
    rows[1].ep_id = 11u; rows[1].byte_bus_id = 2u; rows[1].request_stream_index = 1u;
    rows[2].ep_id = 20u; rows[2].byte_bus_id = 1u; rows[2].request_stream_index = 2u;
    rows[3].ep_id = 21u; rows[3].byte_bus_id = 2u; rows[3].request_stream_index = 2u;

    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(rows, 4u));

    /* A stream index that goes backwards is non-ascending even though
     * the BBID itself would look fine in isolation (1 < 2). */
    rows[2].request_stream_index = 0u;
    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(rows, 3u));
}

/* REQ-RMAP-057: TC18 §12.7.8 recommends, for safety reasons, that an
 * endpoint be mapped to at most one RC Client (request stream) at a
 * time. rcp_regmap_ep_id_map_has_single_client_per_ep() is the
 * dedicated diagnostic -- rcp_regmap_ep_id_map_is_ascending() itself
 * still reports a multi-client table as perfectly fine, since ordering
 * and multi-client-ness are orthogonal concerns (an out-of-order table
 * can be single-client, and an ascending table can be multi-client). */
static void test_ep_id_map_flags_multi_client_ep(void)
{
    rcp_regmap_ep_id_map_entry_t two_clients[2];
    rcp_regmap_ep_id_map_entry_t one_client_two_buses[2];

    /* Same ep_id (7), two DIFFERENT request streams -- this is the
     * actual hazard the recommendation is about: two distinct RC
     * Clients concurrently able to drive the same endpoint. */
    two_clients[0].ep_id = 7u; two_clients[0].byte_bus_id = 1u; two_clients[0].request_stream_index = 1u;
    two_clients[1].ep_id = 7u; two_clients[1].byte_bus_id = 1u; two_clients[1].request_stream_index = 2u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(two_clients, 2u));
    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_has_single_client_per_ep(two_clients, 2u));

    /* Same ep_id (7), same request stream, two different byte_bus_ids
     * -- still only ONE client addressing the endpoint (via two
     * registers), not the multi-client hazard, so this must NOT be
     * flagged. */
    one_client_two_buses[0].ep_id = 7u; one_client_two_buses[0].byte_bus_id = 1u; one_client_two_buses[0].request_stream_index = 1u;
    one_client_two_buses[1].ep_id = 7u; one_client_two_buses[1].byte_bus_id = 2u; one_client_two_buses[1].request_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_has_single_client_per_ep(one_client_two_buses, 2u));

    /* Vacuous cases. */
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_has_single_client_per_ep(NULL, 0u));
}

/* REQ-RMAP-058: TC18 §12.7.8 recommends that endpoints sharing a
 * byte_bus_id within one request stream share the same ep_type -- a
 * shared byte_bus_id is a deliberate multicast-within-a-stream
 * mechanism, and a request broadcast to endpoints of differing
 * ep_type would be decoded differently by each.
 * rcp_regmap_ep_id_map_shared_bus_homogeneous() is the dedicated
 * diagnostic; it takes a caller-supplied, index-parallel ep_types[]
 * array since the row itself carries no ep_type (TC18's own row
 * layout doesn't have one). */
static void test_ep_id_map_flags_heterogeneous_shared_bus(void)
{
    rcp_regmap_ep_id_map_entry_t heterogeneous[2];
    rcp_regmap_ep_id_map_entry_t homogeneous[2];
    rcp_regmap_ep_id_map_entry_t not_shared[2];
    uint8_t het_types[2]   = {1u, 2u}; /* different ep_type */
    uint8_t homo_types[2]  = {1u, 1u}; /* same ep_type */

    /* Two different endpoints sharing one (stream, BBID) -- a
     * multicast-within-a-stream group -- with DIFFERING ep_type. Note:
     * a genuinely shared byte_bus_id necessarily has an EQUAL, not
     * strictly increasing, BBID between these two rows, so this table
     * correctly fails is_ascending()'s own pre-existing, unrelated
     * strict-ordering rule (REQ-RMAP-020/021) -- that is -056's own
     * concern, orthogonal to the ep_type-homogeneity concern tested
     * here. */
    heterogeneous[0].ep_id = 8u; heterogeneous[0].byte_bus_id = 3u; heterogeneous[0].request_stream_index = 1u;
    heterogeneous[1].ep_id = 9u; heterogeneous[1].byte_bus_id = 3u; heterogeneous[1].request_stream_index = 1u;
    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(heterogeneous, 2u));
    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_shared_bus_homogeneous(heterogeneous, het_types, 2u));

    /* Same shared-bus shape, but matching ep_type -- a legitimate
     * multicast group, must NOT be flagged. */
    homogeneous[0].ep_id = 8u; homogeneous[0].byte_bus_id = 3u; homogeneous[0].request_stream_index = 1u;
    homogeneous[1].ep_id = 9u; homogeneous[1].byte_bus_id = 3u; homogeneous[1].request_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_shared_bus_homogeneous(homogeneous, homo_types, 2u));

    /* Two different endpoints, two DIFFERENT byte_bus_ids -- not a
     * shared-bus group at all, so differing ep_type here is
     * irrelevant and must NOT be flagged. */
    not_shared[0].ep_id = 8u; not_shared[0].byte_bus_id = 3u; not_shared[0].request_stream_index = 1u;
    not_shared[1].ep_id = 9u; not_shared[1].byte_bus_id = 4u; not_shared[1].request_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_shared_bus_homogeneous(not_shared, het_types, 2u));

    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_ep_id_map_entry_t), sizeof(heterogeneous[0]));

    /* Vacuous case. */
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_shared_bus_homogeneous(NULL, NULL, 0u));
}

/* REQ-WAKEUP-020: TC18 §13.7.2.1 fixes the WakeUp endpoint's own EP_Nr
 * to 1. rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id() is the dedicated
 * diagnostic, same shape as the two above: a caller-supplied,
 * index-parallel ep_types[] array (this table's own row carries no
 * ep_type field) checked against a caller-supplied
 * target_ep_type/required_ep_id pair. */
static void test_ep_id_map_flags_wrong_ep_id_for_a_fixed_endpoint_type(void)
{
    rcp_regmap_ep_id_map_entry_t correct[2];
    rcp_regmap_ep_id_map_entry_t wrong[2];
    rcp_regmap_ep_id_map_entry_t no_such_type[2];
    uint8_t types[2] = {1u, 2u}; /* row 0 is the fixed type (1), row 1 is not */

    /* row 0's ep_id correctly matches the required fixed value. */
    correct[0].ep_id = 1u; correct[0].byte_bus_id = 3u; correct[0].request_stream_index = 1u;
    correct[1].ep_id = 9u; correct[1].byte_bus_id = 4u; correct[1].request_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id(correct, types, 2u, 1u, 1u));

    /* Same shape, but row 0 (the fixed-type row) carries the wrong
     * ep_id -- must be flagged regardless of row 1's own ep_id, which
     * is a different, unconstrained ep_type. */
    wrong[0].ep_id = 5u; wrong[0].byte_bus_id = 3u; wrong[0].request_stream_index = 1u;
    wrong[1].ep_id = 9u; wrong[1].byte_bus_id = 4u; wrong[1].request_stream_index = 1u;
    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id(wrong, types, 2u, 1u, 1u));

    /* A table containing no row of the target ep_type at all is
     * vacuously fine -- there is nothing to violate the invariant. */
    no_such_type[0].ep_id = 5u; no_such_type[0].byte_bus_id = 3u; no_such_type[0].request_stream_index = 1u;
    no_such_type[1].ep_id = 9u; no_such_type[1].byte_bus_id = 4u; no_such_type[1].request_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id(no_such_type, types, 2u, 7u, 1u));

    /* Vacuous case. */
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id(NULL, NULL, 0u, 1u, 1u));
}

/* TC18 §12.7.8 Table 23 carries BBID in a 16-bit register holding an
 * 11-bit byte_bus_id, and the ACF byte_message_info header transports
 * byte_bus_id[10:8] in octet 2. Fixed (REQ-RMAP-053/REQ-ACF-020):
 * rcp_byte_bus_id_t is now uint16_t, so the full 0x000..0x7FF range is
 * representable, encodable, and decodable -- all 2048 endpoints per
 * stream the protocol defines are reachable, not just the first 256. */
static void test_byte_bus_id_is_now_eleven_bits_wide(void)
{
    rcp_acf_byte_message_info_t hdr;
    uint8_t                     raw[8];

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(rcp_byte_bus_id_t));
    TEST_ASSERT_EQUAL_HEX16(0x07FFu, (uint16_t)(rcp_byte_bus_id_t)0x07FFu);

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id = 0x7FFu;
    hdr.op          = (uint8_t)RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(raw, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL_HEX8(0x07, (uint8_t)(raw[2] & 0x07u)); /* [10:8] now real */
    TEST_ASSERT_EQUAL_HEX8(0xFF, raw[3]);

    /* Round-trips through decode too, rather than being refused. */
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(raw, &hdr));
    TEST_ASSERT_EQUAL_UINT16(0x7FFu, hdr.byte_bus_id);
}

/* REQ-RMAP-052/054 (row-stride half): TC18 §12.7.8 lays each row out as
 * four consecutive octets -- request_stream_index (0x0000), ep_id/EP_Nr
 * (0x0001), byte_bus_id/BBID+Ctrl (0x0002, 16 bit) -- so row N begins at
 * relative address 4*N. rcp_regmap_ep_id_map_render() (regmap.h/regmap.c)
 * now serializes a real table at exactly this stride, proven directly
 * via a byte-offset check across two rows -- including ep_id's own
 * honest truncation to the wire's real 8-bit EP_Nr width (this module's
 * own in-memory ep_id is 16 bit). The ACF_ABB wire request/response
 * wrapper itself is still not implemented -- see regmap.h's own
 * file-header note on the same genuine, unresolved addressing question
 * REQ-RMAP-040/041 (HW_config) already documents.
 *
 * CORRECTED 2026-08-14 (issue #421): this test used to lock in a flat,
 * unshifted byte_bus_id at 0x0002 -- TC18 Table 25/26 (this codebase
 * used to mis-cite this content as "Table 23", an unrelated table; see
 * regmap.h's own "EP_ID_config wire stride" file-header note) instead
 * packs BBID into bits[15:5] and a Ctrl sub-field into bits[4:0]. Both
 * rows below use a byte_bus_id >= 32 (so the >> 5 shift genuinely moves
 * real bits, not just zeros) and crc_required = false; the new test
 * immediately below this one separately exercises crc_required = true
 * and a byte_bus_id needing all 11 bits. */
static void test_ep_id_map_render_matches_table_25_26_byte_offsets(void)
{
    rcp_regmap_ep_id_map_entry_t rows[2];
    uint8_t                      img[8];

    rows[0].request_stream_index = 0x11u;
    rows[0].ep_id                = 0x1234u; /* truncates to 0x34 on render */
    rows[0].byte_bus_id          = 0x0056u; /* 86 -- exercises the >> 5 shift */
    rows[0].crc_required         = false;
    rows[1].request_stream_index = 0x22u;
    rows[1].ep_id                = 0x0078u;
    rows[1].byte_bus_id          = 0x009Au; /* 154 -- exercises the >> 5 shift */
    rows[1].crc_required         = false;

    rcp_regmap_ep_id_map_render(rows, 2, img);

    TEST_ASSERT_EQUAL_HEX8(0x11u, img[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, img[1]); /* truncated from 0x1234 */
    /* 0x0056 << 5 = 0x0AC0 (crc_required=false, Ctrl bits[3:0]=0). */
    TEST_ASSERT_EQUAL_HEX8(0x0Au, img[2]);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, img[3]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, img[4]);
    TEST_ASSERT_EQUAL_HEX8(0x78u, img[5]);
    /* 0x009A << 5 = 0x1340. */
    TEST_ASSERT_EQUAL_HEX8(0x13u, img[6]);
    TEST_ASSERT_EQUAL_HEX8(0x40u, img[7]);
}

/* REQ-RMAP-052/053 (issue #421): a byte_bus_id needing the full 11-bit
 * width (2000, i.e. >= 32 and > 0xFF -- genuinely exercises every shifted
 * bit, not just the low ones) round-trips through render() and
 * apply_reconfig() together with crc_required true and false, and
 * Channel_selection (Ctrl bits[3:0]) is proven to be silently dropped on
 * write, never landing in any field, matching this table's own
 * deliberate non-implementation of that sub-field (see
 * rcp_regmap_ep_id_map_entry_t.crc_required's own field comment,
 * regmap.h). */
static void test_ep_id_map_render_and_apply_reconfig_pack_bbid_ctrl_per_table_25_26(void)
{
    rcp_regmap_ep_id_map_entry_t rows[1];
    uint8_t                      img[4];
    uint8_t                      patch[2];
    rcp_regmap_ep_id_map_reconfig_errc_t rc;

    /* crc_required = true. */
    rows[0].request_stream_index = 1u;
    rows[0].ep_id                = 5u;
    rows[0].byte_bus_id          = 2000u; /* 0x7D0, fits the full 11-bit range */
    rows[0].crc_required         = true;

    rcp_regmap_ep_id_map_render(rows, 1, img);
    /* 2000 << 5 = 0xFA00; | 0x10 (crc_required) = 0xFA10. */
    TEST_ASSERT_EQUAL_HEX8(0xFAu, img[2]);
    TEST_ASSERT_EQUAL_HEX8(0x10u, img[3]);

    /* crc_required = false, same byte_bus_id -- only bit 4 changes. */
    rows[0].crc_required = false;
    rcp_regmap_ep_id_map_render(rows, 1, img);
    TEST_ASSERT_EQUAL_HEX8(0xFAu, img[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, img[3]);

    /* apply_reconfig() round-trip: patch the BBID/Ctrl word directly,
     * with Channel_selection (bits[3:0]) set to a nonzero, clearly
     * distinguishable pattern (0xF) that must NOT survive into any
     * field -- proving it is genuinely dropped, not accidentally stored
     * somewhere. */
    patch[0] = 0xFAu;
    patch[1] = 0x1Fu; /* crc_required=1 (bit4), Channel_selection=0xF (bits3:0) */
    rc = rcp_regmap_ep_id_map_apply_reconfig(rows, 1, 2u, patch, 2u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP_ID_MAP_RECONFIG_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(2000u, rows[0].byte_bus_id);
    TEST_ASSERT_TRUE(rows[0].crc_required);

    /* Re-render and confirm the round-trip is exact: Channel_selection's
     * own nonzero pattern above must NOT reappear anywhere -- bits[3:0]
     * are always rendered 0, never round-tripped. */
    rcp_regmap_ep_id_map_render(rows, 1, img);
    TEST_ASSERT_EQUAL_HEX8(0xFAu, img[2]);
    TEST_ASSERT_EQUAL_HEX8(0x10u, img[3]); /* NOT 0x1F -- Channel_selection dropped */
}

/* TC18 §12.7.8/§12.7.9 mark EP_ID_config rows and the Table 24
 * STREAM_UID/flush_on_count/Flush_time registers R/W+ -- explicitly
 * LOCKABLE by the configuring instance, independently of the lifecycle
 * state that governs W and W*. FIXED (REQ-RMAP-055): a real, tested W+
 * primitive now exists -- rcp_lifecycle_field_writable_w_plus()/
 * _write_error_w_plus() (lifecycle.h/lifecycle.c) -- deliberately a
 * SEPARATE function rather than a new rcp_lifecycle_field_kind_t value
 * threaded through rcp_lifecycle_field_writable()'s own ~90-call-site
 * signature (see lifecycle.h's own doc comment for the full blast-
 * radius rationale). STILL PARTIAL: no register-map write path in the
 * codebase classifies EP_ID_config or the Table 24 queue registers as
 * W+ yet -- the same deferred-ACF_ABB-wrapper gap already tracked for
 * both tables themselves (REQ-RMAP-052/054/061/065), so this
 * now-correct primitive is not yet wired to either table anywhere. */
static void test_w_plus_field_now_has_a_real_lockable_primitive(void)
{
    /* Same underlying state/writer rule as FUNCTIONAL_W_STAR when
     * unlocked: writable in HW_UNCONFIGURED, authorized-writer-only in
     * HW_CONFIGURED, permanently locked once RCP_CONFIGURED. */
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable_w_plus(
        RCP_LIFECYCLE_HW_UNCONFIGURED, PLAIN_WRITER, false));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable_w_plus(
        RCP_LIFECYCLE_HW_CONFIGURED, PLAIN_WRITER, false));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable_w_plus(
        RCP_LIFECYCLE_HW_CONFIGURED, ROOT_WRITER, false));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable_w_plus(
        RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER, false));

    /* The independent lock: unwritable in EVERY state, even
     * HW_UNCONFIGURED with a fully-authorized writer, once locked --
     * TC18's own "independently of the lifecycle state" wording. */
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable_w_plus(
        RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER, true));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable_w_plus(
        RCP_LIFECYCLE_HW_CONFIGURED, ROOT_WRITER, true));

    /* Error classification mirrors rcp_lifecycle_field_write_error()'s
     * own two-code split, with the lock folded in as its own
     * unconditional LOCKED_MEM_ACCESS case. */
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, rcp_lifecycle_field_write_error_w_plus(
        RCP_LIFECYCLE_HW_UNCONFIGURED, PLAIN_WRITER, false));
    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error_w_plus(
        RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER, true));
    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error_w_plus(
        RCP_LIFECYCLE_RCP_CONFIGURED, ROOT_WRITER, false));
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error_w_plus(
        RCP_LIFECYCLE_HW_CONFIGURED, PLAIN_WRITER, false));
}

/* ── §12.7.9 Table 24: response/acknowledge queues ─────────────────────────── */

/* REQ-RMAP-060 (TC18 §12.7.9 Table 24, relative address 0x0000, 16 bit,
 * R/W+): STREAM_UID supplies bits [63:48] of the stream_id a response
 * queue transmits on. rcp_regmap_response_queue_cfg_t.stream_uid now
 * carries that register directly, and
 * rcp_regmap_response_queue_stream_id() combines it with the interface's
 * own mac exactly as rcp_stream_id_make() would -- a queue now has an
 * identity rx_ack_stream_index/rx_resp_stream_index (Table 22) can point
 * at. */
static void test_response_queue_stream_id_is_configurable(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_stream_id_t                 sid;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.stream_uid);

    cfg.stream_uid = 0x1234u;
    sid            = rcp_regmap_response_queue_stream_id(&cfg, SERVER_MAC);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, sid.unique_id);
    TEST_ASSERT_EQUAL_MEMORY(SERVER_MAC, sid.mac, sizeof(SERVER_MAC));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(sid, rcp_stream_id_make(SERVER_MAC, 0x1234u)));
}

/* REQ-RMAP-059 (TC18 §12.7.9 Table 24, relative address 0x0004, 16 bit,
 * R/W*): a response/acknowledge queue's transmit-memory reservation, in
 * 32-bit words, and the real storage behind it. regmap.h's
 * rcp_regmap_response_queue_cfg_t now carries the queue_size register
 * itself; respqueue.h's rcp_respqueue_t is the actual queue this
 * codebase was missing entirely before this fix -- distinct from
 * server.h's ep_enable pre-load queue (which holds inbound requests, not
 * outbound responses). rcp_respqueue_init() takes the configured
 * capacity already converted from quadlets to octets (queue_size x 4),
 * matching rcp_acf_reg_write_len()'s own "caller supplies already-
 * classified units" convention. */
static void test_response_queue_size_register_and_storage_now_exist(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    const uint8_t                   frame[4] = {1, 2, 3, 4};

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.queue_size);

    cfg.queue_size = 2u; /* 2 quadlets = 8 octets */
    rcp_respqueue_init(&q, (size_t)cfg.queue_size * 4u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT(8u, rcp_respqueue_octets(&q));
    /* GitHub #446: a third push once the 8-octet reservation is exhausted
     * now evicts the lowest-sequence_num entry (TC18 §12.9.4/§12.9.5) to
     * make room, rather than being refused -- the reservation itself is
     * unchanged, still exactly 8 octets, still enforced. */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT(8u, rcp_respqueue_octets(&q));
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    rcp_respqueue_destroy(&q);
}

/* REQ-RMAP-061/062 (TC18 §12.7.9): "The RC Server shall not transmit an
 * AVTPDU longer than the configured Max_AVTPDUsize" and "fragmentation
 * as supported in ACF_ABB and ACF_GBB by the ms-bit will be performed"
 * for a message that would otherwise exceed it. respqueue.h's
 * rcp_respqueue_t now enforces the per-message ceiling directly
 * (REQ-RMAP-061's transmit-enforcement half), and
 * rcp_respqueue_max_fragment_payload() gives a caller the budget to feed
 * fragment.h's existing rcp_fragment_plan()/_plan_count() so an
 * oversized single message is split before it is ever pushed
 * (REQ-RMAP-062, closed). REQ-RMAP-061's own MTU-consistency-check half
 * is also now closed (see the dedicated test below,
 * rcp_respqueue_max_avtpdu_size_within_mtu()) -- REQ-RMAP-061 stays
 * `partial` overall only because Table 24 (this whole register block)
 * has no ACF_ABB wire request/response wrapper yet, the same genuine,
 * unresolved addressing question already documented for HW_config and
 * EP_ID_config, not a per-message queue concern this module could close
 * on its own. */
static void test_max_avtpdu_size_is_now_enforced_and_feeds_fragmentation(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    uint8_t                         ok_frame[16]  = {0};
    uint8_t                         over_frame[17] = {0};
    size_t                          budget;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.max_avtpdu_size);

    cfg.max_avtpdu_size = 4u; /* 4 quadlets = 16 octets */
    rcp_respqueue_init(&q, 0, (size_t)cfg.max_avtpdu_size * 4u);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, ok_frame, sizeof(ok_frame)));
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, over_frame, sizeof(over_frame)));
    rcp_respqueue_destroy(&q);

    /* A caller with a payload too large for one AVTPDU derives its
     * fragment.h budget from the same max_avtpdu_size, then hands it to
     * rcp_fragment_plan_count()/_plan() -- not modeled again here, since
     * fragment.h's own tests already pin that mechanism's own behavior;
     * this only confirms the budget itself is correctly derived. */
    budget = rcp_respqueue_max_fragment_payload((size_t)cfg.max_avtpdu_size * 4u,
                                                RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT((size_t)5u, budget); /* 16 - 8 - 3(pad) */
}

/* REQ-RMAP-061 CLOSED (MTU-consistency half): TC18 §12.7.9 (TC18.txt
 * L3010-3011) requires "the Max_AVTPDUsize shall always be configured
 * such that the final network frame does not exceed the maximum
 * transmit unit size of the network." rcp_respqueue_max_avtpdu_size_
 * within_mtu() (respqueue.h/respqueue.c) is the config-time check a
 * caller runs before ever calling rcp_respqueue_init(). TC18 defines no
 * fixed MTU value of its own -- mtu_budget_octets is the caller's own
 * already-adjusted ceiling, matching this module's established
 * "caller supplies already-classified units" convention throughout. */
static void test_max_avtpdu_size_within_mtu_check(void)
{
    /* Ordinary case: fits. */
    TEST_ASSERT_TRUE(rcp_respqueue_max_avtpdu_size_within_mtu(1400u, 1500u));
    /* Exactly at the boundary: fits (the check is <=, not <). */
    TEST_ASSERT_TRUE(rcp_respqueue_max_avtpdu_size_within_mtu(1500u, 1500u));
    /* One octet over: rejected. */
    TEST_ASSERT_FALSE(rcp_respqueue_max_avtpdu_size_within_mtu(1501u, 1500u));

    /* max_avtpdu_size_octets == 0 means "unbounded" (matching
     * rcp_respqueue_init()'s own convention) -- an unbounded ceiling can
     * never be MTU-safe against a finite budget. */
    TEST_ASSERT_FALSE(rcp_respqueue_max_avtpdu_size_within_mtu(0u, 1500u));
    /* Both unbounded is the one degenerate case this function treats as
     * vacuously true -- "no ceiling configured" is consistent with "no
     * MTU budget configured either", not a real conformance answer. */
    TEST_ASSERT_TRUE(rcp_respqueue_max_avtpdu_size_within_mtu(0u, 0u));
}

/* REQ-RMAP-063 (TC18 §12.7.9 Table 24, relative address 0x0006, 16 bit,
 * R/W+, default 1, legal range 1..queue_size): "Once a queue is filled
 * with an amount of quadlets that is equal or larger than given by
 * flush_on_count, the transmission of one or multiple AVTPDUs shall be
 * initiated... only as much as fitting to the MAX_AVTPDUsize ACF_types
 * will be included in a generated AVTPDU... packed in a fitting number
 * of AVTPDUs." respqueue.h's rcp_respqueue_should_flush() implements
 * the trigger; rcp_respqueue_plan_batch() implements the packing --
 * both operate on the queue's own state alone, needing no clock. */
static void test_flush_on_count_trigger_and_avtpdu_packing(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    uint8_t                         frame[5] = {0};
    rcp_bytes_t                     out;
    size_t                          i;

    rcp_regmap_response_queue_cfg_init(&cfg);
    cfg.flush_on_count = 2u; /* 2 quadlets = 8 octets */
    rcp_respqueue_init(&q, 0, 0);

    TEST_ASSERT_FALSE(rcp_respqueue_should_flush(&q, (size_t)cfg.flush_on_count * 4u));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush(&q, (size_t)cfg.flush_on_count * 4u));

    /* The push that crosses the threshold (5 + 5 = 10 >= 8 octets)
     * itself triggers the flush -- "including the one which was
     * exceeding the Flush_on_Count value." */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush(&q, (size_t)cfg.flush_on_count * 4u));

    /* Packing: a third, small entry plus a Max_AVTPDUsize of 12 octets
     * means the first AVTPDU packs 2 entries (10 octets), the second
     * AVTPDU packs the last one alone. */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, rcp_respqueue_plan_batch(&q, 12u));
    for (i = 0; i < 2u; i++) {
        TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
        rcp_bytes_free(&out);
    }
    TEST_ASSERT_EQUAL_UINT((size_t)1u, rcp_respqueue_plan_batch(&q, 12u));
    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    rcp_bytes_free(&out);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_plan_batch(&q, 12u));

    rcp_respqueue_destroy(&q);
}

/* REQ-RMAP-064/065 (TC18 §12.7.9): Flush_time (0x0008, microseconds)
 * forces transmission once that interval elapses since the queue's last
 * transmission, independently of flush_on_count, and on expiry an EMPTY
 * queue still emits a heartbeat AVTPDU so a client can see the server is
 * alive. respqueue.h's rcp_respqueue_should_flush_by_time() implements
 * the trigger (REQ-RMAP-064, closed) and deliberately fires the same way
 * whether the queue is empty or not; combined with
 * rcp_respqueue_plan_batch() reporting 0 for an empty queue and
 * avtp.h's rcp_avtp_encode_ntscf() already accepting payload_len == 0,
 * the empty heartbeat AVTPDU is fully constructible (REQ-RMAP-065, the
 * primitive-composition half). What remains open for REQ-RMAP-065 is
 * exactly what REQ-SRV-017 (server.h) already states as this library's
 * own scope boundary: actually SCHEDULING this composition against a
 * real clock and driving a transport with it is an integrator concern,
 * not this protocol library's -- so REQ-RMAP-065 stays catalogued
 * "partial", not "implemented". */
static void test_flush_time_trigger_and_empty_heartbeat_are_composable(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    rcp_avtp_ntscf_header_t         hdr;
    rcp_bytes_t                     heartbeat;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(0u, cfg.flush_time_us);

    cfg.flush_time_us = 1000u; /* 1000 us */
    TEST_ASSERT_EQUAL_UINT32(1000u, cfg.flush_time_us);

    /* Below the interval: no trigger. At/past it: triggers, independently
     * of flush_on_count (no push()/should_flush() call anywhere here). */
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush_by_time(999u, (uint64_t)cfg.flush_time_us));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(1000u, (uint64_t)cfg.flush_time_us));

    /* An EMPTY queue still trips the trigger -- REQ-RMAP-065's own
     * precondition for the heartbeat case. */
    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(1000u, (uint64_t)cfg.flush_time_us));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_plan_batch(&q, 0u));

    /* Once triggered on an empty queue, the caller-composed heartbeat is
     * exactly a zero-payload NTSCF AVTPDU -- the *only* header format an
     * RC Server itself ever sends (avtp.h) -- proving the composition
     * this codebase's tree makes available end to end. */
    hdr.sv = 1;
    hdr.version = 0;
    hdr.sequence_num = 0;
    hdr.stream_id = rcp_stream_id_make(SERVER_MAC, 0x0001);
    heartbeat = rcp_avtp_encode_ntscf(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(heartbeat.data);
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_NTSCF_HEADER_LEN, heartbeat.len);
    rcp_bytes_free(&heartbeat);

    rcp_respqueue_destroy(&q);

    /* Liveness on the RECEIVE side (rcp_deadline_config_t) is the mirror
     * image of this obligation and is unaffected by it -- still real,
     * still separate machinery. */
    TEST_ASSERT_EQUAL_UINT64(50u, rcp_deadline_default_config().default_deadline_ms);
}

/* REQ-RMAP-062 (TC18 §12.7.9): a single ACF message that would push an
 * AVTPDU past the queue's Max_AVTPDUsize must be fragmented via the ms
 * bit, i.e. rcp_fragment_plan() must be driven from
 * rcp_regmap_response_queue_cfg_t.max_avtpdu_size. The register is
 * expressed in QUADLETS, not octets -- feeding it into
 * rcp_fragment_plan_count() raw plans against a quarter of the real
 * budget (13 fragments for a 100-octet payload, rather than the correct
 * answer), exactly the mistake this test used to demonstrate as an open
 * deviation. respqueue.h's rcp_respqueue_max_fragment_payload() now
 * closes it: a caller converts quadlets to octets itself
 * (max_avtpdu_size x 4, this codebase's own "caller supplies already-
 * classified units" convention) and gets back a budget that already
 * reserves the fixed ACF header and worst-case pad, ready to hand
 * straight to fragment.h. */
static void test_transmit_fragmentation_now_uses_the_correct_octet_budget(void)
{
    rcp_regmap_response_queue_cfg_t rsp;
    size_t                          budget;

    rcp_regmap_response_queue_cfg_init(&rsp);
    rsp.max_avtpdu_size = 8u; /* 8 quadlets == 32 octets */

    budget = rcp_respqueue_max_fragment_payload((size_t)rsp.max_avtpdu_size * 4u,
                                                RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT((size_t)21u, budget); /* 32 - 8(header) - 3(pad) */
    TEST_ASSERT_EQUAL_UINT((size_t)5u, rcp_fragment_plan_count(100u, budget));

    /* The same register value fed to fragment.h RAW (quadlets, no
     * header/pad accounted for) still plans the wrong, needlessly
     * fragmented result -- the mistake this test originally flagged,
     * kept here as the contrast that shows why the conversion matters. */
    TEST_ASSERT_EQUAL_UINT((size_t)13u, rcp_fragment_plan_count(100u, rsp.max_avtpdu_size));
}

/* ── §13.7.1.2 Table 33/36: the RC Server's own functional-config content ── */

/* TC18 §13.7.1.2's own table (Table 33 in the RC1 baseline, renumbered
 * Table 36 in the current RC5 baseline -- confirmed the same table via
 * direct PDF page-image reads of both) lists svr_ep_len/reserved/
 * svr_ep_enable&clr/svr_ep_options (the generic EP_FUNC common-header
 * shape) AND svr_root_client_index/svr_lifecycle_state/
 * svr_discovery_timeout/svr_ep_status (RC-Server-specific), with a real,
 * confirmed-on-both-revisions address collision between the two groups
 * -- see regmap.h's own file-header note for the full investigation.
 * REQ-RMAP-066/067: rcp_regmap_svr_ep_cfg_t now models the two fields
 * free of both the collision and the "does the RC Server even have an
 * EP_FUNC block" self-contradiction: svr_discovery_timeout and
 * svr_ep_status. Deliberately still NOT modeled: the four common-header
 * fields (§13.7.1.1's own prose says the RC Server "is not included in
 * the EP_FUNC_config register maps" -- directly contradicting Table
 * 33/36 listing them anyway) and svr_root_client_index/svr_lifecycle_state
 * (already correctly modeled at their own uncontested Table 18
 * addresses -- REQ-RMAP-038/023 -- not duplicated here under this
 * table's own disputed local addressing).
 *
 * Separately, rcp_discovery_claim_t's own Discovery_TimeOut remains a
 * caller-supplied constructor argument in MILLISECONDS (RCP_DISCOVERY_
 * DEFAULT_TIMEOUT_MS), not read from rcp_regmap_svr_ep_cfg_t's own
 * microsecond register -- the two are not yet wired together (same
 * deferred-wire-dispatch scope as the rest of this table). */
static void test_svr_ep_cfg_now_models_discovery_timeout_and_status(void)
{
    rcp_regmap_svr_ep_cfg_t cfg;
    rcp_discovery_claim_t   claim;

    TEST_ASSERT_TRUE(rcp_regmap_is_ep0(RCP_REGMAP_EP0_INDEX));

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_svr_ep_cfg_init(&cfg);

    /* REQ-RMAP-066: TC18's own stated power-on default, 20000 us = 20 ms. */
    TEST_ASSERT_EQUAL_UINT16(20000u, cfg.svr_discovery_timeout);
    /* REQ-RMAP-067: svr_ep_status, no TC18 bit-level breakdown given. */
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.svr_ep_status);

    /* Table 33/36's common-header fields (svr_enable etc.) are NOT
     * modeled for the RC Server -- the generic prefix's own init still
     * zeroes ep_enable and there is no RC-Server special case to put it
     * back, matching this deliberate scope-exclusion. */
    {
        rcp_regmap_ep_functional_cfg_t generic;
        memset(&generic, 0xAA, sizeof(generic));
        rcp_regmap_ep_functional_cfg_init(&generic);
        TEST_ASSERT_FALSE(generic.ep_enable);
    }

    /* Discovery_TimeOut: still a constructor argument in ms, not read
     * from the register modeled above -- the two are not yet wired
     * together. */
    rcp_discovery_claim_init(&claim, RCP_DISCOVERY_DEFAULT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT32(20u, claim.timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(20u, RCP_DISCOVERY_DEFAULT_TIMEOUT_MS);
}

/* TC18 gives TWO distinct concrete write-denial examples, both verified
 * directly against the primary-source PDF (initially only §13.7.1.2's
 * prose was checked, which is necessary but not sufficient -- Figure
 * 16's own diagram carries a second, more specific rule the prose alone
 * does not surface):
 *
 *   - Figure 16's HW_CONFIGURED-box transition: "Request on discovery
 *     stream or known stream/bb_id for configuration to HW_CONFIG or
 *     QUEUE_CFG or EP_GEN_CFG -> send error response LOCKED_CONFIG_ACCESS"
 *     -- an otherwise-authorized request, denied purely because the
 *     target block is state-locked. LOCKED_CONFIG_ACCESS does not
 *     literally match any of the seventeen numbered wire error codes'
 *     own strings (§12.9.6's table lists both UNAUTHORIZED_ACCESS and
 *     LOCKED_MEM_ACCESS by name with no description column for either),
 *     but is unambiguously the same concept as RCP_ERROR_LOCKED_MEM_ACCESS
 *     (4), the only numbered code with a semantically matching name --
 *     the same prose-vs-table naming variance already documented for
 *     POCI_FAILURE.
 *   - §13.7.1.2's own prose: "Writing to a write prohibited register
 *     (e.g. lock bit for map set) creates a response with err=1 and an
 *     error code UNAUTHORIZED_ACCESS" -- a writer/frame-specific denial
 *     on top of an otherwise-permitting state.
 *
 * REQ-WIREERR-004 distinguishes these two. Previously (REQ-LIFECYCLE-024,
 * now closed): rcp_lifecycle_field_writable() reported writability as
 * one plain bool with no wire-error-code mapping at all, and this
 * test's own prior forms (across two revisions) each mislabeled part of
 * this: HW_GENERIC-past-HW_UNCONFIGURED was first called TC18's distinct
 * "read only" case, then (after that was corrected) called
 * UNAUTHORIZED_ACCESS uniformly with every other denial -- both wrong,
 * per Figure 16's own LOCKED_CONFIG_ACCESS transition found on a closer
 * re-reading of the primary source. */
static void test_field_write_error_distinguishes_state_from_writer_denial(void)
{
    /* HW_GENERIC past HW_UNCONFIGURED: state-locked configuration
     * (§12.3.1.2's own text: "access to the HW_config...are locked";
     * Figure 16's own HW_CONFIG/EP_GEN_CFG/QUEUE_CFG grouping,
     * REQ-LIFECYCLE-023) -- LOCKED_MEM_ACCESS, even for the
     * fully-authorized ROOT_WRITER, since HW_GENERIC's own writability
     * rule has no authorization concept at all. */
    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, ROOT_WRITER));

    /* FUNCTIONAL_W in RCP_CONFIGURED from an unauthorized writer:
     * write-prohibited for a different reason (writer, not state) --
     * UNAUTHORIZED_ACCESS, matching §13.7.1.2's own separate example. */
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, PLAIN_WRITER));

    /* An actually-writable case: RCP_ERROR_NONE. HW_GENERIC's HW_UNCONFIGURED
     * writability requires the discovery claimant specifically
     * (REQ-LIFECYCLE-026/035) -- DISCOVERY_WRITER, not ROOT_WRITER (no
     * root client can exist yet this early in bring-up). */
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, DISCOVERY_WRITER));

    TEST_ASSERT_EQUAL_INT(3, (int)RCP_ERROR_UNAUTHORIZED_ACCESS);
    TEST_ASSERT_EQUAL_INT(4, (int)RCP_ERROR_LOCKED_MEM_ACCESS);
}

/* REQ-RMAP-069 (TC18 §13.7.1.2, corrected in spec revision 0.5.1_RC5): the
 * effective number of register-write DATA octets is
 * (acf_msg_length - 3) x 4 - pad - 2 -- distinct from, and not
 * interchangeable with, the raw ACF payload_len rcp_acf_decode_abb()
 * reports (which spans the whole payload, address/CRC region included).
 * rcp_acf_reg_write_len() now provides this formula directly; asserted
 * here against a real 5-octet ACF_ABB encoding, confirming pad and
 * acf_msg_length are carried correctly and the helper's own answer
 * matches the spec formula exactly (by construction) while remaining
 * genuinely different from the decoder's own payload_len -- the two
 * numbers answer different questions, and a caller must not conflate
 * them. FIXED 2026-08-11: under the OLD (pre-RC5) formula this case
 * returned 1 (pad(3) <= total_octets(4)); the corrected formula returns
 * 0, since pad(3) plus the address's 2 octets (5) now exceeds
 * total_octets(4). */
static void test_effective_register_write_length_helper_matches_the_formula(void)
{
    rcp_acf_byte_message_info_t hdr;
    rcp_bytes_t                 msg;
    const uint8_t              *payload;
    size_t                      payload_len;
    const uint8_t               data[5] = {1, 2, 3, 4, 5};

    TEST_ASSERT_EQUAL_UINT8(0u, rcp_acf_pad_len(8u));
    TEST_ASSERT_EQUAL_UINT8(3u, rcp_acf_pad_len(9u));
    TEST_ASSERT_EQUAL_UINT8(2u, rcp_acf_pad_len(10u));
    TEST_ASSERT_EQUAL_UINT8(1u, rcp_acf_pad_len(11u));

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id = 0u;
    hdr.op          = (uint8_t)RCP_ACF_OP_WRITE;
    msg = rcp_acf_encode_abb(&hdr, data, sizeof(data));
    TEST_ASSERT_NOT_NULL(msg.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(msg.data, msg.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL_UINT((size_t)5u, payload_len);
    TEST_ASSERT_EQUAL_UINT8(3u, hdr.pad);
    TEST_ASSERT_EQUAL_UINT16(4u, hdr.acf_msg_length);

    TEST_ASSERT_EQUAL_UINT((size_t)0u,
                           rcp_acf_reg_write_len(hdr.acf_msg_length, hdr.pad));
    TEST_ASSERT_NOT_EQUAL((int)payload_len,
                          (int)rcp_acf_reg_write_len(hdr.acf_msg_length, hdr.pad));
    rcp_bytes_free(&msg);
}

/* REQ-RMAP-068 (TC18 13.7.1.2): direct unit test of the shared
 * combine primitive, independent of any dispatcher plumbing -- SET is
 * a pure passthrough (current ignored), OR/AND/XOR compute the named
 * bitwise op byte-wise, and the spec's own worked example (OR with an
 * all-zero request leaves current unchanged) holds. */
static void test_ep0_combine_write_op_implements_set_or_and_xor(void)
{
    uint8_t current[4] = {0xF0u, 0x0Fu, 0xAAu, 0x55u};
    uint8_t request[4] = {0x0Fu, 0xF0u, 0x55u, 0xAAu};
    uint8_t out[4];

    rcp_regmap_ep0_combine_write_op(RCP_REGMAP_EP0_WRITE_OP_SET, current, request, out, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(request, out, 4);

    rcp_regmap_ep0_combine_write_op(RCP_REGMAP_EP0_WRITE_OP_OR, current, request, out, 4u);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[3]);

    rcp_regmap_ep0_combine_write_op(RCP_REGMAP_EP0_WRITE_OP_AND, current, request, out, 4u);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[3]);

    rcp_regmap_ep0_combine_write_op(RCP_REGMAP_EP0_WRITE_OP_XOR, current, request, out, 4u);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out[3]);

    /* TC18 13.7.1.2's own worked example: "A request with
     * byte_msg_payload 0x0000 0000 and evt = 0x001 (OR) results in
     * 'no effect'." */
    {
        uint8_t zero[4] = {0};

        rcp_regmap_ep0_combine_write_op(RCP_REGMAP_EP0_WRITE_OP_OR, current, zero, out, 4u);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(current, out, 4);
    }
}

/* REQ-RMAP-068: evt[2:0] in {4..7} has no defined SET/OR/AND/XOR
 * meaning for an EP0 register-map write -- rejected with
 * RCP_ERROR_UNSUPPORTED_CMD before any address routing is even
 * attempted, table left entirely unchanged. */
static void test_ep0_dispatcher_rejects_reserved_write_op_before_any_routing(void)
{
    rcp_acf_byte_message_info_t   hdr = {0};
    rcp_bytes_t                   frame;
    rcp_regmap_general_t          map;
    rcp_regmap_hw_pin_map_entry_t hw_pin_map[1] = {{1u, 2u, 0x03u}};
    rcp_lifecycle_state_t         state         = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t    writer        = DISCOVERY_WRITER;
    rcp_wire_error_t              err;
    uint8_t                       tn = 0;
    rcp_regmap_ep0_errc_t         rc;
    uint8_t                       payload[3];

    rcp_regmap_general_init(&map);
    map.svr_hw_cfg_ptr = 0x0100u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0x04u; /* reserved for this context -- {4..7} */
    hdr.transaction_num = 5;

    put_test_u16(payload, (uint16_t)(map.svr_hw_cfg_ptr + 2u)); /* row 0's own hw_pin_type */
    payload[2] = 0xFFu;

    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);
    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer,
                                              hw_pin_map, 1u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_UNSUPPORTED_CMD, err);
    TEST_ASSERT_EQUAL_UINT8(0x03u, hw_pin_map[0].hw_pin_type); /* unchanged */
    rcp_bytes_free(&frame);
}

/* REQ-RMAP-068: evt[2:0]=001b (OR) applied end-to-end through the EP0
 * write dispatcher against HW_config -- proves the render+combine step
 * actually reads the table's own CURRENT content, not just the raw
 * request bytes (0x03 | 0x04 = 0x07, not 0x04). */
static void test_ep0_dispatcher_applies_or_write_op_to_hw_pin_map(void)
{
    rcp_acf_byte_message_info_t   hdr = {0};
    rcp_bytes_t                   frame;
    rcp_regmap_general_t          map;
    rcp_regmap_hw_pin_map_entry_t hw_pin_map[1] = {{1u, 2u, 0x03u}};
    rcp_lifecycle_state_t         state         = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t    writer        = DISCOVERY_WRITER;
    rcp_wire_error_t              err;
    uint8_t                       tn = 0;
    rcp_regmap_ep0_errc_t         rc;
    uint8_t                       payload[3];

    rcp_regmap_general_init(&map);
    map.svr_hw_cfg_ptr = 0x0100u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)RCP_REGMAP_EP0_WRITE_OP_OR;
    hdr.transaction_num = 6;

    put_test_u16(payload, (uint16_t)(map.svr_hw_cfg_ptr + 2u));
    payload[2] = 0x04u;

    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);
    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer,
                                              hw_pin_map, 1u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT8(0x07u, hw_pin_map[0].hw_pin_type);
    rcp_bytes_free(&frame);
}

/* REQ-RMAP-068: evt[2:0]=010b (AND) applied end-to-end against
 * EP_ID_config's own ep_id octet (0xFF & 0x0F = 0x0F). */
static void test_ep0_dispatcher_applies_and_write_op_to_ep_id_map(void)
{
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_bytes_t                  frame;
    rcp_regmap_general_t         map;
    rcp_regmap_ep_id_map_entry_t ep_id_map[1] = {{0xFFu, 10u, 1u}}; /* ep_id, byte_bus_id,
                                                                        request_stream_index --
                                                                        the struct's own real
                                                                        declaration order */
    rcp_lifecycle_state_t        state        = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t   writer       = DISCOVERY_WRITER;
    rcp_wire_error_t             err;
    uint8_t                      tn = 0;
    rcp_regmap_ep0_errc_t        rc;
    uint8_t                      payload[3];

    rcp_regmap_general_init(&map);
    map.svr_ep_bytebus_id_map_ptr = 0x0200u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)RCP_REGMAP_EP0_WRITE_OP_AND;
    hdr.transaction_num = 7;

    put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 1u)); /* row 0's own ep_id */
    payload[2] = 0x0Fu;

    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);
    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL, 0u,
                                              ep_id_map, 1u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, NULL,
                                              0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT16(0x0Fu, ep_id_map[0].ep_id);
    rcp_bytes_free(&frame);
}

/* REQ-RMAP-068: evt[2:0]=011b (XOR) applied end-to-end against the
 * sequencer table's own Seq_state octet (0x0F ^ 0x0F = 0x00), through
 * the SAME ownership-authorized path REQ-SEQ-013's own tests already
 * cover -- proves the write-op combine step and REQ-SEQ-013's own
 * per-octet ownership authorization compose correctly (authorization
 * depends only on which octets the write's own span touches, not
 * their value, so it is unaffected either way). */
static void test_ep0_dispatcher_applies_xor_write_op_to_sequencer_table(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_regmap_general_t        map;
    uint8_t                     sequencer_state[1] = {0x0Fu};
    uint8_t                     sequencer_owner[1] = {3u};
    uint8_t                     payload[3];
    rcp_bytes_t                 frame;
    rcp_wire_error_t            err;
    uint8_t                     tn;
    rcp_regmap_ep0_errc_t       rc;

    rcp_regmap_general_init(&map);
    map.svr_sequencer_state_ptr = 0x0600u;

    put_test_u16(payload, (uint16_t)map.svr_sequencer_state_ptr);
    payload[2] = 0x0Fu;
    hdr.byte_bus_id = RCP_REGMAP_EP0_INDEX;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = (uint8_t)RCP_REGMAP_EP0_WRITE_OP_XOR;
    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                              RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u,
                                              sequencer_state, sequencer_owner, 1u, 3u,
                                              &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT8(0x00u, sequencer_state[0]);
    rcp_bytes_free(&frame);
}

/* REQ-RMAP-068 x REQ-WAKEUP-020: the fixed-ep_id invariant prediction
 * (ep_id_map_write_keeps_fixed_ep_id()) must see the SAME combined
 * bytes the write will really apply, not the raw pre-combine request
 * -- checking against raw bytes would be wrong under OR/AND/XOR since
 * the write's real effect depends on the table's own current content
 * too. Row 0 is WakeUp-typed (ep_type=9) with ep_id already fixed to
 * 1; both sub-cases OR at the SAME address with different data,
 * distinguished only by whether the COMBINED result keeps ep_id==1. */
static void test_ep0_dispatcher_or_write_op_respects_fixed_ep_id_after_combine(void)
{
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_bytes_t                  frame;
    rcp_regmap_general_t         map;
    rcp_lifecycle_state_t        state  = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t   writer = PLAIN_WRITER;
    rcp_wire_error_t             err;
    uint8_t                      tn = 0;
    rcp_regmap_ep0_errc_t        rc;
    uint8_t                      ep_types[1] = {9u};

    rcp_regmap_general_init(&map);
    map.svr_ep_bytebus_id_map_ptr = 0x0200u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)RCP_REGMAP_EP0_WRITE_OP_OR;
    hdr.transaction_num = 8;

    /* 1) OR 0x00 onto ep_id=1 -- combined stays 1, invariant held,
     * permitted. */
    {
        rcp_regmap_ep_id_map_entry_t ep_id_map[1] = {{1u, 1u, 10u}};
        uint8_t                      payload[3];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 1u));
        payload[2] = 0x00u;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, ep_id_map, 1u, NULL, 0u, NULL, 0u, NULL, 0u,
                                                   NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, ep_types,
                                                   9u, 1u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
        TEST_ASSERT_EQUAL_UINT16(1u, ep_id_map[0].ep_id);
        rcp_bytes_free(&frame);
    }

    /* 2) OR 0x06 onto ep_id=1 -- combined becomes 7, invariant broken,
     * denied, table left entirely unchanged (the raw request byte
     * alone, 0x06, would have looked fine if checked without
     * combining first -- this is exactly the bug this fix closes). */
    {
        rcp_regmap_ep_id_map_entry_t ep_id_map[1] = {{1u, 1u, 10u}};
        uint8_t                      payload[3];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 1u));
        payload[2] = 0x06u;

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL,
                                                   0u, ep_id_map, 1u, NULL, 0u, NULL, 0u, NULL, 0u,
                                                   NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, ep_types,
                                                   9u, 1u);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, err);
        TEST_ASSERT_EQUAL_UINT16(1u, ep_id_map[0].ep_id); /* unchanged */
        rcp_bytes_free(&frame);
    }
}

/* REQ-RMAP-068: evt[2:0]=010b (AND) applied end-to-end against an
 * optional-subsystem section (network_interface_cfg) -- proves the
 * direct cfg->data combine path (no separate render() call needed,
 * unlike every row-typed table above). */
static void test_ep0_dispatcher_applies_and_write_op_to_optional_subsystem_section(void)
{
    rcp_acf_byte_message_info_t              hdr          = {0};
    rcp_bytes_t                               frame;
    rcp_regmap_general_t                      map;
    rcp_regmap_optional_subsystem_cfg_t       network_cfg  = {{0}, 4u};
    rcp_regmap_optional_subsystem_cfg_ptrs_t  optional_cfg = {0};
    rcp_lifecycle_state_t                     state        = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t                writer       = PLAIN_WRITER;
    rcp_wire_error_t                          err;
    uint8_t                                   tn = 0;
    rcp_regmap_ep0_errc_t                     rc;
    uint8_t                                   payload[3];

    network_cfg.data[2] = 0xFFu;
    optional_cfg.network_interface_cfg = &network_cfg;

    rcp_regmap_general_init(&map);
    map.svr_network_interface_cfg_ptr = 0x0600u;

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)RCP_REGMAP_EP0_WRITE_OP_AND;
    hdr.transaction_num = 9;

    put_test_u16(payload, (uint16_t)(map.svr_network_interface_cfg_ptr + 2u));
    payload[2] = 0x0Fu;

    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);
    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map, state, writer, NULL, 0u,
                                              NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, NULL, 0u,
                                              0u, &err, &tn, 0u, &optional_cfg, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT8(0x0Fu, network_cfg.data[2]); /* 0xFF & 0x0F */
    rcp_bytes_free(&frame);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_reg_write_len_matches_the_formula);
    RUN_TEST(test_generic_config_request_implemented_endpoints);
    RUN_TEST(test_ep_len_overrun_rule_implemented_endpoints);
    RUN_TEST(test_discovery_claim_refusal_now_returns_a_real_signal);
    RUN_TEST(test_lifecycle_state_register_field_tracks_the_authoritative_state);
    RUN_TEST(test_general_map_render_matches_table_18_byte_offsets);
    RUN_TEST(test_general_map_wire_reach_now_covers_full_table_18);
    RUN_TEST(test_general_map_short_read_size_leaves_the_remainder_untouched);
    RUN_TEST(test_general_map_strerror_never_null_and_distinct);
    RUN_TEST(test_general_map_read_response_decode_rejects_malformed_frames);
    RUN_TEST(test_general_static_part_write_attempts_are_now_rejected_over_the_wire);
    RUN_TEST(test_mock_server_regmap_pointer_is_still_directly_mutable_in_process);
    RUN_TEST(test_req_stream_max_and_responder_streams_max_are_now_correctly_sized);
    RUN_TEST(test_responder_and_req_mem_size_are_now_distinctly_addressed);
    RUN_TEST(test_sequencers_max_is_now_correctly_sized_and_synced_from_the_table);
    RUN_TEST(test_configuration_lock_register_now_exists_and_defaults_unlocked);
    RUN_TEST(test_configuration_lock_not_yet_consulted_by_field_writable);
    RUN_TEST(test_implemented_options_now_matches_table_18_exactly);
    RUN_TEST(test_reserved_octet_at_0x17_is_now_explicitly_modeled);
    RUN_TEST(test_reserved_register_at_0x22_is_now_explicitly_modeled);
    RUN_TEST(test_io_pin_count_is_now_explicitly_modeled);
    RUN_TEST(test_hw_cfg_ptr_is_now_correctly_shaped);
    RUN_TEST(test_stream_cfg_registers_are_now_correctly_sized);
    RUN_TEST(test_ep_generic_cfg_ptr_and_capacity_are_now_correctly_shaped);
    RUN_TEST(test_ep_bytebus_id_map_ptr_and_capacity_are_now_correctly_shaped);
    RUN_TEST(test_functional_cfg_and_sequencer_state_ptrs_are_now_correctly_shaped);
    RUN_TEST(test_four_optional_subsystem_pointer_pairs_are_now_present);
    RUN_TEST(test_hw_config_table_now_has_real_server_side_storage);
    RUN_TEST(test_hw_pin_map_rejects_oversized_table_leaving_existing_data_intact);
    RUN_TEST(test_set_hw_pin_map_syncs_svr_io_pin_count);
    RUN_TEST(test_set_request_stream_cfg_syncs_svr_request_stream_cfg_capacity);
    RUN_TEST(test_set_response_queue_cfg_syncs_svr_response_stream_cfg_capacity);
    RUN_TEST(test_set_ep_id_map_syncs_svr_ep_bytebus_id_map_capacity);
    RUN_TEST(test_endpoint_registration_syncs_svr_ep_generic_cfg_capacity);
    RUN_TEST(test_hw_pin_map_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_hw_pin_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched);
    RUN_TEST(test_ep0_dispatcher_routes_all_five_pointed_to_tables_and_unknown_addresses);
    RUN_TEST(test_ep0_dispatcher_routes_optional_subsystem_cfg_sections);
    RUN_TEST(test_optional_subsystem_cfg_apply_reconfig_rejects_out_of_range_and_short);
    RUN_TEST(test_ep0_dispatcher_enforces_fixed_ep_id_for_configured_ep_type);
    RUN_TEST(test_mock_server_optional_subsystem_cfg_setters_sync_capacity);
    RUN_TEST(test_ep_id_map_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_ep_id_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched);
    RUN_TEST(test_response_queue_cfg_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_response_queue_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched);
    RUN_TEST(test_response_queue_cfg_render_saturates_oversized_flush_time_us_without_wrapping);
    RUN_TEST(test_ep_generic_cfg_render_matches_table_28_byte_offsets);
    RUN_TEST(test_ep_generic_cfg_render_uses_12_octet_stride_across_entries);
    RUN_TEST(test_ep_generic_cfg_render_falls_back_to_1us_for_unconfigured_delay_time);
    RUN_TEST(test_ep_generic_cfg_render_falls_back_to_1us_for_any_disallowed_delay_value);
    RUN_TEST(test_ep_generic_cfg_render_clamps_oversized_req_storage_size_without_wrapping);
    RUN_TEST(test_ep_generic_cfg_render_clamps_non_multiple_of_4_req_storage_size);
    RUN_TEST(test_ep_generic_cfg_render_has_no_ep_resp_on_error_bit_reserved_bits_stay_zero);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_write_touching_only_ep_type_is_a_no_op_confirmed_normally);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_row0_ep_used_write_is_ignored_stays_true);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_ep0_ep_used_forced_true_ep1_honors_write);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_leaves_partially_covered_field_unchanged);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_does_not_launder_an_untouched_rows_own_invalid_delay_time);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_extracts_delay_time_register_value);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_rejects_short_payload);
    RUN_TEST(test_ep_generic_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched);
    RUN_TEST(test_request_stream_cfg_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_request_stream_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched);
    RUN_TEST(test_request_stream_cfg_render_saturates_oversized_max_request_size_without_wrapping);
    RUN_TEST(test_request_stream_cfg_render_saturates_oversized_safestate_sequencer_without_wrapping);
    RUN_TEST(test_request_stream_cfg_render_packs_four_config_bits_at_0x000d);
    RUN_TEST(test_request_stream_cfg_render_couples_sequence_bit_with_and_not_or);
    RUN_TEST(test_request_stream_cfg_render_couples_watchdog_bit_with_and_not_or);
    RUN_TEST(test_request_stream_cfg_apply_reconfig_couples_sequence_and_watchdog_bits);
    RUN_TEST(test_request_stream_cfg_render_wires_rx_stream_status_bit_from_live_array);
    RUN_TEST(test_ep0_read_dispatcher_surfaces_live_rx_stream_status_from_a_real_crc_fault);
    RUN_TEST(test_ep0_write_dispatcher_authorizes_rx_stream_status_bit_even_when_rcp_configured);
    RUN_TEST(test_request_stream_cfg_render_falls_back_to_zero_when_watchdog_tick_rate_unconfigured);
    RUN_TEST(test_request_stream_cfg_render_produces_real_ticks_when_watchdog_tick_rate_configured);
    RUN_TEST(test_request_stream_cfg_render_falls_back_to_zero_when_ms_value_does_not_fit_even_configured);
    RUN_TEST(test_request_stream_cfg_apply_reconfig_converts_ticks_to_ms_when_watchdog_tick_rate_configured);
    RUN_TEST(test_request_stream_cfg_apply_reconfig_leaves_rx_wd_timeout_ms_unchanged_when_unconfigured);

    RUN_TEST(test_resolve_index_matches_by_rx_stream_id);
    RUN_TEST(test_resolve_index_no_match_returns_zero_sentinel);
    RUN_TEST(test_resolve_index_null_or_empty_returns_zero);

    RUN_TEST(test_sequencer_table_render_interleaves_state_and_owner);
    RUN_TEST(test_sequencer_table_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_sequencer_table_apply_reconfig_rejects_out_of_range_leaving_table_untouched);

    RUN_TEST(test_ep0_write_dispatcher_denies_seq_state_write_when_sequencer_unclaimed);
    RUN_TEST(test_ep0_write_dispatcher_permits_seq_state_write_by_the_recorded_owner);
    RUN_TEST(test_ep0_write_dispatcher_denies_seq_state_write_by_a_different_client);
    RUN_TEST(test_ep0_write_dispatcher_permits_claiming_an_unclaimed_sequencer);
    RUN_TEST(test_ep0_write_dispatcher_denies_stealing_an_already_claimed_sequencer);
    RUN_TEST(test_ep0_write_dispatcher_permits_owner_releasing_its_own_sequencer);
    RUN_TEST(test_ep0_write_dispatcher_still_enforces_functional_w_star_for_sequencer_writes);
    RUN_TEST(test_ep0_dispatcher_denies_unauthorized_writes_before_applying_or_bounds_checking);
    RUN_TEST(test_ep0_read_dispatcher_routes_all_seven_extents_and_unknown_addresses);
    RUN_TEST(test_hw_config_row_stride_now_modeled_gpio_access_class_still_diverges);
    RUN_TEST(test_hw_pin_type_matches_table_20);
    RUN_TEST(test_hw_pin_output_stage_has_no_exclusive_input_flag);
    RUN_TEST(test_output_pin_loses_its_input_capability);
    RUN_TEST(test_named_signal_index_covers_every_endpoint_type);
    RUN_TEST(test_request_stream_cfg_now_has_channel_and_stream_indices);
    RUN_TEST(test_watchdog_timeout_internal_unit_is_still_milliseconds);
    RUN_TEST(test_wd_timeout_ms_to_ticks_rounds_down_and_bounds_checks);
    RUN_TEST(test_wd_timeout_ticks_to_ms_round_trips);
    RUN_TEST(test_table22_w_star_writable_in_both_pre_rcp_configured_states);
    RUN_TEST(test_ep_id_row_now_has_request_stream_index);
    RUN_TEST(test_ep_id_map_effective_count_stops_at_sentinel);
    RUN_TEST(test_ep_id_map_power_on_default_permits_ep0);
    RUN_TEST(test_ep_id_ordering_considers_request_stream_index);
    RUN_TEST(test_ep_id_map_flags_multi_client_ep);
    RUN_TEST(test_ep_id_map_flags_heterogeneous_shared_bus);
    RUN_TEST(test_ep_id_map_flags_wrong_ep_id_for_a_fixed_endpoint_type);
    RUN_TEST(test_byte_bus_id_is_now_eleven_bits_wide);
    RUN_TEST(test_ep_id_map_render_matches_table_25_26_byte_offsets);
    RUN_TEST(test_ep_id_map_render_and_apply_reconfig_pack_bbid_ctrl_per_table_25_26);
    RUN_TEST(test_w_plus_field_now_has_a_real_lockable_primitive);
    RUN_TEST(test_response_queue_stream_id_is_configurable);
    RUN_TEST(test_response_queue_size_register_and_storage_now_exist);
    RUN_TEST(test_max_avtpdu_size_is_now_enforced_and_feeds_fragmentation);
    RUN_TEST(test_max_avtpdu_size_within_mtu_check);
    RUN_TEST(test_flush_on_count_trigger_and_avtpdu_packing);
    RUN_TEST(test_flush_time_trigger_and_empty_heartbeat_are_composable);
    RUN_TEST(test_transmit_fragmentation_now_uses_the_correct_octet_budget);
    RUN_TEST(test_svr_ep_cfg_now_models_discovery_timeout_and_status);
    RUN_TEST(test_field_write_error_distinguishes_state_from_writer_denial);
    RUN_TEST(test_effective_register_write_length_helper_matches_the_formula);

    RUN_TEST(test_ep0_combine_write_op_implements_set_or_and_xor);
    RUN_TEST(test_ep0_dispatcher_rejects_reserved_write_op_before_any_routing);
    RUN_TEST(test_ep0_dispatcher_applies_or_write_op_to_hw_pin_map);
    RUN_TEST(test_ep0_dispatcher_applies_and_write_op_to_ep_id_map);
    RUN_TEST(test_ep0_dispatcher_applies_xor_write_op_to_sequencer_table);
    RUN_TEST(test_ep0_dispatcher_or_write_op_respects_fixed_ep_id_after_combine);
    RUN_TEST(test_ep0_dispatcher_applies_and_write_op_to_optional_subsystem_section);

    return UNITY_END();
}
