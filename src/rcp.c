/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/rcp.h"
#include "rcp/alloc.h"

#include <stdlib.h>
#include <string.h>

/* ── relay error strings ───────────────────────────────────────────────────── */

//cfusa:req REQ-RELAY-014
const char *relay_strerror(relay_errc_t e)
{
    switch (e) {
    case RELAY_ERRC_CLOSED:            return "relay: closed";
    case RELAY_ERRC_NOT_CONNECTED:     return "relay: not connected";
    case RELAY_ERRC_TIMEOUT:           return "relay: timeout";
    case RELAY_ERRC_PAYLOAD_TOO_LARGE: return "relay: payload too large";
    default:                           return "relay: unknown";
    }
}

/* ── rcp error strings ─────────────────────────────────────────────────────── */

//cfusa:req REQ-ERR-012
const char *rcp_strerror(rcp_errc_t e)
{
    switch (e) {
    case RCP_OK:                 return "rcp: success";
    case RCP_ERR_CLOSED:         return "rcp: closed";
    case RCP_ERR_NOT_FOUND:      return "rcp: not found";
    case RCP_ERR_ALREADY_EXISTS: return "rcp: already exists";
    case RCP_ERR_TIMEOUT:        return "rcp: timeout";
    case RCP_ERR_BUSY:           return "rcp: busy";
    case RCP_ERR_NOT_SUPPORTED:  return "rcp: not supported (transport backend not compiled in)";
    case RCP_ERR_FORBIDDEN:      return "rcp: command forbidden by access policy";
    default:                     return "rcp: unknown error";
    }
}

/* ── Byte buffers ──────────────────────────────────────────────────────────── */

//cfusa:req REQ-CORE-001
rcp_bytes_t rcp_bytes_dup(const uint8_t *data, size_t len)
{
    rcp_bytes_t b;
    b.data = NULL;
    b.len  = 0;
    if (len == 0) return b;

    b.data = (uint8_t *)rcp_malloc(len);
    if (!b.data) return b; /* len stays 0: allocation failure yields an empty buffer */

    memcpy(b.data, data, len);
    b.len = len;
    return b;
}

//cfusa:req REQ-CORE-002
void rcp_bytes_free(rcp_bytes_t *b)
{
    if (!b) return;
    rcp_free(b->data);
    b->data = NULL;
    b->len  = 0;
}
