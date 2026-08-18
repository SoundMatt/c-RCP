/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_pwm.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/ep_gpio.c's
 * house convention of not sharing a byte-order util across modules) ────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void put_pwm_value(uint8_t *p, rcp_ep_pwm_value_t v)
{
    put_u16(p, v.period);
    put_u16(p + 2, v.active_duration);
}

static rcp_ep_pwm_value_t get_pwm_value(const uint8_t *p)
{
    rcp_ep_pwm_value_t v;

    v.period          = get_u16(p);
    v.active_duration = get_u16(p + 2);
    return v;
}

/* Saturating 16-bit add/subtract shared by every RCP_EP_PWM_OUT_WRITE_ADD/
 * _SUB field application below -- see the file header. Both saturate
 * rather than wrap, per the specification's own no-overflow/no-wrap-around
 * rule for these two operations (extraction §4.5 Group C). */
static uint16_t saturating_add_u16(uint16_t current, uint16_t request)
{
    uint32_t sum = (uint32_t)current + (uint32_t)request;
    return (sum > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)sum;
}

/* The subtraction's operand order is normatively "request minus current"
 * -- the requested payload value is the minuend and the current interface
 * status the subtrahend, not the other way round (extraction §4.5 Group C,
 * the evt[2:0]=110b row). The row's parenthetical remark about decreasing
 * a PWM duty cycle is an illustrative note attached to that row, not a
 * second, contradictory definition of the operand order; the operation
 * itself is stated in the row's own normative sentence. Saturates at
 * 0x0000 on the low side (issue: this was inverted before -- it computed
 * current minus request, which produces a different value for every
 * request where the two operands differ). */
static uint16_t saturating_sub_u16(uint16_t current, uint16_t request)
{
    return (current > request) ? (uint16_t)0u : (uint16_t)(request - current);
}

static uint16_t apply_write_field(uint16_t current, uint16_t request,
                                   rcp_ep_pwm_out_write_semantics_t evt)
{
    switch (evt) {
    case RCP_EP_PWM_OUT_WRITE_REPLACE: return request;
    case RCP_EP_PWM_OUT_WRITE_OR:      return (uint16_t)(current | request);
    case RCP_EP_PWM_OUT_WRITE_AND:     return (uint16_t)(current & request);
    case RCP_EP_PWM_OUT_WRITE_XOR:     return (uint16_t)(current ^ request);
    case RCP_EP_PWM_OUT_WRITE_ADD:     return saturating_add_u16(current, request);
    case RCP_EP_PWM_OUT_WRITE_SUB:     return saturating_sub_u16(current, request);
    case RCP_EP_PWM_OUT_WRITE_RESERVED4:
        /* The "ignored" half of Table 33's own two-part reserved-value
         * rule; the "err-response" half is
         * rcp_ep_pwm_out_decode_write_request()'s own
         * RCP_EP_PWM_OUT_ERR_RESERVED_EVT -- see the file header. */
        return current;
    case RCP_EP_PWM_OUT_WRITE_RECONFIG:
    default:
        /* Not a data-write semantic -- callers must route evt == RECONFIG
         * to rcp_ep_pwm_out_apply_reconfig() instead. Fail safe for a
         * caller that violates that contract. */
        return current;
    }
}

/* ── PWM_OUT: evt[2:0] write semantics ─────────────────────────────────────── */

//cfusa:req REQ-PWM-001
bool rcp_ep_pwm_out_write_semantics_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_PWM_OUT_WRITE_RECONFIG;
}

//cfusa:req REQ-PWM-002
//cfusa:req REQ-PWM-003
//cfusa:req REQ-PWM-004
//cfusa:req REQ-PWM-005
//cfusa:req REQ-PWM-006
//cfusa:req REQ-PWM-007
//cfusa:req REQ-PWM-008
//cfusa:req REQ-PWM-009
//cfusa:req REQ-PWM-056
rcp_ep_pwm_value_t rcp_ep_pwm_out_apply_write(rcp_ep_pwm_value_t current,
                                               rcp_ep_pwm_value_t request,
                                               rcp_ep_pwm_out_write_semantics_t evt,
                                               uint16_t duty_cycle_min,
                                               uint16_t duty_cycle_max)
{
    rcp_ep_pwm_value_t result;

    result.period          = apply_write_field(current.period, request.period, evt);
    result.active_duration = apply_write_field(current.active_duration, request.active_duration, evt);

    /* REQ-PWM-056 (TC18 Table 46): cap, don't reject -- see the header's
     * own doc comment. */
    if (result.active_duration < duty_cycle_min) result.active_duration = duty_cycle_min;
    if (result.active_duration > duty_cycle_max) result.active_duration = duty_cycle_max;

    return result;
}

//cfusa:req REQ-PWM-057
rcp_ep_pwm_out_generation_state_t rcp_ep_pwm_out_generation_state(rcp_ep_pwm_value_t value)
{
    if (value.period == 0u) return RCP_EP_PWM_OUT_GEN_STOPPED;
    if (value.active_duration == 0u) return RCP_EP_PWM_OUT_GEN_OUTPUT_DISABLED;
    return RCP_EP_PWM_OUT_GEN_RUNNING;
}

/* ── PWM_OUT: the EP_func register block (evt[2:0] == 111b) ────────────────── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets, packed
 * from / unpacked into the flags regmap.h's shared functional-config
 * prefix already models. Only the bits regmap.h models are represented:
 * the specification's own common-entries table defines further option
 * bits (separate ack/response CRC enables, an error-stream selector, an
 * ack timestamp enable, an error-message suppressor) that
 * rcp_regmap_ep_functional_cfg_t does not yet carry, so those bit
 * positions read back as 0 and a write to them is accepted and dropped.
 * That is a pre-existing regmap.h modeling gap, called out here so the
 * register-block round trip below is honest about what it preserves. */
#define PWM_OUT_ENABLE_CLR_BIT_ENABLE   ((uint8_t)(1u << 0))
#define PWM_OUT_ENABLE_CLR_BIT_CLEAR    ((uint8_t)(1u << 4))
#define PWM_OUT_OPTIONS_BIT_REQ_CRC     ((uint8_t)(1u << 0))
#define PWM_OUT_OPTIONS_BIT_RESP_TS     ((uint8_t)(1u << 3))
#define PWM_OUT_OPTIONS_BIT_SUPPRESS    ((uint8_t)(1u << 7))

//cfusa:req REQ-PWM-010
void rcp_ep_pwm_out_render_registers(const rcp_ep_pwm_out_functional_cfg_t *cfg,
                                      uint8_t out[RCP_EP_PWM_OUT_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;

    if (cfg->common.ep_enable) enable_clr |= PWM_OUT_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= PWM_OUT_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= PWM_OUT_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= PWM_OUT_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= PWM_OUT_OPTIONS_BIT_SUPPRESS;

    out[RCP_EP_PWM_OUT_REG_EP_LEN]        = (uint8_t)RCP_EP_PWM_OUT_EP_FUNC_LEN;
    out[RCP_EP_PWM_OUT_REG_RESERVED_01]   = 0u;
    out[RCP_EP_PWM_OUT_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_PWM_OUT_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_PWM_OUT_REG_BASE_CLK], cfg->base_clk);
    put_u16(&out[RCP_EP_PWM_OUT_REG_EP_STATUS], cfg->ep_status);
    out[RCP_EP_PWM_OUT_REG_CLK_DIVIDER]  = cfg->clk_divider;
    out[RCP_EP_PWM_OUT_REG_SIGNAL_FLAGS] = cfg->signal_flags;
    put_u16(&out[RCP_EP_PWM_OUT_REG_DUTY_CYCLE_MIN], cfg->duty_cycle_min);
    put_u16(&out[RCP_EP_PWM_OUT_REG_DUTY_CYCLE_MAX], cfg->duty_cycle_max);
    out[RCP_EP_PWM_OUT_REG_SKEW] = cfg->skew;
}

/* The inverse of render: adopts every R/W register from an already
 * patched block image. The read-only offsets (EP_LEN, the reserved octet,
 * base_clk) are deliberately not read back -- apply_reconfig() re-renders
 * them from cfg before patching, so a write covering them is a no-op. */
static void parse_registers(rcp_ep_pwm_out_functional_cfg_t *cfg,
                             const uint8_t in[RCP_EP_PWM_OUT_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_PWM_OUT_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_PWM_OUT_REG_EP_OPTIONS];

    cfg->common.ep_enable            = (enable_clr & PWM_OUT_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage = (enable_clr & PWM_OUT_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable    = (options & PWM_OUT_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & PWM_OUT_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & PWM_OUT_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status      = get_u16(&in[RCP_EP_PWM_OUT_REG_EP_STATUS]);
    cfg->clk_divider    = in[RCP_EP_PWM_OUT_REG_CLK_DIVIDER];
    cfg->signal_flags   = in[RCP_EP_PWM_OUT_REG_SIGNAL_FLAGS];
    cfg->duty_cycle_min = get_u16(&in[RCP_EP_PWM_OUT_REG_DUTY_CYCLE_MIN]);
    cfg->duty_cycle_max = get_u16(&in[RCP_EP_PWM_OUT_REG_DUTY_CYCLE_MAX]);
    cfg->skew           = in[RCP_EP_PWM_OUT_REG_SKEW];
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, the reserved octet, and both octets of
 * base_clk. */
static bool reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_PWM_OUT_REG_EP_LEN ||
           addr == RCP_EP_PWM_OUT_REG_RESERVED_01 ||
           addr == RCP_EP_PWM_OUT_REG_BASE_CLK ||
           addr == (uint16_t)(RCP_EP_PWM_OUT_REG_BASE_CLK + 1u);
}

//cfusa:req REQ-PWM-011
const char *rcp_ep_pwm_out_reconfig_strerror(rcp_ep_pwm_out_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_PWM_OUT_RECONFIG_OK:
        return "rcp/ep_pwm: PWM_OUT configuration write applied";
    case RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT:
        return "rcp/ep_pwm: PWM_OUT configuration write has no address and data";
    case RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_pwm: PWM_OUT configuration write extends past the EP_func block";
    default:
        return "rcp/ep_pwm: PWM_OUT unknown configuration-write error";
    }
}

//cfusa:req REQ-PWM-010
//cfusa:req REQ-PWM-011
rcp_ep_pwm_out_reconfig_errc_t
rcp_ep_pwm_out_apply_reconfig(rcp_ep_pwm_out_functional_cfg_t *cfg,
                               const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_PWM_OUT_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN) {
        return RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (extraction §3.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_PWM_OUT_EP_FUNC_LEN) {
        return RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Patch the block's current image at octet granularity, then adopt
     * it wholesale -- so a write covering only part of a multi-octet
     * register updates exactly the octets it addresses and leaves that
     * register's other octets alone. */
    rcp_ep_pwm_out_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN + i];
    }
    parse_registers(cfg, block);

    return RCP_EP_PWM_OUT_RECONFIG_OK;
}

//cfusa:req REQ-PWM-010
rcp_bytes_t rcp_ep_pwm_out_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                    uint16_t start_address,
                                                    const uint8_t *data, size_t data_len,
                                                    uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                    *payload;
    size_t                      payload_len;
    rcp_bytes_t                 frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)rcp_malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    rcp_memcpy_bounded(payload + RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN, data_len, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)RCP_EP_PWM_OUT_WRITE_RECONFIG;
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    rcp_free(payload);
    return frame;
}

/* ── PWM_OUT: triggers ──────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-012
//cfusa:req REQ-PWM-013
//cfusa:req REQ-PWM-014
//cfusa:req REQ-PWM-015
bool rcp_ep_pwm_out_trigger_fires(rcp_ep_pwm_out_trigger_t trigger, rcp_ep_pwm_out_event_t event)
{
    switch (trigger) {
    case RCP_EP_PWM_OUT_TRIGGER_CYCLE_START: return event == RCP_EP_PWM_OUT_EVENT_CYCLE_START;
    case RCP_EP_PWM_OUT_TRIGGER_MID_PULSE:   return event == RCP_EP_PWM_OUT_EVENT_MID_PULSE;
    case RCP_EP_PWM_OUT_TRIGGER_DONE:        return event == RCP_EP_PWM_OUT_EVENT_DONE;
    case RCP_EP_PWM_OUT_TRIGGER_NONE:
    default:                                 return false;
    }
}

//cfusa:req REQ-PWM-055
uint8_t rcp_ep_pwm_out_trigger_events_at_tick(uint16_t period, uint16_t active_duration,
                                               uint8_t skew, uint32_t raw_tick)
{
    uint32_t skew_mod;
    uint32_t delayed_tick;
    uint8_t  events = 0;

    if (period == 0) return 0; /* STOPPED -- no cycle to derive a phase within */

    /* TC18 §13.7.5.1: "For trigger signal generation the delayed signal
     * is used" -- delayed_tick is raw_tick measured from the SKEWED
     * edge, i.e. skew ticks later than the undelayed source edge
     * raw_tick is itself measured from. skew is reduced mod period
     * first since an 8-bit skew register (0-255) can legally exceed a
     * small period. */
    skew_mod     = (uint32_t)skew % period;
    delayed_tick = (raw_tick % period + period - skew_mod) % period;

    if (delayed_tick == 0) events |= RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START;

    /* Table 45's own "even in case duty cycle is 0%" carve-out: no
     * special case needed -- when active_duration == 0 this reduces to
     * "at the delayed cycle start too", firing alongside CYCLE_START
     * rather than being suppressed. */
    if (delayed_tick == (uint32_t)(active_duration / 2u)) events |= RCP_EP_PWM_OUT_TRIGGER_EVENT_MID_PULSE;

    return events;
}

/* ── PWM_OUT: functional config ─────────────────────────────────────────────── */

//cfusa:req REQ-PWM-016
void rcp_ep_pwm_out_functional_cfg_init(rcp_ep_pwm_out_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* cfg->trigger is already RCP_EP_PWM_OUT_TRIGGER_NONE (0), every
     * EP_func register is already 0, and common.ep_enable (this
     * endpoint's own enable bit) is already false, via the memset and
     * rcp_regmap_ep_functional_cfg_init() above. */
}

//cfusa:req REQ-PWM-017
//cfusa:req REQ-PWM-018
//cfusa:req REQ-PWM-019
bool rcp_ep_pwm_out_functional_cfg_writable(rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-PWM-020
//cfusa:req REQ-PWM-021
bool rcp_ep_pwm_out_set_trigger(rcp_ep_pwm_out_functional_cfg_t *cfg,
                                 rcp_ep_pwm_out_trigger_t trigger,
                                 rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_pwm_out_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

//cfusa:req REQ-PWM-022
//cfusa:req REQ-PWM-023
bool rcp_ep_pwm_out_set_enabled(rcp_ep_pwm_out_functional_cfg_t *cfg, bool enabled,
                                 rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_pwm_out_functional_cfg_writable(state, writer)) return false;

    cfg->common.ep_enable = enabled;
    return true;
}

/* ── PWM_OUT: error codes ──────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-024
const char *rcp_ep_pwm_out_strerror(rcp_ep_pwm_out_errc_t e)
{
    switch (e) {
    case RCP_EP_PWM_OUT_OK:                  return "rcp/ep_pwm: PWM_OUT success";
    case RCP_EP_PWM_OUT_ERR_SHORT_FRAME:     return "rcp/ep_pwm: PWM_OUT frame too short";
    case RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE:    return "rcp/ep_pwm: PWM_OUT unexpected ACF message type";
    case RCP_EP_PWM_OUT_ERR_WRONG_BUS:       return "rcp/ep_pwm: PWM_OUT wrong byte_bus_id";
    case RCP_EP_PWM_OUT_ERR_WRONG_OP:        return "rcp/ep_pwm: PWM_OUT wrong ACF op";
    case RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN: return "rcp/ep_pwm: PWM_OUT unexpected payload length";
    case RCP_EP_PWM_OUT_ERR_RESERVED_EVT:    return "rcp/ep_pwm: PWM_OUT evt[2:0] is the reserved value 100b";
    default:                                 return "rcp/ep_pwm: PWM_OUT unknown error";
    }
}

//cfusa:req REQ-PWM-028
//cfusa:req REQ-PWM-008
rcp_wire_error_t rcp_ep_pwm_out_wire_error(rcp_ep_pwm_out_errc_t e)
{
    switch (e) {
    /* TC18 §13.7.5.3: "A request not having exactly four bytes is
     * rejected and an error response with error code = INVALID_PARAMETER
     * will be sent." (verbatim identical to GPIO's own §13.7.4.1 rule --
     * mirrors ep_gpio.c's rcp_ep_gpio_wire_error(), REQ-GPIO-033.) */
    case RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN: return RCP_ERROR_INVALID_PARAMETER;
    /* TC18 §13.5 Table 33's GPIO/PWM_OUT row, evt[2:0]=100b: "reserved --
     * request shall be ignored and an err-response with error code =
     * UNSUPPORTED_CMD shall be sent." FIXED 2026-08-14, issue #426. */
    case RCP_EP_PWM_OUT_ERR_RESERVED_EVT:    return RCP_ERROR_UNSUPPORTED_CMD;
    /* RCP_EP_PWM_OUT_OK and the remaining error codes are all local
     * framing/routing outcomes with no numbered wire-error-code
     * counterpart -- see this function's own doc comment (ep_pwm.h). */
    default: return RCP_ERROR_NONE;
    }
}

/* ── PWM_OUT: read request ─────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-025
rcp_bytes_t rcp_ep_pwm_out_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-PWM-025
//cfusa:req REQ-PWM-026
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_read_request(const uint8_t *b, size_t len,
                                                          rcp_byte_bus_id_t expected_bus_id,
                                                          uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_PWM_OUT_ERR_WRONG_OP;

    (void)payload;
    (void)payload_len; /* a read request carries no payload -- see ep_pwm.h */

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_PWM_OUT_OK;
}

/* ── PWM_OUT: write request ────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-027
rcp_bytes_t rcp_ep_pwm_out_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                                 rcp_ep_pwm_value_t value,
                                                 rcp_ep_pwm_out_write_semantics_t evt,
                                                 uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                      payload[RCP_EP_PWM_PAYLOAD_LEN];

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)((unsigned)evt & 0x7u);
    hdr.transaction_num = transaction_num;

    put_pwm_value(payload, value);

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-PWM-027
//cfusa:req REQ-PWM-028
//cfusa:req REQ-PWM-008
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_write_request(const uint8_t *b, size_t len,
                                                           rcp_byte_bus_id_t expected_bus_id,
                                                           rcp_ep_pwm_value_t *out_value,
                                                           uint8_t *out_evt,
                                                           uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_PWM_OUT_ERR_WRONG_OP;
    if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN;
    /* Table 33's own GPIO/PWM_OUT row, evt[2:0]=100b: "reserved -- request
     * shall be ignored and an err-response with error code =
     * UNSUPPORTED_CMD shall be sent" -- the "err-response" half of that
     * two-part rule (rcp_ep_pwm_out_apply_write() already implements the
     * "ignored" half). FIXED 2026-08-14, issue #426; mirrors
     * ep_gpio.c's identical fix and src/regmap.c's REQ-RMAP-068 fix for
     * the same evt[2:0]=100b reserved value in a different context. */
    if ((hdr.evt & 0x7u) == (uint8_t)RCP_EP_PWM_OUT_WRITE_RESERVED4) {
        return RCP_EP_PWM_OUT_ERR_RESERVED_EVT;
    }

    *out_value           = get_pwm_value(payload);
    *out_evt             = (uint8_t)(hdr.evt & 0x7u);
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_PWM_OUT_OK;
}

/* ── PWM_OUT: response ─────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-029
//cfusa:req REQ-PWM-030
rcp_bytes_t rcp_ep_pwm_out_encode_response(rcp_byte_bus_id_t byte_bus_id, rcp_ep_pwm_value_t value,
                                            uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    uint8_t payload[RCP_EP_PWM_PAYLOAD_LEN];

    put_pwm_value(payload, value);

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, payload, sizeof(payload));
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    }
}

//cfusa:req REQ-PWM-029
//cfusa:req REQ-PWM-030
//cfusa:req REQ-PWM-031
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_response(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      rcp_ep_pwm_value_t *out_value,
                                                      bool *out_timed, uint64_t *out_timestamp,
                                                      uint8_t *out_transaction_num)
{
    uint8_t                      msg_type;
    rcp_acf_errc_t               acf_rc;
    rcp_acf_byte_message_info_t  abb_hdr;
    rcp_acf_gbb_header_t         gbb_hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_byte_bus_id_t            bus_id;
    uint8_t                      transaction_num;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = false;
        *out_timestamp = 0u;
    }

    *out_transaction_num = transaction_num;
    return RCP_EP_PWM_OUT_OK;
}

/* ── PWM_IN: triggers ───────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-032
//cfusa:req REQ-PWM-033
//cfusa:req REQ-PWM-034
bool rcp_ep_pwm_in_trigger_fires(rcp_ep_pwm_in_trigger_t trigger, bool prev_level, bool new_level)
{
    switch (trigger) {
    case RCP_EP_PWM_IN_TRIGGER_RISING:  return !prev_level && new_level;
    case RCP_EP_PWM_IN_TRIGGER_FALLING: return prev_level && !new_level;
    case RCP_EP_PWM_IN_TRIGGER_NONE:
    default:                            return false;
    }
}

/* ── PWM_IN: functional config ─────────────────────────────────────────────── */

//cfusa:req REQ-PWM-035
void rcp_ep_pwm_in_functional_cfg_init(rcp_ep_pwm_in_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* cfg->trigger is already RCP_EP_PWM_IN_TRIGGER_NONE (0), and every
     * EP_func register (base_clk/ep_status/clk_divider/flags/max_period)
     * is already 0, via the memset above. */
}

//cfusa:req REQ-PWM-036
//cfusa:req REQ-PWM-037
//cfusa:req REQ-PWM-038
bool rcp_ep_pwm_in_functional_cfg_writable(rcp_lifecycle_state_t state,
                                           rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-PWM-039
//cfusa:req REQ-PWM-040
bool rcp_ep_pwm_in_set_trigger(rcp_ep_pwm_in_functional_cfg_t *cfg,
                                rcp_ep_pwm_in_trigger_t trigger, rcp_lifecycle_state_t state,
                                rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_pwm_in_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

/* ── PWM_IN: the EP_func register block (evt[2:0] == 111b) ────────────────── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets -- same
 * house convention and same "regmap.h modeling gap" caveat as PWM_OUT's own
 * copy above. */
#define PWM_IN_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define PWM_IN_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))
#define PWM_IN_OPTIONS_BIT_REQ_CRC   ((uint8_t)(1u << 0))
#define PWM_IN_OPTIONS_BIT_RESP_TS   ((uint8_t)(1u << 3))
#define PWM_IN_OPTIONS_BIT_SUPPRESS  ((uint8_t)(1u << 7))

//cfusa:req REQ-PWM-058
void rcp_ep_pwm_in_render_registers(const rcp_ep_pwm_in_functional_cfg_t *cfg,
                                     uint8_t out[RCP_EP_PWM_IN_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;

    if (cfg->common.ep_enable) enable_clr |= PWM_IN_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= PWM_IN_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= PWM_IN_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= PWM_IN_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= PWM_IN_OPTIONS_BIT_SUPPRESS;

    out[RCP_EP_PWM_IN_REG_EP_LEN]        = (uint8_t)RCP_EP_PWM_IN_EP_FUNC_LEN;
    out[RCP_EP_PWM_IN_REG_RESERVED_01]   = 0u;
    out[RCP_EP_PWM_IN_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_PWM_IN_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_PWM_IN_REG_BASE_CLK], cfg->base_clk);
    put_u16(&out[RCP_EP_PWM_IN_REG_EP_STATUS], cfg->ep_status);
    out[RCP_EP_PWM_IN_REG_CLK_DIVIDER] = cfg->clk_divider;
    out[RCP_EP_PWM_IN_REG_FLAGS]       = cfg->flags;
    put_u16(&out[RCP_EP_PWM_IN_REG_MAX_PERIOD], cfg->max_period);
}

/* The inverse of render -- same "read-only offsets not read back" design as
 * PWM_OUT's own parse_registers(). */
static void parse_pwm_in_registers(rcp_ep_pwm_in_functional_cfg_t *cfg,
                                    const uint8_t in[RCP_EP_PWM_IN_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_PWM_IN_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_PWM_IN_REG_EP_OPTIONS];

    cfg->common.ep_enable             = (enable_clr & PWM_IN_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage  = (enable_clr & PWM_IN_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable     = (options & PWM_IN_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & PWM_IN_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & PWM_IN_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status   = get_u16(&in[RCP_EP_PWM_IN_REG_EP_STATUS]);
    cfg->clk_divider = in[RCP_EP_PWM_IN_REG_CLK_DIVIDER];
    cfg->flags       = in[RCP_EP_PWM_IN_REG_FLAGS];
    cfg->max_period  = get_u16(&in[RCP_EP_PWM_IN_REG_MAX_PERIOD]);
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, the reserved octet, and both octets of
 * base_clk. */
static bool pwm_in_reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_PWM_IN_REG_EP_LEN ||
           addr == RCP_EP_PWM_IN_REG_RESERVED_01 ||
           addr == RCP_EP_PWM_IN_REG_BASE_CLK ||
           addr == (uint16_t)(RCP_EP_PWM_IN_REG_BASE_CLK + 1u);
}

//cfusa:req REQ-PWM-058
const char *rcp_ep_pwm_in_reconfig_strerror(rcp_ep_pwm_in_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_PWM_IN_RECONFIG_OK:
        return "rcp/ep_pwm: PWM_IN configuration write applied";
    case RCP_EP_PWM_IN_RECONFIG_ERR_SHORT:
        return "rcp/ep_pwm: PWM_IN configuration write has no address and data";
    case RCP_EP_PWM_IN_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_pwm: PWM_IN configuration write extends past the EP_func block";
    default:
        return "rcp/ep_pwm: PWM_IN unknown configuration-write error";
    }
}

//cfusa:req REQ-PWM-058
rcp_ep_pwm_in_reconfig_errc_t
rcp_ep_pwm_in_apply_reconfig(rcp_ep_pwm_in_functional_cfg_t *cfg,
                              const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_PWM_IN_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_PWM_IN_RECONFIG_ADDR_LEN) {
        return RCP_EP_PWM_IN_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_PWM_IN_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (extraction §3.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_PWM_IN_EP_FUNC_LEN) {
        return RCP_EP_PWM_IN_RECONFIG_ERR_OUT_OF_RANGE;
    }

    rcp_ep_pwm_in_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (pwm_in_reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_PWM_IN_RECONFIG_ADDR_LEN + i];
    }
    parse_pwm_in_registers(cfg, block);

    return RCP_EP_PWM_IN_RECONFIG_OK;
}

//cfusa:req REQ-PWM-058
rcp_bytes_t rcp_ep_pwm_in_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                   uint16_t start_address,
                                                   const uint8_t *data, size_t data_len,
                                                   uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                    *payload;
    size_t                      payload_len;
    rcp_bytes_t                 frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_PWM_IN_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)rcp_malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    rcp_memcpy_bounded(payload + RCP_EP_PWM_IN_RECONFIG_ADDR_LEN, data_len, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0x7u; /* the reconfiguration escape hatch --
                                    PWM_IN has no named write-semantics enum
                                    of its own (it belongs to the
                                    ADC/I2C/LIN/CAN/UART/ISELED/MDIO
                                    reserved-range group, not PWM_OUT's/
                                    GPIO's own eight-value write-semantics
                                    group), so the raw value is used
                                    directly, matching ep_adc.c's/
                                    ep_i2c.c's own equivalent encoders. */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    rcp_free(payload);
    return frame;
}

/* ── PWM_IN: error codes ───────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-041
const char *rcp_ep_pwm_in_strerror(rcp_ep_pwm_in_errc_t e)
{
    switch (e) {
    case RCP_EP_PWM_IN_OK:                  return "rcp/ep_pwm: PWM_IN success";
    case RCP_EP_PWM_IN_ERR_SHORT_FRAME:     return "rcp/ep_pwm: PWM_IN frame too short";
    case RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE:    return "rcp/ep_pwm: PWM_IN unexpected ACF message type";
    case RCP_EP_PWM_IN_ERR_WRONG_BUS:       return "rcp/ep_pwm: PWM_IN wrong byte_bus_id";
    case RCP_EP_PWM_IN_ERR_WRONG_OP:        return "rcp/ep_pwm: PWM_IN wrong ACF op";
    case RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN: return "rcp/ep_pwm: PWM_IN unexpected payload length";
    case RCP_EP_PWM_IN_ERR_BAD_EVT:         return "rcp/ep_pwm: PWM_IN unsupported evt[2:0]";
    default:                                return "rcp/ep_pwm: PWM_IN unknown error";
    }
}

/* ── PWM_IN: read request ──────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-042
rcp_bytes_t rcp_ep_pwm_in_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                               uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-PWM-042
//cfusa:req REQ-PWM-043
rcp_ep_pwm_in_errc_t rcp_ep_pwm_in_decode_read_request(const uint8_t *b, size_t len,
                                                        rcp_byte_bus_id_t expected_bus_id,
                                                        uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_PWM_IN_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_PWM_IN_ERR_WRONG_OP;
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_PWM_IN_ERR_BAD_EVT;

    (void)payload;
    (void)payload_len;

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_PWM_IN_OK;
}

/* ── PWM_IN: response ──────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-044
//cfusa:req REQ-PWM-045
//cfusa:req REQ-PWM-047
rcp_bytes_t rcp_ep_pwm_in_encode_response(rcp_byte_bus_id_t byte_bus_id, rcp_ep_pwm_value_t value,
                                           uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    uint8_t payload[RCP_EP_PWM_PAYLOAD_LEN];

    put_pwm_value(payload, value);

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, payload, sizeof(payload));
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    }
}

//cfusa:req REQ-PWM-044
//cfusa:req REQ-PWM-045
//cfusa:req REQ-PWM-046
//cfusa:req REQ-PWM-047
rcp_ep_pwm_in_errc_t rcp_ep_pwm_in_decode_response(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    rcp_ep_pwm_value_t *out_value, bool *out_timed,
                                                    uint64_t *out_timestamp,
                                                    uint8_t *out_transaction_num)
{
    uint8_t                      msg_type;
    rcp_acf_errc_t               acf_rc;
    rcp_acf_byte_message_info_t  abb_hdr;
    rcp_acf_gbb_header_t         gbb_hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_byte_bus_id_t            bus_id;
    uint8_t                      transaction_num;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_IN_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_IN_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = false;
        *out_timestamp = 0u;
    }

    *out_transaction_num = transaction_num;
    return RCP_EP_PWM_IN_OK;
}

/* ── PWM_IN: MAX_PERIOD timeout classification (REQ-PWM-058 remainder) ──────── */

//cfusa:req REQ-PWM-058
rcp_ep_pwm_in_max_period_outcome_t
rcp_ep_pwm_in_max_period_outcome(uint16_t measured_period, uint16_t max_period,
                                  bool err_on_max_period, bool resp_on_err_enabled)
{
    /* Table 48's own pwmi_err_on_max_period row (0x0009.1) -- see this
     * function's own doc comment (ep_pwm.h) for the full rule text and
     * the pure-classifier/caller-owns-the-timer design this mirrors from
     * rcp_ep_gpio_debounce_sample() (ep_gpio.c). */
    if (measured_period <= max_period) return RCP_EP_PWM_IN_MAX_PERIOD_OK;

    if (!err_on_max_period) return RCP_EP_PWM_IN_MAX_PERIOD_INVALIDATE;

    return resp_on_err_enabled ? RCP_EP_PWM_IN_MAX_PERIOD_STOP_AND_ERROR
                                : RCP_EP_PWM_IN_MAX_PERIOD_STOP;
}

//cfusa:req REQ-WIREERR-007
rcp_wire_error_t rcp_ep_pwm_in_wire_error(rcp_ep_pwm_in_max_period_outcome_t outcome)
{
    switch (outcome) {
    /* TC18 §13.7.6.2 Table 48's own pwmi_err_on_max_period row:
     * "1b: if MAX_PERIOD is exceeded stop measurement and signal error
     * if error response is enabled in EP_config" -- PWM_IN's own
     * numbered wire error code. */
    case RCP_EP_PWM_IN_MAX_PERIOD_STOP_AND_ERROR: return RCP_ERROR_PWM_IN_NO_SIGNAL;
    /* OK/INVALIDATE/STOP each explicitly signal no error of their own
     * -- see rcp_ep_pwm_in_max_period_outcome()'s own doc comment. */
    default: return RCP_ERROR_NONE;
    }
}

/* ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ────────────── */

//cfusa:req REQ-PWM-048
bool rcp_ep_pwm_in_compound_wait_mode_valid(uint8_t v)
{
    return v >= (uint8_t)RCP_EP_PWM_IN_CMP_PERIOD_GE && v <= (uint8_t)RCP_EP_PWM_IN_CMP_DUTY_LE;
}

//cfusa:req REQ-PWM-049
//cfusa:req REQ-PWM-050
//cfusa:req REQ-PWM-051
//cfusa:req REQ-PWM-052
//cfusa:req REQ-PWM-053
//cfusa:req REQ-PWM-054
bool rcp_ep_pwm_in_compound_wait_compare(rcp_ep_pwm_value_t captured,
                                          rcp_ep_pwm_in_compound_wait_mode_t mode,
                                          uint16_t threshold)
{
    /* TC18 §13.5.1: evt[2:0]=100b/110b ("GE") is met when byte_msg_payload
     * (threshold) is >= the current interface status (captured) -- i.e.
     * threshold >= captured, equivalently captured <= threshold. evt=101b/
     * 111b ("LE") is the mirror: threshold <= captured, i.e.
     * captured >= threshold. Corrected 2026-08-10 (c-RCP-AUDIT-06, issue
     * #256 Group B): this function previously computed captured >=
     * threshold for the GE case and captured <= threshold for LE -- the
     * reverse of TC18's own rule in both cases. src/acf.c's
     * rcp_acf_compound_wait_match() (this same §13.5.1 rule's own
     * reference implementation, COMPOUND_WAIT_MODE_HI_GE/_LE) always got
     * this right: payload compared directly against status, never
     * swapped -- this function's own copy of the identical rule was the
     * one that diverged. */
    switch (mode) {
    case RCP_EP_PWM_IN_CMP_PERIOD_GE:
        if (captured.period == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.period <= threshold;
    case RCP_EP_PWM_IN_CMP_PERIOD_LE:
        if (captured.period == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.period >= threshold;
    case RCP_EP_PWM_IN_CMP_DUTY_GE:
        if (captured.active_duration == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.active_duration <= threshold;
    case RCP_EP_PWM_IN_CMP_DUTY_LE:
        if (captured.active_duration == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.active_duration >= threshold;
    default:
        return false;
    }
}
