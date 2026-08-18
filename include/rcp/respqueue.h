/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-062
//cfusa:req REQ-RMAP-063
//cfusa:req REQ-RMAP-064
//cfusa:req REQ-RMAP-065
//cfusa:req REQ-RMAP-085
/*
 * respqueue.h -- per-response/acknowledge-stream transmit queue for the
 * TC18 Remote Control Protocol wire layer (ROADMAP.md gap-closure Phase
 * 5d, "RMAP register-map exposure gaps", issue #200 Group 4).
 *
 * TC18 §12.7.9 Table 27 describes a per-response/ack-stream transmit
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
 * a scheduler; heartbeat emission is left to the integrator").
 * REQ-RMAP-061's own remaining "MTU-consistency" half is now closed too
 * (rcp_respqueue_max_avtpdu_size_within_mtu() below, a config-time check
 * a caller uses before ever calling rcp_respqueue_init(), not a
 * per-message queue concern). REQ-RMAP-061 still stays `partial`
 * overall, for a reason that has nothing to do with this module: TC18
 * §12.7.9's own Table 27 (response/ack queue config, including
 * Max_AVTPDUsize) is a separate table pointed to by Table 20's own
 * svr_response_stream_cfg_ptr register (REQ-RMAP-034, already
 * implemented) -- exactly the same genuine, unresolved ACF_ABB
 * addressing question REQ-RMAP-040/041 (HW_config) and REQ-RMAP-052/054
 * (EP_ID_config) already document applies here too. An earlier revision
 * of this requirement's own catalog text described the gap as
 * "exposing the value in the discovery general-register slice" -- that
 * framing predates this codebase's own later discovery that Table 27
 * was never part of the 14-octet discovery slice at all (it is a
 * separate, pointer-addressed table like the other two), and has been
 * corrected in `.fusa-reqs.json` accordingly.
 *
 * ── queue_size (capacity_octets) overflow: evict-lowest-sequence_num, not
 *    reject-newest ────────────────────────────────────────────────────────
 *
 * GitHub #423 (REQ-RMAP-059/061), CORRECTED by GitHub #446: TC18 §12.9.4
 * (response queue) and §12.9.5 (acknowledge queue) both give the SAME
 * additional, mandatory rule: "In case a [response/acknowledge] queue is
 * completely full and not yet sent while the next [response/acknowledge]
 * is delivered by an endpoint, then the AVTPDU with the lowest
 * sequence_num has to be removed from the queue to make space for the
 * new [response/acknowledge]. The overflow bit in the respective
 * [header] shall be set." TC18 §12.7.9 defines exactly one "queue is
 * completely full" concept for this queue: the queue_size memory
 * reservation (REQ-RMAP-059) this module already models as
 * capacity_octets -- there is no TC18-defined entry-count bound. The
 * #423 fix (GitHub #423) wired this eviction rule to a NEW,
 * spec-uncited RCP_RESPQUEUE_MAX_ENTRIES slot-count bound instead, and
 * left the pre-existing capacity_octets byte-budget path doing plain
 * reject-and-leave-unchanged -- so in any realistically-configured
 * server where capacity_octets is the binding constraint (the ordinary
 * case), the TC18-mandated eviction/overflow behavior never fired. This
 * fix (GitHub #446) moves the trigger to where TC18 actually puts it:
 * once accepting an incoming frame would exhaust q->capacity_octets
 * (REQ-RMAP-059's own queue_size bound), rcp_respqueue_push()/
 * _push_seq() now evict queued entries in ascending sequence_num order
 * -- a genuine, literal numeric minimum over q->entries_seq[] each
 * iteration, not merely the FIFO-oldest -- REPEATEDLY, as many times as
 * needed to free enough BYTES for the incoming frame (a single eviction
 * frees exactly its own evicted entry's own byte size, which may be
 * smaller than the incoming frame -- one eviction is not always
 * enough), and set q->overflow, exactly as TC18 mandates. A frame whose
 * own length exceeds q->capacity_octets outright is still refused
 * (queue unchanged, no eviction attempted) -- no amount of eviction
 * could ever make room for it, even against a fully empty queue.
 *
 * RCP_RESPQUEUE_MAX_ENTRIES is a UNIVERSAL slot-count bound (issue #521,
 * ASIL-D-oriented no-dynamic-allocation push): entries[]/entries_seq[]
 * are fixed-capacity arrays embedded directly in rcp_respqueue_t, sized
 * to this constant, so q itself now carries no heap allocation of its
 * own regardless of how capacity_octets is configured (only each
 * queued frame's own rcp_bytes_t payload copy remains a per-entry heap
 * allocation -- see this header's own file comment for why that
 * particular allocation is out of this fix's scope). Consequently the
 * eviction loop now triggers on EITHER condition: q->capacity_octets != 0
 * and accepting frame_len would exceed the remaining byte budget, OR
 * q->entries_len has reached RCP_RESPQUEUE_MAX_ENTRIES outright -- not
 * only as a capacity_octets == 0 fallback as an earlier revision of this
 * header stated. A server configured with a generous capacity_octets
 * (bytes) but a workload of many small messages will therefore find the
 * slot count a real, additional binding constraint even while byte
 * budget remains -- an intentional trade accepted in exchange for
 * eliminating this module's own realloc()-based array growth, matching
 * the "realistic bound" convention this codebase already uses for every
 * other repeated-row table (RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES,
 * RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES,
 * RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES, RCP_REGMAP_EP_ID_MAP_MAX_
 * ENTRIES, all 64). q->entries_len can never exceed
 * RCP_RESPQUEUE_MAX_ENTRIES under any configuration, by construction.
 *
 * rcp_respqueue_push() itself keeps its original signature and assigns
 * sequence_num automatically, from an internal wrapping uint8_t
 * counter, so no existing caller needs to change; rcp_respqueue_push_seq()
 * is the same operation with an explicitly-supplied sequence_num, for a
 * caller that already tracks its own (e.g. one that wants
 * queue-internal sequence_num to agree with the eventual AVTPDU
 * header's own sequence_num field). Reading and clearing the resulting
 * overflow bit (rcp_respqueue_overflow()/_clear_overflow() below)
 * follows this module's own established "primitive is real and
 * directly tested, live AVTPDU-header population is a caller/
 * integrator concern" disposition -- the same one
 * rcp_e2e_stream_status_rx_blocked() (e2e.h) and REQ-RMAP-065's own
 * empty-heartbeat composition already use, and for the identical
 * reason stated in this header's own file comment above: this module
 * has no ACF/AVTP framing knowledge of its own to populate a header
 * field with.
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
 * convention). Capacity is primarily enforced in OCTETS, not entry
 * count -- TC18's own queue_size register is a memory reservation
 * ("assigned memory in 32bit words"), not a message-count limit. As of
 * GitHub #446, rcp_respqueue_push() no longer simply refuses once
 * accepting a frame would push the queue's own running octet total past
 * capacity_octets: it first evicts lowest-sequence_num entries (TC18
 * §12.9.4/§12.9.5, see above) to try to free enough room, and only
 * refuses (queue entirely unchanged) if frame_len exceeds
 * capacity_octets outright -- unfixable by any amount of eviction.
 * capacity_octets == 0 means unbounded byte budget (no reservation
 * configured at all), the same fail-open default server.h's own queue
 * uses for a caller that never sets an explicit cap. As of issue #521,
 * RCP_RESPQUEUE_MAX_ENTRIES is ALSO always enforced as a hard slot-count
 * ceiling regardless of capacity_octets (see above) -- q's own
 * entries[]/entries_seq[] storage is fixed-capacity, not realloc()-grown,
 * so this is a real structural bound, not merely a fallback for the
 * unbounded-byte-budget case.
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

/* TC18 §12.9.4/§12.9.5 (REQ-RMAP-085, split 2026-08-18 off REQ-RMAP-059/061,
 * c-RCP-18-tracker issue #533; originally GitHub #423, corrected by
 * GitHub #446, made universal by issue #521, the ASIL-D-oriented
 * no-dynamic-allocation push): the fixed capacity of entries[]/
 * entries_seq[] below, and so a hard ceiling on q->entries_len under
 * EVERY configuration, not only capacity_octets == 0. Whenever
 * capacity_octets is nonzero, it remains TC18's own "queue is
 * completely full" condition (§12.7.9's queue_size register) and the
 * eviction loop triggers against it first -- but once q->entries_len
 * also reaches this slot-count bound, eviction triggers on that alone
 * too, even with byte budget still available. This is a deliberate
 * trade, made to give q's own storage a fixed compile-time footprint
 * (no realloc()-grown array): a server configured with a generous
 * capacity_octets but a workload of many small messages will find the
 * slot count a real, additional binding constraint. Matches the
 * "realistic bound" convention this codebase already uses for every
 * other repeated-row table (RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES,
 * RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES,
 * RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES,
 * RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES, all regmap.h, all 64). */
#define RCP_RESPQUEUE_MAX_ENTRIES ((size_t)64u)

typedef struct {
    rcp_bytes_t entries[RCP_RESPQUEUE_MAX_ENTRIES]; /* Fixed-capacity
                                     (issue #521): each entries[i].data
                                     is still an individual rcp_bytes_dup()
                                     heap allocation sized to that one
                                     frame's own frame_len -- the array
                                     SLOTS are static, the per-entry
                                     PAYLOAD bytes are not (see this
                                     header's own file comment for why:
                                     frame_len is caller/message-
                                     dependent, not a compile-time
                                     protocol constant). */
    uint8_t     entries_seq[RCP_RESPQUEUE_MAX_ENTRIES]; /* entries_seq[i]
                                     is the sequence_num
                                     rcp_respqueue_push()/_push_seq()
                                     assigned entries[i] at push time,
                                     0 <= i < entries_len -- the value
                                     rcp_respqueue_push()/_push_seq()
                                     compares to find the lowest-
                                     sequence_num entry to evict once
                                     capacity_octets would be exhausted,
                                     or once RCP_RESPQUEUE_MAX_ENTRIES
                                     itself is reached (TC18
                                     §12.9.4/§12.9.5, GitHub #423/#446,
                                     issue #521). */
    size_t       entries_len;    /* Always <= RCP_RESPQUEUE_MAX_ENTRIES,
                                     by construction. */
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
    uint8_t      next_sequence_num; /* auto-increment counter
                                        rcp_respqueue_push() assigns
                                        from and advances on every
                                        successful push; wraps mod 256,
                                        matching AVTP's own 8-bit
                                        sequence_num field width
                                        (avtp.h). Unused by
                                        rcp_respqueue_push_seq(), which
                                        takes an explicit sequence_num
                                        instead. */
    bool         overflow;        /* TC18 §12.9.4/§12.9.5's overflow
                                      bit: latched true the moment an
                                      eviction first occurs, and stays
                                      true until a caller calls
                                      rcp_respqueue_clear_overflow()
                                      (read with rcp_respqueue_overflow()
                                      below) -- matching e2e.h's own
                                      note_*()/reset_*() latch
                                      convention (rcp_e2e_stream_status_t)
                                      rather than auto-clearing on the
                                      next successful non-evicting push. */
} rcp_respqueue_t;

/* Initializes q empty, with the given octet capacity and per-message
 * ceiling (regmap.h's rcp_regmap_response_queue_cfg_t.queue_size and
 * .max_avtpdu_size, already converted from quadlets to octets by the
 * caller -- this module does that conversion nowhere, matching
 * rcp_acf_reg_write_len()'s own "the caller supplies already-classified
 * units" convention). 0 for either means unbounded. */
void rcp_respqueue_init(rcp_respqueue_t *q, size_t capacity_octets,
                         size_t max_avtpdu_size_octets);

/* Frees every queued entry's own per-frame payload allocation (issue
 * #521: entries[]/entries_seq[] themselves are fixed-capacity arrays
 * embedded in *q, not heap storage -- there is no array to free any
 * more, only each entries[i].data). Safe to call on an empty queue.
 * Does not free q itself. */
void rcp_respqueue_destroy(rcp_respqueue_t *q);

/* Appends a copy of frame[0..frame_len) to q's tail (frame may be NULL
 * iff frame_len == 0), tagged with q->next_sequence_num (then advances
 * that counter, wrapping mod 256). Identical in every other respect to
 * rcp_respqueue_push_seq() below -- see its doc comment for the full
 * REQ-RMAP-059/061 byte-budget-and-eviction behavior and the
 * RCP_RESPQUEUE_MAX_ENTRIES slot-count ceiling.
 * Existing callers that never cared about sequence_num keep working
 * unchanged; a caller that wants explicit control over sequence_num
 * (e.g. to keep it in agreement with an eventual AVTPDU header's own
 * sequence_num field) uses rcp_respqueue_push_seq() directly instead. */
bool rcp_respqueue_push(rcp_respqueue_t *q, const uint8_t *frame, size_t frame_len);

/* Same as rcp_respqueue_push() above, except sequence_num is supplied
 * by the caller rather than assigned from q's own internal counter (and
 * q->next_sequence_num is left untouched). Returns true and grows
 * q->octets by frame_len on success. Returns false, leaving q entirely
 * unchanged, if:
 *   - q->max_avtpdu_size_octets is nonzero and frame_len exceeds it
 *     (REQ-RMAP-061 -- checked first, independently of capacity_octets;
 *     UNCHANGED by this fix); or
 *   - q->capacity_octets is nonzero and frame_len exceeds it outright
 *     (no amount of eviction could ever make room for a frame larger
 *     than the entire configured queue_size budget, even against a
 *     fully empty queue); or
 *   - the internal frame-copy allocation fails.
 *
 * Otherwise, once the checks above pass, TC18 §12.9.4/§12.9.5
 * (REQ-RMAP-059/061, GitHub #423, corrected by GitHub #446, made
 * universal by issue #521) applies: entries are evicted in ascending
 * sequence_num order -- always the queued entry with the
 * currently-lowest sequence_num (a literal numeric minimum over
 * q->entries_seq[], evaluated by linear scan), never merely the
 * FIFO-oldest -- REPEATEDLY, once for each of the following that still
 * holds, until neither does:
 *
 *   - q->capacity_octets is nonzero (a real queue_size budget is
 *     configured -- TC18's own, and the only, "queue is completely
 *     full" condition, §12.7.9) and accepting frame_len would still
 *     exceed the remaining budget (a single eviction frees only its own
 *     evicted entry's own byte length, which may be smaller than
 *     frame_len -- one eviction is not always enough); or
 *
 *   - q->entries_len has reached RCP_RESPQUEUE_MAX_ENTRIES, q's own
 *     fixed-capacity slot-count ceiling (issue #521) -- enforced
 *     unconditionally, independently of capacity_octets, since
 *     entries[]/entries_seq[] cannot hold more than that many slots by
 *     construction.
 *
 * Since frame_len was already confirmed not to exceed capacity_octets
 * outright, and RCP_RESPQUEUE_MAX_ENTRIES >= 1, this always terminates
 * successfully. Either way, every eviction latches q->overflow true
 * (rcp_respqueue_overflow()/_clear_overflow() below). This is NOT
 * simply "evict the FIFO-oldest entry" -- sequence_num-order and
 * FIFO-order coincide only until sequence_num wraps (256 values,
 * uint8_t), matching this function's own contract exactly: the entry
 * evicted is always whichever one currently holds the numerically
 * lowest sequence_num, full stop. */
bool rcp_respqueue_push_seq(rcp_respqueue_t *q, const uint8_t *frame, size_t frame_len,
                             uint8_t sequence_num);

/* TC18 §12.9.4/§12.9.5's overflow bit (REQ-RMAP-085, split 2026-08-18 off
 * REQ-RMAP-059/061, c-RCP-18-tracker issue #533; originally GitHub #423):
 * true iff rcp_respqueue_push()/_push_seq() has evicted at least one
 * entry since q was last rcp_respqueue_init()'d or last had
 * rcp_respqueue_clear_overflow() called. Matches this module's own
 * "primitive is real and directly tested, live AVTPDU-header population
 * is a caller/integrator concern" disposition (see this header's own
 * file comment) -- a caller reflects this bit into whichever outgoing
 * AVTPDU/ACF header field TC18 assigns it to, the same way
 * rcp_e2e_stream_status_rx_blocked() (e2e.h) is read into the
 * rx_stream_status wire bit. */
bool rcp_respqueue_overflow(const rcp_respqueue_t *q);

/* Clears q->overflow back to false. Safe to call whether or not it was
 * ever set. */
void rcp_respqueue_clear_overflow(rcp_respqueue_t *q);

/* REQ-RMAP-061's own remaining "MTU-consistency" half (TC18 §12.7.9,
 * TC18.txt L3010-3011: "The Max_AVTPDUsize shall always be configured
 * such that the final network frame does not exceed the maximum
 * transmit unit size of the network"). This is a config-time check, not
 * a per-message queue concern -- rcp_respqueue_push() (above) already
 * enforces the transmit-bounding half against a fixed, already-accepted
 * max_avtpdu_size_octets; this function is what a caller uses BEFORE
 * ever calling rcp_respqueue_init(), to decide whether a candidate
 * Max_AVTPDUsize value is even acceptable for its own network in the
 * first place.
 *
 * TC18 defines no fixed MTU value of its own (network deployment is out
 * of its scope) and does not say whether "the final network frame"
 * means max_avtpdu_size_octets directly or that value plus some further
 * header overhead this codebase has no citation for -- so, matching
 * rcp_respqueue_max_fragment_payload()'s own "caller supplies already-
 * classified units" convention, mtu_budget_octets is the caller's own
 * already-adjusted ceiling (whatever it determines "how many
 * Max_AVTPDUsize octets fit under this deployment's real MTU" to be,
 * netted of any header overhead its own network stack adds) -- this
 * function does not itself add or assume any such overhead.
 *
 * Returns true iff max_avtpdu_size_octets does not exceed
 * mtu_budget_octets. max_avtpdu_size_octets == 0 (unbounded, matching
 * rcp_respqueue_init()'s own convention) is never within budget for a
 * nonzero mtu_budget_octets -- an unbounded ceiling cannot be MTU-safe
 * by definition -- and is vacuously true only when mtu_budget_octets is
 * also 0 (both "no ceiling configured" on both sides, a degenerate
 * caller error this function does not itself further diagnose). */
bool rcp_respqueue_max_avtpdu_size_within_mtu(size_t max_avtpdu_size_octets,
                                               size_t mtu_budget_octets);

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

/* REQ-RMAP-063 (TC18 §12.7.9, Table 27 relative address 0x0006, 16 bit,
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
 * and 0 is not itself a legal configured value per Table 27's own
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

/* REQ-RMAP-064 (TC18 §12.7.9, Table 27 relative address 0x0008, 16 bit,
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
