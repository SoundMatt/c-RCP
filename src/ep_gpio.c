/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_gpio.h"
#include "rcp/alloc.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/discovery.c's
 * house convention of not sharing a byte-order util across modules) ────── */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* ── Pin addressing ────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-002
bool rcp_ep_gpio_pin_index_valid(uint8_t pin_index)
{
    return pin_index < RCP_EP_GPIO_MAX_PINS;
}

//cfusa:req REQ-GPIO-003
uint32_t rcp_ep_gpio_pin_mask(uint8_t pin_index)
{
    if (!rcp_ep_gpio_pin_index_valid(pin_index)) return 0;
    return (uint32_t)1u << pin_index;
}

//cfusa:req REQ-GPIO-004
bool rcp_ep_gpio_pin_get(uint32_t bitmask, uint8_t pin_index)
{
    return (bitmask & rcp_ep_gpio_pin_mask(pin_index)) != 0;
}

/* ── evt[2:0]: the eight write-semantics variants ──────────────────────────── */

//cfusa:req REQ-GPIO-005
bool rcp_ep_gpio_write_semantics_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_GPIO_WRITE_RECONFIG;
}

//cfusa:req REQ-GPIO-006
//cfusa:req REQ-GPIO-007
//cfusa:req REQ-GPIO-008
//cfusa:req REQ-GPIO-009
//cfusa:req REQ-GPIO-010
//cfusa:req REQ-GPIO-011
//cfusa:req REQ-GPIO-012
uint32_t rcp_ep_gpio_apply_write(uint32_t current, uint32_t request,
                                  rcp_ep_gpio_write_semantics_t evt)
{
    switch (evt) {
    case RCP_EP_GPIO_WRITE_REPLACE: return request;
    case RCP_EP_GPIO_WRITE_OR:      return current | request;
    /* Known editorial defect in the source spec (issue #433): §13.7.4.1's
     * own prose summary names this operation "NAND" ("Output pins can be
     * set to a defined state and the current state can be changed by a
     * logical operation (NAND, OR, XOR)"), but Table 33's own
     * authoritative, worked-example row for evt[2:0]=010b both names it
     * "AND" and demonstrates it with a worked example that only holds for
     * a plain bitwise AND: "byte_msg_payload bitwise AND current
     * interface status is written to the interface (example: with a
     * byte_msg_payload of 0xFFFF FFFE the first IO pin will be reset,
     * while other IO pins remain unchanged)" -- a NAND of the same
     * operands would invert, not leave unchanged, every pin whose payload
     * bit is set. Table 33's own worked example is the more
     * authoritative, unambiguous definition, so this code follows AND,
     * not the prose's "NAND" -- no code or behavior change here, this
     * note exists only to record the specification's own internal
     * inconsistency, matching this file's gpio_debounce_IO31 offset
     * defect note (ep_gpio.h's own file header) and ep_pwm.h's PWM
     * idle-state bit-collision note's own practice of pinning known TC18
     * editorial defects in place. */
    case RCP_EP_GPIO_WRITE_AND:     return current & request;
    case RCP_EP_GPIO_WRITE_XOR:     return current ^ request;
    case RCP_EP_GPIO_WRITE_ADD:
        /* Saturate at 0xFFFFFFFF rather than wrapping -- see the file
         * header. (current + request) itself cannot overflow a uint64_t,
         * so the sum is computed widened and then clamped. */
        {
            uint64_t sum = (uint64_t)current + (uint64_t)request;
            return (sum > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)sum;
        }
    case RCP_EP_GPIO_WRITE_SUB:
        /* The subtraction's operand order is normatively "request minus
         * current": the requested payload value is the minuend and the
         * current interface status the subtrahend (extraction §4.5 Group
         * C, the evt[2:0]=110b row -- one row covering GPIO and PWM_OUT
         * jointly, so the identical rule ep_pwm.c applies). The row's
         * parenthetical remark about decreasing a PWM duty cycle is an
         * illustrative note, not a second definition of the operand
         * order. Saturates at 0x00000000 rather than wrapping. */
        return (current > request) ? 0u : (request - current);
    case RCP_EP_GPIO_WRITE_RESERVED4:
        return current; /* documented no-op; see the file header */
    case RCP_EP_GPIO_WRITE_RECONFIG:
    default:
        /* Not a data-write semantic -- callers must route evt == RECONFIG
         * to rcp_ep_gpio_apply_reconfig() instead (see the file header).
         * Fail safe for a caller that violates that contract: leave the
         * register unchanged rather than perform an unintended write. */
        return current;
    }
}

/* RENAMED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group G, REQ-GPIO-013):
 * formerly rcp_ep_gpio_apply_reconfig() -- see ep_gpio.h's file header and
 * this function's own declaration for why this is no longer described as
 * evt[2:0]==111b's handler. */
//cfusa:req REQ-GPIO-013
void rcp_ep_gpio_toggle_pin_direction(uint8_t pins[RCP_EP_GPIO_MAX_PINS], uint32_t toggle_mask)
{
    uint8_t i;

    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        if ((toggle_mask & rcp_ep_gpio_pin_mask(i)) == 0) continue;

        if ((pins[i] & RCP_REGMAP_PIN_PROP_OUTPUT) != 0) {
            pins[i] = (uint8_t)((pins[i] & ~(unsigned)RCP_REGMAP_PIN_PROP_OUTPUT) |
                                 RCP_REGMAP_PIN_PROP_INPUT);
        } else {
            pins[i] = (uint8_t)((pins[i] & ~(unsigned)RCP_REGMAP_PIN_PROP_INPUT) |
                                 RCP_REGMAP_PIN_PROP_OUTPUT);
        }
    }
}

//cfusa:req REQ-GPIO-037
uint32_t rcp_ep_gpio_apply_masked_write(uint32_t current, uint32_t request,
                                         rcp_ep_gpio_write_semantics_t evt,
                                         const uint8_t pins[RCP_EP_GPIO_MAX_PINS])
{
    uint32_t combined    = rcp_ep_gpio_apply_write(current, request, evt);
    uint32_t output_mask = 0;
    uint8_t  i;

    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        if ((pins[i] & RCP_REGMAP_PIN_PROP_OUTPUT) != 0) {
            output_mask |= rcp_ep_gpio_pin_mask(i);
        }
    }

    return (combined & output_mask) | (current & ~output_mask);
}

/* ── Per-pin trigger signals ────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-014
//cfusa:req REQ-GPIO-015
//cfusa:req REQ-GPIO-016
//cfusa:req REQ-GPIO-017
bool rcp_ep_gpio_trigger_fires(rcp_ep_gpio_trigger_t trigger, bool prev_level, bool new_level)
{
    switch (trigger) {
    case RCP_EP_GPIO_TRIGGER_ANY_CHANGE: return prev_level != new_level;
    case RCP_EP_GPIO_TRIGGER_RISING:     return !prev_level && new_level;
    case RCP_EP_GPIO_TRIGGER_FALLING:    return prev_level && !new_level;
    case RCP_EP_GPIO_TRIGGER_NONE:
    default:                             return false;
    }
}

//cfusa:req REQ-GPIO-034
bool rcp_ep_gpio_trigger_signal_number(uint8_t pin_index, rcp_ep_gpio_trigger_t trigger,
                                        uint8_t *out_signal_number)
{
    if (pin_index >= RCP_EP_GPIO_MAX_PINS) return false;

    switch (trigger) {
    case RCP_EP_GPIO_TRIGGER_ANY_CHANGE:
    case RCP_EP_GPIO_TRIGGER_RISING:
    case RCP_EP_GPIO_TRIGGER_FALLING:
        /* Table 43's own 3n+{1,2,3} pattern -- rcp_ep_gpio_trigger_t's
         * ordinals (ANY_CHANGE=1/RISING=2/FALLING=3) are exactly the
         * table's own +1/+2/+3 offset, by this module's own design (see
         * the header's doc comment), so no per-case arithmetic is
         * needed. */
        *out_signal_number = (uint8_t)(3u * pin_index + (uint8_t)trigger);
        return true;
    case RCP_EP_GPIO_TRIGGER_NONE:
    default:
        return false;
    }
}

//cfusa:req REQ-GPIO-035
void rcp_ep_gpio_debounce_state_init(rcp_ep_gpio_debounce_state_t *s)
{
    memset(s, 0, sizeof(*s));
}

//cfusa:req REQ-GPIO-035
bool rcp_ep_gpio_debounce_sample(rcp_ep_gpio_debounce_state_t *s, bool raw_value, uint8_t n,
                                  bool *out_changed)
{
    bool changed;
    bool prev_settled;

    prev_settled = s->has_settled && s->settled_value;

    if (n == 0u) {
        /* "0: no debounce" -- every sample is immediately the settled
         * value. */
        s->has_candidate = false;
        changed = s->has_settled && (prev_settled != raw_value);
        s->has_settled   = true;
        s->settled_value = raw_value;
        if (out_changed) *out_changed = changed;
        return s->settled_value;
    }

    if (!s->has_candidate || s->candidate_value != raw_value) {
        /* A differing sample discards any partial run and starts a new
         * one at count 1 -- "n CONSECUTIVE samples", not merely n samples
         * seen at any point. */
        s->has_candidate     = true;
        s->candidate_value   = raw_value;
        s->consecutive_count = 1u;
    } else if (s->consecutive_count < 0xFFu) {
        s->consecutive_count++;
    }

    changed = false;
    if (s->consecutive_count >= n &&
        (!s->has_settled || s->settled_value != s->candidate_value)) {
        changed          = s->has_settled; /* first-ever settle isn't a "change" */
        s->has_settled   = true;
        s->settled_value = s->candidate_value;
    }

    if (out_changed) *out_changed = changed;
    /* Before the very first debounce window completes there is no
     * settled value yet -- default to false rather than leaking the raw,
     * unfiltered sample, which would defeat the filter's own purpose for
     * every caller checking the output mid-run. */
    return s->has_settled ? s->settled_value : false;
}

//cfusa:req REQ-GPIO-036
rcp_ep_gpio_response_timing_t rcp_ep_gpio_response_timing(rcp_acf_op_t op, size_t payload_len)
{
    if (op == RCP_ACF_OP_WRITE) return RCP_EP_GPIO_RESPONSE_AFTER_DEBOUNCE;
    if (op == RCP_ACF_OP_READ) {
        return (payload_len == 0u) ? RCP_EP_GPIO_RESPONSE_IMMEDIATE
                                    : RCP_EP_GPIO_RESPONSE_AFTER_DEBOUNCE;
    }
    return RCP_EP_GPIO_RESPONSE_IMMEDIATE;
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-018
void rcp_ep_gpio_functional_cfg_init(rcp_ep_gpio_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* Every pins[i].trigger is already RCP_EP_GPIO_TRIGGER_NONE (0) and
     * pin_property already 0 via the memset above. */
}

//cfusa:req REQ-GPIO-019
//cfusa:req REQ-GPIO-020
//cfusa:req REQ-GPIO-021
bool rcp_ep_gpio_functional_cfg_writable(rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-GPIO-022
//cfusa:req REQ-GPIO-023
bool rcp_ep_gpio_set_pin_property(rcp_ep_gpio_functional_cfg_t *cfg, uint8_t pin_index,
                                   uint8_t pin_property, rcp_lifecycle_state_t state,
                                   rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_gpio_pin_index_valid(pin_index)) return false;
    if (!rcp_ep_gpio_functional_cfg_writable(state, writer)) return false;

    cfg->pins[pin_index].pin_property = pin_property;
    return true;
}

//cfusa:req REQ-GPIO-024
//cfusa:req REQ-GPIO-025
bool rcp_ep_gpio_set_pin_trigger(rcp_ep_gpio_functional_cfg_t *cfg, uint8_t pin_index,
                                  rcp_ep_gpio_trigger_t trigger, rcp_lifecycle_state_t state,
                                  rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_gpio_pin_index_valid(pin_index)) return false;
    if (!rcp_ep_gpio_functional_cfg_writable(state, writer)) return false;

    cfg->pins[pin_index].trigger = (uint8_t)trigger;
    return true;
}

/* ── The EP_func register block (evt[2:0] == 111b) -- see the file header ──── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets, packed
 * from / unpacked into the flags regmap.h's shared functional-config
 * prefix already models -- the same Table 35 common-entries bit positions
 * ep_pwm.c's own PWM_OUT_ENABLE_CLR_BIT_ and PWM_OUT_OPTIONS_BIT_ constants
 * use, since both endpoint types cite the identical shared table. */
#define GPIO_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define GPIO_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))
#define GPIO_OPTIONS_BIT_REQ_CRC   ((uint8_t)(1u << 0))
#define GPIO_OPTIONS_BIT_RESP_TS   ((uint8_t)(1u << 3))
#define GPIO_OPTIONS_BIT_SUPPRESS  ((uint8_t)(1u << 7))

//cfusa:req REQ-GPIO-038
void rcp_ep_gpio_render_registers(const rcp_ep_gpio_functional_cfg_t *cfg,
                                   uint8_t out[RCP_EP_GPIO_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;
    uint8_t i;

    if (cfg->common.ep_enable) enable_clr |= GPIO_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= GPIO_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= GPIO_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= GPIO_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= GPIO_OPTIONS_BIT_SUPPRESS;

    out[RCP_EP_GPIO_REG_EP_LEN]        = (uint8_t)RCP_EP_GPIO_EP_FUNC_LEN;
    out[RCP_EP_GPIO_REG_IO_MAX]        = RCP_EP_GPIO_MAX_PINS;
    out[RCP_EP_GPIO_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_GPIO_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_GPIO_REG_BASE_CLK], 0u); /* read-only, this module
                                                     defines no GPIO clock
                                                     source -- see the file
                                                     header */
    put_u16(&out[RCP_EP_GPIO_REG_EP_STATUS], cfg->ep_status);
    out[RCP_EP_GPIO_REG_CLK_DIVIDER] = cfg->clk_divider;
    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        out[RCP_EP_GPIO_REG_DEBOUNCE_IO0 + i] = cfg->debounce[i];
    }
}

/* The inverse of render: adopts every R/W register from an already patched
 * block image. The read-only offsets (EP_LEN, IO_MAX, base_clk) are
 * deliberately not read back -- apply_reconfig() re-renders them from cfg
 * before patching, so a write covering them is a no-op. */
static void parse_registers(rcp_ep_gpio_functional_cfg_t *cfg,
                             const uint8_t in[RCP_EP_GPIO_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_GPIO_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_GPIO_REG_EP_OPTIONS];
    uint8_t i;

    cfg->common.ep_enable            = (enable_clr & GPIO_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage = (enable_clr & GPIO_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable    = (options & GPIO_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & GPIO_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & GPIO_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status   = get_u16(&in[RCP_EP_GPIO_REG_EP_STATUS]);
    cfg->clk_divider = in[RCP_EP_GPIO_REG_CLK_DIVIDER];
    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        cfg->debounce[i] = in[RCP_EP_GPIO_REG_DEBOUNCE_IO0 + i];
    }
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, IO_MAX, and both octets of base_clk. */
static bool reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_GPIO_REG_EP_LEN ||
           addr == RCP_EP_GPIO_REG_IO_MAX ||
           addr == RCP_EP_GPIO_REG_BASE_CLK ||
           addr == (uint16_t)(RCP_EP_GPIO_REG_BASE_CLK + 1u);
}

//cfusa:req REQ-GPIO-039
const char *rcp_ep_gpio_reconfig_strerror(rcp_ep_gpio_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_GPIO_RECONFIG_OK:
        return "rcp/ep_gpio: GPIO configuration write applied";
    case RCP_EP_GPIO_RECONFIG_ERR_SHORT:
        return "rcp/ep_gpio: GPIO configuration write has no address and data";
    case RCP_EP_GPIO_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_gpio: GPIO configuration write extends past the EP_func block";
    default:
        return "rcp/ep_gpio: GPIO unknown configuration-write error";
    }
}

//cfusa:req REQ-GPIO-013
//cfusa:req REQ-GPIO-038
//cfusa:req REQ-GPIO-039
rcp_ep_gpio_reconfig_errc_t
rcp_ep_gpio_apply_reconfig(rcp_ep_gpio_functional_cfg_t *cfg,
                            const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_GPIO_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_GPIO_RECONFIG_ADDR_LEN) {
        return RCP_EP_GPIO_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_GPIO_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (extraction §3.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_GPIO_EP_FUNC_LEN) {
        return RCP_EP_GPIO_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Patch the block's current image at octet granularity, then adopt it
     * wholesale -- so a write covering only part of a multi-octet register
     * updates exactly the octets it addresses and leaves that register's
     * other octets alone. */
    rcp_ep_gpio_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_GPIO_RECONFIG_ADDR_LEN + i];
    }
    parse_registers(cfg, block);

    return RCP_EP_GPIO_RECONFIG_OK;
}

//cfusa:req REQ-GPIO-038
rcp_bytes_t rcp_ep_gpio_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                 uint16_t start_address,
                                                 const uint8_t *data, size_t data_len,
                                                 uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                     *payload;
    size_t                       payload_len;
    rcp_bytes_t                  frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_GPIO_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)rcp_malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    memcpy(payload + RCP_EP_GPIO_RECONFIG_ADDR_LEN, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)RCP_EP_GPIO_WRITE_RECONFIG;
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    rcp_free(payload);
    return frame;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-001
const char *rcp_ep_gpio_strerror(rcp_ep_gpio_errc_t e)
{
    switch (e) {
    case RCP_EP_GPIO_OK:                  return "rcp/ep_gpio: success";
    case RCP_EP_GPIO_ERR_SHORT_FRAME:     return "rcp/ep_gpio: frame too short";
    case RCP_EP_GPIO_ERR_BAD_MSG_TYPE:    return "rcp/ep_gpio: unexpected ACF message type";
    case RCP_EP_GPIO_ERR_WRONG_BUS:       return "rcp/ep_gpio: wrong byte_bus_id";
    case RCP_EP_GPIO_ERR_WRONG_OP:        return "rcp/ep_gpio: wrong ACF op";
    case RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN: return "rcp/ep_gpio: unexpected payload length";
    case RCP_EP_GPIO_ERR_RESERVED_EVT:    return "rcp/ep_gpio: evt[2:0] is the reserved value 100b";
    default:                              return "rcp/ep_gpio: unknown error";
    }
}

//cfusa:req REQ-GPIO-033
//cfusa:req REQ-GPIO-012
rcp_wire_error_t rcp_ep_gpio_wire_error(rcp_ep_gpio_errc_t e)
{
    switch (e) {
    /* TC18 §13.7.4.1: "A request not having exactly four bytes is
     * rejected and an error response with error code = INVALID_PARAMETER
     * will be sent." */
    case RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN: return RCP_ERROR_INVALID_PARAMETER;
    /* TC18 §13.5 Table 33's GPIO/PWM_OUT row, evt[2:0]=100b: "reserved --
     * request shall be ignored and an err-response with error code =
     * UNSUPPORTED_CMD shall be sent." FIXED 2026-08-14, issue #426 --
     * matches src/regmap.c's REQ-RMAP-068 "reserved value ->
     * UNSUPPORTED_CMD" precedent for the same evt[2:0]=100b case. */
    case RCP_EP_GPIO_ERR_RESERVED_EVT:    return RCP_ERROR_UNSUPPORTED_CMD;
    /* RCP_EP_GPIO_OK and the remaining error codes are all local
     * framing/routing outcomes with no numbered wire-error-code
     * counterpart -- see this function's own doc comment (ep_gpio.h). */
    default: return RCP_ERROR_NONE;
    }
}

/* ── Read request ──────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-026
rcp_bytes_t rcp_ep_gpio_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id      = byte_bus_id;
    hdr.op               = RCP_ACF_OP_READ;
    hdr.transaction_num  = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-GPIO-026
//cfusa:req REQ-GPIO-027
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_GPIO_ERR_WRONG_OP;

    (void)payload;
    (void)payload_len; /* a read request carries no payload -- see ep_gpio.h */

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_GPIO_OK;
}

/* ── Write request ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-028
rcp_bytes_t rcp_ep_gpio_encode_write_request(rcp_byte_bus_id_t byte_bus_id, uint32_t bitmask,
                                              rcp_ep_gpio_write_semantics_t evt,
                                              uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                      payload[RCP_EP_GPIO_PAYLOAD_LEN];

    hdr.byte_bus_id      = byte_bus_id;
    hdr.op               = RCP_ACF_OP_WRITE;
    hdr.evt              = (uint8_t)((unsigned)evt & 0x7u);
    hdr.transaction_num  = transaction_num;

    put_u32(payload, bitmask);

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-GPIO-028
//cfusa:req REQ-GPIO-029
//cfusa:req REQ-GPIO-012
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_write_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     uint32_t *out_bitmask, uint8_t *out_evt,
                                                     uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_GPIO_ERR_WRONG_OP;
    if (payload_len != RCP_EP_GPIO_PAYLOAD_LEN) return RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN;
    /* Table 33's own GPIO/PWM_OUT row, evt[2:0]=100b: "reserved -- request
     * shall be ignored and an err-response with error code =
     * UNSUPPORTED_CMD shall be sent" -- the "err-response" half of that
     * two-part rule (rcp_ep_gpio_apply_write() already implements the
     * "ignored" half). FIXED 2026-08-14, issue #426; mirrors
     * src/regmap.c's REQ-RMAP-068 fix for the same evt[2:0]=100b reserved
     * value in a different context. */
    if ((hdr.evt & 0x7u) == (uint8_t)RCP_EP_GPIO_WRITE_RESERVED4) {
        return RCP_EP_GPIO_ERR_RESERVED_EVT;
    }

    *out_bitmask         = get_u32(payload);
    *out_evt             = (uint8_t)(hdr.evt & 0x7u);
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_GPIO_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-030
//cfusa:req REQ-GPIO-031
rcp_bytes_t rcp_ep_gpio_encode_response(rcp_byte_bus_id_t byte_bus_id, uint32_t bitmask,
                                         uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN];

    put_u32(payload, bitmask);

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

//cfusa:req REQ-GPIO-030
//cfusa:req REQ-GPIO-031
//cfusa:req REQ-GPIO-032
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_response(const uint8_t *b, size_t len,
                                                rcp_byte_bus_id_t expected_bus_id,
                                                uint32_t *out_bitmask, bool *out_timed,
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

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_GPIO_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_GPIO_PAYLOAD_LEN) return RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN;

        *out_bitmask = get_u32(payload);
        *out_timed   = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_GPIO_PAYLOAD_LEN) return RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN;

        *out_bitmask   = get_u32(payload);
        *out_timed     = false;
        *out_timestamp = 0u;
    }

    *out_transaction_num = transaction_num;
    return RCP_EP_GPIO_OK;
}
