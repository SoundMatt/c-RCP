/* SPDX-License-Identifier: MPL-2.0 */
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
 * ── rcp_triggered_step_t: the trigger-selection sub-fields ─────────────────
 *
 * A triggered request's execution condition is "a named trigger signal,
 * emitted by a named endpoint, has occurred at least a named number of
 * times". Those three things are carried on the wire as three separate
 * one-octet sub-fields, and the eight octets of the repurposed
 * message_timestamp region carry, in order (offsets relative to the start
 * of that region):
 *
 *   offset 0  request_type      (the opcode octet, 0x0E or 0x8E)
 *   offset 1  trigger_source_ep (which endpoint emits the trigger)
 *   offset 2  trigger_signal_nr (which of that endpoint's trigger signals)
 *   offset 3  trigger_threshold (how many occurrences must precede execution)
 *   offset 4  exec_delay        (trigger_exec_delay), two octets, big-endian
 *             offset 5
 *   offset 6  repeat_count      (trigger_repetitions), two octets, big-endian
 *             offset 7
 *
 * Before v0.102.0 this module carried request_compound.h's own
 * sequencer_index/start_state/next_state sub-fields here instead, which
 * meant a triggered request had no way at all to express *which* trigger
 * it was waiting on -- the entire trigger-selection mechanism was absent.
 * A triggered request has no sequencer of its own and no start/next state:
 * it is not a sequencer-driven request kind, and this module consequently
 * has no dependency on request_sequencer.h at all.
 *
 * exec_delay is counted in multiples of the addressed endpoint's own
 * configured ep_delay_time, not in milliseconds. repeat_count's all-ones
 * value (RCP_TRIGGERED_REPEAT_INFINITE) is the infinite-repetition
 * sentinel.
 *
 * ── The trigger-occurrence counter and threshold ────────────────────────────
 *
 * A triggered request's own runtime state (rcp_triggered_runtime_t) tracks
 * how many matching trigger occurrences it has observed since entering its
 * "started" state (rcp_triggered_runtime_enter_started(), which resets the
 * counter to 0). rcp_triggered_runtime_record_occurrence() takes the
 * observed occurrence's own (source_ep, signal_nr) pair and counts it only
 * if it matches this request's own trigger_source_ep/trigger_signal_nr --
 * a request waiting on one endpoint's trigger signal is never advanced by
 * a different endpoint's, nor by a different signal number of the same
 * endpoint.
 *
 * trigger_threshold counts occurrences *before* execution, so a threshold
 * of zero means the request executes on the first occurrence and a
 * threshold of N means it executes on occurrence N+1 --
 * rcp_triggered_threshold_reached() is that comparison in its own pure,
 * directly-testable form.
 *
 * Only the *fire* transition itself (rcp_triggered_tick()) is additionally
 * gated on the caller-supplied endpoint_idle flag -- the counter itself
 * free-runs independent of that flag.
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

/* Sentinel repeat_count value meaning "repeat indefinitely": the
 * all-ones value of the 2-octet repetition sub-field. Identical in
 * meaning to request_compound.h's RCP_COMPOUND_REPEAT_INFINITE. */
#define RCP_TRIGGERED_REPEAT_INFINITE ((uint16_t)0xFFFFu)

typedef struct {
    uint8_t  trigger_source_ep;  /* the endpoint whose trigger signal this
                                     request waits on */
    uint8_t  trigger_signal_nr;  /* which of that endpoint's trigger
                                     signals -- endpoint-defined numbering */
    uint8_t  trigger_threshold;  /* occurrences that must precede execution:
                                     0 fires on the first occurrence, N on
                                     the (N+1)th -- see the file header */
    uint16_t exec_delay;         /* trigger_exec_delay, counted in multiples
                                     of the addressed endpoint's configured
                                     ep_delay_time -- NOT milliseconds */
    uint16_t repeat_count;       /* remaining repetitions;
                                     RCP_TRIGGERED_REPEAT_INFINITE means
                                     never decrement */
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

/* Records one observed trigger occurrence, emitted by endpoint source_ep
 * as its own trigger signal number signal_nr. Increments
 * rt->occurrence_count by one, and returns true, iff rt->started *and*
 * the occurrence matches this request's own selection -- that is,
 * source_ep == step->trigger_source_ep and signal_nr ==
 * step->trigger_signal_nr. A non-matching occurrence, or one arriving
 * while rt has not entered "started" (or has already fired -- see
 * rcp_triggered_tick()), leaves rt entirely unchanged and returns false.
 * Independent of any endpoint idle/busy status: callers invoke this for
 * every trigger occurrence regardless of that status, per the file
 * header. */
bool rcp_triggered_runtime_record_occurrence(rcp_triggered_runtime_t *rt,
                                              const rcp_triggered_step_t *step,
                                              uint8_t source_ep, uint8_t signal_nr);

/* True iff enough matching occurrences have been recorded for this
 * request to execute: rt->occurrence_count > step->trigger_threshold. A
 * threshold of 0 is therefore satisfied by a single occurrence, and a
 * threshold of N by N+1 occurrences -- see the file header. */
bool rcp_triggered_threshold_reached(const rcp_triggered_step_t *step,
                                      const rcp_triggered_runtime_t *rt);

/* True iff elapsed >= step->exec_delay, in that field's own unit
 * (multiples of the endpoint's configured ep_delay_time). Pure; see
 * request_compound.h's rcp_compound_exec_delay_elapsed() for the identical shape
 * applied to that module's own delay field. */
bool rcp_triggered_exec_delay_elapsed(const rcp_triggered_step_t *step, uint32_t elapsed);

/* The fire transition: resets rt (occurrence_count = 0, started = false)
 * and returns true iff *all* of the following hold: rt->started,
 * rcp_triggered_threshold_reached(step, rt),
 * rcp_triggered_exec_delay_elapsed(step, elapsed), and endpoint_idle.
 * Otherwise rt is left entirely unchanged and this returns false.
 * endpoint_idle gates the fire transition only -- the occurrence counter
 * itself (see rcp_triggered_runtime_record_occurrence()) is deliberately
 * not gated on it. This function advances no sequencer: a triggered
 * request has none, see the file header. */
bool rcp_triggered_tick(const rcp_triggered_step_t *step, rcp_triggered_runtime_t *rt,
                         uint32_t elapsed, bool endpoint_idle);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_TRIGGERED_H */
