/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-PWM-001
//cfusa:test REQ-PWM-002
//cfusa:test REQ-PWM-003
//cfusa:test REQ-PWM-004
//cfusa:test REQ-PWM-005
//cfusa:test REQ-PWM-006
//cfusa:test REQ-PWM-007
//cfusa:test REQ-PWM-008
//cfusa:test REQ-PWM-009
//cfusa:test REQ-PWM-010
//cfusa:test REQ-PWM-011
//cfusa:test REQ-PWM-012
//cfusa:test REQ-PWM-013
//cfusa:test REQ-PWM-014
//cfusa:test REQ-PWM-015
//cfusa:test REQ-PWM-016
//cfusa:test REQ-PWM-017
//cfusa:test REQ-PWM-018
//cfusa:test REQ-PWM-019
//cfusa:test REQ-PWM-020
//cfusa:test REQ-PWM-021
//cfusa:test REQ-PWM-022
//cfusa:test REQ-PWM-023
//cfusa:test REQ-PWM-024
//cfusa:test REQ-PWM-025
//cfusa:test REQ-PWM-026
//cfusa:test REQ-PWM-027
//cfusa:test REQ-PWM-028
//cfusa:test REQ-PWM-029
//cfusa:test REQ-PWM-030
//cfusa:test REQ-PWM-031
//cfusa:test REQ-PWM-032
//cfusa:test REQ-PWM-033
//cfusa:test REQ-PWM-034
//cfusa:test REQ-PWM-035
//cfusa:test REQ-PWM-036
//cfusa:test REQ-PWM-037
//cfusa:test REQ-PWM-038
//cfusa:test REQ-PWM-039
//cfusa:test REQ-PWM-040
//cfusa:test REQ-PWM-041
//cfusa:test REQ-PWM-042
//cfusa:test REQ-PWM-043
//cfusa:test REQ-PWM-044
//cfusa:test REQ-PWM-045
//cfusa:test REQ-PWM-046
//cfusa:test REQ-PWM-047
//cfusa:test REQ-PWM-048
//cfusa:test REQ-PWM-049
//cfusa:test REQ-PWM-050
//cfusa:test REQ-PWM-051
//cfusa:test REQ-PWM-052
//cfusa:test REQ-PWM-053
//cfusa:test REQ-PWM-054
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

static void test_out_write_semantics_valid(void)
{
    uint8_t v;

    for (v = 0; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_pwm_out_write_semantics_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_write_semantics_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_write_semantics_valid(255));
}

static void test_out_apply_write_replace(void)
{
    rcp_ep_pwm_value_t current = {100, 50};
    rcp_ep_pwm_value_t request = {200, 75};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_REPLACE);

    TEST_ASSERT_EQUAL_UINT16(200, result.period);
    TEST_ASSERT_EQUAL_UINT16(75, result.active_duration);
}

static void test_out_apply_write_or(void)
{
    rcp_ep_pwm_value_t current = {0x00F0, 0x0F00};
    rcp_ep_pwm_value_t request = {0x000F, 0x00F0};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_OR);

    TEST_ASSERT_EQUAL_UINT16(0x00FF, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

static void test_out_apply_write_and(void)
{
    rcp_ep_pwm_value_t current = {0x00FF, 0x0FF0};
    rcp_ep_pwm_value_t request = {0x000F, 0x0FF0};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_AND);

    TEST_ASSERT_EQUAL_UINT16(0x000F, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

static void test_out_apply_write_xor(void)
{
    rcp_ep_pwm_value_t current = {0x00FF, 0x0F0F};
    rcp_ep_pwm_value_t request = {0x000F, 0x00FF};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_XOR);

    TEST_ASSERT_EQUAL_UINT16(0x00F0, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

static void test_out_apply_write_add_saturates(void)
{
    rcp_ep_pwm_value_t current = {0xFFF0, 100};
    rcp_ep_pwm_value_t request = {0x0020, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_ADD);

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
static void test_out_apply_write_sub_is_request_minus_current_and_saturates(void)
{
    rcp_ep_pwm_value_t current = {10, 200};
    rcp_ep_pwm_value_t request = {50, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_SUB);

    TEST_ASSERT_EQUAL_UINT16(40, result.period);
    TEST_ASSERT_EQUAL_UINT16(0, result.active_duration); /* saturates at 0x0000, no wrap */
}

/* Companion to the above: the operand order is directly observable
 * because subtraction is not commutative. Table 30's "payload minus
 * current" with payload=0x0100 and current=0x0001 must be 0x00FF; the
 * inverted order would saturate to 0x0000 instead. */
static void test_out_apply_write_sub_operand_order_is_observable(void)
{
    rcp_ep_pwm_value_t current = {0x0001, 0x0001};
    rcp_ep_pwm_value_t request = {0x0100, 0x0100};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_SUB);

    TEST_ASSERT_EQUAL_UINT16(0x00FF, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x00FF, result.active_duration);
}

static void test_out_apply_write_reserved4_is_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              RCP_EP_PWM_OUT_WRITE_RESERVED4);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

/* Regression test for issue #104: raw wire evt[2:0] values 4/5/6 must map
 * to Reserved/ADD/SUB respectively (not the previous off-by-one ADD/SUB/
 * Reserved mapping). Exercises rcp_ep_pwm_out_apply_write() with the raw
 * wire-value enum casts a decoder would actually produce, not just the
 * named constants. */
static void test_out_apply_write_wire_value_4_is_reserved_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)4u);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

static void test_out_apply_write_wire_value_5_is_add(void)
{
    rcp_ep_pwm_value_t current = {10, 200};
    rcp_ep_pwm_value_t request = {20, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)5u);

    TEST_ASSERT_EQUAL_UINT16(30, result.period);
    TEST_ASSERT_EQUAL_UINT16(250, result.active_duration);
}

/* Raw wire evt[2:0] == 6 is the Table 30 subtract row, and it computes
 * payload - current (see test_out_apply_write_sub_is_request_minus_current
 * _and_saturates() for the cited text): current={30,250},
 * request={100,300} yields 70 and 50. */
static void test_out_apply_write_wire_value_6_is_sub(void)
{
    rcp_ep_pwm_value_t current = {30, 250};
    rcp_ep_pwm_value_t request = {100, 300};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)6u);

    TEST_ASSERT_EQUAL_UINT16(70, result.period);
    TEST_ASSERT_EQUAL_UINT16(50, result.active_duration);
}

static void test_out_apply_write_reconfig_misrouted_is_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              RCP_EP_PWM_OUT_WRITE_RECONFIG);

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

/* Figure 18 shows the configuration request as an ordinary ACF_ABB write
 * with evt[2:0] = 111b, address prefix then data. Round-trip: encode,
 * decode as a write request, route the payload to apply_reconfig(). */
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

static void test_out_reconfig_strerror_never_null(void)
{
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_reconfig_strerror(RCP_EP_PWM_OUT_RECONFIG_OK));
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_reconfig_strerror(RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT));
    TEST_ASSERT_NOT_NULL(
        rcp_ep_pwm_out_reconfig_strerror(RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE));
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_reconfig_strerror((rcp_ep_pwm_out_reconfig_errc_t)99));
}

/* ── PWM_OUT: triggers ──────────────────────────────────────────────────────── */

static void test_out_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_DONE));
}

static void test_out_trigger_cycle_start(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_DONE));
}

static void test_out_trigger_mid_pulse(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_DONE));
}

static void test_out_trigger_done(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_DONE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
}

/* ── PWM_OUT: functional config ─────────────────────────────────────────────── */

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

static void test_out_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

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

static void test_out_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t unauth = {0};
    rcp_lifecycle_writer_ctx_t auth   = {0};

    auth.via_root_client_ep0 = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, unauth));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, auth));
}

static void test_out_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_set_trigger(&cfg, RCP_EP_PWM_OUT_TRIGGER_DONE,
                                                  RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_TRIGGER_NONE, cfg.trigger);
}

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

static void test_out_set_enabled_rejects_unauthorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_set_enabled(&cfg, true, RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_FALSE(cfg.common.ep_enable);
}

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

static void test_out_strerror_never_null_and_distinct(void)
{
    rcp_ep_pwm_out_errc_t codes[] = {
        RCP_EP_PWM_OUT_OK,               RCP_EP_PWM_OUT_ERR_SHORT_FRAME,
        RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE, RCP_EP_PWM_OUT_ERR_WRONG_BUS,
        RCP_EP_PWM_OUT_ERR_WRONG_OP,     RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN,
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

/* ── PWM_OUT: read request ─────────────────────────────────────────────────── */

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

static void test_out_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t           frame = rcp_ep_pwm_out_encode_read_request(3, 7);
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 4, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_BUS, rc);
    rcp_bytes_free(&frame);
}

static void test_out_read_request_rejects_wrong_op(void)
{
    rcp_ep_pwm_value_t    value = {1, 2};
    rcp_bytes_t           frame = rcp_ep_pwm_out_encode_write_request(3, value, RCP_EP_PWM_OUT_WRITE_REPLACE, 1);
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 3, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_OP, rc);
    rcp_bytes_free(&frame);
}

static void test_out_read_request_rejects_short_frame(void)
{
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(NULL, 0, 3, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_SHORT_FRAME, rc);
}

/* ── PWM_OUT: write request ────────────────────────────────────────────────── */

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

/* ── PWM_OUT: response ─────────────────────────────────────────────────────── */

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

/* ── PWM_IN: triggers ───────────────────────────────────────────────────────── */

static void test_in_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_NONE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_NONE, true, false));
}

static void test_in_trigger_rising(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, true));
}

static void test_in_trigger_falling(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, false, false));
}

/* ── PWM_IN: functional config ─────────────────────────────────────────────── */

static void test_in_functional_cfg_init_zeroes(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
}

static void test_in_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

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

static void test_in_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t unauth = {0};
    rcp_lifecycle_writer_ctx_t auth   = {0};

    auth.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, unauth));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, auth));
}

static void test_in_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_set_trigger(&cfg, RCP_EP_PWM_IN_TRIGGER_RISING,
                                                 RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
}

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

static void test_in_strerror_never_null_and_distinct(void)
{
    rcp_ep_pwm_in_errc_t codes[] = {
        RCP_EP_PWM_IN_OK,               RCP_EP_PWM_IN_ERR_SHORT_FRAME,
        RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE, RCP_EP_PWM_IN_ERR_WRONG_BUS,
        RCP_EP_PWM_IN_ERR_WRONG_OP,     RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN,
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

static void test_in_read_request_rejects_short_frame(void)
{
    uint8_t              out_tn;
    rcp_ep_pwm_in_errc_t rc = rcp_ep_pwm_in_decode_read_request(NULL, 0, 2, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_ERR_SHORT_FRAME, rc);
}

/* ── PWM_IN: response ──────────────────────────────────────────────────────── */

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

/* ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ────────────── */

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

static void test_compound_wait_period_ge(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 1000));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 999));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 1001));
}

static void test_compound_wait_period_le(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 1000));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 1001));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 999));
}

static void test_compound_wait_duty_ge(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 500));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 501));
}

static void test_compound_wait_duty_le(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 500));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 499));
}

static void test_compound_wait_invalid_mode_returns_false(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, (rcp_ep_pwm_in_compound_wait_mode_t)0, 0));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, (rcp_ep_pwm_in_compound_wait_mode_t)8, 0));
}

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

    RUN_TEST(test_out_read_request_round_trip);
    RUN_TEST(test_out_read_request_rejects_wrong_bus);
    RUN_TEST(test_out_read_request_rejects_wrong_op);
    RUN_TEST(test_out_read_request_rejects_short_frame);

    RUN_TEST(test_out_write_request_round_trip);
    RUN_TEST(test_out_write_request_rejects_bad_payload_len);
    RUN_TEST(test_out_write_request_rejects_wrong_bus);

    RUN_TEST(test_out_response_round_trip_untimed);
    RUN_TEST(test_out_response_round_trip_timed);
    RUN_TEST(test_out_response_decode_rejects_short_frame);

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

    RUN_TEST(test_in_response_round_trip_untimed);
    RUN_TEST(test_in_response_round_trip_timed);
    RUN_TEST(test_in_response_decode_rejects_wrong_bus);
    RUN_TEST(test_in_response_no_signal_sentinel_round_trips);

    RUN_TEST(test_compound_wait_mode_valid_accepts_exactly_4_to_7);
    RUN_TEST(test_compound_wait_period_ge);
    RUN_TEST(test_compound_wait_period_le);
    RUN_TEST(test_compound_wait_duty_ge);
    RUN_TEST(test_compound_wait_duty_le);
    RUN_TEST(test_compound_wait_invalid_mode_returns_false);
    RUN_TEST(test_compound_wait_no_signal_never_matches);

    return UNITY_END();
}
