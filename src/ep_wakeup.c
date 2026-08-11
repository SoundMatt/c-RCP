/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_wakeup.h"

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

/* ── wup_status latch ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-WAKEUP-005
void rcp_ep_wakeup_wup_status_init(rcp_ep_wakeup_wup_status_t *s)
{
    s->latched = false;
}

//cfusa:req REQ-WAKEUP-006
void rcp_ep_wakeup_wup_status_latch(rcp_ep_wakeup_wup_status_t *s)
{
    s->latched = true;
}

//cfusa:req REQ-WAKEUP-007
void rcp_ep_wakeup_wup_status_clear(rcp_ep_wakeup_wup_status_t *s)
{
    s->latched = false;
}

//cfusa:req REQ-WAKEUP-008
bool rcp_ep_wakeup_wup_status_is_clear(const rcp_ep_wakeup_wup_status_t *s)
{
    return !s->latched;
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
rcp_bytes_t rcp_ep_wakeup_encode_sleepcmd_response(rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_pwrmode_entry_result_t result,
                                                    uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     payload[SLEEPCMD_RESPONSE_PAYLOAD_LEN];

    payload[0] = RCP_EP_WAKEUP_SLEEPCMD_OPCODE;
    payload[1] = (uint8_t)result;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_NONE;
    hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-WAKEUP-013
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
    /* wup_status: only bit 0 is rendered (this module's own single-
     * aggregate-latch simplification -- see the file header). */
    put_u16(&out[RCP_EP_WAKEUP_REG_WUP_STATUS],
            cfg->wup_status.latched ? 0x0001u : 0x0000u);

    for (i = 0; i < RCP_EP_WAKEUP_MAX_SOURCES; i++) {
        const rcp_ep_wakeup_source_cfg_t *src = &cfg->sources[i];
        uint16_t base = (uint16_t)(RCP_EP_WAKEUP_REG_SOURCE_BASE +
                                    (uint16_t)i * RCP_EP_WAKEUP_REG_SOURCE_SPAN);
        uint8_t  io_src;
        uint16_t reg;

        if (!src->enabled) io_src = RCP_EP_WAKEUP_IO_SRC_INACTIVE;
        else io_src = src->active_high ? RCP_EP_WAKEUP_IO_SRC_HIGH_LEVEL
                                        : RCP_EP_WAKEUP_IO_SRC_LOW_LEVEL;

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

    /* write-1-to-clear (TC18 §13.7.2.2's own rule): the written value's
     * bit 0 set clears the latch; any other bit pattern (including 0) is
     * a no-op -- writing 0 does not itself set the latch, matching the
     * pre-existing rcp_ep_wakeup_wup_status_t API's own semantics (only
     * a real wake-source assertion calls _latch()). */
    wup = get_u16(&in[RCP_EP_WAKEUP_REG_WUP_STATUS]);
    if ((wup & 0x0001u) != 0u) rcp_ep_wakeup_wup_status_clear(&cfg->wup_status);

    for (i = 0; i < RCP_EP_WAKEUP_MAX_SOURCES; i++) {
        rcp_ep_wakeup_source_cfg_t *src = &cfg->sources[i];
        uint16_t base = (uint16_t)(RCP_EP_WAKEUP_REG_SOURCE_BASE +
                                    (uint16_t)i * RCP_EP_WAKEUP_REG_SOURCE_SPAN);
        uint16_t reg  = get_u16(&in[base]);
        uint8_t  io_src = (uint8_t)((reg >> 11) & 0x1Fu);

        src->pin_number = (uint16_t)(reg & 0x07FFu);

        switch (io_src) {
        case RCP_EP_WAKEUP_IO_SRC_INACTIVE:
            src->enabled = false;
            break;
        case RCP_EP_WAKEUP_IO_SRC_HIGH_LEVEL:
            src->enabled     = true;
            src->active_high = true;
            break;
        case RCP_EP_WAKEUP_IO_SRC_LOW_LEVEL:
            src->enabled     = true;
            src->active_high = false;
            break;
        default:
            /* Edge-triggered (0x01-0x03) or reserved (0x06-0x1F): this
             * module cannot represent it -- enabled/active_high are left
             * exactly as they were, an honest "cannot apply" rather than
             * a silently wrong reinterpretation as a level mode. See the
             * file header's own register-block note. */
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

    payload = (uint8_t *)malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    memcpy(payload + RCP_EP_WAKEUP_RECONFIG_ADDR_LEN, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0x7u; /* evt[2:0] == 111b, §12.7.1 */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    free(payload);
    return frame;
}
