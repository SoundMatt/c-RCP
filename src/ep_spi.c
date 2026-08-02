/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_spi.h"

#include <string.h>

/* ── Channel addressing ────────────────────────────────────────────────────── */

//cfusa:req REQ-SPI-002
bool rcp_ep_spi_channel_valid(uint8_t channel)
{
    return channel < RCP_EP_SPI_MAX_CHANNELS;
}

/* ── Clock mode: the 4 standard CPOL/CPHA combinations ─────────────────────── */

//cfusa:req REQ-SPI-003
bool rcp_ep_spi_mode_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_SPI_MODE_3;
}

//cfusa:req REQ-SPI-004
bool rcp_ep_spi_mode_cpol(rcp_ep_spi_mode_t mode)
{
    switch (mode) {
    case RCP_EP_SPI_MODE_2:
    case RCP_EP_SPI_MODE_3: return true;
    case RCP_EP_SPI_MODE_0:
    case RCP_EP_SPI_MODE_1:
    default:                return false;
    }
}

//cfusa:req REQ-SPI-005
bool rcp_ep_spi_mode_cpha(rcp_ep_spi_mode_t mode)
{
    switch (mode) {
    case RCP_EP_SPI_MODE_1:
    case RCP_EP_SPI_MODE_3: return true;
    case RCP_EP_SPI_MODE_0:
    case RCP_EP_SPI_MODE_2:
    default:                return false;
    }
}

/* ── Per-channel trigger signals ────────────────────────────────────────────── */

//cfusa:req REQ-SPI-006
//cfusa:req REQ-SPI-007
//cfusa:req REQ-SPI-008
//cfusa:req REQ-SPI-009
bool rcp_ep_spi_trigger_fires(rcp_ep_spi_trigger_t trigger, rcp_ep_spi_event_t event)
{
    switch (trigger) {
    case RCP_EP_SPI_TRIGGER_TRANSFER_DONE: return event == RCP_EP_SPI_EVENT_TRANSFER_DONE;
    case RCP_EP_SPI_TRIGGER_CS_ASSERT:     return event == RCP_EP_SPI_EVENT_CS_ASSERT;
    case RCP_EP_SPI_TRIGGER_CS_DEASSERT:   return event == RCP_EP_SPI_EVENT_CS_DEASSERT;
    case RCP_EP_SPI_TRIGGER_NONE:
    default:                               return false;
    }
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-SPI-010
void rcp_ep_spi_functional_cfg_init(rcp_ep_spi_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* Every channels[i].mode is already RCP_EP_SPI_MODE_0 (0), bit_order
     * RCP_EP_SPI_BIT_ORDER_MSB_FIRST (0), cs_polarity
     * RCP_EP_SPI_CS_ACTIVE_LOW (0), and trigger RCP_EP_SPI_TRIGGER_NONE (0)
     * via the memset above. */
}

//cfusa:req REQ-SPI-011
//cfusa:req REQ-SPI-012
//cfusa:req REQ-SPI-013
bool rcp_ep_spi_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-SPI-014
//cfusa:req REQ-SPI-015
bool rcp_ep_spi_set_channel_mode(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                  rcp_ep_spi_mode_t mode, rcp_lifecycle_state_t state,
                                  rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_spi_channel_valid(channel)) return false;
    if (!rcp_ep_spi_functional_cfg_writable(state, writer)) return false;

    cfg->channels[channel].mode = (uint8_t)mode;
    return true;
}

//cfusa:req REQ-SPI-016
//cfusa:req REQ-SPI-017
bool rcp_ep_spi_set_channel_bit_order(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                       rcp_ep_spi_bit_order_t bit_order,
                                       rcp_lifecycle_state_t state,
                                       rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_spi_channel_valid(channel)) return false;
    if (!rcp_ep_spi_functional_cfg_writable(state, writer)) return false;

    cfg->channels[channel].bit_order = (uint8_t)bit_order;
    return true;
}

//cfusa:req REQ-SPI-018
//cfusa:req REQ-SPI-019
bool rcp_ep_spi_set_channel_cs_polarity(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                         rcp_ep_spi_cs_polarity_t cs_polarity,
                                         rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_spi_channel_valid(channel)) return false;
    if (!rcp_ep_spi_functional_cfg_writable(state, writer)) return false;

    cfg->channels[channel].cs_polarity = (uint8_t)cs_polarity;
    return true;
}

//cfusa:req REQ-SPI-020
//cfusa:req REQ-SPI-021
bool rcp_ep_spi_set_channel_clock_divider(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                           uint32_t clock_divider, rcp_lifecycle_state_t state,
                                           rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_spi_channel_valid(channel)) return false;
    if (!rcp_ep_spi_functional_cfg_writable(state, writer)) return false;

    cfg->channels[channel].clock_divider = clock_divider;
    return true;
}

//cfusa:req REQ-SPI-022
//cfusa:req REQ-SPI-023
bool rcp_ep_spi_set_channel_timing(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                    uint32_t inter_byte_delay_ns,
                                    uint32_t inter_transfer_delay_ns,
                                    rcp_lifecycle_state_t state,
                                    rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_spi_channel_valid(channel)) return false;
    if (!rcp_ep_spi_functional_cfg_writable(state, writer)) return false;

    cfg->channels[channel].inter_byte_delay_ns     = inter_byte_delay_ns;
    cfg->channels[channel].inter_transfer_delay_ns = inter_transfer_delay_ns;
    return true;
}

//cfusa:req REQ-SPI-024
//cfusa:req REQ-SPI-025
bool rcp_ep_spi_set_channel_trigger(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                     rcp_ep_spi_trigger_t trigger, rcp_lifecycle_state_t state,
                                     rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_spi_channel_valid(channel)) return false;
    if (!rcp_ep_spi_functional_cfg_writable(state, writer)) return false;

    cfg->channels[channel].trigger = (uint8_t)trigger;
    return true;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-SPI-001
const char *rcp_ep_spi_strerror(rcp_ep_spi_errc_t e)
{
    switch (e) {
    case RCP_EP_SPI_OK:               return "rcp/ep_spi: success";
    case RCP_EP_SPI_ERR_SHORT_FRAME:  return "rcp/ep_spi: frame too short";
    case RCP_EP_SPI_ERR_BAD_MSG_TYPE: return "rcp/ep_spi: unexpected ACF message type";
    case RCP_EP_SPI_ERR_WRONG_BUS:    return "rcp/ep_spi: wrong byte_bus_id";
    case RCP_EP_SPI_ERR_WRONG_OP:     return "rcp/ep_spi: wrong ACF op";
    case RCP_EP_SPI_ERR_BAD_CHANNEL:  return "rcp/ep_spi: invalid channel selector";
    default:                          return "rcp/ep_spi: unknown error";
    }
}

/* ── Transfer request ──────────────────────────────────────────────────────── */

//cfusa:req REQ-SPI-026
rcp_bytes_t rcp_ep_spi_encode_transfer_request(rcp_byte_bus_id_t byte_bus_id, uint8_t channel,
                                                const uint8_t *tx_data, size_t tx_len,
                                                uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    /* An SPI transfer request is a read-direction request: the payload
     * goes out on PICO and the endpoint replies with what came back on
     * POCI. The wire op bit's read sense is 0 (acf.h's RCP_ACF_OP_READ),
     * and the specification's own worked SPI transfer example -- "write
     * N bytes and get a response with M" -- carries exactly that
     * encoding, op=0 with a non-zero read_size (extraction §5.3.3, and
     * the general request-handling rule in §3.9.1). Encoding this as a
     * write (op=1) told a conforming peer "no data response expected",
     * i.e. the exact opposite of what the request means. */
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.evt             = (uint8_t)(channel & 0x7u);
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-SPI-026
//cfusa:req REQ-SPI-027
rcp_ep_spi_errc_t rcp_ep_spi_decode_transfer_request(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      uint8_t *out_channel,
                                                      const uint8_t **out_tx_data,
                                                      size_t *out_tx_len,
                                                      uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    uint8_t                      channel;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_SPI_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_SPI_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_SPI_ERR_WRONG_BUS;
    /* Read direction -- see rcp_ep_spi_encode_transfer_request() above. */
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_SPI_ERR_WRONG_OP;

    channel = (uint8_t)(hdr.evt & 0x7u);
    if (!rcp_ep_spi_channel_valid(channel)) return RCP_EP_SPI_ERR_BAD_CHANNEL;

    *out_channel         = channel;
    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_SPI_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-SPI-028
//cfusa:req REQ-SPI-029
rcp_bytes_t rcp_ep_spi_encode_response(rcp_byte_bus_id_t byte_bus_id, uint8_t channel,
                                        const uint8_t *rx_data, size_t rx_len,
                                        uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.evt             = (uint8_t)(channel & 0x7u);
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, rx_data, rx_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.evt             = (uint8_t)(channel & 0x7u);
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, rx_data, rx_len);
    }
}

//cfusa:req REQ-SPI-028
//cfusa:req REQ-SPI-029
//cfusa:req REQ-SPI-030
rcp_ep_spi_errc_t rcp_ep_spi_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              uint8_t *out_channel,
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
    uint8_t                      evt;
    uint8_t                      transaction_num;
    uint8_t                      channel;
    bool                         timed;
    uint64_t                     timestamp;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_SPI_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_SPI_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_SPI_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        evt             = gbb_hdr.info.evt;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_SPI_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_SPI_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        evt             = abb_hdr.evt;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_SPI_ERR_WRONG_BUS;

    channel = (uint8_t)(evt & 0x7u);
    if (!rcp_ep_spi_channel_valid(channel)) return RCP_EP_SPI_ERR_BAD_CHANNEL;

    *out_channel         = channel;
    *out_rx_data         = payload;
    *out_rx_len          = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_SPI_OK;
}
