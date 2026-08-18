/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_lin.h"
#include "rcp/alloc.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/ep_pwm.c's/
 * ep_gpio.c's/ep_spi.c's/ep_i2c.c's/ep_uart.c's house convention of not
 * sharing a byte-order util across modules) ──────────────────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* ── evt[2:0]: exact-match, per Table 33's plain-request row ─────────────── */

//cfusa:req REQ-LINEP-025
bool rcp_ep_lin_response_matches(const uint8_t *tx_data, size_t tx_len,
                                  const uint8_t *rx_data, size_t rx_len)
{
    return rcp_acf_compound_wait_match(0x0u, tx_data, tx_len, rx_data, rx_len);
}

/* ── Transmission-done trigger ─────────────────────────────────────────────── */

//cfusa:req REQ-LINEP-006
//cfusa:req REQ-LINEP-023
bool rcp_ep_lin_trigger_fires(rcp_ep_lin_trigger_t trigger, bool tx_done_event,
                               bool trailing_time_expired)
{
    switch (trigger) {
    case RCP_EP_LIN_TRIGGER_TX_DONE: return tx_done_event && trailing_time_expired;
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
     * RCP_EP_LIN_TRIGGER_NONE (0) via the memset above; likewise
     * ep_status/wire_clk_divider are already 0. */
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

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets, packed
 * from / unpacked into the flags regmap.h's shared functional-config
 * prefix already models -- same bit positions as ep_pwm.c's/ep_gpio.c's/
 * ep_spi.c's/ep_i2c.c's/ep_uart.c's own copies, since Table 35's common
 * entries are shared across every endpoint type. Only the bits regmap.h
 * models are represented; see ep_pwm.c's identical note for why the rest
 * read back as 0. */
#define LIN_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define LIN_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))
#define LIN_OPTIONS_BIT_REQ_CRC   ((uint8_t)(1u << 0))
#define LIN_OPTIONS_BIT_RESP_TS   ((uint8_t)(1u << 3))
#define LIN_OPTIONS_BIT_SUPPRESS  ((uint8_t)(1u << 7))

//cfusa:req REQ-LINEP-028
void rcp_ep_lin_render_registers(const rcp_ep_lin_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_LIN_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;

    if (cfg->common.ep_enable) enable_clr |= LIN_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= LIN_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= LIN_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= LIN_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= LIN_OPTIONS_BIT_SUPPRESS;

    out[RCP_EP_LIN_REG_EP_LEN]        = (uint8_t)RCP_EP_LIN_EP_FUNC_LEN;
    out[RCP_EP_LIN_REG_RESERVED_01]   = 0u;
    out[RCP_EP_LIN_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_LIN_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_LIN_REG_BASE_CLK], 0u); /* no real clock source
                                                    modelled -- see the file
                                                    header */
    put_u16(&out[RCP_EP_LIN_REG_EP_STATUS], cfg->ep_status);
    out[RCP_EP_LIN_REG_CLK_DIVIDER] = cfg->wire_clk_divider;
}

/* The inverse of render: adopts every R/W register from an already
 * patched block image. The read-only offsets (EP_LEN, the reserved
 * octet, base_clk) are deliberately not read back -- apply_reconfig()
 * re-renders them from cfg before patching, so a write covering them is a
 * no-op. */
static void parse_registers(rcp_ep_lin_functional_cfg_t *cfg,
                             const uint8_t in[RCP_EP_LIN_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_LIN_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_LIN_REG_EP_OPTIONS];

    cfg->common.ep_enable             = (enable_clr & LIN_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage  = (enable_clr & LIN_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable     = (options & LIN_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & LIN_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & LIN_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status        = get_u16(&in[RCP_EP_LIN_REG_EP_STATUS]);
    cfg->wire_clk_divider = in[RCP_EP_LIN_REG_CLK_DIVIDER];
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, the reserved octet, and both octets of
 * base_clk. */
static bool reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_LIN_REG_EP_LEN ||
           addr == RCP_EP_LIN_REG_RESERVED_01 ||
           addr == RCP_EP_LIN_REG_BASE_CLK ||
           addr == (uint16_t)(RCP_EP_LIN_REG_BASE_CLK + 1u);
}

//cfusa:req REQ-LINEP-029
const char *rcp_ep_lin_reconfig_strerror(rcp_ep_lin_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_LIN_RECONFIG_OK:
        return "rcp/ep_lin: LIN configuration write applied";
    case RCP_EP_LIN_RECONFIG_ERR_SHORT:
        return "rcp/ep_lin: LIN configuration write has no address and data";
    case RCP_EP_LIN_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_lin: LIN configuration write extends past the EP_func block";
    default:
        return "rcp/ep_lin: LIN unknown configuration-write error";
    }
}

//cfusa:req REQ-LINEP-028
//cfusa:req REQ-LINEP-029
rcp_ep_lin_reconfig_errc_t rcp_ep_lin_apply_reconfig(rcp_ep_lin_functional_cfg_t *cfg,
                                                      const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_LIN_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_LIN_RECONFIG_ADDR_LEN) {
        return RCP_EP_LIN_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_LIN_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (§12.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_LIN_EP_FUNC_LEN) {
        return RCP_EP_LIN_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Patch the block's current image at octet granularity, then adopt it
     * wholesale -- so a write covering only part of a multi-octet
     * register updates exactly the octets it addresses and leaves that
     * register's other octets alone. */
    rcp_ep_lin_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_LIN_RECONFIG_ADDR_LEN + i];
    }
    parse_registers(cfg, block);

    return RCP_EP_LIN_RECONFIG_OK;
}

//cfusa:req REQ-LINEP-028
rcp_bytes_t rcp_ep_lin_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint16_t start_address, const uint8_t *data,
                                                size_t data_len, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr   = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                     *payload;
    size_t                       payload_len;
    rcp_bytes_t                  frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_LIN_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)rcp_malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    memcpy(payload + RCP_EP_LIN_RECONFIG_ADDR_LEN, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE; /* §12.7.1 Figure 18: "the data
                                                of the byte_msg_payload from
                                                a write request is written
                                                into" the EP_func block --
                                                matches PWM_OUT's/GPIO's/
                                                SPI's/I2C's/UART's own
                                                encode_reconfig_request(). */
    hdr.evt             = 0x7u; /* evt[2:0] = 111b */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    rcp_free(payload);
    payload = NULL;
    return frame;
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
    case RCP_EP_LIN_ERR_BAD_EVT:      return "rcp/ep_lin: evt[2:0] is not plain (000b)";
    default:                          return "rcp/ep_lin: unknown error";
    }
}

/* ── Command request ───────────────────────────────────────────────────────── */

//cfusa:req REQ-LINEP-026
rcp_bytes_t rcp_ep_lin_encode_command_request(rcp_byte_bus_id_t byte_bus_id,
                                               const uint8_t *tx_data, size_t tx_len,
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
     * means. evt is left 0 -- Table 33 constrains a plain LIN request's
     * evt[2:0] to 000b; see the file header. */
    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-LINEP-017
//cfusa:req REQ-LINEP-018
//cfusa:req REQ-LINEP-027
rcp_ep_lin_errc_t rcp_ep_lin_decode_command_request(const uint8_t *b, size_t len,
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
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_LIN_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_LIN_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_LIN_ERR_WRONG_BUS;
    /* Read direction -- see rcp_ep_lin_encode_command_request() above. */
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_LIN_ERR_WRONG_OP;
    /* Table 33's plain-request row rule; see the file header. */
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_LIN_ERR_BAD_EVT;

    /* payload is round-tripped verbatim, byte for byte, with no
     * protocol-level LIN-frame parsing of any kind -- see the file
     * header. */
    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
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
