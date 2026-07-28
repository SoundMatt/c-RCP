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
 * rcp_compound_step_t -- this module's own design choice, following
 * ep_pwm.h's exact precedent of PWM_OUT/PWM_IN sharing one payload shape
 * (rcp_ep_pwm_value_t) for two closely related request kinds.
 * sequencer_index selects which of a table's sequencers (request_sequencer.h) this
 * step targets; start_state/next_state are the state-number sub-fields the
 * roadmap's scope calls for; exec_delay_ms is this step's cmp_exec_delay
 * (compound) or cmpw_exec_delay (compound-wait) timer, in milliseconds
 * (this module's own unit choice, the same kind of explicit-unit decision
 * ep_spi.h's inter_byte_delay_ns already sets a precedent for);
 * repeat_count is the repetition-count sub-field, round-tripped by the
 * encode/decode functions below but not itself consumed by any scheduling
 * behavior in this milestone's scope, which extends only to the
 * exec_delay timer and the advance guard -- see
 * rcp_compound_tick()/rcp_compound_wait_tick() below.
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

/* Sentinel repeat_count value meaning "repeat indefinitely" -- this
 * module's own 1-byte-field analogue of Triggered's (milestone 69)
 * 0xFFFF infinite-repeat sentinel, scaled to this module's own 1-byte
 * repeat_count sub-field. Round-tripped only; see the file header. */
#define RCP_COMPOUND_REPEAT_INFINITE ((uint8_t)0xFFu)

typedef struct {
    uint16_t sequencer_index; /* which of a table's sequencers this step targets */
    uint8_t  start_state;     /* the sequencer state this step requires (see
                                  rcp_compound_advance_guard()) */
    uint8_t  next_state;      /* the state this step advances the sequencer to */
    uint16_t exec_delay_ms;   /* cmp_exec_delay / cmpw_exec_delay, in
                                  milliseconds -- see the file header */
    uint8_t  repeat_count;    /* round-tripped only; see the file header */
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
 * opaque, endpoint-specific data (e.g. a compound-wait's comparison target
 * bytes); payload may be NULL iff payload_len == 0. Returns a zeroed
 * rcp_bytes_t (data=NULL) if request_type is not recognized, payload_len
 * exceeds RCP_ACF_MAX_PAYLOAD, or on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_compound_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                         const rcp_compound_step_t *step, uint8_t transaction_num,
                                         const uint8_t *payload, size_t payload_len);

/* Decodes and validates an ACF-level compound or compound-wait request
 * from b[0..len). Fails with RCP_COMPOUND_ERR_SHORT_FRAME (b shorter than
 * the ACF_GBB fixed header or its declared payload length),
 * RCP_COMPOUND_ERR_BAD_MSG_TYPE (b is not an ACF_GBB message),
 * RCP_COMPOUND_ERR_NOT_REPURPOSED (decoded mtv != RCP_ACF_MTV_UNTIMED), or
 * RCP_COMPOUND_ERR_UNKNOWN_TYPE (the decoded opcode byte is not
 * rcp_request_type_is_compound()/_is_compound_wait()). On
 * RCP_COMPOUND_OK, *out_request_type, *out_byte_bus_id, *out_step, and
 * *out_transaction_num are populated, and *out_payload / *out_payload_len
 * are set to a *borrowed* view into b (not copied -- matching acf.c's own
 * decode_* convention) of this request's opaque payload. */
rcp_compound_errc_t rcp_compound_decode_request(const uint8_t *b, size_t len,
                                                 uint8_t *out_request_type,
                                                 rcp_byte_bus_id_t *out_byte_bus_id,
                                                 rcp_compound_step_t *out_step,
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
 * is not RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE. On RCP_COMPOUND_OK,
 * *out_byte_bus_id and *out_transaction_num are populated. */
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

/* True iff elapsed_ms >= step->exec_delay_ms, i.e. this step's
 * cmp_exec_delay/cmpw_exec_delay timer has elapsed. Pure; owns no timer or
 * clock of its own -- callers track elapsed_ms themselves (e.g. via
 * clock.h's rcp_monotonic_ms()), matching this project's established
 * convention -- see the file header. */
bool rcp_compound_exec_delay_elapsed(const rcp_compound_step_t *step, uint32_t elapsed_ms);

/* Compound's own tick: advances table's sequencer step->sequencer_index to
 * step->next_state, and returns true, iff both
 * rcp_compound_exec_delay_elapsed(step, elapsed_ms) and
 * rcp_compound_advance_guard(table, step) hold; otherwise table is left
 * entirely unchanged and this returns false. */
bool rcp_compound_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                        uint32_t elapsed_ms);

/* Compound-wait's own tick: advances table's sequencer
 * step->sequencer_index to step->next_state, and returns true, iff both
 * condition_met (the caller's own already-evaluated comparison result --
 * see the file header) and rcp_compound_advance_guard(table, step) hold;
 * otherwise table is left entirely unchanged and this returns false.
 * Unlike rcp_compound_tick(), elapsing exec_delay_ms alone is never
 * sufficient here -- callers that want to give up on an unmatched wait
 * once its timer elapses do so themselves, using
 * rcp_compound_exec_delay_elapsed() directly; this function only ever
 * advances on a genuine condition match. */
bool rcp_compound_wait_tick(rcp_sequencer_table_t *table, const rcp_compound_step_t *step,
                             bool condition_met);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_COMPOUND_H */
