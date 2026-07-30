/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-CANCEL-001
//cfusa:req REQ-CANCEL-002
//cfusa:req REQ-CANCEL-003
//cfusa:req REQ-CANCEL-004
//cfusa:req REQ-CANCEL-005
//cfusa:req REQ-CANCEL-006
//cfusa:req REQ-CANCEL-007
//cfusa:req REQ-CANCEL-008
//cfusa:req REQ-CANCEL-009
//cfusa:req REQ-CANCEL-010
//cfusa:req REQ-CANCEL-011
//cfusa:req REQ-CANCEL-012
/*
 * request_cancel.h -- The cancellation taxonomy for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 17, "Conditional Requests &
 * Sequencers", milestone 69).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59) and the ACF message format (acf.h/
 * acf.c, milestone 60). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/
 * acf.c, server.h/server.c, regmap.h/regmap.c, or any ep_* endpoint module
 * is touched here -- the same layering discipline every module since
 * milestone 64 has followed.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── One coherent taxonomy, three request types ───────────────────────────────
 *
 *   0x05  clear-all             this module (new). Mandatory since Phase
 *                                13's baseline, but not previously given
 *                                its own encode/decode surface anywhere in
 *                                this codebase.
 *   0x06  clear-non-safestate   request_compound.c/compound.h (already shipped at
 *                                v0.68.0, milestone 68) -- see
 *                                rcp_compound_encode_clear_non_safestate()/
 *                                _decode_clear_non_safestate(). Not
 *                                duplicated here.
 *   0x07  clear-single          this module (new). Carries a
 *                                clear_transaction_num field identifying
 *                                which queued request to cancel.
 *
 * All three share request_compound.h's message_timestamp-repurposing wire
 * convention (see that header's file comment); this module reuses it
 * without including or calling into request_compound.h itself, matching
 * request_triggered.h/chained.h/timed.h's own precedent. clear-all and
 * clear-single both carry no payload of their own beyond their opcode
 * byte and (for clear-single) the one clear_transaction_num sub-field --
 * the remaining sub-field bytes are always encoded/decoded as zero,
 * mirroring request_compound.h's own clear-non-safestate encoding.
 *
 * ── General cancellation semantics ───────────────────────────────────────────
 *
 * A request is cancellable only in the window between being queued and
 * actually beginning execution -- rcp_cancel_is_cancellable() is the
 * pure, directly-testable expression of that rule, applying uniformly to
 * every cancellation type above (clear-all, clear-non-safestate,
 * clear-single all target requests in this same lifecycle window).
 * rcp_cancel_attempt() composes that rule with a caller-supplied "was the
 * target request found at all" flag to report one of this module's own
 * outcomes: RCP_CANCEL_RESULT_CANCELED (the roadmap's REQUEST_CANCELED,
 * reported once per individually-cancelled request),
 * RCP_CANCEL_RESULT_NOT_FOUND (the roadmap's REQUEST_NOT_FOUND, reported
 * on a clear-single miss), or RCP_CANCEL_RESULT_NOT_CANCELLABLE (this
 * module's own outcome for a request found but already past the
 * queued/executing window -- deliberately distinct from NOT_FOUND, since
 * the two roadmap-named codes only cover "never existed" and "actually
 * canceled", not "found, but too late").
 *
 * rcp_cancel_chain_should_cascade() is this module's own expression of
 * the roadmap's chained-successor cascade rule: canceling one member of a
 * chained request (request_chained.h) also cancels every member at or after that
 * member's own chain_position within the same chain -- this function is
 * the pure per-member predicate a caller applies across a chain's
 * members, mirroring the same "pure decision function, caller drives the
 * loop" shape request_chained.h's own rcp_chained_advance() already established.
 */
#ifndef RCP_REQUEST_CANCEL_H
#define RCP_REQUEST_CANCEL_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── request_type opcode values (neither carries a safety-tagged variant) ─── */

#define RCP_REQUEST_TYPE_CLEAR_ALL    ((uint8_t)0x05u)
#define RCP_REQUEST_TYPE_CLEAR_SINGLE ((uint8_t)0x07u)

/* ── Errors ─────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_CANCEL_OK                 = 0,
    RCP_CANCEL_ERR_SHORT_FRAME    = 1,
    RCP_CANCEL_ERR_BAD_MSG_TYPE   = 2,
    RCP_CANCEL_ERR_NOT_REPURPOSED = 3, /* decoded mtv != RCP_ACF_MTV_UNTIMED */
    RCP_CANCEL_ERR_UNKNOWN_TYPE   = 4, /* opcode byte matches neither
                                           request type the function
                                           called recognizes */
} rcp_cancel_errc_t;

/* Human-readable message for an rcp_cancel_errc_t value. Never returns NULL. */
const char *rcp_cancel_strerror(rcp_cancel_errc_t e);

/* ── clear-all (0x05) ─────────────────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed clear-all cancellation request addressed to
 * byte_bus_id, with the repurposed message_timestamp region's opcode
 * byte set to RCP_REQUEST_TYPE_CLEAR_ALL and its remaining 7 sub-field
 * bytes zeroed (this request type carries none of its own) and mtv
 * forced to RCP_ACF_MTV_UNTIMED, same convention as
 * rcp_compound_encode_clear_non_safestate() (request_compound.h). Returns a
 * zeroed rcp_bytes_t (data=NULL) only on allocation failure. Caller frees
 * the result with rcp_bytes_free(). */
rcp_bytes_t rcp_cancel_encode_clear_all(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num);

/* Decodes and validates a clear-all request from b[0..len). Same
 * failure-mode conventions as rcp_compound_decode_clear_non_safestate()
 * (request_compound.h), with RCP_CANCEL_ERR_UNKNOWN_TYPE returned whenever the
 * decoded opcode byte is not RCP_REQUEST_TYPE_CLEAR_ALL. On
 * RCP_CANCEL_OK, *out_byte_bus_id and *out_transaction_num are
 * populated. */
rcp_cancel_errc_t rcp_cancel_decode_clear_all(const uint8_t *b, size_t len,
                                               rcp_byte_bus_id_t *out_byte_bus_id,
                                               uint8_t *out_transaction_num);

/* ── clear-single (0x07) ──────────────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed clear-single cancellation request addressed
 * to byte_bus_id, packing clear_transaction_num into the repurposed
 * message_timestamp region's first sub-field byte (the remaining 6
 * reserved and zeroed) with the leading opcode byte set to
 * RCP_REQUEST_TYPE_CLEAR_SINGLE and mtv forced to RCP_ACF_MTV_UNTIMED.
 * clear_transaction_num identifies which previously-queued request
 * (by its own transaction_num) this clear-single request targets.
 * Returns a zeroed rcp_bytes_t (data=NULL) only on allocation failure.
 * Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_cancel_encode_clear_single(rcp_byte_bus_id_t byte_bus_id,
                                            uint8_t clear_transaction_num,
                                            uint8_t transaction_num);

/* Decodes and validates a clear-single request from b[0..len). Same
 * failure-mode conventions as rcp_cancel_decode_clear_all(), with
 * RCP_CANCEL_ERR_UNKNOWN_TYPE returned whenever the decoded opcode byte
 * is not RCP_REQUEST_TYPE_CLEAR_SINGLE. On RCP_CANCEL_OK,
 * *out_byte_bus_id, *out_clear_transaction_num, and *out_transaction_num
 * are populated. */
rcp_cancel_errc_t rcp_cancel_decode_clear_single(const uint8_t *b, size_t len,
                                                  rcp_byte_bus_id_t *out_byte_bus_id,
                                                  uint8_t *out_clear_transaction_num,
                                                  uint8_t *out_transaction_num);

/* ── General cancellation semantics ───────────────────────────────────────── */

typedef enum {
    RCP_CANCEL_LIFECYCLE_QUEUED    = 0,
    RCP_CANCEL_LIFECYCLE_EXECUTING = 1,
    RCP_CANCEL_LIFECYCLE_DONE      = 2,
} rcp_cancel_lifecycle_t;

/* True iff state == RCP_CANCEL_LIFECYCLE_QUEUED -- see the file header. */
bool rcp_cancel_is_cancellable(rcp_cancel_lifecycle_t state);

typedef enum {
    RCP_CANCEL_RESULT_CANCELED        = 0, /* REQUEST_CANCELED */
    RCP_CANCEL_RESULT_NOT_FOUND       = 1, /* REQUEST_NOT_FOUND */
    RCP_CANCEL_RESULT_NOT_CANCELLABLE = 2, /* found, but past the
                                               queued/executing window --
                                               see the file header */
} rcp_cancel_result_t;

/* found is whether the target request (identified by, e.g., a
 * clear-single's clear_transaction_num) was located at all; state is
 * that request's own lifecycle state if found is true (ignored
 * otherwise). Returns RCP_CANCEL_RESULT_NOT_FOUND if !found, else
 * RCP_CANCEL_RESULT_NOT_CANCELLABLE if !rcp_cancel_is_cancellable(state),
 * else RCP_CANCEL_RESULT_CANCELED. */
rcp_cancel_result_t rcp_cancel_attempt(bool found, rcp_cancel_lifecycle_t state);

/* True iff a chain member at member_position must also be canceled as
 * part of cascading a cancellation targeted at canceled_position within
 * the same chain -- i.e. member_position is at or after
 * canceled_position. Both positions use request_chained.h's own 0-based
 * chain_position numbering. A member strictly before canceled_position
 * has already executed by the time a chain member is canceled (chained
 * execution is sequential, per request_chained.h's own file header) and is
 * therefore never cascaded to. */
bool rcp_cancel_chain_should_cascade(uint8_t member_position, uint8_t canceled_position);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_CANCEL_H */
