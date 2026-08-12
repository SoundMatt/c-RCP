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
 * independent single bits, matching Table 18 exactly. */

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

/* ── Table 18 wire codec (REQ-RMAP-024) ──────────────────────────────────────
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

//cfusa:req REQ-RMAP-024
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

//cfusa:req REQ-RMAP-040
//cfusa:req REQ-RMAP-041
//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-054
//cfusa:req REQ-RMAP-061
rcp_regmap_ep0_errc_t
rcp_regmap_ep0_decode_write_request(const uint8_t *b, size_t len,
                                     const rcp_regmap_general_t *map,
                                     rcp_regmap_hw_pin_map_entry_t *hw_pin_map,
                                     size_t hw_pin_map_count,
                                     rcp_regmap_ep_id_map_entry_t *ep_id_map,
                                     size_t ep_id_map_count,
                                     rcp_regmap_response_queue_cfg_t *response_queue_cfg,
                                     size_t response_queue_cfg_count,
                                     rcp_wire_error_t *out_error,
                                     uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    rcp_lifecycle_writer_ctx_t   writer = {0};
    uint16_t                     addr;
    size_t                       data_len;
    size_t                       hw_cfg_len;
    size_t                       ep_id_map_len;
    size_t                       response_queue_cfg_len;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_REGMAP_EP0_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_REGMAP_EP0_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != RCP_REGMAP_EP0_INDEX) return RCP_REGMAP_EP0_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_REGMAP_EP0_ERR_WRONG_OP;
    if (payload_len < 2u) return RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD;

    addr     = get_u16(payload);
    data_len = payload_len - 2u;
    *out_transaction_num = hdr.transaction_num;

    if ((size_t)addr < RCP_REGMAP_GENERAL_LEN) {
        /* Table 18's own extent -- unconditionally read-only (REQ-RMAP-025),
         * reusing rcp_lifecycle_field_write_error() exactly as
         * rcp_regmap_general_decode_write_request() already does, not a
         * second, separately-maintained copy of that logic. */
        *out_error = rcp_lifecycle_field_write_error(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                      RCP_LIFECYCLE_FIELD_READ_ONLY, writer);
        return RCP_REGMAP_EP0_OK;
    }

    hw_cfg_len = hw_pin_map_count * 3u;
    if ((size_t)addr >= map->svr_hw_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_hw_cfg_ptr + hw_cfg_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_hw_cfg_ptr);
        rcp_regmap_hw_pin_map_reconfig_errc_t rc;

        rc = rcp_regmap_hw_pin_map_apply_reconfig(hw_pin_map, hw_pin_map_count, relative,
                                                    &payload[2], data_len);
        *out_error = (rc == RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    ep_id_map_len = ep_id_map_count * 4u;
    if ((size_t)addr >= map->svr_ep_bytebus_id_map_ptr &&
        (size_t)addr < (size_t)map->svr_ep_bytebus_id_map_ptr + ep_id_map_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_ep_bytebus_id_map_ptr);
        rcp_regmap_ep_id_map_reconfig_errc_t rc;

        rc = rcp_regmap_ep_id_map_apply_reconfig(ep_id_map, ep_id_map_count, relative,
                                                   &payload[2], data_len);
        *out_error = (rc == RCP_REGMAP_EP_ID_MAP_RECONFIG_OK) ? RCP_ERROR_NONE
                                                               : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    response_queue_cfg_len = response_queue_cfg_count * 10u;
    if ((size_t)addr >= map->svr_response_stream_cfg_ptr &&
        (size_t)addr < (size_t)map->svr_response_stream_cfg_ptr + response_queue_cfg_len) {
        uint16_t relative = (uint16_t)((size_t)addr - map->svr_response_stream_cfg_ptr);
        rcp_regmap_response_queue_cfg_reconfig_errc_t rc;

        rc = rcp_regmap_response_queue_cfg_apply_reconfig(response_queue_cfg, response_queue_cfg_count,
                                                             relative, &payload[2], data_len);
        *out_error = (rc == RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_OK) ? RCP_ERROR_NONE
                                                                        : RCP_ERROR_INVALID_PARAMETER;
        return RCP_REGMAP_EP0_OK;
    }

    /* Neither Table 18's own extent nor any known pointed-to table --
     * see this function's own header doc comment for which tables this
     * milestone routes (issue #301). */
    *out_error = RCP_ERROR_EP_NOT_FOUND;
    return RCP_REGMAP_EP0_OK;
}

/* ── EP_ID_config wire stride (REQ-RMAP-052/054) ───────────────────────────── */

//cfusa:req REQ-RMAP-052
void rcp_regmap_ep_id_map_render(const rcp_regmap_ep_id_map_entry_t *entries, size_t count,
                                  uint8_t *out)
{
    size_t i;

    for (i = 0; i < count; i++) {
        out[4u * i + 0u] = entries[i].request_stream_index;
        out[4u * i + 1u] = (uint8_t)entries[i].ep_id; /* truncated -- see this
                                                          function's own doc
                                                          comment (regmap.h) */
        put_u16(&out[4u * i + 2u], (uint16_t)entries[i].byte_bus_id);
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
        entries[i].request_stream_index = block[4u * i + 0u];
        entries[i].ep_id                = block[4u * i + 1u]; /* zero-extends -- see this
                                                                   struct's own ep_id field
                                                                   comment (regmap.h) */
        entries[i].byte_bus_id           = (rcp_byte_bus_id_t)get_u16(&block[4u * i + 2u]);
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
rcp_lifecycle_writer_ctx_t rcp_regmap_writer_ctx(const rcp_regmap_general_t *map,
                                               const rcp_regmap_ep_client_t *ep_client,
                                               uint16_t requesting_stream_index,
                                               bool via_ep0,
                                               bool via_unicast,
                                               bool via_discovery_stream)
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

    return ctx;
}

/* ── The generic-vs-functional per-endpoint config split ───────────────────── */

//cfusa:req REQ-RMAP-016
void rcp_regmap_ep_generic_cfg_init(rcp_regmap_ep_generic_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
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

//cfusa:req REQ-RMAP-060
rcp_stream_id_t rcp_regmap_response_queue_stream_id(const rcp_regmap_response_queue_cfg_t *cfg,
                                                     const uint8_t mac[6])
{
    return rcp_stream_id_make(mac, cfg->stream_uid);
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
