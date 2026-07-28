//cfusa:req REQ-TRIG-001
//cfusa:req REQ-TRIG-002
//cfusa:req REQ-TRIG-003
//cfusa:req REQ-TRIG-004
//cfusa:req REQ-TRIG-005
//cfusa:req REQ-TRIG-006
//cfusa:req REQ-TRIG-007
//cfusa:req REQ-TRIG-008
//cfusa:req REQ-TRIG-009
//cfusa:req REQ-TRIG-010
//cfusa:req REQ-TRIG-011
//cfusa:req REQ-TRIG-012
//cfusa:req REQ-TRIG-013
/*
 * request_triggered.h -- Triggered conditional requests for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 17, "Conditional Requests &
 * Sequencers", milestone 69).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), and this project's own sequencer-state primitive
 * (request_sequencer.h/sequencer.c, milestone 68). Nothing in rcp.h, wire.c, avtp.h/
 * avtp.c, acf.h/acf.c, server.h/server.c, regmap.h/regmap.c, or any ep_*
 * endpoint module is touched here -- the same layering discipline every
 * module since milestone 64 has followed. This module is a peer of
 * request_compound.h (milestone 68), not a dependent of it: it follows the exact
 * same architectural template (the message_timestamp-repurposing trick,
 * the advance-only-if-still-in-start_state guard against request_sequencer.h) but
 * does not include or call into request_compound.h itself, matching this project's
 * own precedent of every request-kind module owning its own small pure
 * helpers rather than sharing them through a cross-module dependency.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── request_type and the shared repurposing trick ───────────────────────────
 *
 * Triggered requests reuse request_compound.h's own message_timestamp-repurposing
 * convention (see request_compound.h's file header for the full rationale): an
 * ACF_GBB message whose mtv is RCP_ACF_MTV_UNTIMED has its 8-byte
 * message_timestamp region reinterpreted as a 1-byte request_type opcode
 * followed by 7 kind-specific sub-field bytes. request_type
 * RCP_REQUEST_TYPE_TRIGGERED (0x0E) and its safety-tagged counterpart
 * RCP_REQUEST_TYPE_TRIGGERED_SAFETY (0x8E) are this module's two opcode
 * values within that shared convention -- request_compound.h's own docstring
 * reserved both ahead of time. Safety-tagged gating (only executing once
 * the endpoint is in its configured safe state) is Phase 18's job
 * (e2e.h, milestone 70), not this one's, mirroring request_compound.h's own
 * precedent for its own safety-tagged variants.
 *
 * ── rcp_triggered_step_t: this module's own 7-byte sub-field layout ────────
 *
 * Unlike request_compound.h's rcp_compound_step_t (16-bit sequencer_index, 8-bit
 * repeat_count -- exactly 7 bytes with those widths), this module's own
 * repeat_count is 16 bits wide per this milestone's roadmap scope, which
 * does not fit alongside a 16-bit sequencer_index in the same 7-byte
 * budget. This module's own byte-level design choice (not reproduced from
 * the specification) narrows sequencer_index to 8 bits instead:
 * sequencer_index (1 byte) | start_state (1) | next_state (1) |
 * trigger_exec_delay_ms (2) | repeat_count (2) == 7 bytes. repeat_count is
 * round-tripped by the encode/decode functions below, exactly like
 * request_compound.h's own repeat_count (see that header's file comment) -- this
 * milestone's scope extends only to RCP_TRIGGERED_REPEAT_INFINITE's wire
 * round-trip, not to any re-arming/repetition scheduling behavior.
 *
 * ── The trigger-occurrence counter ──────────────────────────────────────────
 *
 * A triggered request's own runtime state (rcp_triggered_runtime_t) tracks
 * how many independent trigger occurrences it has observed since entering
 * its "started" state (rcp_triggered_runtime_enter_started(), which resets
 * the counter to 0) via rcp_triggered_runtime_record_occurrence() -- callers
 * invoke that for every trigger occurrence regardless of the target
 * endpoint's own idle/busy status, per this milestone's roadmap scope.
 * Only the *fire* transition itself (rcp_triggered_tick(), which both
 * advances the target sequencer and requires at least one recorded
 * occurrence) is additionally gated on the caller-supplied endpoint_idle
 * flag -- the counter itself free-runs independent of that flag.
 *
 * Neither this module nor rcp_triggered_tick() owns a timer, thread, or
 * polling loop of its own -- every tick is caller-driven, matching every
 * protocol-core module built so far (see request_compound.h's own file header for
 * the same convention).
 */
#ifndef RCP_REQUEST_TRIGGERED_H
#define RCP_REQUEST_TRIGGERED_H

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

#define RCP_REQUEST_TYPE_TRIGGERED        ((uint8_t)0x0Eu)
#define RCP_REQUEST_TYPE_TRIGGERED_SAFETY ((uint8_t)0x8Eu)

/* True iff request_type is RCP_REQUEST_TYPE_TRIGGERED or
 * RCP_REQUEST_TYPE_TRIGGERED_SAFETY. */
bool rcp_request_type_is_triggered(uint8_t request_type);

/* ── Errors ─────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_TRIGGERED_OK                 = 0,
    RCP_TRIGGERED_ERR_SHORT_FRAME    = 1,
    RCP_TRIGGERED_ERR_BAD_MSG_TYPE   = 2,
    RCP_TRIGGERED_ERR_NOT_REPURPOSED = 3, /* decoded mtv != RCP_ACF_MTV_UNTIMED */
    RCP_TRIGGERED_ERR_UNKNOWN_TYPE   = 4, /* opcode byte is not a
                                              triggered request_type */
} rcp_triggered_errc_t;

/* Human-readable message for an rcp_triggered_errc_t value. Never returns NULL. */
const char *rcp_triggered_strerror(rcp_triggered_errc_t e);

/* ── rcp_triggered_step_t: wire sub-fields ────────────────────────────────── */

/* Sentinel repeat_count value meaning "repeat indefinitely" -- this
 * module's own 16-bit analogue of request_compound.h's 8-bit
 * RCP_COMPOUND_REPEAT_INFINITE, per this milestone's own roadmap scope.
 * Round-tripped only; see the file header. */
#define RCP_TRIGGERED_REPEAT_INFINITE ((uint16_t)0xFFFFu)

typedef struct {
    uint8_t  sequencer_index;     /* which of a table's sequencers this
                                      step targets -- 8 bits wide, see the
                                      file header */
    uint8_t  start_state;         /* required current sequencer state,
                                      see rcp_triggered_advance_guard() */
    uint8_t  next_state;          /* state this step advances the
                                      sequencer to on fire */
    uint16_t trigger_exec_delay_ms; /* this step's own trigger_exec_delay
                                        timer, in milliseconds (this
                                        module's own unit choice, matching
                                        request_compound.h's exec_delay_ms
                                        precedent) */
    uint16_t repeat_count;        /* round-tripped only; see the file header */
} rcp_triggered_step_t;

/* ── Triggered request encode/decode ──────────────────────────────────────── */

/* Encodes an ACF_GBB-framed triggered request addressed to byte_bus_id,
 * packing step into the repurposed message_timestamp region's 7 sub-field
 * bytes with the leading opcode byte set to request_type and mtv forced
 * to RCP_ACF_MTV_UNTIMED -- same conventions as
 * rcp_compound_encode_request() (request_compound.h). request_type must be
 * RCP_REQUEST_TYPE_TRIGGERED or RCP_REQUEST_TYPE_TRIGGERED_SAFETY.
 * payload/payload_len is this request's own opaque, endpoint-specific
 * data; payload may be NULL iff payload_len == 0. Returns a zeroed
 * rcp_bytes_t (data=NULL) if request_type is not recognized, payload_len
 * exceeds RCP_ACF_MAX_PAYLOAD, or on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_triggered_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                          const rcp_triggered_step_t *step, uint8_t transaction_num,
                                          const uint8_t *payload, size_t payload_len);

/* Decodes and validates an ACF-level triggered request from b[0..len).
 * Same failure-mode conventions as rcp_compound_decode_request()
 * (request_compound.h), with RCP_TRIGGERED_ERR_UNKNOWN_TYPE returned whenever the
 * decoded opcode byte is not rcp_request_type_is_triggered(). On
 * RCP_TRIGGERED_OK, *out_request_type, *out_byte_bus_id, *out_step, and
 * *out_transaction_num are populated, and *out_payload / *out_payload_len
 * are set to a *borrowed* view into b. */
rcp_triggered_errc_t rcp_triggered_decode_request(const uint8_t *b, size_t len,
                                                   uint8_t *out_request_type,
                                                   rcp_byte_bus_id_t *out_byte_bus_id,
                                                   rcp_triggered_step_t *out_step,
                                                   const uint8_t **out_payload, size_t *out_payload_len,
                                                   uint8_t *out_transaction_num);

/* ── The trigger-occurrence counter and fire tick ─────────────────────────── */

/* One triggered request's own runtime (not wire-carried) state: how many
 * trigger occurrences have been observed since entering "started". */
typedef struct {
    uint32_t occurrence_count;
    bool     started;
} rcp_triggered_runtime_t;

/* Resets rt to occurrence_count == 0, started == true -- the "entering
 * started" transition per the file header. */
void rcp_triggered_runtime_enter_started(rcp_triggered_runtime_t *rt);

/* Increments rt->occurrence_count by one iff rt->started -- a no-op
 * (returns false) if rt has never entered "started" (or has already
 * fired -- see rcp_triggered_tick()). Returns whether the occurrence was
 * recorded. Independent of any endpoint idle/busy status: callers invoke
 * this for every trigger occurrence regardless of that status, per the
 * file header. */
bool rcp_triggered_runtime_record_occurrence(rcp_triggered_runtime_t *rt);

/* True iff table's sequencer step->sequencer_index is currently sitting
 * in step->start_state -- this module's own copy of the advance-only-if-
 * still-in-start_state guard (request_compound.h's rcp_compound_advance_guard()
 * expresses the identical rule for compound/compound-wait; this module
 * intentionally does not share that function across the module boundary,
 * see the file header). False if step->sequencer_index is not
 * rcp_sequencer_index_valid() for table. */
bool rcp_triggered_advance_guard(const rcp_sequencer_table_t *table,
                                  const rcp_triggered_step_t *step);

/* True iff elapsed_ms >= step->trigger_exec_delay_ms. Pure; see
 * request_compound.h's rcp_compound_exec_delay_elapsed() for the identical shape
 * applied to this module's own delay field. */
bool rcp_triggered_exec_delay_elapsed(const rcp_triggered_step_t *step, uint32_t elapsed_ms);

/* The fire transition: advances table's sequencer step->sequencer_index
 * to step->next_state, resets rt (occurrence_count = 0, started = false),
 * and returns true, iff *all* of the following hold: rt->started, at
 * least one occurrence has been recorded (rt->occurrence_count > 0),
 * rcp_triggered_exec_delay_elapsed(step, elapsed_ms), endpoint_idle is
 * true, and rcp_triggered_advance_guard(table, step) holds. Otherwise
 * table and rt are both left entirely unchanged and this returns false.
 * endpoint_idle is the one caller-supplied flag this milestone's roadmap
 * scope requires gating the fire transition on -- the occurrence counter
 * itself (see rcp_triggered_runtime_record_occurrence()) is deliberately
 * not gated on it. */
bool rcp_triggered_tick(rcp_sequencer_table_t *table, const rcp_triggered_step_t *step,
                         rcp_triggered_runtime_t *rt, uint32_t elapsed_ms, bool endpoint_idle);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_TRIGGERED_H */
