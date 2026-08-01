/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_lin.h"

#include <string.h>

/* ── evt[2:0]: the response-generation comparison rule ──────────────────────── */

//cfusa:req REQ-LINEP-001
bool rcp_ep_lin_compare_mode_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_LIN_COMPARE_RESERVED7;
}

//cfusa:req REQ-LINEP-002
//cfusa:req REQ-LINEP-003
//cfusa:req REQ-LINEP-004
//cfusa:req REQ-LINEP-005
bool rcp_ep_lin_compare_fires(rcp_ep_lin_compare_mode_t mode, const uint8_t *request_data,
                               size_t request_len, const uint8_t *rx_data, size_t rx_len)
{
    switch (mode) {
    case RCP_EP_LIN_COMPARE_EXACT:
        if (rx_len != request_len) return false;
        return request_len == 0 || memcmp(rx_data, request_data, request_len) == 0;

    case RCP_EP_LIN_COMPARE_PREFIX:
        if (rx_len < request_len) return false;
        return request_len == 0 || memcmp(rx_data, request_data, request_len) == 0;

    case RCP_EP_LIN_COMPARE_ANY:
        return true;

    case RCP_EP_LIN_COMPARE_NEVER:
    case RCP_EP_LIN_COMPARE_RESERVED4:
    case RCP_EP_LIN_COMPARE_RESERVED5:
    case RCP_EP_LIN_COMPARE_RESERVED6:
    case RCP_EP_LIN_COMPARE_RESERVED7:
    default:
        return false;
    }
}

/* ── Transmission-done trigger ─────────────────────────────────────────────── */

//cfusa:req REQ-LINEP-006
bool rcp_ep_lin_trigger_fires(rcp_ep_lin_trigger_t trigger, bool tx_done_event)
{
    switch (trigger) {
    case RCP_EP_LIN_TRIGGER_TX_DONE: return tx_done_event;
    case RCP_EP_LIN_TRIGGER_NONE:
    default:                         return false;
    }
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-LINEP-007
void rcp_ep_lin_functional_cfg_init(rcp_ep_lin_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* lin_clk_divider is already 0 and trigger is already
     * RCP_EP_LIN_TRIGGER_NONE (0) via the memset above. */
}

//cfusa:req REQ-LINEP-008
//cfusa:req REQ-LINEP-009
//cfusa:req REQ-LINEP-010
bool rcp_ep_lin_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-LINEP-011
//cfusa:req REQ-LINEP-012
bool rcp_ep_lin_set_clk_divider(rcp_ep_lin_functional_cfg_t *cfg, uint32_t lin_clk_divider,
                                 rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_lin_functional_cfg_writable(state, writer)) return false;

    cfg->lin_clk_divider = lin_clk_divider;
    return true;
}

//cfusa:req REQ-LINEP-013
//cfusa:req REQ-LINEP-014
bool rcp_ep_lin_set_trigger(rcp_ep_lin_functional_cfg_t *cfg, rcp_ep_lin_trigger_t trigger,
                             rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_lin_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-LINEP-015
const char *rcp_ep_lin_strerror(rcp_ep_lin_errc_t e)
{
    switch (e) {
    case RCP_EP_LIN_OK:               return "rcp/ep_lin: success";
    case RCP_EP_LIN_ERR_SHORT_FRAME:  return "rcp/ep_lin: frame too short";
    case RCP_EP_LIN_ERR_BAD_MSG_TYPE: return "rcp/ep_lin: unexpected ACF message type";
    case RCP_EP_LIN_ERR_WRONG_BUS:    return "rcp/ep_lin: wrong byte_bus_id";
    case RCP_EP_LIN_ERR_WRONG_OP:     return "rcp/ep_lin: wrong ACF op";
    default:                          return "rcp/ep_lin: unknown error";
    }
}

/* ── Command request ───────────────────────────────────────────────────────── */

//cfusa:req REQ-LINEP-016
rcp_bytes_t rcp_ep_lin_encode_command_request(rcp_byte_bus_id_t byte_bus_id,
                                               const uint8_t *tx_data, size_t tx_len,
                                               rcp_ep_lin_compare_mode_t compare_mode,
                                               uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    /* A LIN command request is a read-direction request: it expects the
     * endpoint to reply with the bytes received on the bus. The wire op
     * bit's read sense is 0 (acf.h's RCP_ACF_OP_READ), and the LIN
     * endpoint's own reply rule is stated in terms of that same read
     * sense -- a reply is sent only for the read direction (extraction
     * §5.10.1, and the general request-handling rule in §3.9.1).
     * Encoding this as a write (op=1) told a conforming peer "no data
     * response expected", i.e. the exact opposite of what the request
     * means. */
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.evt             = (uint8_t)compare_mode;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-LINEP-017
//cfusa:req REQ-LINEP-018
rcp_ep_lin_errc_t rcp_ep_lin_decode_command_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     const uint8_t **out_tx_data,
                                                     size_t *out_tx_len,
                                                     uint8_t *out_compare_mode,
                                                     uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_LIN_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_LIN_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_LIN_ERR_WRONG_BUS;
    /* Read direction -- see rcp_ep_lin_encode_command_request() above. */
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_LIN_ERR_WRONG_OP;

    /* payload is round-tripped verbatim, byte for byte, with no
     * protocol-level LIN-frame parsing of any kind -- see the file
     * header. */
    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
    *out_compare_mode    = hdr.evt & 0x07u;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_LIN_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-LINEP-019
rcp_bytes_t rcp_ep_lin_encode_response(rcp_byte_bus_id_t byte_bus_id, const uint8_t *rx_data,
                                        size_t rx_len, uint8_t transaction_num, bool timed,
                                        uint64_t timestamp)
{
    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.evt             = 0;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, rx_data, rx_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.evt             = 0;
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, rx_data, rx_len);
    }
}

//cfusa:req REQ-LINEP-020
//cfusa:req REQ-LINEP-021
//cfusa:req REQ-LINEP-022
rcp_ep_lin_errc_t rcp_ep_lin_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              const uint8_t **out_rx_data, size_t *out_rx_len,
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
    bool                         timed;
    uint64_t                     timestamp;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_LIN_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_LIN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_LIN_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_LIN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_LIN_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_LIN_ERR_WRONG_BUS;

    *out_rx_data         = payload;
    *out_rx_len          = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_LIN_OK;
}
