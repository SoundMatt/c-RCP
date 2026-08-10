/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-062
//cfusa:req REQ-RMAP-063
//cfusa:req REQ-RMAP-064
//cfusa:req REQ-RMAP-065
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
 * queue: REQ-RMAP-059's storage-and-capacity scope, REQ-RMAP-061's
 * per-message Max_AVTPDUsize ceiling, REQ-RMAP-062's fragmentation-
 * budget helper (all three the same "keep every transmitted AVTPDU
 * within Max_AVTPDUsize" concern), REQ-RMAP-063's flush_on_count packing
 * trigger, and REQ-RMAP-064's Flush_time trigger (the four together
 * being everything this queue's own contents and state can decide,
 * without needing a clock or a caller to have transmitted anything --
 * REQ-RMAP-064 included, since rcp_respqueue_should_flush_by_time()
 * below takes elapsed time as a caller-supplied input, exactly like
 * e2e.h's rcp_e2e_wd_evaluate(elapsed_since_last_kick_ms), rather than
 * reading a clock of its own). REQ-RMAP-065 (the empty-queue heartbeat
 * AVTPDU) is PARTIALLY modeled: the same should_flush_by_time() trigger
 * deliberately fires the same way whether q is empty or not, and
 * avtp.h's rcp_avtp_encode_ntscf() already accepts payload_len == 0 for
 * exactly this case (see rcp_respqueue_should_flush_by_time()'s own doc
 * comment for how a caller composes the two) -- but actually SCHEDULING
 * and TRANSMITTING that heartbeat on a real clock stays outside this
 * module's and this library's scope entirely (REQ-SRV-017 in server.h
 * already states this same boundary: "c-RCP is a protocol library, not
 * a scheduler; heartbeat emission is left to the integrator"). Also
 * still open: REQ-RMAP-061's own MTU-consistency-check and discovery-
 * exposure halves (a config-time check and a discovery.h change, neither
 * a per-message queue concern).
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

/* ── flush_on_count packing trigger ────────────────────────────────────────── */

/* REQ-RMAP-063 (TC18 §12.7.9, Table 24 relative address 0x0006, 16 bit,
 * R/W+, default 1, legal range 1..queue_size in 32-bit-word entries):
 * "Once a queue is filled with an amount of quadlets that is equal or
 * larger than given by flush_on_count, the transmission of one or
 * multiple AVTPDUs shall be initiated." True iff q currently holds at
 * least flush_on_count_octets octets -- flush_on_count_octets is the
 * register's own quadlet value already converted to octets by the
 * caller (flush_on_count x 4), the same "caller supplies already-
 * classified units" convention as capacity_octets/max_avtpdu_size_octets
 * above. flush_on_count_octets == 0 always returns true iff q is
 * non-empty (the register's own documented default, 1 quadlet, is the
 * smallest possible nonzero threshold -- "immediate transmission" --
 * and 0 is not itself a legal configured value per Table 24's own
 * range, so this is this function's fail-safe reading of an
 * unconfigured/zero threshold, not a real TC18 case). */
bool rcp_respqueue_should_flush(const rcp_respqueue_t *q, size_t flush_on_count_octets);

/* REQ-RMAP-063: "Hereby only as much as fitting to the MAX_AVTPDUsize
 * ACF_types will be included in a generated AVTPDU. Basically all
 * ACF_types including the one which was exceeding the Flush_on_Count
 * value will be transmitted, packed in a fitting number of AVTPDUs."
 * Reports how many of q's own FIFO-ordered entries, starting from the
 * front, fit together within max_avtpdu_size_octets octets total -- the
 * membership of ONE generated AVTPDU. A caller builds that AVTPDU by
 * calling rcp_respqueue_pop() exactly that many times (draining exactly
 * those entries), then calls this function again for the next AVTPDU,
 * repeating until rcp_respqueue_len() reaches 0 -- "packed in a fitting
 * number of AVTPDUs" for the whole queue.
 *
 * Always plans at least 1 entry when q is non-empty (an entry too large
 * for max_avtpdu_size_octets on its own could never have been pushed in
 * the first place -- rcp_respqueue_push()'s own REQ-RMAP-061
 * enforcement already guarantees every queued entry individually fits,
 * so this function never needs to represent "0 entries fit"). Returns 0
 * iff q is empty. max_avtpdu_size_octets == 0 (unbounded -- no
 * Max_AVTPDUsize configured) plans every remaining entry into one
 * AVTPDU, matching this module's own fail-open convention for an
 * unconfigured ceiling. */
size_t rcp_respqueue_plan_batch(const rcp_respqueue_t *q, size_t max_avtpdu_size_octets);

/* ── Flush_time trigger + empty-queue heartbeat composition ────────────────── */

/* REQ-RMAP-064 (TC18 §12.7.9, Table 24 relative address 0x0008, 16 bit,
 * R/W+, microseconds, default 0, 0 meaning "flush only by count"): "The
 * server shall initiate transmission from a response queue whenever the
 * time since that queue's last transmission is equal to or greater than
 * Flush_time, independently of flush_on_count." True iff flush_time_us
 * is nonzero and elapsed_since_last_transmit_us has reached it.
 * elapsed_since_last_transmit_us is a caller-tracked duration, not a
 * timestamp this function reads itself -- this module owns no clock
 * (see this header's own file comment), mirroring e2e.h's own
 * rcp_e2e_wd_evaluate(elapsed_since_last_kick_ms) convention exactly:
 * the caller measures elapsed time however it likes and hands in the
 * result.
 *
 * Deliberately independent of q->entries_len -- unlike
 * rcp_respqueue_should_flush() above (the flush_on_count trigger, which
 * has nothing meaningful to report on an empty queue and so treats one
 * as never due), this trigger fires the same way whether q is empty or
 * not, because REQ-RMAP-065 requires the server to still transmit -- an
 * empty heartbeat AVTPDU -- even when nothing is queued, so this
 * queue's own emptiness must never suppress the Flush_time trigger.
 *
 * What that empty AVTPDU actually is stays outside this module's own
 * no-ACF/AVTP-framing scope (see this header's file comment):
 * avtp.h's rcp_avtp_encode_ntscf() already accepts payload_len == 0
 * for exactly this case (its own doc comment: "payload may be NULL iff
 * payload_len == 0"), and NTSCF is "the *only* AVTPDU header format an
 * RC Server itself ever sends" (avtp.h). A caller composes the full
 * REQ-RMAP-064/065 behaviour as: once this function returns true,
 * either call rcp_respqueue_plan_batch()+drain+encode a real batch (a
 * nonzero plan_batch() result), or -- if plan_batch() reports 0, i.e.
 * q is empty -- call rcp_avtp_encode_ntscf(hdr, NULL, 0) directly for
 * the heartbeat. This function and rcp_avtp_encode_ntscf() together are
 * everything a caller needs to recognize the trigger and construct the
 * correct wire frame either way; actually SCHEDULING that composition
 * against a real clock (i.e. running it periodically and driving a real
 * transport) is a caller/integrator concern this library deliberately
 * does not take on, matching REQ-SRV-017's own already-accepted scope
 * boundary (server.h): "c-RCP is a protocol library, not a scheduler;
 * heartbeat emission is left to the integrator." REQ-RMAP-065 is
 * therefore closed only as far as this module's own layer goes --
 * catalogued "partial", not "implemented". */
bool rcp_respqueue_should_flush_by_time(uint64_t elapsed_since_last_transmit_us,
                                         uint64_t flush_time_us);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RESPQUEUE_H */
