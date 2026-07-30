/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-CHAIN-001
//cfusa:req REQ-CHAIN-002
//cfusa:req REQ-CHAIN-003
//cfusa:req REQ-CHAIN-004
//cfusa:req REQ-CHAIN-005
//cfusa:req REQ-CHAIN-006
//cfusa:req REQ-CHAIN-007
//cfusa:req REQ-CHAIN-008
//cfusa:req REQ-CHAIN-009
//cfusa:req REQ-CHAIN-010
/*
 * request_chained.h -- Chained conditional requests for the TC18 Remote Control
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
 * ── What a chain is, in this codebase's own terms ────────────────────────────
 *
 * A chained request forces sequential execution of two or more requests
 * packed as separate ACF messages within one AVTPDU. Each member of a
 * chain is its own ACF_GBB message carrying request_type
 * RCP_REQUEST_TYPE_CHAINED (0x01, no safety-tagged variant exists) via
 * request_compound.h's shared message_timestamp-repurposing convention (see that
 * header's file comment for the full rationale) -- this module reuses
 * that same wire convention but, like request_triggered.h, does not include or
 * call into request_compound.h itself. This module's own 7-byte sub-field layout
 * for a chain member is chain_length (this chain's total member count,
 * >= 2) and chain_position (this member's own 0-based position within the
 * chain, < chain_length), with the remaining 5 bytes reserved and always
 * encoded/decoded as zero.
 *
 * ── The cs bit: abort-on-error vs. continue-regardless ───────────────────────
 *
 * acf.h's byte_message_info.cs field is round-tripped, but otherwise
 * inert, as of milestone 60 ("belong[s] to functionality this milestone
 * deliberately does not implement"). This module is the first to give
 * that already-published, unmodified field real behavior, exactly the
 * kind of "round-trip now, activate later" precedent request_compound.h's own
 * safety-tagged (MSB) request_type variants already established. A chain
 * member's own cs value (RCP_CHAINED_CS_CONTINUE_ON_ERROR /
 * RCP_CHAINED_CS_ABORT_ON_ERROR) selects, for the chain members still to
 * come, whether that member's own error aborts the rest of the chain or
 * whether execution proceeds regardless -- rcp_chained_advance() below is
 * the pure, directly-testable expression of that sequencing rule.
 *
 * ── Outcomes: CHAIN_ERROR and CHAIN_ABORTED ─────────────────────────────────
 *
 * rcp_chained_member_outcome_t's RCP_CHAINED_MEMBER_CHAIN_ERROR and
 * RCP_CHAINED_MEMBER_CHAIN_ABORTED are this module's own spelling of the
 * roadmap's CHAIN_ERROR (a chain member that was itself executed and
 * itself failed) and CHAIN_ABORTED (a chain member skipped outright
 * because an earlier member's error, combined with
 * RCP_CHAINED_CS_ABORT_ON_ERROR, aborted the remainder of the chain)
 * outcomes -- callers report these as this member's own result exactly as
 * they would any other per-request outcome. This module owns no endpoint
 * dispatch of its own: the caller executes each chain member through
 * whatever mechanism the rest of the server uses for a Standard request,
 * and reports that member's own success/failure back into
 * rcp_chained_advance() to learn what the *next* member's status should
 * be.
 */
#ifndef RCP_REQUEST_CHAINED_H
#define RCP_REQUEST_CHAINED_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── request_type opcode value (no safety-tagged variant) ─────────────────── */

#define RCP_REQUEST_TYPE_CHAINED ((uint8_t)0x01u)

/* ── cs bit semantics, given behavior by this module ─────────────────────── */

#define RCP_CHAINED_CS_CONTINUE_ON_ERROR ((uint8_t)0u)
#define RCP_CHAINED_CS_ABORT_ON_ERROR    ((uint8_t)1u)

/* ── Errors ─────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_CHAINED_OK                       = 0,
    RCP_CHAINED_ERR_SHORT_FRAME          = 1,
    RCP_CHAINED_ERR_BAD_MSG_TYPE         = 2,
    RCP_CHAINED_ERR_NOT_REPURPOSED       = 3, /* decoded mtv != RCP_ACF_MTV_UNTIMED */
    RCP_CHAINED_ERR_UNKNOWN_TYPE         = 4, /* opcode byte is not RCP_REQUEST_TYPE_CHAINED */
    RCP_CHAINED_ERR_TOO_FEW_MEMBERS      = 5, /* chain_length < 2 on encode */
    RCP_CHAINED_ERR_POSITION_OUT_OF_RANGE = 6, /* chain_position >= chain_length on encode */
} rcp_chained_errc_t;

/* Human-readable message for an rcp_chained_errc_t value. Never returns NULL. */
const char *rcp_chained_strerror(rcp_chained_errc_t e);

/* Smallest legal chain_length -- a "chain" of fewer than 2 members is not
 * sequential execution of anything, per the file header. */
#define RCP_CHAINED_MIN_MEMBERS ((uint8_t)2u)

/* ── Chain member encode/decode ───────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed chained-request member addressed to
 * byte_bus_id, packing chain_length/chain_position into the repurposed
 * message_timestamp region (see the file header) with the leading opcode
 * byte set to RCP_REQUEST_TYPE_CHAINED, cs set to one of
 * RCP_CHAINED_CS_CONTINUE_ON_ERROR/_ABORT_ON_ERROR, and mtv forced to
 * RCP_ACF_MTV_UNTIMED -- same conventions as
 * rcp_compound_encode_request() (request_compound.h). payload/payload_len is this
 * member's own opaque, endpoint-specific request data; payload may be
 * NULL iff payload_len == 0. Returns a zeroed rcp_bytes_t (data=NULL) if
 * chain_length < RCP_CHAINED_MIN_MEMBERS, chain_position >= chain_length,
 * payload_len exceeds RCP_ACF_MAX_PAYLOAD, or on allocation failure.
 * Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_chained_encode_member(rcp_byte_bus_id_t byte_bus_id, uint8_t chain_length,
                                       uint8_t chain_position, uint8_t cs, uint8_t transaction_num,
                                       const uint8_t *payload, size_t payload_len);

/* Decodes and validates a chained-request member from b[0..len). Same
 * failure-mode conventions as rcp_compound_decode_request() (request_compound.h),
 * with RCP_CHAINED_ERR_UNKNOWN_TYPE returned whenever the decoded opcode
 * byte is not RCP_REQUEST_TYPE_CHAINED. On RCP_CHAINED_OK,
 * *out_byte_bus_id, *out_chain_length, *out_chain_position, *out_cs, and
 * *out_transaction_num are populated, and *out_payload / *out_payload_len
 * are set to a *borrowed* view into b. This function does not itself
 * enforce chain_length >= RCP_CHAINED_MIN_MEMBERS or chain_position <
 * chain_length -- those are encode-time validations only; a decoder
 * receiving an out-of-range value from the wire reports it faithfully so
 * the caller can decide how to treat a nonconformant peer. */
rcp_chained_errc_t rcp_chained_decode_member(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t *out_byte_bus_id,
                                              uint8_t *out_chain_length, uint8_t *out_chain_position,
                                              uint8_t *out_cs, const uint8_t **out_payload,
                                              size_t *out_payload_len, uint8_t *out_transaction_num);

/* ── Sequencing: the cs-bit-driven abort/continue rule ────────────────────── */

typedef enum {
    RCP_CHAINED_MEMBER_OK            = 0, /* executed without error */
    RCP_CHAINED_MEMBER_CHAIN_ERROR   = 1, /* this member itself errored (CHAIN_ERROR) */
    RCP_CHAINED_MEMBER_CHAIN_ABORTED = 2, /* skipped: an earlier member's
                                              error, combined with
                                              RCP_CHAINED_CS_ABORT_ON_ERROR,
                                              aborted the rest of the chain
                                              (CHAIN_ABORTED) */
} rcp_chained_member_outcome_t;

/* Advances one chain's sequencing state by one member, in chain order.
 * *chain_aborted must start false before a chain's first member and is
 * this function's own accumulated "abort the rest" state, carried by the
 * caller from one call to the next across a chain's members.
 *
 * If *chain_aborted is already true (an earlier member both errored and
 * selected RCP_CHAINED_CS_ABORT_ON_ERROR), this member must not be
 * executed at all -- the caller passes member_errored/cs as don't-cares
 * and this function returns RCP_CHAINED_MEMBER_CHAIN_ABORTED without
 * consulting them, leaving *chain_aborted unchanged (still true).
 *
 * Otherwise the caller has already executed this member and reports its
 * own member_errored/cs here: if !member_errored, returns
 * RCP_CHAINED_MEMBER_OK and leaves *chain_aborted false. If
 * member_errored, returns RCP_CHAINED_MEMBER_CHAIN_ERROR, and sets
 * *chain_aborted to true iff cs == RCP_CHAINED_CS_ABORT_ON_ERROR (a
 * member with cs == RCP_CHAINED_CS_CONTINUE_ON_ERROR that itself errors
 * leaves *chain_aborted false -- the chain proceeds to its next member
 * regardless). */
rcp_chained_member_outcome_t rcp_chained_advance(bool *chain_aborted, bool member_errored,
                                                   uint8_t cs);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_CHAINED_H */
