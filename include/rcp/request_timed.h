/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-TIMED-001
//cfusa:req REQ-TIMED-002
//cfusa:req REQ-TIMED-003
//cfusa:req REQ-TIMED-004
//cfusa:req REQ-TIMED-005
//cfusa:req REQ-TIMED-006
//cfusa:req REQ-TIMED-007
//cfusa:req REQ-TIMED-008
//cfusa:req REQ-TIMED-009
//cfusa:req REQ-TIMED-010
//cfusa:req REQ-TIMED-011

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-TIMED-012
//cfusa:req REQ-TIMED-013
//cfusa:req REQ-WIREERR-006
/*
 * request_timed.h -- Timed conditional requests for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 17, "Conditional Requests &
 * Sequencers", milestone 69).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), and the register-map's svr_implemented_options
 * time-sync feature group (regmap.h, milestone 62). Nothing in rcp.h,
 * wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c, regmap.h/regmap.c,
 * or any ep_* endpoint module is touched here -- the same layering
 * discipline every module since milestone 64 has followed.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── A per-request alternative to a TSCF header ───────────────────────────────
 *
 * avtp.h's TSCF header already carries one avtp_timestamp, the "earliest
 * moment" presentation time for every ACF message inside that AVTPDU
 * (avtp.h's own file header). A Timed request (request_type
 * RCP_REQUEST_TYPE_TIMED, 0x0A, no safety-tagged variant exists) instead
 * carries its own presentation_time sub-field directly, via request_compound.h's
 * shared message_timestamp-repurposing convention (see that header's file
 * comment) -- letting a client express the identical "don't execute
 * before this instant" semantics inside a plain NTSCF frame, without
 * needing a TSCF header at all. Like request_triggered.h and request_chained.h, this
 * module reuses that shared wire convention without including or calling
 * into request_compound.h itself.
 *
 * ── wire sub-field layout ───────────────────────────────────────────────────
 *
 * The eight octets of the repurposed message_timestamp region carry, in
 * order (offsets relative to the start of that region):
 *
 *   offset 0     request_type      (the opcode octet, 0x0A)
 *   offset 1     reserved          (one octet, all bits zero)
 *   offsets 2..7 presentation_time (six octets, big-endian)
 *
 * presentation_time is a 48-bit quantity: a gPTP-domain instant expressed
 * in nanoseconds, reduced modulo 2^48 (so it rolls over every few days).
 * Before v0.102.0 this module packed only a 32-bit value, starting one
 * octet too early -- which both overwrote the mandatory reserved octet and
 * truncated the field's rollover period. RCP_TIMED_PRESENTATION_TIME_MAX
 * below is that field's own maximum encodable value.
 *
 * The reserved octet at offset 1 is not merely conventionally zero: a
 * received Timed request carrying a non-zero value there is rejected
 * outright (RCP_TIMED_ERR_RESERVED_NONZERO), as are the hs and cs header
 * bits, which a Timed request must likewise leave clear
 * (RCP_TIMED_ERR_UNSUPPORTED_CMD).
 *
 * ── Admission: PRESENTATION_TIME_TOO_FAR and GPTP_FAIL ──────────────────────
 *
 * rcp_timed_admit() is the pure, directly-testable expression of this
 * milestone's roadmap-required rejection paths: GPTP_FAIL (the server has
 * no locked gPTP time base to evaluate presentation_time against at all)
 * takes priority over PRESENTATION_TIME_TOO_FAR (a presentation_time that
 * *can* be evaluated, but sits further in the future than the caller's
 * own configured admission horizon allows). A presentation_time already
 * in the past is never rejected by either path -- matching avtp.h's own
 * "earliest moment... not a hard deadline" semantics for TSCF's
 * avtp_timestamp, a late server is still conforming if it executes a
 * request whose presentation_time has already passed.
 *
 * ── Feature gating ────────────────────────────────────────────────────────
 *
 * rcp_timed_feature_enabled() reads (but the register map module itself
 * is not modified by this file) regmap.h's already-published
 * RCP_REGMAP_OPT_TIME_SYNC bit (REQ-RMAP-030) -- it must be set for a
 * server to accept Timed requests at all, since a Timed request is
 * meaningless without the same underlying time-sync capability TSCF
 * itself depends on.
 */
#ifndef RCP_REQUEST_TIMED_H
#define RCP_REQUEST_TIMED_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── request_type opcode value (no safety-tagged variant) ─────────────────── */

#define RCP_REQUEST_TYPE_TIMED ((uint8_t)0x0Au)

/* ── Errors ─────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_TIMED_OK                    = 0,
    RCP_TIMED_ERR_SHORT_FRAME       = 1,
    RCP_TIMED_ERR_BAD_MSG_TYPE      = 2,
    RCP_TIMED_ERR_NOT_REPURPOSED    = 3, /* decoded mtv != RCP_ACF_MTV_UNTIMED */
    RCP_TIMED_ERR_UNKNOWN_TYPE      = 4, /* opcode byte is not RCP_REQUEST_TYPE_TIMED */
    RCP_TIMED_ERR_RESERVED_NONZERO  = 5, /* the reserved octet at offset 1 of the
                                             repurposed region is not all-zero */
    RCP_TIMED_ERR_UNSUPPORTED_CMD   = 6, /* hs and/or cs set: a Timed request must
                                             leave both clear */
} rcp_timed_errc_t;

/* The largest encodable presentation_time: the all-ones value of the
 * 48-bit sub-field. rcp_timed_encode_request() rejects anything above
 * this rather than silently truncating it. */
#define RCP_TIMED_PRESENTATION_TIME_MAX ((uint64_t)0x0000FFFFFFFFFFFFull)

/* One past RCP_TIMED_PRESENTATION_TIME_MAX: the modulus presentation_time
 * arithmetic (rcp_timed_too_far()) wraps around. */
#define RCP_TIMED_PRESENTATION_TIME_MODULUS ((uint64_t)0x0001000000000000ull)

/* Human-readable message for an rcp_timed_errc_t value. Never returns NULL. */
const char *rcp_timed_strerror(rcp_timed_errc_t e);

/* ── Feature gating ─────────────────────────────────────────────────────────── */

/* True iff RCP_REGMAP_OPT_TIME_SYNC (regmap.h, REQ-RMAP-030's own
 * single "d: time synch and timed requests" bit -- retyped from this
 * function's own former two-bit-pair check, REQ-RMAP-030) is set in
 * options -- see the file header. */
bool rcp_timed_feature_enabled(uint8_t options);

/* ── Timed request encode/decode ──────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed timed request addressed to byte_bus_id,
 * packing presentation_time into the repurposed message_timestamp
 * region's trailing six octets, leaving the reserved octet at offset 1
 * all-zero, with the leading opcode byte set to RCP_REQUEST_TYPE_TIMED,
 * hs/cs left clear, and mtv forced to RCP_ACF_MTV_UNTIMED -- same
 * conventions as rcp_compound_encode_request() (request_compound.h).
 * payload/payload_len is this request's own opaque, endpoint-specific
 * data; payload may be NULL iff payload_len == 0. Returns a zeroed
 * rcp_bytes_t (data=NULL) if presentation_time exceeds
 * RCP_TIMED_PRESENTATION_TIME_MAX (never silently truncated), payload_len
 * exceeds RCP_ACF_MAX_PAYLOAD, or on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_timed_encode_request(rcp_byte_bus_id_t byte_bus_id, uint64_t presentation_time,
                                      uint8_t transaction_num, const uint8_t *payload,
                                      size_t payload_len);

/* REQ-TIMED-013: the OTHER of TC18 §11.2/§11.2.1's own two ways to time a
 * request -- see the file header's "A per-request alternative to a TSCF
 * header" section. rcp_timed_encode_request() above is the NTSCF-only
 * path (no TSCF header needed, presentation_time packed into the ACF_GBB
 * payload's own repurposed message_timestamp region). This function is
 * the TSCF-header path: it encodes byte_bus_id/evt/op/payload as a
 * PLAIN ACF_ABB message -- a standard request shape, no request_type
 * opcode byte, no repurposing trick at all -- via acf.h's
 * rcp_acf_encode_abb(), then wraps that frame in a TSCF header (avtp.h's
 * rcp_avtp_encode_tscf()) whose own avtp_timestamp carries
 * presentation_time and whose tv (timestamp-valid) bit is set. TC18's
 * own text is explicit that a timed request under a TSCF header "shall
 * likewise be encoded as an ACF_ABB message" rather than ACF_GBB, unlike
 * this module's other, NTSCF-only encoder above.
 *
 * A thin, named convenience composing two already-existing, independently
 * tested primitives (acf.h's rcp_acf_encode_abb(), avtp.h's
 * rcp_avtp_encode_tscf()) rather than duplicating either -- a caller
 * could already compose them directly (see tests/test_discovery.c's own
 * TSCF-wrapped-ABB construction), but request_timed.h is where a caller
 * reasoning about "timed requests" as a concept should find both of
 * TC18's own encoding paths, not just the NTSCF one.
 *
 * hdr is the caller-supplied ACF_ABB header (byte_bus_id/op/evt/etc.);
 * mtv is forced to RCP_ACF_MTV_UNTIMED by rcp_acf_encode_abb() itself,
 * matching every other ABB encode in this codebase (ABB has no
 * timestamp field of its own to validate -- the TSCF header's own
 * avtp_timestamp is the timing signal here, not mtv). payload may be
 * NULL iff payload_len == 0. Returns a zeroed rcp_bytes_t (data=NULL) on
 * any encode failure at either layer (oversized payload, allocation
 * failure). Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_timed_encode_request_tscf(const rcp_acf_byte_message_info_t *hdr,
                                           const uint8_t *payload, size_t payload_len,
                                           rcp_stream_id_t stream_id,
                                           uint32_t avtp_timestamp, uint8_t sequence_num);

/* Decodes and validates a timed request from b[0..len). Same failure-mode
 * conventions as rcp_compound_decode_request() (request_compound.h), with
 * RCP_TIMED_ERR_UNKNOWN_TYPE returned whenever the decoded opcode byte is
 * not RCP_REQUEST_TYPE_TIMED, RCP_TIMED_ERR_RESERVED_NONZERO when the
 * reserved octet at offset 1 of the repurposed region carries any set bit,
 * and RCP_TIMED_ERR_UNSUPPORTED_CMD when the decoded hs or cs header bit
 * is set. On RCP_TIMED_OK, *out_byte_bus_id, *out_presentation_time (in
 * [0, RCP_TIMED_PRESENTATION_TIME_MAX]), and *out_transaction_num are
 * populated, and *out_payload / *out_payload_len are set to a *borrowed*
 * view into b. */
rcp_timed_errc_t rcp_timed_decode_request(const uint8_t *b, size_t len,
                                           rcp_byte_bus_id_t *out_byte_bus_id,
                                           uint64_t *out_presentation_time,
                                           const uint8_t **out_payload, size_t *out_payload_len,
                                           uint8_t *out_transaction_num);

/* ── Admission ─────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_TIMED_ACCEPT                          = 0,
    RCP_TIMED_REJECT_GPTP_FAIL                = 1,
    RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR = 2,
} rcp_timed_admission_t;

/* True iff presentation_time sits strictly in the future of now (by
 * wraparound-safe subtraction modulo RCP_TIMED_PRESENTATION_TIME_MODULUS,
 * the 48-bit rollover period of the field itself) by more than
 * max_horizon -- i.e. RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR's own
 * pure trigger condition. A presentation_time at or before now is never
 * "too far"; a difference of more than half the modulus is read as "in
 * the past", the usual unambiguous split for a wrapping time domain.
 * Does not consider gPTP lock state -- see rcp_timed_admit(). */
bool rcp_timed_too_far(uint64_t presentation_time, uint64_t now, uint64_t max_horizon);

/* The combined admission decision: RCP_TIMED_REJECT_GPTP_FAIL if
 * !gptp_locked (presentation_time cannot be trusted at all without a
 * locked time base, so this check takes priority over the horizon
 * check), else RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR if
 * rcp_timed_too_far(presentation_time, now, max_horizon), else
 * RCP_TIMED_ACCEPT. */
rcp_timed_admission_t rcp_timed_admit(bool gptp_locked, uint64_t presentation_time, uint64_t now,
                                       uint64_t max_horizon);

/* True iff presentation_time is at or before now, in the same wrapping
 * 48-bit domain rcp_timed_too_far() uses -- i.e. this request's execution
 * condition is satisfied and it may now run. */
bool rcp_timed_due(uint64_t presentation_time, uint64_t now);

/* REQ-WIREERR-006 (issue #163): maps a to its numbered wire error code
 * (errors.h), for a caller populating a Response frame's err field --
 * mirrors rcp_e2e_wire_error()'s own established pattern exactly.
 * RCP_TIMED_REJECT_GPTP_FAIL maps to RCP_ERROR_GPTP_FAIL and
 * RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR to RCP_ERROR_
 * PRESENTATION_TIME_TOO_FAR -- the governing spec's own numbered
 * error-code table's assigned codes for each of rcp_timed_admit()'s two
 * rejection reasons (see that function's own doc comment). RCP_TIMED_
 * ACCEPT maps to RCP_ERROR_NONE (nothing to report).
 *
 * Only the GPTP_FAIL half of rcp_timed_admit() is currently reachable
 * from a real dispatch path (rcp_mock_server_dispatch()'s own
 * time_sync_supported parameter -- TC18's own gPTP-lock concept -- is
 * already threaded through every dispatch entry point; see mock.c's
 * dispatch_plain_inner()). PRESENTATION_TIME_TOO_FAR's own trigger
 * (rcp_timed_too_far(), against a "product specific limit" TC18 itself
 * leaves implementation-defined) has no configured admission-horizon
 * value anywhere in this codebase's register map to evaluate against --
 * wiring it up for real would mean inventing that configuration concept
 * from scratch, not just relaying an outcome this implementation
 * already computes, so it is left real future work rather than forced
 * here. This mapping function itself is still exercised, and correct,
 * for both outcomes -- only the dispatch-side wiring is partial. */
rcp_wire_error_t rcp_timed_wire_error(rcp_timed_admission_t a);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_TIMED_H */
