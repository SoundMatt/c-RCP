#include "rcp/ep_iseled.h"

#include <stdlib.h>
#include <string.h>

/* ── Native 4-bit/5-bit bit framing ──────────────────────────────────────── */

static uint8_t popcount4(uint8_t n)
{
    uint8_t c = 0;
    uint8_t v = (uint8_t)(n & 0x0Fu);

    while (v != 0u) {
        c = (uint8_t)(c + (v & 0x01u));
        v = (uint8_t)(v >> 1);
    }
    return c;
}

//cfusa:req REQ-ISELED-001
uint8_t rcp_ep_iseled_symbol_encode(uint8_t nibble)
{
    uint8_t n      = (uint8_t)(nibble & 0x0Fu);
    uint8_t parity = (uint8_t)(popcount4(n) & 0x01u);

    return (uint8_t)((uint8_t)(parity << 4) | n);
}

//cfusa:req REQ-ISELED-002
bool rcp_ep_iseled_symbol_decode(uint8_t symbol, uint8_t *out_nibble)
{
    uint8_t s            = (uint8_t)(symbol & 0x1Fu);
    uint8_t n            = (uint8_t)(s & 0x0Fu);
    uint8_t parity_bit   = (uint8_t)((s >> 4) & 0x01u);
    uint8_t want_parity  = (uint8_t)(popcount4(n) & 0x01u);

    if (parity_bit != want_parity) return false;

    *out_nibble = n;
    return true;
}

//cfusa:req REQ-ISELED-003
size_t rcp_ep_iseled_bitframe_encoded_len(size_t data_len, bool append_crc)
{
    size_t content_len = data_len + (append_crc ? (size_t)1u : (size_t)0u);

    return content_len * 2u;
}

//cfusa:req REQ-ISELED-004
//cfusa:req REQ-ISELED-005
rcp_bytes_t rcp_ep_iseled_encode_bitframe(const uint8_t *data, size_t data_len, bool append_crc)
{
    rcp_bytes_t out         = {0};
    size_t      content_len = data_len + (append_crc ? (size_t)1u : (size_t)0u);
    size_t      n           = content_len * 2u;
    uint8_t    *b;
    uint8_t     crc = 0;
    size_t      i;

    if (n == 0u) return out;

    b = (uint8_t *)malloc(n);
    if (!b) return out;

    if (append_crc) crc = rcp_ep_iseled_crc8(data, data_len);

    for (i = 0; i < content_len; i++) {
        uint8_t octet = (i < data_len) ? data[i] : crc;

        b[2u * i]      = rcp_ep_iseled_symbol_encode((uint8_t)(octet >> 4));
        b[2u * i + 1u] = rcp_ep_iseled_symbol_encode((uint8_t)(octet & 0x0Fu));
    }

    out.data = b;
    out.len  = n;
    return out;
}

/* ── ISELED-level CRC (distinct from e2e.c; see the file header) ──────────── */

static uint8_t crc8_update(uint8_t crc, uint8_t b)
{
    static const uint8_t poly = 0x07u;
    int                  i;

    crc = (uint8_t)(crc ^ b);
    for (i = 0; i < 8; i++) {
        crc = (crc & 0x80u) ? (uint8_t)((uint8_t)(crc << 1) ^ poly) : (uint8_t)(crc << 1);
    }
    return crc;
}

//cfusa:req REQ-ISELED-006
uint8_t rcp_ep_iseled_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00u;
    size_t  i;

    for (i = 0; i < len; i++) crc = crc8_update(crc, data[i]);
    return crc;
}

/* ── Recovered-clock mode ──────────────────────────────────────────────────── */

//cfusa:req REQ-ISELED-007
bool rcp_ep_iseled_requires_isp_n(bool use_rcv_clk)
{
    return !use_rcv_clk;
}

/* ── Transmission-complete trigger ─────────────────────────────────────────── */

//cfusa:req REQ-ISELED-008
bool rcp_ep_iseled_trigger_fires(rcp_ep_iseled_trigger_t trigger, bool tx_complete_event)
{
    switch (trigger) {
    case RCP_EP_ISELED_TRIGGER_TX_COMPLETE: return tx_complete_event;
    case RCP_EP_ISELED_TRIGGER_NONE:
    default:                                return false;
    }
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-ISELED-009
void rcp_ep_iseled_functional_cfg_init(rcp_ep_iseled_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* iseled_bit_clk_divider is already 0, iseled_use_rcv_clk and
     * iseled_crc_enable are already false, and trigger is already
     * RCP_EP_ISELED_TRIGGER_NONE (0) via the memset above. */
}

//cfusa:req REQ-ISELED-010
bool rcp_ep_iseled_functional_cfg_writable(rcp_server_lifecycle_t state,
                                            rcp_server_writer_ctx_t writer)
{
    return rcp_server_field_writable(state, RCP_SERVER_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-ISELED-011
bool rcp_ep_iseled_set_bit_clk_divider(rcp_ep_iseled_functional_cfg_t *cfg, uint32_t divider,
                                        rcp_server_lifecycle_t state,
                                        rcp_server_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->iseled_bit_clk_divider = divider;
    return true;
}

//cfusa:req REQ-ISELED-012
bool rcp_ep_iseled_set_use_rcv_clk(rcp_ep_iseled_functional_cfg_t *cfg, bool use_rcv_clk,
                                    rcp_server_lifecycle_t state, rcp_server_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->iseled_use_rcv_clk = use_rcv_clk;
    return true;
}

//cfusa:req REQ-ISELED-013
bool rcp_ep_iseled_set_crc_enable(rcp_ep_iseled_functional_cfg_t *cfg, bool enable,
                                   rcp_server_lifecycle_t state, rcp_server_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->iseled_crc_enable = enable;
    return true;
}

//cfusa:req REQ-ISELED-014
bool rcp_ep_iseled_set_trigger(rcp_ep_iseled_functional_cfg_t *cfg,
                                rcp_ep_iseled_trigger_t trigger, rcp_server_lifecycle_t state,
                                rcp_server_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-ISELED-015
const char *rcp_ep_iseled_strerror(rcp_ep_iseled_errc_t e)
{
    switch (e) {
    case RCP_EP_ISELED_OK:                  return "rcp/ep_iseled: success";
    case RCP_EP_ISELED_ERR_SHORT_FRAME:     return "rcp/ep_iseled: frame too short";
    case RCP_EP_ISELED_ERR_BAD_MSG_TYPE:    return "rcp/ep_iseled: unexpected ACF message type";
    case RCP_EP_ISELED_ERR_WRONG_BUS:       return "rcp/ep_iseled: wrong byte_bus_id";
    case RCP_EP_ISELED_ERR_WRONG_OP:        return "rcp/ep_iseled: wrong ACF op";
    case RCP_EP_ISELED_ERR_BAD_SYMBOL:      return "rcp/ep_iseled: invalid bit-framing symbol";
    case RCP_EP_ISELED_ERR_CRC_MISMATCH:    return "rcp/ep_iseled: native ISELED CRC-8 mismatch";
    case RCP_EP_ISELED_ERR_ODD_SYMBOL_COUNT:
        return "rcp/ep_iseled: odd symbol count -- every octet frames to two symbols";
    case RCP_EP_ISELED_ERR_ALLOC:           return "rcp/ep_iseled: allocation failure";
    default:                                return "rcp/ep_iseled: unknown error";
    }
}

/* ── Bit-frame decode ──────────────────────────────────────────────────────── */

//cfusa:req REQ-ISELED-016
//cfusa:req REQ-ISELED-017
//cfusa:req REQ-ISELED-018
//cfusa:req REQ-ISELED-019
//cfusa:req REQ-ISELED-020
rcp_ep_iseled_errc_t rcp_ep_iseled_decode_bitframe(const uint8_t *symbols, size_t symbol_count,
                                                    bool expect_crc, rcp_bytes_t *out_data)
{
    size_t   byte_count;
    uint8_t *bytes = NULL;
    size_t   i;

    out_data->data = NULL;
    out_data->len  = 0;

    if ((symbol_count & (size_t)1u) != 0u) return RCP_EP_ISELED_ERR_ODD_SYMBOL_COUNT;

    byte_count = symbol_count / 2u;
    if (expect_crc && byte_count == 0u) return RCP_EP_ISELED_ERR_SHORT_FRAME;

    if (byte_count > 0u) {
        bytes = (uint8_t *)malloc(byte_count);
        if (!bytes) return RCP_EP_ISELED_ERR_ALLOC;

        for (i = 0; i < byte_count; i++) {
            uint8_t hi, lo;

            if (!rcp_ep_iseled_symbol_decode(symbols[2u * i], &hi) ||
                !rcp_ep_iseled_symbol_decode(symbols[2u * i + 1u], &lo)) {
                free(bytes);
                return RCP_EP_ISELED_ERR_BAD_SYMBOL;
            }
            bytes[i] = (uint8_t)((uint8_t)(hi << 4) | lo);
        }
    }

    if (expect_crc) {
        size_t  data_len = byte_count - 1u;
        uint8_t want     = rcp_ep_iseled_crc8(bytes, data_len);

        if (bytes[byte_count - 1u] != want) {
            free(bytes);
            return RCP_EP_ISELED_ERR_CRC_MISMATCH;
        }

        *out_data = rcp_bytes_dup(bytes, data_len);
        free(bytes);
    } else {
        out_data->data = bytes;
        out_data->len  = byte_count;
    }

    return RCP_EP_ISELED_OK;
}

/* ── Command request ───────────────────────────────────────────────────────── */

//cfusa:req REQ-ISELED-021
rcp_bytes_t rcp_ep_iseled_encode_command_request(rcp_byte_bus_id_t byte_bus_id,
                                                  const uint8_t *tx_data, size_t tx_len,
                                                  uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-ISELED-022
rcp_ep_iseled_errc_t rcp_ep_iseled_decode_command_request(const uint8_t *b, size_t len,
                                                            rcp_byte_bus_id_t expected_bus_id,
                                                            const uint8_t **out_tx_data,
                                                            size_t *out_tx_len,
                                                            uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ISELED_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_ISELED_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_ISELED_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_ISELED_ERR_WRONG_OP;

    /* payload is round-tripped verbatim, byte for byte -- see the file
     * header; this endpoint's own bit-framing happens only when it
     * actually drives ISP_P/ISP_N, never at this ACF layer. */
    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_ISELED_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-ISELED-023
rcp_bytes_t rcp_ep_iseled_encode_response(rcp_byte_bus_id_t byte_bus_id, const uint8_t *rx_data,
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

//cfusa:req REQ-ISELED-024
rcp_ep_iseled_errc_t rcp_ep_iseled_decode_response(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    const uint8_t **out_rx_data,
                                                    size_t *out_rx_len, bool *out_timed,
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
    bool                         timed;
    uint64_t                     timestamp;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK)
        return RCP_EP_ISELED_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ISELED_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_ISELED_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ISELED_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_ISELED_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_ISELED_ERR_WRONG_BUS;

    *out_rx_data         = payload;
    *out_rx_len          = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_ISELED_OK;
}
