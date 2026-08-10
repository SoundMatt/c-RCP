/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-RMAP-059
/*
 * respqueue.h -- per-response/acknowledge-stream transmit queue for the
 * TC18 Remote Control Protocol wire layer (ROADMAP.md gap-closure Phase
 * 5d, "RMAP register-map exposure gaps", issue #200 Group 4).
 *
 * TC18 §12.7.9 Table 24 describes a per-response/ack-stream transmit
 * queue with a configured memory reservation (queue_size, in 32-bit
 * words) that responses and acknowledges from the RC Server's own
 * endpoints are collected into for aggregated transmission. Nothing in
 * this codebase modeled that queue before this module: server.h's own
 * per-endpoint ep_enable pre-load queue holds INBOUND requests awaiting
 * execution, a structurally different concept (see server.h's own file
 * header) -- there was no OUTBOUND queue of framed responses/
 * acknowledges awaiting transmission anywhere. This module is that
 * queue, and only that queue: REQ-RMAP-059's own storage-and-capacity
 * scope. The remaining Group 4 items this queue's own contents will
 * eventually need to satisfy -- Max_AVTPDUsize transmit enforcement and
 * fragmentation (REQ-RMAP-061/062), the flush_on_count packing trigger
 * (REQ-RMAP-063), the Flush_time timer and its empty-queue heartbeat
 * (REQ-RMAP-064/065) -- are deliberately NOT modeled here; this module
 * gives them a real queue to operate on, in a later batch, rather than
 * attempting all of Group 4 in one change.
 *
 * This module owns no register-map instance of its own (regmap.h's
 * rcp_regmap_response_queue_cfg_t.queue_size is the configured capacity
 * a caller reads and passes to rcp_respqueue_init() below -- the same
 * "caller supplies already-classified inputs" convention this codebase
 * uses throughout, e.g. power.h's network_available), no transport, and
 * no knowledge of ACF/AVTP framing -- push()/pop() operate on
 * caller-supplied byte buffers exactly as server.h's own
 * rcp_server_endpoint_submit()/_drain_one() do, matching that module's
 * own "own small pure primitives, caller composes" scope.
 *
 * ── FIFO order, byte-budget capacity ─────────────────────────────────────────
 *
 * Entries drain in the order they were pushed (server.h's own queue
 * convention). Capacity is enforced in OCTETS, not entry count --
 * TC18's own queue_size register is a memory reservation ("assigned
 * memory in 32bit words"), not a message-count limit, so
 * rcp_respqueue_push() refuses (and changes nothing) if accepting frame
 * would push the queue's own running octet total past the capacity
 * given at rcp_respqueue_init() -- capacity_octets == 0 means
 * unbounded (no reservation configured at all), the same fail-open
 * default server.h's own queue uses for a caller that never sets an
 * explicit cap.
 */
#ifndef RCP_RESPQUEUE_H
#define RCP_RESPQUEUE_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    rcp_bytes_t *entries;
    size_t       entries_len;
    size_t       entries_cap;    /* entries[] array growth capacity --
                                     an implementation detail, not the
                                     TC18 queue_size reservation below. */
    size_t       octets;         /* running total of entries[i].len,
                                     0 <= i < entries_len */
    size_t       capacity_octets; /* REQ-RMAP-059: the configured
                                      reservation, in octets (queue_size
                                      quadlets x 4); 0 means unbounded. */
} rcp_respqueue_t;

/* Initializes q empty, with the given octet capacity (regmap.h's
 * rcp_regmap_response_queue_cfg_t.queue_size, already converted to
 * octets by the caller -- this module does the quadlet/octet
 * conversion nowhere, matching rcp_acf_reg_write_len()'s own "the
 * caller supplies already-classified units" convention). 0 means
 * unbounded. */
void rcp_respqueue_init(rcp_respqueue_t *q, size_t capacity_octets);

/* Frees every queued entry and q's own internal array storage. Safe to
 * call on an empty queue. Does not free q itself. */
void rcp_respqueue_destroy(rcp_respqueue_t *q);

/* Appends a copy of frame[0..frame_len) to q's tail (frame may be NULL
 * iff frame_len == 0). Returns true and grows q->octets by frame_len on
 * success. Returns false, leaving q entirely unchanged, if
 * q->capacity_octets is nonzero and frame_len would push q->octets past
 * it, or if the internal copy/array-growth allocation fails. */
bool rcp_respqueue_push(rcp_respqueue_t *q, const uint8_t *frame, size_t frame_len);

/* Dequeues q's oldest entry (FIFO) into *out_frame (caller takes
 * ownership; free with rcp_bytes_free()) and shrinks q->octets by its
 * length. Returns true on success. Returns false, leaving *out_frame
 * untouched, iff q is empty. */
bool rcp_respqueue_pop(rcp_respqueue_t *q, rcp_bytes_t *out_frame);

/* Number of entries currently queued. */
size_t rcp_respqueue_len(const rcp_respqueue_t *q);

/* Sum of every currently-queued entry's own length, in octets -- the
 * same quantity rcp_respqueue_push() checks against q->capacity_octets. */
size_t rcp_respqueue_octets(const rcp_respqueue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RESPQUEUE_H */
