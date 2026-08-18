/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_wakeup.h"
#include "rcp/alloc.h"

#include <stdlib.h>
#include <string.h>

/* ── Wire-layout constants (this module's own choice; see the file header) ── */

/* REQ-WAKEUP-010/011 (corrected 2026-08-10, c-RCP-AUDIT-06, issue #256
 * Group E): the request payload is 1 byte (opcode only, padded to the
 * next quadlet by rcp_acf_encode_abb() itself) -- TC18 §13.7.2.3 Figure
 * 22 shows no second payload byte at all; the response's own 2-byte
 * (opcode + result) layout remains this module's own original design
 * (TC18 defines no response wire format), so it keeps its own constant. */
#define SLEEPCMD_REQUEST_PAYLOAD_LEN  ((size_t)1u) /* opcode(1) */
#define SLEEPCMD_RESPONSE_PAYLOAD_LEN ((size_t)2u) /* opcode(1) + result(1) */
#define WAKEUP_PAYLOAD_LEN            ((size_t)1u) /* opcode(1) */
#define WAKEUP_WITH_SOURCE_PAYLOAD_LEN ((size_t)3u) /* opcode(1) + source(1) + source_index(1) --
                                                         REQ-WAKEUP-017 */

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/ep_mdio.c's
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

/* ── Wake-source pin configuration/monitoring ────────────────────────────────── */

//cfusa:req REQ-WAKEUP-001
void rcp_ep_wakeup_functional_cfg_init(rcp_ep_wakeup_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    rcp_ep_wakeup_wup_status_init(&cfg->wup_status);
}

//cfusa:req REQ-WAKEUP-002
bool rcp_ep_wakeup_functional_cfg_writable(rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-PWRMODE-023
bool rcp_ep_wakeup_sleepcmd_writable(rcp_lifecycle_writer_ctx_t writer)
{
    return writer.via_root_client_ep0;
}

//cfusa:req REQ-WAKEUP-003
bool rcp_ep_wakeup_source_asserted(rcp_ep_wakeup_source_cfg_t cfg, bool pin_level)
{
    return cfg.enabled && (pin_level == cfg.active_high);
}

//cfusa:req REQ-WAKEUP-004
bool rcp_ep_wakeup_any_source_asserted(const rcp_ep_wakeup_functional_cfg_t *fcfg,
                                        const bool *pin_levels, size_t pin_level_count)
{
    size_t n;
    size_t i;

    if (!fcfg) return false;
    if (pin_level_count > 0u && !pin_levels) return false;

    n = pin_level_count < RCP_EP_WAKEUP_MAX_SOURCES ? pin_level_count : RCP_EP_WAKEUP_MAX_SOURCES;

    for (i = 0; i < n; i++) {
        if (rcp_ep_wakeup_source_asserted(fcfg->sources[i], pin_levels[i])) return true;
    }
    return false;
}

/* ── Edge-triggered wake-source detection (REQ-WAKEUP-022) ──────────────────── */

//cfusa:req REQ-WAKEUP-022
void rcp_ep_wakeup_source_edge_state_init(rcp_ep_wakeup_source_edge_state_t *s)
{
    s->has_previous   = false;
    s->previous_level = false;
}

//cfusa:req REQ-WAKEUP-022
bool rcp_ep_wakeup_source_edge_asserted(rcp_ep_wakeup_source_cfg_t cfg,
                                         rcp_ep_wakeup_source_edge_state_t *state,
                                         bool pin_level)
{
    bool rose, fell, fired;

    if (!cfg.trigger_on_rising_edge && !cfg.trigger_on_falling_edge) {
        /* LEVEL mode: delegate, state untouched. */
        return rcp_ep_wakeup_source_asserted(cfg, pin_level);
    }

    if (!state->has_previous) {
        /* First observation for this slot only seeds previous_level --
         * never fires, avoiding a false-positive edge from an
         * arbitrary/unknown starting level. */
        state->has_previous   = true;
        state->previous_level = pin_level;
        return false;
    }

    rose = !state->previous_level && pin_level;
    fell = state->previous_level && !pin_level;
    fired = cfg.enabled &&
            ((cfg.trigger_on_rising_edge && rose) || (cfg.trigger_on_falling_edge && fell));

    state->previous_level = pin_level; /* every call updates state, fired or not */
    return fired;
}

//cfusa:req REQ-WAKEUP-022
bool rcp_ep_wakeup_any_source_edge_asserted(const rcp_ep_wakeup_functional_cfg_t *fcfg,
                                             rcp_ep_wakeup_source_edge_state_t *states,
                                             const bool *pin_levels, size_t pin_level_count)
{
    size_t n;
    size_t i;
    bool   any = false;

    if (!fcfg) return false;
    if (!states) return false;
    if (pin_level_count > 0u && !pin_levels) return false;

    n = pin_level_count < RCP_EP_WAKEUP_MAX_SOURCES ? pin_level_count : RCP_EP_WAKEUP_MAX_SOURCES;

    /* Deliberately does NOT short-circuit -- every in-range source's own
     * state must be updated on every call (see this function's own doc
     * comment, ep_wakeup.h). */
    for (i = 0; i < n; i++) {
        if (rcp_ep_wakeup_source_edge_asserted(fcfg->sources[i], &states[i], pin_levels[i])) {
            any = true;
        }
    }
    return any;
}

/* ── wup_status latch ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-WAKEUP-005
void rcp_ep_wakeup_wup_status_init(rcp_ep_wakeup_wup_status_t *s)
{
    s->mask = 0;
}

//cfusa:req REQ-WAKEUP-006
//cfusa:req REQ-WAKEUP-021
void rcp_ep_wakeup_wup_status_latch_source(rcp_ep_wakeup_wup_status_t *s, size_t source_index)
{
    if (source_index >= RCP_EP_WAKEUP_MAX_SOURCES) return;
    s->mask = (uint16_t)(s->mask | (uint16_t)(1u << source_index));
}

//cfusa:req REQ-WAKEUP-007
void rcp_ep_wakeup_wup_status_clear(rcp_ep_wakeup_wup_status_t *s)
{
    s->mask = 0;
}

//cfusa:req REQ-WAKEUP-021
void rcp_ep_wakeup_wup_status_clear_source(rcp_ep_wakeup_wup_status_t *s, size_t source_index)
{
    if (source_index >= RCP_EP_WAKEUP_MAX_SOURCES) return;
    s->mask = (uint16_t)(s->mask & (uint16_t)~(1u << source_index));
}

//cfusa:req REQ-WAKEUP-008
bool rcp_ep_wakeup_wup_status_is_clear(const rcp_ep_wakeup_wup_status_t *s)
{
    return s->mask == 0;
}

//cfusa:req REQ-WAKEUP-021
bool rcp_ep_wakeup_wup_status_source_is_latched(const rcp_ep_wakeup_wup_status_t *s,
                                                  size_t source_index)
{
    if (source_index >= RCP_EP_WAKEUP_MAX_SOURCES) return false;
    return (s->mask & (uint16_t)(1u << source_index)) != 0u;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-WAKEUP-009
const char *rcp_ep_wakeup_strerror(rcp_ep_wakeup_errc_t e)
{
    switch (e) {
    case RCP_EP_WAKEUP_OK:                  return "rcp/ep_wakeup: success";
    case RCP_EP_WAKEUP_ERR_SHORT_FRAME:     return "rcp/ep_wakeup: frame too short";
    case RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE:    return "rcp/ep_wakeup: unexpected ACF message type";
    case RCP_EP_WAKEUP_ERR_WRONG_BUS:       return "rcp/ep_wakeup: wrong byte_bus_id";
    case RCP_EP_WAKEUP_ERR_BAD_OPCODE:      return "rcp/ep_wakeup: wrong fixed opcode byte";
    default:                                return "rcp/ep_wakeup: unknown error";
    }
}

/* ── SleepCMD request/response (0xA5) ────────────────────────────────────────── */

//cfusa:req REQ-WAKEUP-010
rcp_bytes_t rcp_ep_wakeup_encode_sleepcmd_request(rcp_byte_bus_id_t byte_bus_id,
                                                   uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr   = {0};
    uint8_t                     payload[SLEEPCMD_REQUEST_PAYLOAD_LEN];

    payload[0] = RCP_EP_WAKEUP_SLEEPCMD_OPCODE;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-WAKEUP-011
rcp_ep_wakeup_errc_t rcp_ep_wakeup_decode_sleepcmd_request(const uint8_t *b, size_t len,
                                                            rcp_byte_bus_id_t expected_bus_id,
                                                            uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_WAKEUP_ERR_WRONG_BUS;
    if (payload_len < SLEEPCMD_REQUEST_PAYLOAD_LEN) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (payload[0] != RCP_EP_WAKEUP_SLEEPCMD_OPCODE) return RCP_EP_WAKEUP_ERR_BAD_OPCODE;

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_WAKEUP_OK;
}

//cfusa:req REQ-WAKEUP-012
//cfusa:req REQ-WAKEUP-019
rcp_bytes_t rcp_ep_wakeup_encode_sleepcmd_response(rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_pwrmode_entry_result_t result,
                                                    uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload[SLEEPCMD_RESPONSE_PAYLOAD_LEN];

    if (result == RCP_PWRMODE_ENTRY_REFUSED) {
        /* REQ-WAKEUP-019 (TC18 §12.5): a refused standby/sleep entry is
         * signalled with "an error message with error code =
         * REQUEST_CANCELED", not this message's own positive-form
         * SleepCMD-opcode-plus-result-byte payload -- see this
         * function's own doc comment. */
        return rcp_acf_build_error_response(byte_bus_id, transaction_num,
                                             RCP_ERROR_REQUEST_CANCELED);
    }

    payload[0] = RCP_EP_WAKEUP_SLEEPCMD_OPCODE;
    payload[1] = (uint8_t)result;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-WAKEUP-013
//cfusa:req REQ-WAKEUP-019
rcp_ep_wakeup_errc_t rcp_ep_wakeup_decode_sleepcmd_response(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t expected_bus_id,
                                                             rcp_pwrmode_entry_result_t *out_result,
                                                             uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_WAKEUP_ERR_WRONG_BUS;

    if (hdr.err) {
        /* REQ-WAKEUP-019: the refused-entry half of this pair now
         * arrives as a genuine ACF Error Response (see the encode
         * side's own doc comment) -- recognize the specific
         * REQUEST_CANCELED code that response's own contract defines.
         * Any other err code was never built by this function's own
         * encode counterpart, so it is not this decoder's concern. */
        if (payload_len < 1u || payload[0] != (uint8_t)RCP_ERROR_REQUEST_CANCELED) {
            return RCP_EP_WAKEUP_ERR_BAD_OPCODE;
        }
        *out_result           = RCP_PWRMODE_ENTRY_REFUSED;
        *out_transaction_num  = hdr.transaction_num;
        return RCP_EP_WAKEUP_OK;
    }

    if (payload_len < SLEEPCMD_RESPONSE_PAYLOAD_LEN) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (payload[0] != RCP_EP_WAKEUP_SLEEPCMD_OPCODE) return RCP_EP_WAKEUP_ERR_BAD_OPCODE;

    *out_result = (payload[1] == (uint8_t)RCP_PWRMODE_ENTRY_OK) ? RCP_PWRMODE_ENTRY_OK
                                                                  : RCP_PWRMODE_ENTRY_REFUSED;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_WAKEUP_OK;
}

/* ── WakeUp-message emission ─────────────────────────────────────────────────── */

//cfusa:req REQ-WAKEUP-014
rcp_bytes_t rcp_ep_wakeup_encode_wakeup_message(rcp_byte_bus_id_t byte_bus_id,
                                                 uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload[WAKEUP_PAYLOAD_LEN];

    payload[0] = RCP_EP_WAKEUP_WAKEUP_OPCODE;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-WAKEUP-015
rcp_ep_wakeup_errc_t rcp_ep_wakeup_decode_wakeup_message(const uint8_t *b, size_t len,
                                                          rcp_byte_bus_id_t expected_bus_id,
                                                          uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_WAKEUP_ERR_WRONG_BUS;
    if (payload_len < WAKEUP_PAYLOAD_LEN) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (payload[0] != RCP_EP_WAKEUP_WAKEUP_OPCODE) return RCP_EP_WAKEUP_ERR_BAD_OPCODE;

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_WAKEUP_OK;
}

//cfusa:req REQ-WAKEUP-016
bool rcp_ep_wakeup_is_wakeup_echo(const uint8_t *b, size_t len, rcp_byte_bus_id_t expected_bus_id,
                                   uint8_t sent_transaction_num)
{
    uint8_t              transaction_num;
    rcp_ep_wakeup_errc_t rc;

    rc = rcp_ep_wakeup_decode_wakeup_message(b, len, expected_bus_id, &transaction_num);
    if (rc != RCP_EP_WAKEUP_OK) return false;

    return transaction_num == sent_transaction_num;
}

//cfusa:req REQ-WAKEUP-017
rcp_bytes_t rcp_ep_wakeup_encode_wakeup_message_with_source(rcp_byte_bus_id_t byte_bus_id,
                                                              uint8_t transaction_num,
                                                              rcp_ep_wakeup_source_t source,
                                                              uint8_t source_index)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload[WAKEUP_WITH_SOURCE_PAYLOAD_LEN];

    payload[0] = RCP_EP_WAKEUP_WAKEUP_OPCODE;
    payload[1] = (uint8_t)source;
    payload[2] = source_index;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-WAKEUP-017
rcp_ep_wakeup_errc_t
rcp_ep_wakeup_decode_wakeup_message_with_source(const uint8_t *b, size_t len,
                                                 rcp_byte_bus_id_t expected_bus_id,
                                                 uint8_t *out_transaction_num,
                                                 rcp_ep_wakeup_source_t *out_source,
                                                 uint8_t *out_source_index)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_WAKEUP_ERR_WRONG_BUS;
    if (payload_len < WAKEUP_WITH_SOURCE_PAYLOAD_LEN) return RCP_EP_WAKEUP_ERR_SHORT_FRAME;
    if (payload[0] != RCP_EP_WAKEUP_WAKEUP_OPCODE) return RCP_EP_WAKEUP_ERR_BAD_OPCODE;

    switch (payload[1]) {
    case (uint8_t)RCP_EP_WAKEUP_SOURCE_UNKNOWN:
    case (uint8_t)RCP_EP_WAKEUP_SOURCE_IO:
    case (uint8_t)RCP_EP_WAKEUP_SOURCE_WAKEPIN:
    case (uint8_t)RCP_EP_WAKEUP_SOURCE_NETWORK:
        break;
    default:
        return RCP_EP_WAKEUP_ERR_BAD_OPCODE;
    }

    *out_transaction_num = hdr.transaction_num;
    *out_source          = (rcp_ep_wakeup_source_t)payload[1];
    *out_source_index    = payload[2];
    return RCP_EP_WAKEUP_OK;
}

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

//cfusa:req REQ-WAKEUP-021
//cfusa:req REQ-WAKEUP-022
void rcp_ep_wakeup_render_registers(const rcp_ep_wakeup_functional_cfg_t *cfg,
                                     uint8_t out[RCP_EP_WAKEUP_EP_FUNC_LEN])
{
    size_t i;

    out[RCP_EP_WAKEUP_REG_EP_LEN]         = (uint8_t)RCP_EP_WAKEUP_EP_FUNC_LEN;
    out[RCP_EP_WAKEUP_REG_NR_IO_PINS_MAX] = (uint8_t)RCP_EP_WAKEUP_MAX_SOURCES;
    put_u16(&out[RCP_EP_WAKEUP_REG_EP_STATUS], cfg->ep_status);
    /* wup_status: the full per-source bitmask (REQ-WAKEUP-021) -- bits
     * [15:RCP_EP_WAKEUP_MAX_SOURCES] are masked off since this module
     * never latches a source index that high (see the file header). */
    put_u16(&out[RCP_EP_WAKEUP_REG_WUP_STATUS],
            (uint16_t)(cfg->wup_status.mask & ((1u << RCP_EP_WAKEUP_MAX_SOURCES) - 1u)));

    for (i = 0; i < RCP_EP_WAKEUP_MAX_SOURCES; i++) {
        const rcp_ep_wakeup_source_cfg_t *src = &cfg->sources[i];
        uint16_t base = (uint16_t)(RCP_EP_WAKEUP_REG_SOURCE_BASE +
                                    (uint16_t)i * RCP_EP_WAKEUP_REG_SOURCE_SPAN);
        uint8_t  io_src;
        uint16_t reg;

        /* REQ-WAKEUP-022: EDGE mode (either trigger bit set) takes
         * precedence over the LEVEL-mode enabled/active_high pair --
         * see rcp_ep_wakeup_source_cfg_t's own doc comment for this
         * precedence rule. */
        if (src->trigger_on_rising_edge && src->trigger_on_falling_edge) {
            io_src = RCP_EP_WAKEUP_IO_SRC_BOTH_EDGES;
        } else if (src->trigger_on_rising_edge) {
            io_src = RCP_EP_WAKEUP_IO_SRC_RISING_EDGE;
        } else if (src->trigger_on_falling_edge) {
            io_src = RCP_EP_WAKEUP_IO_SRC_FALLING_EDGE;
        } else if (!src->enabled) {
            io_src = RCP_EP_WAKEUP_IO_SRC_INACTIVE;
        } else {
            io_src = src->active_high ? RCP_EP_WAKEUP_IO_SRC_HIGH_LEVEL
                                       : RCP_EP_WAKEUP_IO_SRC_LOW_LEVEL;
        }

        reg = (uint16_t)(((uint16_t)(io_src & 0x1Fu) << 11) |
                          (src->pin_number & 0x07FFu));
        put_u16(&out[base], reg);
    }
}

/* The inverse of render: adopts every R/W register from an already patched
 * block image. The read-only offsets (EP_LEN, NR_IO_PINS_MAX) are
 * deliberately not read back -- rcp_ep_wakeup_apply_reconfig() re-renders
 * them from cfg before patching, so a write covering them is a no-op. */
static void parse_wakeup_registers(rcp_ep_wakeup_functional_cfg_t *cfg,
                                    const uint8_t in[RCP_EP_WAKEUP_EP_FUNC_LEN])
{
    uint16_t wup;
    size_t   i;

    cfg->ep_status = get_u16(&in[RCP_EP_WAKEUP_REG_EP_STATUS]);

    /* write-1-to-clear, per bit (TC18 §13.7.2.2's own rule, REQ-WAKEUP-021):
     * each wire bit set to 1 clears that SAME bit's own source in the
     * mask, independently of every other bit -- a write naming only some
     * sources clears only those, leaving the rest latched exactly as
     * TC18's own per-bit register semantics require. A written bit that
     * is 0 is a no-op for that source (does not itself latch it -- only
     * a real wake-source assertion calls _latch_source()). */
    wup = get_u16(&in[RCP_EP_WAKEUP_REG_WUP_STATUS]);
    for (i = 0; i < RCP_EP_WAKEUP_MAX_SOURCES; i++) {
        if ((wup & (uint16_t)(1u << i)) != 0u) {
            rcp_ep_wakeup_wup_status_clear_source(&cfg->wup_status, i);
        }
    }

    for (i = 0; i < RCP_EP_WAKEUP_MAX_SOURCES; i++) {
        rcp_ep_wakeup_source_cfg_t *src = &cfg->sources[i];
        uint16_t base = (uint16_t)(RCP_EP_WAKEUP_REG_SOURCE_BASE +
                                    (uint16_t)i * RCP_EP_WAKEUP_REG_SOURCE_SPAN);
        uint16_t reg  = get_u16(&in[base]);
        uint8_t  io_src = (uint8_t)((reg >> 11) & 0x1Fu);

        src->pin_number = (uint16_t)(reg & 0x07FFu);

        switch (io_src) {
        case RCP_EP_WAKEUP_IO_SRC_INACTIVE:
            src->enabled                = false;
            src->trigger_on_rising_edge  = false;
            src->trigger_on_falling_edge = false;
            break;
        case RCP_EP_WAKEUP_IO_SRC_RISING_EDGE:
            src->enabled                 = true;
            src->trigger_on_rising_edge  = true;
            src->trigger_on_falling_edge = false;
            break;
        case RCP_EP_WAKEUP_IO_SRC_FALLING_EDGE:
            src->enabled                 = true;
            src->trigger_on_rising_edge  = false;
            src->trigger_on_falling_edge = true;
            break;
        case RCP_EP_WAKEUP_IO_SRC_BOTH_EDGES:
            src->enabled                 = true;
            src->trigger_on_rising_edge  = true;
            src->trigger_on_falling_edge = true;
            break;
        case RCP_EP_WAKEUP_IO_SRC_HIGH_LEVEL:
            src->enabled                 = true;
            src->active_high             = true;
            src->trigger_on_rising_edge  = false;
            src->trigger_on_falling_edge = false;
            break;
        case RCP_EP_WAKEUP_IO_SRC_LOW_LEVEL:
            src->enabled                 = true;
            src->active_high             = false;
            src->trigger_on_rising_edge  = false;
            src->trigger_on_falling_edge = false;
            break;
        default:
            /* Reserved (0x06-0x1F): this module cannot represent it --
             * enabled/active_high/trigger_on_*_edge are left exactly as
             * they were, an honest "cannot apply" rather than a silently
             * wrong reinterpretation. See the file header's own
             * register-block note. */
            break;
        }
    }
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN or NR_IO_PINS_MAX. */
static bool wakeup_reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_WAKEUP_REG_EP_LEN ||
           addr == RCP_EP_WAKEUP_REG_NR_IO_PINS_MAX;
}

//cfusa:req REQ-WAKEUP-021
const char *rcp_ep_wakeup_reconfig_strerror(rcp_ep_wakeup_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_WAKEUP_RECONFIG_OK:
        return "rcp/ep_wakeup: configuration write applied";
    case RCP_EP_WAKEUP_RECONFIG_ERR_SHORT:
        return "rcp/ep_wakeup: configuration write has no address and data";
    case RCP_EP_WAKEUP_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_wakeup: configuration write extends past the EP_func block";
    default:
        return "rcp/ep_wakeup: unknown configuration-write error";
    }
}

//cfusa:req REQ-WAKEUP-021
//cfusa:req REQ-WAKEUP-022
rcp_ep_wakeup_reconfig_errc_t rcp_ep_wakeup_apply_reconfig(rcp_ep_wakeup_functional_cfg_t *cfg,
                                                            const uint8_t *payload,
                                                            size_t payload_len)
{
    uint8_t  block[RCP_EP_WAKEUP_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_WAKEUP_RECONFIG_ADDR_LEN) {
        return RCP_EP_WAKEUP_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_WAKEUP_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (§12.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_WAKEUP_EP_FUNC_LEN) {
        return RCP_EP_WAKEUP_RECONFIG_ERR_OUT_OF_RANGE;
    }

    rcp_ep_wakeup_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (wakeup_reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_WAKEUP_RECONFIG_ADDR_LEN + i];
    }
    parse_wakeup_registers(cfg, block);

    return RCP_EP_WAKEUP_RECONFIG_OK;
}

//cfusa:req REQ-WAKEUP-021
rcp_bytes_t rcp_ep_wakeup_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                   uint16_t start_address, const uint8_t *data,
                                                   size_t data_len, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr   = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                     *payload;
    size_t                       payload_len;
    rcp_bytes_t                  frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_WAKEUP_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)rcp_malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    memcpy(payload + RCP_EP_WAKEUP_RECONFIG_ADDR_LEN, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0x7u; /* evt[2:0] == 111b, §12.7.1 */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    rcp_free(payload);
    payload = NULL;
    return frame;
}
