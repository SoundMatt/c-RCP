/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/regmap.h"

#include <stdint.h>
#include <string.h>

/* ── EP0 ────────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-001
bool rcp_regmap_is_ep0(uint16_t ep_index)
{
    return ep_index == RCP_REGMAP_EP0_INDEX;
}

/* ── svr_implemented_options: REQ-RMAP-004..008 retired (REQ-RMAP-030) ────── */

//cfusa:req REQ-RMAP-004
//cfusa:req REQ-RMAP-005
//cfusa:req REQ-RMAP-006
//cfusa:req REQ-RMAP-007
//cfusa:req REQ-RMAP-008
/* REQ-RMAP-004..008 (formerly: "svr_implemented_options bit assignments
 * are pairwise distinct" / rcp_regmap_options_group_consistent()'s own
 * four accept/reject behaviors) are RETIRED as of this milestone --
 * see .fusa-reqs.json for each entry's own full retirement text. Direct
 * primary-source verification (OA_TC18_specification_v_0.5.1_RC.pdf,
 * §12.9.1.1, page 64) found their shared citation incorrect: that
 * section is entirely about an RC Server handling multiple ACF-type
 * requests packed into one AVTPDU frame -- it says nothing about
 * svr_implemented_options, feature advertisement, or any bit-pairing
 * rule. The all-or-nothing-pair grouping these five requirements
 * described, and the rcp_regmap_options_group_consistent() function
 * that enforced it, had no TC18 basis and are removed outright (not
 * deprecated-then-removed; there is no correct behavior to preserve a
 * transition window for). REQ-RMAP-030's own field comment (regmap.h)
 * gives the correct, primary-source-verified replacement: five
 * independent single bits, matching Table 20 exactly. */

/* ── The general register map ──────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-003
//cfusa:req REQ-RMAP-023
void rcp_regmap_general_init(rcp_regmap_general_t *map)
{
    memset(map, 0, sizeof(*map));
    map->svr_root_client_index = RCP_REGMAP_NO_ROOT_CLIENT;
    /* svr_lifecycle_state == 0 == RCP_LIFECYCLE_HW_UNCONFIGURED, the
     * correct default -- matches every real server's own starting
     * rcp_lifecycle_state_t (see server.h/mock.h). */
}

/* ── Table 20 wire codec (REQ-RMAP-024) ──────────────────────────────────────
 * This TU's own copy of the byte-order helpers, matching acf.c's/avtp.c's/
 * discovery.c's house convention of not sharing one across modules. */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* Added for request-stream-cfg's own rx_stream_id (64 bit, issue #306) --
 * no prior table in this TU needed a field this wide. */
static void put_u64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)((v >> 56) & 0xFFu);
    p[1] = (uint8_t)((v >> 48) & 0xFFu);
    p[2] = (uint8_t)((v >> 40) & 0xFFu);
    p[3] = (uint8_t)((v >> 32) & 0xFFu);
    p[4] = (uint8_t)((v >> 24) & 0xFFu);
    p[5] = (uint8_t)((v >> 16) & 0xFFu);
    p[6] = (uint8_t)((v >> 8)  & 0xFFu);
    p[7] = (uint8_t)(v & 0xFFu);
}

static uint64_t get_u64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |  (uint64_t)p[7];
}

//cfusa:req REQ-RMAP-024
//cfusa:req REQ-RMAP-039
void rcp_regmap_general_render(const rcp_regmap_general_t *map, uint8_t out[RCP_REGMAP_GENERAL_LEN])
{
    memset(out, 0, RCP_REGMAP_GENERAL_LEN);

    put_u32(&out[0x0000], map->magic);
    put_u32(&out[0x0004], map->svr_version);
    put_u16(&out[0x0008], map->vendor_id);
    put_u16(&out[0x000A], map->device_id);
    put_u16(&out[0x000C], map->svr_ep_count);
    /* 0x000E..0x000F: svr_lifecycle_state deliberately NOT rendered here --
     * see this function's own doc comment (regmap.h). */
    out[0x000E] = map->svr_req_stream_max;
    out[0x000F] = map->svr_responder_streams_max;
    put_u16(&out[0x0010], map->svr_responder_mem_size);
    put_u16(&out[0x0012], map->svr_req_mem_size);
    out[0x0014] = map->svr_sequencers_max;
    out[0x0015] = map->svr_configuration_lock;
    out[0x0016] = map->svr_implemented_options;
    out[0x0017] = map->reserved_0x17;
    put_u16(&out[0x0018], map->svr_io_pin_count);
    /* svr_root_client_index deliberately NOT rendered here -- same
     * exclusion as svr_lifecycle_state, see this function's own doc
     * comment (regmap.h). */
    put_u16(&out[0x001A], map->svr_hw_cfg_ptr);
    out[0x001C] = map->svr_request_stream_cfg_capacity;
    out[0x001D] = map->svr_response_stream_cfg_capacity;
    put_u16(&out[0x001E], map->svr_request_stream_cfg_ptr);
    put_u16(&out[0x0020], map->svr_response_stream_cfg_ptr);
    put_u16(&out[0x0022], map->reserved_0x22);
    put_u16(&out[0x0024], map->svr_ep_generic_cfg_ptr);
    put_u16(&out[0x0026], map->svr_ep_generic_cfg_capacity);
    put_u16(&out[0x0028], map->svr_ep_bytebus_id_map_ptr);
    out[0x002A] = map->svr_ep_bytebus_id_map_capacity;
    /* 0x002B: inferred, unconfirmed one-octet alignment gap -- left 0x00
     * by the memset above, see this function's own doc comment (regmap.h). */
    put_u16(&out[0x002C], map->svr_ep_functional_cfg_ptr);
    put_u16(&out[0x002E], map->svr_sequencer_state_ptr);
    put_u16(&out[0x0030], map->svr_network_interface_cfg_ptr);
    put_u16(&out[0x0032], map->svr_network_interface_cfg_capacity);
    put_u16(&out[0x0034], map->svr_physical_layer_cfg_ptr);
    put_u16(&out[0x0036], map->svr_physical_layer_cfg_capacity);
    put_u16(&out[0x0038], map->svr_time_synch_cfg_ptr);
    put_u16(&out[0x003A], map->svr_time_synch_cfg_capacity);
    put_u16(&out[0x003C], map->svr_security_cfg_ptr);
    put_u16(&out[0x003E], map->svr_security_cfg_capacity);
    put_u16(&out[0x0040], map->svr_device_specific_cfg_ptr);
    put_u16(&out[0x0042], map->svr_device_specific_cfg_capacity);
}

//cfusa:req REQ-RMAP-024
const char *rcp_regmap_general_strerror(rcp_regmap_general_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_GENERAL_OK:               return "rcp/regmap: success";
    case RCP_REGMAP_GENERAL_ERR_SHORT_FRAME:  return "rcp/regmap: frame too short";
    case RCP_REGMAP_GENERAL_ERR_BAD_MSG_TYPE: return "rcp/regmap: unexpected ACF message type";
    case RCP_REGMAP_GENERAL_ERR_WRONG_BUS:    return "rcp/regmap: wrong byte_bus_id";
    case RCP_REGMAP_GENERAL_ERR_WRONG_OP:     return "rcp/regmap: wrong ACF op";
    default:                                  return "rcp/regmap: unknown error";
    }
}

//cfusa:req REQ-RMAP-024
rcp_bytes_t rcp_regmap_general_encode_read_response(const rcp_regmap_general_t *map,
                                                      uint8_t read_size,
                                                      uint8_t transaction_num)
{
    uint8_t                     image[RCP_REGMAP_GENERAL_LEN];
    uint8_t                     payload[256];
    size_t                      copy_len;
    rcp_acf_byte_message_info_t hdr = {0};

    rcp_regmap_general_render(map, image);

    memset(payload, 0, sizeof(payload));
    copy_len = ((size_t)read_size < RCP_REGMAP_GENERAL_LEN) ? (size_t)read_size
                                                             : RCP_REGMAP_GENERAL_LEN;
    memcpy(payload, image, copy_len);

    hdr.byte_bus_id              = RCP_REGMAP_EP0_INDEX;
    hdr.op                       = RCP_ACF_OP_READ;
    hdr.rsp                      = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
    hdr.read_size_or_segment_num = read_size;
    hdr.transaction_num          = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, read_size);
}

//cfusa:req REQ-RMAP-024
//cfusa:req REQ-RMAP-039
rcp_regmap_general_errc_t rcp_regmap_general_decode_read_response(const uint8_t *b, size_t len,
                                                                    rcp_regmap_general_t *out_map)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    uint8_t                      image[RCP_REGMAP_GENERAL_LEN];
    size_t                       have;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_REGMAP_GENERAL_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_REGMAP_GENERAL_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != RCP_REGMAP_EP0_INDEX) return RCP_REGMAP_GENERAL_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_REGMAP_GENERAL_ERR_WRONG_OP;

    /* A short response (read_size smaller than the full extent) is not an
     * error -- it just carries fewer trailing fields; only the fields that
     * fit are overwritten, matching this function's own doc comment. */
    memset(image, 0, sizeof(image));
    have = (payload_len < RCP_REGMAP_GENERAL_LEN) ? payload_len : RCP_REGMAP_GENERAL_LEN;
    memcpy(image, payload, have);

    out_map->magic       = get_u32(&image[0x0000]);
    out_map->svr_version = get_u32(&image[0x0004]);
    out_map->vendor_id   = get_u16(&image[0x0008]);
    out_map->device_id   = get_u16(&image[0x000A]);
    out_map->svr_ep_count = get_u16(&image[0x000C]);
    if (have <= 0x000E) return RCP_REGMAP_GENERAL_OK;
    out_map->svr_req_stream_max         = image[0x000E];
    out_map->svr_responder_streams_max  = image[0x000F];
    out_map->svr_responder_mem_size     = get_u16(&image[0x0010]);
    out_map->svr_req_mem_size           = get_u16(&image[0x0012]);
    out_map->svr_sequencers_max         = image[0x0014];
    out_map->svr_configuration_lock     = image[0x0015];
    out_map->svr_implemented_options    = image[0x0016];
    out_map->reserved_0x17              = image[0x0017];
    out_map->svr_io_pin_count           = get_u16(&image[0x0018]);
    out_map->svr_hw_cfg_ptr             = get_u16(&image[0x001A]);
    out_map->svr_request_stream_cfg_capacity  = image[0x001C];
    out_map->svr_response_stream_cfg_capacity = image[0x001D];
    out_map->svr_request_stream_cfg_ptr       = get_u16(&image[0x001E]);
    out_map->svr_response_stream_cfg_ptr      = get_u16(&image[0x0020]);
    out_map->reserved_0x22                    = get_u16(&image[0x0022]);
    out_map->svr_ep_generic_cfg_ptr           = get_u16(&image[0x0024]);
    out_map->svr_ep_generic_cfg_capacity      = get_u16(&image[0x0026]);
    out_map->svr_ep_bytebus_id_map_ptr        = get_u16(&image[0x0028]);
    out_map->svr_ep_bytebus_id_map_capacity   = image[0x002A];
    out_map->svr_ep_functional_cfg_ptr        = get_u16(&image[0x002C]);
    out_map->svr_sequencer_state_ptr          = get_u16(&image[0x002E]);
    out_map->svr_network_interface_cfg_ptr      = get_u16(&image[0x0030]);
    out_map->svr_network_interface_cfg_capacity = get_u16(&image[0x0032]);
    out_map->svr_physical_layer_cfg_ptr         = get_u16(&image[0x0034]);
    out_map->svr_physical_layer_cfg_capacity    = get_u16(&image[0x0036]);
    out_map->svr_time_synch_cfg_ptr             = get_u16(&image[0x0038]);
    out_map->svr_time_synch_cfg_capacity        = get_u16(&image[0x003A]);
    out_map->svr_security_cfg_ptr               = get_u16(&image[0x003C]);
    out_map->svr_security_cfg_capacity          = get_u16(&image[0x003E]);
    out_map->svr_device_specific_cfg_ptr        = get_u16(&image[0x0040]);
    out_map->svr_device_specific_cfg_capacity   = get_u16(&image[0x0042]);
    return RCP_REGMAP_GENERAL_OK;
}

//cfusa:req REQ-RMAP-025
rcp_regmap_general_errc_t rcp_regmap_general_decode_write_request(const uint8_t *b, size_t len,
                                                                    rcp_wire_error_t *out_error,
                                                                    uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    rcp_lifecycle_writer_ctx_t   writer = {0};

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_REGMAP_GENERAL_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_REGMAP_GENERAL_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != RCP_REGMAP_EP0_INDEX) return RCP_REGMAP_GENERAL_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_REGMAP_GENERAL_ERR_WRONG_OP;

    /* RCP_LIFECYCLE_FIELD_READ_ONLY is unconditional -- state/writer never
     * change the outcome (see lifecycle.c's own switch case) -- so which
     * concrete state/writer is passed here does not matter; any value
     * yields the same RCP_ERROR_LOCKED_MEM_ACCESS, exactly as
     * REQ-RMAP-025 (regmap.h's own field comment) already documents. */
    *out_error = rcp_lifecycle_field_write_error(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_READ_ONLY, writer);
    *out_transaction_num = hdr.transaction_num;
    return RCP_REGMAP_GENERAL_OK;
}

/* ── HW_config server-side storage + wire codec (REQ-RMAP-040/041) ────────── */

//cfusa:req REQ-RMAP-041
void rcp_regmap_hw_pin_map_render(const rcp_regmap_hw_pin_map_entry_t *entries, size_t len,
                                   uint8_t *out)
{
    size_t i;

    for (i = 0; i < len; i++) {
        out[3u * i + 0u] = entries[i].hw_ep_nr;
        out[3u * i + 1u] = entries[i].hw_ep_pin_nr;
        out[3u * i + 2u] = entries[i].hw_pin_type;
    }
}

//cfusa:req REQ-RMAP-040
const char *rcp_regmap_hw_pin_map_reconfig_strerror(rcp_regmap_hw_pin_map_reconfig_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK:
        return "rcp/regmap: HW_config write applied";
    case RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_SHORT:
        return "rcp/regmap: HW_config write has no data";
    case RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/regmap: HW_config write extends past the table's own current extent";
    default:
        return "rcp/regmap: HW_config unknown configuration-write error";
    }
}

//cfusa:req REQ-RMAP-040
rcp_regmap_hw_pin_map_reconfig_errc_t
rcp_regmap_hw_pin_map_apply_reconfig(rcp_regmap_hw_pin_map_entry_t *entries, size_t count,
                                      uint16_t relative_start_address,
                                      const uint8_t *data, size_t data_len)
{
    uint8_t block[RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES * 3u];
    size_t  block_len = count * 3u;
    size_t  i;

    if (data_len == 0u) return RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_SHORT;

    if ((size_t)relative_start_address + data_len > block_len) {
        return RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Same "render current image, patch the addressed octets, re-parse
     * the whole image back" idiom every other endpoint type's own
     * apply_reconfig() already uses -- see this function's own header
     * doc comment. */
    rcp_regmap_hw_pin_map_render(entries, count, block);
    for (i = 0; i < data_len; i++) {
        block[relative_start_address + i] = data[i]; /* every octet R/W*, none read-only */
    }
    for (i = 0; i < count; i++) {
        entries[i].hw_ep_nr     = block[3u * i + 0u];
        entries[i].hw_ep_pin_nr = block[3u * i + 1u];
        entries[i].hw_pin_type  = block[3u * i + 2u];
    }

    return RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK;
}

/* ── EP0 address-routed dispatcher (issue #301) ─────────────────────────────── */

//cfusa:req REQ-RMAP-040
//cfusa:req REQ-RMAP-041
const char *rcp_regmap_ep0_strerror(rcp_regmap_ep0_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_EP0_OK:                return "rcp/regmap: success";
    case RCP_REGMAP_EP0_ERR_SHORT_FRAME:   return "rcp/regmap: frame too short";
    case RCP_REGMAP_EP0_ERR_BAD_MSG_TYPE:  return "rcp/regmap: unexpected ACF message type";
    case RCP_REGMAP_EP0_ERR_WRONG_BUS:     return "rcp/regmap: wrong byte_bus_id";
    case RCP_REGMAP_EP0_ERR_WRONG_OP:      return "rcp/regmap: wrong ACF op";
    case RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD: return "rcp/regmap: payload has no room for its own leading address";
    default:                               return "rcp/regmap: unknown error";
    }
}

/* response-queue-config's own 10-octet row is MIXED per-field (issue
 * #308): STREAM_UID [0,2) and flush_on_count/Flush_time [6,10) are
 * TC18 R/W+; Max_AVTPDUsize/queue_size [2,6) are R/W*. A single write
 * may span more than one field (or even more than one row, since every
 * row repeats the identical pattern), so this walks every octet the
 * write's own [relative_start_address, relative_start_address+data_len)
 * range touches and requires whichever access type(s) it actually
 * touches to authorize -- not just the first octet's own type. Returns
 * RCP_ERROR_NONE if authorized (both, if both types are touched and
 * both permit), else the first denial found, checked W+ before W* (an
 * arbitrary but deterministic tie-break when a write touches both and
 * only one permits). */
//cfusa:req REQ-RMAP-061
static rcp_wire_error_t respqueue_cfg_row_write_authorize(rcp_lifecycle_state_t state,
                                                            rcp_lifecycle_writer_ctx_t writer,
                                                            bool locked,
                                                            uint16_t relative_start_address,
                                                            size_t data_len)
{
    bool   touches_w_plus = false;
    size_t i;

    /* CORRECTED 2026-08-13 (issue #338, REQ-LIFECYCLE-023): QUEUE_CFG
     * (this whole table, TC18 §12.7.9 Table 27 "Responder QUEUE_config")
     * is one of the three tables Figure 17's own HW_CONFIGURED-box
     * transition explicitly locks -- "Request on discovery stream or
     * known stream/bb_id for configuration to HW_CONFIG or QUEUE_CFG or
     * EP_GEN_CFG -> send error response LOCKED_CONFIG_ACCESS"
     * (TC18.txt L2485-2488, directly confirmed against the rendered PDF
     * page image, not just text extraction) -- the SAME RCP_LIFECYCLE_
     * FIELD_HW_GENERIC rule HW_config itself already uses, not the
     * generic per-field R/W-star or R/W-plus default this function
     * previously applied. rcp_lifecycle_field_write_error()'s own doc comment
     * (lifecycle.h) already named this exact block as belonging to
     * HW_GENERIC -- this function itself had simply never been updated
     * to match. Checked first, table-wide: once this fails, no
     * per-field distinction below matters. */
    if (!rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_HW_GENERIC, writer)) {
        return rcp_lifecycle_field_write_error(state, RCP_LIFECYCLE_FIELD_HW_GENERIC, writer);
    }

    /* Within the HW_UNCONFIGURED window HW_GENERIC alone permits, this
     * table's own STREAM_UID/flush_on_count/Flush_time fields still
     * carry their own INDEPENDENT R/W+ lock bit ("independently of the
     * lifecycle state that governs W and W*", TC18's own words,
     * §12.7.8 -- REQ-RMAP-055's own doc comment). Checked directly
     * against `locked` here rather than via
     * rcp_lifecycle_field_writable_w_plus(), which internally composes
     * against FUNCTIONAL_W_STAR's own, now-superseded-for-this-table
     * lifecycle rule, not HW_GENERIC's -- reusing it here would silently
     * reintroduce the very bug this fix closes. Max_AVTPDUsize/
     * queue_size (the table's own W* fields) need no separate check:
     * HW_GENERIC's own table-wide gate above already subsumes whatever
     * W* would have additionally required. */
    for (i = 0; i < data_len; i++) {
        size_t row_offset = ((size_t)relative_start_address + i) % 10u;
        if (row_offset < 2u || row_offset >= 6u) {
            touches_w_plus = true;
            break;
        }
    }

    if (touches_w_plus && locked) return RCP_ERROR_LOCKED_MEM_ACCESS;

    return RCP_ERROR_NONE;
}

/* REQ-SEQ-013 (TC18 §12.7.10 Table 28, issue #335): SEQUENCER_config's
 * own 2-octet row is authorization-mixed in a way no other table in this
 * file is -- on top of the ordinary FUNCTIONAL_W_STAR lifecycle gate
 * every write to this table needs, EACH octet the write's own span
 * touches is additionally checked against the target sequencer's own
 * CURRENT owner: a Seq_state octet (row-relative 0) requires
 * requester_stream_index to already equal that owner, fail-closed on an
 * unclaimed sequencer (mirrors rcp_sequencer_access_permitted(),
 * request_sequencer.h, without this file depending on that header); a
 * Request_stream_index octet (row-relative 1) is permitted if the
 * sequencer is currently unclaimed (first claim) or requester_stream_
 * index already owns it (reassign/release its own) -- see
 * rcp_regmap_ep0_decode_write_request()'s own doc comment (regmap.h)
 * for the full user-approved design rationale. An octet whose own row
 * index is >= count is skipped here (out of range for THIS table) --
 * this function's own caller checks the write's overall bounds
 * separately, the same "authorize first, bounds-check after" ordering
 * already used for the write as a whole. */
static rcp_wire_error_t sequencer_row_write_authorize(rcp_lifecycle_state_t state,
                                                        rcp_lifecycle_writer_ctx_t writer,
                                                        const uint8_t *owner, size_t count,
                                                        uint8_t requester_stream_index,
                                                        uint16_t relative_start_address,
                                                        size_t data_len)
{
    size_t i;

    if (!rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer)) {
        return rcp_lifecycle_field_write_error(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer);
    }

    for (i = 0; i < data_len; i++) {
        size_t  row_index  = ((size_t)relative_start_address + i) / 2u;
        size_t  row_offset = ((size_t)relative_start_address + i) % 2u;
        uint8_t cur_owner;

        if (row_index >= count) continue;
        cur_owner = owner[row_index];

        if (row_offset == 0u) { /* Seq_state */
            if (cur_owner == 0u || cur_owner != requester_stream_index) {
                return RCP_ERROR_UNAUTHORIZED_ACCESS;
            }
        } else { /* Request_stream_index */
            if (cur_owner != 0u && cur_owner != requester_stream_index) {
                return RCP_ERROR_UNAUTHORIZED_ACCESS;
            }
        }
    }

    return RCP_ERROR_NONE;
}

/* REQ-E2E-046/REQ-RMAP-051 (issue #424): request-stream-cfg (TC18
 * §12.7.7 Table 24) is entirely R/W* -- writable only in
 * HW_UNCONFIGURED/HW_CONFIGURED, permanently read-only once
 * RCP_CONFIGURED -- for every field EXCEPT ONE: row-relative offset
 * 0x000D bit 7 (rx_stream_status) is Table 24's own distinct, plain R/W
 * bit in that same octet, genuinely different from the real R/W*
 * enforcement-config bits sharing it. CORRECTED 2026-08-14 (issue
 * #458): that octet's real bits [3:0] are TC18 RC5's own
 * rx_enforce_crc/rx_enforce_sequence/rx_enforce_watchdog/
 * rx_enforce_request_filing (bits [6:4] are Reserved, R only, carrying
 * no field at all) -- NOT the eight independently-configurable bits an
 * older revision of this comment described; see
 * rcp_regmap_request_stream_cfg_render()'s own doc comment (regmap.h)
 * for the full reconciliation this fix made.
 *
 * FUNCTIONAL_W_STAR (checked first, exactly as every other table's own
 * write path in this file does) is the correct, sufficient rule for
 * EVERY octet in this table without exception -- once it passes, the
 * whole write is authorized and no further check is needed. This
 * function's own carve-out only matters when it FAILS (state is
 * RCP_CONFIGURED): a write whose own final effective bytes (write_data,
 * already SET/OR/AND/XOR-combined by the caller -- see
 * rcp_regmap_ep0_decode_write_request()'s own call site) leave bits
 * [6:0] of row-relative octet 0x000D unchanged from the table's own
 * CURRENT rendered content (current_rendered, a full-table image at the
 * identical 24-octet-per-row stride, or NULL if the caller could not
 * resolve one, e.g. the write's own span exceeded its own fixed
 * scratch buffer -- fails closed/denies in that case, matching this
 * file's own established fail-closed convention for every other
 * unresolvable case) is still authorized, since TC18 places no
 * lifecycle restriction on that one bit; a write that would ALSO change
 * any of bits [6:0] remains denied even if it also touches bit 7 -- this
 * carve-out must never let a write disguised as a status-bit update
 * smuggle a change to any of those genuinely safety-relevant enforcement
 * bits through once FUNCTIONAL_W_STAR's own window has closed (the mask
 * is deliberately still the full bits-[6:0] span, not narrowed to the
 * real bits-[3:0] content -- denying a write that ALSO touches a
 * Reserved bit is strictly more conservative than necessary, never less,
 * so it needed no change for this fix). A write touching any OTHER
 * octet in the row is denied outright once FUNCTIONAL_W_STAR itself has
 * failed, with no carve-out of its own -- this function's own bit-7
 * exception is the ONLY relief from FUNCTIONAL_W_STAR this table's write
 * path grants. */
static bool request_stream_cfg_row_write_authorize(rcp_lifecycle_state_t state,
                                                     rcp_lifecycle_writer_ctx_t writer,
                                                     uint16_t relative_start_address,
                                                     const uint8_t *write_data, size_t data_len,
                                                     const uint8_t *current_rendered)
{
    size_t i;

    if (rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer)) {
        return true;
    }

    if (current_rendered == NULL) return false;

    for (i = 0; i < data_len; i++) {
        uint16_t addr_i        = (uint16_t)(relative_start_address + i);
        uint16_t offset_in_row = (uint16_t)(addr_i % 24u);

        if (offset_in_row != 0x000Du) return false;
        if ((write_data[i] & 0x7Fu) != (current_rendered[addr_i] & 0x7Fu)) return false;
    }

    return true;
}

/* ── Optional-subsystem config sections: storage + wire codec
 * (REQ-RMAP-039) -- see regmap.h's own file-header note on
 * rcp_regmap_optional_subsystem_cfg_t for why these four sections are
 * opaque byte buffers, not row-typed tables. ────────────────────────── */

//cfusa:req REQ-RMAP-039
const char *
rcp_regmap_optional_subsystem_cfg_reconfig_strerror(rcp_regmap_optional_subsystem_cfg_reconfig_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_OK:
        return "rcp/regmap: optional-subsystem config write applied";
    case RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_SHORT:
        return "rcp/regmap: optional-subsystem config write has no data";
    case RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/regmap: optional-subsystem config write extends past the section's own current extent";
    default:
        return "rcp/regmap: optional-subsystem config unknown configuration-write error";
    }
}

//cfusa:req REQ-RMAP-039
rcp_regmap_optional_subsystem_cfg_reconfig_errc_t
rcp_regmap_optional_subsystem_cfg_apply_reconfig(rcp_regmap_optional_subsystem_cfg_t *cfg,
                                                  uint16_t relative_start_address,
                                                  const uint8_t *data, size_t data_len)
{
    if (data_len == 0u) return RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_SHORT;

    if ((size_t)relative_start_address + data_len > cfg->len) {
        return RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* No render-patch-reparse idiom needed here, unlike every row-typed
     * table's own apply_reconfig() -- cfg->data IS the wire image
     * already, a direct bounded memcpy. */
    memcpy(&cfg->data[relative_start_address], data, data_len);
    return RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_OK;
}

/* REQ-WAKEUP-020 (issue #336): peeks what entries[]'s own wire-encoded
 * ep_id octets would become after applying data[0..data_len) at
 * relative_start_address, without mutating entries itself -- the
 * "render current image, patch the addressed octets" half of
 * rcp_regmap_ep_id_map_apply_reconfig()'s own idiom, stopped short of
 * the reparse-back-into-entries[] step, since this helper's only job
 * is to check the WOULD-BE result before committing to it (this
 * dispatcher's own established "authorize/validate first, apply after"
 * ordering, same as every access-control check above). Returns true
 * (write may proceed) iff ep_types is NULL (no enforcement configured
 * for this table -- same NULL-means-absent convention every other
 * optional check in this dispatcher already uses), OR every row whose
 * own ep_types[i] == target_ep_type would end up with the wire row's
 * own 8-bit ep_id octet (relative offset 1 within each 4-octet row,
 * matching rcp_regmap_ep_id_map_render()'s own established truncation)
 * equal to (uint8_t)required_ep_id after this write. A write whose own
 * [relative_start_address, relative_start_address+data_len) would
 * overflow this function's own fixed scratch buffer is left unchecked
 * here (returns true) -- rcp_regmap_ep_id_map_apply_reconfig() itself
 * separately rejects any such out-of-range write with
 * RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_OUT_OF_RANGE right after this
 * check, so skipping the fixed-ep_id check for it is safe: the write
 * never actually gets applied either way. */
static bool ep_id_map_write_keeps_fixed_ep_id(const rcp_regmap_ep_id_map_entry_t *entries,
                                               size_t count, const uint8_t *ep_types,
                                               uint8_t target_ep_type, uint16_t required_ep_id,
                                               uint16_t relative_start_address,
                                               const uint8_t *data, size_t data_len)
{
    uint8_t block[RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES * 4u];
    size_t  i;

    if (ep_types == NULL) return true;
    if ((size_t)relative_start_address + data_len > sizeof(block)) return true;

    rcp_regmap_ep_id_map_render(entries, count, block);
    for (i = 0; i < data_len; i++) {
        block[relative_start_address + i] = data[i];
    }

    for (i = 0; i < count; i++) {
        if (ep_types[i] == target_ep_type && block[4u * i + 1u] != (uint8_t)required_ep_id) {
            return false;
        }
    }

    return true;
}

/* Shared per-section write-routing check for the write dispatcher below:
 * true (and *out_error set) iff addr falls within [table_ptr,
 * table_ptr + cfg->len) and cfg is non-NULL -- the caller returns
 * RCP_REGMAP_EP0_OK immediately whenever this returns true, the same
 * "one helper call, one early return" shape
 * respqueue_cfg_row_write_authorize() already establishes for a
 * different table. FUNCTIONAL_W_STAR is this codebase's own documented
 * default access-type choice for all four optional-subsystem sections
 * (regmap.h's own file-header note explains why: TC18 gives no
 * table-specific override for any of them, unlike HW_config's own
 * §12.7.6 override). */
static bool optional_subsystem_cfg_write_route(uint16_t addr, uint16_t table_ptr,
                                                rcp_regmap_optional_subsystem_cfg_t *cfg,
                                                rcp_lifecycle_state_t state,
                                                rcp_lifecycle_writer_ctx_t writer,
                                                rcp_regmap_ep0_write_op_t write_op,
                                                const uint8_t *data, size_t data_len,
                                                rcp_wire_error_t *out_error)
{
    uint16_t                                          relative;
    rcp_regmap_optional_subsystem_cfg_reconfig_errc_t rc;
    const uint8_t                                     *write_data = data;
    uint8_t                                            combined[RCP_ACF_ABB_MAX_PAYLOAD];

    if (cfg == NULL) return false;
    if ((size_t)addr < table_ptr || (size_t)addr >= (size_t)table_ptr + cfg->len) return false;

    relative = (uint16_t)((size_t)addr - table_ptr);
    if (!rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer)) {
        *out_error = rcp_lifecycle_field_write_error(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer);
        return true;
    }

    /* REQ-RMAP-068: cfg->data already IS the section's own current wire
     * image (no separate render() needed, unlike every row-typed
     * table above -- see rcp_regmap_optional_subsystem_cfg_apply_
     * reconfig()'s own doc comment), so this combines directly against
     * it. Same out-of-range fallback as every row-typed table's own
     * pattern: apply_reconfig() itself still correctly rejects a
     * too-large write using cfg->len, the real bound. */
    if (write_op != RCP_REGMAP_EP0_WRITE_OP_SET && (size_t)relative + data_len <= cfg->len &&
        data_len <= sizeof(combined)) {
        rcp_regmap_ep0_combine_write_op(write_op, &cfg->data[relative], data, combined, data_len);
        write_data = combined;
    }

    rc = rcp_regmap_optional_subsystem_cfg_apply_reconfig(cfg, relative, write_data, data_len);
    *out_error = (rc == RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                        : RCP_ERROR_INVALID_PARAMETER;
    return true;
}

//cfusa:req REQ-RMAP-068
void rcp_regmap_ep0_combine_write_op(rcp_regmap_ep0_write_op_t op, const uint8_t *current,
                                      const uint8_t *request, uint8_t *out, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        switch (op) {
        case RCP_REGMAP_EP0_WRITE_OP_OR:  out[i] = (uint8_t)(current[i] | request[i]); break;
        case RCP_REGMAP_EP0_WRITE_OP_AND: out[i] = (uint8_t)(current[i] & request[i]); break;
        case RCP_REGMAP_EP0_WRITE_OP_XOR: out[i] = (uint8_t)(current[i] ^ request[i]); break;
        case RCP_REGMAP_EP0_WRITE_OP_SET:
        default:
            out[i] = request[i];
            break;
        }
    }
}

//cfusa:req REQ-RMAP-040
//cfusa:req REQ-RMAP-041
//cfusa:req REQ-RMAP-047
//cfusa:req REQ-RMAP-048
//cfusa:req REQ-RMAP-049
//cfusa:req REQ-RMAP-071
//cfusa:req REQ-RMAP-039
//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-054
//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-068
//cfusa:req REQ-RMAP-072
//cfusa:req REQ-RMAP-073
//cfusa:req REQ-RMAP-074
//cfusa:req REQ-RMAP-075
//cfusa:req REQ-RMAP-076
//cfusa:req REQ-RMAP-077
//cfusa:req REQ-RMAP-078
//cfusa:req REQ-RMAP-079
//cfusa:req REQ-RMAP-080
rcp_regmap_ep0_errc_t
rcp_regmap_ep0_decode_write_request(const uint8_t *b, size_t len,
                                     const rcp_regmap_general_t *map,
                                     rcp_lifecycle_state_t state,
                                     rcp_lifecycle_writer_ctx_t writer,
                                     rcp_regmap_hw_pin_map_entry_t *hw_pin_map,
                                     size_t hw_pin_map_count,
                                     rcp_regmap_ep_id_map_entry_t *ep_id_map,
                                     size_t ep_id_map_count,
                                     rcp_regmap_response_queue_cfg_t *response_queue_cfg,
                                     size_t response_queue_cfg_count,
                                     rcp_regmap_request_stream_cfg_t *request_stream_cfg,
                                     size_t request_stream_cfg_count,
                                     rcp_regmap_ep_generic_cfg_t *ep_generic_cfg,
                                     size_t ep_generic_cfg_count,
                                     uint8_t *sequencer_state,
                                     uint8_t *sequencer_owner,
                                     size_t sequencer_count,
                                     uint8_t requester_stream_index,
                                     rcp_wire_error_t *out_error,
                                     uint8_t *out_transaction_num,
                                     uint32_t watchdog_ms_per_tick,
                                     const rcp_regmap_optional_subsystem_cfg_ptrs_t *optional_cfg,
                                     const uint8_t *ep_id_map_ep_types,
                                     uint8_t fixed_ep_id_target_ep_type,
                                     uint16_t fixed_ep_id_required_ep_id)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    uint16_t                     addr;
    size_t                       data_len;
    size_t                       hw_cfg_len;
    size_t                       ep_id_map_len;
    size_t                       response_queue_cfg_len;
    size_t                       request_stream_cfg_len;
    size_t                       ep_generic_cfg_len;
    size_t                       sequencer_state_len;
    bool                         locked;
    rcp_regmap_ep0_write_op_t    write_op;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_REGMAP_EP0_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_REGMAP_EP0_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != RCP_REGMAP_EP0_INDEX) return RCP_REGMAP_EP0_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_REGMAP_EP0_ERR_WRONG_OP;
    if (payload_len < 2u) return RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD;

    addr     = get_u16(payload);
    data_len = payload_len - 2u;
    *out_transaction_num = hdr.transaction_num;

    /* REQ-RMAP-068: evt[2:0] in {4..7} has no defined meaning for an
     * EP0 register-map write (TC18 13.7.1.2 names exactly 4 operations
     * -- see rcp_regmap_ep0_write_op_t's own doc comment) -- rejected
     * before any address routing, matching Table 33's own "reserved
     * value -> UNSUPPORTED_CMD" precedent for the same evt[2:0]=100b
     * case in a different context. */
    if ((hdr.evt & 0x07u) > (uint8_t)RCP_REGMAP_EP0_WRITE_OP_XOR) {
        *out_error = RCP_ERROR_UNSUPPORTED_CMD;
        return RCP_REGMAP_EP0_OK;
    }
    write_op = (rcp_regmap_ep0_write_op_t)(hdr.evt & 0x07u);

    if ((size_t)addr < RCP_REGMAP_GENERAL_LEN) {
        /* Table 20's own extent -- unconditionally read-only (REQ-RMAP-025),
         * reusing rcp_lifecycle_field_write_error() exactly as
         * rcp_regmap_general_decode_write_request() already does, not a
         * second, separately-maintained copy of that logic. writer's own
         * identity is irrelevant to a READ_ONLY kind's own outcome (see
         * that kind's own doc comment, lifecycle.h) -- passing the real
         * caller-supplied writer here rather than a dummy is simpler and
         * equally correct. */
        *out_error = rcp_lifecycle_field_write_error(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                      RCP_LIFECYCLE_FIELD_READ_ONLY, writer);
        return RCP_REGMAP_EP0_OK;
    }

    hw_cfg_len = hw_pin_map_count * 3u;
    if ((size_t)addr >= map->svr_hw_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_hw_cfg_ptr + hw_cfg_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_hw_cfg_ptr);
        rcp_regmap_hw_pin_map_reconfig_errc_t rc;

        /* HW_config is NOT FUNCTIONAL_W_STAR despite its own "R/W*" column
         * legend (issue #308) -- direct primary-source verification of
         * this table's own surrounding prose (TC18 §12.7.6, immediately
         * before Table 19/21) finds a narrower, table-specific override:
         * "This configuration table can only be changed in the
         * life-cycle state HW_unconfigured. In other states of the life
         * cycle this is read-only" -- no HW_CONFIGURED-with-authorization
         * writable window at all, unlike FUNCTIONAL_W_STAR's own generic
         * rule. This is exactly RCP_LIFECYCLE_FIELD_HW_GENERIC's own
         * shape, and matches this codebase's own already-established
         * REQ-RMAP-040 citation (written independently, before this
         * dispatcher existed) -- see HW_GENERIC's own doc comment
         * (lifecycle.h). request-stream-cfg and response-queue-config's
         * own W-star/W-plus fields have no such table-specific override in their
         * own surrounding prose (confirmed the same way), so they
         * correctly use the generic FUNCTIONAL_W_STAR/W_PLUS rule below. */
        if (!rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_HW_GENERIC, writer)) {
            *out_error = rcp_lifecycle_field_write_error(state, RCP_LIFECYCLE_FIELD_HW_GENERIC,
                                                          writer);
            return RCP_REGMAP_EP0_OK;
        }

        /* REQ-RMAP-068: SET passes request bytes straight through
         * (identical to this dispatcher's own pre-fix behavior);
         * OR/AND/XOR render the table's own current image first and
         * combine against it. Skipped (falls through to the raw
         * passthrough) when the write's own span exceeds this
         * function's own fixed scratch buffer -- apply_reconfig()
         * itself still correctly rejects that write as out-of-range,
         * using the table's own real count, not this buffer's bound. */
        {
            const uint8_t *write_data = &payload[2];
            uint8_t        combined[RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES * 3u];

            if (write_op != RCP_REGMAP_EP0_WRITE_OP_SET &&
                (size_t)relative + data_len <= sizeof(combined)) {
                uint8_t rendered[RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES * 3u];

                rcp_regmap_hw_pin_map_render(hw_pin_map, hw_pin_map_count, rendered);
                rcp_regmap_ep0_combine_write_op(write_op, &rendered[relative], &payload[2],
                                                 combined, data_len);
                write_data = combined;
            }

            rc = rcp_regmap_hw_pin_map_apply_reconfig(hw_pin_map, hw_pin_map_count, relative,
                                                        write_data, data_len);
        }
        *out_error = (rc == RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    ep_id_map_len = ep_id_map_count * 4u;
    if ((size_t)addr >= map->svr_ep_bytebus_id_map_ptr &&
        (size_t)addr < (size_t)map->svr_ep_bytebus_id_map_ptr + ep_id_map_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_ep_bytebus_id_map_ptr);
        rcp_regmap_ep_id_map_reconfig_errc_t rc;

        /* EP_ID_config is entirely TC18 R/W+ (issue #308). locked is
         * REQ-RMAP-029's own svr_configuration_lock -- TC18's own single,
         * global W+ lock ("0x00: write access to R/W+ type parameters
         * allowed; else: rejected"), not a new per-table lock. */
        locked = map->svr_configuration_lock != 0u;
        if (!rcp_lifecycle_field_writable_w_plus(state, writer, locked)) {
            *out_error = rcp_lifecycle_field_write_error_w_plus(state, writer, locked);
            return RCP_REGMAP_EP0_OK;
        }

        /* REQ-RMAP-068: computed once, up front, so both the
         * REQ-WAKEUP-020 fixed-ep_id prediction below and the actual
         * apply_reconfig() see the SAME bytes the write will really
         * produce -- checking the fixed-ep_id invariant against the
         * raw, pre-combine request bytes would be wrong under OR/AND/
         * XOR (the write's real effect depends on the table's own
         * current content too). See hw_pin_map's own identical pattern
         * above for the out-of-range fallback rationale. */
        {
            const uint8_t *write_data = &payload[2];
            uint8_t        combined[RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES * 4u];

            if (write_op != RCP_REGMAP_EP0_WRITE_OP_SET &&
                (size_t)relative + data_len <= sizeof(combined)) {
                uint8_t rendered[RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES * 4u];

                rcp_regmap_ep_id_map_render(ep_id_map, ep_id_map_count, rendered);
                rcp_regmap_ep0_combine_write_op(write_op, &rendered[relative], &payload[2],
                                                 combined, data_len);
                write_data = combined;
            }

            /* REQ-WAKEUP-020: checked after authorization (an
             * unauthorized writer is denied for that reason first,
             * matching every other table's own check ordering),
             * before applying. */
            if (!ep_id_map_write_keeps_fixed_ep_id(ep_id_map, ep_id_map_count, ep_id_map_ep_types,
                                                    fixed_ep_id_target_ep_type,
                                                    fixed_ep_id_required_ep_id, relative,
                                                    write_data, data_len)) {
                *out_error = RCP_ERROR_INVALID_PARAMETER;
                return RCP_REGMAP_EP0_OK;
            }

            rc = rcp_regmap_ep_id_map_apply_reconfig(ep_id_map, ep_id_map_count, relative,
                                                       write_data, data_len);
            *out_error = (rc == RCP_REGMAP_EP_ID_MAP_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                   : RCP_ERROR_INVALID_PARAMETER;
        }
        return RCP_REGMAP_EP0_OK;
    }

    response_queue_cfg_len = response_queue_cfg_count * 10u;
    if ((size_t)addr >= map->svr_response_stream_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_response_stream_cfg_ptr + response_queue_cfg_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_response_stream_cfg_ptr);
        rcp_regmap_response_queue_cfg_reconfig_errc_t rc;
        rcp_wire_error_t auth_err;

        /* response-queue-config is MIXED per-field within its own
         * 10-octet-per-row stride (issue #308): STREAM_UID [0,2) and
         * flush_on_count/Flush_time [6,10) are R/W+; Max_AVTPDUsize/
         * queue_size [2,6) are R/W*. Every row-relative sub-range the
         * write's own [row_relative, row_relative+data_len) touches must
         * pass its own corresponding check. */
        locked = map->svr_configuration_lock != 0u;
        auth_err = respqueue_cfg_row_write_authorize(state, writer, locked, relative, data_len);
        if (auth_err != RCP_ERROR_NONE) {
            *out_error = auth_err;
            return RCP_REGMAP_EP0_OK;
        }

        /* REQ-RMAP-068: see hw_pin_map's own identical pattern above. */
        {
            const uint8_t *write_data = &payload[2];
            uint8_t        combined[RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES * 10u];

            if (write_op != RCP_REGMAP_EP0_WRITE_OP_SET &&
                (size_t)relative + data_len <= sizeof(combined)) {
                uint8_t rendered[RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES * 10u];

                rcp_regmap_response_queue_cfg_render(response_queue_cfg, response_queue_cfg_count,
                                                      rendered);
                rcp_regmap_ep0_combine_write_op(write_op, &rendered[relative], &payload[2],
                                                 combined, data_len);
                write_data = combined;
            }

            rc = rcp_regmap_response_queue_cfg_apply_reconfig(response_queue_cfg,
                                                                response_queue_cfg_count, relative,
                                                                write_data, data_len);
        }
        *out_error = (rc == RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                        : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    request_stream_cfg_len = request_stream_cfg_count * 24u;
    if ((size_t)addr >= map->svr_request_stream_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_request_stream_cfg_ptr + request_stream_cfg_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_request_stream_cfg_ptr);
        rcp_regmap_request_stream_cfg_reconfig_errc_t rc;
        const uint8_t *write_data     = &payload[2];
        uint8_t        combined[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES * 24u];
        uint8_t        rendered[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES * 24u];
        bool           have_rendered = false;

        /* Rendered unconditionally here (REQ-RMAP-068's own hw_pin_map
         * pattern above only renders for OR/AND/XOR, since it only ever
         * needs `rendered` to combine against) because
         * request_stream_cfg_row_write_authorize() below also needs a
         * current-image snapshot to resolve rx_stream_status's own bit-7
         * carve-out (issue #424) regardless of write_op -- see that
         * function's own doc comment. request_stream_status_blocked is
         * NULL here (not threaded through this whole write dispatcher):
         * safe and correct, since the carve-out check below only ever
         * inspects bits [6:0] of this rendered image, masking bit 7 out
         * entirely (see that function's own doc comment). */
        if ((size_t)relative + data_len <= sizeof(rendered)) {
            rcp_regmap_request_stream_cfg_render(request_stream_cfg, request_stream_cfg_count,
                                                  rendered, watchdog_ms_per_tick, NULL);
            have_rendered = true;
            if (write_op != RCP_REGMAP_EP0_WRITE_OP_SET) {
                rcp_regmap_ep0_combine_write_op(write_op, &rendered[relative], &payload[2],
                                                 combined, data_len);
                write_data = combined;
            }
        }

        /* request-stream-cfg is TC18 R/W* for every field EXCEPT the
         * plain R/W rx_stream_status bit at row-relative 0x000D bit 7
         * (issue #424, REQ-E2E-046/REQ-RMAP-051) -- see this helper's
         * own doc comment for the narrow, safe carve-out. */
        if (!request_stream_cfg_row_write_authorize(state, writer, relative, write_data, data_len,
                                                      have_rendered ? rendered : NULL)) {
            *out_error = rcp_lifecycle_field_write_error(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                          writer);
            return RCP_REGMAP_EP0_OK;
        }

        rc = rcp_regmap_request_stream_cfg_apply_reconfig(request_stream_cfg,
                                                            request_stream_cfg_count, relative,
                                                            write_data, data_len,
                                                            watchdog_ms_per_tick);
        *out_error = (rc == RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                        : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    ep_generic_cfg_len = ep_generic_cfg_count * 12u;
    if ((size_t)addr >= map->svr_ep_generic_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_ep_generic_cfg_ptr + ep_generic_cfg_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_ep_generic_cfg_ptr);
        rcp_regmap_ep_generic_cfg_reconfig_errc_t rc;

        /* CORRECTED 2026-08-13 (issue #338, REQ-LIFECYCLE-023): the
         * issue #311 batch 5 claim above -- "no table-specific
         * lifecycle-state override the way §12.7.6 does for HW_config,
         * so the generic FUNCTIONAL_W_STAR rule applies" -- checked only
         * §13.2's own prose next to Table 31 itself, not Figure 17's own
         * diagram (§12.3), which DOES give EP_GEN_CFG a table-specific
         * override: "Request on discovery stream or known stream/bb_id
         * for configuration to HW_CONFIG or QUEUE_CFG or EP_GEN_CFG ->
         * send error response LOCKED_CONFIG_ACCESS" (TC18.txt
         * L2485-2488, directly confirmed against the rendered PDF page
         * image). EP_GEN_CFG is one of the three tables Figure 17 locks
         * from HW_CONFIGURED onward -- the SAME RCP_LIFECYCLE_FIELD_
         * HW_GENERIC rule HW_config itself already uses, matching
         * rcp_lifecycle_field_write_error()'s own doc comment
         * (lifecycle.h), which already named this exact block as
         * belonging to HW_GENERIC even before this fix updated the
         * dispatch code itself to match. ep_type's own read-only status
         * (relative 0x0000 within each row) remains a SEPARATE,
         * per-field concern enforced inside
         * rcp_regmap_ep_generic_cfg_apply_reconfig() itself (issue #311
         * batch 4), independent of this row-level authorization. */
        if (!rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_HW_GENERIC, writer)) {
            *out_error = rcp_lifecycle_field_write_error(state, RCP_LIFECYCLE_FIELD_HW_GENERIC,
                                                          writer);
            return RCP_REGMAP_EP0_OK;
        }

        /* REQ-RMAP-068: see hw_pin_map's own identical pattern above.
         * ep_type's own read-only status (noted above) is unaffected --
         * apply_reconfig() ignores that octet's write value regardless
         * of whether it came from a raw request or a combined one. */
        {
            const uint8_t *write_data = &payload[2];
            uint8_t        combined[RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES * 12u];

            if (write_op != RCP_REGMAP_EP0_WRITE_OP_SET &&
                (size_t)relative + data_len <= sizeof(combined)) {
                uint8_t rendered[RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES * 12u];

                rcp_regmap_ep_generic_cfg_render(ep_generic_cfg, ep_generic_cfg_count, rendered);
                rcp_regmap_ep0_combine_write_op(write_op, &rendered[relative], &payload[2],
                                                 combined, data_len);
                write_data = combined;
            }

            rc = rcp_regmap_ep_generic_cfg_apply_reconfig(ep_generic_cfg, ep_generic_cfg_count,
                                                            relative, write_data, data_len);
        }
        *out_error = (rc == RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                     : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    sequencer_state_len = sequencer_count * 2u;
    if (sequencer_state != NULL && sequencer_owner != NULL &&
        (size_t)addr >= map->svr_sequencer_state_ptr &&
        (size_t)addr < (size_t)map->svr_sequencer_state_ptr + sequencer_state_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_sequencer_state_ptr);
        rcp_regmap_sequencer_table_reconfig_errc_t rc;

        /* REQ-SEQ-013: ownership-aware authorization, on top of the
         * ordinary FUNCTIONAL_W_STAR gate every other table's own
         * writes already need -- see sequencer_row_write_authorize()'s
         * own doc comment for the full per-octet rule. */
        *out_error = sequencer_row_write_authorize(state, writer, sequencer_owner, sequencer_count,
                                                     requester_stream_index, relative, data_len);
        if (*out_error != RCP_ERROR_NONE) return RCP_REGMAP_EP0_OK;

        /* REQ-RMAP-068: see hw_pin_map's own identical pattern above.
         * The ownership authorization just above depends only on WHICH
         * octets the write's own span touches (relative/data_len), not
         * their value, so it is unaffected by whether write_data ends
         * up being the raw request or a combined one. */
        {
            const uint8_t *write_data = &payload[2];
            uint8_t        combined[RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES * 2u];

            if (write_op != RCP_REGMAP_EP0_WRITE_OP_SET &&
                (size_t)relative + data_len <= sizeof(combined)) {
                uint8_t rendered[RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES * 2u];

                rcp_regmap_sequencer_table_render(sequencer_state, sequencer_owner, sequencer_count,
                                                   rendered);
                rcp_regmap_ep0_combine_write_op(write_op, &rendered[relative], &payload[2],
                                                 combined, data_len);
                write_data = combined;
            }

            rc = rcp_regmap_sequencer_table_apply_reconfig(sequencer_state, sequencer_owner,
                                                             sequencer_count, relative, write_data,
                                                             data_len);
        }
        *out_error = (rc == RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                      : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    /* REQ-RMAP-039: the four optional-subsystem sections -- see
     * optional_subsystem_cfg_write_route()'s own doc comment above for
     * what "handled" means here (a NULL optional_cfg, or a NULL
     * individual field within it, simply falls through to every other
     * check exactly like NULL sequencer_state/sequencer_owner already
     * does). */
    if (optional_cfg != NULL) {
        if (optional_subsystem_cfg_write_route(addr, map->svr_network_interface_cfg_ptr,
                                                optional_cfg->network_interface_cfg, state, writer,
                                                write_op, &payload[2], data_len, out_error)) {
            return RCP_REGMAP_EP0_OK;
        }
        if (optional_subsystem_cfg_write_route(addr, map->svr_physical_layer_cfg_ptr,
                                                optional_cfg->physical_layer_cfg, state, writer,
                                                write_op, &payload[2], data_len, out_error)) {
            return RCP_REGMAP_EP0_OK;
        }
        if (optional_subsystem_cfg_write_route(addr, map->svr_time_synch_cfg_ptr,
                                                optional_cfg->time_synch_cfg, state, writer,
                                                write_op, &payload[2], data_len, out_error)) {
            return RCP_REGMAP_EP0_OK;
        }
        if (optional_subsystem_cfg_write_route(addr, map->svr_security_cfg_ptr,
                                                optional_cfg->security_cfg, state, writer,
                                                write_op, &payload[2], data_len, out_error)) {
            return RCP_REGMAP_EP0_OK;
        }
    }

    /* Neither Table 20's own extent nor any known pointed-to table --
     * see this function's own header doc comment for which tables this
     * milestone routes (issue #301, issue #306, issue #311, issue #335,
     * issue #336). */
    *out_error = RCP_ERROR_EP_NOT_FOUND;
    return RCP_REGMAP_EP0_OK;
}

//cfusa:req REQ-RMAP-040
//cfusa:req REQ-RMAP-041
//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-054
//cfusa:req REQ-RMAP-061
rcp_regmap_ep0_errc_t
rcp_regmap_ep0_decode_read_request(const uint8_t *b, size_t len,
                                    uint16_t *out_addr, uint8_t *out_read_size,
                                    uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_REGMAP_EP0_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_REGMAP_EP0_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != RCP_REGMAP_EP0_INDEX) return RCP_REGMAP_EP0_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_REGMAP_EP0_ERR_WRONG_OP;
    if (payload_len < 2u) return RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD;

    *out_addr             = get_u16(payload);
    *out_read_size        = (uint8_t)hdr.read_size_or_segment_num; /* truncated -- see this
                                                                        function's own doc
                                                                        comment (regmap.h) */
    *out_transaction_num  = hdr.transaction_num;
    return RCP_REGMAP_EP0_OK;
}

/* Shared body for rcp_regmap_ep0_encode_read_response()'s own four
 * known-extent cases: copies min(read_size, table_len - offset) real
 * octets from table_image starting at offset, zero-fills the rest up to
 * read_size, and encodes the ACF_ABB READ response -- the identical
 * body rcp_regmap_general_encode_read_response() already establishes
 * for Table 20 alone, generalized to an arbitrary source image/offset. */
static rcp_bytes_t ep0_read_response_from_slice(const uint8_t *table_image, size_t table_len,
                                                 size_t offset, uint8_t read_size,
                                                 uint8_t transaction_num)
{
    uint8_t                     payload[256]; /* read_size is uint8_t --
                                                   always <= 255, see this
                                                   dispatcher's own
                                                   read-side file-header
                                                   note (regmap.h) */
    size_t                      avail    = (offset < table_len) ? (table_len - offset) : 0u;
    size_t                      copy_len = (avail < (size_t)read_size) ? avail : (size_t)read_size;
    rcp_acf_byte_message_info_t hdr      = {0};

    memset(payload, 0, sizeof(payload));
    if (copy_len > 0u) memcpy(payload, &table_image[offset], copy_len);

    hdr.byte_bus_id              = RCP_REGMAP_EP0_INDEX;
    hdr.op                       = RCP_ACF_OP_READ;
    hdr.rsp                      = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
    hdr.read_size_or_segment_num = read_size;
    hdr.transaction_num          = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, read_size);
}

/* Shared per-section read-routing check for the read dispatcher below --
 * the read-side counterpart to optional_subsystem_cfg_write_route()
 * above. *out_handled is set true iff addr falls within [table_ptr,
 * table_ptr + cfg->len) and cfg is non-NULL; the caller returns the
 * result immediately whenever *out_handled is true, same shape every
 * other extent's own inline read-side block already uses. Reads
 * directly from cfg->data via ep0_read_response_from_slice() -- no
 * render() step, matching this section's own file-header note (no
 * row-typed table to render). */
static rcp_bytes_t optional_subsystem_cfg_read_route(uint16_t addr, uint16_t table_ptr,
                                                      const rcp_regmap_optional_subsystem_cfg_t *cfg,
                                                      uint8_t read_size, uint8_t transaction_num,
                                                      bool *out_handled)
{
    if (cfg == NULL || (size_t)addr < table_ptr || (size_t)addr >= (size_t)table_ptr + cfg->len) {
        rcp_bytes_t zero = {0};
        *out_handled = false;
        return zero;
    }

    *out_handled = true;
    return ep0_read_response_from_slice(cfg->data, cfg->len, (size_t)addr - table_ptr, read_size,
                                         transaction_num);
}

//cfusa:req REQ-RMAP-040
//cfusa:req REQ-RMAP-041
//cfusa:req REQ-RMAP-047
//cfusa:req REQ-RMAP-048
//cfusa:req REQ-RMAP-049
//cfusa:req REQ-RMAP-071
//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-054
//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-073
//cfusa:req REQ-RMAP-074
//cfusa:req REQ-RMAP-075
//cfusa:req REQ-RMAP-076
//cfusa:req REQ-RMAP-077
//cfusa:req REQ-RMAP-078
//cfusa:req REQ-RMAP-080
//cfusa:req REQ-SEQ-014
//cfusa:req REQ-E2E-046
//cfusa:req REQ-RMAP-051
rcp_bytes_t
rcp_regmap_ep0_encode_read_response(uint16_t addr, uint8_t read_size,
                                     uint8_t transaction_num,
                                     const rcp_regmap_general_t *map,
                                     const rcp_regmap_hw_pin_map_entry_t *hw_pin_map,
                                     size_t hw_pin_map_count,
                                     const rcp_regmap_ep_id_map_entry_t *ep_id_map,
                                     size_t ep_id_map_count,
                                     const rcp_regmap_response_queue_cfg_t *response_queue_cfg,
                                     size_t response_queue_cfg_count,
                                     const rcp_regmap_request_stream_cfg_t *request_stream_cfg,
                                     size_t request_stream_cfg_count,
                                     const rcp_regmap_ep_generic_cfg_t *ep_generic_cfg,
                                     size_t ep_generic_cfg_count,
                                     const uint8_t *sequencer_state,
                                     const uint8_t *sequencer_owner,
                                     size_t sequencer_count,
                                     rcp_wire_error_t *out_error,
                                     uint32_t watchdog_ms_per_tick,
                                     const rcp_regmap_optional_subsystem_cfg_ptrs_t *optional_cfg,
                                     const bool *request_stream_status_blocked)
{
    size_t hw_cfg_len;
    size_t ep_id_map_len;
    size_t response_queue_cfg_len;
    size_t request_stream_cfg_len;
    size_t ep_generic_cfg_len;

    if ((size_t)addr < RCP_REGMAP_GENERAL_LEN) {
        uint8_t image[RCP_REGMAP_GENERAL_LEN];

        rcp_regmap_general_render(map, image);
        *out_error = RCP_ERROR_NONE;
        return ep0_read_response_from_slice(image, RCP_REGMAP_GENERAL_LEN, (size_t)addr,
                                             read_size, transaction_num);
    }

    hw_cfg_len = hw_pin_map_count * 3u;
    if ((size_t)addr >= map->svr_hw_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_hw_cfg_ptr + hw_cfg_len) {
        uint8_t image[RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES * 3u];

        rcp_regmap_hw_pin_map_render(hw_pin_map, hw_pin_map_count, image);
        *out_error = RCP_ERROR_NONE;
        return ep0_read_response_from_slice(image, hw_cfg_len,
                                             (size_t)addr - map->svr_hw_cfg_ptr,
                                             read_size, transaction_num);
    }

    ep_id_map_len = ep_id_map_count * 4u;
    if ((size_t)addr >= map->svr_ep_bytebus_id_map_ptr &&
        (size_t)addr < (size_t)map->svr_ep_bytebus_id_map_ptr + ep_id_map_len) {
        uint8_t image[RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES * 4u];

        rcp_regmap_ep_id_map_render(ep_id_map, ep_id_map_count, image);
        *out_error = RCP_ERROR_NONE;
        return ep0_read_response_from_slice(image, ep_id_map_len,
                                             (size_t)addr - map->svr_ep_bytebus_id_map_ptr,
                                             read_size, transaction_num);
    }

    response_queue_cfg_len = response_queue_cfg_count * 10u;
    if ((size_t)addr >= map->svr_response_stream_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_response_stream_cfg_ptr + response_queue_cfg_len) {
        uint8_t image[RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES * 10u];

        rcp_regmap_response_queue_cfg_render(response_queue_cfg, response_queue_cfg_count, image);
        *out_error = RCP_ERROR_NONE;
        return ep0_read_response_from_slice(image, response_queue_cfg_len,
                                             (size_t)addr - map->svr_response_stream_cfg_ptr,
                                             read_size, transaction_num);
    }

    request_stream_cfg_len = request_stream_cfg_count * 24u;
    if ((size_t)addr >= map->svr_request_stream_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_request_stream_cfg_ptr + request_stream_cfg_len) {
        uint8_t image[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES * 24u];

        rcp_regmap_request_stream_cfg_render(request_stream_cfg, request_stream_cfg_count, image,
                                              watchdog_ms_per_tick, request_stream_status_blocked);
        *out_error = RCP_ERROR_NONE;
        return ep0_read_response_from_slice(image, request_stream_cfg_len,
                                             (size_t)addr - map->svr_request_stream_cfg_ptr,
                                             read_size, transaction_num);
    }

    ep_generic_cfg_len = ep_generic_cfg_count * 12u;
    if ((size_t)addr >= map->svr_ep_generic_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_ep_generic_cfg_ptr + ep_generic_cfg_len) {
        uint8_t image[RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES * 12u];

        rcp_regmap_ep_generic_cfg_render(ep_generic_cfg, ep_generic_cfg_count, image);
        *out_error = RCP_ERROR_NONE;
        return ep0_read_response_from_slice(image, ep_generic_cfg_len,
                                             (size_t)addr - map->svr_ep_generic_cfg_ptr,
                                             read_size, transaction_num);
    }

    /* REQ-SEQ-013/REQ-SEQ-014: sequencer_state/sequencer_owner
     * (svr_sequencer_state_ptr, TC18 §12.7.10 Table 25/28's own real
     * 2-octet-per-sequencer layout) now go through
     * rcp_regmap_sequencer_table_render() -- see that function's own doc
     * comment (regmap.h) for the conformance defect this corrects.
     * clamped_count (a sequencer COUNT, not an octet count) is clamped to
     * this dispatcher's own stack bound the same way every other
     * extent's fixed-array image above is; a caller-supplied count
     * beyond that bound is truncated to it rather than overflowing the
     * stack buffer (matching every sibling extent's own MAX_ENTRIES
     * clamp, none of which validate their own count parameter either --
     * that validation is this dispatcher's caller's own responsibility,
     * the same "caller keeps count in sync with real storage" contract
     * svr_sequencers_max's own field comment (regmap.h) already states). */
    if (sequencer_state != NULL && sequencer_owner != NULL &&
        (size_t)addr >= map->svr_sequencer_state_ptr &&
        (size_t)addr < (size_t)map->svr_sequencer_state_ptr + sequencer_count * 2u) {
        size_t  clamped_count = (sequencer_count > RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES)
                                     ? RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES
                                     : sequencer_count;
        uint8_t image[RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES * 2u];

        rcp_regmap_sequencer_table_render(sequencer_state, sequencer_owner, clamped_count, image);
        *out_error = RCP_ERROR_NONE;
        return ep0_read_response_from_slice(image, clamped_count * 2u,
                                             (size_t)addr - map->svr_sequencer_state_ptr,
                                             read_size, transaction_num);
    }

    /* REQ-RMAP-039: the four optional-subsystem sections -- see
     * optional_subsystem_cfg_read_route()'s own doc comment above. */
    if (optional_cfg != NULL) {
        bool        handled;
        rcp_bytes_t r;

        r = optional_subsystem_cfg_read_route(addr, map->svr_network_interface_cfg_ptr,
                                               optional_cfg->network_interface_cfg, read_size,
                                               transaction_num, &handled);
        if (handled) { *out_error = RCP_ERROR_NONE; return r; }

        r = optional_subsystem_cfg_read_route(addr, map->svr_physical_layer_cfg_ptr,
                                               optional_cfg->physical_layer_cfg, read_size,
                                               transaction_num, &handled);
        if (handled) { *out_error = RCP_ERROR_NONE; return r; }

        r = optional_subsystem_cfg_read_route(addr, map->svr_time_synch_cfg_ptr,
                                               optional_cfg->time_synch_cfg, read_size,
                                               transaction_num, &handled);
        if (handled) { *out_error = RCP_ERROR_NONE; return r; }

        r = optional_subsystem_cfg_read_route(addr, map->svr_security_cfg_ptr,
                                               optional_cfg->security_cfg, read_size,
                                               transaction_num, &handled);
        if (handled) { *out_error = RCP_ERROR_NONE; return r; }
    }

    /* Neither Table 20's own extent nor any known pointed-to table --
     * same fallback the write dispatcher already uses for the identical
     * condition. */
    *out_error = RCP_ERROR_EP_NOT_FOUND;
    {
        rcp_bytes_t zero = {0};
        return zero;
    }
}

/* ── EP_ID_config wire stride (REQ-RMAP-052/054) ───────────────────────────── */

//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-053
void rcp_regmap_ep_id_map_render(const rcp_regmap_ep_id_map_entry_t *entries, size_t count,
                                  uint8_t *out)
{
    size_t i;

    for (i = 0; i < count; i++) {
        uint16_t bbid_ctrl;

        out[4u * i + 0u] = entries[i].request_stream_index;
        out[4u * i + 1u] = (uint8_t)entries[i].ep_id; /* truncated -- see this
                                                          function's own doc
                                                          comment (regmap.h) */

        /* REQ-RMAP-053 (issue #421, TC18 Table 25/26): BBID in
         * bits[15:5] (masked to its own real 11-bit width, same
         * masking convention rcp_acf_pack_header() already uses,
         * acf.c), CRC_required in bit 4, Channel_selection
         * (bits[3:0]) deliberately always 0 -- see this table's own
         * file-header note above and
         * rcp_regmap_ep_id_map_entry_t.crc_required's own field
         * comment (regmap.h). */
        bbid_ctrl = (uint16_t)(((uint16_t)entries[i].byte_bus_id & 0x07FFu) << 5);
        bbid_ctrl = (uint16_t)(bbid_ctrl | (entries[i].crc_required ? 0x10u : 0x00u));
        put_u16(&out[4u * i + 2u], bbid_ctrl);
    }
}

//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-054
const char *rcp_regmap_ep_id_map_reconfig_strerror(rcp_regmap_ep_id_map_reconfig_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_EP_ID_MAP_RECONFIG_OK:
        return "rcp/regmap: EP_ID_config write applied";
    case RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_SHORT:
        return "rcp/regmap: EP_ID_config write has no data";
    case RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/regmap: EP_ID_config write extends past the table's own current extent";
    default:
        return "rcp/regmap: EP_ID_config unknown configuration-write error";
    }
}

//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-053
//cfusa:req REQ-RMAP-054
rcp_regmap_ep_id_map_reconfig_errc_t
rcp_regmap_ep_id_map_apply_reconfig(rcp_regmap_ep_id_map_entry_t *entries, size_t count,
                                     uint16_t relative_start_address,
                                     const uint8_t *data, size_t data_len)
{
    uint8_t block[RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES * 4u];
    size_t  block_len = count * 4u;
    size_t  i;

    if (data_len == 0u) return RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_SHORT;

    if ((size_t)relative_start_address + data_len > block_len) {
        return RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Same "render current image, patch the addressed octets, re-parse
     * the whole image back" idiom rcp_regmap_hw_pin_map_apply_reconfig()
     * and every endpoint type's own apply_reconfig() already use. */
    rcp_regmap_ep_id_map_render(entries, count, block);
    for (i = 0; i < data_len; i++) {
        block[relative_start_address + i] = data[i]; /* every octet R/W+, none read-only */
    }
    for (i = 0; i < count; i++) {
        uint16_t bbid_ctrl = get_u16(&block[4u * i + 2u]);

        entries[i].request_stream_index = block[4u * i + 0u];
        entries[i].ep_id                = block[4u * i + 1u]; /* zero-extends -- see this
                                                                   struct's own ep_id field
                                                                   comment (regmap.h) */
        /* REQ-RMAP-053 (issue #421, TC18 Table 25/26): inverse of
         * render()'s own packing above -- BBID from bits[15:5],
         * crc_required from bit 4; bits[3:0] (Channel_selection) are
         * intentionally discarded, not stored to any field (see this
         * function's own doc comment, regmap.h). */
        entries[i].byte_bus_id  = (rcp_byte_bus_id_t)((bbid_ctrl >> 5) & 0x07FFu);
        entries[i].crc_required = (bbid_ctrl & 0x10u) != 0u;
    }

    return RCP_REGMAP_EP_ID_MAP_RECONFIG_OK;
}

/* ── response-queue-config wire stride (REQ-RMAP-061/065) ──────────────────── */

//cfusa:req REQ-RMAP-061
void rcp_regmap_response_queue_cfg_render(const rcp_regmap_response_queue_cfg_t *entries,
                                           size_t count, uint8_t *out)
{
    size_t i;

    for (i = 0; i < count; i++) {
        uint16_t flush_time_wire = (entries[i].flush_time_us > 0xFFFFu)
                                        ? (uint16_t)0xFFFFu
                                        : (uint16_t)entries[i].flush_time_us; /* saturate, never
                                                                                  wrap -- see this
                                                                                  field's own doc
                                                                                  comment (regmap.h) */

        put_u16(&out[10u * i + 0u], entries[i].stream_uid);
        put_u16(&out[10u * i + 2u], entries[i].max_avtpdu_size);
        put_u16(&out[10u * i + 4u], entries[i].queue_size);
        put_u16(&out[10u * i + 6u], entries[i].flush_on_count);
        put_u16(&out[10u * i + 8u], flush_time_wire);
    }
}

//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-065
const char *
rcp_regmap_response_queue_cfg_reconfig_strerror(rcp_regmap_response_queue_cfg_reconfig_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_OK:
        return "rcp/regmap: response-queue-config write applied";
    case RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_SHORT:
        return "rcp/regmap: response-queue-config write has no data";
    case RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/regmap: response-queue-config write extends past the table's own current extent";
    default:
        return "rcp/regmap: response-queue-config unknown configuration-write error";
    }
}

//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-065
rcp_regmap_response_queue_cfg_reconfig_errc_t
rcp_regmap_response_queue_cfg_apply_reconfig(rcp_regmap_response_queue_cfg_t *entries,
                                              size_t count,
                                              uint16_t relative_start_address,
                                              const uint8_t *data, size_t data_len)
{
    uint8_t block[RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES * 10u];
    size_t  block_len = count * 10u;
    size_t  i;

    if (data_len == 0u) return RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_SHORT;

    if ((size_t)relative_start_address + data_len > block_len) {
        return RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Same "render current image, patch the addressed octets, re-parse
     * the whole image back" idiom rcp_regmap_hw_pin_map_apply_reconfig()/
     * rcp_regmap_ep_id_map_apply_reconfig() and every endpoint type's own
     * apply_reconfig() already use. */
    rcp_regmap_response_queue_cfg_render(entries, count, block);
    for (i = 0; i < data_len; i++) {
        block[relative_start_address + i] = data[i]; /* STREAM_UID/flush_on_count/Flush_time are
                                                          R/W+ (lockable, not yet enforced here --
                                                          same deferral as every other write in
                                                          this dispatcher); Max_AVTPDUsize/
                                                          queue_size are R/W* -- neither access
                                                          type is bit-level read-only, so every
                                                          octet is currently patchable */
    }
    for (i = 0; i < count; i++) {
        entries[i].stream_uid      = get_u16(&block[10u * i + 0u]);
        entries[i].max_avtpdu_size = get_u16(&block[10u * i + 2u]);
        entries[i].queue_size      = get_u16(&block[10u * i + 4u]);
        entries[i].flush_on_count  = get_u16(&block[10u * i + 6u]);
        entries[i].flush_time_us   = (uint32_t)get_u16(&block[10u * i + 8u]); /* widens, never
                                                                                   needs the
                                                                                   render-side
                                                                                   saturation */
    }

    return RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_OK;
}

/* ── Root-client / per-EP-restricted-client model ──────────────────────────── */

//cfusa:req REQ-RMAP-009
//cfusa:req REQ-RMAP-010
//cfusa:req REQ-RMAP-011
//cfusa:req REQ-RMAP-012
//cfusa:req REQ-RMAP-070
//cfusa:req REQ-LIFECYCLE-031
rcp_lifecycle_writer_ctx_t rcp_regmap_writer_ctx(const rcp_regmap_general_t *map,
                                               const rcp_regmap_ep_client_t *ep_client,
                                               uint16_t requesting_stream_index,
                                               bool via_ep0,
                                               bool via_unicast,
                                               bool via_discovery_stream,
                                               rcp_byte_bus_id_t requesting_byte_bus_id,
                                               const rcp_regmap_ep_id_map_entry_t *ep_id_map,
                                               size_t ep_id_map_count)
{
    rcp_lifecycle_writer_ctx_t ctx;

    ctx.via_root_client_ep0 = via_ep0 &&
                              map->svr_root_client_index != RCP_REGMAP_NO_ROOT_CLIENT &&
                              requesting_stream_index == map->svr_root_client_index;

    ctx.via_owning_stream = ep_client != NULL &&
                            ep_client->has_owning_stream &&
                            requesting_stream_index == ep_client->owning_stream_index;

    ctx.via_non_unicast_frame = !via_unicast;

    /* REQ-RMAP-070: pass through, matching via_ep0/via_unicast's own
     * already-classified-input convention -- this function has no wire
     * data of its own from which "arrived via the discovery stream"
     * could be re-derived. Explicitly assigned so every member of ctx
     * is set (REQ-RMAP-009's own fix: previously left uninitialized). */
    ctx.via_discovery_stream = via_discovery_stream;

    /* REQ-LIFECYCLE-025/031 (issue #341 lineage): TC18 §12.3.1.2's own
     * "any valid stream_id/byte_bus_id combination" case applies ONLY
     * when no root client is configured at all -- baked in here (not
     * left to rcp_lifecycle_transition() to re-check) so this member
     * can never wrongly widen access when a root client IS configured,
     * the same "condition baked into the derived member itself" pattern
     * via_root_client_ep0 above already establishes for its own
     * svr_root_client_index check. requesting_stream_index is narrowed
     * to uint8_t here to match rcp_regmap_ep_id_map_entry_t's own
     * request_stream_index field width (TC18 §12.7.8 Table 23: an 8-bit
     * register) -- the same narrowing rcp_regmap_ep_id_map_byte_bus_ids_
     * for_stream()'s own callers already perform at this boundary. */
    ctx.via_valid_stream_association =
        map->svr_root_client_index == RCP_REGMAP_NO_ROOT_CLIENT &&
        rcp_regmap_ep_id_map_is_valid_association(ep_id_map, ep_id_map_count,
                                                    (uint8_t)requesting_stream_index,
                                                    requesting_byte_bus_id);

    return ctx;
}

/* ── The generic-vs-functional per-endpoint config split ───────────────────── */

//cfusa:req REQ-RMAP-016
//cfusa:req REQ-RMAP-073
//cfusa:req REQ-RMAP-074
//cfusa:req REQ-RMAP-075
void rcp_regmap_ep_generic_cfg_init(rcp_regmap_ep_generic_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

//cfusa:req REQ-RMAP-076
bool rcp_regmap_ep_delay_time_us_to_reg(uint32_t delay_us, uint8_t *out_reg)
{
    switch (delay_us) {
    case 1u:  *out_reg = 0u; return true;
    case 10u: *out_reg = 1u; return true;
    case 20u: *out_reg = 2u; return true;
    case 50u: *out_reg = 3u; return true;
    default:  return false; /* not one of TC18's 4 allowed values -- reject, don't round */
    }
}

//cfusa:req REQ-RMAP-076
uint32_t rcp_regmap_ep_delay_time_reg_to_us(uint8_t reg)
{
    static const uint32_t us_by_reg[4] = {1u, 10u, 20u, 50u};

    return us_by_reg[reg & 0x3u]; /* masked: all 4 possible 2-bit values are valid */
}

//cfusa:req REQ-RMAP-077
uint32_t rcp_regmap_ep_req_storage_size_words_to_octets(uint16_t words)
{
    return (uint32_t)words * 4u; /* always exact, always fits uint32_t */
}

//cfusa:req REQ-RMAP-077
bool rcp_regmap_ep_req_storage_size_octets_to_words(uint32_t octets,
                                                      uint16_t *out_words)
{
    uint32_t words;

    if ((octets % 4u) != 0u) return false; /* not a whole number of 32-bit words */

    words = octets / 4u;
    if (words > (uint32_t)UINT16_MAX) return false; /* REQ-RMAP-077: 16-bit register width */

    *out_words = (uint16_t)words;
    return true;
}

//cfusa:req REQ-RMAP-073
//cfusa:req REQ-RMAP-074
//cfusa:req REQ-RMAP-075
//cfusa:req REQ-RMAP-076
//cfusa:req REQ-RMAP-077
//cfusa:req REQ-RMAP-078
void rcp_regmap_ep_generic_cfg_render(const rcp_regmap_ep_generic_cfg_t *entries,
                                       size_t count, uint8_t *out)
{
    size_t i;

    for (i = 0; i < count; i++) {
        uint8_t  delay_reg;
        uint16_t req_storage_words;
        uint8_t  octet1;

        /* ep_delay_time -> reg: fall back to 0 (1us, the shortest valid
         * delay) if the internal value is not exactly one of the 4
         * allowed ones -- expected for any not-yet-configured endpoint
         * (ep_delay_time's own zero-init default is not itself a valid
         * register value). See this function's own doc comment
         * (regmap.h) for the full "never grant more delay than
         * configured" rationale. */
        if (!rcp_regmap_ep_delay_time_us_to_reg(entries[i].ep_delay_time, &delay_reg)) {
            delay_reg = 0u;
        }

        /* ep_req_storage_size -> words: clamp down to the nearest
         * representable word count if the internal octet value is not
         * an exact multiple of 4 or exceeds the register's own 16-bit
         * width -- same "never grant more than configured" bias,
         * saturating rather than wrapping. */
        if (!rcp_regmap_ep_req_storage_size_octets_to_words(entries[i].ep_req_storage_size,
                                                              &req_storage_words)) {
            uint32_t clamped = entries[i].ep_req_storage_size;

            if (clamped > 0xFFFFu * 4u) clamped = 0xFFFFu * 4u; /* max representable octets */
            req_storage_words = (uint16_t)(clamped / 4u);       /* floor: rounds down, never up */
        }

        octet1 = (uint8_t)((entries[i].ep_used ? 0x01u : 0x00u) | ((delay_reg & 0x3u) << 4));

        out[12u * i + 0u] = entries[i].ep_type;
        out[12u * i + 1u] = octet1;
        put_u16(&out[12u * i + 2u], req_storage_words);
        put_u32(&out[12u * i + 4u], entries[i].ep_description);
        put_u16(&out[12u * i + 8u], entries[i].ep_tx_buffer_size);
        put_u16(&out[12u * i + 10u], entries[i].ep_rx_buffer_size);
    }
}

//cfusa:req REQ-RMAP-079
const char *
rcp_regmap_ep_generic_cfg_reconfig_strerror(rcp_regmap_ep_generic_cfg_reconfig_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK:
        return "rcp/regmap: ep_generic_cfg write applied";
    case RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_SHORT:
        return "rcp/regmap: ep_generic_cfg write has no data";
    case RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/regmap: ep_generic_cfg write extends past the table's own current extent";
    default:
        return "rcp/regmap: ep_generic_cfg unknown configuration-write error";
    }
}

//cfusa:req REQ-RMAP-079
rcp_regmap_ep_generic_cfg_reconfig_errc_t
rcp_regmap_ep_generic_cfg_apply_reconfig(rcp_regmap_ep_generic_cfg_t *entries, size_t count,
                                          uint16_t relative_start_address,
                                          const uint8_t *data, size_t data_len)
{
    size_t touched_start, touched_end;
    size_t row_start_idx, row_end_idx;
    size_t row_i;

    if (data_len == 0u) return RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_SHORT;

    touched_start = relative_start_address;
    touched_end   = touched_start + data_len;
    if (touched_end > count * 12u) return RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_OUT_OF_RANGE;

    row_start_idx = touched_start / 12u;
    row_end_idx   = (touched_end - 1u) / 12u; /* inclusive */

    /* PER-FIELD, not whole-row/whole-buffer -- see this function's own doc
     * comment (regmap.h) for why the whole-buffer render/patch/reparse
     * idiom every sibling apply_reconfig() uses is unsafe here. Each field
     * below is updated ONLY if [row_base+offset, row_base+offset+width) is
     * entirely within [touched_start, touched_end); ep_type (offset 0) is
     * never updated at all, matching TC18 §13.7.1.2's own "no effect"
     * rule for read-only registers. */
    for (row_i = row_start_idx; row_i <= row_end_idx; row_i++) {
        size_t row_base = row_i * 12u;

        if (touched_start <= row_base + 1u && row_base + 1u + 1u <= touched_end) {
            uint8_t octet1 = data[(row_base + 1u) - touched_start];

            entries[row_i].ep_used       = (octet1 & 0x01u) != 0u;
            entries[row_i].ep_delay_time =
                rcp_regmap_ep_delay_time_reg_to_us((uint8_t)((octet1 >> 4) & 0x3u));
        }

        if (touched_start <= row_base + 2u && row_base + 2u + 2u <= touched_end) {
            uint16_t words = get_u16(&data[(row_base + 2u) - touched_start]);

            entries[row_i].ep_req_storage_size = rcp_regmap_ep_req_storage_size_words_to_octets(words);
        }

        if (touched_start <= row_base + 4u && row_base + 4u + 4u <= touched_end) {
            entries[row_i].ep_description = get_u32(&data[(row_base + 4u) - touched_start]);
        }

        if (touched_start <= row_base + 8u && row_base + 8u + 2u <= touched_end) {
            entries[row_i].ep_tx_buffer_size = get_u16(&data[(row_base + 8u) - touched_start]);
        }

        if (touched_start <= row_base + 10u && row_base + 10u + 2u <= touched_end) {
            entries[row_i].ep_rx_buffer_size = get_u16(&data[(row_base + 10u) - touched_start]);
        }
    }

    return RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK;
}

//cfusa:req REQ-RMAP-017
void rcp_regmap_ep_functional_cfg_init(rcp_regmap_ep_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

//cfusa:req REQ-RMAP-066
//cfusa:req REQ-RMAP-067
void rcp_regmap_svr_ep_cfg_init(rcp_regmap_svr_ep_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->svr_discovery_timeout = 20000u; /* REQ-RMAP-066: TC18's own stated
                                             default, 20000 us = 20 ms */
}

/* ── Per-endpoint-type named-signal index ──────────────────────────────────── */

//cfusa:req REQ-RMAP-014
//cfusa:req REQ-RMAP-015
const char *rcp_regmap_named_signal_string(rcp_regmap_named_signal_t sig)
{
    switch (sig) {
    case RCP_REGMAP_SIGNAL_GPIO0:  return "GPIO0";
    case RCP_REGMAP_SIGNAL_GPIO1:  return "GPIO1";
    case RCP_REGMAP_SIGNAL_GPIO2:  return "GPIO2";
    case RCP_REGMAP_SIGNAL_GPIO3:  return "GPIO3";
    case RCP_REGMAP_SIGNAL_GPIO4:  return "GPIO4";
    case RCP_REGMAP_SIGNAL_GPIO5:  return "GPIO5";
    case RCP_REGMAP_SIGNAL_GPIO6:  return "GPIO6";
    case RCP_REGMAP_SIGNAL_GPIO7:  return "GPIO7";
    case RCP_REGMAP_SIGNAL_GPIO8:  return "GPIO8";
    case RCP_REGMAP_SIGNAL_GPIO9:  return "GPIO9";
    case RCP_REGMAP_SIGNAL_GPIO10: return "GPIO10";
    case RCP_REGMAP_SIGNAL_GPIO11: return "GPIO11";
    case RCP_REGMAP_SIGNAL_GPIO12: return "GPIO12";
    case RCP_REGMAP_SIGNAL_GPIO13: return "GPIO13";
    case RCP_REGMAP_SIGNAL_GPIO14: return "GPIO14";
    case RCP_REGMAP_SIGNAL_GPIO15: return "GPIO15";
    case RCP_REGMAP_SIGNAL_GPIO16: return "GPIO16";
    case RCP_REGMAP_SIGNAL_GPIO17: return "GPIO17";
    case RCP_REGMAP_SIGNAL_GPIO18: return "GPIO18";
    case RCP_REGMAP_SIGNAL_GPIO19: return "GPIO19";
    case RCP_REGMAP_SIGNAL_GPIO20: return "GPIO20";
    case RCP_REGMAP_SIGNAL_GPIO21: return "GPIO21";
    case RCP_REGMAP_SIGNAL_GPIO22: return "GPIO22";
    case RCP_REGMAP_SIGNAL_GPIO23: return "GPIO23";
    case RCP_REGMAP_SIGNAL_GPIO24: return "GPIO24";
    case RCP_REGMAP_SIGNAL_GPIO25: return "GPIO25";
    case RCP_REGMAP_SIGNAL_GPIO26: return "GPIO26";
    case RCP_REGMAP_SIGNAL_GPIO27: return "GPIO27";
    case RCP_REGMAP_SIGNAL_GPIO28: return "GPIO28";
    case RCP_REGMAP_SIGNAL_GPIO29: return "GPIO29";
    case RCP_REGMAP_SIGNAL_GPIO30: return "GPIO30";
    case RCP_REGMAP_SIGNAL_GPIO31: return "GPIO31";
    case RCP_REGMAP_SIGNAL_SPI_CLK:  return "SPI_CLK";
    case RCP_REGMAP_SIGNAL_SPI_PICO: return "SPI_PICO";
    case RCP_REGMAP_SIGNAL_SPI_POCI: return "SPI_POCI";
    case RCP_REGMAP_SIGNAL_SPI_CS0:  return "SPI_CS0";
    case RCP_REGMAP_SIGNAL_SPI_CS1:  return "SPI_CS1";
    case RCP_REGMAP_SIGNAL_SPI_CS2:  return "SPI_CS2";
    case RCP_REGMAP_SIGNAL_SPI_CS3:  return "SPI_CS3";
    case RCP_REGMAP_SIGNAL_SPI_CS4:  return "SPI_CS4";
    case RCP_REGMAP_SIGNAL_SPI_CS5:  return "SPI_CS5";
    case RCP_REGMAP_SIGNAL_I2C_SCL:  return "I2C_SCL";
    case RCP_REGMAP_SIGNAL_I2C_SDA:  return "I2C_SDA";
    case RCP_REGMAP_SIGNAL_UART_TX:  return "UART_TX";
    case RCP_REGMAP_SIGNAL_UART_RX:  return "UART_RX";
    case RCP_REGMAP_SIGNAL_UART_RTS: return "UART_RTS";
    case RCP_REGMAP_SIGNAL_UART_CTS: return "UART_CTS";
    case RCP_REGMAP_SIGNAL_LIN_TXD:  return "LIN_TXD";
    case RCP_REGMAP_SIGNAL_LIN_RXD:  return "LIN_RXD";
    case RCP_REGMAP_SIGNAL_LIN_NSLP: return "LIN_NSLP";
    case RCP_REGMAP_SIGNAL_PWM_OUT:  return "PWM_OUT";
    case RCP_REGMAP_SIGNAL_PWM_OUTN: return "PWM_OUTN";
    case RCP_REGMAP_SIGNAL_PWM_IN:   return "PWM_IN";
    case RCP_REGMAP_SIGNAL_ADC_IN:   return "ADC_IN";
    case RCP_REGMAP_SIGNAL_DAC_OUT:  return "DAC_OUT";
    case RCP_REGMAP_SIGNAL_CAN_RXD:  return "CAN_RXD";
    case RCP_REGMAP_SIGNAL_CAN_TXD:  return "CAN_TXD";
    case RCP_REGMAP_SIGNAL_ISELED_ISP_P: return "ISELED_ISP_P";
    case RCP_REGMAP_SIGNAL_ISELED_ISP_N: return "ISELED_ISP_N";
    case RCP_REGMAP_SIGNAL_MDIO_MDC:  return "MDIO_MDC";
    case RCP_REGMAP_SIGNAL_MDIO_DATA: return "MDIO_DATA";
    default: return "unknown";
    }
}

//cfusa:req REQ-RMAP-045
uint8_t rcp_regmap_named_signal_ep_signal_nr(rcp_regmap_named_signal_t sig)
{
    /* Explicit lower bound: RCP_REGMAP_SIGNAL_GPIO0 == 0, so the range
     * checks below (each an upper-bound-only "sig <= ...") would
     * otherwise treat a negative/garbage sig as if it were a small
     * GPIO index, silently violating this function's own "0 for any
     * out-of-range value" documented contract. */
    if (sig < RCP_REGMAP_SIGNAL_GPIO0) return 0u;

    if (sig <= RCP_REGMAP_SIGNAL_GPIO31) {
        return (uint8_t)sig; /* GPIOn's own per-type number is n */
    }
    if (sig <= RCP_REGMAP_SIGNAL_SPI_CS5) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_SPI_CLK);
    }
    if (sig <= RCP_REGMAP_SIGNAL_I2C_SDA) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_I2C_SCL);
    }
    if (sig <= RCP_REGMAP_SIGNAL_UART_CTS) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_UART_TX);
    }
    if (sig <= RCP_REGMAP_SIGNAL_LIN_NSLP) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_LIN_TXD);
    }
    if (sig <= RCP_REGMAP_SIGNAL_PWM_OUTN) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_PWM_OUT);
    }
    if (sig == RCP_REGMAP_SIGNAL_PWM_IN)  return 0u;
    if (sig == RCP_REGMAP_SIGNAL_ADC_IN)  return 0u;
    if (sig == RCP_REGMAP_SIGNAL_DAC_OUT) return 0u;
    if (sig <= RCP_REGMAP_SIGNAL_CAN_TXD) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_CAN_RXD);
    }
    if (sig <= RCP_REGMAP_SIGNAL_ISELED_ISP_N) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_ISELED_ISP_P);
    }
    if (sig <= RCP_REGMAP_SIGNAL_MDIO_DATA) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_MDIO_MDC);
    }
    return 0u; /* RCP_REGMAP_SIGNAL_COUNT or any other invalid value */
}

/* ── Request-stream and response/ack queue config ──────────────────────────── */

//cfusa:req REQ-RMAP-018
//cfusa:req REQ-RMAP-049
void rcp_regmap_request_stream_cfg_init(rcp_regmap_request_stream_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    /* REQ-RMAP-049: rx_resp_stream_index's own power-on default is 1, not
     * 0 -- TC18's own deliberate bootstrap guarantee (a freshly reset
     * server can answer a discovery request before any configuration has
     * been written) requires this one field to be the sole, explicit
     * exception to every other field's own zero default. See this
     * field's own doc comment (regmap.h) and REQ-RMAP-018's own
     * corrected text for why "zero everything" is no longer the whole
     * rule as of this field's own addition. */
    cfg->rx_resp_stream_index = 1u;
}

//cfusa:req REQ-RMAP-019
void rcp_regmap_response_queue_cfg_init(rcp_regmap_response_queue_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

//cfusa:req REQ-RMAP-050
bool rcp_regmap_wd_timeout_ms_to_ticks(uint32_t timeout_ms,
                                        uint32_t ms_per_tick,
                                        uint16_t *out_ticks)
{
    uint32_t ticks;

    if (ms_per_tick == 0u) return false; /* no register value for a zero-length tick */

    /* Round down: a requested watchdog period that does not divide evenly
     * into whole tics is truncated, not rounded up, so the register's
     * enforced period is never longer than the caller asked for -- a
     * safety-integrity register should never silently grant more slack
     * than requested. */
    ticks = timeout_ms / ms_per_tick;
    if (ticks > (uint32_t)UINT16_MAX) return false; /* REQ-RMAP-050: 16-bit register width */

    *out_ticks = (uint16_t)ticks;
    return true;
}

//cfusa:req REQ-RMAP-050
bool rcp_regmap_wd_timeout_ticks_to_ms(uint16_t ticks,
                                        uint32_t ms_per_tick,
                                        uint32_t *out_timeout_ms)
{
    uint64_t product;

    if (ms_per_tick == 0u) return false; /* no meaningful conversion for a zero-length tick */

    product = (uint64_t)ticks * (uint64_t)ms_per_tick;
    if (product > (uint64_t)UINT32_MAX) return false;

    *out_timeout_ms = (uint32_t)product;
    return true;
}

/* ── request-stream-cfg wire codec (issue #306, REQ-RMAP-047/048/049/050) ──
 * See this function's own doc comment (regmap.h) for the full field-by-
 * field mapping and the deliberately-excluded fields' own reasoning. */

//cfusa:req REQ-RMAP-047
//cfusa:req REQ-RMAP-048
//cfusa:req REQ-RMAP-049
//cfusa:req REQ-RMAP-050
//cfusa:req REQ-RMAP-071
//cfusa:req REQ-E2E-046
//cfusa:req REQ-RMAP-051
void rcp_regmap_request_stream_cfg_render(const rcp_regmap_request_stream_cfg_t *entries,
                                           size_t count, uint8_t *out,
                                           uint32_t watchdog_ms_per_tick,
                                           const bool *rx_stream_status_blocked)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const rcp_regmap_request_stream_cfg_t *e = &entries[i];
        uint8_t                                bits_0x000d;
        uint16_t                               max_request_size_wire;
        uint8_t                                safestate_sequencer_wire;
        uint16_t                               wd_timeout_ticks;

        put_u64(&out[24u * i + 0x0000u], e->rx_stream_id);

        max_request_size_wire = (e->rx_stream_max_request_size > 0xFFFFu)
                                     ? (uint16_t)0xFFFFu
                                     : (uint16_t)e->rx_stream_max_request_size; /* saturate --
                                                                                    see this
                                                                                    function's
                                                                                    own doc
                                                                                    comment
                                                                                    (regmap.h) */
        put_u16(&out[24u * i + 0x0008u], max_request_size_wire);

        /* REQ-RMAP-050: falls back to 0x0000 if the internal ms value
         * cannot be represented at watchdog_ms_per_tick's own rate
         * (including watchdog_ms_per_tick == 0, "not configured") --
         * see this function's own doc comment (regmap.h) for the full
         * fail-closed rationale. */
        if (rcp_regmap_wd_timeout_ms_to_ticks(e->rx_wd_timeout_ms, watchdog_ms_per_tick,
                                               &wd_timeout_ticks)) {
            put_u16(&out[24u * i + 0x000Au], wd_timeout_ticks);
        } else {
            out[24u * i + 0x000Au] = 0x00u;
            out[24u * i + 0x000Bu] = 0x00u;
        }

        out[24u * i + 0x000Cu] = e->rx_secure_channel_index;

        /* REQ-RMAP-047..051/071 (issue #458): TC18 RC5's own real Table
         * 24 layout for this octet is 4 meaningful bits (bit0
         * rx_enforce_crc, bit1 rx_enforce_sequence, bit2
         * rx_enforce_watchdog, bit3 rx_enforce_request_filing; bits
         * [6:4] Reserved/R-only), NOT this codebase's old RC1-baseline
         * 8-independent-bit model -- see this function's own doc
         * comment (regmap.h) for the full reconciliation.
         *
         *   - bit0: rx_enforce_crc is a pure rename of rx_enforce_e2e,
         *     unchanged single-bit semantics.
         *   - bit1/bit2: RC5's own spec text ties BOTH actions of each
         *     bit together atomically ("stream is blocked ... AND Safe
         *     state will be entered") -- this codebase's own richer
         *     model keeps the "block" (rx_enforce_seq/rx_wd_enable) and
         *     "also enter safe state" (rx_seq_safestate_enable/
         *     rx_wd_safestate_enable) dimensions independently
         *     expressible (e2e.h's own deliberate design), so each wire
         *     bit renders true only when BOTH internal dimensions agree
         *     (logical AND, never OR): this can never overstate a
         *     safety guarantee (claim "blocked AND safe-state-entered"
         *     to a real RC5 peer when only one of those two is actually
         *     configured). A real RC5 write can only ever produce this
         *     exact coupled state anyway -- see apply_reconfig() below.
         *   - bit3: rx_enforce_request_filing maps directly,
         *     uncombined, from rx_ovrflw_safestate_enable -- that field
         *     never had a separate "enable" dimension of its own (TC18
         *     couples it to a single bit on both sides).
         *   - bits [6:4]: Reserved, always render 0.
         *   - rx_safety_measure no longer has any wire register
         *     position of its own -- same disposition already
         *     established for rx_wd_info_enable (see that field's own
         *     doc comment, regmap.h): content-modeling only. */
        bits_0x000d  = (uint8_t)(e->rx_enforce_e2e ? 0x01u : 0x00u);
        bits_0x000d |= (uint8_t)((e->rx_enforce_seq && e->rx_seq_safestate_enable) ? 0x02u
                                                                                    : 0x00u);
        bits_0x000d |= (uint8_t)((e->rx_wd_enable && e->rx_wd_safestate_enable) ? 0x04u : 0x00u);
        bits_0x000d |= (uint8_t)(e->rx_ovrflw_safestate_enable ? 0x08u : 0x00u);
        /* REQ-E2E-046/REQ-RMAP-051 (issue #424): bit 7 is TC18's own
         * distinct, live rx_stream_status bit, NOT rx_wd_info_enable --
         * see this function's own doc comment (regmap.h) for the full
         * mis-wiring-found-and-fixed history. */
        bits_0x000d |= (uint8_t)((rx_stream_status_blocked != NULL &&
                                   rx_stream_status_blocked[i])
                                      ? 0x80u
                                      : 0x00u);
        out[24u * i + 0x000Du] = bits_0x000d;

        safestate_sequencer_wire = (e->rx_safestate_sequencer > 0xFFu)
                                        ? (uint8_t)0xFFu
                                        : (uint8_t)e->rx_safestate_sequencer; /* saturate --
                                                                                  see this
                                                                                  function's
                                                                                  own doc
                                                                                  comment
                                                                                  (regmap.h) */
        out[24u * i + 0x000Eu] = safestate_sequencer_wire;

        out[24u * i + 0x000Fu] = e->rx_safe_sequencer_state;
        out[24u * i + 0x0010u] = e->rx_ack_stream_index;
        out[24u * i + 0x0011u] = e->rx_resp_stream_index;

        out[24u * i + 0x0012u] = 0x00u; /* reserved (16 bit) */
        out[24u * i + 0x0013u] = 0x00u;
        out[24u * i + 0x0014u] = 0x00u; /* reserved (32 bit) */
        out[24u * i + 0x0015u] = 0x00u;
        out[24u * i + 0x0016u] = 0x00u;
        out[24u * i + 0x0017u] = 0x00u;
    }
}

//cfusa:req REQ-RMAP-047
//cfusa:req REQ-RMAP-048
//cfusa:req REQ-RMAP-049
//cfusa:req REQ-RMAP-071
const char *
rcp_regmap_request_stream_cfg_reconfig_strerror(rcp_regmap_request_stream_cfg_reconfig_errc_t e)
{
    switch (e) {
    case RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK:
        return "rcp/regmap: request-stream-cfg write applied";
    case RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_SHORT:
        return "rcp/regmap: request-stream-cfg write has no data";
    case RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/regmap: request-stream-cfg write extends past the table's own current extent";
    default:
        return "rcp/regmap: request-stream-cfg unknown configuration-write error";
    }
}

//cfusa:req REQ-RMAP-047
//cfusa:req REQ-RMAP-048
//cfusa:req REQ-RMAP-049
//cfusa:req REQ-RMAP-071
rcp_regmap_request_stream_cfg_reconfig_errc_t
rcp_regmap_request_stream_cfg_apply_reconfig(rcp_regmap_request_stream_cfg_t *entries,
                                              size_t count,
                                              uint16_t relative_start_address,
                                              const uint8_t *data, size_t data_len,
                                              uint32_t watchdog_ms_per_tick)
{
    uint8_t block[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES * 24u];
    size_t  block_len = count * 24u;
    size_t  i;

    if (data_len == 0u) return RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_SHORT;

    if ((size_t)relative_start_address + data_len > block_len) {
        return RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Same "render current image, patch the addressed octets, re-parse
     * the whole image back" idiom every other pointed-to table's own
     * apply_reconfig() already uses. NULL for rx_stream_status_blocked
     * (bit 7's own live-status source, issue #424) is correct and safe
     * here regardless of any real live status: this function never
     * reads bit 7 back out of block[] below (see this loop's own
     * bits_0x000d handling), so whatever renders into that one bit of
     * this transient substrate image has no effect on the final result
     * either way. */
    rcp_regmap_request_stream_cfg_render(entries, count, block, watchdog_ms_per_tick, NULL);
    for (i = 0; i < data_len; i++) {
        block[relative_start_address + i] = data[i]; /* every octet R/W*, none read-only */
    }
    for (i = 0; i < count; i++) {
        uint8_t  bits_0x000d = block[24u * i + 0x000Du];
        uint32_t wd_timeout_ms;

        entries[i].rx_stream_id               = get_u64(&block[24u * i + 0x0000u]);
        entries[i].rx_stream_max_request_size = (size_t)get_u16(&block[24u * i + 0x0008u]);
        /* REQ-RMAP-050: the arriving wire ticks value is always exactly
         * 16 bits by construction, so it can't itself violate any
         * width constraint -- if the ticks-to-ms conversion still fails
         * (watchdog_ms_per_tick == 0, or a degenerate internal-
         * representation overflow), entries[i].rx_wd_timeout_ms is
         * simply not assigned here, leaving its own pre-existing value
         * untouched -- see this function's own doc comment (regmap.h)
         * for the full rationale. */
        if (rcp_regmap_wd_timeout_ticks_to_ms(get_u16(&block[24u * i + 0x000Au]),
                                               watchdog_ms_per_tick, &wd_timeout_ms)) {
            entries[i].rx_wd_timeout_ms = wd_timeout_ms;
        }
        entries[i].rx_secure_channel_index    = block[24u * i + 0x000Cu];

        /* CORRECTED 2026-08-14 (issue #458): unpack the real RC5 4-bit
         * layout, not the old RC1-baseline 8-independent-bit model --
         * see rcp_regmap_request_stream_cfg_render()'s own doc comment
         * above (this same fix's render-side half) for the full
         * reconciliation. bit1/bit2 each set BOTH of this codebase's own
         * two internal dimensions together: a real RC5 write can only
         * ever express the coupled "block AND enter safe state" state
         * (there is no wire encoding for "block but don't enter safe
         * state" or vice versa), so both rx_enforce_seq/
         * rx_seq_safestate_enable (and rx_wd_enable/
         * rx_wd_safestate_enable) always end up equal after a write --
         * exactly the coupled subset this codebase's own richer,
         * independently-expressible superset model was always said to
         * represent correctly (see regmap.h's own "terminology drift"
         * file-header note). */
        entries[i].rx_enforce_e2e = (bits_0x000d & 0x01u) != 0u;
        {
            bool seq_bit = (bits_0x000d & 0x02u) != 0u;

            entries[i].rx_enforce_seq          = seq_bit;
            entries[i].rx_seq_safestate_enable = seq_bit;
        }
        {
            bool wd_bit = (bits_0x000d & 0x04u) != 0u;

            entries[i].rx_wd_enable           = wd_bit;
            entries[i].rx_wd_safestate_enable = wd_bit;
        }
        entries[i].rx_ovrflw_safestate_enable = (bits_0x000d & 0x08u) != 0u;
        /* rx_safety_measure has no wire register position at all
         * anymore (bits [6:4] are Reserved) -- left unchanged, the same
         * disposition rx_wd_info_enable already has; see that field's
         * own doc comment (regmap.h). */
        /* bit 7 (rx_stream_status, issue #424) is intentionally NOT
         * unpacked into any struct field here -- it is a live,
         * server-computed status, not client-configurable content; a
         * write touching it has no effect, the same "no struct field
         * consumes it" treatment this function's own reserved trailing
         * octets already receive (see this function's own doc comment,
         * regmap.h). rx_wd_info_enable no longer has any wire register
         * position at all -- see that field's own doc comment.
         *
         * INVESTIGATED 2026-08-14 (c-RCP-AUDIT-27, issue #448): re-read
         * TC18 §12.7.7 Table 24's own rx_stream_status row and its
         * margin comment directly against the primary-source PDF
         * (OA_TC18_specification_v_0.5.1_RC_5) before writing this note.
         * The row's ONLY behavioral text describes how the bit gets SET
         * ("will be set automatically as a reaction to either CRC
         * error, sequence error, watchdog overflow, EP overflow, when
         * enabled") -- there is no sentence anywhere in Table 24, nor in
         * §12.7.7's surrounding prose, describing what a CLIENT WRITE to
         * this bit does. This is genuine spec silence, not an
         * under-read: TC18 demonstrably DOES spell out write-clears
         * semantics in prose, in the exact phrasing pattern that would
         * be expected here, when it means them -- compare
         * wup_status (§13.7.2.2 Table 39: "Indication of wake-up
         * source, writing "1" clears the flag") and
         * ep_clear_req_storage (§13.7 Table 34 common entries:
         * "writing a 1b clears the EPs request storage, reads always
         * 0"). rx_stream_status's own row has no such sentence; its R/W
         * typing (vs. every sibling bit's R/W*, see this table's own
         * write-authorization carve-out above) only establishes that a
         * write is architecturally PERMITTED at all lifecycle states,
         * not what that write does. Force-implementing a write-clears-
         * the-latch behavior here would be inventing non-conformant
         * behavior the spec does not ask for, not fixing a bug -- left
         * as a documented, investigated no-op; see issue #448's own
         * closing comment for the same finding. */

        entries[i].rx_safestate_sequencer     = (uint16_t)block[24u * i + 0x000Eu];
        entries[i].rx_safe_sequencer_state    = block[24u * i + 0x000Fu];
        entries[i].rx_ack_stream_index        = block[24u * i + 0x0010u];
        entries[i].rx_resp_stream_index       = block[24u * i + 0x0011u];
    }

    return RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK;
}

//cfusa:req REQ-RMAP-060
rcp_stream_id_t rcp_regmap_response_queue_stream_id(const rcp_regmap_response_queue_cfg_t *cfg,
                                                     const uint8_t mac[6])
{
    return rcp_stream_id_make(mac, cfg->stream_uid);
}

//cfusa:req REQ-SEQ-013
uint8_t rcp_regmap_request_stream_cfg_resolve_index(
    const rcp_regmap_request_stream_cfg_t *entries, size_t count, uint64_t stream_id)
{
    size_t i;

    if (!entries) return 0u;

    for (i = 0; i < count; i++) {
        if (entries[i].rx_stream_id == stream_id) return (uint8_t)(i + 1u);
    }
    return 0u; /* no match -- 0 is the "no such request stream" sentinel */
}

/* ── SEQUENCER_config wire codec (issue #335, REQ-SEQ-013/REQ-SEQ-014) ─────
 * See rcp_regmap_sequencer_table_render()'s own doc comment (regmap.h) for
 * the full field mapping and the conformance defect this batch corrects. */

//cfusa:req REQ-SEQ-014
void rcp_regmap_sequencer_table_render(const uint8_t *state, const uint8_t *owner,
                                        size_t count, uint8_t *out)
{
    size_t i;

    for (i = 0; i < count; i++) {
        out[2u * i + 0u] = state[i];
        out[2u * i + 1u] = owner[i];
    }
}

//cfusa:req REQ-SEQ-013
//cfusa:req REQ-SEQ-014
rcp_regmap_sequencer_table_reconfig_errc_t
rcp_regmap_sequencer_table_apply_reconfig(uint8_t *state, uint8_t *owner, size_t count,
                                           uint16_t relative_start_address,
                                           const uint8_t *data, size_t data_len)
{
    uint8_t block[RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES * 2u];
    size_t  block_len = count * 2u;
    size_t  i;

    if (data_len == 0u) return RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_ERR_SHORT;

    if ((size_t)relative_start_address + data_len > block_len) {
        return RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Same "render current image, patch the addressed octets, re-parse
     * the whole image back" idiom every other pointed-to table's own
     * apply_reconfig() already uses. */
    rcp_regmap_sequencer_table_render(state, owner, count, block);
    for (i = 0; i < data_len; i++) {
        block[relative_start_address + i] = data[i]; /* every octet R/W or R/W* */
    }
    for (i = 0; i < count; i++) {
        state[i] = block[2u * i + 0u];
        owner[i] = block[2u * i + 1u];
    }

    return RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_OK;
}

/* ── EP-ID / byte_bus_id map ────────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-020
//cfusa:req REQ-RMAP-021
//cfusa:req REQ-RMAP-022
bool rcp_regmap_ep_id_map_is_ascending(const rcp_regmap_ep_id_map_entry_t *entries,
                                        size_t count)
{
    size_t i;

    if (count < 2) return true; /* vacuously ascending */

    for (i = 1; i < count; i++) {
        const rcp_regmap_ep_id_map_entry_t *prev = &entries[i - 1];
        const rcp_regmap_ep_id_map_entry_t *cur   = &entries[i];

        if (prev->request_stream_index != cur->request_stream_index) {
            /* A higher stream index always counts as ascending -- TC18
             * does not require byte_bus_id to relate across different
             * streams, only strictly increase within one (REQ-RMAP-056). */
            if (!(prev->request_stream_index < cur->request_stream_index)) return false;
            continue;
        }
        if (!(prev->byte_bus_id < cur->byte_bus_id)) return false;
    }

    return true;
}

//cfusa:req REQ-RMAP-054
size_t rcp_regmap_ep_id_map_effective_count(const rcp_regmap_ep_id_map_entry_t *entries,
                                             size_t capacity)
{
    size_t i;

    for (i = 0; i < capacity; i++) {
        if (entries[i].request_stream_index == 0u) return i;
    }

    return capacity;
}

//cfusa:req REQ-RMAP-054
void rcp_regmap_ep_id_map_row_init_default(rcp_regmap_ep_id_map_entry_t *row)
{
    row->request_stream_index = 1u;
    row->ep_id                = RCP_REGMAP_EP0_INDEX;
    row->byte_bus_id          = 0u;
}

//cfusa:req REQ-RMAP-057
bool rcp_regmap_ep_id_map_has_single_client_per_ep(const rcp_regmap_ep_id_map_entry_t *entries,
                                                    size_t count)
{
    size_t i, j;

    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (entries[i].ep_id == entries[j].ep_id &&
                entries[i].request_stream_index != entries[j].request_stream_index) {
                return false;
            }
        }
    }

    return true;
}

//cfusa:req REQ-RMAP-058
bool rcp_regmap_ep_id_map_shared_bus_homogeneous(const rcp_regmap_ep_id_map_entry_t *entries,
                                                  const uint8_t *ep_types,
                                                  size_t count)
{
    size_t i, j;

    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (entries[i].request_stream_index == entries[j].request_stream_index &&
                entries[i].byte_bus_id == entries[j].byte_bus_id &&
                ep_types[i] != ep_types[j]) {
                return false;
            }
        }
    }

    return true;
}

//cfusa:req REQ-WAKEUP-020
bool rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id(const rcp_regmap_ep_id_map_entry_t *entries,
                                                    const uint8_t *ep_types, size_t count,
                                                    uint8_t target_ep_type,
                                                    uint16_t required_ep_id)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (ep_types[i] == target_ep_type && entries[i].ep_id != required_ep_id) {
            return false;
        }
    }

    return true;
}

//cfusa:req REQ-E2E-029
//cfusa:req REQ-E2E-030
//cfusa:req REQ-E2E-045
size_t rcp_regmap_ep_id_map_byte_bus_ids_for_stream(const rcp_regmap_ep_id_map_entry_t *entries,
                                                      size_t count, uint8_t request_stream_index,
                                                      rcp_byte_bus_id_t *out_byte_bus_ids,
                                                      size_t out_capacity)
{
    size_t found = 0;
    size_t i, j;

    for (i = 0; i < count; i++) {
        bool already_written;

        if (entries[i].request_stream_index != request_stream_index) continue;

        already_written = false;
        for (j = 0; j < i; j++) {
            if (entries[j].request_stream_index == request_stream_index &&
                entries[j].byte_bus_id == entries[i].byte_bus_id) {
                already_written = true;
                break;
            }
        }
        if (already_written) continue;

        if (found < out_capacity) out_byte_bus_ids[found] = entries[i].byte_bus_id;
        found++;
    }

    return found;
}

//cfusa:req REQ-LIFECYCLE-025
//cfusa:req REQ-LIFECYCLE-031
bool rcp_regmap_ep_id_map_is_valid_association(const rcp_regmap_ep_id_map_entry_t *entries,
                                                 size_t count, uint8_t request_stream_index,
                                                 rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (entries[i].request_stream_index == request_stream_index &&
            entries[i].byte_bus_id == byte_bus_id) {
            return true;
        }
    }

    return false;
}
