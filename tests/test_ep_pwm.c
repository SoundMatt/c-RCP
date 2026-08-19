/* SPDX-License-Identifier: MPL-2.0 */
/* Per CONTRIBUTING.md's "Writing a requirement" convention: every
 * requirement-test tag below sits directly above the specific test
 * function that proves it, not stacked here at the file header -- a
 * file-header block of tags satisfies cfusa trace --sec-tested 100 for
 * every requirement in the file regardless of which test function (if
 * any) actually exercises each one. Moved 2026-08-18 (c-RCP-18-tracker,
 * REQ-PWM-* atomicity audit, issue #533). */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_pwm.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── PWM_OUT: evt[2:0] write semantics ─────────────────────────────────────── */

//cfusa:test REQ-PWM-001
static void test_out_write_semantics_valid(void)
{
    uint8_t v;

    for (v = 0; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_pwm_out_write_semantics_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_write_semantics_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_write_semantics_valid(255));
}

//cfusa:test REQ-PWM-002
static void test_out_apply_write_replace(void)
{
    rcp_ep_pwm_value_t current = {100, 50};
    rcp_ep_pwm_value_t request = {200, 75};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_REPLACE, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(200, result.period);
    TEST_ASSERT_EQUAL_UINT16(75, result.active_duration);
}

//cfusa:test REQ-PWM-003
static void test_out_apply_write_or(void)
{
    rcp_ep_pwm_value_t current = {0x00F0, 0x0F00};
    rcp_ep_pwm_value_t request = {0x000F, 0x00F0};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_OR, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(0x00FF, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

//cfusa:test REQ-PWM-004
static void test_out_apply_write_and(void)
{
    rcp_ep_pwm_value_t current = {0x00FF, 0x0FF0};
    rcp_ep_pwm_value_t request = {0x000F, 0x0FF0};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_AND, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(0x000F, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

//cfusa:test REQ-PWM-005
static void test_out_apply_write_xor(void)
{
    rcp_ep_pwm_value_t current = {0x00FF, 0x0F0F};
    rcp_ep_pwm_value_t request = {0x000F, 0x00FF};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_XOR, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(0x00F0, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

//cfusa:test REQ-PWM-006
static void test_out_apply_write_add_saturates(void)
{
    rcp_ep_pwm_value_t current = {0xFFF0, 100};
    rcp_ep_pwm_value_t request = {0x0020, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_ADD, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(0xFFFF, result.period); /* saturates, not wraps */
    TEST_ASSERT_EQUAL_UINT16(150, result.active_duration);
}

/* TC18 v0.5.1_RC Table 30 (§13.5), the evt[2:0]=110b row shared by the
 * GPIO and PWM_OUT endpoint group:
 *
 *   "byte_msg_payload" minus "current interface status" is written as is
 *   to interface
 *
 * i.e. request - current, NOT current - request. (The row's parenthetical
 * "this can be used to decrease the duty cycle of PWM_out" is an
 * illustrative note, not a definition of the operand order.) The same
 * section's closing sentence gives the saturation rule:
 *
 *   While doing additions and subtractions neither overflows nor
 *   wrap-arounds shall occur. The values are saturated at 0x0000 on the
 *   low side and 0xFFFF at the high side.
 *
 * Literal check, per that text: current={10,200}, request={50,50} yields
 * period 50-10 = 40, and active_duration 50-200 saturating low = 0. Under
 * the old, inverted implementation these came out 0 and 150 -- both
 * wrong, and different from these for every request where the operands
 * differ. */
//cfusa:test REQ-PWM-007
static void test_out_apply_write_sub_is_request_minus_current_and_saturates(void)
{
    rcp_ep_pwm_value_t current = {10, 200};
    rcp_ep_pwm_value_t request = {50, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_SUB, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(40, result.period);
    TEST_ASSERT_EQUAL_UINT16(0, result.active_duration); /* saturates at 0x0000, no wrap */
}

/* Companion to the above: the operand order is directly observable
 * because subtraction is not commutative. Table 30's "payload minus
 * current" with payload=0x0100 and current=0x0001 must be 0x00FF; the
 * inverted order would saturate to 0x0000 instead. */
//cfusa:test REQ-PWM-007
static void test_out_apply_write_sub_operand_order_is_observable(void)
{
    rcp_ep_pwm_value_t current = {0x0001, 0x0001};
    rcp_ep_pwm_value_t request = {0x0100, 0x0100};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_SUB, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(0x00FF, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x00FF, result.active_duration);
}

//cfusa:test REQ-PWM-008
static void test_out_apply_write_reserved4_is_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              RCP_EP_PWM_OUT_WRITE_RESERVED4, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

/* Regression test for issue #104: raw wire evt[2:0] values 4/5/6 must map
 * to Reserved/ADD/SUB respectively (not the previous off-by-one ADD/SUB/
 * Reserved mapping). Exercises rcp_ep_pwm_out_apply_write() with the raw
 * wire-value enum casts a decoder would actually produce, not just the
 * named constants. */
//cfusa:test REQ-PWM-008
static void test_out_apply_write_wire_value_4_is_reserved_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)4u, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

//cfusa:test REQ-PWM-006
static void test_out_apply_write_wire_value_5_is_add(void)
{
    rcp_ep_pwm_value_t current = {10, 200};
    rcp_ep_pwm_value_t request = {20, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)5u, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(30, result.period);
    TEST_ASSERT_EQUAL_UINT16(250, result.active_duration);
}

/* Raw wire evt[2:0] == 6 is the Table 30 subtract row, and it computes
 * payload - current (see test_out_apply_write_sub_is_request_minus_current
 * _and_saturates() for the cited text): current={30,250},
 * request={100,300} yields 70 and 50. */
//cfusa:test REQ-PWM-007
static void test_out_apply_write_wire_value_6_is_sub(void)
{
    rcp_ep_pwm_value_t current = {30, 250};
    rcp_ep_pwm_value_t request = {100, 300};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)6u, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(70, result.period);
    TEST_ASSERT_EQUAL_UINT16(50, result.active_duration);
}

//cfusa:test REQ-PWM-009
static void test_out_apply_write_reconfig_misrouted_is_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              RCP_EP_PWM_OUT_WRITE_RECONFIG, 0u, 0xFFFFu);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

/* ── PWM_OUT: evt[2:0] == 111b is an addressed EP_func register write ─────── */

/* TC18 v0.5.1_RC Table 30 (§13.5), the evt[2:0]=111b row for GPIO/PWM_OUT:
 *
 *   The byte_msg_payload is not presented to the interface but used to
 *   change the configuration of the endpoint (see 12.7.1).
 *
 * and §12.7.1 / Figure 18 "Configuration request" define the payload
 * shape: a "relative Register start address in EP_func" field followed by
 * "configuration data". The address field occupies the first 16 bits of
 * the payload's first quadlet (Figure 18's own bit ruler), so it is two
 * big-endian octets, and §12.7.1's prose adds:
 *
 *   The data of the byte_msg_payload from a write request is written into
 *   the section of the EP that is addressed by the byte_bus_id. At
 *   start_address =0x00 the EP_LEN parameter of the respective EP can be
 *   found as described for the EPs. Any byte_msg_payload for which the
 *   length plus the start_address results in a value larger than the
 *   EP_LEN, is to be ignored.
 *
 * The addressable registers are TC18 Table 43 "pwmo functional
 * configuration" (§13.7.5.2): 0x0000 pwmo_ep_len (8 bit, R), 0x0001
 * reserved (8 bit), 0x0002 pwmo_ep_enable&clr (8 bit, R/W), 0x0003
 * pwmo_ep_options (8 bit, R/W*), 0x0004 pwmo_base_clk (16 bit, R), 0x0006
 * pwmo_ep_status (16 bit, R/W), 0x0008 pwmo_clk_divider (8 bit, R/W),
 * 0x0009 the three 1-bit output-signal flags (R/W), 0x000A
 * pwmo_duty_cycle_min (16 bit, R/W), 0x000C pwmo_duty_cycle_max (16 bit,
 * R/W), 0x000E pwmo_skew (8 bit, R/W). The block therefore spans 0x0F
 * octets. */
//cfusa:test REQ-PWM-010
static void test_out_apply_reconfig_writes_clk_divider(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    /* start_address = 0x0008 (Table 43 pwmo_clk_divider), one data octet */
    const uint8_t payload[] = {0x00, 0x08, 0x2Au};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0x2Au, cfg.clk_divider);
    /* Neighbouring registers untouched. */
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.signal_flags);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.ep_status);
}

/* A single write spanning Table 43's 0x0008..0x000E tail: clk_divider,
 * signal flags, duty_cycle_min (16 bit BE), duty_cycle_max (16 bit BE),
 * skew. */
//cfusa:test REQ-PWM-010
static void test_out_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t payload[] = {
        0x00, 0x08,        /* start_address = 0x0008 */
        0x03,              /* 0x0008 pwmo_clk_divider */
        (uint8_t)(RCP_EP_PWM_OUT_FLAG_INV_POLARITY |
                  RCP_EP_PWM_OUT_FLAG_IDLE_STATE_INV), /* 0x0009 flags */
        0x01, 0x00,        /* 0x000A pwmo_duty_cycle_min = 0x0100 */
        0x0F, 0xA0,        /* 0x000C pwmo_duty_cycle_max = 0x0FA0 */
        0x07,              /* 0x000E pwmo_skew */
    };

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0x03u, cfg.clk_divider);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(RCP_EP_PWM_OUT_FLAG_INV_POLARITY |
                                       RCP_EP_PWM_OUT_FLAG_IDLE_STATE_INV),
                            cfg.signal_flags);
    TEST_ASSERT_EQUAL_UINT16(0x0100u, cfg.duty_cycle_min);
    TEST_ASSERT_EQUAL_UINT16(0x0FA0u, cfg.duty_cycle_max);
    TEST_ASSERT_EQUAL_UINT8(0x07u, cfg.skew);
}

/* Table 43 0x0002 pwmo_ep_enable&clr, whose bit layout is TC18 Table 32
 * "EP functional config common entries": 0x0002.0 ep_enable, 0x0002.4
 * ep_clear_req_storage. Writing bit 0 is how a client enables the
 * endpoint -- the operation the old single-bool "toggle" stood in for. */
//cfusa:test REQ-PWM-010
static void test_out_apply_reconfig_writes_enable_bit(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t enable[]  = {0x00, 0x02, 0x01u};
    const uint8_t disable[] = {0x00, 0x02, 0x00u};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    TEST_ASSERT_FALSE(cfg.common.ep_enable);

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, enable, sizeof(enable)));
    TEST_ASSERT_TRUE(cfg.common.ep_enable);

    /* Unlike the old toggle, a register write is absolute: writing the
     * same payload twice leaves the endpoint enabled both times. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, enable, sizeof(enable)));
    TEST_ASSERT_TRUE(cfg.common.ep_enable);

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, disable, sizeof(disable)));
    TEST_ASSERT_FALSE(cfg.common.ep_enable);
}

/* A write covering only one octet of a 16-bit register (Table 43's
 * 0x000C pwmo_duty_cycle_max) touches only that octet. */
//cfusa:test REQ-PWM-010
static void test_out_apply_reconfig_partial_multi_octet_register(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t payload[] = {0x00, 0x0Du, 0x55u}; /* low octet of 0x000C */

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    cfg.duty_cycle_max = 0xAABBu;

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xAA55u, cfg.duty_cycle_max);
}

/* §12.7.1: "Any byte_msg_payload for which the length plus the
 * start_address results in a value larger than the EP_LEN, is to be
 * ignored." Table 43's last register is 0x000E (8 bit), so EP_LEN is
 * 0x0F: a two-octet write at 0x000E overruns by one and the WHOLE write
 * is dropped. */
//cfusa:test REQ-PWM-060
static void test_out_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t payload[] = {0x00, 0x0Eu, 0x11u, 0x22u};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.skew); /* not even the in-range octet applied */
}

/* Table 43 marks 0x0000 pwmo_ep_len, 0x0001 reserved and 0x0004
 * pwmo_base_clk as R (read-only): a write covering them leaves them
 * alone while the rest of the same span still lands. */
//cfusa:test REQ-PWM-010
static void test_out_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t payload[] = {
        0x00, 0x00,        /* start_address = 0x0000 */
        0xFFu,             /* 0x0000 pwmo_ep_len       (R)   */
        0xFFu,             /* 0x0001 reserved          (R)   */
        0x01u,             /* 0x0002 enable&clr        (R/W) */
        0x00u,             /* 0x0003 options           (R/W) */
        0xFFu, 0xFFu,      /* 0x0004 pwmo_base_clk     (R)   */
        0x12u, 0x34u,      /* 0x0006 pwmo_ep_status    (R/W) */
    };
    uint8_t block[RCP_EP_PWM_OUT_EP_FUNC_LEN];

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    cfg.base_clk = 0x1234u;

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0x1234u, cfg.base_clk); /* read-only, unchanged */
    TEST_ASSERT_TRUE(cfg.common.ep_enable);          /* R/W, applied */
    TEST_ASSERT_EQUAL_UINT16(0x1234u, cfg.ep_status);

    rcp_ep_pwm_out_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_EP_FUNC_LEN,
                            block[RCP_EP_PWM_OUT_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0u, block[RCP_EP_PWM_OUT_REG_RESERVED_01]);
}

//cfusa:test REQ-PWM-011
static void test_out_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t addr_only[] = {0x00, 0x08};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, addr_only, 0));
}

/* Added 2026-08-18 (MC/DC gap closure, L285:C9 indices 0 and 1): before this,
 * nothing called rcp_ep_pwm_out_encode_reconfig_request() with either
 * data_len == 0 or data == NULL -- only the successful round-trip call
 * below (nonzero length, real buffer). These two new cases, paired with
 * that success case, give both `||` operands an independent true/false
 * swing. */
//cfusa:test REQ-PWM-010
static void test_out_encode_reconfig_request_rejects_zero_length_data(void)
{
    const uint8_t data[1] = {0x2Au};
    rcp_bytes_t   frame   = rcp_ep_pwm_out_encode_reconfig_request(9, 0x0008u, data, 0u, 77);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-PWM-010
static void test_out_encode_reconfig_request_rejects_null_data(void)
{
    rcp_bytes_t frame = rcp_ep_pwm_out_encode_reconfig_request(9, 0x0008u, NULL, 1u, 77);

    TEST_ASSERT_NULL(frame.data);
}

/* Figure 18 shows the configuration request as an ordinary ACF_ABB write
 * with evt[2:0] = 111b, address prefix then data. Round-trip: encode,
 * decode as a write request, route the payload to apply_reconfig(). */
//cfusa:test REQ-PWM-010
static void test_out_reconfig_request_round_trip(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t   data[] = {0x2Au};
    rcp_bytes_t     frame  = rcp_ep_pwm_out_encode_reconfig_request(9, 0x0008u, data,
                                                                     sizeof(data), 77);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_WRITE_RECONFIG, (uint8_t)(hdr.evt & 0x7u));
    TEST_ASSERT_EQUAL_UINT8(77, hdr.transaction_num);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN + sizeof(data), payload_len);
    /* Address prefix is 16-bit big-endian (Figure 18). */
    TEST_ASSERT_EQUAL_UINT8(0x00u, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x08u, payload[1]);

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_RECONFIG_OK,
                          rcp_ep_pwm_out_apply_reconfig(&cfg, payload, payload_len));
    TEST_ASSERT_EQUAL_UINT8(0x2Au, cfg.clk_divider);

    rcp_bytes_free(&frame);
}

/* render_registers() reports the block at exactly the Table 43 offsets. */
//cfusa:test REQ-PWM-010
static void test_out_render_registers_matches_table_offsets(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    uint8_t block[RCP_EP_PWM_OUT_EP_FUNC_LEN];

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    cfg.base_clk       = 0x0BB8u;
    cfg.ep_status      = 0x1234u;
    cfg.clk_divider    = 0x05u;
    cfg.signal_flags   = RCP_EP_PWM_OUT_FLAG_IDLE_STATE;
    cfg.duty_cycle_min = 0x0010u;
    cfg.duty_cycle_max = 0x0FA0u;
    cfg.skew           = 0x09u;
    cfg.common.ep_enable = true;

    rcp_ep_pwm_out_render_registers(&cfg, block);

    TEST_ASSERT_EQUAL_UINT8(0x0Fu, block[0x00]);  /* pwmo_ep_len   */
    TEST_ASSERT_EQUAL_UINT8(0x00u, block[0x01]);  /* reserved      */
    TEST_ASSERT_EQUAL_UINT8(0x01u, block[0x02]);  /* enable&clr    */
    TEST_ASSERT_EQUAL_UINT8(0x00u, block[0x03]);  /* options       */
    TEST_ASSERT_EQUAL_UINT8(0x0Bu, block[0x04]);  /* base_clk hi   */
    TEST_ASSERT_EQUAL_UINT8(0xB8u, block[0x05]);  /* base_clk lo   */
    TEST_ASSERT_EQUAL_UINT8(0x12u, block[0x06]);  /* status hi     */
    TEST_ASSERT_EQUAL_UINT8(0x34u, block[0x07]);  /* status lo     */
    TEST_ASSERT_EQUAL_UINT8(0x05u, block[0x08]);  /* clk_divider   */
    TEST_ASSERT_EQUAL_UINT8(0x02u, block[0x09]);  /* signal flags  */
    TEST_ASSERT_EQUAL_UINT8(0x00u, block[0x0A]);  /* duty min hi   */
    TEST_ASSERT_EQUAL_UINT8(0x10u, block[0x0B]);  /* duty min lo   */
    TEST_ASSERT_EQUAL_UINT8(0x0Fu, block[0x0C]);  /* duty max hi   */
    TEST_ASSERT_EQUAL_UINT8(0xA0u, block[0x0D]);  /* duty max lo   */
    TEST_ASSERT_EQUAL_UINT8(0x09u, block[0x0E]);  /* skew          */
}

//cfusa:test REQ-PWM-011
static void test_out_reconfig_strerror_never_null(void)
{
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_reconfig_strerror(RCP_EP_PWM_OUT_RECONFIG_OK));
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_reconfig_strerror(RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT));
    TEST_ASSERT_NOT_NULL(
        rcp_ep_pwm_out_reconfig_strerror(RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE));
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_reconfig_strerror((rcp_ep_pwm_out_reconfig_errc_t)99));
}

/* ── PWM_OUT: triggers ──────────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-012
static void test_out_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_DONE));
}

//cfusa:test REQ-PWM-013
static void test_out_trigger_cycle_start(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_DONE));
}

//cfusa:test REQ-PWM-014
static void test_out_trigger_mid_pulse(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_DONE));
}

//cfusa:test REQ-PWM-015
static void test_out_trigger_done(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_DONE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
}

/* ── PWM_OUT: functional config ─────────────────────────────────────────────── */

//cfusa:test REQ-PWM-016
static void test_out_functional_cfg_init_zeroes(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_TRIGGER_NONE, cfg.trigger);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.base_clk);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.clk_divider);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.signal_flags);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.duty_cycle_min);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.duty_cycle_max);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.skew);
}

//cfusa:test REQ-PWM-017
static void test_out_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

//cfusa:test REQ-PWM-018
static void test_out_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream(void)
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
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

//cfusa:test REQ-PWM-019
static void test_out_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t unauth = {0};
    rcp_lifecycle_writer_ctx_t auth   = {0};

    auth.via_root_client_ep0 = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, unauth));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, auth));
}

//cfusa:test REQ-PWM-020
static void test_out_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_set_trigger(&cfg, RCP_EP_PWM_OUT_TRIGGER_DONE,
                                                  RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_TRIGGER_NONE, cfg.trigger);
}

//cfusa:test REQ-PWM-021
static void test_out_set_trigger_applies_when_authorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_pwm_out_set_trigger(&cfg, RCP_EP_PWM_OUT_TRIGGER_DONE,
                                                 RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_TRIGGER_DONE, cfg.trigger);
}

//cfusa:test REQ-PWM-022
static void test_out_set_enabled_rejects_unauthorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_set_enabled(&cfg, true, RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_FALSE(cfg.common.ep_enable);
}

//cfusa:test REQ-PWM-023
static void test_out_set_enabled_applies_when_authorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_pwm_out_set_enabled(&cfg, true, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_TRUE(cfg.common.ep_enable);
}

/* ── PWM_OUT: error codes ──────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-024
static void test_out_strerror_never_null_and_distinct(void)
{
    rcp_ep_pwm_out_errc_t codes[] = {
        RCP_EP_PWM_OUT_OK,               RCP_EP_PWM_OUT_ERR_SHORT_FRAME,
        RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE, RCP_EP_PWM_OUT_ERR_WRONG_BUS,
        RCP_EP_PWM_OUT_ERR_WRONG_OP,     RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN,
        RCP_EP_PWM_OUT_ERR_RESERVED_EVT,
    };
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_pwm_out_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_pwm_out_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_strerror((rcp_ep_pwm_out_errc_t)99));
}

/* ADDED 2026-08-14 (issue #427, REQ-PWM-028): rcp_ep_pwm_out_wire_error()
 * mirrors ep_gpio.c's rcp_ep_gpio_wire_error() (REQ-GPIO-033) exactly for
 * PWM_OUT's own error enum -- TC18 §13.7.5.3's "a request not having
 * exactly four bytes" rule maps to RCP_ERROR_INVALID_PARAMETER, the same
 * numbered code GPIO's own, verbatim-identical §13.7.4.1 rule already
 * uses. */
//cfusa:test REQ-PWM-028
static void test_out_wire_error_maps_bad_payload_len_to_invalid_parameter(void)
{
    const int wire_code = (int)rcp_ep_pwm_out_wire_error(RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN);

    TEST_ASSERT_EQUAL_INT(15, wire_code);
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_INVALID_PARAMETER, wire_code);
}

/* ADDED 2026-08-14 (issue #426, REQ-PWM-008): the new
 * RCP_EP_PWM_OUT_ERR_RESERVED_EVT maps to RCP_ERROR_UNSUPPORTED_CMD --
 * TC18 §13.5 Table 33's GPIO/PWM_OUT row's own "reserved value ->
 * UNSUPPORTED_CMD" rule for evt[2:0]=100b. */
//cfusa:test REQ-PWM-008
static void test_out_wire_error_maps_reserved_evt_to_unsupported_cmd(void)
{
    const int wire_code = (int)rcp_ep_pwm_out_wire_error(RCP_EP_PWM_OUT_ERR_RESERVED_EVT);

    TEST_ASSERT_EQUAL_INT(1, wire_code);
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_UNSUPPORTED_CMD, wire_code);
}

/* Every other rcp_ep_pwm_out_errc_t value is a local framing/routing
 * outcome with no numbered wire-error-code counterpart. */
static void test_out_wire_error_is_none_for_local_only_codes(void)
{
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE, (int)rcp_ep_pwm_out_wire_error(RCP_EP_PWM_OUT_OK));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_pwm_out_wire_error(RCP_EP_PWM_OUT_ERR_SHORT_FRAME));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_pwm_out_wire_error(RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_pwm_out_wire_error(RCP_EP_PWM_OUT_ERR_WRONG_BUS));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_pwm_out_wire_error(RCP_EP_PWM_OUT_ERR_WRONG_OP));
}

/* ── PWM_OUT: read request ─────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-025
static void test_out_read_request_round_trip(void)
{
    rcp_bytes_t            frame = rcp_ep_pwm_out_encode_read_request(3, 7);
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t  rc;

    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 3, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(7, out_tn);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-062
static void test_out_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t           frame = rcp_ep_pwm_out_encode_read_request(3, 7);
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 4, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_BUS, rc);
    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-063
static void test_out_read_request_rejects_wrong_op(void)
{
    rcp_ep_pwm_value_t    value = {1, 2};
    rcp_bytes_t           frame = rcp_ep_pwm_out_encode_write_request(3, value, RCP_EP_PWM_OUT_WRITE_REPLACE, 1);
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 3, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_OP, rc);
    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-026
static void test_out_read_request_rejects_short_frame(void)
{
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(NULL, 0, 3, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_SHORT_FRAME, rc);
}

/* Split 2026-08-18 (c-RCP-18-tracker, REQ-PWM-* atomicity audit, issue
 * #533): REQ-PWM-026 previously bundled this BAD_MSG_TYPE clause with
 * SHORT_FRAME/WRONG_BUS/WRONG_OP under one id; no dedicated test existed
 * for it before this split -- encoding a GBB frame (rcp_acf_encode_gbb())
 * and decoding it as an ABB read request is a non-ACF_ABB frame from
 * rcp_acf_decode_abb()'s own point of view, mirroring ep_gpio.c's
 * identical test_read_request_rejects_bad_msg_type(). */
//cfusa:test REQ-PWM-061
static void test_out_read_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t  gbb_hdr = {0};
    rcp_bytes_t           frame;
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc;

    gbb_hdr.info.byte_bus_id = 3;
    gbb_hdr.info.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 3, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE, rc);

    rcp_bytes_free(&frame);
}

/* ── PWM_OUT: write request ────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-027
static void test_out_write_request_round_trip(void)
{
    rcp_ep_pwm_value_t    value = {1000, 500};
    rcp_bytes_t            frame;
    rcp_ep_pwm_value_t    out_value;
    uint8_t                out_evt;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    frame = rcp_ep_pwm_out_encode_write_request(5, value, RCP_EP_PWM_OUT_WRITE_OR, 42);
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 5, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(1000, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(500, out_value.active_duration);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_WRITE_OR, out_evt);
    TEST_ASSERT_EQUAL_UINT8(42, out_tn);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-028
static void test_out_write_request_rejects_bad_payload_len(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                      bad_payload[3] = {1, 2, 3};
    rcp_bytes_t                  frame;
    rcp_ep_pwm_value_t          out_value;
    uint8_t                      out_evt;
    uint8_t                      out_tn;
    rcp_ep_pwm_out_errc_t       rc;

    hdr.byte_bus_id = 5;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, bad_payload, sizeof(bad_payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 5, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN, rc);

    rcp_bytes_free(&frame);
}

static void test_out_write_request_rejects_wrong_bus(void)
{
    rcp_ep_pwm_value_t    value = {1, 2};
    rcp_bytes_t            frame = rcp_ep_pwm_out_encode_write_request(5, value, RCP_EP_PWM_OUT_WRITE_REPLACE, 1);
    rcp_ep_pwm_value_t    out_value;
    uint8_t                out_evt;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 6, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_BUS, rc);

    rcp_bytes_free(&frame);
}

/* Split 2026-08-18 (c-RCP-18-tracker, REQ-PWM-* atomicity audit, issue
 * #533): REQ-PWM-028 previously bundled this WRONG_OP clause with
 * BAD_PAYLOAD_LEN under one id; no dedicated test existed for it before
 * this split. */
//cfusa:test REQ-PWM-064
static void test_out_write_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_pwm_value_t          out_value;
    uint8_t                     out_evt;
    uint8_t                     out_tn;
    rcp_ep_pwm_out_errc_t       rc;

    hdr.byte_bus_id = 5;
    hdr.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 5, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_OP, rc);

    rcp_bytes_free(&frame);
}

/* FIXED 2026-08-14 (issue #426, REQ-PWM-008): TC18 §13.5 Table 33's
 * GPIO/PWM_OUT row's two-part rule for evt[2:0]=100b
 * (RCP_EP_PWM_OUT_WRITE_RESERVED4): "reserved -- request shall be
 * ignored and an err-response with error code = UNSUPPORTED_CMD shall be
 * sent". Both halves exercised here: rcp_ep_pwm_out_apply_write() still
 * ignores the request (returns current unchanged), and
 * rcp_ep_pwm_out_decode_write_request() now returns the dedicated
 * RCP_EP_PWM_OUT_ERR_RESERVED_EVT for that same evt value. */
//cfusa:test REQ-PWM-008
static void test_out_write_request_rejects_reserved_evt(void)
{
    rcp_ep_pwm_value_t    current = {1234, 567};
    rcp_ep_pwm_value_t    request = {999, 888};
    rcp_ep_pwm_value_t    result;
    rcp_bytes_t            frame;
    rcp_ep_pwm_value_t    out_value = {0xAAAAu, 0xAAAAu};
    uint8_t                out_evt = 0xFFu;
    uint8_t                out_tn = 0xFFu;
    rcp_ep_pwm_out_errc_t rc;

    /* The "ignored" half. */
    result = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_RESERVED4, 0, 0xFFFFu);
    TEST_ASSERT_EQUAL_UINT16(current.period, result.period);
    TEST_ASSERT_EQUAL_UINT16(current.active_duration, result.active_duration);

    /* The "err-response" half. */
    frame = rcp_ep_pwm_out_encode_write_request(5, request, RCP_EP_PWM_OUT_WRITE_RESERVED4, 0x33u);
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 5, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_RESERVED_EVT, rc);
    /* Outputs are untouched on this error path. */
    TEST_ASSERT_EQUAL_UINT16(0xAAAAu, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(0xAAAAu, out_value.active_duration);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out_evt);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, out_tn);

    rcp_bytes_free(&frame);
}

/* ── PWM_OUT: response ─────────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-029
static void test_out_response_round_trip_untimed(void)
{
    rcp_ep_pwm_value_t    value = {2000, 1000};
    rcp_bytes_t            frame;
    rcp_ep_pwm_value_t    out_value;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    frame = rcp_ep_pwm_out_encode_response(9, value, 11, false, 0);
    rc = rcp_ep_pwm_out_decode_response(frame.data, frame.len, 9, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(2000, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(1000, out_value.active_duration);
    TEST_ASSERT_FALSE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(0, out_ts);
    TEST_ASSERT_EQUAL_UINT8(11, out_tn);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-030
static void test_out_response_round_trip_timed(void)
{
    rcp_ep_pwm_value_t    value = {3000, 1500};
    rcp_bytes_t            frame;
    rcp_ep_pwm_value_t    out_value;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    frame = rcp_ep_pwm_out_encode_response(9, value, 12, true, 123456789ULL);
    rc = rcp_ep_pwm_out_decode_response(frame.data, frame.len, 9, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_TRUE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(123456789ULL, out_ts);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-031
static void test_out_response_decode_rejects_short_frame(void)
{
    rcp_ep_pwm_value_t    out_value;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_response(NULL, 0, 9, &out_value, &out_timed,
                                                                &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_SHORT_FRAME, rc);
}

/* Split 2026-08-18 (c-RCP-18-tracker, REQ-PWM-* atomicity audit, issue
 * #533): REQ-PWM-031 previously bundled this WRONG_BUS clause with
 * SHORT_FRAME under one id. No dedicated test existed for it before this
 * split -- the PWM_IN sibling (REQ-PWM-046) already had one
 * (test_in_response_decode_rejects_wrong_bus below); PWM_OUT's own
 * decode_response never had the equivalent, exactly the silent-gap risk
 * this audit exists to close. */
//cfusa:test REQ-PWM-065
static void test_out_response_decode_rejects_wrong_bus(void)
{
    rcp_ep_pwm_value_t    value = {1, 1};
    rcp_bytes_t            frame = rcp_ep_pwm_out_encode_response(9, value, 1, false, 0);
    rcp_ep_pwm_value_t    out_value;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    rc = rcp_ep_pwm_out_decode_response(frame.data, frame.len, 10, &out_value, &out_timed,
                                         &out_ts, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_BUS, rc);

    rcp_bytes_free(&frame);
}

/* ── PWM_IN: triggers ───────────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-032
static void test_in_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_NONE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_NONE, true, false));
}

//cfusa:test REQ-PWM-033
static void test_in_trigger_rising(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, true));
    /* Added 2026-08-18 (MC/DC gap closure, L638:C48 index 1): `!prev_level
     * && new_level` -- every prior case with !prev_level true (prev_level
     * == false) also had new_level == true, so new_level's own
     * contribution (independent of !prev_level) was never demonstrated.
     * This holds !prev_level fixed true (prev_level == false) and flips
     * new_level to false, flipping the outcome to false too. */
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, false, false));
}

//cfusa:test REQ-PWM-034
static void test_in_trigger_falling(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, false, false));
    /* Added 2026-08-18 (MC/DC gap closure, L639:C48 index 1): `prev_level &&
     * !new_level` -- the mirror image of the RISING gap above. Holds
     * prev_level fixed true and flips new_level to true (so !new_level
     * flips to false), flipping the outcome to false. */
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, true, true));
}

/* ── PWM_IN: functional config ─────────────────────────────────────────────── */

//cfusa:test REQ-PWM-035
static void test_in_functional_cfg_init_zeroes(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.base_clk);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.clk_divider);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.flags);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.max_period);
}

//cfusa:test REQ-PWM-036
static void test_in_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

//cfusa:test REQ-PWM-037
static void test_in_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream(void)
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
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

//cfusa:test REQ-PWM-038
static void test_in_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t unauth = {0};
    rcp_lifecycle_writer_ctx_t auth   = {0};

    auth.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, unauth));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, auth));
}

//cfusa:test REQ-PWM-039
static void test_in_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_set_trigger(&cfg, RCP_EP_PWM_IN_TRIGGER_RISING,
                                                 RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
}

//cfusa:test REQ-PWM-040
static void test_in_set_trigger_applies_when_authorized(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_set_trigger(&cfg, RCP_EP_PWM_IN_TRIGGER_FALLING,
                                                RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_FALLING, cfg.trigger);
}

/* ── PWM_IN: error codes ───────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-041
static void test_in_strerror_never_null_and_distinct(void)
{
    rcp_ep_pwm_in_errc_t codes[] = {
        RCP_EP_PWM_IN_OK,               RCP_EP_PWM_IN_ERR_SHORT_FRAME,
        RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE, RCP_EP_PWM_IN_ERR_WRONG_BUS,
        RCP_EP_PWM_IN_ERR_WRONG_OP,     RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN,
        RCP_EP_PWM_IN_ERR_BAD_EVT,
    };
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_pwm_in_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_pwm_in_strerror(codes[j])));
        }
    }
}

/* ── PWM_IN: read request ──────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-042
static void test_in_read_request_round_trip(void)
{
    rcp_bytes_t           frame = rcp_ep_pwm_in_encode_read_request(2, 5);
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t  rc;

    rc = rcp_ep_pwm_in_decode_read_request(frame.data, frame.len, 2, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(5, out_tn);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-043
static void test_in_read_request_rejects_short_frame(void)
{
    uint8_t              out_tn;
    rcp_ep_pwm_in_errc_t rc = rcp_ep_pwm_in_decode_read_request(NULL, 0, 2, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_ERR_SHORT_FRAME, rc);
}

/* REQ-PWM-043: rcp_ep_pwm_in_decode_read_request()'s own WRONG_BUS
 * clause -- test_in_response_decode_rejects_wrong_bus() above proves
 * this for _decode_response(), a different function; the read-request
 * decoder's own byte_bus_id check was never separately exercised. */
//cfusa:test REQ-PWM-066
static void test_in_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t           frame = rcp_ep_pwm_in_encode_read_request(2, 5);
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t  rc;

    rc = rcp_ep_pwm_in_decode_read_request(frame.data, frame.len, 4, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_ERR_WRONG_BUS, rc);

    rcp_bytes_free(&frame);
}

/* REQ-PWM-059 (FIXED 2026-08-11, issue #256 Group I): before this fix,
 * rcp_ep_pwm_in_decode_read_request() never checked evt[2:0] at all, so a
 * real evt=111b configuration-write request from a conforming peer would
 * have been silently misinterpreted as an ordinary read. See the file
 * header. */
//cfusa:test REQ-PWM-059
static void test_in_read_request_rejects_bad_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     out_tn;
    rcp_ep_pwm_in_errc_t        rc;

    hdr.byte_bus_id     = 2;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.evt             = 0x7u; /* the reconfig escape hatch, not a plain read */
    hdr.transaction_num = 5;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_in_decode_read_request(frame.data, frame.len, 2, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_ERR_BAD_EVT, rc);

    rcp_bytes_free(&frame);
}

/* ── PWM_IN: the EP_func register block ──────────────────────────────────── */

//cfusa:test REQ-PWM-058
static void test_in_render_registers_matches_table_offsets(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    uint8_t                        out[RCP_EP_PWM_IN_EP_FUNC_LEN];

    rcp_ep_pwm_in_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status         = 0x1234;
    cfg.clk_divider       = 0x55;
    cfg.flags             = RCP_EP_PWM_IN_FLAG_POLARITY | RCP_EP_PWM_IN_FLAG_ERR_ON_MAX_PERIOD;
    cfg.max_period         = 0xABCD;

    rcp_ep_pwm_in_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_EP_FUNC_LEN, out[RCP_EP_PWM_IN_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_RESERVED_01]);
    TEST_ASSERT_TRUE((out[RCP_EP_PWM_IN_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_BASE_CLK]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_BASE_CLK + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_PWM_IN_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_PWM_IN_REG_EP_STATUS + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x55, out[RCP_EP_PWM_IN_REG_CLK_DIVIDER]);
    TEST_ASSERT_EQUAL_UINT8(cfg.flags, out[RCP_EP_PWM_IN_REG_FLAGS]);
    TEST_ASSERT_EQUAL_UINT8(0xABu, out[RCP_EP_PWM_IN_REG_MAX_PERIOD]);
    TEST_ASSERT_EQUAL_UINT8(0xCDu, out[RCP_EP_PWM_IN_REG_MAX_PERIOD + 1]);

    TEST_ASSERT_EQUAL_UINT16(0x000Cu, RCP_EP_PWM_IN_EP_FUNC_LEN);
}

//cfusa:test REQ-PWM-058
static void test_in_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    uint8_t                        payload[2 + 6];

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_PWM_IN_REG_EP_STATUS;
    payload[2] = 0xAB; payload[3] = 0xCD; /* ep_status */
    payload[4] = 0x11;                    /* clk_divider */
    payload[5] = RCP_EP_PWM_IN_FLAG_CONTINUOUS_MODE; /* flags */
    payload[6] = 0x22; payload[7] = 0x33; /* max_period */

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_RECONFIG_OK,
        rcp_ep_pwm_in_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0x11, cfg.clk_divider);
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_IN_FLAG_CONTINUOUS_MODE, cfg.flags);
    TEST_ASSERT_EQUAL_UINT16(0x2233, cfg.max_period);
}

//cfusa:test REQ-PWM-058
static void test_in_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    uint8_t                        payload[2 + 4];

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00), the reserved octet (0x01), and both octets of
     * base_clk (0x04-0x05) -- all read-only. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;
    payload[4] = 0xFF;
    payload[5] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_RECONFIG_OK,
        rcp_ep_pwm_in_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_PWM_IN_EP_FUNC_LEN];

        rcp_ep_pwm_in_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_EP_FUNC_LEN, out[RCP_EP_PWM_IN_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_RESERVED_01]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_BASE_CLK]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_BASE_CLK + 1]);
    }
}

/* Added 2026-08-18 (MC/DC gap closure, L738:C12 indices 2 and 3): despite
 * its comment, test_in_apply_reconfig_ignores_read_only_registers above
 * only ever reaches offsets 0x00-0x03 (start=0x00, 4 data bytes) -- it
 * never actually reaches 0x04 or 0x05, so pwm_in_reg_offset_read_only()'s
 * `addr == BASE_CLK` and `addr == BASE_CLK+1` arms are never independently
 * exercised. This test targets 0x04-0x05 specifically. */
//cfusa:test REQ-PWM-058
static void test_in_apply_reconfig_ignores_base_clk_octets(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    /* start = 0x04 (base_clk lo), 2 data bytes -> covers 0x04 and 0x05. */
    const uint8_t                  payload[4] = {0x00, 0x04, 0xFFu, 0xFFu};

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_RECONFIG_OK,
        rcp_ep_pwm_in_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_PWM_IN_EP_FUNC_LEN];

        rcp_ep_pwm_in_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_BASE_CLK]);     /* untouched */
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_PWM_IN_REG_BASE_CLK + 1]); /* untouched */
    }
}

//cfusa:test REQ-PWM-071
static void test_in_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    uint8_t                        payload[3];

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x0C; /* == RCP_EP_PWM_IN_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_pwm_in_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.max_period);
}

//cfusa:test REQ-PWM-070
static void test_in_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    uint8_t                        addr_only[2] = {0x00, 0x08};

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_RECONFIG_ERR_SHORT,
        rcp_ep_pwm_in_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_RECONFIG_ERR_SHORT,
        rcp_ep_pwm_in_apply_reconfig(&cfg, NULL, 0));
}

//cfusa:test REQ-PWM-058
static void test_in_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_pwm_in_encode_reconfig_request(0x03, 0x0006, data, sizeof(data), 7);
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

static void test_in_encode_reconfig_request_rejects_empty_data(void)
{
    rcp_bytes_t frame = rcp_ep_pwm_in_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

/* Added 2026-08-18 (MC/DC gap closure, L809:C9 index 1): the case above
 * passes data_len == 0 AND data == NULL together, so `data == NULL` is
 * short-circuit-masked and never independently evaluated with data_len
 * fixed nonzero. Combined with the successful round-trip call above
 * (nonzero length, real buffer -> false), this nonzero-length/NULL-data
 * call gives `data == NULL` its own independent true/false swing. */
//cfusa:test REQ-PWM-058
static void test_in_encode_reconfig_request_rejects_null_data_with_nonzero_length(void)
{
    rcp_bytes_t frame = rcp_ep_pwm_in_encode_reconfig_request(0x03, 0x0006, NULL, 2u, 7u);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-PWM-058
static void test_in_reconfig_strerror_never_null(void)
{
    rcp_ep_pwm_in_reconfig_errc_t codes[] = {
        RCP_EP_PWM_IN_RECONFIG_OK, RCP_EP_PWM_IN_RECONFIG_ERR_SHORT,
        RCP_EP_PWM_IN_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_pwm_in_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_in_reconfig_strerror((rcp_ep_pwm_in_reconfig_errc_t)99));
}

/* ── PWM_IN: response ──────────────────────────────────────────────────────── */

//cfusa:test REQ-PWM-044
static void test_in_response_round_trip_untimed(void)
{
    rcp_ep_pwm_value_t   value = {4000, 2000};
    rcp_bytes_t           frame;
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    frame = rcp_ep_pwm_in_encode_response(1, value, 3, false, 0);
    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 1, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(4000, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(2000, out_value.active_duration);
    TEST_ASSERT_FALSE(out_timed);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-045
static void test_in_response_round_trip_timed(void)
{
    rcp_ep_pwm_value_t   value = {5000, 2500};
    rcp_bytes_t           frame;
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    frame = rcp_ep_pwm_in_encode_response(1, value, 4, true, 555ULL);
    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 1, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_TRUE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(555ULL, out_ts);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-046
static void test_in_response_decode_rejects_wrong_bus(void)
{
    rcp_ep_pwm_value_t   value = {1, 1};
    rcp_bytes_t           frame = rcp_ep_pwm_in_encode_response(1, value, 1, false, 0);
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 2, &out_value, &out_timed, &out_ts, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_ERR_WRONG_BUS, rc);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-PWM-047
static void test_in_response_no_signal_sentinel_round_trips(void)
{
    rcp_ep_pwm_value_t   value = {RCP_EP_PWM_IN_NO_SIGNAL, RCP_EP_PWM_IN_NO_SIGNAL};
    rcp_bytes_t           frame;
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    frame = rcp_ep_pwm_in_encode_response(1, value, 9, false, 0);
    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 1, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, out_value.active_duration);

    rcp_bytes_free(&frame);
}

/* ── PWM_IN: MAX_PERIOD timeout classification (REQ-PWM-058 remainder) ──────── */

/* ADDED 2026-08-14 (issue #428, REQ-PWM-058): a measured period at or
 * below max_period is never a timeout, regardless of err_on_max_period or
 * resp_on_err_enabled. */
//cfusa:test REQ-PWM-072
static void test_in_max_period_outcome_not_exceeded_is_ok(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_MAX_PERIOD_OK,
                      rcp_ep_pwm_in_max_period_outcome(100u, 200u, true, true));
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_MAX_PERIOD_OK,
                      rcp_ep_pwm_in_max_period_outcome(200u, 200u, false, false));
}

/* Table 48's 0b row: "if MAX PERIOD is exceeded, invalidate measurement
 * and wait for new active phase of signal" -- never an error, regardless
 * of resp_on_err_enabled. */
//cfusa:test REQ-PWM-073
static void test_in_max_period_outcome_bit_clear_invalidates_never_errors(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_MAX_PERIOD_INVALIDATE,
                      rcp_ep_pwm_in_max_period_outcome(201u, 200u, false, true));
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_MAX_PERIOD_INVALIDATE,
                      rcp_ep_pwm_in_max_period_outcome(201u, 200u, false, false));
}

/* Table 48's 1b row: "if MAX_PERIOD is exceeded stop measurement and
 * signal error if error response is enabled in EP_config" -- stop always
 * happens; the error signal is conditional on resp_on_err_enabled. Split
 * 2026-08-18 (c-RCP-18-tracker, REQ-PWM-* atomicity audit, issue #533)
 * into two separate test functions -- one per REQ-PWM-074/-075's own
 * split id -- so each split id has its own distinct, independently
 * mutation-testable assertion rather than sharing one test function's
 * two TEST_ASSERT_EQUAL() calls. */
//cfusa:test REQ-PWM-074
static void test_in_max_period_outcome_stop_without_error(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_MAX_PERIOD_STOP,
                      rcp_ep_pwm_in_max_period_outcome(201u, 200u, true, false));
}

//cfusa:test REQ-PWM-075
static void test_in_max_period_outcome_stop_and_error(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_MAX_PERIOD_STOP_AND_ERROR,
                      rcp_ep_pwm_in_max_period_outcome(201u, 200u, true, true));
}

/* REQ-WIREERR-007 (issue #163): STOP_AND_ERROR is the only outcome
 * Table 48's own row ties to "signal error" -- the numbered wire code is
 * PWM_IN's own RCP_ERROR_PWM_IN_NO_SIGNAL (9), the only Table 30 entry
 * naming this endpoint type specifically. */
//cfusa:test REQ-WIREERR-007
static void test_in_wire_error_maps_stop_and_error_to_pwm_in_no_signal(void)
{
    const int wire_code = (int)rcp_ep_pwm_in_wire_error(RCP_EP_PWM_IN_MAX_PERIOD_STOP_AND_ERROR);

    TEST_ASSERT_EQUAL_INT(9, wire_code);
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_PWM_IN_NO_SIGNAL, wire_code);
}

/* OK/INVALIDATE/STOP each explicitly signal no error of their own -- see
 * rcp_ep_pwm_in_max_period_outcome()'s own doc comment. */
static void test_in_wire_error_is_none_for_the_non_error_outcomes(void)
{
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_pwm_in_wire_error(RCP_EP_PWM_IN_MAX_PERIOD_OK));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_pwm_in_wire_error(RCP_EP_PWM_IN_MAX_PERIOD_INVALIDATE));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_pwm_in_wire_error(RCP_EP_PWM_IN_MAX_PERIOD_STOP));
}

/* End-to-end: whatever rcp_ep_pwm_in_max_period_outcome() itself decides,
 * for a range of inputs, rcp_ep_pwm_in_wire_error() of that decision is
 * always the correct numbered code. */
static void test_in_wire_error_matches_max_period_outcome_across_inputs(void)
{
    TEST_ASSERT_EQUAL_INT(
        (int)RCP_ERROR_PWM_IN_NO_SIGNAL,
        (int)rcp_ep_pwm_in_wire_error(rcp_ep_pwm_in_max_period_outcome(201u, 200u, true, true)));
    TEST_ASSERT_EQUAL_INT(
        (int)RCP_ERROR_NONE,
        (int)rcp_ep_pwm_in_wire_error(rcp_ep_pwm_in_max_period_outcome(201u, 200u, true, false)));
    TEST_ASSERT_EQUAL_INT(
        (int)RCP_ERROR_NONE,
        (int)rcp_ep_pwm_in_wire_error(rcp_ep_pwm_in_max_period_outcome(100u, 200u, true, true)));
}

/* ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ────────────── */

//cfusa:test REQ-PWM-048
static void test_compound_wait_mode_valid_accepts_exactly_4_to_7(void)
{
    uint8_t v;

    for (v = 0; v <= 3; v++) {
        TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_mode_valid(v));
    }
    for (v = 4; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_mode_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_mode_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_mode_valid(255));
}

/* TC18 §13.5.1: evt=100b ("GE") is met when byte_msg_payload (threshold)
 * >= current interface status (captured), i.e. captured <= threshold.
 * Corrected 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group B) -- this test
 * previously pinned the reverse (captured >= threshold). */
//cfusa:test REQ-PWM-049
static void test_compound_wait_period_ge(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 1000));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 1001));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 999));
}

/* evt=101b ("LE"): threshold <= captured, i.e. captured >= threshold. */
//cfusa:test REQ-PWM-050
static void test_compound_wait_period_le(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 1000));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 999));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 1001));
}

/* evt=110b ("GE"), duty-cycle sub-field: captured <= threshold. */
//cfusa:test REQ-PWM-051
static void test_compound_wait_duty_ge(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 500));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 501));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 499));
}

/* evt=111b ("LE"), duty-cycle sub-field: captured >= threshold. */
//cfusa:test REQ-PWM-052
static void test_compound_wait_duty_le(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 500));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 499));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 501));
}

//cfusa:test REQ-PWM-053
static void test_compound_wait_invalid_mode_returns_false(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, (rcp_ep_pwm_in_compound_wait_mode_t)0, 0));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, (rcp_ep_pwm_in_compound_wait_mode_t)8, 0));
}

//cfusa:test REQ-PWM-054
static void test_compound_wait_no_signal_never_matches(void)
{
    rcp_ep_pwm_value_t captured = {RCP_EP_PWM_IN_NO_SIGNAL, RCP_EP_PWM_IN_NO_SIGNAL};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 0));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 0xFFFFu));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 0));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 0xFFFFu));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_out_write_semantics_valid);
    RUN_TEST(test_out_apply_write_replace);
    RUN_TEST(test_out_apply_write_or);
    RUN_TEST(test_out_apply_write_and);
    RUN_TEST(test_out_apply_write_xor);
    RUN_TEST(test_out_apply_write_add_saturates);
    RUN_TEST(test_out_apply_write_sub_is_request_minus_current_and_saturates);
    RUN_TEST(test_out_apply_write_sub_operand_order_is_observable);
    RUN_TEST(test_out_apply_write_reserved4_is_noop);
    RUN_TEST(test_out_apply_write_wire_value_4_is_reserved_noop);
    RUN_TEST(test_out_apply_write_wire_value_5_is_add);
    RUN_TEST(test_out_apply_write_wire_value_6_is_sub);
    RUN_TEST(test_out_apply_write_reconfig_misrouted_is_noop);
    RUN_TEST(test_out_apply_reconfig_writes_clk_divider);
    RUN_TEST(test_out_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_out_apply_reconfig_writes_enable_bit);
    RUN_TEST(test_out_apply_reconfig_partial_multi_octet_register);
    RUN_TEST(test_out_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_out_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_out_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_out_encode_reconfig_request_rejects_zero_length_data);
    RUN_TEST(test_out_encode_reconfig_request_rejects_null_data);
    RUN_TEST(test_out_reconfig_request_round_trip);
    RUN_TEST(test_out_render_registers_matches_table_offsets);
    RUN_TEST(test_out_reconfig_strerror_never_null);

    RUN_TEST(test_out_trigger_none_never_fires);
    RUN_TEST(test_out_trigger_cycle_start);
    RUN_TEST(test_out_trigger_mid_pulse);
    RUN_TEST(test_out_trigger_done);

    RUN_TEST(test_out_functional_cfg_init_zeroes);
    RUN_TEST(test_out_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_out_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_out_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_out_set_trigger_rejects_unauthorized);
    RUN_TEST(test_out_set_trigger_applies_when_authorized);
    RUN_TEST(test_out_set_enabled_rejects_unauthorized);
    RUN_TEST(test_out_set_enabled_applies_when_authorized);

    RUN_TEST(test_out_strerror_never_null_and_distinct);
    RUN_TEST(test_out_wire_error_maps_bad_payload_len_to_invalid_parameter);
    RUN_TEST(test_out_wire_error_maps_reserved_evt_to_unsupported_cmd);
    RUN_TEST(test_out_wire_error_is_none_for_local_only_codes);

    RUN_TEST(test_out_read_request_round_trip);
    RUN_TEST(test_out_read_request_rejects_wrong_bus);
    RUN_TEST(test_out_read_request_rejects_wrong_op);
    RUN_TEST(test_out_read_request_rejects_short_frame);
    RUN_TEST(test_out_read_request_rejects_bad_msg_type);

    RUN_TEST(test_out_write_request_round_trip);
    RUN_TEST(test_out_write_request_rejects_bad_payload_len);
    RUN_TEST(test_out_write_request_rejects_wrong_bus);
    RUN_TEST(test_out_write_request_rejects_wrong_op);
    RUN_TEST(test_out_write_request_rejects_reserved_evt);

    RUN_TEST(test_out_response_round_trip_untimed);
    RUN_TEST(test_out_response_round_trip_timed);
    RUN_TEST(test_out_response_decode_rejects_short_frame);
    RUN_TEST(test_out_response_decode_rejects_wrong_bus);

    RUN_TEST(test_in_trigger_none_never_fires);
    RUN_TEST(test_in_trigger_rising);
    RUN_TEST(test_in_trigger_falling);

    RUN_TEST(test_in_functional_cfg_init_zeroes);
    RUN_TEST(test_in_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_in_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_in_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_in_set_trigger_rejects_unauthorized);
    RUN_TEST(test_in_set_trigger_applies_when_authorized);

    RUN_TEST(test_in_strerror_never_null_and_distinct);

    RUN_TEST(test_in_read_request_round_trip);
    RUN_TEST(test_in_read_request_rejects_short_frame);
    RUN_TEST(test_in_read_request_rejects_wrong_bus);
    RUN_TEST(test_in_read_request_rejects_bad_evt);

    RUN_TEST(test_in_render_registers_matches_table_offsets);
    RUN_TEST(test_in_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_in_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_in_apply_reconfig_ignores_base_clk_octets);
    RUN_TEST(test_in_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_in_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_in_reconfig_request_round_trip);
    RUN_TEST(test_in_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_in_encode_reconfig_request_rejects_null_data_with_nonzero_length);
    RUN_TEST(test_in_reconfig_strerror_never_null);

    RUN_TEST(test_in_response_round_trip_untimed);
    RUN_TEST(test_in_response_round_trip_timed);
    RUN_TEST(test_in_response_decode_rejects_wrong_bus);
    RUN_TEST(test_in_response_no_signal_sentinel_round_trips);

    RUN_TEST(test_in_max_period_outcome_not_exceeded_is_ok);
    RUN_TEST(test_in_max_period_outcome_bit_clear_invalidates_never_errors);
    RUN_TEST(test_in_max_period_outcome_stop_without_error);
    RUN_TEST(test_in_max_period_outcome_stop_and_error);
    RUN_TEST(test_in_wire_error_maps_stop_and_error_to_pwm_in_no_signal);
    RUN_TEST(test_in_wire_error_is_none_for_the_non_error_outcomes);
    RUN_TEST(test_in_wire_error_matches_max_period_outcome_across_inputs);

    RUN_TEST(test_compound_wait_mode_valid_accepts_exactly_4_to_7);
    RUN_TEST(test_compound_wait_period_ge);
    RUN_TEST(test_compound_wait_period_le);
    RUN_TEST(test_compound_wait_duty_ge);
    RUN_TEST(test_compound_wait_duty_le);
    RUN_TEST(test_compound_wait_invalid_mode_returns_false);
    RUN_TEST(test_compound_wait_no_signal_never_matches);

    return UNITY_END();
}
