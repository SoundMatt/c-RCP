/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-E2E-001
//cfusa:req REQ-E2E-002
//cfusa:req REQ-E2E-003
//cfusa:req REQ-E2E-004
//cfusa:req REQ-E2E-005
//cfusa:req REQ-E2E-006
//cfusa:req REQ-E2E-007
//cfusa:req REQ-E2E-008
//cfusa:req REQ-E2E-009
//cfusa:req REQ-E2E-010
//cfusa:req REQ-E2E-011
//cfusa:req REQ-E2E-012
//cfusa:req REQ-E2E-013
//cfusa:req REQ-E2E-014
//cfusa:req REQ-E2E-015
//cfusa:req REQ-E2E-016
//cfusa:req REQ-E2E-017
//cfusa:req REQ-E2E-018
//cfusa:req REQ-E2E-019
//cfusa:req REQ-E2E-020
//cfusa:req REQ-E2E-021
//cfusa:req REQ-E2E-022
//cfusa:req REQ-E2E-023
//cfusa:req REQ-E2E-024
//cfusa:req REQ-E2E-025
//cfusa:req REQ-E2E-026
//cfusa:req REQ-E2E-027

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-E2E-028
//cfusa:req REQ-E2E-029
//cfusa:req REQ-E2E-030
//cfusa:req REQ-E2E-031
//cfusa:req REQ-E2E-032
//cfusa:req REQ-E2E-033
//cfusa:req REQ-E2E-034
//cfusa:req REQ-E2E-035
//cfusa:req REQ-E2E-036
//cfusa:req REQ-E2E-037
//cfusa:req REQ-E2E-038
//cfusa:req REQ-E2E-039
//cfusa:req REQ-E2E-040
//cfusa:req REQ-E2E-041
//cfusa:req REQ-E2E-042
/*
 * e2e.h -- CRC32 safe points and safety-request execution gating for the
 * TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 18, "E2E
 * Protection (Safe Points)", milestone 70).
 *
 * This header now fully replaces this module's pre-replacement content:
 * the ad-hoc CRC-16/CCITT-FALSE + sequence-counter + replay-window wrapper
 * around rcp.h's legacy rcp_controller_t (this file's own shape from
 * milestone 13 onward) is discarded outright, not adapted -- superseded by
 * the real TC18 CRC32 "safe point" mechanism below, which first landed
 * alongside the old content as a separate `safept.h`/`safept.c` at
 * milestone 70. The merge back into this file (deleting `safept.h`/
 * `safept.c`, renaming rcp_safept_* / RCP_SAFEPT_* to rcp_e2e_* / RCP_E2E_*)
 * is the module-naming reconciliation tracked at
 * github.com/SoundMatt/c-RCP/issues/87, which brought this repo's naming
 * in line with RELAY spec v1.14's §13.7.2 `e2e` registry entry ("end-to-end
 * / E2E safety protection") and with the equivalent, already-completed
 * merges in cpp-RCP's `e2e.hpp` and rust-RCP's `e2e.rs` -- both of which
 * independently made the identical call (full content replacement, no
 * legacy shim) when they reached their own milestone implementing this
 * mechanism, each noting that nothing else in their tree depended on the
 * old CRC-16 API. The same was true here: only this file's own test
 * (`tests/test_e2e.c`, itself replaced by the former `test_safept.c`'s
 * cases) depended on the discarded API, so no shim is needed.
 *
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * or any ep_* endpoint module is touched here. This module depends on
 * request_sequencer.h (milestone 68) only, the same "first-class supporting
 * primitive" every compound/compound-wait/triggered-adjacent module
 * already depends on directly (request_compound.h and request_triggered.h both include it)
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
 * rcp_e2e_crc32() implements one fixed 32-bit CRC parameterization:
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
 * the 4-byte avtp_timestamp (IEEE 1722's own field width; again all-zero
 * under NTSCF framing, which carries no timestamp of its own), and
 * finally the complete ACF header-and-payload region exactly as acf.c's
 * rcp_acf_encode_abb()/_encode_gbb() would produce it, *after* the
 * length adaptation below has already been applied to it.
 *
 * Before that CRC is computed, the ACF header's own acf_msg_length field
 * (acf.h, a 9-bit quadlet count spanning bit 0 of octet 0 and all of
 * octet 1) has to be adapted by plus one quadlet (RCP_E2E_CRC_LEN, 4
 * octets is exactly one quadlet, so this is a plain +1) to account for
 * the trailer about to be appended -- a receiver needs acf_msg_length to
 * describe the message's true on-wire length, trailer included, to know
 * where it ends.
 * rcp_e2e_wrap() owns this adaptation itself (patching the copy it
 * returns, not the caller's original acf_frame) rather than requiring
 * the caller to pre-assemble an already-extended payload: it is the
 * "composed encode entry point" that adapts the ACF length and inserts
 * the CRC together, called against an already fully-encoded ACF
 * header-and-payload region exactly as acf.c's own encoders produced
 * it, with their original (un-adapted) acf_msg_length still in place.
 * rcp_e2e_unwrap() reverses both steps: it verifies the CRC (computed,
 * like the sender did, over the *adapted* frame) and returns a copy of
 * the header-and-payload region with acf_msg_length adapted back down
 * by one quadlet, ready to hand to acf.c's rcp_acf_decode_abb()/_gbb()
 * unmodified. rcp_e2e_length_with_crc() remains available as the pure
 * arithmetic expression of the same +1-quadlet adjustment, for a caller
 * that instead wants to pre-size its own payload buffer before calling
 * into acf.c directly (e.g. to reserve the trailer's space up front
 * rather than have rcp_e2e_wrap() reallocate a copy).
 *
 * On a CRC mismatch, rcp_e2e_unwrap() returns
 * RCP_E2E_ERR_CRC_MISMATCH -- this module's own spelling of CRC_ERROR
 * -- and the caller must skip executing the request the frame carried,
 * per this milestone's roadmap scope.
 *
 * ── Fragmentation/CRC interaction: modeled here, driven by fragment.h ──────
 *
 * acf.h's read_size_or_segment_num field, and the ms bit alongside it, are
 * interpreted as a fragmentation signal by fragment.h (Phase 20,
 * ROADMAP.md milestone 76 -- named `fragment` per RELAY spec v1.14
 * §13.7.2, per the registry note ROADMAP.md/issue #87 left for that
 * milestone). This module's own fragmentation/CRC interaction rule
 * predates that module and is unchanged by it: only the last fragment of
 * a multi-segment message carries a CRC (computed across the fully
 * reassembled payload), and the length-accounting pre-adjustment applies
 * only to that final segment. rcp_e2e_fragment_carries_crc() is the pure,
 * directly-testable expression of that rule; fragment.h's own file header
 * documents it as the caller-facing entry point a fragmentation-aware
 * encode/decode path (e.g. ep_can.h's fragmented response codec) drives
 * against its own final-segment determination -- this module still has no
 * fragmentation state of its own to model, by design (see the file header
 * above for why: reused, not duplicated).
 *
 * ── Safety-request execution gating ─────────────────────────────────────────
 *
 * request_compound.h's RCP_REQUEST_TYPE_COMPOUND_SAFETY/_COMPOUND_WAIT_SAFETY
 * (0x8F/0x8B) and request_triggered.h's RCP_REQUEST_TYPE_TRIGGERED_SAFETY (0x8E)
 * already exist and round-trip through their own modules' encode/decode
 * functions as of milestones 68/69 -- both of those modules' own file
 * headers explicitly named gating their execution on the endpoint's
 * configured safe state as this milestone's job, not theirs.
 * rcp_e2e_is_safety_request() is this module's own, self-contained
 * MSB(0x80) test (mirroring, but not depending on, request_compound.c's identical
 * rcp_request_type_is_safety() -- the same "every request-kind module
 * owns its own small pure helpers" precedent request_triggered.h already set by
 * not including request_compound.h itself). rcp_e2e_request_may_execute() is
 * the actual gate: a safety-tagged request_type executes only once the
 * endpoint has reached its configured safe state; a non-safety-tagged
 * request_type is unaffected by this particular rule (any other
 * admission check -- e.g. scheduler.h's priority ordering -- is a
 * separate, unduplicated concern).
 *
 * ── The configured safe state itself ────────────────────────────────────────
 *
 * regmap.h's rx_safety_measure selects one of two safe-state measures:
 * RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE (0), an instantaneous action
 * with no state of its own to poll -- commanding it *is* reaching the
 * safe state, so rcp_e2e_endpoint_in_safe_state() always reports true
 * for it -- and RCP_E2E_MEASURE_SEQUENCER (1), which instead polls
 * regmap.h's rx_safestate_sequencer/rx_safe_sequencer_state against a
 * live request_sequencer.h table via rcp_sequencer_get_state(). An unrecognized
 * rx_safety_measure byte, or a RCP_E2E_MEASURE_SEQUENCER configuration
 * whose rx_safestate_sequencer index isn't valid in the table supplied,
 * both fail *closed*: rcp_e2e_endpoint_in_safe_state() reports false,
 * so a safety-tagged request stays blocked rather than executing against
 * an unverifiable safe-state claim. This is an explicit engineering
 * choice by this implementation (fail toward blocking, not toward
 * permitting), not a value taken from the specification.
 *
 * ── The watchdog-purge-vs-safety-survive rule ───────────────────────────────
 *
 * On a watchdog-timeout event (rcp_e2e_wd_evaluate() reporting
 * enter_safe_state == true), every currently-queued request whose
 * request_type is not safety-tagged is purged from the endpoint's queue;
 * only the safety-tagged variants survive, and it is those surviving
 * instances -- still subject to rcp_e2e_request_may_execute()'s own
 * gate -- that actually drive the endpoint to/through its configured safe
 * state. This is the primary safety mechanism this milestone's roadmap
 * scope calls for, not an edge case: rcp_e2e_watchdog_purge_should_keep()
 * (single entry) and rcp_e2e_watchdog_purge_classify() (a whole
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
 * rcp_e2e_wd_evaluate() is the single pure function tying regmap.h's
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
 * rcp_e2e_crc_error_action() maps rx_enforce_e2e to one of two
 * responses to a CRC_ERROR: false selects
 * RCP_E2E_CRC_ACTION_DROP_REQUEST (only the one CRC-failed request is
 * skipped; the stream itself stays usable), true selects
 * RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT (the first CRC_ERROR latches
 * the whole stream permanently faulted). rcp_e2e_stream_fault_t is
 * this module's own small caller-owned latch primitive implementing that
 * second behavior statefully -- the same "small stateful helper type
 * living alongside this module's otherwise-pure functions" shape the
 * pre-replacement content used for its own replay-guard.
 *
 * Note for downstream security/safety documentation: this module does not
 * reimplement the pre-replacement content's sequence-counter/replay-window
 * mechanism (there was no rx_enforce_seq-equivalent in this milestone's
 * roadmap scope) -- the same gap cpp-RCP's and rust-RCP's own merges left
 * open. tara.md/CYBERSECURITY.md/SAFETY_PLAN.md/FORMAL_VERIFICATION.md
 * still reference the pre-replacement REQ-E2E-004..012 ids for replay/CRC
 * threat mitigations; those ids no longer exist in .fusa-reqs.json
 * (superseded by REQ-E2E-001..027 above, describing the CRC32 mechanism
 * only). Updating that threat-model documentation is a security-review
 * judgment call, not a naming fix, and is out of scope for issue #87 --
 * left for a dedicated follow-up, matching the same latent gap already
 * present in cpp-RCP's shipped tara.md/CYBERSECURITY.md/SAFETY_PLAN.md.
 */
#ifndef RCP_E2E_H
#define RCP_E2E_H

#include "rcp/errors.h"
#include "rcp/rcp.h"
#include "rcp/request_sequencer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Trailing CRC32 size this module appends/expects: one quadlet, 4 octets. */
#define RCP_E2E_CRC_LEN ((size_t)4u)

typedef enum {
    RCP_E2E_OK               = 0,
    RCP_E2E_ERR_SHORT_FRAME  = 1,
    RCP_E2E_ERR_CRC_MISMATCH = 2, /* caller must skip executing this
                                         request; see rcp_e2e_wire_error() */
} rcp_e2e_errc_t;

/* Never returns NULL. */
const char *rcp_e2e_strerror(rcp_e2e_errc_t e);

/* Maps e to its numbered wire error code (errors.h), for a caller
 * populating a Response frame's err field. Returns RCP_ERROR_POCI_FAILURE
 * for RCP_E2E_ERR_CRC_MISMATCH (the governing spec's own numbered
 * error-code table's assigned code for a CRC mismatch -- see errors.h's
 * file header for why this isn't literally named "CRC_ERROR"), and
 * RCP_ERROR_NONE for every other value (RCP_E2E_OK and
 * RCP_E2E_ERR_SHORT_FRAME are both local outcomes with no wire-error-code
 * counterpart). */
rcp_wire_error_t rcp_e2e_wire_error(rcp_e2e_errc_t e);

/* ── CRC32 ──────────────────────────────────────────────────────────────────── */

/* Computes this module's fixed CRC32 parameterization (see the file
 * header) over data[0..len). data may be NULL iff len == 0. */
uint32_t rcp_e2e_crc32(const uint8_t *data, size_t len);

/* The coverage-span-specific wrapper: CRC32 over stream_id (8 bytes,
 * big-endian) + avtp_timestamp (4 bytes, big-endian -- IEEE 1722's own
 * field width; pass 0 for the NTSCF all-zero stand-in) +
 * acf_frame[0..acf_frame_len). acf_frame may be NULL iff acf_frame_len ==
 * 0. Equivalent to concatenating those three regions and calling
 * rcp_e2e_crc32() once, without the allocation such a concatenation
 * would need. */
uint32_t rcp_e2e_compute_crc(uint64_t stream_id, uint32_t avtp_timestamp,
                              const uint8_t *acf_frame, size_t acf_frame_len);

/* The length-accounting pre-adjustment: payload_len + RCP_E2E_CRC_LEN.
 * Saturates at SIZE_MAX rather than wrapping if payload_len is already
 * within RCP_E2E_CRC_LEN of SIZE_MAX. */
size_t rcp_e2e_length_with_crc(size_t payload_len);

//cfusa:req REQ-E2E-037
/* TC18 §13.6: an AVTPDU's ntscf_data_length (NTSCF) or stream_data_length
 * (TSCF) field must grow by RCP_E2E_CRC_LEN (4 octets) for every
 * E2E-protected ACF message its payload carries. avtp.c's
 * rcp_avtp_encode_ntscf()/_encode_tscf() already satisfy this
 * automatically and correctly -- both recompute the field from the
 * actual length of the payload buffer they are given, never from a
 * caller-supplied value, so the +4-per-protected-member accounting is
 * always right as long as the caller concatenated rcp_e2e_wrap()'s (or
 * _wrap_framed()'s) own output for each protected member before calling
 * either encoder. This function exists because, until now, nothing in
 * this codebase gave that same rule its own name: protected_member_count
 * * RCP_E2E_CRC_LEN, saturating at SIZE_MAX on overflow (the same
 * discipline rcp_e2e_length_with_crc() already follows) rather than
 * wrapping. Useful to a caller that wants to reason about, or
 * pre-validate, the expected delta independently of actually building
 * the payload -- e.g. sizing a buffer up front, or cross-checking a
 * peer's own encoded ntscf_data_length/stream_data_length against how
 * many protected members it claims to carry. */
size_t rcp_e2e_data_length_for_protected_members(size_t protected_member_count);

/* ── wrap / unwrap ─────────────────────────────────────────────────────────── */

/* The composed encode entry point: adapts a copy of acf_frame (an
 * already fully-encoded ACF_ABB/ACF_GBB header-and-payload region, as
 * produced by acf.c, still carrying its own original un-adapted
 * acf_msg_length) by incrementing its acf_msg_length field (acf.h, the
 * 9-bit quadlet count spanning bit 0 of octet 0 and all of octet 1;
 * octet 0's other 7 bits, acf_msg_type, are preserved unmodified) by one
 * quadlet, computes the CRC via rcp_e2e_compute_crc() over stream_id +
 * avtp_timestamp + that adapted copy, and appends the RCP_E2E_CRC_LEN-byte
 * big-endian trailer. acf_frame may be NULL iff acf_frame_len == 0.
 * acf_frame_len must be at least 2 octets (enough to contain the
 * acf_msg_length field) for the adaptation to have anywhere to write;
 * shorter non-NULL frames fail safe (data=NULL, len=0), as does an
 * acf_msg_length value already at its 9-bit field's maximum (0x1FF).
 * acf_frame_len must also be a whole quadlet (a multiple of 4) -- TC18
 * §13.6 Figures 19/20 compute the CRC over whole quadlets of the ACF
 * message so the trailer itself occupies the message's final whole
 * quadlet; a non-quadlet-aligned acf_frame_len fails safe the same way
 * (data=NULL, len=0) rather than appending a trailer that straddles a
 * quadlet boundary. Caller owns acf_frame throughout -- it is read, never
 * modified. Returns a freshly heap-allocated, owned rcp_bytes_t (data=NULL,
 * len=0 on allocation failure or any of the above); caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_e2e_wrap(uint64_t stream_id, uint32_t avtp_timestamp,
                          const uint8_t *acf_frame, size_t acf_frame_len);

/* The composed decode entry point, reversing rcp_e2e_wrap(): validates
 * the trailing CRC32 of frame (header-and-payload plus trailer, as
 * produced by rcp_e2e_wrap(), acf_msg_length still reflecting the
 * sender's +1-quadlet adaptation) via rcp_e2e_compute_crc() over
 * stream_id + avtp_timestamp + frame-minus-trailer, then -- regardless
 * of match -- writes *out_acf_frame a freshly heap-allocated copy of the
 * header-and-payload region (frame minus the trailing RCP_E2E_CRC_LEN
 * bytes) with acf_msg_length adapted back down by one quadlet, ready to
 * hand to acf.c's rcp_acf_decode_abb()/_decode_gbb() unmodified. Caller
 * frees *out_acf_frame with rcp_bytes_free() once RCP_E2E_OK or
 * RCP_E2E_ERR_CRC_MISMATCH is returned. Returns RCP_E2E_ERR_SHORT_FRAME
 * if frame_len < RCP_E2E_CRC_LEN (*out_acf_frame left zeroed, nothing to
 * free). Returns RCP_E2E_ERR_CRC_MISMATCH (CRC_ERROR) if the trailing
 * CRC does not match -- the caller must skip executing the request this
 * frame carries; *out_acf_frame is still populated in this case, for
 * diagnostic use, but must not be treated as a validated payload. */
rcp_e2e_errc_t rcp_e2e_unwrap(uint64_t stream_id, uint32_t avtp_timestamp,
                               const uint8_t *frame, size_t frame_len,
                               rcp_bytes_t *out_acf_frame);

/* ── Framing-aware wrap/unwrap: the NTSCF all-zero timestamp stand-in ───────── */

/* Framing-safe convenience wrapper over rcp_e2e_wrap(): forces the CRC's
 * avtp_timestamp contribution to 0 when is_ntscf_framed is true (TC18
 * §13.6's own all-zero stand-in for a message riding an NTSCF header,
 * which carries no timestamp field of its own -- ignoring whatever the
 * caller passed in avtp_timestamp for that case, rather than trusting it
 * to already be 0), or passes avtp_timestamp through unchanged when
 * is_ntscf_framed is false (a TSCF-framed message, whose own
 * avtp_timestamp field is real and must be reflected in the CRC). Prefer
 * this over calling rcp_e2e_wrap() directly whenever the caller already
 * knows which framing a message is riding -- e.g. avtp.h's own
 * RCP_AVTP_SUBTYPE_NTSCF/_TSCF discriminator -- since rcp_e2e_wrap()
 * itself has no way to catch a caller passing a nonzero avtp_timestamp
 * for NTSCF-framed traffic; see the file header. Every other parameter
 * and the return value are exactly rcp_e2e_wrap()'s own. */
rcp_bytes_t rcp_e2e_wrap_framed(uint64_t stream_id, bool is_ntscf_framed,
                                 uint32_t avtp_timestamp,
                                 const uint8_t *acf_frame, size_t acf_frame_len);

/* The decode-side counterpart of rcp_e2e_wrap_framed(): forces
 * avtp_timestamp to 0 when is_ntscf_framed is true before delegating to
 * rcp_e2e_unwrap(), so a caller that already knows a message's framing
 * cannot accidentally verify it against the wrong (nonzero) timestamp
 * contribution. Every other parameter and the return value are exactly
 * rcp_e2e_unwrap()'s own. */
rcp_e2e_errc_t rcp_e2e_unwrap_framed(uint64_t stream_id, bool is_ntscf_framed,
                                      uint32_t avtp_timestamp,
                                      const uint8_t *frame, size_t frame_len,
                                      rcp_bytes_t *out_acf_frame);

/* ── Fragmentation/CRC interaction (modeled now, activated at Phase 20) ────── */

/* True iff a fragment carries a CRC under the fragmentation/CRC
 * interaction rule -- literally is_last_fragment, since only a multi-
 * segment message's final fragment ever does. See the file header. */
bool rcp_e2e_fragment_carries_crc(bool is_last_fragment);

//cfusa:req REQ-E2E-038
/* TC18 §13.6's fragmented-message CRC coverage rule, the one case
 * rcp_e2e_compute_crc() alone cannot express: for a message split across
 * more than one AVTPDU (fragment.h), the CRC32 spans stream_id +
 * avtp_timestamp (as always) followed by the FIRST fragment's ACF header
 * -- not the last fragment's, even though the trailer this CRC produces
 * is the one appended to (and only to) the last fragment's own message --
 * followed by the concatenated byte_msg_payload of EVERY segment in
 * order (fragment.h's rcp_fragment_reassembler_get() already produces
 * exactly this concatenation on the decode side; an encode-side caller
 * assembles the same by concatenating each rcp_fragment_plan() segment's
 * own payload slice in order).
 *
 * first_fragment_header is the first fragment's own encoded
 * byte_message_info bytes (RCP_ACF_ABB_HEADER_LEN or
 * RCP_ACF_GBB_HEADER_LEN octets, acf.h -- fixed regardless of which
 * fragment in the sequence it comes from, so this parameter's length is
 * always one of those two values in practice, though this function
 * itself does not require it). reassembled_payload is the full
 * concatenation described above, NOT any single fragment's own payload
 * slice. Equivalent to concatenating first_fragment_header ++
 * reassembled_payload and calling rcp_e2e_compute_crc(stream_id,
 * avtp_timestamp, ..., ...) once, without the allocation such a
 * concatenation would need -- the same "running CRC over several
 * caller-owned regions" technique rcp_e2e_compute_crc() itself already
 * uses for stream_id/avtp_timestamp/acf_frame.
 *
 * This function is the CRC arithmetic alone. It does not itself locate
 * "the first fragment's header" out of a sequence of already-encoded
 * fragments, does not itself append the resulting CRC to the last
 * fragment's message, and does not itself adapt the last fragment's
 * acf_msg_length/AVTPDU data-length fields (rcp_e2e_length_with_crc()
 * and rcp_e2e_data_length_for_protected_members() remain the pure
 * expressions of those two adjustments respectively) -- composing all of
 * that into an actual fragmented-and-protected encode/decode pipeline is
 * a caller's job, matching every other function in this module's "own
 * small pure helpers, operate on caller-owned data" layering discipline.
 * No caller in this codebase does so yet: mock.c has no fragmented-
 * message dispatch path of any kind (protected or not) to wire this
 * into, a materially larger, separate architecture item than adding this
 * one arithmetic primitive. */
uint32_t rcp_e2e_compute_fragmented_crc(uint64_t stream_id, uint32_t avtp_timestamp,
                                         const uint8_t *first_fragment_header,
                                         size_t first_fragment_header_len,
                                         const uint8_t *reassembled_payload,
                                         size_t reassembled_payload_len);

/* ── Safety-tagged request classification ────────────────────────────────── */

/* True iff request_type's MSB (0x80) is set. This module's own,
 * self-contained spelling of the safety-tagged-variant test -- see the
 * file header's "Safety-request execution gating" section. */
bool rcp_e2e_is_safety_request(uint8_t request_type);

/* The safety-tagged-variant execution-admission rule: for a safety-tagged
 * request_type (rcp_e2e_is_safety_request() true), returns
 * endpoint_in_safe_state; for any other request_type, always returns
 * true (this rule does not gate non-safety-tagged requests at all). */
bool rcp_e2e_request_may_execute(uint8_t request_type, bool endpoint_in_safe_state);

/* ── The watchdog-purge-vs-safety-survive rule ───────────────────────────── */

/* True iff a queued request of request_type survives a watchdog-overflow
 * purge -- equivalently, rcp_e2e_is_safety_request(request_type). */
bool rcp_e2e_watchdog_purge_should_keep(uint8_t request_type);

/* Applies rcp_e2e_watchdog_purge_should_keep() to every entry of
 * request_types[0..count), writing the per-entry keep/purge verdict into
 * the correspondingly-indexed out_keep[0..count). request_types and
 * out_keep must not overlap; out_keep may be NULL only iff count == 0.
 * Makes no assumption about the order or origin of request_types -- a
 * caller supplies whatever it currently has queued, in whatever order its
 * own queue keeps them. */
void rcp_e2e_watchdog_purge_classify(const uint8_t *request_types, size_t count,
                                      bool *out_keep);

/* ── The configured safe state ───────────────────────────────────────────── */

typedef enum {
    RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE = 0,
    RCP_E2E_MEASURE_SEQUENCER            = 1,
} rcp_e2e_measure_t;

/* True iff rx_safety_measure is a recognized rcp_e2e_measure_t value. */
bool rcp_e2e_measure_valid(uint8_t rx_safety_measure);

/* Endpoint-reached-its-configured-safe-state check, feeding
 * rcp_e2e_request_may_execute()'s endpoint_in_safe_state argument. See
 * the file header's "The configured safe state itself" section for the
 * full rationale, including the deliberate fail-closed behavior for an
 * unrecognized rx_safety_measure or an invalid safestate_sequencer index.
 * table may be NULL iff rx_safety_measure names
 * RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE (table is never consulted in
 * that case). */
bool rcp_e2e_endpoint_in_safe_state(uint8_t rx_safety_measure,
                                     const rcp_sequencer_table_t *table,
                                     uint16_t safestate_sequencer,
                                     uint8_t safe_sequencer_state);

/* ── Per-stream watchdog ──────────────────────────────────────────────────── */

typedef struct {
    bool overflowed;       /* rx_wd_enable && elapsed >= rx_wd_timeout_ms */
    bool enter_safe_state; /* overflowed && rx_wd_safestate_enable */
    bool notify;            /* overflowed && rx_wd_info_enable */
} rcp_e2e_wd_result_t;

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
rcp_e2e_wd_result_t rcp_e2e_wd_evaluate(bool rx_wd_enable, uint32_t rx_wd_timeout_ms,
                                         bool rx_wd_safestate_enable,
                                         bool rx_wd_info_enable,
                                         uint64_t elapsed_since_last_kick_ms);

/* ── Request-storage overflow ────────────────────────────────────────────── */

//cfusa:req REQ-E2E-030
/* TC18 §12.7.7 Table 22's rx_ovrflw_safestate_enable, evaluated the same
 * shape as rcp_e2e_wd_evaluate() above: overflow has already happened by
 * construction when a caller reaches this function (an endpoint's request
 * storage is exhausted -- see rcp_server_endpoint_admit()'s
 * RCP_ERROR_REQUEST_STORAGE_OVERFLOW path in server.c), so this is
 * trivially rx_ovrflw_safestate_enable gated on nothing else. It exists as
 * its own named, pure, directly-testable predicate -- rather than an
 * inline `if (rx_ovrflw_safestate_enable)` at each call site -- because
 * TC18 names it as a distinct configured behavior, and because the one
 * caller currently in this codebase (rcp_server_endpoint_admit()) can only
 * ever act on a single rcp_server_endpoint_t: TC18 requires this decision
 * to drive every endpoint bound to the affected request stream into its
 * configured safe state, and this library's current data model has no
 * type representing "all endpoints on a stream" for a single endpoint's
 * admit() call to reach across into. That escalation is therefore left to
 * whichever future phase gives request streams their own cross-endpoint
 * management (the same boundary e2e.h's file header already documents for
 * rcp_e2e_watchdog_purge_should_keep()/_classify()); this function is the
 * caller-facing decision such an orchestrator would consult. */
bool rcp_e2e_overflow_should_enter_safe_state(bool rx_ovrflw_safestate_enable);

/* ── Per-stream sequence-number enforcement ──────────────────────────────── */

/* Caller-owned per-stream tracker: the "previously accepted request on this
 * stream" state TC18 §12.7.7 Table 22's rx_enforce_seq/rx_seq_safestate_enable
 * are both defined against. Same "own small stateful helper living alongside
 * this module's otherwise-pure functions" shape as rcp_e2e_stream_fault_t
 * above -- one instance per configured request stream, caller-allocated,
 * this module owns no registry of them. */
typedef struct {
    bool    has_prev;  /* false until the first call to
                           rcp_e2e_seq_evaluate() -- there is no
                           "previously accepted request" yet, so that first
                           call always accepts (nothing to compare against)
                           and never reports a discontinuity. */
    uint8_t prev_seq;  /* the last sequence_num rcp_e2e_seq_evaluate()
                           accepted -- see that function's own doc comment
                           for why this only ever advances on accept. */
} rcp_e2e_seq_tracker_t;

/* Zero-initializes t (has_prev = false, prev_seq = 0). */
void rcp_e2e_seq_tracker_init(rcp_e2e_seq_tracker_t *t);

typedef struct {
    bool accept;           /* the request may be filed for execution */
    bool discontinuity;    /* seq did not advance by exactly one increment
                               from the previously tracked value (never true
                               on the first call -- see has_prev above) */
    bool enter_safe_state; /* discontinuity && rx_seq_safestate_enable */
} rcp_e2e_seq_result_t;

//cfusa:req REQ-E2E-028
//cfusa:req REQ-E2E-029
/* TC18 §12.7.7 Table 22 defines two independently-configurable reactions
 * to a request stream's AVTPDU sequence_num, evaluated together here
 * because both compare the same incoming seq against the same tracked
 * state, but deliberately NOT collapsed into one bool: they answer
 * different questions and either can be enabled without the other.
 *
 *   - rx_enforce_seq (bit 1): "Requests are only filed for execution if
 *     sequence number in AVTPDU is increased" -- the coarser, admission-
 *     gating check. result.accept is true whenever !rx_enforce_seq (the
 *     gate is off) or seq is strictly ahead of the tracked value (see the
 *     wraparound note below); false when seq is stale (a replay or
 *     reorder) and rx_enforce_seq is on.
 *
 *   - rx_seq_safestate_enable (bit 2): "bring all endpoints to safety
 *     state if Sequence_Nr has no single increment" -- a stricter,
 *     independent check for a *gap* (seq advanced by more than one, e.g.
 *     a request was lost in transit), which fires even when
 *     result.accept is true, because an increase-but-not-by-exactly-one
 *     is still evidence something is wrong even though ordering itself
 *     was preserved. Same cross-endpoint escalation boundary as
 *     rcp_e2e_overflow_should_enter_safe_state() -- this function reports
 *     the decision, not the stream-wide action itself; see that
 *     function's own doc comment.
 *
 * Wraparound: avtp.h's sequence_num is a plain uint8_t (matching IEEE
 * 1722's own field width) that free-runs and wraps 0xFF -> 0x00 over any
 * long-lived stream -- TC18's own prose ("a strict monotonous increasing
 * sequence number of the requests can be enforced") does not spell out
 * modular comparison, but a literal always-greater-than reading would
 * make rx_enforce_seq reject every single request once the counter first
 * wraps, which cannot be the intended behavior of a mechanism meant to
 * run indefinitely. This function instead uses the standard serial-number
 * comparison technique (RFC 1982): seq is "ahead" of prev_seq iff their
 * unsigned difference, taken modulo 256, lies in [1, 127] -- the nearer
 * half of the circle in the forward direction -- which treats 0x00 as
 * ahead of 0xFF (a real wrap) while still rejecting a seq that jumped
 * backward by any amount up to half the space (a replay). "Exactly one
 * increment" for discontinuity is unambiguous regardless: seq ==
 * (uint8_t)(prev_seq + 1).
 *
 * t's state advances only when result.accept is true: t->prev_seq is
 * specifically "the previously ACCEPTED request on this stream" (this
 * function's own contract), not merely the last seq observed. Advancing
 * on a rejected (stale/replayed) seq would drag the reference point
 * backward and weaken this same call's detection of the genuine next
 * request -- a caller-owned-state discipline in the same spirit as
 * rcp_e2e_stream_fault_t above, but deliberately not "unconditional"
 * update, unlike that type's own f->faulted latch. */
rcp_e2e_seq_result_t rcp_e2e_seq_evaluate(rcp_e2e_seq_tracker_t *t, bool rx_enforce_seq,
                                           bool rx_seq_safestate_enable, uint8_t seq);

/* ── rx_enforce_e2e: single-request drop vs. whole-stream latch-to-fault ───── */

typedef enum {
    RCP_E2E_CRC_ACTION_DROP_REQUEST       = 0,
    RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT = 1,
} rcp_e2e_crc_action_t;

/* rx_enforce_e2e's wired behavior -- see the file header. */
rcp_e2e_crc_action_t rcp_e2e_crc_error_action(bool rx_enforce_e2e);

/* Caller-owned per-stream latch implementing
 * RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT statefully -- this module's own
 * small stateful helper type living alongside its otherwise-pure
 * functions. */
typedef struct {
    bool faulted;
} rcp_e2e_stream_fault_t;

/* Initializes f to the not-faulted state. */
void rcp_e2e_stream_fault_init(rcp_e2e_stream_fault_t *f);

/* Applies a CRC_ERROR observed on a stream configured with rx_enforce_e2e
 * to f. Always returns true (a CRC_ERROR always means "skip this
 * request", regardless of rx_enforce_e2e). Additionally latches
 * f->faulted permanently (until rcp_e2e_stream_fault_reset()) iff
 * rcp_e2e_crc_error_action(rx_enforce_e2e) ==
 * RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT; leaves f unchanged for
 * RCP_E2E_CRC_ACTION_DROP_REQUEST. */
bool rcp_e2e_stream_fault_on_crc_error(rcp_e2e_stream_fault_t *f, bool rx_enforce_e2e);

/* True iff f is currently latched faulted. */
bool rcp_e2e_stream_fault_is_faulted(const rcp_e2e_stream_fault_t *f);

/* Clears f back to the not-faulted state. */
void rcp_e2e_stream_fault_reset(rcp_e2e_stream_fault_t *f);

#ifdef __cplusplus
}
#endif

#endif /* RCP_E2E_H */
