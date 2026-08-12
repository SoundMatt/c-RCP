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
    rcp_lifecycle_request_stream_plausibility_t rs[1]  = {{true, true}};
    rcp_lifecycle_plausibility_snapshot_t       snap;
    rcp_lifecycle_writer_ctx_t                  writer = ROOT_WRITER;

    snap.endpoints            = eps;
    snap.endpoint_count       = 1u;
    snap.request_streams      = rs;
    snap.request_stream_count = 1u;

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
 * all four pairs (REQ-RMAP-039, the LAST Group 1 item), closing Group
 * 1 entirely: every one of the general map's original seven
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

/* REQ-RMAP-052/054 CLOSED (write-dispatch half, issue #301 batch 2):
 * same finding as HW_config's own, applied to EP_ID_config --
 * svr_ep_bytebus_id_map_ptr's own value is an absolute address in the
 * same EP0-scoped space Table 18 itself lives in. New
 * rcp_regmap_ep_id_map_apply_reconfig() is the parse-side inverse of
 * rcp_regmap_ep_id_map_render(); rcp_regmap_ep0_decode_write_request()
 * now routes to it the identical way it already routes to HW_config. */
static void test_ep0_dispatcher_routes_table18_hw_config_ep_id_config_and_unknown_addresses(void)
{
    rcp_acf_byte_message_info_t   hdr = {0};
    rcp_bytes_t                   frame;
    rcp_regmap_general_t          map;
    rcp_regmap_hw_pin_map_entry_t hw_pin_map[2] = {
        {1, 2, 0x03u},
        {4, 5, 0x06u},
    };
    rcp_regmap_ep_id_map_entry_t  ep_id_map[2] = {
        {10u, 20u, 1u},
        {30u, 40u, 1u},
    };
    rcp_wire_error_t              err;
    uint8_t                       tn = 0;
    rcp_regmap_ep0_errc_t         rc;

    rcp_regmap_general_init(&map);
    map.svr_hw_cfg_ptr             = 0x0100u; /* arbitrary, past Table 18's own 0x40 extent */
    map.svr_ep_bytebus_id_map_ptr  = 0x0200u; /* arbitrary, clear of HW_config's own range */

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 11;

    /* 1) An address within Table 18's own extent -- always denied,
     * regardless of what either pointed-to table's own pointer/table
     * say. */
    {
        uint8_t payload[3] = {0x00u, 0x10u, 0xFFu}; /* addr=0x0010, 1 data octet */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
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
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
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
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, err);
        rcp_bytes_free(&frame);
    }

    /* 4) An address within EP_ID_config's own extent -- applied. Row 1's
     * own byte_bus_id (2 octets) is at svr_ep_bytebus_id_map_ptr + 6
     * (row 1 begins at +4; byte_bus_id is that row's own octets 2-3). */
    {
        uint8_t payload[4];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 6u));
        put_test_u16(&payload[2], 99u);

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
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
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, err);
        rcp_bytes_free(&frame);
    }

    /* 5b) An address exactly one past EP_ID_config's own current extent
     * (count=2 -> 8 octets; ptr+8 is the first address NOT in range) --
     * must fall through to the unknown-address case (EP_NOT_FOUND), NOT
     * be routed into EP_ID_config's own apply_reconfig() at all. This is
     * the dispatcher's own routing boundary, distinct from (and not
     * substitutable by) apply_reconfig()'s own internal bounds check in
     * case 5 above -- a mutation-testing pass confirmed loosening the
     * dispatcher's own upper-bound comparison is NOT caught by any other
     * case in this function, since the inner function's own bounds check
     * happens to mask it for still-out-of-range addresses. */
    {
        uint8_t payload[2];

        put_test_u16(payload, (uint16_t)(map.svr_ep_bytebus_id_map_ptr + 8u));

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 6) An address that lands in none of Table 18, HW_config, or
     * EP_ID_config. */
    {
        uint8_t payload[3] = {0x00u, 0x50u, 0x00u}; /* 0x0050: past Table 18, before svr_hw_cfg_ptr */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
        TEST_ASSERT_EQUAL(RCP_ERROR_EP_NOT_FOUND, err);
        rcp_bytes_free(&frame);
    }

    /* 7) ACF-level failures still propagate correctly. */
    {
        uint8_t payload[1] = {0x00u}; /* too short for its own leading address */

        frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
        TEST_ASSERT_NOT_NULL(frame.data);
        rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                                   hw_pin_map, 2u, ep_id_map, 2u, &err, &tn);
        TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD, rc);
        rcp_bytes_free(&frame);
    }

    /* strerror() never NULL, distinct across at least two values. */
    TEST_ASSERT_NOT_NULL(rcp_regmap_ep0_strerror(RCP_REGMAP_EP0_OK));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(rcp_regmap_ep0_strerror(RCP_REGMAP_EP0_OK),
                                     rcp_regmap_ep0_strerror(RCP_REGMAP_EP0_ERR_WRONG_BUS)));
    TEST_ASSERT_NOT_NULL(rcp_regmap_hw_pin_map_reconfig_strerror(RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK));
    TEST_ASSERT_NOT_NULL(rcp_regmap_ep_id_map_reconfig_strerror(RCP_REGMAP_EP_ID_MAP_RECONFIG_OK));
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

/* REQ-RMAP-052/054 (row-stride half): TC18 §12.7.8 Table 23 lays each
 * row out as four consecutive octets -- request_stream_index (0x0000),
 * ep_id/EP_Nr (0x0001), byte_bus_id/BBID (0x0002, 16 bit) -- so row N
 * begins at relative address 4*N. rcp_regmap_ep_id_map_render()
 * (regmap.h/regmap.c) now serializes a real table at exactly this
 * stride, proven directly via a byte-offset check across two rows --
 * including ep_id's own honest truncation to the wire's real 8-bit
 * EP_Nr width (this module's own in-memory ep_id is 16 bit). The
 * ACF_ABB wire request/response wrapper itself is still not
 * implemented -- see regmap.h's own file-header note on the same
 * genuine, unresolved addressing question REQ-RMAP-040/041 (HW_config)
 * already documents. */
static void test_ep_id_map_render_matches_table_23_byte_offsets(void)
{
    rcp_regmap_ep_id_map_entry_t rows[2];
    uint8_t                      img[8];

    rows[0].request_stream_index = 0x11u;
    rows[0].ep_id                = 0x1234u; /* truncates to 0x34 on render */
    rows[0].byte_bus_id          = 0x0056u;
    rows[1].request_stream_index = 0x22u;
    rows[1].ep_id                = 0x0078u;
    rows[1].byte_bus_id          = 0x009Au;

    rcp_regmap_ep_id_map_render(rows, 2, img);

    TEST_ASSERT_EQUAL_HEX8(0x11u, img[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, img[1]); /* truncated from 0x1234 */
    TEST_ASSERT_EQUAL_HEX8(0x00u, img[2]);
    TEST_ASSERT_EQUAL_HEX8(0x56u, img[3]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, img[4]);
    TEST_ASSERT_EQUAL_HEX8(0x78u, img[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, img[6]);
    TEST_ASSERT_EQUAL_HEX8(0x9Au, img[7]);
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
    /* A third push would exceed the 8-octet reservation: refused. */
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, frame, sizeof(frame)));

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
    RUN_TEST(test_hw_pin_map_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_hw_pin_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched);
    RUN_TEST(test_ep0_dispatcher_routes_table18_hw_config_ep_id_config_and_unknown_addresses);
    RUN_TEST(test_ep_id_map_apply_reconfig_patches_addressed_octets_only);
    RUN_TEST(test_ep_id_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched);
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
    RUN_TEST(test_byte_bus_id_is_now_eleven_bits_wide);
    RUN_TEST(test_ep_id_map_render_matches_table_23_byte_offsets);
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

    return UNITY_END();
}
