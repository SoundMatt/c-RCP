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
 * request.h -- The conditional-request taxonomy for the TC18 Remote Control
 * Protocol wire layer: compound, compound-wait, triggered, chained, timed,
 * and the three cancellation forms (clear-all, clear-non-safestate,
 * clear-single) (ROADMAP.md Phase 17, "Conditional Requests & Sequencers",
 * milestones 68-69).
 *
 * ── History: five files unified into one (c-RCP-165) ────────────────────────
 *
 * Through v0.13x this taxonomy shipped as five separate header/source pairs
 * -- request_compound.{h,c}, request_triggered.{h,c}, request_chained.{h,c},
 * request_timed.{h,c}, request_cancel.{h,c} -- one per request kind, each
 * independently reimplementing the same message_timestamp-repurposing
 * decode convention and the same private put_u64() byte-order helper. That
 * split diverged from cpp-RCP's `include/rcp/request.hpp` and rust-RCP's
 * `src/request.rs`, which model the same taxonomy as one module each (see
 * RELAY's docs/RCP-ARCHITECTURE.md, canonical choice #4). This header (and
 * its paired request.c) is the mechanical, behavior-preserving merge of
 * those five pairs into one module, keeping every function, type, and
 * REQ-CMP, REQ-TRIG, REQ-CHAIN, REQ-TIMED, and REQ-CANCEL requirement id
 * unchanged -- only the file boundary moved. It is purely organizational:
 * no wire encoding, no field layout, and no error/outcome semantics changed
 * as part of this merge (verified by mutation-testing a sample of the moved
 * logic; see c-RCP-165's PR description for specifics). Each request
 * kind's own section below still carries its own original file-header
 * documentation verbatim, since that documentation is specific to that
 * kind's own wire shape and TC18 basis, not to the taxonomy as a whole.
 *
 * Two of the target shape's other properties (RCP-ARCHITECTURE.md's
 * canonical choice #4) already existed before this merge, as their own
 * separate, already-shared modules, and are unchanged by it:
 * request_sequencer.h/request_sequencer.c already own the one shared
 * sequencer-state bank compound/compound-wait steps advance (REQ-SEQ-*),
 * and scheduler.h/scheduler.c already own the one shared request-kind
 * classification enum (rcp_sched_kind_t) and cross-kind execution-priority
 * ordering (cancellation > triggered > timed > compound > compound-wait >
 * chained > standard, REQ-SCHED-*) cpp-RCP's RequestCategory/priority_rank/
 * select_next_due implement in-file. Neither is folded into this header:
 * this merge's scope is exactly the five originally-split files above, per
 * c-RCP-165's own stated scope.
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the register-map's svr_implemented_options
 * time-sync feature group (regmap.h, milestone 62), and this project's own
 * sequencer-state primitive (request_sequencer.h/request_sequencer.c,
 * milestone 68). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c,
 * server.h/server.c, regmap.h/regmap.c, or any ep_* endpoint module is
 * touched here -- the same layering discipline every module since milestone
 * 64 has followed.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ══ Compound / compound-wait (request_type 0x0F/0x8F, 0x0B/0x8B) and
 *    clear-non-safestate (0x06) ════════════════════════════════════════════
 *
 * ── request_type: the message_timestamp-repurposing trick ──────────────────
 *
 * acf.h's ACF_GBB variant carries an 8-byte message_timestamp field,
 * meaningful only when its byte_message_info.mtv is RCP_ACF_MTV_VALID
 * (acf.h). This taxonomy gives a second meaning to that same 8-byte
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
 * request_type values every request kind in this file recognizes:
 *
 *   0x01       chained
 *   0x05       clear-all
 *   0x06       clear-non-safestate (this section's own cancellation
 *              request; no safety-tagged counterpart of its own)
 *   0x07       clear-single
 *   0x0A       timed
 *   0x0B/0x8B  compound-wait / compound-wait, safety-tagged
 *   0x0E/0x8E  triggered / triggered, safety-tagged
 *   0x0F/0x8F  compound / compound, safety-tagged
 *
 * The safety-tagged (MSB-set) variants are round-tripped by this module's
 * encode/decode functions exactly like any other request_type value --
 * gating their execution on the endpoint's configured safe state is Phase
 * 18's job (e2e.h, milestone 70), not this one's, mirroring acf.h's own
 * precedent of round-tripping a field before its full behavior is
 * implemented elsewhere.
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
 * configured ep_delay_time, not in milliseconds. repeat_count's
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
 *
 * ══ Triggered (request_type 0x0E/0x8E) ═══════════════════════════════════
 *
 * ── request_type and the shared repurposing trick ───────────────────────────
 *
 * Triggered requests reuse the message_timestamp-repurposing convention
 * above: an ACF_GBB message whose mtv is RCP_ACF_MTV_UNTIMED has its
 * 8-byte message_timestamp region reinterpreted as a 1-byte request_type
 * opcode followed by 7 kind-specific sub-field bytes. request_type
 * RCP_REQUEST_TYPE_TRIGGERED (0x0E) and its safety-tagged counterpart
 * RCP_REQUEST_TYPE_TRIGGERED_SAFETY (0x8E) are this section's two opcode
 * values within that shared convention. Safety-tagged gating (only
 * executing once the endpoint is in its configured safe state) is Phase 18's
 * job (e2e.h, milestone 70), not this one's, mirroring the compound
 * section's own precedent for its own safety-tagged variants.
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
 * Before v0.102.0 this section carried the compound section's own
 * sequencer_index/start_state/next_state sub-fields here instead, which
 * meant a triggered request had no way at all to express *which* trigger
 * it was waiting on -- the entire trigger-selection mechanism was absent.
 * A triggered request has no sequencer of its own and no start/next state:
 * it is not a sequencer-driven request kind, and this section consequently
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
 * Neither this section nor rcp_triggered_tick() owns a timer, thread, or
 * polling loop of its own -- every tick is caller-driven, matching every
 * protocol-core module built so far.
 *
 * ══ Chained (request_type 0x01) ═══════════════════════════════════════════
 *
 * ── What a chain is, in this codebase's own terms ────────────────────────────
 *
 * A chained request forces sequential execution of two or more requests
 * packed as separate ACF messages within one AVTPDU. Each member of a
 * chain is its own ACF_GBB message carrying request_type
 * RCP_REQUEST_TYPE_CHAINED (0x01, no safety-tagged variant exists) via
 * the shared message_timestamp-repurposing convention above.
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
 * Before v0.102.0 this section invented chain_length and chain_position
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
 * deliberately does not implement"). This section is the first to give
 * that already-published, unmodified field real behavior, exactly the
 * kind of "round-trip now, activate later" precedent this file's own
 * safety-tagged (MSB) request_type variants already established.
 *
 * cs is a *conditional start* selector, and it is read on the member
 * about to run, about the member that just ran:
 * RCP_CHAINED_CS_CONTINUE_ON_ERROR means this member executes even if its
 * predecessor returned an error, while RCP_CHAINED_CS_ABORT_ON_ERROR
 * means this member does not execute at all when its predecessor errored
 * -- and, as a consequence, neither does the remainder of the chain.
 * Before v0.102.0 this section read cs off the member that *errored*
 * rather than off its successor, which inverted control of the
 * abort decision: a failing member could veto its own successors instead
 * of each successor deciding for itself whether to proceed.
 * rcp_chained_advance() below is the pure, directly-testable expression of
 * the corrected rule.
 *
 * ── Outcomes: CHAIN_ERROR and CHAIN_ABORTED ─────────────────────────────────
 *
 * rcp_chained_member_outcome_t's RCP_CHAINED_MEMBER_CHAIN_ERROR and
 * RCP_CHAINED_MEMBER_CHAIN_ABORTED are this section's own spelling of the
 * roadmap's CHAIN_ERROR (a chain member that was itself executed and
 * itself failed) and CHAIN_ABORTED (a chain member skipped outright
 * because an earlier member's error, combined with
 * RCP_CHAINED_CS_ABORT_ON_ERROR, aborted the remainder of the chain)
 * outcomes -- callers report these as this member's own result exactly as
 * they would any other per-request outcome. This section owns no endpoint
 * dispatch of its own: the caller executes each chain member through
 * whatever mechanism the rest of the server uses for a Standard request,
 * and reports that member's own success/failure back into
 * rcp_chained_advance() to learn what the *next* member's status should
 * be.
 *
 * ══ Timed (request_type 0x0A) ═════════════════════════════════════════════
 *
 * ── A per-request alternative to a TSCF header ───────────────────────────────
 *
 * avtp.h's TSCF header already carries one avtp_timestamp, the "earliest
 * moment" presentation time for every ACF message inside that AVTPDU
 * (avtp.h's own file header). A Timed request (request_type
 * RCP_REQUEST_TYPE_TIMED, 0x0A, no safety-tagged variant exists) instead
 * carries its own presentation_time sub-field directly, via the shared
 * message_timestamp-repurposing convention above -- letting a client
 * express the identical "don't execute before this instant" semantics
 * inside a plain NTSCF frame, without needing a TSCF header at all.
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
 * Before v0.102.0 this section packed only a 32-bit value, starting one
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
 *
 * ══ Cancellation: clear-all (0x05) and clear-single (0x07) ═══════════════
 *
 * ── One coherent taxonomy, three request types ───────────────────────────────
 *
 *   0x05  clear-all             this section. Mandatory since Phase
 *                                13's baseline, but not previously given
 *                                its own encode/decode surface anywhere in
 *                                this codebase.
 *   0x06  clear-non-safestate   the compound section above (already
 *                                shipped at v0.68.0, milestone 68) -- see
 *                                rcp_compound_encode_clear_non_safestate()/
 *                                _decode_clear_non_safestate(). Not
 *                                duplicated here.
 *   0x07  clear-single          this section. Carries a
 *                                clear_transaction_num field identifying
 *                                which queued request to cancel.
 *
 * All three share the message_timestamp-repurposing wire convention above.
 * clear-all and clear-single both carry no payload of their own beyond
 * their opcode byte and (for clear-single) the one clear_transaction_num
 * sub-field -- the remaining sub-field bytes are always encoded/decoded as
 * zero, mirroring the compound section's own clear-non-safestate encoding.
 *
 * ── General cancellation semantics ───────────────────────────────────────────
 *
 * A request is cancellable only in the window between being queued and
 * actually beginning execution -- rcp_cancel_is_cancellable() is the
 * pure, directly-testable expression of that rule, applying uniformly to
 * every cancellation type above (clear-all, clear-non-safestate,
 * clear-single all target requests in this same lifecycle window).
 * rcp_cancel_attempt() composes that rule with a caller-supplied "was the
 * target request found at all" flag to report one of this section's own
 * outcomes: RCP_CANCEL_RESULT_CANCELED (the roadmap's REQUEST_CANCELED,
 * reported once per individually-cancelled request),
 * RCP_CANCEL_RESULT_NOT_FOUND (the roadmap's REQUEST_NOT_FOUND, reported
 * on a clear-single miss), or RCP_CANCEL_RESULT_NOT_CANCELLABLE (this
 * section's own outcome for a request found but already past the
 * queued/executing window -- deliberately distinct from NOT_FOUND, since
 * the two roadmap-named codes only cover "never existed" and "actually
 * canceled", not "found, but too late").
 *
 * rcp_cancel_chain_should_cascade() is this section's own expression of
 * the roadmap's chained-successor cascade rule: canceling one member of a
 * chained request (the chained section above) also cancels every member at
 * or after that member's own chain_position within the same chain -- this
 * function is the pure per-member predicate a caller applies across a
 * chain's members, mirroring the same "pure decision function, caller
 * drives the loop" shape rcp_chained_advance() above already established.
 */
#ifndef RCP_REQUEST_H
#define RCP_REQUEST_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/request_sequencer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ════════════════════════════════════════════════════════════════════════
 * Compound / compound-wait (0x0F/0x8F, 0x0B/0x8B) and clear-non-safestate
 * (0x06)
 * ════════════════════════════════════════════════════════════════════════ */

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

/* ════════════════════════════════════════════════════════════════════════
 * Triggered (0x0E/0x8E)
 * ════════════════════════════════════════════════════════════════════════ */

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
 * meaning to RCP_COMPOUND_REPEAT_INFINITE above. */
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
 * rcp_compound_encode_request() above. request_type must be
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
 * Same failure-mode conventions as rcp_compound_decode_request() above,
 * with RCP_TRIGGERED_ERR_UNKNOWN_TYPE returned whenever the decoded
 * opcode byte is not rcp_request_type_is_triggered(). On
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
 * rcp_compound_exec_delay_elapsed() above for the identical shape applied
 * to that section's own delay field. */
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

/* ════════════════════════════════════════════════════════════════════════
 * Chained (0x01)
 * ════════════════════════════════════════════════════════════════════════ */

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
 * rcp_compound_encode_request() above. chain_exec_delay is counted in
 * multiples of the addressed endpoint's configured ep_delay_time,
 * measured from the moment the predecessor request finalized.
 * payload/payload_len is this member's own opaque, endpoint-specific
 * request data; payload may be NULL iff payload_len == 0. Returns a
 * zeroed rcp_bytes_t (data=NULL) if payload_len exceeds
 * RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_chained_encode_member(rcp_byte_bus_id_t byte_bus_id, uint16_t chain_exec_delay,
                                       uint8_t cs, uint8_t transaction_num,
                                       const uint8_t *payload, size_t payload_len);

/* Decodes and validates a chained-request member from b[0..len). Same
 * failure-mode conventions as rcp_compound_decode_request() above, with
 * RCP_CHAINED_ERR_UNKNOWN_TYPE returned whenever the decoded opcode byte
 * is not RCP_REQUEST_TYPE_CHAINED and RCP_CHAINED_ERR_RESERVED_NONZERO
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

/* ════════════════════════════════════════════════════════════════════════
 * Timed (0x0A)
 * ════════════════════════════════════════════════════════════════════════ */

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
 * conventions as rcp_compound_encode_request() above.
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
 * TSCF-wrapped-ABB construction), but this function is where a caller
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
 * conventions as rcp_compound_decode_request() above, with
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

/* ════════════════════════════════════════════════════════════════════════
 * Cancellation: clear-all (0x05) and clear-single (0x07)
 * ════════════════════════════════════════════════════════════════════════ */

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
    RCP_CANCEL_ERR_RESERVED_NONZERO = 5, /* a reserved sub-field octet
                                             carries a set bit */
    RCP_CANCEL_ERR_EVT_HS_CS_NONZERO = 6, /* the ACF byte_message_info
                                              header's evt[2:0], hs, or cs
                                              bits are set -- TC18 Tables
                                              11/13 require all three be
                                              zero for clear-all/-single
                                              (REQ-CANCEL-013/-015); a
                                              distinct wire field from
                                              RESERVED_NONZERO's
                                              message_timestamp octets
                                              above */
} rcp_cancel_errc_t;

/* ── wire sub-field layout ───────────────────────────────────────────────────
 *
 * Clear-all (0x05) and clear-non-safestate (0x06) carry no sub-field of
 * their own: every octet of the repurposed message_timestamp region after
 * the opcode is reserved and transmitted as zero.
 *
 * Clear-single (0x07) carries exactly one:
 *
 *   offset 0     request_type          (the opcode octet, 0x07)
 *   offsets 1..2 reserved              (all bits zero)
 *   offset 3     clear_transaction_num (the transaction_num to cancel)
 *   offsets 4..7 reserved              (all bits zero)
 *
 * Before v0.102.0 clear_transaction_num was packed at offset 1 instead,
 * which both placed it two octets early and overwrote an octet the
 * specification mandates be transmitted as zero. A round-trip through
 * this module's own encode/decode pair could not detect that, since both
 * halves shared the same wrong offset. */

/* Human-readable message for an rcp_cancel_errc_t value. Never returns NULL. */
const char *rcp_cancel_strerror(rcp_cancel_errc_t e);

/* ── clear-all (0x05) ─────────────────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed clear-all cancellation request addressed to
 * byte_bus_id, with the repurposed message_timestamp region's opcode
 * byte set to RCP_REQUEST_TYPE_CLEAR_ALL and its remaining 7 sub-field
 * bytes zeroed (this request type carries none of its own) and mtv
 * forced to RCP_ACF_MTV_UNTIMED, same convention as
 * rcp_compound_encode_clear_non_safestate() above. Returns a
 * zeroed rcp_bytes_t (data=NULL) only on allocation failure. Caller frees
 * the result with rcp_bytes_free(). */
rcp_bytes_t rcp_cancel_encode_clear_all(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num);

/* Decodes and validates a clear-all request from b[0..len). Same
 * failure-mode conventions as rcp_compound_decode_clear_non_safestate()
 * above, with RCP_CANCEL_ERR_UNKNOWN_TYPE returned whenever the
 * decoded opcode byte is not RCP_REQUEST_TYPE_CLEAR_ALL,
 * RCP_CANCEL_ERR_RESERVED_NONZERO when any of message_timestamp's 7
 * trailing octets carries a set bit (REQ-CANCEL-013), and
 * RCP_CANCEL_ERR_EVT_HS_CS_NONZERO when evt[2:0], hs, or cs is nonzero
 * (TC18 Table 13; REQ-CANCEL-014). On RCP_CANCEL_OK, *out_byte_bus_id
 * and *out_transaction_num are populated. */
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
 * is not RCP_REQUEST_TYPE_CLEAR_SINGLE, RCP_CANCEL_ERR_RESERVED_NONZERO
 * per REQ-CANCEL-007's own contract, and RCP_CANCEL_ERR_EVT_HS_CS_NONZERO
 * when evt[2:0], hs, or cs is nonzero (TC18 Table 15; REQ-CANCEL-015).
 * On RCP_CANCEL_OK, *out_byte_bus_id, *out_clear_transaction_num, and
 * *out_transaction_num are populated. */
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
 * canceled_position. Both positions use rcp_chained_decode_member()'s own
 * 0-based chain_position numbering. A member strictly before
 * canceled_position has already executed by the time a chain member is
 * canceled (chained execution is sequential, per the chained section's
 * own file header) and is therefore never cascaded to. */
bool rcp_cancel_chain_should_cascade(uint8_t member_position, uint8_t canceled_position);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_H */
