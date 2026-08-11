/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ACF-022
//cfusa:test REQ-GPIO-001
//cfusa:test REQ-GPIO-002
//cfusa:test REQ-GPIO-003
//cfusa:test REQ-GPIO-004
//cfusa:test REQ-GPIO-005
//cfusa:test REQ-GPIO-006
//cfusa:test REQ-GPIO-007
//cfusa:test REQ-GPIO-008
//cfusa:test REQ-GPIO-009
//cfusa:test REQ-GPIO-010
//cfusa:test REQ-GPIO-011
//cfusa:test REQ-GPIO-012
//cfusa:test REQ-GPIO-013
//cfusa:test REQ-GPIO-014
//cfusa:test REQ-GPIO-015
//cfusa:test REQ-GPIO-016
//cfusa:test REQ-GPIO-017
//cfusa:test REQ-GPIO-018
//cfusa:test REQ-GPIO-019
//cfusa:test REQ-GPIO-020
//cfusa:test REQ-GPIO-021
//cfusa:test REQ-GPIO-022
//cfusa:test REQ-GPIO-023
//cfusa:test REQ-GPIO-024
//cfusa:test REQ-GPIO-025
//cfusa:test REQ-GPIO-026
//cfusa:test REQ-GPIO-027
//cfusa:test REQ-GPIO-028
//cfusa:test REQ-GPIO-029
//cfusa:test REQ-GPIO-030
//cfusa:test REQ-GPIO-031
//cfusa:test REQ-GPIO-032
//cfusa:test REQ-GPIO-037
//cfusa:test REQ-GPIO-038
//cfusa:test REQ-GPIO-039
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_gpio.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Pin addressing ────────────────────────────────────────────────────────── */

static void test_pin_index_valid_bounds(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_index_valid(0));
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_index_valid(31));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_index_valid(32));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_index_valid(255));
}

static void test_pin_mask(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, rcp_ep_gpio_pin_mask(0));
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, rcp_ep_gpio_pin_mask(31));
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_ep_gpio_pin_mask(32));
}

static void test_pin_get(void)
{
    uint32_t bitmask = (1u << 3) | (1u << 17);

    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_get(bitmask, 3));
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_get(bitmask, 17));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_get(bitmask, 4));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_get(bitmask, 32)); /* invalid index -> false, not error */
}

/* ── evt[2:0] write semantics ──────────────────────────────────────────────── */

static void test_write_semantics_valid(void)
{
    uint8_t v;

    for (v = 0; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_gpio_write_semantics_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_gpio_write_semantics_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_gpio_write_semantics_valid(255));
}

static void test_apply_write_replace(void)
{
    TEST_ASSERT_EQUAL_UINT32(0xABCDu,
        rcp_ep_gpio_apply_write(0x1234u, 0xABCDu, RCP_EP_GPIO_WRITE_REPLACE));
}

static void test_apply_write_or(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x0Fu,
        rcp_ep_gpio_apply_write(0x0Au, 0x05u, RCP_EP_GPIO_WRITE_OR));
}

static void test_apply_write_and(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x0Au,
        rcp_ep_gpio_apply_write(0x0Fu, 0x0Au, RCP_EP_GPIO_WRITE_AND));
}

static void test_apply_write_xor(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x0Fu,
        rcp_ep_gpio_apply_write(0x0Au, 0x05u, RCP_EP_GPIO_WRITE_XOR));
}

static void test_apply_write_add_ordinary(void)
{
    TEST_ASSERT_EQUAL_UINT32(30u, rcp_ep_gpio_apply_write(10u, 20u, RCP_EP_GPIO_WRITE_ADD));
}

static void test_apply_write_add_saturates_at_upper_boundary(void)
{
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu,
        rcp_ep_gpio_apply_write(0xFFFFFFF0u, 0x100u, RCP_EP_GPIO_WRITE_ADD));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu,
        rcp_ep_gpio_apply_write(0xFFFFFFFFu, 0xFFFFFFFFu, RCP_EP_GPIO_WRITE_ADD));
}

/* TC18 v0.5.1_RC Table 30 (§13.5). The evt[2:0]=110b row belongs to the
 * "GPIO, PWM_OUT" endpoint group -- one row, one rule, covering both --
 * and states the operation as:
 *
 *   "byte_msg_payload" minus "current interface status" is written as is
 *   to interface
 *
 * i.e. request - current. (Its parenthetical "this can be used to
 * decrease the duty cycle of PWM_out" is an illustrative note; the row
 * has no GPIO-specific worked example that would suggest a different
 * operand order for GPIO.) Since subtraction is not commutative the order
 * is directly observable: apply_write(current=20, request=30) must be 10,
 * and the reverse operand order would give 10 for (30, 20) instead. */
static void test_apply_write_sub_is_request_minus_current(void)
{
    TEST_ASSERT_EQUAL_UINT32(10u, rcp_ep_gpio_apply_write(20u, 30u, RCP_EP_GPIO_WRITE_SUB));
    TEST_ASSERT_EQUAL_UINT32(0x00FFu,
        rcp_ep_gpio_apply_write(0x00000001u, 0x00000100u, RCP_EP_GPIO_WRITE_SUB));
}

/* Same section's closing sentence:
 *
 *   While doing additions and subtractions neither overflows nor
 *   wrap-arounds shall occur. The values are saturated at 0x0000 on the
 *   low side and 0xFFFF at the high side.
 *
 * (applied here at this endpoint's own 32-bit register width). A request
 * smaller than the current status saturates at zero rather than wrapping. */
static void test_apply_write_sub_saturates_at_lower_boundary(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_ep_gpio_apply_write(20u, 5u, RCP_EP_GPIO_WRITE_SUB));
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_ep_gpio_apply_write(1u, 0u, RCP_EP_GPIO_WRITE_SUB));
}

static void test_apply_write_reserved4_is_noop(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x1234u,
        rcp_ep_gpio_apply_write(0x1234u, 0xFFFFu, RCP_EP_GPIO_WRITE_RESERVED4));
}

/* Regression test for issue #104: raw wire evt[2:0] values 4/5/6 must map
 * to Reserved/ADD/SUB respectively (not the previous off-by-one ADD/SUB/
 * Reserved mapping). Exercises rcp_ep_gpio_apply_write() with the raw
 * wire-value enum casts a decoder would actually produce, not just the
 * named constants, so a future accidental re-shuffle of the enum values
 * themselves (not just their names) would still be caught. */
static void test_apply_write_wire_value_4_is_reserved_noop(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x1234u,
        rcp_ep_gpio_apply_write(0x1234u, 0xFFFFu, (rcp_ep_gpio_write_semantics_t)4u));
}

static void test_apply_write_wire_value_5_is_add(void)
{
    TEST_ASSERT_EQUAL_UINT32(30u,
        rcp_ep_gpio_apply_write(10u, 20u, (rcp_ep_gpio_write_semantics_t)5u));
}

/* Raw wire evt[2:0] == 6 is TC18 Table 30's subtract row, computing
 * payload - current (see test_apply_write_sub_is_request_minus_current()
 * for the cited text): current=20, request=30 yields 10. */
static void test_apply_write_wire_value_6_is_sub(void)
{
    TEST_ASSERT_EQUAL_UINT32(10u,
        rcp_ep_gpio_apply_write(20u, 30u, (rcp_ep_gpio_write_semantics_t)6u));
}

static void test_toggle_pin_direction_toggles_only_flagged_pins(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];

    memset(pins, 0, sizeof(pins));
    pins[0] = RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP;
    pins[1] = RCP_REGMAP_PIN_PROP_INPUT;
    pins[2] = RCP_REGMAP_PIN_PROP_OUTPUT;

    /* Flag pins 0 and 1 only; pin 2 must be left untouched. */
    rcp_ep_gpio_toggle_pin_direction(pins, (1u << 0) | (1u << 1));

    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_INPUT | RCP_REGMAP_PIN_PROP_PULL_UP, pins[0]);
    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_OUTPUT, pins[1]);
    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_OUTPUT, pins[2]); /* untouched */
}

/* ── Input-pin write masking (13.7.4.3) ───────────────────────────────────── */

static void make_pins(uint8_t pins[RCP_EP_GPIO_MAX_PINS], uint32_t output_mask)
{
    uint8_t i;
    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        pins[i] = (uint8_t)((output_mask & rcp_ep_gpio_pin_mask(i)) != 0
                                 ? RCP_REGMAP_PIN_PROP_OUTPUT
                                 : RCP_REGMAP_PIN_PROP_INPUT);
    }
}

static void test_apply_masked_write_replace_preserves_input_pins(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];
    make_pins(pins, 0x0000000Fu); /* pins 0-3 output, rest input */

    uint32_t result = rcp_ep_gpio_apply_masked_write(0xABCD0005u, 0xFFFFFFF0u,
                                                       RCP_EP_GPIO_WRITE_REPLACE, pins);

    /* Output pins 0-3 take the request's bits (0x0); every input pin keeps
     * its prior value (0xABCD0000) untouched, including bit 4. */
    TEST_ASSERT_EQUAL_UINT32(0xABCD0000u, result);
}

static void test_apply_masked_write_or_and_only_affect_output_pins(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];
    make_pins(pins, 0x000000FFu); /* pins 0-7 output, rest input */

    uint32_t result =
        rcp_ep_gpio_apply_masked_write(0x00000F0Fu, 0xFFFFFFF0u, RCP_EP_GPIO_WRITE_OR, pins);
    TEST_ASSERT_EQUAL_UINT32(0x00000FFFu, result);

    result = rcp_ep_gpio_apply_masked_write(0x0000FFFFu, 0x00000000u, RCP_EP_GPIO_WRITE_AND, pins);
    TEST_ASSERT_EQUAL_UINT32(0x0000FF00u, result); /* output byte cleared, input untouched */
}

static void test_apply_masked_write_add_saturation_still_respects_mask(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];
    make_pins(pins, 0x0000FFFFu); /* low 16 bits output, high 16 bits input */

    uint32_t result =
        rcp_ep_gpio_apply_masked_write(0xBEEF0000u, 0xFFFFFFFFu, RCP_EP_GPIO_WRITE_ADD, pins);

    /* The 32-bit saturating add itself saturates to 0xFFFFFFFF, but masking
     * still confines the committed change to the output half. */
    TEST_ASSERT_EQUAL_UINT32(0xBEEFFFFFu, result);
}

static void test_apply_masked_write_reconfig_is_a_noop(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];
    make_pins(pins, 0x00000000u); /* every pin input */

    uint32_t result =
        rcp_ep_gpio_apply_masked_write(0x12345678u, 0xFFFFFFFFu, RCP_EP_GPIO_WRITE_RECONFIG, pins);

    TEST_ASSERT_EQUAL_UINT32(0x12345678u, result);
}

/* ── Per-pin trigger signals ────────────────────────────────────────────────── */

static void test_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, true, true));
}

static void test_trigger_any_change(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, false, true));
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, true, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, false, false));
}

static void test_trigger_rising(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, true, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, false, false));
}

static void test_trigger_falling(void)
{
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, true, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, false, false));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    size_t i;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);

    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, cfg.pins[i].pin_property);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_NONE, cfg.pins[i].trigger);
    }

    TEST_ASSERT_EQUAL_UINT(0u, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.clk_divider);
    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        TEST_ASSERT_EQUAL_UINT8(0u, cfg.debounce[i]);
    }
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_gpio_functional_cfg_writable(
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
    TEST_ASSERT_FALSE(rcp_ep_gpio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t via_ep0 = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_gpio_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

static void test_set_pin_property_rejects_invalid_pin_or_unauthorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      authorized = {0};
    rcp_lifecycle_writer_ctx_t      none = {0};

    authorized.via_root_client_ep0 = true;
    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_property(
        &cfg, 32, RCP_REGMAP_PIN_PROP_OUTPUT, RCP_LIFECYCLE_HW_CONFIGURED, authorized));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.pins[0].pin_property);

    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_property(
        &cfg, 0, RCP_REGMAP_PIN_PROP_OUTPUT, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.pins[0].pin_property);
}

static void test_set_pin_property_applies_when_authorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_gpio_set_pin_property(
        &cfg, 5, RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP,
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP,
                             cfg.pins[5].pin_property);
}

static void test_set_pin_trigger_rejects_invalid_pin_or_unauthorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      none = {0};

    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_trigger(
        &cfg, 32, RCP_EP_GPIO_TRIGGER_RISING, RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_trigger(
        &cfg, 0, RCP_EP_GPIO_TRIGGER_RISING, RCP_LIFECYCLE_HW_UNCONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_NONE, cfg.pins[0].trigger);
}

static void test_set_pin_trigger_applies_when_authorized(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t      writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_gpio_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_gpio_set_pin_trigger(
        &cfg, 9, RCP_EP_GPIO_TRIGGER_FALLING, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_FALLING, cfg.pins[9].trigger);
}

/* ── The EP_func register block (evt[2:0] == 111b) ────────────────────────────
 * ADDED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group G, REQ-GPIO-013). */

static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    uint8_t                      block[RCP_EP_GPIO_EP_FUNC_LEN];

    rcp_ep_gpio_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status         = 0x1234u;
    cfg.clk_divider       = 0x05u;
    cfg.debounce[0]       = 0x11u;
    cfg.debounce[31]      = 0x22u;

    rcp_ep_gpio_render_registers(&cfg, block);

    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_GPIO_EP_FUNC_LEN, block[0x00]); /* gpio_ep_len */
    TEST_ASSERT_EQUAL_HEX8(RCP_EP_GPIO_MAX_PINS, block[0x01]);            /* gpio_io_max */
    TEST_ASSERT_EQUAL_HEX8(0x01u, block[0x02]);                          /* enable&clr: ep_enable bit */
    TEST_ASSERT_EQUAL_HEX8(0x00u, block[0x03]);                          /* options */
    TEST_ASSERT_EQUAL_HEX8(0x00u, block[0x04]);                          /* base_clk hi */
    TEST_ASSERT_EQUAL_HEX8(0x00u, block[0x05]);                          /* base_clk lo */
    TEST_ASSERT_EQUAL_HEX8(0x12u, block[0x06]);                          /* ep_status hi */
    TEST_ASSERT_EQUAL_HEX8(0x34u, block[0x07]);                          /* ep_status lo */
    TEST_ASSERT_EQUAL_HEX8(0x05u, block[0x08]);                          /* clk_divider */
    TEST_ASSERT_EQUAL_HEX8(0x11u, block[0x09]);                          /* debounce_IO0 */
    TEST_ASSERT_EQUAL_HEX8(0x22u, block[0x28]);                          /* debounce_IO31 */
}

static void test_apply_reconfig_writes_clk_divider(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    const uint8_t                 write[3] = {0x00, 0x08, 0x2Au};

    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_OK, rcp_ep_gpio_apply_reconfig(&cfg, write, sizeof(write)));
    TEST_ASSERT_EQUAL_HEX8(0x2Au, cfg.clk_divider);
}

static void test_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    /* start 0x08 (clk_divider), then debounce_IO0/IO1 */
    const uint8_t                 write[5] = {0x00, 0x08, 0x07u, 0xAAu, 0xBBu};

    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_OK, rcp_ep_gpio_apply_reconfig(&cfg, write, sizeof(write)));
    TEST_ASSERT_EQUAL_HEX8(0x07u, cfg.clk_divider);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, cfg.debounce[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBBu, cfg.debounce[1]);
}

static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    /* Targets EP_LEN(0x00, R), IO_MAX(0x01, R), and base_clk(0x04-0x05, R)
     * simultaneously with a real write starting at 0x00; only the
     * read-only offsets are ignored. */
    const uint8_t                 write[7] = {0x00, 0x00, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_OK, rcp_ep_gpio_apply_reconfig(&cfg, write, sizeof(write)));

    {
        uint8_t block[RCP_EP_GPIO_EP_FUNC_LEN];
        rcp_ep_gpio_render_registers(&cfg, block);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_GPIO_EP_FUNC_LEN, block[0x00]); /* untouched */
        TEST_ASSERT_EQUAL_HEX8(RCP_EP_GPIO_MAX_PINS, block[0x01]);            /* untouched */
        TEST_ASSERT_EQUAL_HEX8(0x00u, block[0x04]);                          /* untouched */
        TEST_ASSERT_EQUAL_HEX8(0x00u, block[0x05]);                          /* untouched */
    }
    /* The one writable octet in the span (0x02, ep_enable&clr) DID apply. */
    TEST_ASSERT_TRUE(cfg.common.ep_enable);
}

static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    /* 0x28 (gpio_debounce_IO31) + 2 octets overruns 0x29. */
    const uint8_t                 overrun[4] = {0x00, 0x28, 0xAAu, 0xBBu};

    rcp_ep_gpio_functional_cfg_init(&cfg);
    cfg.debounce[31] = 0x99u;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_ERR_OUT_OF_RANGE,
                      rcp_ep_gpio_apply_reconfig(&cfg, overrun, sizeof(overrun)));
    TEST_ASSERT_EQUAL_HEX8(0x99u, cfg.debounce[31]); /* whole write ignored */
}

static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;
    const uint8_t                 addr_only[2] = {0x00, 0x08};

    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_ERR_SHORT,
                      rcp_ep_gpio_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_ERR_SHORT,
                      rcp_ep_gpio_apply_reconfig(&cfg, NULL, 0));
}

static void test_reconfig_request_round_trip(void)
{
    const uint8_t data[2] = {0x11u, 0x22u};
    rcp_bytes_t   frame;
    rcp_ep_gpio_functional_cfg_t cfg;

    frame = rcp_ep_gpio_encode_reconfig_request(3u, 0x0008u, data, sizeof(data), 7u);
    TEST_ASSERT_NOT_NULL(frame.data);

    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RECONFIG_OK,
                      rcp_ep_gpio_apply_reconfig(&cfg, frame.data + 8, frame.len - 8));
    TEST_ASSERT_EQUAL_HEX8(0x11u, cfg.clk_divider);
    TEST_ASSERT_EQUAL_HEX8(0x22u, cfg.debounce[0]);

    rcp_bytes_free(&frame);
}

static void test_reconfig_strerror_never_null(void)
{
    TEST_ASSERT_NOT_NULL(rcp_ep_gpio_reconfig_strerror(RCP_EP_GPIO_RECONFIG_OK));
    TEST_ASSERT_NOT_NULL(rcp_ep_gpio_reconfig_strerror(RCP_EP_GPIO_RECONFIG_ERR_SHORT));
    TEST_ASSERT_NOT_NULL(rcp_ep_gpio_reconfig_strerror(RCP_EP_GPIO_RECONFIG_ERR_OUT_OF_RANGE));
    TEST_ASSERT_NOT_NULL(rcp_ep_gpio_reconfig_strerror((rcp_ep_gpio_reconfig_errc_t)99));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_gpio_errc_t codes[] = {
        RCP_EP_GPIO_OK, RCP_EP_GPIO_ERR_SHORT_FRAME, RCP_EP_GPIO_ERR_BAD_MSG_TYPE,
        RCP_EP_GPIO_ERR_WRONG_BUS, RCP_EP_GPIO_ERR_WRONG_OP, RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_gpio_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_gpio_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_gpio_strerror((rcp_ep_gpio_errc_t)999));
}

/* ── Read request round trip ───────────────────────────────────────────────── */

static void test_read_request_round_trip(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_read_request(3, 42);
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK, rcp_ep_gpio_decode_read_request(frame.data, frame.len, 3, &txn));
    TEST_ASSERT_EQUAL_UINT8(42, txn);

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_read_request(3, 42);
    uint8_t     txn = 0;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_BUS,
                       rcp_ep_gpio_decode_read_request(frame.data, frame.len, 4, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     txn = 0;

    hdr.byte_bus_id = 3;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_OP,
                       rcp_ep_gpio_decode_read_request(frame.data, frame.len, 3, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    uint8_t              txn = 0;

    gbb_hdr.info.byte_bus_id = 3;
    gbb_hdr.info.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_MSG_TYPE,
                       rcp_ep_gpio_decode_read_request(frame.data, frame.len, 3, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_short_frame(void)
{
    uint8_t too_short[3] = {0};
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_SHORT_FRAME,
                       rcp_ep_gpio_decode_read_request(too_short, sizeof(too_short), 3, &txn));
}

/* ── Write request round trip ──────────────────────────────────────────────── */

static void test_write_request_round_trip(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_write_request(5, 0xDEADBEEFu, RCP_EP_GPIO_WRITE_XOR, 7);
    uint32_t    bitmask = 0;
    uint8_t     evt = 0xFF;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, bitmask);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_WRITE_XOR, evt);
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

static void test_write_request_evt_masked_to_low_3_bits(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_write_request(5, 0, RCP_EP_GPIO_WRITE_RECONFIG, 0);
    uint32_t    bitmask;
    uint8_t     evt;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_WRITE_RECONFIG, evt);

    rcp_bytes_free(&frame);
}

static void test_write_request_rejects_wrong_bus_op_and_bad_payload_len(void)
{
    rcp_bytes_t                 frame;
    uint32_t                    bitmask;
    uint8_t                     evt;
    uint8_t                     txn;
    rcp_acf_byte_message_info_t hdr = {0};

    frame = rcp_ep_gpio_encode_write_request(5, 1, RCP_EP_GPIO_WRITE_REPLACE, 0);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_BUS,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 6, &bitmask, &evt, &txn));
    rcp_bytes_free(&frame);

    hdr.byte_bus_id = 5;
    hdr.op          = RCP_ACF_OP_READ; /* not a write */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_OP,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    rcp_bytes_free(&frame);

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id = 5;
    hdr.op          = RCP_ACF_OP_WRITE;
    {
        uint8_t short_payload[2] = {0, 0};
        frame = rcp_acf_encode_abb(&hdr, short_payload, sizeof(short_payload));
    }
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
        rcp_ep_gpio_decode_write_request(frame.data, frame.len, 5, &bitmask, &evt, &txn));
    rcp_bytes_free(&frame);
}

/* ── Response round trip ───────────────────────────────────────────────────── */

static void test_response_round_trip_untimed(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(2, 0x0000FF00u, 11, false, 0);
    uint32_t    bitmask = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 2, &bitmask, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(0x0000FF00u, bitmask);
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0, ts);
    TEST_ASSERT_EQUAL_UINT8(11, txn);

    rcp_bytes_free(&frame);
}

static void test_response_round_trip_timed(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(2, 0x11223344u, 200, true, 0x0102030405060708ull);
    uint32_t    bitmask = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 2, &bitmask, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, bitmask);
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);
    TEST_ASSERT_EQUAL_UINT8(200, txn);

    rcp_bytes_free(&frame);
}

/* REQ-ACF-022, TC18.txt L1885: "rsp 1b -- identifies this ABB or GBB
 * message as response". Decoded directly via rcp_acf_decode_abb() (not
 * rcp_ep_gpio_decode_response(), which doesn't expose the raw header bit)
 * so this checks the actual wire byte, not a value round-tripped through
 * the same encoder/decoder pair that produced it. */
static void test_response_sets_rsp_bit(void)
{
    rcp_bytes_t                 frame = rcp_ep_gpio_encode_response(2, 0x0000FF00u, 11, false, 0);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload     = NULL;
    size_t                       payload_len = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
        rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.rsp);

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(2, 0, 0, false, 0);
    uint32_t    bitmask;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_WRONG_BUS,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 3, &bitmask, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_bad_payload_len(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint32_t                    bitmask;
    bool                        timed;
    uint64_t                    ts;
    uint8_t                     txn;
    uint8_t                     short_payload[2] = {0, 0};

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_abb(&hdr, short_payload, sizeof(short_payload));

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
        rcp_ep_gpio_decode_response(frame.data, frame.len, 2, &bitmask, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_short_frame(void)
{
    uint8_t  too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    uint32_t bitmask;
    bool     timed;
    uint64_t ts;
    uint8_t  txn;

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_SHORT_FRAME,
        rcp_ep_gpio_decode_response(too_short, sizeof(too_short), 2, &bitmask, &timed, &ts, &txn));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pin_index_valid_bounds);
    RUN_TEST(test_pin_mask);
    RUN_TEST(test_pin_get);

    RUN_TEST(test_write_semantics_valid);
    RUN_TEST(test_apply_write_replace);
    RUN_TEST(test_apply_write_or);
    RUN_TEST(test_apply_write_and);
    RUN_TEST(test_apply_write_xor);
    RUN_TEST(test_apply_write_add_ordinary);
    RUN_TEST(test_apply_write_add_saturates_at_upper_boundary);
    RUN_TEST(test_apply_write_sub_is_request_minus_current);
    RUN_TEST(test_apply_write_sub_saturates_at_lower_boundary);
    RUN_TEST(test_apply_write_reserved4_is_noop);
    RUN_TEST(test_apply_write_wire_value_4_is_reserved_noop);
    RUN_TEST(test_apply_write_wire_value_5_is_add);
    RUN_TEST(test_apply_write_wire_value_6_is_sub);
    RUN_TEST(test_toggle_pin_direction_toggles_only_flagged_pins);
    RUN_TEST(test_apply_masked_write_replace_preserves_input_pins);
    RUN_TEST(test_apply_masked_write_or_and_only_affect_output_pins);
    RUN_TEST(test_apply_masked_write_add_saturation_still_respects_mask);
    RUN_TEST(test_apply_masked_write_reconfig_is_a_noop);

    RUN_TEST(test_trigger_none_never_fires);
    RUN_TEST(test_trigger_any_change);
    RUN_TEST(test_trigger_rising);
    RUN_TEST(test_trigger_falling);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_set_pin_property_rejects_invalid_pin_or_unauthorized);
    RUN_TEST(test_set_pin_property_applies_when_authorized);
    RUN_TEST(test_set_pin_trigger_rejects_invalid_pin_or_unauthorized);
    RUN_TEST(test_set_pin_trigger_applies_when_authorized);

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_apply_reconfig_writes_clk_divider);
    RUN_TEST(test_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_reconfig_strerror_never_null);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_read_request_round_trip);
    RUN_TEST(test_read_request_rejects_wrong_bus);
    RUN_TEST(test_read_request_rejects_wrong_op);
    RUN_TEST(test_read_request_rejects_bad_msg_type);
    RUN_TEST(test_read_request_rejects_short_frame);

    RUN_TEST(test_write_request_round_trip);
    RUN_TEST(test_write_request_evt_masked_to_low_3_bits);
    RUN_TEST(test_write_request_rejects_wrong_bus_op_and_bad_payload_len);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_sets_rsp_bit);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_bad_payload_len);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
