/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_wakeup.h"

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

/* ── Wake-source pin configuration/monitoring ────────────────────────────────── */

//cfusa:req REQ-WAKEUP-001
void rcp_ep_wakeup_functional_cfg_init(rcp_ep_wakeup_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
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
