/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_iseled.h"
#include "rcp/alloc.h"

#include "alloc_overflow.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy -- see ep_pwm.c's/ep_adc.c's
 * own identical copies for the same house convention) ──────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

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

    b = (uint8_t *)rcp_malloc(n);
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

/* CORRECTED 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group G): TC18 Table 58
 * documents 0x0007.4 iseled_use_rcv_clk itself as "Use clock provided by
 * ISELED 1st device instead of FreqSync pattern" -- true selects the
 * device-provided clock, which arrives on ISP_N (§13.7.12.2: ISP_N need
 * not be wired only when the Freq_Sync pattern is used instead). ISP_N is
 * therefore required exactly when use_rcv_clk is true, the opposite of
 * what this function returned before this fix -- see the (now-removed)
 * pinned deviation test in tests/test_tc18_gaps_ep2.c for the prior,
 * already-diagnosed-but-uncorrected state of this bug. */
//cfusa:req REQ-ISELED-007
bool rcp_ep_iseled_requires_isp_n(bool use_rcv_clk)
{
    return use_rcv_clk;
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
     * iseled_crc_enable are already false, trigger is already
     * RCP_EP_ISELED_TRIGGER_NONE (0), and every EP_func register
     * (base_clk/ep_status/wire_clk_divider/collect_resp/nr_leds/
     * rcv_timeout) is already 0/false via the memset above. */
}

//cfusa:req REQ-ISELED-010
bool rcp_ep_iseled_functional_cfg_writable(rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-ISELED-011
bool rcp_ep_iseled_set_bit_clk_divider(rcp_ep_iseled_functional_cfg_t *cfg, uint32_t divider,
                                        rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->iseled_bit_clk_divider = divider;
    return true;
}

//cfusa:req REQ-ISELED-012
bool rcp_ep_iseled_set_use_rcv_clk(rcp_ep_iseled_functional_cfg_t *cfg, bool use_rcv_clk,
                                    rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->iseled_use_rcv_clk = use_rcv_clk;
    return true;
}

//cfusa:req REQ-ISELED-013
bool rcp_ep_iseled_set_crc_enable(rcp_ep_iseled_functional_cfg_t *cfg, bool enable,
                                   rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->iseled_crc_enable = enable;
    return true;
}

//cfusa:req REQ-ISELED-014
bool rcp_ep_iseled_set_trigger(rcp_ep_iseled_functional_cfg_t *cfg,
                                rcp_ep_iseled_trigger_t trigger, rcp_lifecycle_state_t state,
                                rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_iseled_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets -- same
 * house convention and same "regmap.h modeling gap" caveat as ep_pwm.c's
 * own copy. */
#define ISELED_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define ISELED_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))
#define ISELED_OPTIONS_BIT_REQ_CRC   ((uint8_t)(1u << 0))
#define ISELED_OPTIONS_BIT_RESP_TS   ((uint8_t)(1u << 3))
#define ISELED_OPTIONS_BIT_SUPPRESS  ((uint8_t)(1u << 7))

//cfusa:req REQ-ISELED-029
void rcp_ep_iseled_render_registers(const rcp_ep_iseled_functional_cfg_t *cfg,
                                     uint8_t out[RCP_EP_ISELED_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;
    uint8_t flags      = 0u;

    if (cfg->common.ep_enable) enable_clr |= ISELED_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= ISELED_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= ISELED_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= ISELED_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= ISELED_OPTIONS_BIT_SUPPRESS;
    if (cfg->collect_resp) flags |= RCP_EP_ISELED_FLAG_COLLECT_RESP;
    if (cfg->iseled_use_rcv_clk) flags |= RCP_EP_ISELED_FLAG_USE_RCV_CLK;

    out[RCP_EP_ISELED_REG_EP_LEN]        = (uint8_t)RCP_EP_ISELED_EP_FUNC_LEN;
    out[RCP_EP_ISELED_REG_RESERVED_01]   = 0u;
    out[RCP_EP_ISELED_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_ISELED_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_ISELED_REG_BASE_CLK], cfg->base_clk);
    put_u16(&out[RCP_EP_ISELED_REG_EP_STATUS], cfg->ep_status);
    out[RCP_EP_ISELED_REG_CLK_DIVIDER] = cfg->wire_clk_divider;
    out[RCP_EP_ISELED_REG_FLAGS]       = flags;
    put_u16(&out[RCP_EP_ISELED_REG_NR_LEDS], cfg->nr_leds);
    put_u16(&out[RCP_EP_ISELED_REG_RCV_TIMEOUT], cfg->rcv_timeout);
}

/* The inverse of render -- same "read-only offsets not read back" design as
 * ep_pwm.c's own parse_registers(). */
static void parse_iseled_registers(rcp_ep_iseled_functional_cfg_t *cfg,
                                    const uint8_t in[RCP_EP_ISELED_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_ISELED_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_ISELED_REG_EP_OPTIONS];
    uint8_t flags      = in[RCP_EP_ISELED_REG_FLAGS];

    cfg->common.ep_enable             = (enable_clr & ISELED_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage  = (enable_clr & ISELED_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable     = (options & ISELED_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & ISELED_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & ISELED_OPTIONS_BIT_SUPPRESS) != 0u;
    cfg->collect_resp                 = (flags & RCP_EP_ISELED_FLAG_COLLECT_RESP) != 0u;
    cfg->iseled_use_rcv_clk           = (flags & RCP_EP_ISELED_FLAG_USE_RCV_CLK) != 0u;

    cfg->ep_status        = get_u16(&in[RCP_EP_ISELED_REG_EP_STATUS]);
    cfg->wire_clk_divider = in[RCP_EP_ISELED_REG_CLK_DIVIDER];
    cfg->nr_leds          = get_u16(&in[RCP_EP_ISELED_REG_NR_LEDS]);
    cfg->rcv_timeout      = get_u16(&in[RCP_EP_ISELED_REG_RCV_TIMEOUT]);
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, the reserved octet, and both octets of
 * base_clk. */
static bool iseled_reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_ISELED_REG_EP_LEN ||
           addr == RCP_EP_ISELED_REG_RESERVED_01 ||
           addr == RCP_EP_ISELED_REG_BASE_CLK ||
           addr == (uint16_t)(RCP_EP_ISELED_REG_BASE_CLK + 1u);
}

//cfusa:req REQ-ISELED-029
const char *rcp_ep_iseled_reconfig_strerror(rcp_ep_iseled_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_ISELED_RECONFIG_OK:
        return "rcp/ep_iseled: configuration write applied";
    case RCP_EP_ISELED_RECONFIG_ERR_SHORT:
        return "rcp/ep_iseled: configuration write has no address and data";
    case RCP_EP_ISELED_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_iseled: configuration write extends past the EP_func block";
    default:
        return "rcp/ep_iseled: unknown configuration-write error";
    }
}

//cfusa:req REQ-ISELED-029
rcp_ep_iseled_reconfig_errc_t
rcp_ep_iseled_apply_reconfig(rcp_ep_iseled_functional_cfg_t *cfg,
                              const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_ISELED_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_ISELED_RECONFIG_ADDR_LEN) {
        return RCP_EP_ISELED_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_ISELED_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (extraction §3.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_ISELED_EP_FUNC_LEN) {
        return RCP_EP_ISELED_RECONFIG_ERR_OUT_OF_RANGE;
    }

    rcp_ep_iseled_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (iseled_reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_ISELED_RECONFIG_ADDR_LEN + i];
    }
    parse_iseled_registers(cfg, block);

    return RCP_EP_ISELED_RECONFIG_OK;
}

//cfusa:req REQ-ISELED-029
rcp_bytes_t rcp_ep_iseled_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
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

    payload_len = RCP_EP_ISELED_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)rcp_malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    memcpy(payload + RCP_EP_ISELED_RECONFIG_ADDR_LEN, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0x7u; /* the reconfiguration escape hatch -- ISELED
                                    has no named write-semantics enum of its
                                    own (it belongs to the ADC/I2C/LIN/CAN/
                                    UART/PWM_IN/MDIO reserved-range group,
                                    not PWM_OUT's/GPIO's own eight-value
                                    write-semantics group), so the raw value
                                    is used directly, matching ep_adc.c's/
                                    ep_pwm.c's own equivalent encoders. */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    rcp_free(payload);
    payload = NULL;
    return frame;
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
    case RCP_EP_ISELED_ERR_BAD_EVT:         return "rcp/ep_iseled: evt[2:0] is not 0b000";
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
        bytes = (uint8_t *)rcp_malloc(byte_count);
        if (!bytes) return RCP_EP_ISELED_ERR_ALLOC;

        for (i = 0; i < byte_count; i++) {
            uint8_t hi, lo;

            if (!rcp_ep_iseled_symbol_decode(symbols[2u * i], &hi) ||
                !rcp_ep_iseled_symbol_decode(symbols[2u * i + 1u], &lo)) {
                rcp_free(bytes);
                bytes = NULL;
                return RCP_EP_ISELED_ERR_BAD_SYMBOL;
            }
            bytes[i] = (uint8_t)((uint8_t)(hi << 4) | lo);
        }
    }

    if (expect_crc) {
        size_t  data_len = byte_count - 1u;
        uint8_t want     = rcp_ep_iseled_crc8(bytes, data_len);

        if (bytes[byte_count - 1u] != want) {
            rcp_free(bytes);
            bytes = NULL;
            return RCP_EP_ISELED_ERR_CRC_MISMATCH;
        }

        *out_data = rcp_bytes_dup(bytes, data_len);
        rcp_free(bytes);
        bytes = NULL;
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
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_ISELED_ERR_BAD_EVT;

    /* payload is round-tripped verbatim, byte for byte -- see the file
     * header; this endpoint's own bit-framing happens only when it
     * actually drives ISP_P/ISP_N, never at this ACF layer. */
    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_ISELED_OK;
}

/* ── Read request (issue #471) ─────────────────────────────────────────────── */

//cfusa:req REQ-ISELED-030
rcp_bytes_t rcp_ep_iseled_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                               const uint8_t *tx_data, size_t tx_len,
                                               uint16_t read_size, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr  = {0};
    rcp_bytes_t                 fail = {0};

    if (read_size > RCP_EP_ISELED_MAX_READ_SIZE) return fail;

    hdr.byte_bus_id              = byte_bus_id;
    hdr.op                       = RCP_ACF_OP_READ;
    hdr.evt                      = 0;
    hdr.transaction_num          = transaction_num;
    hdr.read_size_or_segment_num = read_size;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-ISELED-031
rcp_ep_iseled_errc_t rcp_ep_iseled_decode_read_request(const uint8_t *b, size_t len,
                                                        rcp_byte_bus_id_t expected_bus_id,
                                                        const uint8_t **out_tx_data,
                                                        size_t *out_tx_len,
                                                        uint16_t *out_read_size,
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
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_ISELED_ERR_WRONG_OP;
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_ISELED_ERR_BAD_EVT;

    /* payload (the plain Instruction/Address content selecting what to
     * read back) is round-tripped verbatim, byte for byte -- see
     * rcp_ep_iseled_decode_command_request()'s own identical note. */
    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
    *out_read_size       = hdr.read_size_or_segment_num;
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

//cfusa:req REQ-ISELED-025
size_t rcp_ep_iseled_response_fragment_count(size_t available_len, uint16_t read_size,
                                              size_t max_fragment_payload)
{
    size_t capped_len = (available_len < (size_t)read_size) ? available_len : (size_t)read_size;

    return rcp_fragment_plan_count(capped_len, max_fragment_payload);
}

//cfusa:req REQ-ISELED-025
size_t rcp_ep_iseled_encode_response_fragmented(rcp_byte_bus_id_t byte_bus_id,
                                                 const uint8_t *rx_data, size_t rx_len,
                                                 uint16_t read_size, uint8_t transaction_num,
                                                 bool timed, uint64_t timestamp,
                                                 size_t max_fragment_payload,
                                                 rcp_bytes_t *out_frames)
{
    size_t                  capped_len;
    size_t                  count;
    rcp_fragment_segment_t *segs;
    size_t                  i;

    capped_len = (rx_len < (size_t)read_size) ? rx_len : (size_t)read_size;

    count = rcp_fragment_plan_count(capped_len, max_fragment_payload);
    if (count == 0) return 0;

    {
        size_t alloc_bytes = rcp_alloc_checked_size(count, sizeof(*segs));
        segs = alloc_bytes == 0 ? NULL : (rcp_fragment_segment_t *)rcp_malloc(alloc_bytes);
    }
    if (!segs) return 0;

    if (rcp_fragment_plan(capped_len, max_fragment_payload, segs, count) != RCP_FRAGMENT_OK) {
        rcp_free(segs);
        segs = NULL;
        return 0;
    }

    for (i = 0; i < count; i++) {
        const uint8_t *slice     = (capped_len > 0) ? &rx_data[segs[i].offset] : NULL;
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
