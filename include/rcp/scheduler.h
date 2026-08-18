/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-SCHED-001
//cfusa:req REQ-SCHED-002
//cfusa:req REQ-SCHED-003
//cfusa:req REQ-SCHED-004
//cfusa:req REQ-SCHED-005
//cfusa:req REQ-SCHED-006
//cfusa:req REQ-SCHED-007
//cfusa:req REQ-SCHED-008
/*
 * scheduler.h -- Server-side request-kind execution priority and
 * multi-request-per-frame evaluation for the TC18 Remote Control Protocol
 * wire layer (ROADMAP.md Phase 17, "Conditional Requests & Sequencers",
 * milestone 69).
 *
 * This is new, additive protocol-core surface. It composes request.h (the
 * conditional-request taxonomy from milestones 68-69, unified from five
 * originally-separate per-kind files into one module by c-RCP-165) purely
 * to classify an already-decoded request_type byte --
 * it does not touch rcp.h, wire.c, avtp.h/avtp.c, server.h/server.c,
 * regmap.h/regmap.c, or any ep_* endpoint module, and it reads (not
 * modifies) acf.h's already-published ACF_ABB/ACF_GBB header layout to
 * split a multi-message AVTPDU payload into its individual members --
 * the same layering discipline every module since milestone 64 has
 * followed.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── Execution priority ordering ─────────────────────────────────────────────
 *
 * This is the first genuine server-side scheduler property in this
 * project's history (per ROADMAP.md's own milestone-69 framing): request
 * *kind* alone determines execution priority --
 * cancellation > triggered > timed > compound > compound-wait > chained >
 * standard -- with FIFO tie-breaking among requests of equal kind.
 * rcp_sched_kind_rank() is the pure lookup from rcp_sched_kind_t to a
 * numeric rank (higher services first); rcp_sched_compare() combines that
 * rank with a caller-assigned monotonic arrival sequence number to
 * produce one total ordering a priority-queue-shaped caller can sort or
 * heap-order requests by.
 *
 * This is also the direct, spec-conformant replacement for the old
 * prioqueue.h/prioqueue.c's client-side priority heap (see
 * ROADMAP.md's Satellite Package Disposition table): that module is
 * DEPRECATE-dispositioned, not removed by this milestone (its own
 * removal is scheduled for a later "Deprecation batch" milestone per
 * ROADMAP.md's Legacy Release Plan) -- this module is new protocol-core
 * surface standing in for it going forward, not a rewrite of its
 * internals.
 *
 * ── Multi-request-per-frame handling ─────────────────────────────────────────
 *
 * rcp_sched_split_frame_members() walks an already AVTPDU-header-stripped
 * ACF payload stream (the same byte range avtp.c's own
 * rcp_avtp_decode_ntscf()/_tscf() already hand back as *out_payload) and
 * reports the byte offset of every individual ACF message packed within
 * it, using only acf.h's own already-published acf_msg_type/
 * acf_msg_length fields -- each member is then decoded and evaluated
 * independently by whichever of this project's request-kind modules its
 * own repurposed opcode byte (or lack thereof, for a plain Standard
 * request) identifies, per this milestone's roadmap scope.
 *
 * rcp_sched_frame_timing_consistent() is the pure, directly-testable
 * expression of this same scope's one-presentation-time-per-TSCF-frame
 * rule: a TSCF-headed AVTPDU's single avtp_timestamp applies uniformly to
 * every member packed inside it, so a frame mixing a Timed (0x0A,
 * request.h) request with any non-Timed member is never well-formed --
 * either every member in a TSCF frame is itself Timed, or none are.
 * NTSCF frames carry no shared presentation time at all and are exempt
 * from this rule entirely.
 */
#ifndef RCP_SCHEDULER_H
#define RCP_SCHEDULER_H

#include "rcp/acf.h"
#include "rcp/rcp.h"
#include "rcp/request.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Request kind classification ─────────────────────────────────────────── */

typedef enum {
    RCP_SCHED_KIND_STANDARD      = 0, /* no repurposed opcode: a plain
                                          read/write/acknowledge ACF
                                          message */
    RCP_SCHED_KIND_CHAINED       = 1,
    RCP_SCHED_KIND_COMPOUND_WAIT = 2,
    RCP_SCHED_KIND_COMPOUND      = 3,
    RCP_SCHED_KIND_TIMED         = 4,
    RCP_SCHED_KIND_TRIGGERED     = 5,
    RCP_SCHED_KIND_CANCELLATION  = 6, /* clear-all / clear-non-safestate /
                                          clear-single, all three */
} rcp_sched_kind_t;

/* Classifies an already-decoded repurposed-opcode byte (see request.h's
 * file header for the shared message_timestamp-repurposing convention
 * every non-Standard request type in this milestone and milestone 68
 * uses) into its rcp_sched_kind_t. is_repurposed must be true iff the
 * message this opcode byte came from actually had mtv ==
 * RCP_ACF_MTV_UNTIMED (i.e. one of this milestone's or milestone 68's
 * decode_* functions returned other than *_ERR_NOT_REPURPOSED for it);
 * when is_repurposed is false, request_type is ignored and this always
 * returns RCP_SCHED_KIND_STANDARD. */
rcp_sched_kind_t rcp_sched_classify(bool is_repurposed, uint8_t request_type);

/* Numeric execution-priority rank for kind -- higher services first.
 * cancellation > triggered > timed > compound > compound-wait > chained >
 * standard, per the file header; equal ranks are never assigned to
 * different kinds (each rcp_sched_kind_t value maps to its own unique
 * rank). */
uint8_t rcp_sched_kind_rank(rcp_sched_kind_t kind);

/* ── Total ordering: rank, then FIFO ─────────────────────────────────────── */

typedef struct {
    rcp_sched_kind_t kind;
    uint64_t         sequence; /* caller-assigned monotonic arrival order,
                                   for FIFO tie-breaking among equal-rank
                                   entries -- this module owns no sequence
                                   counter of its own */
} rcp_sched_entry_t;

/* Total-order comparator: negative if a must be serviced strictly before
 * b, positive if strictly after, zero only if kind and sequence are both
 * equal (a caller assigning unique sequence numbers per request never
 * observes zero from two distinct requests). Higher
 * rcp_sched_kind_rank() services first; among equal ranks, the lower
 * sequence (earlier arrival) services first. */
int rcp_sched_compare(const rcp_sched_entry_t *a, const rcp_sched_entry_t *b);

/* ── Multi-request-per-frame handling ─────────────────────────────────────── */

/* Walks an ACF payload stream b[0..len) (as carried, post-AVTPDU-header,
 * by a single NTSCF or TSCF frame) and reports the byte offset of every
 * individual ACF_ABB/ACF_GBB message packed within it into out_offsets
 * (up to out_cap entries -- additional members beyond out_cap are still
 * counted in the return value but not written, the usual C "ask first,
 * then size a buffer" idiom). Returns the number of members found, or 0
 * if b is empty, the very first byte is neither RCP_ACF_MSG_TYPE_ABB nor
 * RCP_ACF_MSG_TYPE_GBB, or any member's own declared header-plus-payload
 * length would run past b's end (a malformed stream -- this function
 * never reports a partial member). out_offsets may be NULL iff out_cap
 * == 0. */
size_t rcp_sched_split_frame_members(const uint8_t *b, size_t len, size_t *out_offsets,
                                      size_t out_cap);

/* True iff a TSCF-headed AVTPDU carrying count members, each member's own
 * "is this member itself a Timed (0x0A) request" flag given by
 * member_is_timed[0..count), is well-formed under the
 * one-presentation-time-per-frame rule: either every entry in
 * member_is_timed is true, or every entry is false. count == 0 is
 * trivially consistent (true). is_tscf must be true iff the AVTPDU these
 * members came from actually carried a TSCF header (avtp.h's
 * RCP_AVTP_SUBTYPE_TSCF) -- an NTSCF frame carries no shared presentation
 * time at all and is exempt from this rule, so this always returns true
 * when is_tscf is false, regardless of member_is_timed's contents. */
bool rcp_sched_frame_timing_consistent(bool is_tscf, const bool *member_is_timed, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SCHEDULER_H */
