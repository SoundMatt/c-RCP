/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_uart.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include "alloc_overflow.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/ep_pwm.c's/
 * ep_gpio.c's/ep_spi.c's/ep_i2c.c's house convention of not sharing a
 * byte-order util across modules) ─────────────────────────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* ── Word format: data bits, parity, stop bits ──────────────────────────────── */

//cfusa:req REQ-UART-001
bool rcp_ep_uart_nr_bits_valid(uint8_t nr_bits)
{
    return nr_bits >= RCP_EP_UART_NR_BITS_MIN && nr_bits <= RCP_EP_UART_NR_BITS_MAX;
}

//cfusa:req REQ-UART-002
uint8_t rcp_ep_uart_bit_pad_mask(uint8_t nr_bits)
{
    if (!rcp_ep_uart_nr_bits_valid(nr_bits)) return 0;
    if (nr_bits == 8) return 0xFFu; /* (1u << 8) would overflow uint8_t's own
                                        arithmetic promotion range for this
                                        boundary case */
    return (uint8_t)((1u << nr_bits) - 1u);
}

//cfusa:req REQ-UART-003
void rcp_ep_uart_apply_bit_padding(uint8_t *buf, size_t len, uint8_t nr_bits)
{
    uint8_t mask = rcp_ep_uart_bit_pad_mask(nr_bits);
    size_t  i;

    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(buf[i] & mask);
    }
}

/* ── HW trigger signals (§13.7.8.4 Table 52) ─────────────────────────────────
 * See the file header's own "HW trigger signals" section. */

//cfusa:req REQ-UART-041
//cfusa:req REQ-UART-042
//cfusa:req REQ-UART-043
bool rcp_ep_uart_trigger_fires(rcp_ep_uart_trigger_t trigger, rcp_ep_uart_event_t event)
{
    switch (trigger) {
    case RCP_EP_UART_TRIGGER_TX_FINALIZED: return event == RCP_EP_UART_EVENT_TX_REQUEST_FINALIZED;
    case RCP_EP_UART_TRIGGER_RX_FINALIZED: return event == RCP_EP_UART_EVENT_READ_REQUEST_FINALIZED;
    case RCP_EP_UART_TRIGGER_NONE:
    default:                                return false;
    }
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-UART-004
void rcp_ep_uart_functional_cfg_init(rcp_ep_uart_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* parity is already RCP_EP_UART_PARITY_NONE (0) and stop_bits already
     * RCP_EP_UART_STOP_BITS_ONE (0) via the memset above. uart_nr_bits
     * cannot be left at that memset's 0, since 0 is not itself
     * rcp_ep_uart_nr_bits_valid() -- see the file header. ep_status/
     * baud_rate_kbps/wire_timeout_bit_times/trail are already 0 and
     * rts_enable/cts_enable/half_duplex already false, via the memset.
     * trigger is already RCP_EP_UART_TRIGGER_NONE (0) via the memset. */
    cfg->uart_nr_bits = RCP_EP_UART_NR_BITS_MAX;
}

//cfusa:req REQ-UART-005
//cfusa:req REQ-UART-006
//cfusa:req REQ-UART-007
bool rcp_ep_uart_functional_cfg_writable(rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-UART-008
//cfusa:req REQ-UART-009
bool rcp_ep_uart_set_baud_rate(rcp_ep_uart_functional_cfg_t *cfg, uint32_t baud_rate,
                                rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_uart_functional_cfg_writable(state, writer)) return false;

    cfg->baud_rate = baud_rate;
    return true;
}

//cfusa:req REQ-UART-010
//cfusa:req REQ-UART-011
//cfusa:req REQ-UART-012
bool rcp_ep_uart_set_frame_format(rcp_ep_uart_functional_cfg_t *cfg, uint8_t nr_bits,
                                   rcp_ep_uart_parity_t parity, rcp_ep_uart_stop_bits_t stop_bits,
                                   rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_uart_nr_bits_valid(nr_bits)) return false;
    if (!rcp_ep_uart_functional_cfg_writable(state, writer)) return false;

    cfg->uart_nr_bits = nr_bits;
    cfg->parity       = (uint8_t)parity;
    cfg->stop_bits    = (uint8_t)stop_bits;
    return true;
}

//cfusa:req REQ-UART-013
//cfusa:req REQ-UART-014
bool rcp_ep_uart_set_rx_buffer_size(rcp_ep_uart_functional_cfg_t *cfg, uint16_t rx_buffer_size,
                                     rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_uart_functional_cfg_writable(state, writer)) return false;

    cfg->ep_rx_buffer_size = rx_buffer_size;
    return true;
}

//cfusa:req REQ-UART-015
//cfusa:req REQ-UART-016
bool rcp_ep_uart_set_timeout(rcp_ep_uart_functional_cfg_t *cfg, uint32_t timeout_ms,
                              rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_uart_functional_cfg_writable(state, writer)) return false;

    cfg->uart_timeout_ms = timeout_ms;
    return true;
}

//cfusa:req REQ-UART-044
//cfusa:req REQ-UART-045
bool rcp_ep_uart_set_trigger(rcp_ep_uart_functional_cfg_t *cfg, rcp_ep_uart_trigger_t trigger,
                              rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_uart_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets, packed
 * from / unpacked into the flags regmap.h's shared functional-config
 * prefix already models -- same bit positions as ep_pwm.c's/ep_gpio.c's/
 * ep_spi.c's/ep_i2c.c's own copies, since Table 35's common entries are
 * shared across every endpoint type. Only the bits regmap.h models are
 * represented; see ep_pwm.c's identical note for why the rest read back
 * as 0. */
#define UART_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define UART_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))
#define UART_OPTIONS_BIT_REQ_CRC   ((uint8_t)(1u << 0))
#define UART_OPTIONS_BIT_RESP_TS   ((uint8_t)(1u << 3))
#define UART_OPTIONS_BIT_SUPPRESS  ((uint8_t)(1u << 7))

/* uart_stop_bits's own half-stop-bit units <-> rcp_ep_uart_stop_bits_t --
 * see the file header for the exact three-way mapping (REQ-UART-049,
 * split 2026-08-18 from this id's former home under REQ-UART-037). */
//cfusa:req REQ-UART-049
static uint8_t stop_bits_to_half_units(uint8_t stop_bits)
{
    if (stop_bits == (uint8_t)RCP_EP_UART_STOP_BITS_TWO) return 4u;
    if (stop_bits == (uint8_t)RCP_EP_UART_STOP_BITS_ONE_HALF) return 3u;
    return 2u; /* RCP_EP_UART_STOP_BITS_ONE, and any other/invalid value */
}

//cfusa:req REQ-UART-049
static uint8_t half_units_to_stop_bits(uint8_t half_units)
{
    if (half_units == 3u) return (uint8_t)RCP_EP_UART_STOP_BITS_ONE_HALF;
    return half_units >= 4u ? (uint8_t)RCP_EP_UART_STOP_BITS_TWO
                             : (uint8_t)RCP_EP_UART_STOP_BITS_ONE;
}

//cfusa:req REQ-UART-036
//cfusa:req REQ-UART-039
void rcp_ep_uart_render_registers(const rcp_ep_uart_functional_cfg_t *cfg,
                                   uint8_t out[RCP_EP_UART_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;
    uint8_t flags       = 0u;

    if (cfg->common.ep_enable) enable_clr |= UART_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= UART_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= UART_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= UART_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= UART_OPTIONS_BIT_SUPPRESS;

    if (cfg->parity != (uint8_t)RCP_EP_UART_PARITY_NONE) flags |= RCP_EP_UART_FLAG_PARITY_ENABLE;
    if (cfg->parity == (uint8_t)RCP_EP_UART_PARITY_EVEN) flags |= RCP_EP_UART_FLAG_PARITY_POL;
    if (cfg->rts_enable) flags |= RCP_EP_UART_FLAG_RTS_ENABLE;
    if (cfg->cts_enable) flags |= RCP_EP_UART_FLAG_CTS_ENABLE;
    if (cfg->half_duplex) flags |= RCP_EP_UART_FLAG_HALF_DUPLEX;

    out[RCP_EP_UART_REG_EP_LEN]        = (uint8_t)RCP_EP_UART_EP_FUNC_LEN;
    out[RCP_EP_UART_REG_RESERVED_01]   = 0u;
    out[RCP_EP_UART_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_UART_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_UART_REG_EP_STATUS], cfg->ep_status);
    put_u16(&out[RCP_EP_UART_REG_BAUD_RATE], cfg->baud_rate_kbps);
    out[RCP_EP_UART_REG_NR_BITS]   = cfg->uart_nr_bits;
    out[RCP_EP_UART_REG_FLAGS]     = flags;
    out[RCP_EP_UART_REG_STOP_BITS] = stop_bits_to_half_units(cfg->stop_bits);
    out[RCP_EP_UART_REG_TIMEOUT]   = cfg->wire_timeout_bit_times;
    out[RCP_EP_UART_REG_TRAIL]     = cfg->trail;
}

/* The inverse of render: adopts every R/W register from an already
 * patched block image. The read-only offsets (EP_LEN, the reserved
 * octet) are deliberately not read back -- apply_reconfig() re-renders
 * them from cfg before patching, so a write covering them is a no-op. */
//cfusa:req REQ-UART-040
static void parse_registers(rcp_ep_uart_functional_cfg_t *cfg,
                             const uint8_t in[RCP_EP_UART_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_UART_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_UART_REG_EP_OPTIONS];
    uint8_t flags       = in[RCP_EP_UART_REG_FLAGS];
    bool    parity_enable = (flags & RCP_EP_UART_FLAG_PARITY_ENABLE) != 0u;
    bool    parity_pol    = (flags & RCP_EP_UART_FLAG_PARITY_POL) != 0u;

    cfg->common.ep_enable             = (enable_clr & UART_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage  = (enable_clr & UART_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable     = (options & UART_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & UART_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & UART_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status      = get_u16(&in[RCP_EP_UART_REG_EP_STATUS]);
    cfg->baud_rate_kbps = get_u16(&in[RCP_EP_UART_REG_BAUD_RATE]);
    cfg->uart_nr_bits   = in[RCP_EP_UART_REG_NR_BITS];

    if (!parity_enable) {
        cfg->parity = (uint8_t)RCP_EP_UART_PARITY_NONE;
    } else {
        cfg->parity = parity_pol ? (uint8_t)RCP_EP_UART_PARITY_EVEN
                                  : (uint8_t)RCP_EP_UART_PARITY_ODD;
    }
    cfg->rts_enable  = (flags & RCP_EP_UART_FLAG_RTS_ENABLE) != 0u;
    cfg->cts_enable  = (flags & RCP_EP_UART_FLAG_CTS_ENABLE) != 0u;
    cfg->half_duplex = (flags & RCP_EP_UART_FLAG_HALF_DUPLEX) != 0u;

    cfg->stop_bits              = half_units_to_stop_bits(in[RCP_EP_UART_REG_STOP_BITS]);
    cfg->wire_timeout_bit_times = in[RCP_EP_UART_REG_TIMEOUT];
    cfg->trail                  = in[RCP_EP_UART_REG_TRAIL];
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN or the reserved octet. */
//cfusa:req REQ-UART-040
static bool reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_UART_REG_EP_LEN || addr == RCP_EP_UART_REG_RESERVED_01;
}

//cfusa:req REQ-UART-040
const char *rcp_ep_uart_reconfig_strerror(rcp_ep_uart_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_UART_RECONFIG_OK:
        return "rcp/ep_uart: UART configuration write applied";
    case RCP_EP_UART_RECONFIG_ERR_SHORT:
        return "rcp/ep_uart: UART configuration write has no address and data";
    case RCP_EP_UART_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_uart: UART configuration write extends past the EP_func block";
    default:
        return "rcp/ep_uart: UART unknown configuration-write error";
    }
}

//cfusa:req REQ-UART-039
//cfusa:req REQ-UART-040
rcp_ep_uart_reconfig_errc_t rcp_ep_uart_apply_reconfig(rcp_ep_uart_functional_cfg_t *cfg,
                                                        const uint8_t *payload,
                                                        size_t payload_len)
{
    uint8_t  block[RCP_EP_UART_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_UART_RECONFIG_ADDR_LEN) {
        return RCP_EP_UART_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_UART_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (§12.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_UART_EP_FUNC_LEN) {
        return RCP_EP_UART_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Patch the block's current image at octet granularity, then adopt it
     * wholesale -- so a write covering only part of a multi-octet
     * register updates exactly the octets it addresses and leaves that
     * register's other octets alone. */
    rcp_ep_uart_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_UART_RECONFIG_ADDR_LEN + i];
    }
    parse_registers(cfg, block);

    return RCP_EP_UART_RECONFIG_OK;
}

//cfusa:req REQ-UART-039
rcp_bytes_t rcp_ep_uart_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                 uint16_t start_address, const uint8_t *data,
                                                 size_t data_len, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr   = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                     *payload;
    size_t                       payload_len;
    rcp_bytes_t                  frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_UART_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)rcp_malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    rcp_memcpy_bounded(payload + RCP_EP_UART_RECONFIG_ADDR_LEN, data_len, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE; /* §12.7.1 Figure 18: "the data
                                                of the byte_msg_payload from
                                                a write request is written
                                                into" the EP_func block --
                                                matches PWM_OUT's/GPIO's/
                                                SPI's/I2C's own
                                                encode_reconfig_request(). */
    hdr.evt             = 0x7u; /* evt[2:0] = 111b */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    rcp_free(payload);
    payload = NULL;
    return frame;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-UART-017
const char *rcp_ep_uart_strerror(rcp_ep_uart_errc_t e)
{
    switch (e) {
    case RCP_EP_UART_OK:               return "rcp/ep_uart: success";
    case RCP_EP_UART_ERR_SHORT_FRAME:  return "rcp/ep_uart: frame too short";
    case RCP_EP_UART_ERR_BAD_MSG_TYPE: return "rcp/ep_uart: unexpected ACF message type";
    case RCP_EP_UART_ERR_WRONG_BUS:    return "rcp/ep_uart: wrong byte_bus_id";
    case RCP_EP_UART_ERR_WRONG_OP:     return "rcp/ep_uart: wrong ACF op";
    case RCP_EP_UART_ERR_UNKNOWN_CMD:  return "rcp/ep_uart: unrecognized command (payload-bearing read request)";
    case RCP_EP_UART_ERR_BAD_EVT:      return "rcp/ep_uart: evt[2:0] is not 0b000";
    default:                           return "rcp/ep_uart: unknown error";
    }
}

/* ── TX: write request/response ────────────────────────────────────────────── */

//cfusa:req REQ-UART-018
rcp_bytes_t rcp_ep_uart_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                              const uint8_t *tx_data, size_t tx_len,
                                              uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0; /* no channel selector -- see the file header */
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-UART-019
//cfusa:req REQ-UART-020
rcp_ep_uart_errc_t rcp_ep_uart_decode_write_request(const uint8_t *b, size_t len,
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
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_UART_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_UART_ERR_WRONG_OP;
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_UART_ERR_BAD_EVT;

    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_UART_OK;
}

//cfusa:req REQ-UART-021
rcp_bytes_t rcp_ep_uart_encode_write_response(rcp_byte_bus_id_t byte_bus_id,
                                               const uint8_t *accepted_data, size_t accepted_len,
                                               uint8_t transaction_num, bool timed,
                                               uint64_t timestamp)
{
    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_WRITE;
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.evt             = 0;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, accepted_data, accepted_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_WRITE;
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.evt             = 0;
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, accepted_data, accepted_len);
    }
}

//cfusa:req REQ-UART-022
//cfusa:req REQ-UART-046
rcp_ep_uart_errc_t rcp_ep_uart_decode_write_response(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      const uint8_t **out_accepted_data,
                                                      size_t *out_accepted_len, bool *out_timed,
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

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_UART_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_UART_ERR_WRONG_BUS;

    *out_accepted_data   = payload;
    *out_accepted_len    = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_UART_OK;
}

/* ── RX: read request/response ─────────────────────────────────────────────── */

//cfusa:req REQ-UART-023
//cfusa:req REQ-UART-034
rcp_bytes_t rcp_ep_uart_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint16_t read_size,
                                             uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id               = byte_bus_id;
    hdr.op                        = RCP_ACF_OP_READ;
    hdr.evt                       = 0; /* no channel selector -- see the file header */
    hdr.transaction_num           = transaction_num;
    hdr.read_size_or_segment_num  = read_size;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-UART-024
//cfusa:req REQ-UART-025
//cfusa:req REQ-UART-034
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    uint16_t *out_read_size,
                                                    uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_UART_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_UART_ERR_WRONG_OP;
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_UART_ERR_BAD_EVT;

    /* A UART read request has nothing meaningful a payload could carry
     * (read_size already rides the ACF header itself) -- a payload-bearing
     * one is treated as an unrecognized command, a deliberate asymmetry
     * against ep_gpio.h's write requests / the future PWM_OUT endpoint,
     * which do accept a payload on some of their own request types. See
     * the file header. */
    if (payload_len != 0) return RCP_EP_UART_ERR_UNKNOWN_CMD;

    /* FIXED 2026-08-12 (issue #201, REQ-UART-034): out_read_size is now
     * the wire header's own 12-bit-wide uint16_t type -- no narrowing
     * cast needed (or possible data loss) any more; see the header's own
     * doc comment. */
    *out_read_size       = hdr.read_size_or_segment_num;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_UART_OK;
}

//cfusa:req REQ-UART-026
rcp_bytes_t rcp_ep_uart_encode_read_response(rcp_byte_bus_id_t byte_bus_id,
                                              const uint8_t *rx_data, size_t rx_len,
                                              uint8_t transaction_num, bool timed,
                                              uint64_t timestamp)
{
    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.evt             = 0;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, rx_data, rx_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.evt             = 0;
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, rx_data, rx_len);
    }
}

//cfusa:req REQ-UART-027
//cfusa:req REQ-UART-028
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_response(const uint8_t *b, size_t len,
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

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_UART_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_UART_ERR_WRONG_BUS;

    /* payload_len may legitimately be shorter than the originating read
     * request's read_size (a short read, raced against uart_timeout_ms) --
     * this milestone's single-AVTPDU scope treats that exactly like any
     * other payload length, with no segment_num-based reassembly. See the
     * file header. */
    *out_rx_data         = payload;
    *out_rx_len          = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_UART_OK;
}

/* ── Read-completion arbitration (REQ-UART-033) ─────────────────────────────── */

//cfusa:req REQ-UART-037
uint32_t rcp_ep_uart_wire_timeout_us(uint16_t baud_rate_kbps, uint8_t wire_timeout_bit_times)
{
    uint32_t bit_periods_us_numerator;

    if (baud_rate_kbps == 0u) return 0u; /* fails open -- no configured clock to derive from */

    /* wire_timeout_bit_times * 1000 / baud_rate_kbps, rounded UP (ceiling)
     * so a caller never underestimates TC18's own configured timeout. */
    bit_periods_us_numerator = (uint32_t)wire_timeout_bit_times * 1000u;
    return (bit_periods_us_numerator + (uint32_t)baud_rate_kbps - 1u) / (uint32_t)baud_rate_kbps;
}

//cfusa:req REQ-UART-033
rcp_ep_uart_read_completion_t rcp_ep_uart_read_completion_decision(
    uint16_t bytes_available, uint16_t read_size, uint32_t elapsed_ms,
    uint32_t uart_timeout_ms, uint16_t rx_fifo_size)
{
    /* THIRD trigger: read_size larger than the fifo's own capacity, and the
     * fifo has filled to that capacity -- fragmentation is required because
     * a single response can never carry the whole request's worth of data. */
    if (read_size > rx_fifo_size && bytes_available >= rx_fifo_size) {
        return RCP_EP_UART_READ_RESPOND_FRAGMENTED;
    }

    /* FIRST trigger: the fifo already holds everything the request asked
     * for. */
    if (bytes_available >= read_size) {
        return RCP_EP_UART_READ_RESPOND_NORMAL;
    }

    /* SECOND trigger: uart_timeout has expired -- whatever is in the fifo
     * right now (possibly nothing) goes out as a short read. */
    if (elapsed_ms >= uart_timeout_ms) {
        return RCP_EP_UART_READ_RESPOND_NORMAL;
    }

    return RCP_EP_UART_READ_NOT_YET_COMPLETE;
}

/* ── Fragmented read response (Phase 20, fragment.h) ───────────────────────── */

//cfusa:req REQ-UART-029
size_t rcp_ep_uart_read_response_fragment_count(size_t rx_len, size_t max_fragment_payload)
{
    return rcp_fragment_plan_count(rx_len, max_fragment_payload);
}

//cfusa:req REQ-UART-030
size_t rcp_ep_uart_encode_read_response_fragmented(rcp_byte_bus_id_t byte_bus_id,
                                                    const uint8_t *rx_data, size_t rx_len,
                                                    uint8_t transaction_num, bool timed,
                                                    uint64_t timestamp,
                                                    size_t max_fragment_payload,
                                                    rcp_bytes_t *out_frames)
{
    size_t                  count;
    rcp_fragment_segment_t *segs;
    size_t                  i;

    count = rcp_fragment_plan_count(rx_len, max_fragment_payload);
    if (count == 0) return 0;

    {
        size_t alloc_bytes = rcp_alloc_checked_size(count, sizeof(*segs));
        segs = alloc_bytes == 0 ? NULL : (rcp_fragment_segment_t *)rcp_malloc(alloc_bytes);
    }
    if (!segs) return 0;

    if (rcp_fragment_plan(rx_len, max_fragment_payload, segs, count) != RCP_FRAGMENT_OK) {
        rcp_free(segs);
        segs = NULL;
        return 0;
    }

    for (i = 0; i < count; i++) {
        const uint8_t *slice     = (rx_data != NULL) ? &rx_data[segs[i].offset] : NULL;
        size_t         slice_len = segs[i].len;
        rcp_bytes_t    frame;

        if (timed) {
            rcp_acf_gbb_header_t hdr = {0};

            hdr.info.byte_bus_id              = byte_bus_id;
            hdr.info.op                       = RCP_ACF_OP_READ;
            hdr.info.rsp                      = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
            hdr.info.evt                      = 0;
            hdr.info.mtv                      = RCP_ACF_MTV_VALID;
            hdr.info.transaction_num          = transaction_num;
            hdr.info.ms                       = segs[i].ms ? 1u : 0u;
            hdr.info.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : 0u;
            hdr.message_timestamp             = timestamp;

            frame = rcp_acf_encode_gbb(&hdr, slice, slice_len);
        } else {
            rcp_acf_byte_message_info_t hdr = {0};

            hdr.byte_bus_id              = byte_bus_id;
            hdr.op                       = RCP_ACF_OP_READ;
            hdr.rsp                      = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
            hdr.evt                      = 0;
            hdr.transaction_num          = transaction_num;
            hdr.ms                       = segs[i].ms ? 1u : 0u;
            hdr.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : 0u;

            frame = rcp_acf_encode_abb(&hdr, slice, slice_len);
        }

        if (!frame.data) {
            size_t j;

            for (j = 0; j < i; j++) rcp_bytes_free(&out_frames[j]);
            rcp_free(segs);
            segs = NULL;
            return 0;
        }

        out_frames[i] = frame;
    }

    rcp_free(segs);
    segs = NULL;
    return count;
}

//cfusa:req REQ-UART-031
//cfusa:req REQ-UART-047
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_response_fragment(const uint8_t *b, size_t len,
                                                              rcp_byte_bus_id_t expected_bus_id,
                                                              bool *out_ms,
                                                              uint8_t *out_segment_num,
                                                              const uint8_t **out_payload,
                                                              size_t *out_payload_len,
                                                              bool *out_timed,
                                                              uint64_t *out_timestamp,
                                                              uint8_t *out_transaction_num)
{
    uint8_t                     msg_type;
    rcp_acf_errc_t               acf_rc;
    rcp_acf_byte_message_info_t  abb_hdr;
    rcp_acf_gbb_header_t         gbb_hdr;
    const uint8_t                *payload;
    size_t                        payload_len;
    rcp_byte_bus_id_t             bus_id;
    uint8_t                       ms;
    /* This local is this endpoint's own pre-existing octet-wide type
     * (unchanged by the acf.c header rework); the wire field it's read
     * from below is now the header's real 12-bit uint16_t field, so both
     * assignments need an explicit narrowing cast for MSVC's /W4 -- same
     * reasoning and same pre-existing (not newly introduced) truncation
     * behavior as ep_uart.c's out_read_size cast above. */
    uint8_t                       segment_num;
    uint8_t                       transaction_num;
    bool                          timed;
    uint64_t                      timestamp;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_UART_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        ms              = gbb_hdr.info.ms;
        segment_num     = (uint8_t)gbb_hdr.info.read_size_or_segment_num;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_UART_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_UART_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        ms              = abb_hdr.ms;
        segment_num     = (uint8_t)abb_hdr.read_size_or_segment_num;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_UART_ERR_WRONG_BUS;

    *out_ms              = (ms != 0u);
    *out_segment_num     = segment_num;
    *out_payload         = payload;
    *out_payload_len     = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_UART_OK;
}
