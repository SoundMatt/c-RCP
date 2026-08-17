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

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* ── FrameFormat selection (payload leading quadlet, TC18 Table 57) ──────── */

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

//cfusa:req REQ-CANEP-030
bool rcp_ep_can_xl_frame_matches_provisioned_pl(bool xl_new_pl_provisioned,
                                                 rcp_ep_can_frame_format_t format)
{
    /* Physical-layer provisioning only concerns XL frames -- a non-XL
     * frame carries no PL choice of its own to conflict with, so it
     * trivially matches regardless of what is provisioned. */
    if (!rcp_ep_can_frame_format_is_xl(format)) return true;

    return xl_new_pl_provisioned ? (format == RCP_EP_CAN_FRAME_XL_NEW_PL)
                                  : (format == RCP_EP_CAN_FRAME_XL_CLASSICAL_PL);
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
     * exec_delay_clk_divider, every xl_filters[i] (id/mask/enable), and
     * xl_new_pl_provisioned are already zero/false via the memset
     * above. */
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

//cfusa:req REQ-CANEP-030
bool rcp_ep_can_set_xl_new_pl_provisioned(rcp_ep_can_functional_cfg_t *cfg,
                                           bool new_pl_provisioned,
                                           rcp_lifecycle_state_t state,
                                           rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_can_functional_cfg_writable(state, writer)) return false;

    cfg->xl_new_pl_provisioned = new_pl_provisioned;
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
    case RCP_EP_CAN_ERR_BAD_FRAME_FORMAT:  return "rcp/ep_can: invalid frame format";
    case RCP_EP_CAN_ERR_BAD_EVT:           return "rcp/ep_can: evt[2:0] is not 0b000";
    case RCP_EP_CAN_ERR_BAD_ARBITRATION_ID: return "rcp/ep_can: arbitration_id out of range for frame format";
    default:                              return "rcp/ep_can: unknown error";
    }
}

/* ── Wire layout helpers (TC18 §13.7.11.3 Figure 39) ─────────────────────── */

static size_t prefix_len_for(rcp_ep_can_frame_format_t format)
{
    return rcp_ep_can_frame_format_is_xl(format) ? (size_t)10u : (size_t)4u;
}

/* Figure 39's leading quadlet: frame_format in the top 3 bits, the
 * (right-aligned, for an 11-bit base id) arbitration_id in the bottom 29
 * bits. */
static void write_prefix(uint8_t *p, rcp_ep_can_frame_format_t format, uint32_t arbitration_id,
                          const rcp_ep_can_xl_header_t *xl_header)
{
    uint32_t combined = ((uint32_t)format << 29) | (arbitration_id & 0x1FFFFFFFu);

    put_u32(p, combined);
    if (rcp_ep_can_frame_format_is_xl(format)) {
        p[4] = xl_header->sdt;
        p[5] = xl_header->vcid;
        put_u32(&p[6], xl_header->af);
    }
}

/* Reads just the leading quadlet's top 3 bits -- the only field a caller
 * needs before it knows which format (and therefore which full prefix
 * length) it is looking at. */
static rcp_ep_can_frame_format_t read_frame_format(const uint8_t *p)
{
    return (rcp_ep_can_frame_format_t)((get_u32(p) >> 29) & 0x7u);
}

static void read_prefix(const uint8_t *p, rcp_ep_can_frame_format_t format,
                         uint32_t *out_arbitration_id, rcp_ep_can_xl_header_t *out_xl_header)
{
    *out_arbitration_id = get_u32(p) & 0x1FFFFFFFu;
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
    hdr.evt             = 0; /* TC18 Table 33: plain request in CAN's Row-2 -- frame_format now lives in the payload, see the file header */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    free(payload);
    payload = NULL;
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
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_CAN_ERR_BAD_EVT;

    if (payload_len < 4u) return RCP_EP_CAN_ERR_SHORT_FRAME;
    frame_format = read_frame_format(payload);
    if (!rcp_ep_can_frame_format_valid((uint8_t)frame_format)) return RCP_EP_CAN_ERR_BAD_FRAME_FORMAT;

    prefix_len = prefix_len_for(frame_format);
    if (payload_len < prefix_len) return RCP_EP_CAN_ERR_SHORT_FRAME;

    read_prefix(payload, frame_format, out_arbitration_id, out_xl_header);
    if (!rcp_ep_can_arbitration_id_valid(frame_format, *out_arbitration_id))
        return RCP_EP_CAN_ERR_BAD_ARBITRATION_ID;

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
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.evt             = 0; /* TC18 Table 33: plain response, see the file header */
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        frame = rcp_acf_encode_gbb(&hdr, payload, payload_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.evt             = 0; /* TC18 Table 33: plain request in CAN's Row-2 -- frame_format now lives in the payload, see the file header */
        hdr.transaction_num = transaction_num;

        frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    }

    free(payload);
    payload = NULL;
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
    if (!rcp_acf_evt_row2_is_plain(evt)) return RCP_EP_CAN_ERR_BAD_EVT;

    if (payload_len < 4u) return RCP_EP_CAN_ERR_SHORT_FRAME;
    frame_format = read_frame_format(payload);
    if (!rcp_ep_can_frame_format_valid((uint8_t)frame_format)) return RCP_EP_CAN_ERR_BAD_FRAME_FORMAT;

    prefix_len = prefix_len_for(frame_format);
    if (payload_len < prefix_len) return RCP_EP_CAN_ERR_SHORT_FRAME;

    read_prefix(payload, frame_format, out_arbitration_id, out_xl_header);
    if (!rcp_ep_can_arbitration_id_valid(frame_format, *out_arbitration_id))
        return RCP_EP_CAN_ERR_BAD_ARBITRATION_ID;

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
        combined = NULL;
        return 0;
    }

    if (rcp_fragment_plan(combined_len, max_fragment_payload, segs, count) != RCP_FRAGMENT_OK) {
        free(segs);
        segs = NULL;
        free(combined);
        combined = NULL;
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
            hdr.info.rsp                      = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
            hdr.info.evt                      = 0; /* TC18 Table 33: plain response, see the file header */
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
            hdr.evt                      = 0; /* TC18 Table 33: plain response, see the file header */
            hdr.transaction_num          = transaction_num;
            hdr.ms                       = segs[i].ms ? 1u : 0u;
            hdr.read_size_or_segment_num = segs[i].ms ? segs[i].segment_num : 0u;

            frame = rcp_acf_encode_abb(&hdr, slice, slice_len);
        }

        if (!frame.data) {
            size_t j;

            for (j = 0; j < i; j++) rcp_bytes_free(&out_frames[j]);
            free(segs);
            segs = NULL;
            free(combined);
            combined = NULL;
            return 0;
        }

        out_frames[i] = frame;
    }

    free(segs);
    segs = NULL;
    free(combined);
    combined = NULL;
    return count;
}

//cfusa:req REQ-CANEP-025
//cfusa:req REQ-CANEP-026
rcp_ep_can_errc_t rcp_ep_can_decode_frame_response_fragment(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t expected_bus_id,
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
    if (!rcp_acf_evt_row2_is_plain(evt)) return RCP_EP_CAN_ERR_BAD_EVT;

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
                                                                rcp_ep_can_frame_format_t *out_frame_format,
                                                                uint32_t *out_arbitration_id,
                                                                rcp_ep_can_xl_header_t *out_xl_header,
                                                                const uint8_t **out_rx_data,
                                                                size_t *out_rx_len)
{
    rcp_ep_can_frame_format_t frame_format;
    size_t                    prefix_len;

    if (reassembled_len < 4u) return RCP_EP_CAN_ERR_SHORT_FRAME;
    frame_format = read_frame_format(reassembled);
    if (!rcp_ep_can_frame_format_valid((uint8_t)frame_format)) return RCP_EP_CAN_ERR_BAD_FRAME_FORMAT;

    prefix_len = prefix_len_for(frame_format);
    if (reassembled_len < prefix_len) return RCP_EP_CAN_ERR_SHORT_FRAME;

    read_prefix(reassembled, frame_format, out_arbitration_id, out_xl_header);
    if (!rcp_ep_can_arbitration_id_valid(frame_format, *out_arbitration_id))
        return RCP_EP_CAN_ERR_BAD_ARBITRATION_ID;

    *out_frame_format = frame_format;
    *out_rx_data       = &reassembled[prefix_len];
    *out_rx_len        = reassembled_len - prefix_len;
    return RCP_EP_CAN_OK;
}

/* ── The EP_func register block (the evt[2:0] == 111b target), REQ-CANEP-028 ─ */

#define RCP_EP_CAN_REG_EP_LEN        ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_CAN_REG_RESERVED_01   ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_CAN_REG_EP_ENABLE_CLR ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_CAN_REG_EP_OPTIONS    ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_CAN_REG_BASE_CLK      ((uint16_t)0x0004u) /* 16 bit, R   */
#define RCP_EP_CAN_REG_EP_STATUS     ((uint16_t)0x0006u) /* 16 bit, R/W */
/* 0x0008-0x001B: clk_divider, two reserved regions, and the three "CAN
 * bit time register" fields plus TDCC -- not yet decomposed, see this
 * module's own header comment. Treated as one contiguous read-only span. */
#define RCP_EP_CAN_REG_UNDECOMPOSED_START ((uint16_t)0x0008u)
#define RCP_EP_CAN_REG_UNDECOMPOSED_LEN   ((uint16_t)0x0014u) /* 0x0008..0x001B */
#define RCP_EP_CAN_REG_STATUS        ((uint16_t)0x001Cu) /* 32 bit, R/W */
#define RCP_EP_CAN_REG_FIFO_STATUS   ((uint16_t)0x0020u) /* 32 bit, R/W */

/* TC18 Table 35 (EP functional config common entries) fixes ep_enable at
 * 0x0002.0 and ep_clear_req_storage at 0x0002.4 for EVERY endpoint type --
 * CAN's own Table 56 can_ep_enable&clr row (0x0002) explicitly defers to
 * Table 35 for this octet's bit layout rather than redefining it, so no
 * CAN-specific table can override this. 0x0002.1:3 is reserved (reads
 * 000b) in Table 35, so bit 1 -- this constant's previous, wrong value --
 * has no meaning of its own to collide with here. Every sibling endpoint
 * (ep_uart.c/ep_lin.c/ep_adc.c/ep_iseled.c/ep_mdio.c) already defines its
 * own *_ENABLE_CLR_BIT_CLEAR as (1u<<4); this module alone used (1u<<1),
 * a wire-format conformance defect (issue #470) that made
 * ep_clear_req_storage react to the wrong wire bit -- a real TC18 peer
 * setting bit 4 per spec would have had no effect on this endpoint. */
//cfusa:req REQ-CANEP-028
#define CAN_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define CAN_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))

#define CAN_OPTIONS_BIT_REQ_CRC  ((uint8_t)(1u << 0))
#define CAN_OPTIONS_BIT_RESP_TS  ((uint8_t)(1u << 3))
#define CAN_OPTIONS_BIT_SUPPRESS ((uint8_t)(1u << 7))

//cfusa:req REQ-CANEP-028
void rcp_ep_can_render_registers(const rcp_ep_can_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_CAN_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;

    if (cfg->common.ep_enable) enable_clr |= CAN_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= CAN_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= CAN_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= CAN_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= CAN_OPTIONS_BIT_SUPPRESS;

    out[RCP_EP_CAN_REG_EP_LEN]        = (uint8_t)RCP_EP_CAN_EP_FUNC_LEN;
    out[RCP_EP_CAN_REG_RESERVED_01]   = 0u;
    out[RCP_EP_CAN_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_CAN_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_CAN_REG_BASE_CLK], 0u); /* no real clock source
                                                    modelled -- see the
                                                    file header */
    put_u16(&out[RCP_EP_CAN_REG_EP_STATUS], cfg->ep_status);
    memset(&out[RCP_EP_CAN_REG_UNDECOMPOSED_START], 0,
           RCP_EP_CAN_REG_UNDECOMPOSED_LEN); /* not yet decomposed -- see
                                                 the file header */
    put_u32(&out[RCP_EP_CAN_REG_STATUS], cfg->status);
    put_u32(&out[RCP_EP_CAN_REG_FIFO_STATUS], cfg->fifo_status);
}

/* The inverse of render: adopts every R/W register from an already
 * patched block image. The read-only offsets (EP_LEN, the reserved
 * octet, base_clk, and the whole not-yet-decomposed span) are
 * deliberately not read back -- apply_reconfig() re-renders them from
 * cfg before patching, so a write covering them is a no-op. */
static void parse_can_registers(rcp_ep_can_functional_cfg_t *cfg,
                                 const uint8_t in[RCP_EP_CAN_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_CAN_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_CAN_REG_EP_OPTIONS];

    cfg->common.ep_enable             = (enable_clr & CAN_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage  = (enable_clr & CAN_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable     = (options & CAN_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & CAN_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & CAN_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status   = get_u16(&in[RCP_EP_CAN_REG_EP_STATUS]);
    cfg->status      = get_u32(&in[RCP_EP_CAN_REG_STATUS]);
    cfg->fifo_status = get_u32(&in[RCP_EP_CAN_REG_FIFO_STATUS]);
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, the reserved octet, both octets of
 * base_clk, or any octet of the not-yet-decomposed 0x0008-0x001B span. */
static bool can_reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_CAN_REG_EP_LEN || addr == RCP_EP_CAN_REG_RESERVED_01 ||
           (addr >= RCP_EP_CAN_REG_BASE_CLK && addr < RCP_EP_CAN_REG_BASE_CLK + 2u) ||
           (addr >= RCP_EP_CAN_REG_UNDECOMPOSED_START &&
            addr < RCP_EP_CAN_REG_UNDECOMPOSED_START + RCP_EP_CAN_REG_UNDECOMPOSED_LEN);
}

//cfusa:req REQ-CANEP-028
const char *rcp_ep_can_reconfig_strerror(rcp_ep_can_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_CAN_RECONFIG_OK:
        return "rcp/ep_can: CAN configuration write applied";
    case RCP_EP_CAN_RECONFIG_ERR_SHORT:
        return "rcp/ep_can: CAN configuration write has no address and data";
    case RCP_EP_CAN_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_can: CAN configuration write extends past the EP_func block";
    default:
        return "rcp/ep_can: CAN unknown configuration-write error";
    }
}

//cfusa:req REQ-CANEP-028
rcp_ep_can_reconfig_errc_t rcp_ep_can_apply_reconfig(rcp_ep_can_functional_cfg_t *cfg,
                                                      const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_CAN_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_CAN_RECONFIG_ADDR_LEN) {
        return RCP_EP_CAN_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_CAN_RECONFIG_ADDR_LEN;

    if ((size_t)start_address + data_len > (size_t)RCP_EP_CAN_EP_FUNC_LEN) {
        return RCP_EP_CAN_RECONFIG_ERR_OUT_OF_RANGE;
    }

    rcp_ep_can_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (can_reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + i];
    }
    parse_can_registers(cfg, block);

    return RCP_EP_CAN_RECONFIG_OK;
}
