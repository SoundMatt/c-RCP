/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-062
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
 * queue: REQ-RMAP-059's storage-and-capacity scope, plus
 * REQ-RMAP-061's per-message Max_AVTPDUsize ceiling and REQ-RMAP-062's
 * fragmentation-budget helper, added together since both are the same
 * "keep every transmitted AVTPDU within Max_AVTPDUsize" concern, one
 * enforcing it (refuse an over-size push) and the other enabling a
 * caller to avoid the refusal (fragment first). The remaining Group 4
 * items this queue's own contents will eventually need to satisfy --
 * the flush_on_count packing trigger (REQ-RMAP-063), the Flush_time
 * timer and its empty-queue heartbeat (REQ-RMAP-064/065), and
 * REQ-RMAP-061's own MTU-consistency-check and discovery-exposure
 * halves (a config-time check and a discovery.h change, neither a
 * per-message queue concern) -- are deliberately NOT modeled here.
 *
 * This module owns no register-map instance of its own (regmap.h's
 * rcp_regmap_response_queue_cfg_t.queue_size/max_avtpdu_size are the
 * configured values a caller reads and passes to rcp_respqueue_init()
 * below -- the same "caller supplies already-classified inputs"
 * convention this codebase uses throughout, e.g. power.h's
 * network_available), no transport, and no knowledge of ACF/AVTP
 * framing -- push()/pop() operate on caller-supplied byte buffers
 * exactly as server.h's own rcp_server_endpoint_submit()/_drain_one()
 * do, matching that module's own "own small pure primitives, caller
 * composes" scope.
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
 *
 * ── Per-message Max_AVTPDUsize ceiling ────────────────────────────────────────
 *
 * REQ-RMAP-061 (TC18 §12.7.9): "The maximum length of an AVTPDU sent by
 * the RC Server shall be configurable... The RC Server shall not
 * transmit an AVTPDU longer than the configured Max_AVTPDUsize." This
 * is a DIFFERENT ceiling than capacity_octets (the queue's own
 * aggregate reservation, REQ-RMAP-059) -- checked independently,
 * per-message, on every rcp_respqueue_push() call: a frame whose own
 * length alone exceeds max_avtpdu_size_octets is refused (queue
 * unchanged), never silently truncated or split by this module itself
 * (which has no ACF/framing knowledge of its own to split with -- see
 * above). A caller with a payload too large for one AVTPDU fragments it
 * FIRST (rcp_respqueue_max_fragment_payload() below, REQ-RMAP-062) and
 * pushes each resulting fragment as its own, individually-bounded
 * push() call. max_avtpdu_size_octets == 0 means unbounded, the same
 * fail-open default as capacity_octets.
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
    size_t       max_avtpdu_size_octets; /* REQ-RMAP-061: the configured
                                             per-message ceiling, in
                                             octets (Max_AVTPDUsize
                                             quadlets x 4); 0 means
                                             unbounded. */
} rcp_respqueue_t;

/* Initializes q empty, with the given octet capacity and per-message
 * ceiling (regmap.h's rcp_regmap_response_queue_cfg_t.queue_size and
 * .max_avtpdu_size, already converted from quadlets to octets by the
 * caller -- this module does that conversion nowhere, matching
 * rcp_acf_reg_write_len()'s own "the caller supplies already-classified
 * units" convention). 0 for either means unbounded. */
void rcp_respqueue_init(rcp_respqueue_t *q, size_t capacity_octets,
                         size_t max_avtpdu_size_octets);

/* Frees every queued entry and q's own internal array storage. Safe to
 * call on an empty queue. Does not free q itself. */
void rcp_respqueue_destroy(rcp_respqueue_t *q);

/* Appends a copy of frame[0..frame_len) to q's tail (frame may be NULL
 * iff frame_len == 0). Returns true and grows q->octets by frame_len on
 * success. Returns false, leaving q entirely unchanged, if:
 *   - q->max_avtpdu_size_octets is nonzero and frame_len exceeds it
 *     (REQ-RMAP-061 -- checked first, independently of capacity_octets);
 *   - q->capacity_octets is nonzero and frame_len would push q->octets
 *     past it (REQ-RMAP-059); or
 *   - the internal copy/array-growth allocation fails. */
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

/* REQ-RMAP-062 (TC18 §12.7.9): "In case an AVTPDU containing a single
 * ACF_type would exceed the Max_AVTPDUsize, fragmentation... by the
 * ms-bit will be performed." Computes the max_fragment_payload a
 * caller hands to fragment.h's rcp_fragment_plan_count()/_plan() so
 * that every resulting fragment's own encoded AVTPDU (fixed ACF header
 * + fragment payload + trailing pad) stays within
 * max_avtpdu_size_octets -- header_len is the caller's own already-
 * known ACF fixed-header length for the message kind being sent
 * (acf.h's RCP_ACF_ABB_HEADER_LEN or RCP_ACF_GBB_HEADER_LEN; this
 * module has no ACF knowledge of its own, matching the rest of this
 * header's scope). Conservatively reserves the worst case 3 octets of
 * trailing pad (rcp_acf_pad_len()'s own maximum) so the result is safe
 * regardless of the actual pad a given fragment ends up needing, at
 * the cost of a few fewer payload octets per fragment than the
 * theoretical maximum.
 *
 * Returns 0 (fragment.h's own RCP_FRAGMENT_ERR_DISABLED convention for
 * max_fragment_payload == 0: "fragmentation disabled") if
 * max_avtpdu_size_octets == 0 (unbounded -- no ceiling configured, so
 * there is nothing to derive a budget from) or if header_len + 3
 * already meets or exceeds max_avtpdu_size_octets (no payload budget
 * remains at all once the fixed header and worst-case pad are
 * reserved). */
size_t rcp_respqueue_max_fragment_payload(size_t max_avtpdu_size_octets, size_t header_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RESPQUEUE_H */
