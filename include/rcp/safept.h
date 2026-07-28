//cfusa:req REQ-SAFEPT-001
//cfusa:req REQ-SAFEPT-002
//cfusa:req REQ-SAFEPT-003
//cfusa:req REQ-SAFEPT-004
//cfusa:req REQ-SAFEPT-005
//cfusa:req REQ-SAFEPT-006
//cfusa:req REQ-SAFEPT-007
//cfusa:req REQ-SAFEPT-008
//cfusa:req REQ-SAFEPT-009
//cfusa:req REQ-SAFEPT-010
//cfusa:req REQ-SAFEPT-011
//cfusa:req REQ-SAFEPT-012
//cfusa:req REQ-SAFEPT-013
//cfusa:req REQ-SAFEPT-014
//cfusa:req REQ-SAFEPT-015
//cfusa:req REQ-SAFEPT-016
//cfusa:req REQ-SAFEPT-017
//cfusa:req REQ-SAFEPT-018
//cfusa:req REQ-SAFEPT-019
//cfusa:req REQ-SAFEPT-020
//cfusa:req REQ-SAFEPT-021
//cfusa:req REQ-SAFEPT-022
//cfusa:req REQ-SAFEPT-023
//cfusa:req REQ-SAFEPT-024
//cfusa:req REQ-SAFEPT-025
//cfusa:req REQ-SAFEPT-026
//cfusa:req REQ-SAFEPT-027
/*
 * safept.h -- CRC32 safe points and safety-request execution gating for the
 * TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 18, "E2E
 * Protection (Safe Points)", milestone 70).
 *
 * This is new, additive protocol-core surface. It is this milestone's
 * direct, explicit replacement for e2e.h/e2e.c's ad-hoc CRC-16/CCITT-FALSE
 * mechanism (see e2e.h's own file header): this module implements a fresh
 * CRC32, not a reuse of e2e.c's CRC-16 table or sequence-counter/replay-
 * guard machinery, which cover a different (non-spec) threat model.
 * e2e.h/e2e.c themselves are REPLACE-dispositioned per ROADMAP.md's
 * Satellite Package Disposition table, but their actual removal is
 * deliberately deferred to the satellite-rework milestone (v0.79.0) named
 * there -- this module lands alongside e2e.c, not in place of it, exactly
 * mirroring milestone 69's own precedent of scheduler.h landing as new
 * protocol-core surface next to prioqueue.c while that module's own
 * removal stayed a separate, later milestone.
 *
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * or any ep_* endpoint module is touched here. This module depends on
 * sequencer.h (milestone 68) only, the same "first-class supporting
 * primitive" every compound/compound-wait/triggered-adjacent module
 * already depends on directly (compound.h and triggered.h both include it)
 * -- reusing it here, for reading whether a configured safe-state
 * sequencer has reached its target state, is that same precedent, not a
 * new coupling.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced,
 * except where a constant (the CRC32 parameter set below) is intrinsically
 * a bare number with no expressive alternative.
 *
 * ── The CRC32 parameter set ─────────────────────────────────────────────────
 *
 * rcp_safept_crc32() implements one fixed 32-bit CRC parameterization:
 * polynomial 0xF4ACFB13, width 32, init 0xFFFFFFFF, final XOR 0xFFFFFFFF,
 * both input and output reflection enabled. This matches the published
 * CRC-32/AUTOSAR catalog parameterization exactly (check value 0x1697D06A
 * over the ASCII bytes "123456789"), which this module's own test suite
 * verifies as a known-answer test -- not because this module targets
 * AUTOSAR specifically, but because the two parameter sets happen to
 * coincide, giving this implementation an independently-published
 * reference vector to validate against beyond round-tripping its own
 * wrap()/unwrap() pair.
 *
 * ── Coverage span and the length-accounting pre-adjustment ─────────────────
 *
 * The CRC spans, in order: the 8-byte StreamID this request stream is
 * addressed by (avtp.h's own addressing model; pass 0 for the all-zero
 * stand-in an NTSCF-framed message uses in place of a real StreamID),
 * the 8-byte avtp_timestamp (again 0 under NTSCF framing, which carries
 * no timestamp of its own), and finally the complete ACF header-and-
 * payload region exactly as acf.c's rcp_acf_encode_abb()/_encode_gbb()
 * would produce it. rcp_safept_wrap() appends a RCP_SAFEPT_CRC_LEN-byte
 * (4-octet / one-quadlet) big-endian trailer of that CRC value; because
 * that trailer becomes part of what the ACF header's own acf_msg_length
 * field (acf.h) must declare, acf_msg_length has to be computed against
 * the *safety-protected* payload length, not the original one --
 * rcp_safept_length_with_crc() is the pure arithmetic expression of that
 * pre-adjustment. This module does not call into acf.c itself (matching
 * every request-kind module's own "own small pure helpers, don't reach
 * into sibling modules" layering discipline): a caller assembles the
 * safety-protected payload (original payload + placeholder CRC bytes),
 * passes its length via rcp_safept_length_with_crc() to whichever of
 * acf.c's encoders it is using, and separately calls rcp_safept_wrap() to
 * produce the actual trailer bytes, or drives both ends through
 * rcp_safept_wrap()/rcp_safept_unwrap() directly against an
 * already-assembled ACF header-and-payload region.
 *
 * On a CRC mismatch, rcp_safept_unwrap() returns
 * RCP_SAFEPT_ERR_CRC_MISMATCH -- this module's own spelling of CRC_ERROR
 * -- and the caller must skip executing the request the frame carried,
 * per this milestone's roadmap scope.
 *
 * ── Fragmentation/CRC interaction: modeled now, activated at Phase 20 ──────
 *
 * acf.h's read_size_or_segment_num field already exists but is round-
 * tripped only (fragmentation itself is Phase 20's job). This milestone's
 * roadmap scope calls for the fragmentation/CRC interaction rule to be
 * modeled now regardless: only the last fragment of a multi-segment
 * message carries a CRC (computed across the fully reassembled payload),
 * and the length-accounting pre-adjustment applies only to that final
 * segment. rcp_safept_fragment_carries_crc() is the pure, directly-
 * testable expression of that rule, ready for Phase 20 to call once real
 * segment_num-driven reassembly exists; it has no fragmentation state of
 * its own to model in the meantime.
 *
 * ── Safety-request execution gating ─────────────────────────────────────────
 *
 * compound.h's RCP_REQUEST_TYPE_COMPOUND_SAFETY/_COMPOUND_WAIT_SAFETY
 * (0x8F/0x8B) and triggered.h's RCP_REQUEST_TYPE_TRIGGERED_SAFETY (0x8E)
 * already exist and round-trip through their own modules' encode/decode
 * functions as of milestones 68/69 -- both of those modules' own file
 * headers explicitly named gating their execution on the endpoint's
 * configured safe state as this milestone's job, not theirs.
 * rcp_safept_is_safety_request() is this module's own, self-contained
 * MSB(0x80) test (mirroring, but not depending on, compound.c's identical
 * rcp_request_type_is_safety() -- the same "every request-kind module
 * owns its own small pure helpers" precedent triggered.h already set by
 * not including compound.h itself). rcp_safept_request_may_execute() is
 * the actual gate: a safety-tagged request_type executes only once the
 * endpoint has reached its configured safe state; a non-safety-tagged
 * request_type is unaffected by this particular rule (any other
 * admission check -- e.g. scheduler.h's priority ordering -- is a
 * separate, unduplicated concern).
 *
 * ── The configured safe state itself ────────────────────────────────────────
 *
 * regmap.h's rx_safety_measure selects one of two safe-state measures:
 * RCP_SAFEPT_MEASURE_FORCE_HIGH_IMPEDANCE (0), an instantaneous action
 * with no state of its own to poll -- commanding it *is* reaching the
 * safe state, so rcp_safept_endpoint_in_safe_state() always reports true
 * for it -- and RCP_SAFEPT_MEASURE_SEQUENCER (1), which instead polls
 * regmap.h's rx_safestate_sequencer/rx_safe_sequencer_state against a
 * live sequencer.h table via rcp_sequencer_get_state(). An unrecognized
 * rx_safety_measure byte, or a RCP_SAFEPT_MEASURE_SEQUENCER configuration
 * whose rx_safestate_sequencer index isn't valid in the table supplied,
 * both fail *closed*: rcp_safept_endpoint_in_safe_state() reports false,
 * so a safety-tagged request stays blocked rather than executing against
 * an unverifiable safe-state claim. This is an explicit engineering
 * choice by this implementation (fail toward blocking, not toward
 * permitting), not a value taken from the specification.
 *
 * ── The watchdog-purge-vs-safety-survive rule ───────────────────────────────
 *
 * On a watchdog-timeout event (rcp_safept_wd_evaluate() reporting
 * enter_safe_state == true), every currently-queued request whose
 * request_type is not safety-tagged is purged from the endpoint's queue;
 * only the safety-tagged variants survive, and it is those surviving
 * instances -- still subject to rcp_safept_request_may_execute()'s own
 * gate -- that actually drive the endpoint to/through its configured safe
 * state. This is the primary safety mechanism this milestone's roadmap
 * scope calls for, not an edge case: rcp_safept_watchdog_purge_should_keep()
 * (single entry) and rcp_safept_watchdog_purge_classify() (a whole
 * caller-owned array) are its two pure, directly-testable expressions.
 * Neither function reaches into server.h's rcp_server_endpoint_t queue
 * directly -- they operate on caller-supplied request_type bytes, the
 * same "operate on caller-owned data, not a concrete queue type" pattern
 * scheduler.h's own rcp_sched_compare() already established, so this
 * module stays self-contained. Wiring these against an actual
 * rcp_server_endpoint_t queue (server.h) is left to whichever future
 * phase gives that queue request-kind-aware management in the first
 * place -- server.h's queue today is untyped raw frames used only for
 * the ep_enable pre-load-then-drain mechanism (milestone 61), not yet a
 * request-kind-classified structure this module could safely reach into
 * without also owning that reclassification itself.
 *
 * rcp_safept_wd_evaluate() is the single pure function tying regmap.h's
 * whole rx_wd_* family together: rx_wd_enable gates the watchdog on/off
 * entirely (a disabled watchdog can never overflow), rx_wd_timeout_ms is
 * the elapsed-time threshold, rx_wd_safestate_enable selects whether an
 * overflow should actually drive the endpoint toward its safe state
 * (enter_safe_state), and rx_wd_info_enable selects whether an overflow
 * should separately raise an informational event (notify) -- the two
 * outputs are independent, so a stream can be configured to notify
 * without entering its safe state, enter its safe state without a
 * separate notification, both, or neither. rx_wd_action (present since
 * milestone 62) stays caller-defined and round-tripped only: this
 * milestone's roadmap scope names no concrete action enumeration for it.
 *
 * ── rx_enforce_e2e: single-request drop vs. whole-stream latch-to-fault ────
 *
 * rcp_safept_crc_error_action() maps rx_enforce_e2e to one of two
 * responses to a CRC_ERROR: false selects
 * RCP_SAFEPT_CRC_ACTION_DROP_REQUEST (only the one CRC-failed request is
 * skipped; the stream itself stays usable), true selects
 * RCP_SAFEPT_CRC_ACTION_LATCH_STREAM_FAULT (the first CRC_ERROR latches
 * the whole stream permanently faulted). rcp_safept_stream_fault_t is
 * this module's own small caller-owned latch primitive implementing that
 * second behavior statefully, mirroring e2e.h's own
 * rcp_e2e_replay_guard_t precedent for a small stateful helper type
 * living alongside this module's otherwise-pure functions.
 */
#ifndef RCP_SAFEPT_H
#define RCP_SAFEPT_H

#include "rcp/rcp.h"
#include "rcp/sequencer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Trailing CRC32 size this module appends/expects: one quadlet, 4 octets. */
#define RCP_SAFEPT_CRC_LEN ((size_t)4u)

typedef enum {
    RCP_SAFEPT_OK               = 0,
    RCP_SAFEPT_ERR_SHORT_FRAME  = 1,
    RCP_SAFEPT_ERR_CRC_MISMATCH = 2, /* CRC_ERROR: caller must skip
                                         executing this request */
} rcp_safept_errc_t;

/* Never returns NULL. */
const char *rcp_safept_strerror(rcp_safept_errc_t e);

/* ── CRC32 ──────────────────────────────────────────────────────────────────── */

/* Computes this module's fixed CRC32 parameterization (see the file
 * header) over data[0..len). data may be NULL iff len == 0. */
uint32_t rcp_safept_crc32(const uint8_t *data, size_t len);

/* The coverage-span-specific wrapper: CRC32 over stream_id (8 bytes,
 * big-endian) + avtp_timestamp (8 bytes, big-endian) +
 * acf_frame[0..acf_frame_len). acf_frame may be NULL iff acf_frame_len ==
 * 0. Equivalent to concatenating those three regions and calling
 * rcp_safept_crc32() once, without the allocation such a concatenation
 * would need. */
uint32_t rcp_safept_compute_crc(uint64_t stream_id, uint64_t avtp_timestamp,
                                 const uint8_t *acf_frame, size_t acf_frame_len);

/* The length-accounting pre-adjustment: payload_len + RCP_SAFEPT_CRC_LEN.
 * Saturates at SIZE_MAX rather than wrapping if payload_len is already
 * within RCP_SAFEPT_CRC_LEN of SIZE_MAX. */
size_t rcp_safept_length_with_crc(size_t payload_len);

/* ── wrap / unwrap ─────────────────────────────────────────────────────────── */

/* Appends a RCP_SAFEPT_CRC_LEN-byte big-endian trailer to acf_frame (an
 * already fully-encoded ACF_ABB/ACF_GBB header-and-payload region, as
 * produced by acf.c), computed via rcp_safept_compute_crc() over
 * stream_id + avtp_timestamp + acf_frame. acf_frame may be NULL iff
 * acf_frame_len == 0. Returns a freshly heap-allocated, owned rcp_bytes_t
 * (data=NULL, len=0 on allocation failure); caller frees the result with
 * rcp_bytes_free(). */
rcp_bytes_t rcp_safept_wrap(uint64_t stream_id, uint64_t avtp_timestamp,
                             const uint8_t *acf_frame, size_t acf_frame_len);

/* Validates and strips the trailing CRC32 from frame (header-and-payload
 * plus trailer, as produced by rcp_safept_wrap()). On RCP_SAFEPT_OK,
 * *out_acf_frame / *out_acf_frame_len point into frame (borrowed, not
 * copied -- matching acf.c's own decode_* convention) covering just the
 * header-and-payload region, ready to hand to acf.c's
 * rcp_acf_decode_abb()/_decode_gbb(). Returns RCP_SAFEPT_ERR_SHORT_FRAME
 * if frame_len < RCP_SAFEPT_CRC_LEN (*out_acf_frame / *out_acf_frame_len
 * left untouched). Returns RCP_SAFEPT_ERR_CRC_MISMATCH (CRC_ERROR) if the
 * trailing CRC does not match -- the caller must skip executing the
 * request this frame carries; *out_acf_frame / *out_acf_frame_len are still
 * populated in this case, for diagnostic use, but must not be treated as
 * a validated payload. */
rcp_safept_errc_t rcp_safept_unwrap(uint64_t stream_id, uint64_t avtp_timestamp,
                                     const uint8_t *frame, size_t frame_len,
                                     const uint8_t **out_acf_frame, size_t *out_acf_frame_len);

/* ── Fragmentation/CRC interaction (modeled now, activated at Phase 20) ────── */

/* True iff a fragment carries a CRC under the fragmentation/CRC
 * interaction rule -- literally is_last_fragment, since only a multi-
 * segment message's final fragment ever does. See the file header. */
bool rcp_safept_fragment_carries_crc(bool is_last_fragment);

/* ── Safety-tagged request classification ────────────────────────────────── */

/* True iff request_type's MSB (0x80) is set. This module's own,
 * self-contained spelling of the safety-tagged-variant test -- see the
 * file header's "Safety-request execution gating" section. */
bool rcp_safept_is_safety_request(uint8_t request_type);

/* The safety-tagged-variant execution-admission rule: for a safety-tagged
 * request_type (rcp_safept_is_safety_request() true), returns
 * endpoint_in_safe_state; for any other request_type, always returns
 * true (this rule does not gate non-safety-tagged requests at all). */
bool rcp_safept_request_may_execute(uint8_t request_type, bool endpoint_in_safe_state);

/* ── The watchdog-purge-vs-safety-survive rule ───────────────────────────── */

/* True iff a queued request of request_type survives a watchdog-overflow
 * purge -- equivalently, rcp_safept_is_safety_request(request_type). */
bool rcp_safept_watchdog_purge_should_keep(uint8_t request_type);

/* Applies rcp_safept_watchdog_purge_should_keep() to every entry of
 * request_types[0..count), writing the per-entry keep/purge verdict into
 * the correspondingly-indexed out_keep[0..count). request_types and
 * out_keep must not overlap; out_keep may be NULL only iff count == 0.
 * Makes no assumption about the order or origin of request_types -- a
 * caller supplies whatever it currently has queued, in whatever order its
 * own queue keeps them. */
void rcp_safept_watchdog_purge_classify(const uint8_t *request_types, size_t count,
                                         bool *out_keep);

/* ── The configured safe state ───────────────────────────────────────────── */

typedef enum {
    RCP_SAFEPT_MEASURE_FORCE_HIGH_IMPEDANCE = 0,
    RCP_SAFEPT_MEASURE_SEQUENCER            = 1,
} rcp_safept_measure_t;

/* True iff rx_safety_measure is a recognized rcp_safept_measure_t value. */
bool rcp_safept_measure_valid(uint8_t rx_safety_measure);

/* Endpoint-reached-its-configured-safe-state check, feeding
 * rcp_safept_request_may_execute()'s endpoint_in_safe_state argument. See
 * the file header's "The configured safe state itself" section for the
 * full rationale, including the deliberate fail-closed behavior for an
 * unrecognized rx_safety_measure or an invalid safestate_sequencer index.
 * table may be NULL iff rx_safety_measure names
 * RCP_SAFEPT_MEASURE_FORCE_HIGH_IMPEDANCE (table is never consulted in
 * that case). */
bool rcp_safept_endpoint_in_safe_state(uint8_t rx_safety_measure,
                                        const rcp_sequencer_table_t *table,
                                        uint16_t safestate_sequencer,
                                        uint8_t safe_sequencer_state);

/* ── Per-stream watchdog ──────────────────────────────────────────────────── */

typedef struct {
    bool overflowed;       /* rx_wd_enable && elapsed >= rx_wd_timeout_ms */
    bool enter_safe_state; /* overflowed && rx_wd_safestate_enable */
    bool notify;            /* overflowed && rx_wd_info_enable */
} rcp_safept_wd_result_t;

/* Ties regmap.h's rx_wd_enable/rx_wd_timeout_ms/rx_wd_safestate_enable/
 * rx_wd_info_enable together into the single watchdog-overflow evaluation
 * this milestone's roadmap scope calls for. elapsed_since_last_kick_ms is
 * caller-computed: this module owns no clock or background thread of its
 * own (unlike watchdog.h's rcp_watchdog_keeper_t, which is a different
 * concept -- a client-side liveness *kicker* against a whole zone
 * controller, not this per-request-stream overflow evaluation), keeping
 * this function pure and directly testable, the same "own small pure
 * helpers" discipline every request-kind module in this milestone's
 * neighborhood already follows. See the file header for the full
 * field-by-field rationale. */
rcp_safept_wd_result_t rcp_safept_wd_evaluate(bool rx_wd_enable, uint32_t rx_wd_timeout_ms,
                                               bool rx_wd_safestate_enable,
                                               bool rx_wd_info_enable,
                                               uint64_t elapsed_since_last_kick_ms);

/* ── rx_enforce_e2e: single-request drop vs. whole-stream latch-to-fault ───── */

typedef enum {
    RCP_SAFEPT_CRC_ACTION_DROP_REQUEST       = 0,
    RCP_SAFEPT_CRC_ACTION_LATCH_STREAM_FAULT = 1,
} rcp_safept_crc_action_t;

/* rx_enforce_e2e's wired behavior -- see the file header. */
rcp_safept_crc_action_t rcp_safept_crc_error_action(bool rx_enforce_e2e);

/* Caller-owned per-stream latch implementing
 * RCP_SAFEPT_CRC_ACTION_LATCH_STREAM_FAULT statefully. Mirrors e2e.h's
 * rcp_e2e_replay_guard_t precedent: a small stateful helper type living
 * alongside this module's otherwise-pure functions. */
typedef struct {
    bool faulted;
} rcp_safept_stream_fault_t;

/* Initializes f to the not-faulted state. */
void rcp_safept_stream_fault_init(rcp_safept_stream_fault_t *f);

/* Applies a CRC_ERROR observed on a stream configured with rx_enforce_e2e
 * to f. Always returns true (a CRC_ERROR always means "skip this
 * request", regardless of rx_enforce_e2e). Additionally latches
 * f->faulted permanently (until rcp_safept_stream_fault_reset()) iff
 * rcp_safept_crc_error_action(rx_enforce_e2e) ==
 * RCP_SAFEPT_CRC_ACTION_LATCH_STREAM_FAULT; leaves f unchanged for
 * RCP_SAFEPT_CRC_ACTION_DROP_REQUEST. */
bool rcp_safept_stream_fault_on_crc_error(rcp_safept_stream_fault_t *f, bool rx_enforce_e2e);

/* True iff f is currently latched faulted. */
bool rcp_safept_stream_fault_is_faulted(const rcp_safept_stream_fault_t *f);

/* Clears f back to the not-faulted state. */
void rcp_safept_stream_fault_reset(rcp_safept_stream_fault_t *f);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SAFEPT_H */
