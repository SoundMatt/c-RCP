/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-CMP-001
//cfusa:req REQ-CMP-002
//cfusa:req REQ-CMP-003
//cfusa:req REQ-CMP-004
//cfusa:req REQ-CMP-005
//cfusa:req REQ-CMP-006
//cfusa:req REQ-CMP-007
//cfusa:req REQ-CMP-008
//cfusa:req REQ-CMP-009
//cfusa:req REQ-CMP-010
//cfusa:req REQ-CMP-011
//cfusa:req REQ-CMP-012
//cfusa:req REQ-CMP-013
//cfusa:req REQ-CMP-014
//cfusa:req REQ-CMP-015
//cfusa:req REQ-CMP-016
//cfusa:req REQ-CMP-017
//cfusa:req REQ-CMP-018
//cfusa:req REQ-CMP-019
//cfusa:req REQ-CMP-020
//cfusa:req REQ-CMP-021
//cfusa:req REQ-CMP-022
//cfusa:req REQ-CMP-023
//cfusa:req REQ-CMP-024
//cfusa:req REQ-CMP-025
//cfusa:req REQ-CMP-026
//cfusa:req REQ-CMP-027
/*
 * request_compound.h -- Compound / compound-wait conditional requests and the
 * clear-non-safestate cancellation request type for the TC18 Remote
 * Control Protocol wire layer (ROADMAP.md Phase 17, "Conditional Requests &
 * Sequencers", milestone 68).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), and this same milestone's own sequencer-state
 * primitive (request_sequencer.h/sequencer.c). Nothing in rcp.h, wire.c, avtp.h/
 * avtp.c, acf.h/acf.c, server.h/server.c, regmap.h/regmap.c, or any ep_*
 * endpoint module is touched here -- the same layering discipline every
 * module since milestone 64 has followed.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── request_type: the message_timestamp-repurposing trick ──────────────────
 *
 * acf.h's ACF_GBB variant carries an 8-byte message_timestamp field,
 * meaningful only when its byte_message_info.mtv is RCP_ACF_MTV_VALID
 * (acf.h). This milestone gives a second meaning to that same 8-byte
 * region when mtv is instead RCP_ACF_MTV_UNTIMED (0): its first byte is
 * reinterpreted as a request_type opcode identifying which non-Standard
 * request kind this message carries, and its remaining 7 bytes as that
 * kind's own sub-fields -- this module's own byte-level packing of those 7
 * bytes (see rcp_compound_step_t below), not reproduced from the
 * specification.
 *
 * Because acf.c's own rcp_acf_encode_gbb() deliberately forces that same
 * 8-byte region to all-zero whenever mtv == RCP_ACF_MTV_UNTIMED (a rule
 * written at milestone 60, before this repurposing existed, and left
 * unchanged here per this milestone's own layering discipline -- acf.c is
 * not touched by this module), the encoders below build the ACF_GBB frame
 * directly against acf.h's already-published, fixed byte_message_info
 * layout rather than calling rcp_acf_encode_gbb(). Decoding has no such
 * conflict: rcp_acf_decode_gbb() never modifies message_timestamp based on
 * mtv, so this module decodes through it unmodified and reinterprets the
 * result itself.
 *
 * request_type values this module recognizes (every other request_type
 * byte this shared convention carries is a peer module's own, landed
 * alongside this one at milestone 69: chained 0x01 -- request_chained.h, clear-all
 * 0x05 and clear-single 0x07 -- request_cancel.h, timed 0x0A -- request_timed.h, and
 * triggered 0x0E/0x8E -- request_triggered.h):
 *
 *   0x06       clear-non-safestate (this module's own cancellation
 *              request; no safety-tagged counterpart of its own)
 *   0x0B/0x8B  compound-wait / compound-wait, safety-tagged
 *   0x0F/0x8F  compound / compound, safety-tagged
 *
 * The safety-tagged (MSB-set) variants are round-tripped by this
 * milestone's encode/decode functions exactly like any other request_type
 * value -- gating their execution on the endpoint's configured safe state
 * is Phase 18's job (e2e.h, milestone 70), not this one's, mirroring
 * acf.h's own precedent of round-tripping a field before its full behavior
 * is implemented elsewhere.
 *
 * ── rcp_compound_step_t: one shared sub-field shape ─────────────────────────
 *
 * Compound and compound-wait requests share one on-wire sub-field shape,
 * rcp_compound_step_t -- the specification defines the two request kinds
 * with identical sub-field widths and offsets (only the field-name prefix
 * differs, cmp_ vs cmpw_), so one struct models both.
 *
 * ── wire sub-field layout ───────────────────────────────────────────────────
 *
 * The eight octets of the repurposed message_timestamp region carry, in
 * order (offsets relative to the start of that region):
 *
 *   offset 0  request_type   (the opcode octet, 0x0F/0x8F or 0x0B/0x8B)
 *   offset 1  start_state    (cmp_start_state  / cmpw_start_state)
 *   offset 2  next_state     (cmp_next_state   / cmpw_next_state)
 *   offset 3  sequencer_index(cmp_sequencer    / cmpw_sequencer)
 *   offset 4  exec_delay     (cmp_exec_delay   / cmpw_exec_delay), two
 *             offset 5       octets, big-endian
 *   offset 6  repeat_count   (cmp_repetitions  / cmpw_repetitions), two
 *             offset 7       octets, big-endian
 *
 * All three single-octet sub-fields are exactly one octet wide -- notably
 * sequencer_index, which addresses at most 256 sequencers, and which this
 * module carried as a 16-bit field at the wrong offset before v0.102.0.
 * exec_delay is counted in multiples of the addressed endpoint's own
 * configured ep_delay_time, not in milliseconds; repeat_count's
 * all-ones value (RCP_COMPOUND_REPEAT_INFINITE) is the infinite-repetition
 * sentinel, and a repeat_count of zero at the end of an execution means the
 * request is removed from the endpoint's request store.
 *
 * ── The advance-only-if-still-in-start_state guard ──────────────────────────
 *
 * rcp_compound_advance_guard() is the pure, directly-testable expression
 * of extraction §3.14's rule: a compound (or compound-wait) step only ever
 * advances its target sequencer if that sequencer is still sitting in the
 * step's own start_state at the moment its execution condition is
 * satisfied -- a sequencer some other request already moved away from
 * start_state is left alone, never force-advanced. rcp_compound_tick()
 * composes that guard with compound's own unconditional-after-the-delay
 * timer; rcp_compound_wait_tick() composes it instead with a
 * caller-supplied match condition (see below).
 *
 * ── compound-wait: a generic guard, not a comparison engine of its own ──────
 *
 * This module owns no endpoint-specific comparison logic itself. Per
 * ROADMAP.md's own instruction, rcp_compound_wait_tick() below takes an
 * already-evaluated condition_met bool, expecting the caller to have
 * produced it via an endpoint type's own comparison-mode helper (e.g.
 * ep_spi.h's rcp_ep_spi_compound_wait_status_equal(), ep_pwm.h's
 * rcp_ep_pwm_in_compound_wait_compare()) -- exactly the "isolated
 * precedent" those two helpers were built ahead of time to be consumed by.
 * Neither this module nor rcp_compound_wait_tick() owns a timer, thread,
 * or polling loop of its own -- every tick is caller-driven, matching this
 * project's established convention (ep_adc.h's averaging functions, etc.)
 * for every protocol-core module built so far.
 */
#ifndef RCP_REQUEST_COMPOUND_H
#define RCP_REQUEST_COMPOUND_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/request_sequencer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── request_type opcode values ────────────────────────────────────────────── */

#define RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE  ((uint8_t)0x06u)
#define RCP_REQUEST_TYPE_COMPOUND_WAIT        ((uint8_t)0x0Bu)
#define RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY ((uint8_t)0x8Bu)
#define RCP_REQUEST_TYPE_COMPOUND             ((uint8_t)0x0Fu)
#define RCP_REQUEST_TYPE_COMPOUND_SAFETY      ((uint8_t)0x8Fu)

/* True iff request_type's MSB (0x80) is set -- meaningful only for
 * RCP_REQUEST_TYPE_COMPOUND[_WAIT]; RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE
 * has no safety-tagged counterpart of its own, see the file header. */
bool rcp_request_type_is_safety(uint8_t request_type);

/* True iff request_type is RCP_REQUEST_TYPE_COMPOUND or
 * RCP_REQUEST_TYPE_COMPOUND_SAFETY. */
bool rcp_request_type_is_compound(uint8_t request_type);

/* True iff request_type is RCP_REQUEST_TYPE_COMPOUND_WAIT or
 * RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY. */
bool rcp_request_type_is_compound_wait(uint8_t request_type);

/* ── Errors ─────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_COMPOUND_OK                 = 0,
    RCP_COMPOUND_ERR_SHORT_FRAME    = 1,
    RCP_COMPOUND_ERR_BAD_MSG_TYPE   = 2,
    RCP_COMPOUND_ERR_NOT_REPURPOSED = 3, /* decoded mtv != RCP_ACF_MTV_UNTIMED */
    RCP_COMPOUND_ERR_UNKNOWN_TYPE   = 4, /* first sub-field byte is not a
                                             request_type this module
                                             recognizes for the function
                                             called */
    RCP_COMPOUND_ERR_RESERVED_NONZERO = 5, /* a reserved message_timestamp
                                                sub-field octet carries a
                                                set bit (REQ-CMP-028) */
    RCP_COMPOUND_ERR_EVT_HS_CS_NONZERO = 6, /* the ACF byte_message_info
                                                 header's evt[2:0], hs, or
                                                 cs bits are set -- TC18
                                                 Table 14 requires all
                                                 three be zero for
                                                 clear-non-safestate,
                                                 rejecting with wire error
                                                 UNSUPPORTED_CMD
                                                 (REQ-CMP-029) */
} rcp_compound_errc_t;

/* Human-readable message for an rcp_compound_errc_t value. Never returns NULL. */
const char *rcp_compound_strerror(rcp_compound_errc_t e);

/* ── Dispatch ───────────────────────────────────────────────────────────────── */

/* Reads just the repurposed request_type byte (offset 8, the first byte of
 * the message_timestamp region) from a received ACF_GBB message, so a
 * caller can dispatch to the right decode_* function below without a full
 * decode attempt first -- mirroring acf.h's own rcp_acf_peek_msg_type().
 * Fails with RCP_COMPOUND_ERR_SHORT_FRAME if b is shorter than
 * RCP_ACF_GBB_HEADER_LEN, RCP_COMPOUND_ERR_BAD_MSG_TYPE if b[0] is not
 * RCP_ACF_MSG_TYPE_GBB, or RCP_COMPOUND_ERR_NOT_REPURPOSED if the decoded
 * mtv nibble is not RCP_ACF_MTV_UNTIMED -- none of this module's request
 * types are ever carried any other way. */
rcp_compound_errc_t rcp_compound_peek_request_type(const uint8_t *b, size_t len,
                                                    uint8_t *out_request_type);

/* ── rcp_compound_step_t: shared compound/compound-wait sub-fields ──────────── */

/* Sentinel repeat_count value meaning "repeat indefinitely": the
 * all-ones value of the 2-octet repetition sub-field, per the
 * specification's own definition of that field. A request carrying it is
 * never decremented at the end of an execution and is never removed from
 * the endpoint's request store on repetition-exhaustion grounds. */
#define RCP_COMPOUND_REPEAT_INFINITE ((uint16_t)0xFFFFu)

/* The five sub-field octets of a compound/compound-wait request, in wire
 * order after the leading opcode octet: start_state, next_state,
 * sequencer_index (one octet each), then exec_delay and repeat_count (two
 * big-endian octets each). See the "wire sub-field layout" section of the
 * file header for the octet offsets these map to. */
typedef struct {
    uint8_t  start_state;     /* the sequencer state this step requires (see
                                  rcp_compound_advance_guard()) */
    uint8_t  next_state;      /* the state this step advances the sequencer to */
    uint8_t  sequencer_index; /* which of a table's sequencers this step targets */
    uint16_t exec_delay;      /* cmp_exec_delay / cmpw_exec_delay, counted in
                                  multiples of the addressed endpoint's own
                                  configured ep_delay_time -- NOT milliseconds;
                                  see the file header */
    uint16_t repeat_count;    /* remaining repetitions; RCP_COMPOUND_REPEAT_INFINITE
                                  means never decrement, 0 means "this execution
                                  is the last" */
} rcp_compound_step_t;

/* ── Compound / compound-wait request encode/decode ──────────────────────────── */

/* Encodes an ACF_GBB-framed compound or compound-wait request addressed to
 * byte_bus_id, packing step into the repurposed message_timestamp region's
 * 7 sub-field bytes (see the file header) with the leading opcode byte set
 * to request_type and mtv forced to RCP_ACF_MTV_UNTIMED (0) -- the
 * repurposing trick only applies when mtv == 0, so this encoder always
 * writes it that way regardless of what an ordinary ACF_GBB message's mtv
 * might otherwise carry. request_type must be one of
 * RCP_REQUEST_TYPE_COMPOUND[_SAFETY] or
 * RCP_REQUEST_TYPE_COMPOUND_WAIT[_SAFETY] -- use
 * rcp_compound_encode_clear_non_safestate() for the payload-free 0x06
 * cancellation request instead. payload/payload_len is this request's own
 * opaque, endpoint-specific data -- for a compound-wait request, this is
 * TC18 §13.5.1's byte_msg_payload, the comparison target
 * rcp_acf_compound_wait_match() compares against an endpoint's current
 * status, dispatched by evt (see below); payload may be NULL iff
 * payload_len == 0. evt is the ACF header's own evt field
 * (byte_message_info.evt, NOT one of rcp_compound_step_t's repurposed
 * sub-fields): for a compound-wait request it selects the comparison mode
 * per §13.5.1 (acf.h's rcp_acf_compound_wait_evt_valid()/_match()); a
 * plain (non-wait) compound request has no comparison of its own and
 * should pass 0. Returns a zeroed
 * rcp_bytes_t (data=NULL) if request_type is not recognized, payload_len
 * exceeds RCP_ACF_MAX_PAYLOAD, or on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_compound_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                         const rcp_compound_step_t *step, uint8_t evt,
                                         uint8_t transaction_num,
                                         const uint8_t *payload, size_t payload_len);

/* Decodes and validates an ACF-level compound or compound-wait request
 * from b[0..len). Fails with RCP_COMPOUND_ERR_SHORT_FRAME (b shorter than
 * the ACF_GBB fixed header or its declared payload length),
 * RCP_COMPOUND_ERR_BAD_MSG_TYPE (b is not an ACF_GBB message),
 * RCP_COMPOUND_ERR_NOT_REPURPOSED (decoded mtv != RCP_ACF_MTV_UNTIMED), or
 * RCP_COMPOUND_ERR_UNKNOWN_TYPE (the decoded opcode byte is not
 * rcp_request_type_is_compound()/_is_compound_wait()). On
 * RCP_COMPOUND_OK, *out_request_type, *out_byte_bus_id, *out_step,
 * *out_evt (the ACF header's own evt field -- see
 * rcp_compound_encode_request()'s own doc comment), and
 * *out_transaction_num are populated, and *out_payload / *out_payload_len
 * are set to a *borrowed* view into b (not copied -- matching acf.c's own
 * decode_* convention) of this request's opaque payload. */
rcp_compound_errc_t rcp_compound_decode_request(const uint8_t *b, size_t len,
                                                 uint8_t *out_request_type,
                                                 rcp_byte_bus_id_t *out_byte_bus_id,
                                                 rcp_compound_step_t *out_step,
                                                 uint8_t *out_evt,
                                                 const uint8_t **out_payload, size_t *out_payload_len,
                                                 uint8_t *out_transaction_num);

/* ── clear-non-safestate (0x06) ──────────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed clear-non-safestate cancellation request
 * addressed to byte_bus_id, with the repurposed message_timestamp
 * region's opcode byte set to RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE and its
 * remaining 7 sub-field bytes zeroed (this request type carries none of
 * its own -- see the file header) and mtv forced to RCP_ACF_MTV_UNTIMED,
 * same convention as rcp_compound_encode_request(). Returns a zeroed
 * rcp_bytes_t (data=NULL) only on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_compound_encode_clear_non_safestate(rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t transaction_num);

/* Decodes and validates an ACF-level clear-non-safestate request from
 * b[0..len). Same failure modes as rcp_compound_decode_request(), with
 * RCP_COMPOUND_ERR_UNKNOWN_TYPE returned whenever the decoded opcode byte
 * is not RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE,
 * RCP_COMPOUND_ERR_RESERVED_NONZERO when any of message_timestamp's 7
 * trailing octets carries a set bit (REQ-CMP-028), and
 * RCP_COMPOUND_ERR_EVT_HS_CS_NONZERO when evt[2:0], hs, or cs is nonzero
 * (TC18 Table 14; REQ-CMP-029). On RCP_COMPOUND_OK, *out_byte_bus_id and
 * *out_transaction_num are populated. */
rcp_compound_errc_t rcp_compound_decode_clear_non_safestate(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t *out_byte_bus_id,
                                                             uint8_t *out_transaction_num);

/* ── The advance-only-if-still-in-start_state guard, delay timer, and tick ──── */

/* True iff table's sequencer step->sequencer_index is currently sitting in
 * step->start_state -- extraction §3.14's advance-only-if-still-in-
 * cmp_start_state guard, in this module's own pure, directly-testable
 * form. False (never a fabricated true) if step->sequencer_index is not
 * rcp_sequencer_index_valid() for table. */
bool rcp_compound_advance_guard(const rcp_sequencer_table_t *table,
                                 const rcp_compound_step_t *step);

/* True iff this step's *start* condition is satisfied, i.e. the request
 * may begin: a start_state of zero starts the request in whatever state
 * the sequencer currently holds, and any other start_state requires the
 * sequencer to actually be sitting in it. Either way the addressed
 * sequencer must exist (rcp_sequencer_index_valid()) -- a request naming
 * a sequencer this server does not have is never started, which is this
 * module's own modelling of a disabled sequencer prohibiting execution.
 *
 * This is deliberately *not* the same predicate as
 * rcp_compound_advance_guard(): the start condition decides whether the
 * request runs at all, while the advance guard decides whether the
 * sequencer is moved to next_state afterwards. For a start_state of zero
 * the two differ -- the request starts in any state, but only advances
 * the sequencer if it happens to still be in state zero. */
bool rcp_compound_start_condition_met(const rcp_sequencer_table_t *table,
                                       const rcp_compound_step_t *step);

/* True iff elapsed >= step->exec_delay, i.e. this step's
 * cmp_exec_delay/cmpw_exec_delay timer has elapsed. elapsed is counted in
 * the same unit as step->exec_delay itself -- multiples of the addressed
 * endpoint's configured ep_delay_time, see the file header. Pure; owns no
 * timer or clock of its own -- callers track elapsed themselves, matching
 * this project's established convention. */
bool rcp_compound_exec_delay_elapsed(const rcp_compound_step_t *step, uint32_t elapsed);

/* Compound's own tick: advances table's sequencer step->sequencer_index to
 * step->next_state, and returns true, iff both
 * rcp_compound_exec_delay_elapsed(step, elapsed) and
 * rcp_compound_advance_guard(table, step) hold; otherwise table is left
 * entirely unchanged and this returns false.
 *
 * A next_state of zero is the "remain in the current state" sentinel: the
 * sequencer is left exactly where it is, and this still returns true --
 * the request executed, it simply advanced nothing. */
bool rcp_compound_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                        uint32_t elapsed);

/* Compound-wait's own tick: advances table's sequencer
 * step->sequencer_index to step->next_state, and returns true, iff both
 * condition_met (the caller's own already-evaluated comparison result --
 * see the file header) and rcp_compound_advance_guard(table, step) hold;
 * otherwise table is left entirely unchanged and this returns false.
 * Unlike rcp_compound_tick(), elapsing exec_delay_ms alone is never
 * sufficient here -- callers that want to give up on an unmatched wait
 * once its timer elapses do so themselves, using
 * rcp_compound_exec_delay_elapsed() directly; this function only ever
 * advances on a genuine condition match. A next_state of zero is the same
 * "remain in the current state" sentinel rcp_compound_tick() honours. */
bool rcp_compound_wait_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                             bool condition_met);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_COMPOUND_H */
