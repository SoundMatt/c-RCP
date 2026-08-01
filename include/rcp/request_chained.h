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
//cfusa:req REQ-CHAIN-011
//cfusa:req REQ-CHAIN-012
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
 * call into request_compound.h itself.
 *
 * ── wire sub-field layout ───────────────────────────────────────────────────
 *
 * A chain member carries exactly one sub-field of its own. The eight
 * octets of the repurposed message_timestamp region hold, in order
 * (offsets relative to the start of that region):
 *
 *   offset 0     request_type     (the opcode octet, 0x01)
 *   offsets 1..3 reserved         (all bits zero)
 *   offsets 4..5 chain_exec_delay (two octets, big-endian)
 *   offsets 6..7 reserved         (all bits zero)
 *
 * Before v0.102.0 this module invented chain_length and chain_position
 * sub-fields at offsets 1 and 2, which both overwrote octets the
 * specification mandates be transmitted as zero and omitted
 * chain_exec_delay entirely. Neither invented field is needed: a chain is
 * defined positionally, by consecutive members of a single AVTPDU, so a
 * member's position and the chain's length are properties of the enclosing
 * frame rather than of any member's own sub-fields. A chain starts at the
 * first non-chained request in the frame and extends across every
 * immediately following chained member; a chained member appearing as the
 * frame's very first request has no predecessor to chain to and is
 * therefore rejected.
 *
 * The reserved octets are not merely conventionally zero: a received chain
 * member carrying any set bit in them is rejected outright
 * (RCP_CHAINED_ERR_RESERVED_NONZERO).
 *
 * ── The cs bit: abort-on-error vs. continue-regardless ───────────────────────
 *
 * acf.h's byte_message_info.cs field is round-tripped, but otherwise
 * inert, as of milestone 60 ("belong[s] to functionality this milestone
 * deliberately does not implement"). This module is the first to give
 * that already-published, unmodified field real behavior, exactly the
 * kind of "round-trip now, activate later" precedent request_compound.h's own
 * safety-tagged (MSB) request_type variants already established.
 *
 * cs is a *conditional start* selector, and it is read on the member
 * about to run, about the member that just ran:
 * RCP_CHAINED_CS_CONTINUE_ON_ERROR means this member executes even if its
 * predecessor returned an error, while RCP_CHAINED_CS_ABORT_ON_ERROR
 * means this member does not execute at all when its predecessor errored
 * -- and, as a consequence, neither does the remainder of the chain.
 * Before v0.102.0 this module read cs off the member that *errored*
 * rather than off its successor, which inverted control of the
 * abort decision: a failing member could veto its own successors instead
 * of each successor deciding for itself whether to proceed.
 * rcp_chained_advance() below is the pure, directly-testable expression of
 * the corrected rule.
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
    RCP_CHAINED_OK                   = 0,
    RCP_CHAINED_ERR_SHORT_FRAME      = 1,
    RCP_CHAINED_ERR_BAD_MSG_TYPE     = 2,
    RCP_CHAINED_ERR_NOT_REPURPOSED   = 3, /* decoded mtv != RCP_ACF_MTV_UNTIMED */
    RCP_CHAINED_ERR_UNKNOWN_TYPE     = 4, /* opcode byte is not RCP_REQUEST_TYPE_CHAINED */
    RCP_CHAINED_ERR_RESERVED_NONZERO = 5, /* a reserved sub-field octet carries a set bit */
} rcp_chained_errc_t;

/* Human-readable message for an rcp_chained_errc_t value. Never returns NULL. */
const char *rcp_chained_strerror(rcp_chained_errc_t e);

/* ── Chain member encode/decode ───────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed chained-request member addressed to
 * byte_bus_id, packing chain_exec_delay into the repurposed
 * message_timestamp region's octets 4..5 and leaving every reserved octet
 * of that region all-zero (see the file header), with the leading opcode
 * byte set to RCP_REQUEST_TYPE_CHAINED, cs set to one of
 * RCP_CHAINED_CS_CONTINUE_ON_ERROR/_ABORT_ON_ERROR, and mtv forced to
 * RCP_ACF_MTV_UNTIMED -- same conventions as
 * rcp_compound_encode_request() (request_compound.h). chain_exec_delay is
 * counted in multiples of the addressed endpoint's configured
 * ep_delay_time, measured from the moment the predecessor request
 * finalized. payload/payload_len is this member's own opaque,
 * endpoint-specific request data; payload may be NULL iff payload_len ==
 * 0. Returns a zeroed rcp_bytes_t (data=NULL) if payload_len exceeds
 * RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_chained_encode_member(rcp_byte_bus_id_t byte_bus_id, uint16_t chain_exec_delay,
                                       uint8_t cs, uint8_t transaction_num,
                                       const uint8_t *payload, size_t payload_len);

/* Decodes and validates a chained-request member from b[0..len). Same
 * failure-mode conventions as rcp_compound_decode_request() (request_compound.h),
 * with RCP_CHAINED_ERR_UNKNOWN_TYPE returned whenever the decoded opcode
 * byte is not RCP_REQUEST_TYPE_CHAINED and RCP_CHAINED_ERR_RESERVED_NONZERO
 * whenever any reserved octet of the repurposed region (offsets 1..3 and
 * 6..7) carries a set bit. On RCP_CHAINED_OK, *out_byte_bus_id,
 * *out_chain_exec_delay, *out_cs, and *out_transaction_num are populated,
 * and *out_payload / *out_payload_len are set to a *borrowed* view into
 * b. */
rcp_chained_errc_t rcp_chained_decode_member(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t *out_byte_bus_id,
                                              uint16_t *out_chain_exec_delay,
                                              uint8_t *out_cs, const uint8_t **out_payload,
                                              size_t *out_payload_len, uint8_t *out_transaction_num);

/* True iff elapsed >= chain_exec_delay, in that field's own unit
 * (multiples of the endpoint's configured ep_delay_time), where elapsed
 * is measured from the moment this member's predecessor finalized. */
bool rcp_chained_exec_delay_elapsed(uint16_t chain_exec_delay, uint32_t elapsed);

/* ── Sequencing: the cs-bit-driven abort/continue rule ────────────────────── */

typedef enum {
    RCP_CHAINED_MEMBER_OK            = 0, /* this member may execute */
    RCP_CHAINED_MEMBER_CHAIN_ERROR   = 1, /* this member has no predecessor to
                                              chain to, so the whole chain is
                                              ignored (CHAIN_ERROR) */
    RCP_CHAINED_MEMBER_CHAIN_ABORTED = 2, /* skipped: its predecessor errored
                                              and this member selected
                                              RCP_CHAINED_CS_ABORT_ON_ERROR,
                                              or an earlier member already
                                              aborted the chain
                                              (CHAIN_ABORTED) */
} rcp_chained_member_outcome_t;

/* Decides, *before* executing one chain member, whether it may run.
 * Called once per chained member in chain order. *chain_aborted must
 * start false before a chain's first member and is this function's own
 * accumulated "abort the rest" state, carried by the caller from one call
 * to the next across a chain's members.
 *
 * has_predecessor is whether any request precedes this member within the
 * same AVTPDU. A chained member appearing as the frame's very first
 * request has nothing to chain to: this returns
 * RCP_CHAINED_MEMBER_CHAIN_ERROR and sets *chain_aborted, so that member
 * and every member after it is ignored.
 *
 * If *chain_aborted is already true, this member must not be executed
 * either -- predecessor_errored/cs are not consulted and
 * RCP_CHAINED_MEMBER_CHAIN_ABORTED is returned.
 *
 * Otherwise predecessor_errored reports whether the immediately preceding
 * request finalized with an error, and cs is *this* member's own
 * conditional-start selector (see the file header). A member with cs ==
 * RCP_CHAINED_CS_ABORT_ON_ERROR whose predecessor errored does not
 * execute: this returns RCP_CHAINED_MEMBER_CHAIN_ABORTED and sets
 * *chain_aborted, ending the chain. Every other combination returns
 * RCP_CHAINED_MEMBER_OK with *chain_aborted left false -- including a
 * member with cs == RCP_CHAINED_CS_CONTINUE_ON_ERROR whose predecessor
 * errored, which proceeds regardless. */
rcp_chained_member_outcome_t rcp_chained_advance(bool *chain_aborted, bool has_predecessor,
                                                   bool predecessor_errored, uint8_t cs);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_CHAINED_H */
