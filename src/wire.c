#include "rcp/wire.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-UDP-014
const char *rcp_wire_strerror(rcp_wire_errc_t e)
{
    switch (e) {
    case RCP_WIRE_OK:              return "rcp/wire: success";
    case RCP_WIRE_ERR_SHORT_FRAME: return "rcp/wire: frame too short";
    case RCP_WIRE_ERR_BAD_MAGIC:   return "rcp/wire: bad magic bytes";
    case RCP_WIRE_ERR_BAD_VERSION: return "rcp/wire: unsupported protocol version";
    default:                       return "rcp/wire: unknown error";
    }
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

//cfusa:req REQ-UDP-001
//cfusa:req REQ-UDP-002
//cfusa:req REQ-UDP-006
//cfusa:req REQ-UDP-010
//cfusa:req REQ-UDP-011
rcp_wire_errc_t rcp_wire_validate_header(const uint8_t *b, size_t len)
{
    if (len < RCP_WIRE_HEADER_LEN) return RCP_WIRE_ERR_SHORT_FRAME;
    if (b[0] != RCP_WIRE_MAGIC_0 || b[1] != RCP_WIRE_MAGIC_1) return RCP_WIRE_ERR_BAD_MAGIC;
    if (b[2] != RCP_WIRE_PROTO_VERSION) return RCP_WIRE_ERR_BAD_VERSION;
    return RCP_WIRE_OK;
}

static rcp_bytes_t alloc_frame(uint8_t msg_type, rcp_zone_t zone, uint16_t field56,
                                uint8_t field7, uint32_t field8, const uint8_t *payload,
                                size_t payload_len)
{
    rcp_bytes_t frame;
    size_t n = RCP_WIRE_HEADER_LEN + payload_len;

    frame.data = (uint8_t *)malloc(n);
    frame.len  = 0;
    if (!frame.data) return frame;

    frame.data[0] = RCP_WIRE_MAGIC_0;
    frame.data[1] = RCP_WIRE_MAGIC_1;
    frame.data[2] = RCP_WIRE_PROTO_VERSION;
    frame.data[3] = msg_type;
    frame.data[4] = (uint8_t)zone;
    put_u16(&frame.data[5], field56);
    frame.data[7] = field7;
    put_u32(&frame.data[8], field8);
    put_u32(&frame.data[12], (uint32_t)payload_len);
    if (payload_len > 0) memcpy(&frame.data[RCP_WIRE_HEADER_LEN], payload, payload_len);
    frame.len = n;
    return frame;
}

//cfusa:req REQ-UDP-004
//cfusa:req REQ-UDP-005
//cfusa:req REQ-UDP-009
rcp_bytes_t rcp_wire_encode_command(const rcp_command_t *cmd)
{
    return alloc_frame(RCP_WIRE_TYPE_COMMAND, cmd->zone, (uint16_t)cmd->type,
                        (uint8_t)cmd->priority, cmd->id,
                        cmd->payload.data, cmd->payload.len);
}

//cfusa:req REQ-UDP-004
//cfusa:req REQ-UDP-005
//cfusa:req REQ-UDP-006
//cfusa:req REQ-UDP-013
rcp_wire_errc_t rcp_wire_decode_command(const uint8_t *b, size_t len, rcp_command_t *out)
{
    uint32_t body_len;
    rcp_wire_errc_t ec = rcp_wire_validate_header(b, len);
    if (ec != RCP_WIRE_OK) return ec;

    body_len = get_u32(&b[12]);
    if (body_len > RCP_WIRE_MAX_PAYLOAD) return RCP_WIRE_ERR_SHORT_FRAME;
    if (len < RCP_WIRE_HEADER_LEN + (size_t)body_len) return RCP_WIRE_ERR_SHORT_FRAME;

    out->zone     = (rcp_zone_t)b[4];
    out->type     = (rcp_command_type_t)get_u16(&b[5]);
    out->priority = (rcp_priority_t)b[7];
    out->id       = get_u32(&b[8]);
    out->payload  = rcp_bytes_dup(&b[RCP_WIRE_HEADER_LEN], body_len);
    return RCP_WIRE_OK;
}

//cfusa:req REQ-UDP-007
rcp_bytes_t rcp_wire_encode_response(const rcp_response_t *resp)
{
    return alloc_frame(RCP_WIRE_TYPE_RESPONSE, resp->zone, 0, (uint8_t)resp->status,
                        resp->command_id, resp->payload.data, resp->payload.len);
}

//cfusa:req REQ-UDP-007
//cfusa:req REQ-UDP-013
rcp_wire_errc_t rcp_wire_decode_response(const uint8_t *b, size_t len, rcp_response_t *out)
{
    uint32_t body_len;
    rcp_wire_errc_t ec = rcp_wire_validate_header(b, len);
    if (ec != RCP_WIRE_OK) return ec;

    body_len = get_u32(&b[12]);
    if (body_len > RCP_WIRE_MAX_PAYLOAD) return RCP_WIRE_ERR_SHORT_FRAME;
    if (len < RCP_WIRE_HEADER_LEN + (size_t)body_len) return RCP_WIRE_ERR_SHORT_FRAME;

    out->zone       = (rcp_zone_t)b[4];
    out->status     = (rcp_response_status_t)b[7];
    out->command_id = get_u32(&b[8]);
    out->payload    = rcp_bytes_dup(&b[RCP_WIRE_HEADER_LEN], body_len);
    return RCP_WIRE_OK;
}

//cfusa:req REQ-UDP-008
rcp_bytes_t rcp_wire_encode_status(const rcp_status_t *st)
{
    return alloc_frame(RCP_WIRE_TYPE_STATUS, st->zone, 0, st->healthy ? 1u : 0u,
                        st->seq, st->payload.data, st->payload.len);
}

//cfusa:req REQ-UDP-008
//cfusa:req REQ-UDP-013
rcp_wire_errc_t rcp_wire_decode_status(const uint8_t *b, size_t len, rcp_status_t *out)
{
    uint32_t body_len;
    rcp_wire_errc_t ec = rcp_wire_validate_header(b, len);
    if (ec != RCP_WIRE_OK) return ec;

    body_len = get_u32(&b[12]);
    if (body_len > RCP_WIRE_MAX_PAYLOAD) return RCP_WIRE_ERR_SHORT_FRAME;
    if (len < RCP_WIRE_HEADER_LEN + (size_t)body_len) return RCP_WIRE_ERR_SHORT_FRAME;

    out->zone    = (rcp_zone_t)b[4];
    out->healthy = (b[7] == 1u);
    out->seq     = get_u32(&b[8]);
    out->payload = rcp_bytes_dup(&b[RCP_WIRE_HEADER_LEN], body_len);
    return RCP_WIRE_OK;
}

//cfusa:req REQ-UDP-012
rcp_bytes_t rcp_wire_encode_control(uint8_t msg_type, rcp_zone_t zone)
{
    return alloc_frame(msg_type, zone, 0, 0, 0, NULL, 0);
}
