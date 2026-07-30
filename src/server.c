/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/server.h"

#include <stdlib.h>
#include <string.h>

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
void rcp_server_endpoint_init(rcp_server_endpoint_t *ep, bool ep_enable)
{
    ep->ep_enable = ep_enable;
    ep->queue     = NULL;
    ep->queue_len = 0;
    ep->queue_cap = 0;
}

//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
void rcp_server_endpoint_destroy(rcp_server_endpoint_t *ep)
{
    size_t i;

    for (i = 0; i < ep->queue_len; i++) {
        rcp_bytes_free(&ep->queue[i]);
    }
    free(ep->queue);
    ep->queue     = NULL;
    ep->queue_len = 0;
    ep->queue_cap = 0;
}

//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
bool rcp_server_endpoint_submit(rcp_server_endpoint_t *ep,
                                const uint8_t *frame, size_t frame_len)
{
    rcp_bytes_t *grown;

    if (ep->ep_enable) return true; /* caller must execute this now */

    if (ep->queue_len == ep->queue_cap) {
        size_t new_cap = (ep->queue_cap == 0) ? 4 : ep->queue_cap * 2;

        grown = (rcp_bytes_t *)realloc(ep->queue, new_cap * sizeof(*grown));
        if (!grown) return false; /* still "queued": nothing to execute now */
        ep->queue     = grown;
        ep->queue_cap = new_cap;
    }

    ep->queue[ep->queue_len] = rcp_bytes_dup(frame, frame_len);
    ep->queue_len++;
    return false;
}

//cfusa:req REQ-SRV-003
void rcp_server_endpoint_set_enable(rcp_server_endpoint_t *ep, bool enable)
{
    ep->ep_enable = enable;
}

//cfusa:req REQ-SRV-003
bool rcp_server_endpoint_drain_one(rcp_server_endpoint_t *ep, rcp_bytes_t *out_frame)
{
    size_t i;

    if (!ep->ep_enable) return false;
    if (ep->queue_len == 0) return false;

    *out_frame = ep->queue[0];
    for (i = 1; i < ep->queue_len; i++) {
        ep->queue[i - 1] = ep->queue[i];
    }
    ep->queue_len--;
    return true;
}

//cfusa:req REQ-SRV-001
size_t rcp_server_endpoint_queue_len(const rcp_server_endpoint_t *ep)
{
    return ep->queue_len;
}
