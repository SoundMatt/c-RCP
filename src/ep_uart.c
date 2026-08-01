/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_uart.h"

#include <stdlib.h>
#include <string.h>

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

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-UART-004
void rcp_ep_uart_functional_cfg_init(rcp_ep_uart_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* parity is already RCP_EP_UART_PARITY_NONE (0) and stop_bits already
     * RCP_EP_UART_STOP_BITS_ONE (0) via the memset above. uart_nr_bits
     * cannot be left at that memset's 0, since 0 is not itself
     * rcp_ep_uart_nr_bits_valid() -- see the file header. */
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
rcp_bytes_t rcp_ep_uart_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint8_t read_size,
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
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    uint8_t *out_read_size,
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

    /* out_read_size is this endpoint's own pre-existing octet-wide type
     * (unchanged by the acf.c header rework); hdr.read_size_or_segment_num
     * is now the wire header's real 12-bit uint16_t field, so the
     * narrowing needs an explicit cast for MSVC's /W4 -- same reasoning as
     * discovery.c's identical cast. A UART read request larger than 255
     * bytes truncates here, same as it silently did before this pass; not
     * a new limitation introduced by it. */
    *out_read_size       = (uint8_t)hdr.read_size_or_segment_num;
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

    segs = (rcp_fragment_segment_t *)malloc(count * sizeof(*segs));
    if (!segs) return 0;

    if (rcp_fragment_plan(rx_len, max_fragment_payload, segs, count) != RCP_FRAGMENT_OK) {
        free(segs);
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
            free(segs);
            return 0;
        }

        out_frames[i] = frame;
    }

    free(segs);
    return count;
}

//cfusa:req REQ-UART-031
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
