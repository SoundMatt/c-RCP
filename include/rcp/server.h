/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
/*
 * server.h -- Per-endpoint ep_enable pre-load-then-drain request queue for
 * the TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 14, "RC
 * Server Lifecycle & Register Map", milestone 61).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59) and ACF message format (acf.h/acf.c,
 * milestone 60). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, or any
 * satellite package is touched here.
 *
 * The RC Server lifecycle state machine this module originally shipped
 * alongside (HW_UNCONFIGURED/HW_CONFIGURED/RCP_CONFIGURED and the
 * transition/plausibility/field-writability rules tied to it) moved to
 * lifecycle.h/lifecycle.c as part of the module-naming reconciliation
 * tracked at github.com/SoundMatt/c-RCP/issues/87: RELAY spec v1.14's
 * §13.7.2 registry names `lifecycle` for that concern specifically. This
 * remaining per-endpoint request queue -- an RC-Server-as-endpoint
 * concern, not itself the lifecycle state machine -- has no entry of its
 * own in that registry, so it keeps its original name. No queueing
 * behavior changed; this is a pure relocation. See lifecycle.h for the
 * state-machine surface this module used to also provide.
 */
#ifndef RCP_SERVER_H
#define RCP_SERVER_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

/* A single endpoint's enable flag and its pending-request queue. A disabled
 * endpoint still accepts (queues) incoming requests without executing them;
 * queued requests drain out, in FIFO order, once the endpoint is
 * re-enabled. This struct owns the byte copies held in its queue. */
typedef struct {
    bool         ep_enable;
    rcp_bytes_t *queue;
    size_t       queue_len;
    size_t       queue_cap;
} rcp_server_endpoint_t;

/* Initializes ep with an empty queue and the given initial ep_enable value. */
void rcp_server_endpoint_init(rcp_server_endpoint_t *ep, bool ep_enable);

/* Frees every queued request and ep's internal queue storage. Safe to call
 * on an endpoint with an empty queue. Does not free ep itself. */
void rcp_server_endpoint_destroy(rcp_server_endpoint_t *ep);

/* Submits one already-framed request of frame_len octets (frame may be NULL
 * iff frame_len == 0) to ep. If ep->ep_enable is true, this is a no-op on
 * the queue and the function returns true, meaning the caller must execute
 * the request itself right now. If ep->ep_enable is false, a copy of the
 * request is appended to ep's queue and the function returns false,
 * meaning the request has been queued rather than executed. Returns false
 * (still meaning "queued") without actually growing the queue if the
 * internal reallocation fails -- callers relying on eventual delivery under
 * allocation failure must check rcp_server_endpoint_queue_len() themselves. */
bool rcp_server_endpoint_submit(rcp_server_endpoint_t *ep,
                                const uint8_t *frame, size_t frame_len);

/* Sets ep->ep_enable. Toggling it does not itself execute or discard
 * anything queued; call rcp_server_endpoint_drain_one() afterward to pull
 * queued requests back out once re-enabled. */
void rcp_server_endpoint_set_enable(rcp_server_endpoint_t *ep, bool enable);

/* If ep->ep_enable is true and ep's queue is non-empty, dequeues the
 * oldest queued request into *out_frame (caller takes ownership; free with
 * rcp_bytes_free()) and returns true. Otherwise returns false and leaves
 * *out_frame untouched -- including while ep->ep_enable is false, so a
 * disabled endpoint's queue can never be silently drained out from under it. */
bool rcp_server_endpoint_drain_one(rcp_server_endpoint_t *ep, rcp_bytes_t *out_frame);

/* Number of requests currently queued (awaiting drain) on ep. */
size_t rcp_server_endpoint_queue_len(const rcp_server_endpoint_t *ep);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SERVER_H */
