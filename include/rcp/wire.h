//cfusa:req REQ-UDP-001
//cfusa:req REQ-UDP-002
//cfusa:req REQ-UDP-003
//cfusa:req REQ-UDP-004
//cfusa:req REQ-UDP-005
//cfusa:req REQ-UDP-006
//cfusa:req REQ-UDP-007
//cfusa:req REQ-UDP-008
//cfusa:req REQ-UDP-009
//cfusa:req REQ-UDP-010
//cfusa:req REQ-UDP-011
//cfusa:req REQ-UDP-012
/*
 * Binary frame codec shared by the UDP and (later) TLS transports.
 *
 * Frame layout (all multi-byte fields big-endian):
 *   [0]     Magic 'R'
 *   [1]     Magic 'C'
 *   [2]     Protocol version (0x01)
 *   [3]     Message type (RCP_WIRE_TYPE_*)
 *   [4]     Zone (uint8)
 *   [5:7]   CommandType or 0x0000 (uint16)
 *   [7]     Priority, ResponseStatus, or healthy flag (uint8)
 *   [8:12]  Command/response id or Status seq (uint32)
 *   [12:16] Payload length (uint32)
 *   [16:]   Payload (variable)
 *
 * Ownership: encode_* functions return a freshly heap-allocated frame
 * (caller frees with rcp_bytes_free). decode_* functions allocate a fresh
 * copy of the payload into *out — unlike rcp_command_t.payload's normal
 * borrowed-by-default convention elsewhere in rcp.h, a wire-decoded
 * command/response/status OWNS its payload and the caller must free it
 * (rcp_response_free/rcp_status_free, or rcp_bytes_free(&out->payload) for
 * a decoded command) once done.
 */
#ifndef RCP_WIRE_H
#define RCP_WIRE_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RCP_WIRE_MAGIC_0        ((uint8_t)0x52u) /* 'R' */
#define RCP_WIRE_MAGIC_1        ((uint8_t)0x43u) /* 'C' */
#define RCP_WIRE_PROTO_VERSION  ((uint8_t)0x01u)

#define RCP_WIRE_TYPE_COMMAND     ((uint8_t)0x01u)
#define RCP_WIRE_TYPE_RESPONSE    ((uint8_t)0x02u)
#define RCP_WIRE_TYPE_STATUS      ((uint8_t)0x03u)
#define RCP_WIRE_TYPE_SUBSCRIBE   ((uint8_t)0x04u)
#define RCP_WIRE_TYPE_UNSUBSCRIBE ((uint8_t)0x05u)

#define RCP_WIRE_HEADER_LEN   ((size_t)16u)
/* 65507 = largest UDP payload over IPv4 (65535 - 8-byte UDP header - 20-byte
 * minimum IPv4 header); a frame including its 16-byte RCP header must still
 * fit in one datagram. */
#define RCP_WIRE_MAX_PAYLOAD  ((size_t)(65507u - 16u))

typedef enum {
    RCP_WIRE_OK              = 0,
    RCP_WIRE_ERR_SHORT_FRAME = 1,
    RCP_WIRE_ERR_BAD_MAGIC   = 2,
    RCP_WIRE_ERR_BAD_VERSION = 3,
} rcp_wire_errc_t;

const char *rcp_wire_strerror(rcp_wire_errc_t e);

/* Validates the 16-byte header only (magic + version); does not check
 * declared payload length against the buffer's actual length (decode_*
 * does that separately, since it needs the payload-length field first). */
rcp_wire_errc_t rcp_wire_validate_header(const uint8_t *b, size_t len);

rcp_bytes_t rcp_wire_encode_command(const rcp_command_t *cmd);
rcp_wire_errc_t rcp_wire_decode_command(const uint8_t *b, size_t len, rcp_command_t *out);

rcp_bytes_t rcp_wire_encode_response(const rcp_response_t *resp);
rcp_wire_errc_t rcp_wire_decode_response(const uint8_t *b, size_t len, rcp_response_t *out);

rcp_bytes_t rcp_wire_encode_status(const rcp_status_t *st);
rcp_wire_errc_t rcp_wire_decode_status(const uint8_t *b, size_t len, rcp_status_t *out);

/* Header-only control frame (subscribe/unsubscribe): exactly
 * RCP_WIRE_HEADER_LEN bytes, zero payload. */
rcp_bytes_t rcp_wire_encode_control(uint8_t msg_type, rcp_zone_t zone);

#ifdef __cplusplus
}
#endif

#endif /* RCP_WIRE_H */
