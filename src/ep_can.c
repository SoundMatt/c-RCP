/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_can.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/avtp.c's house
 * convention of not sharing a byte-order util across modules) ──────────── */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

/* ── FrameFormat selection (evt[2:0]) ────────────────────────────────────── */

//cfusa:req REQ-CANEP-001
bool rcp_ep_can_frame_format_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_CAN_FRAME_XL_NEW_PL;
}

//cfusa:req REQ-CANEP-002
bool rcp_ep_can_frame_format_is_xl(rcp_ep_can_frame_format_t format)
{
    switch (format) {
    case RCP_EP_CAN_FRAME_XL_CLASSICAL_PL:
    case RCP_EP_CAN_FRAME_XL_NEW_PL: return true;
    case RCP_EP_CAN_FRAME_CBFF:
    case RCP_EP_CAN_FRAME_CEFF:
    case RCP_EP_CAN_FRAME_FBFF:
    case RCP_EP_CAN_FRAME_FEFF:
    default:                         return false;
    }
}

//cfusa:req REQ-CANEP-003
rcp_ep_can_id_width_t rcp_ep_can_frame_format_id_width(rcp_ep_can_frame_format_t format)
{
    switch (format) {
    case RCP_EP_CAN_FRAME_CEFF:
    case RCP_EP_CAN_FRAME_FEFF:            return RCP_EP_CAN_ID_WIDTH_EXTENDED_29;
    case RCP_EP_CAN_FRAME_CBFF:
    case RCP_EP_CAN_FRAME_FBFF:
    case RCP_EP_CAN_FRAME_XL_CLASSICAL_PL:
    case RCP_EP_CAN_FRAME_XL_NEW_PL:
    default:                                return RCP_EP_CAN_ID_WIDTH_BASE_11;
    }
}

//cfusa:req REQ-CANEP-004
bool rcp_ep_can_arbitration_id_valid(rcp_ep_can_frame_format_t format, uint32_t id)
{
    if (!rcp_ep_can_frame_format_valid((uint8_t)format)) return false;

    if (rcp_ep_can_frame_format_id_width(format) == RCP_EP_CAN_ID_WIDTH_EXTENDED_29) {
        return id <= 0x1FFFFFFFu;
    }
    return id <= 0x7FFu;
}

//cfusa:req REQ-CANEP-005
size_t rcp_ep_can_frame_format_max_data_len(rcp_ep_can_frame_format_t format)
{
    switch (format) {
    case RCP_EP_CAN_FRAME_CBFF:
    case RCP_EP_CAN_FRAME_CEFF:            return RCP_EP_CAN_CLASSICAL_MAX_DATA_LEN;
    case RCP_EP_CAN_FRAME_FBFF:
    case RCP_EP_CAN_FRAME_FEFF:            return RCP_EP_CAN_FD_MAX_DATA_LEN;
    case RCP_EP_CAN_FRAME_XL_CLASSICAL_PL:
    case RCP_EP_CAN_FRAME_XL_NEW_PL:       return RCP_EP_CAN_XL_MAX_DATA_LEN;
    default:                               return 0;
    }
}

/* ── Functional config: CAN-XL acceptance/ID filters ─────────────────────── */

//cfusa:req REQ-CANEP-006
bool rcp_ep_can_xl_filter_index_valid(uint8_t index)
{
    return index < RCP_EP_CAN_XL_MAX_FILTERS;
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-CANEP-007
void rcp_ep_can_functional_cfg_init(rcp_ep_can_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* Every bit-timing register set, delay_comp_enable/_offset,
     * exec_delay_clk_divider, and every xl_filters[i] (id/mask/enable) are
     * already zero/false via the memset above. */
}

//cfusa:req REQ-CANEP-008
bool rcp_ep_can_functional_cfg_writable(rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-CANEP-009
bool rcp_ep_can_set_arbitration_timing(rcp_ep_can_functional_cfg_t *cfg,
                                        rcp_ep_can_bit_timing_t timing,
                                        rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_can_functional_cfg_writable(state, writer)) return false;

    cfg->arbitration_timing = timing;
    return true;
}

//cfusa:req REQ-CANEP-010
bool rcp_ep_can_set_fd_data_timing(rcp_ep_can_functional_cfg_t *cfg,
                                    rcp_ep_can_bit_timing_t timing,
                                    rcp_lifecycle_state_t state,
                                    rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_can_functional_cfg_writable(state, writer)) return false;

    cfg->fd_data_timing = timing;
    return true;
}

//cfusa:req REQ-CANEP-011
bool rcp_ep_can_set_xl_data_timing(rcp_ep_can_functional_cfg_t *cfg,
                                    rcp_ep_can_bit_timing_t timing,
                                    rcp_lifecycle_state_t state,
                                    rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_can_functional_cfg_writable(state, writer)) return false;

    cfg->xl_data_timing = timing;
    return true;
}

//cfusa:req REQ-CANEP-012
bool rcp_ep_can_set_delay_compensation(rcp_ep_can_functional_cfg_t *cfg, bool enable,
                                        uint8_t offset, rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_can_functional_cfg_writable(state, writer)) return false;

    cfg->delay_comp_enable = enable;
    cfg->delay_comp_offset = offset;
    return true;
}

//cfusa:req REQ-CANEP-013
bool rcp_ep_can_set_exec_delay_clk_divider(rcp_ep_can_functional_cfg_t *cfg, uint32_t divider,
                                            rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_can_functional_cfg_writable(state, writer)) return false;

    cfg->exec_delay_clk_divider = divider;
    return true;
}

//cfusa:req REQ-CANEP-014
bool rcp_ep_can_set_xl_filter(rcp_ep_can_functional_cfg_t *cfg, uint8_t index,
                               rcp_ep_can_xl_filter_t filter, rcp_lifecycle_state_t state,
                               rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_can_xl_filter_index_valid(index)) return false;
    if (!rcp_ep_can_functional_cfg_writable(state, writer)) return false;

    cfg->xl_filters[index] = filter;
    return true;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-CANEP-015
const char *rcp_ep_can_strerror(rcp_ep_can_errc_t e)
{
    switch (e) {
    case RCP_EP_CAN_OK:                   return "rcp/ep_can: success";
    case RCP_EP_CAN_ERR_SHORT_FRAME:      return "rcp/ep_can: frame too short";
    case RCP_EP_CAN_ERR_BAD_MSG_TYPE:     return "rcp/ep_can: unexpected ACF message type";
    case RCP_EP_CAN_ERR_WRONG_BUS:        return "rcp/ep_can: wrong byte_bus_id";
    case RCP_EP_CAN_ERR_WRONG_OP:         return "rcp/ep_can: wrong ACF op";
    case RCP_EP_CAN_ERR_BAD_FRAME_FORMAT: return "rcp/ep_can: invalid frame format selector";
    default:                              return "rcp/ep_can: unknown error";
    }
}

/* ── Wire layout helpers (this module's own prefix-then-data choice) ────── */

static size_t prefix_len_for(rcp_ep_can_frame_format_t format)
{
    return rcp_ep_can_frame_format_is_xl(format) ? (size_t)10u : (size_t)4u;
}

static void write_prefix(uint8_t *p, rcp_ep_can_frame_format_t format, uint32_t arbitration_id,
                          const rcp_ep_can_xl_header_t *xl_header)
{
    put_u32(p, arbitration_id);
    if (rcp_ep_can_frame_format_is_xl(format)) {
        p[4] = xl_header->sdt;
        p[5] = xl_header->vcid;
        put_u32(&p[6], xl_header->af);
    }
}

static void read_prefix(const uint8_t *p, rcp_ep_can_frame_format_t format,
                         uint32_t *out_arbitration_id, rcp_ep_can_xl_header_t *out_xl_header)
{
    *out_arbitration_id = get_u32(p);
    if (rcp_ep_can_frame_format_is_xl(format)) {
        out_xl_header->sdt  = p[4];
        out_xl_header->vcid = p[5];
        out_xl_header->af   = get_u32(&p[6]);
    }
}

/* Validates the shared encode preconditions for a request/response;
 * returns false (nothing further should be encoded) iff any precondition
 * fails. */
static bool encode_preconditions_ok(rcp_ep_can_frame_format_t frame_format,
                                     uint32_t arbitration_id,
                                     const rcp_ep_can_xl_header_t *xl_header, size_t data_len)
{
    bool is_xl;

    if (!rcp_ep_can_frame_format_valid((uint8_t)frame_format)) return false;
    if (!rcp_ep_can_arbitration_id_valid(frame_format, arbitration_id)) return false;
    if (data_len > rcp_ep_can_frame_format_max_data_len(frame_format)) return false;

    is_xl = rcp_ep_can_frame_format_is_xl(frame_format);
    if (is_xl && xl_header == NULL) return false;
    if (!is_xl && xl_header != NULL) return false;

    return true;
}

/* Builds this module's own prefix-then-data buffer into a freshly
 * malloc()'d block; returns NULL on allocation failure. *out_len is set to
 * the block's length either way it is reached (0 on allocation failure is
 * not meaningful since data is NULL). Caller frees the result with
 * free(). */
static uint8_t *build_payload(rcp_ep_can_frame_format_t frame_format, uint32_t arbitration_id,
                               const rcp_ep_can_xl_header_t *xl_header, const uint8_t *data,
                               size_t data_len, size_t *out_len)
{
    size_t   prefix_len = prefix_len_for(frame_format);
    size_t   total_len  = prefix_len + data_len;
    uint8_t *buf        = (uint8_t *)malloc(total_len > 0 ? total_len : 1);

    if (!buf) return NULL;

    write_prefix(buf, frame_format, arbitration_id, xl_header);
    if (data_len > 0) memcpy(&buf[prefix_len], data, data_len);

    *out_len = total_len;
    return buf;
}

/* ── Frame request ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-CANEP-016
rcp_bytes_t rcp_ep_can_encode_frame_request(rcp_byte_bus_id_t byte_bus_id,
                                             rcp_ep_can_frame_format_t frame_format,
                                             uint32_t arbitration_id,
                                             const rcp_ep_can_xl_header_t *xl_header,
                                             const uint8_t *tx_data, size_t tx_len,
                                             uint8_t transaction_num)
{
    rcp_bytes_t                 frame = {0};
    rcp_acf_byte_message_info_t hdr   = {0};
    uint8_t                     *payload;
    size_t                       payload_len;

    if (!encode_preconditions_ok(frame_format, arbitration_id, xl_header, tx_len)) return frame;

    payload = build_payload(frame_format, arbitration_id, xl_header, tx_data, tx_len,
                             &payload_len);
    if (!payload) return frame;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)frame_format;
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    free(payload);
    return frame;
}

//cfusa:req REQ-CANEP-017
//cfusa:req REQ-CANEP-018
rcp_ep_can_errc_t rcp_ep_can_decode_frame_request(const uint8_t *b, size_t len,
                                                   rcp_byte_bus_id_t expected_bus_id,
                                                   rcp_ep_can_frame_format_t *out_frame_format,
                                                   uint32_t *out_arbitration_id,
                                                   rcp_ep_can_xl_header_t *out_xl_header,
                                                   const uint8_t **out_tx_data,
                                                   size_t *out_tx_len,
                                                   uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    rcp_ep_can_frame_format_t    frame_format;
    size_t                       prefix_len;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_CAN_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_CAN_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_CAN_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_CAN_ERR_WRONG_OP;

    if (!rcp_ep_can_frame_format_valid(hdr.evt & 0x07u)) return RCP_EP_CAN_ERR_BAD_FRAME_FORMAT;
    frame_format = (rcp_ep_can_frame_format_t)(hdr.evt & 0x07u);

    prefix_len = prefix_len_for(frame_format);
    if (payload_len < prefix_len) return RCP_EP_CAN_ERR_SHORT_FRAME;

    read_prefix(payload, frame_format, out_arbitration_id, out_xl_header);

    *out_frame_format    = frame_format;
    *out_tx_data         = &payload[prefix_len];
    *out_tx_len          = payload_len - prefix_len;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_CAN_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-CANEP-019
rcp_bytes_t rcp_ep_can_encode_frame_response(rcp_byte_bus_id_t byte_bus_id,
                                              rcp_ep_can_frame_format_t frame_format,
                                              uint32_t arbitration_id,
                                              const rcp_ep_can_xl_header_t *xl_header,
                                              const uint8_t *rx_data, size_t rx_len,
                                              uint8_t transaction_num, bool timed,
                                              uint64_t timestamp)
{
    rcp_bytes_t frame = {0};
    uint8_t     *payload;
    size_t       payload_len;

    if (!encode_preconditions_ok(frame_format, arbitration_id, xl_header, rx_len)) return frame;

    payload = build_payload(frame_format, arbitration_id, xl_header, rx_data, rx_len,
                             &payload_len);
    if (!payload) return frame;

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.evt             = (uint8_t)frame_format;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        frame = rcp_acf_encode_gbb(&hdr, payload, payload_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.evt             = (uint8_t)frame_format;
        hdr.transaction_num = transaction_num;

        frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    }

    free(payload);
    return frame;
}

//cfusa:req REQ-CANEP-020
//cfusa:req REQ-CANEP-021
//cfusa:req REQ-CANEP-022
rcp_ep_can_errc_t rcp_ep_can_decode_frame_response(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    rcp_ep_can_frame_format_t *out_frame_format,
                                                    uint32_t *out_arbitration_id,
                                                    rcp_ep_can_xl_header_t *out_xl_header,
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
    uint8_t                      evt;
    uint8_t                      transaction_num;
    bool                         timed;
    uint64_t                     timestamp;
    rcp_ep_can_frame_format_t    frame_format;
    size_t                       prefix_len;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_CAN_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_CAN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_CAN_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        evt             = gbb_hdr.info.evt;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_CAN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_CAN_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        evt             = abb_hdr.evt;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_CAN_ERR_WRONG_BUS;

    if (!rcp_ep_can_frame_format_valid(evt & 0x07u)) return RCP_EP_CAN_ERR_BAD_FRAME_FORMAT;
    frame_format = (rcp_ep_can_frame_format_t)(evt & 0x07u);

    prefix_len = prefix_len_for(frame_format);
    if (payload_len < prefix_len) return RCP_EP_CAN_ERR_SHORT_FRAME;

    read_prefix(payload, frame_format, out_arbitration_id, out_xl_header);

    *out_frame_format    = frame_format;
    *out_rx_data         = &payload[prefix_len];
    *out_rx_len          = payload_len - prefix_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_CAN_OK;
}

/* ── Fragmented response (Phase 20, fragment.h) ────────────────────────────── */

//cfusa:req REQ-CANEP-023
size_t rcp_ep_can_frame_response_fragment_count(rcp_ep_can_frame_format_t frame_format,
                                                 uint32_t arbitration_id,
                                                 const rcp_ep_can_xl_header_t *xl_header,
                                                 size_t rx_len, size_t max_fragment_payload)
{
    size_t combined_len;

    if (!encode_preconditions_ok(frame_format, arbitration_id, xl_header, rx_len)) return 0;

    combined_len = prefix_len_for(frame_format) + rx_len;
    return rcp_fragment_plan_count(combined_len, max_fragment_payload);
}

//cfusa:req REQ-CANEP-024
size_t rcp_ep_can_encode_frame_response_fragmented(rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_ep_can_frame_format_t frame_format,
                                                    uint32_t arbitration_id,
                                                    const rcp_ep_can_xl_header_t *xl_header,
                                                    const uint8_t *rx_data, size_t rx_len,
                                                    uint8_t transaction_num, bool timed,
                                                    uint64_t timestamp,
                                                    size_t max_fragment_payload,
                                                    rcp_bytes_t *out_frames)
{
    uint8_t                *combined;
    size_t                   combined_len;
    size_t                   count;
    rcp_fragment_segment_t  *segs;
    size_t                   i;

    count = rcp_ep_can_frame_response_fragment_count(frame_format, arbitration_id, xl_header,
                                                       rx_len, max_fragment_payload);
    if (count == 0) return 0;

    combined = build_payload(frame_format, arbitration_id, xl_header, rx_data, rx_len,
                              &combined_len);
    if (!combined) return 0;

    segs = (rcp_fragment_segment_t *)malloc(count * sizeof(*segs));
    if (!segs) {
        free(combined);
        return 0;
    }

    if (rcp_fragment_plan(combined_len, max_fragment_payload, segs, count) != RCP_FRAGMENT_OK) {
        free(segs);
        free(combined);
        return 0;
    }

    for (i = 0; i < count; i++) {
        const uint8_t *slice     = &combined[segs[i].offset];
        size_t         slice_len = segs[i].len;
        rcp_bytes_t    frame;

        if (timed) {
            rcp_acf_gbb_header_t hdr = {0};

            hdr.info.byte_bus_id              = byte_bus_id;
            hdr.info.op                       = RCP_ACF_OP_READ;
            hdr.info.evt                      = (uint8_t)frame_format;
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
            hdr.evt                      = (uint8_t)frame_format;
            hdr.transaction_num          = transaction_num;
            hdr.ms                       = segs[i].ms ? 1u : 0u;
            hdr.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : 0u;

            frame = rcp_acf_encode_abb(&hdr, slice, slice_len);
        }

        if (!frame.data) {
            size_t j;

            for (j = 0; j < i; j++) rcp_bytes_free(&out_frames[j]);
            free(segs);
            free(combined);
            return 0;
        }

        out_frames[i] = frame;
    }

    free(segs);
    free(combined);
    return count;
}

//cfusa:req REQ-CANEP-025
//cfusa:req REQ-CANEP-026
rcp_ep_can_errc_t rcp_ep_can_decode_frame_response_fragment(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t expected_bus_id,
                                                             rcp_ep_can_frame_format_t *out_frame_format,
                                                             bool *out_ms,
                                                             uint8_t *out_segment_num,
                                                             const uint8_t **out_payload,
                                                             size_t *out_payload_len,
                                                             bool *out_timed,
                                                             uint64_t *out_timestamp,
                                                             uint8_t *out_transaction_num)
{
    uint8_t                      msg_type;
    rcp_acf_errc_t                acf_rc;
    rcp_acf_byte_message_info_t   abb_hdr;
    rcp_acf_gbb_header_t          gbb_hdr;
    const uint8_t                *payload;
    size_t                        payload_len;
    rcp_byte_bus_id_t             bus_id;
    uint8_t                       evt;
    uint8_t                       ms;
    /* This local is this endpoint's own pre-existing octet-wide type
     * (unchanged by the acf.c header rework); the wire field it's read
     * from below is now the header's real 12-bit uint16_t field, so both
     * assignments need an explicit narrowing cast for MSVC's /W4 -- same
     * reasoning and same pre-existing (not newly introduced) truncation
     * behavior as ep_uart.c's identical cast. */
    uint8_t                       segment_num;
    uint8_t                       transaction_num;
    bool                          timed;
    uint64_t                      timestamp;
    rcp_ep_can_frame_format_t     frame_format;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_CAN_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_CAN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_CAN_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        evt             = gbb_hdr.info.evt;
        ms              = gbb_hdr.info.ms;
        segment_num     = (uint8_t)gbb_hdr.info.read_size_or_segment_num;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_CAN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_CAN_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        evt             = abb_hdr.evt;
        ms              = abb_hdr.ms;
        segment_num     = (uint8_t)abb_hdr.read_size_or_segment_num;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_CAN_ERR_WRONG_BUS;

    if (!rcp_ep_can_frame_format_valid(evt & 0x07u)) return RCP_EP_CAN_ERR_BAD_FRAME_FORMAT;
    frame_format = (rcp_ep_can_frame_format_t)(evt & 0x07u);

    *out_frame_format    = frame_format;
    *out_ms              = (ms != 0u);
    *out_segment_num     = segment_num;
    *out_payload         = payload;
    *out_payload_len     = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_CAN_OK;
}

//cfusa:req REQ-CANEP-027
rcp_ep_can_errc_t rcp_ep_can_decode_reassembled_frame_response(const uint8_t *reassembled,
                                                                size_t reassembled_len,
                                                                rcp_ep_can_frame_format_t frame_format,
                                                                uint32_t *out_arbitration_id,
                                                                rcp_ep_can_xl_header_t *out_xl_header,
                                                                const uint8_t **out_rx_data,
                                                                size_t *out_rx_len)
{
    size_t prefix_len;

    if (!rcp_ep_can_frame_format_valid((uint8_t)frame_format)) return RCP_EP_CAN_ERR_BAD_FRAME_FORMAT;

    prefix_len = prefix_len_for(frame_format);
    if (reassembled_len < prefix_len) return RCP_EP_CAN_ERR_SHORT_FRAME;

    read_prefix(reassembled, frame_format, out_arbitration_id, out_xl_header);

    *out_rx_data = &reassembled[prefix_len];
    *out_rx_len  = reassembled_len - prefix_len;
    return RCP_EP_CAN_OK;
}
