/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_spi.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/ep_pwm.c's/
 * ep_gpio.c's house convention of not sharing a byte-order util across
 * modules) ──────────────────────────────────────────────────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

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
     * RCP_EP_SPI_CS_ACTIVE_LOW (0), trigger RCP_EP_SPI_TRIGGER_NONE (0),
     * use_common_cs false (this channel's own CSN), and every numeric
     * timing/rate field 0, via the memset above; likewise cfg->ep_status. */
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

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets, packed
 * from / unpacked into the flags regmap.h's shared functional-config
 * prefix already models -- same bit positions as ep_pwm.c's
 * PWM_OUT_ENABLE_CLR_BIT_/PWM_OUT_OPTIONS_BIT_ constants and ep_gpio.c's
 * own copies, since Table 32's common entries are shared across every
 * endpoint type. Only the bits regmap.h models are represented; see
 * ep_pwm.c's identical note for why the rest read back as 0. */
#define SPI_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define SPI_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))
#define SPI_OPTIONS_BIT_REQ_CRC   ((uint8_t)(1u << 0))
#define SPI_OPTIONS_BIT_RESP_TS   ((uint8_t)(1u << 3))
#define SPI_OPTIONS_BIT_SUPPRESS  ((uint8_t)(1u << 7))

/* The inverse of rcp_ep_spi_mode_cpol()/_cpha(): recovers the mode implied
 * by a (cpol, cpha) bit pair. The mapping is bijective (each of the four
 * modes yields a distinct pair), so this is a lossless round trip. */
static rcp_ep_spi_mode_t mode_from_bits(bool cpol, bool cpha)
{
    if (!cpol && !cpha) return RCP_EP_SPI_MODE_0;
    if (!cpol && cpha) return RCP_EP_SPI_MODE_1;
    if (cpol && !cpha) return RCP_EP_SPI_MODE_2;
    return RCP_EP_SPI_MODE_3;
}

//cfusa:req REQ-SPI-038
//cfusa:req REQ-SPI-040
void rcp_ep_spi_render_registers(const rcp_ep_spi_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_SPI_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;
    uint8_t c;

    if (cfg->common.ep_enable) enable_clr |= SPI_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= SPI_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= SPI_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= SPI_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= SPI_OPTIONS_BIT_SUPPRESS;

    out[RCP_EP_SPI_REG_EP_LEN]        = (uint8_t)RCP_EP_SPI_EP_FUNC_LEN;
    /* TC18 0.5.1_RC5: spi_nr_cs is a 4-bit "(count - 1)" field in bits
     * [3:0], upper nibble reserved -- see the file header's own "FIXED
     * 2026-08-11" note. RCP_EP_SPI_MAX_CHANNELS (6) renders as 0x05. */
    out[RCP_EP_SPI_REG_NR_CS]         = (uint8_t)((RCP_EP_SPI_MAX_CHANNELS - 1u) & 0x0Fu);
    out[RCP_EP_SPI_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_SPI_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_SPI_REG_EP_STATUS], cfg->ep_status);

    for (c = 0; c < RCP_EP_SPI_MAX_CHANNELS; c++) {
        const rcp_ep_spi_channel_cfg_t *ch = &cfg->channels[c];
        uint16_t base = (uint16_t)(RCP_EP_SPI_REG_CHANNEL_BASE +
                                    (uint16_t)c * RCP_EP_SPI_REG_CHANNEL_SPAN);
        uint8_t  cfg_byte = 0u;

        if (rcp_ep_spi_mode_cpol((rcp_ep_spi_mode_t)ch->mode)) cfg_byte |= RCP_EP_SPI_CFG_BIT_CLK_POLARITY;
        if (rcp_ep_spi_mode_cpha((rcp_ep_spi_mode_t)ch->mode)) cfg_byte |= RCP_EP_SPI_CFG_BIT_CLK_PHASE;
        if (ch->cs_polarity == (uint8_t)RCP_EP_SPI_CS_ACTIVE_HIGH) cfg_byte |= RCP_EP_SPI_CFG_BIT_CS_POLARITY;
        if (ch->use_common_cs) cfg_byte |= RCP_EP_SPI_CFG_BIT_USE_CS;
        if (ch->deassert_cs_pause) cfg_byte |= RCP_EP_SPI_CFG_BIT_DEASSERT_CS_PAUSE;

        put_u16(&out[base + RCP_EP_SPI_CHREG_BAUD_RATE], ch->baud_rate_kbps);
        out[base + RCP_EP_SPI_CHREG_CFG]          = cfg_byte;
        out[base + RCP_EP_SPI_CHREG_CS_LEADTIME]  = ch->cs_clk_leadtime;
        out[base + RCP_EP_SPI_CHREG_CS_TRAILTIME] = ch->clk_cs_trailtime;
        out[base + RCP_EP_SPI_CHREG_BITS_MAX]     = ch->bits_max;
        out[base + RCP_EP_SPI_CHREG_PAUSE_MIN]    = ch->pause_min;
        out[base + RCP_EP_SPI_CHREG_RESERVED]     = 0u;
    }
}

/* The inverse of render: adopts every R/W register from an already patched
 * block image. The read-only offsets (EP_LEN, NR_CS, and each channel's own
 * reserved octet) are deliberately not read back -- apply_reconfig()
 * re-renders them from cfg before patching, so a write covering them is a
 * no-op. bit_order and trigger have no wire counterpart (see the file
 * header) and are left untouched here, exactly as render leaves them
 * unrendered. */
static void parse_registers(rcp_ep_spi_functional_cfg_t *cfg,
                             const uint8_t in[RCP_EP_SPI_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_SPI_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_SPI_REG_EP_OPTIONS];
    uint8_t c;

    cfg->common.ep_enable             = (enable_clr & SPI_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage  = (enable_clr & SPI_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable     = (options & SPI_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & SPI_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & SPI_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status = get_u16(&in[RCP_EP_SPI_REG_EP_STATUS]);

    for (c = 0; c < RCP_EP_SPI_MAX_CHANNELS; c++) {
        rcp_ep_spi_channel_cfg_t *ch = &cfg->channels[c];
        uint16_t base = (uint16_t)(RCP_EP_SPI_REG_CHANNEL_BASE +
                                    (uint16_t)c * RCP_EP_SPI_REG_CHANNEL_SPAN);
        uint8_t  cfg_byte = in[base + RCP_EP_SPI_CHREG_CFG];
        bool     cpol = (cfg_byte & RCP_EP_SPI_CFG_BIT_CLK_POLARITY) != 0u;
        bool     cpha = (cfg_byte & RCP_EP_SPI_CFG_BIT_CLK_PHASE) != 0u;

        ch->mode          = (uint8_t)mode_from_bits(cpol, cpha);
        ch->cs_polarity   = (cfg_byte & RCP_EP_SPI_CFG_BIT_CS_POLARITY) != 0u
                                 ? (uint8_t)RCP_EP_SPI_CS_ACTIVE_HIGH
                                 : (uint8_t)RCP_EP_SPI_CS_ACTIVE_LOW;
        ch->use_common_cs = (cfg_byte & RCP_EP_SPI_CFG_BIT_USE_CS) != 0u;
        ch->deassert_cs_pause = (cfg_byte & RCP_EP_SPI_CFG_BIT_DEASSERT_CS_PAUSE) != 0u;

        ch->baud_rate_kbps  = get_u16(&in[base + RCP_EP_SPI_CHREG_BAUD_RATE]);
        ch->cs_clk_leadtime = in[base + RCP_EP_SPI_CHREG_CS_LEADTIME];
        ch->clk_cs_trailtime = in[base + RCP_EP_SPI_CHREG_CS_TRAILTIME];
        ch->bits_max        = in[base + RCP_EP_SPI_CHREG_BITS_MAX];
        ch->pause_min        = in[base + RCP_EP_SPI_CHREG_PAUSE_MIN];
    }
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, NR_CS, or any channel's own reserved
 * octet (computed via the channel span's own modulus, so it applies
 * uniformly to all RCP_EP_SPI_MAX_CHANNELS channels). */
static bool reg_offset_read_only(uint16_t addr)
{
    if (addr == RCP_EP_SPI_REG_EP_LEN || addr == RCP_EP_SPI_REG_NR_CS) return true;
    if (addr >= RCP_EP_SPI_REG_CHANNEL_BASE) {
        uint16_t rel = (uint16_t)((addr - RCP_EP_SPI_REG_CHANNEL_BASE) % RCP_EP_SPI_REG_CHANNEL_SPAN);
        return rel == RCP_EP_SPI_CHREG_RESERVED;
    }
    return false;
}

//cfusa:req REQ-SPI-039
const char *rcp_ep_spi_reconfig_strerror(rcp_ep_spi_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_SPI_RECONFIG_OK:
        return "rcp/ep_spi: SPI configuration write applied";
    case RCP_EP_SPI_RECONFIG_ERR_SHORT:
        return "rcp/ep_spi: SPI configuration write has no address and data";
    case RCP_EP_SPI_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_spi: SPI configuration write extends past the EP_func block";
    default:
        return "rcp/ep_spi: SPI unknown configuration-write error";
    }
}

//cfusa:req REQ-SPI-038
//cfusa:req REQ-SPI-039
rcp_ep_spi_reconfig_errc_t rcp_ep_spi_apply_reconfig(rcp_ep_spi_functional_cfg_t *cfg,
                                                      const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_SPI_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_SPI_RECONFIG_ADDR_LEN) {
        return RCP_EP_SPI_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_SPI_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (§12.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_SPI_EP_FUNC_LEN) {
        return RCP_EP_SPI_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Patch the block's current image at octet granularity, then adopt it
     * wholesale -- so a write covering only part of a multi-octet register
     * updates exactly the octets it addresses and leaves that register's
     * other octets alone. */
    rcp_ep_spi_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_SPI_RECONFIG_ADDR_LEN + i];
    }
    parse_registers(cfg, block);

    return RCP_EP_SPI_RECONFIG_OK;
}

//cfusa:req REQ-SPI-038
rcp_bytes_t rcp_ep_spi_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint16_t start_address, const uint8_t *data,
                                                size_t data_len, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr   = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                     *payload;
    size_t                       payload_len;
    rcp_bytes_t                  frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_SPI_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    memcpy(payload + RCP_EP_SPI_RECONFIG_ADDR_LEN, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE; /* §12.7.1 Figure 18: "the data
                                                of the byte_msg_payload from
                                                a write request is written
                                                into" the EP_func block --
                                                matches PWM_OUT's/GPIO's own
                                                encode_reconfig_request(). */
    hdr.evt             = 0x7u; /* evt[2:0] = 111b, TC18 Table 33 SPI row */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    free(payload);
    return frame;
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
//cfusa:req REQ-SPI-036
rcp_bytes_t rcp_ep_spi_encode_transfer_request(rcp_byte_bus_id_t byte_bus_id, uint8_t channel,
                                                const uint8_t *tx_data, size_t tx_len,
                                                uint16_t read_size, uint8_t transaction_num)
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
    hdr.byte_bus_id              = byte_bus_id;
    hdr.op                       = RCP_ACF_OP_READ;
    hdr.evt                      = (uint8_t)(channel & 0x7u);
    hdr.read_size_or_segment_num = read_size;
    hdr.transaction_num          = transaction_num;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-SPI-026
//cfusa:req REQ-SPI-027
//cfusa:req REQ-SPI-036
rcp_ep_spi_errc_t rcp_ep_spi_decode_transfer_request(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      uint8_t *out_channel,
                                                      const uint8_t **out_tx_data,
                                                      size_t *out_tx_len,
                                                      uint16_t *out_read_size,
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
    *out_read_size       = hdr.read_size_or_segment_num;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_SPI_OK;
}

//cfusa:req REQ-SPI-036
size_t rcp_ep_spi_transfer_length(size_t tx_len, uint16_t read_size)
{
    return (read_size > tx_len) ? (size_t)read_size : tx_len;
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
