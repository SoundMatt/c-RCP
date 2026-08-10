/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-PWRMODE-001
//cfusa:req REQ-PWRMODE-002
//cfusa:req REQ-PWRMODE-003
//cfusa:req REQ-PWRMODE-004
//cfusa:req REQ-PWRMODE-005
//cfusa:req REQ-PWRMODE-006
//cfusa:req REQ-PWRMODE-007
//cfusa:req REQ-PWRMODE-008
//cfusa:req REQ-PWRMODE-009
//cfusa:req REQ-PWRMODE-010
//cfusa:req REQ-PWRMODE-011
//cfusa:req REQ-PWRMODE-012
//cfusa:req REQ-PWRMODE-013

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-PWRMODE-014
//cfusa:req REQ-PWRMODE-015
//cfusa:req REQ-PWRMODE-016
//cfusa:req REQ-PWRMODE-017
//cfusa:req REQ-PWRMODE-018
//cfusa:req REQ-PWRMODE-019
//cfusa:req REQ-PWRMODE-020
//cfusa:req REQ-PWRMODE-021
//cfusa:req REQ-PWRMODE-022
//cfusa:req REQ-PWRMODE-023
//cfusa:req REQ-PWRMODE-024
//cfusa:req REQ-PWRMODE-025
//cfusa:req REQ-PWRMODE-026
//cfusa:req REQ-PWRMODE-027
//cfusa:req REQ-PWRMODE-028
/*
 * power.h -- RC Server power-mode state machine for the TC18 Remote
 * Control Protocol wire layer (ROADMAP.md Phase 19, "Remaining Endpoint
 * Types", milestone 75).
 *
 * This is new, additive protocol-core surface layered on top of the RC
 * Server lifecycle state machine (lifecycle.h/lifecycle.c, milestone 61).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, lifecycle.h/lifecycle.c, or
 * any ep_* endpoint module is touched here -- the same layering discipline
 * every module since milestone 64 has followed. This module cross-
 * references lifecycle.h (not server.h) for lifecycle-state concerns,
 * matching the module boundary the #87/#88 naming split established:
 * lifecycle.h owns the RC Server's own bring-up/lifecycle state, and this
 * module treats that state as an orthogonal axis a cold power-mode start
 * resets to RCP_LIFECYCLE_HW_UNCONFIGURED (see
 * rcp_pwrmode_cold_start_lifecycle_target() below), without itself calling
 * into lifecycle.c's transition logic -- driving the actual re-init
 * sequence through lifecycle.h remains a caller's job.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 * The four power-mode names (Normal, StandBy, Sleep, Unpowered) and their
 * high-level roles come from that specification by reference (extraction
 * §3.3-3.4, cited here only by section number, never by content, per this
 * project's confidentiality policy for that internal extraction document);
 * every on-wire value, transition rule, and handshake-step shape below is
 * this module's own original design.
 *
 * ── Requirement-id naming note ──────────────────────────────────────────────
 *
 * Verified directly (`grep`) against `.fusa-reqs.json` before picking this
 * module's own prefix: a pre-existing `REQ-PWR-001`..`010` group already
 * exists (`src/powerstate.c`, the legacy v0.13.0 client-side Active/
 * Sleeping/BusOff satellite convenience manager -- see powerstate.h). This
 * module is not that manager, and does not replace or renumber its
 * requirements (powerstate.c is untouched here; its own REPLACE work is
 * Satellite Rework v0.79.0, built *on top of* this milestone's actual
 * protocol-core mechanism). To avoid any collision with `REQ-PWR-*`, this
 * module's own requirements are tagged `REQ-PWRMODE-*` instead -- a
 * distinct prefix, not a "-EP"/numeric-continuation suffix, since this is
 * a different module with a different (non-overlapping) requirement set,
 * not a renumbering of the legacy one.
 *
 * ── Four power modes, one state variable ────────────────────────────────────
 *
 * rcp_pwrmode_t names the four modes; deliberately prefixed rcp_pwrmode_/
 * RCP_PWRMODE_ rather than reusing powerstate.h's own rcp_power_state_t/
 * RCP_POWER_* names, both to avoid any symbol/enumerator collision in C's
 * single global namespace and to keep this module's identifiers visibly
 * distinct from the legacy client-side model it supersedes at the wire
 * level.
 *
 * ── Hot vs. cold starts ──────────────────────────────────────────────────────
 *
 * rcp_pwrmode_transition() is this module's general-purpose transition
 * function, covering every direct mode-to-mode move except waking from
 * Sleep (see below): Normal<->StandBy is always a hot start (no lifecycle
 * re-init -- the RC Server's own lifecycle.h state is left untouched);
 * entering Sleep from Normal or StandBy, entering Unpowered from any mode,
 * and powering up from Unpowered back to Normal are all cold starts (full
 * lifecycle re-init required, down to and back up from
 * RCP_LIFECYCLE_HW_UNCONFIGURED). Skipping directly between StandBy and
 * Sleep, or between Unpowered and any mode other than Normal, is rejected
 * with RCP_PWRMODE_ERR_INVALID_TRANSITION -- the same "no skipping"
 * discipline lifecycle.h's own rcp_lifecycle_transition() already
 * enforces for its three states.
 *
 * Waking from Sleep back to Normal has its own function,
 * rcp_pwrmode_wake_from_sleep(), because -- unlike every other
 * transition -- whether it is a hot or cold start depends on *how* the
 * wake happened, not just on the (from, to) mode pair:
 *
 *   - REQ-PWRMODE-020 (TC18 §12.4.1, corrected from this module's
 *     original design): both a pin-level wake signal
 *     (RCP_PWRMODE_WAKE_VIA_PIN) and a network-level one
 *     (RCP_PWRMODE_WAKE_VIA_NETWORK, this module's own name for the
 *     roadmap's TC14/TC10 wake path) run the SAME four-step handshake --
 *     "If the wake-up source was a TC14/TC10 wake-up request on the
 *     network this will also wake the RC Server, but it will directly
 *     check for the network availability and proceed as before" (TC18's
 *     own text: "proceed as before" means run the same hot-start
 *     procedure just described for a pin/EP-signal wake, not skip it).
 *     This module originally modeled a network wake as always-hot with
 *     the handshake actively skipped -- primary-source re-verification
 *     found that wrong; only step (a)'s literal network-interface
 *     re-enable is unnecessary for a wake that arrived over the network
 *     in the first place (the interface is, by construction, already up),
 *     a hardware-level nuance this module's state machine does not need
 *     to represent specially -- a network-path caller still calls
 *     rcp_pwrmode_handshake_iface_reenabled() as a state-machine
 *     formality. Either path yields a hot start only if the caller has
 *     already driven the rcp_pwrmode_handshake_t below to
 *     RCP_PWRMODE_HANDSHAKE_COMPLETE; otherwise (handshake not started,
 *     still in progress, or failed) the wake falls back to this module's
 *     own safe default -- an ordinary cold start, matching the "Sleep is
 *     always a cold start" baseline rule stated above for every other
 *     entry into or exit from Sleep.
 *
 * ── The four-step hot-start-from-Sleep handshake ────────────────────────────
 *
 * rcp_pwrmode_handshake_t models the roadmap's own four-step sequence as
 * an explicit, directly-testable state machine (this module's own
 * step-naming and struct shape, not spec-derived):
 *
 *   (a) network-interface re-enable       -- rcp_pwrmode_handshake_iface_reenabled()
 *   (b) WakeUp message repeated until      -- rcp_pwrmode_handshake_wakeup_attempt()
 *       echoed back, or a repeat-limit hit    (RCP_PWRMODE_HANDSHAKE_ECHOED /
 *                                               RCP_PWRMODE_HANDSHAKE_FAILED)
 *   (c) other response/ack queues resume  -- rcp_pwrmode_handshake_resume_queues()
 *
 * rcp_pwrmode_hotstart_required() (REQ-PWRMODE-020) is required for
 * every wake path, not skipped for a network-level one -- see the "Hot
 * vs. cold starts" section above for the primary-source correction.
 *
 * Steps must be driven in order (a, then repeated b, then c); each
 * function below documents which step(s) it requires the handshake to
 * already be in. The actual WakeUp message itself -- its wire encoding,
 * and recognizing an echo -- is ep_wakeup.h's job (rcp_ep_wakeup.h's
 * rcp_ep_wakeup_encode_wakeup_message()/_is_wakeup_echo()); this module
 * only tracks the handshake's own progress, taking "was this attempt
 * echoed" as a caller-supplied bool rather than parsing wire frames
 * itself, mirroring lifecycle.h's own "minimal self-contained stand-in,
 * not the full register-map job" scoping precedent.
 *
 * ── Entry-refusal gating ─────────────────────────────────────────────────────
 *
 * rcp_pwrmode_check_entry() is the shared gating rule the roadmap requires
 * for *both* StandBy and Sleep entry requests: refuse
 * (RCP_PWRMODE_ENTRY_REFUSED, this module's own name for the roadmap's
 * REQUEST_CANCELED outcome -- request_cancel.h's own
 * RCP_CANCEL_RESULT_CANCELED names the same roadmap-level outcome for its
 * own, differently-shaped cancellation domain; the two are deliberately
 * not the same enum, since this module's gate is a yes/no admission check,
 * not a cancellation-lifecycle outcome) whenever any of the three named
 * conditions holds: wup_status has not been cleared, at least one endpoint
 * of the whole RC Server is not idle, or at least one responder queue
 * server-wide still holds a message pending transmission (REQ-PWRMODE-025,
 * TC18 §12.5: "At least one EP is not idle" / "In at least one responder
 * queue there is a message pending for transmission" -- both conditions
 * are server-wide, not scoped to the one endpoint the request arrived on).
 * This function takes plain bools rather than reaching into ep_wakeup.h's
 * or any queue module's own state directly, keeping this module
 * endpoint-agnostic; a caller wires up ep_wakeup.h's own
 * rcp_ep_wakeup_wup_status_is_clear() into wup_status_clear, and its own
 * logical AND of every endpoint's idleness / every responder queue's
 * emptiness into endpoint_idle / response_queue_empty -- see those
 * fields' own doc comments below.
 *
 * rcp_pwrmode_commit_entry() is the authoritative second half of that same
 * admission -- see its own doc comment for why a single
 * rcp_pwrmode_check_entry() call at request-arrival time is not enough
 * (REQ-PWRMODE-024, REQ-PWRMODE-026).
 *
 * Neither function knows anything about which RC Client sent the request
 * or whether it was authorized to; per REQ-PWRMODE-023 (TC18 §12.5: "The
 * RC Client that is allowed to access the RC Server endpoint can request
 * the entire RC Server implementation to enter standby or sleep mode"),
 * that check is ep_wakeup.h's own rcp_ep_wakeup_sleepcmd_writable(), run
 * before either of these -- see that function's own doc comment. Both
 * functions here operate on a single rcp_pwrmode_t, this module's
 * existing whole-server mode representation (never a per-endpoint one);
 * a caller composing the entire admitted sequence points *mode at its own
 * one server-wide power-mode variable, satisfying TC18's "entire RC
 * Server implementation" wording by construction.
 */
#ifndef RCP_POWER_H
#define RCP_POWER_H

#include "rcp/lifecycle.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Power modes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_PWRMODE_NORMAL    = 0,
    RCP_PWRMODE_STANDBY   = 1,
    RCP_PWRMODE_SLEEP     = 2,
    RCP_PWRMODE_UNPOWERED = 3,
} rcp_pwrmode_t;

/* Unique, non-empty, human-readable name for mode. Never returns NULL. */
const char *rcp_pwrmode_string(rcp_pwrmode_t mode);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_PWRMODE_OK                     = 0,
    RCP_PWRMODE_ERR_INVALID_TRANSITION = 1,
    RCP_PWRMODE_ERR_ENTRY_REFUSED      = 2, /* rcp_pwrmode_commit_entry() only --
                                                see that function's own doc comment. */
} rcp_pwrmode_errc_t;

/* Human-readable message for an rcp_pwrmode_errc_t value. Never returns NULL. */
const char *rcp_pwrmode_strerror(rcp_pwrmode_errc_t e);

/* ── Hot vs. cold starts ──────────────────────────────────────────────────────── */

typedef enum {
    RCP_PWRMODE_START_HOT  = 0, /* prior lifecycle.h state preserved */
    RCP_PWRMODE_START_COLD = 1, /* full lifecycle.h re-init required */
} rcp_pwrmode_start_kind_t;

/* The lifecycle.h state a cold start's own re-init sequence targets before
 * bringing the server back up -- see the file header. This module never
 * calls rcp_lifecycle_transition() itself; it only names the target state
 * for a caller's own re-init sequence to drive toward. */
rcp_lifecycle_state_t rcp_pwrmode_cold_start_lifecycle_target(void);

/* ── General mode transitions (everything except waking from Sleep) ─────────── */

/* Attempts to move *mode to target, reporting via *out_start_kind (if
 * non-NULL) whether the move is a hot or cold start per the file header's
 * transition table. On success *mode is updated and RCP_PWRMODE_OK is
 * returned; on failure *mode is left unchanged, *out_start_kind is
 * untouched, and RCP_PWRMODE_ERR_INVALID_TRANSITION is returned.
 *
 * Waking from Sleep back to Normal is deliberately NOT reachable through
 * this function -- use rcp_pwrmode_wake_from_sleep() instead, since that
 * transition's hot/cold classification depends on more than the (from,
 * to) pair alone. Calling this function with *mode == RCP_PWRMODE_SLEEP
 * and target == RCP_PWRMODE_NORMAL always fails with
 * RCP_PWRMODE_ERR_INVALID_TRANSITION for exactly that reason. */
rcp_pwrmode_errc_t rcp_pwrmode_transition(rcp_pwrmode_t *mode, rcp_pwrmode_t target,
                                           rcp_pwrmode_start_kind_t *out_start_kind);

/* ── Waking from Sleep ─────────────────────────────────────────────────────── */

typedef enum {
    RCP_PWRMODE_WAKE_VIA_PIN     = 0,
    RCP_PWRMODE_WAKE_VIA_NETWORK = 1, /* the roadmap's TC14/TC10 network-level wake signal */
} rcp_pwrmode_wake_path_t;

/* True iff the handshake below is required for this wake path to be
 * classified hot. As of REQ-PWRMODE-020, true unconditionally: TC18
 * §12.4.1's own text ("a TC14/TC10 wake-up request on the network...
 * will directly check for the network availability and proceed as
 * before") requires RCP_PWRMODE_WAKE_VIA_NETWORK to run the same
 * handshake as RCP_PWRMODE_WAKE_VIA_PIN, not skip it -- see the file
 * header's "Hot vs. cold starts" section for the full primary-source
 * correction. path is retained in the signature (rather than dropped)
 * as the natural hook for any future wake path this module might need
 * to model with genuinely different hot-start preconditions. */
bool rcp_pwrmode_hotstart_required(rcp_pwrmode_wake_path_t path);

typedef enum {
    RCP_PWRMODE_HANDSHAKE_NOT_STARTED     = 0,
    RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED = 1, /* step (a) done */
    RCP_PWRMODE_HANDSHAKE_ECHOED          = 2, /* step (b) succeeded */
    RCP_PWRMODE_HANDSHAKE_COMPLETE        = 3, /* step (c) done; hot start authorized */
    RCP_PWRMODE_HANDSHAKE_FAILED          = 4, /* step (b) exhausted its repeat limit */
} rcp_pwrmode_handshake_step_t;

typedef struct {
    rcp_pwrmode_handshake_step_t step;
    uint32_t                     wakeup_attempts;    /* count of rcp_pwrmode_handshake_wakeup_attempt() calls so far */
    uint32_t                     wakeup_repeat_limit; /* max attempts before FAILED; see _init() */
} rcp_pwrmode_handshake_t;

/* Initializes *hs to RCP_PWRMODE_HANDSHAKE_NOT_STARTED, zero attempts,
 * repeat_limit as given. repeat_limit == 0 is treated as "no attempts
 * permitted" (the very first rcp_pwrmode_handshake_wakeup_attempt() call
 * fails immediately with FAILED) rather than "unlimited" -- a caller
 * wanting a generous bound should pass an explicit large value instead. */
void rcp_pwrmode_handshake_init(rcp_pwrmode_handshake_t *hs, uint32_t repeat_limit);

/* Step (a). Requires hs->step == RCP_PWRMODE_HANDSHAKE_NOT_STARTED;
 * returns false (leaving hs unchanged) otherwise. REQ-PWRMODE-016 (TC18
 * §12.4.1): the interface is enabled, then network availability is
 * checked, before any WakeUp message is sent -- network_available is
 * that already-classified check's own result (e.g. "BEACONs detected by
 * the PHY", this module's own primitives do not read hardware
 * themselves, matching the rest of this codebase's "caller supplies
 * already-classified inputs" convention). false leaves hs at
 * NOT_STARTED (a cheap, retriable "not yet" -- a caller polls this
 * again once the network comes up; these retries are NOT counted
 * against wakeup_repeat_limit, which governs only step (b)'s own
 * WakeUp-message repetition once step (a) has actually advanced). On
 * success (hs->step was NOT_STARTED AND network_available), advances
 * hs->step to RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED and returns true. */
bool rcp_pwrmode_handshake_iface_reenabled(rcp_pwrmode_handshake_t *hs, bool network_available);

/* Step (b), one repeat of "send WakeUp, see if it comes back". Requires
 * hs->step == RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED; returns false
 * (leaving hs unchanged) otherwise. Each call increments
 * hs->wakeup_attempts. If echoed is true, advances hs->step to
 * RCP_PWRMODE_HANDSHAKE_ECHOED and returns true (step (b) has succeeded;
 * stop calling this function). If echoed is false and
 * hs->wakeup_attempts has now reached hs->wakeup_repeat_limit, advances
 * hs->step to RCP_PWRMODE_HANDSHAKE_FAILED and returns false (the
 * repeat-limit has been hit; stop calling this function and fall back to
 * a cold start). If echoed is false and the limit has not yet been
 * reached, hs->step is left at RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED and
 * true is returned (still pending; the caller should repeat the WakeUp
 * message and call this function again).
 *
 * REQ-PWRMODE-018 (TC18 §12.4.1): despite its name, echoed's real
 * meaning is broader than "an exact echo of the WakeUp message" -- the
 * specification's own termination condition is "a valid AVTPDU from the
 * sleep request Client", of which a literal WakeUp echo
 * (ep_wakeup.h's rcp_ep_wakeup_is_wakeup_echo(), and
 * powerstate.h's rcp_powerstate_manager_apply_wakeup_echo() built on
 * it) is one concrete, directly-testable example, not the only one --
 * this module's own name choice ("echoed"/RCP_PWRMODE_HANDSHAKE_ECHOED)
 * predates that closer reading and is kept for API stability, but a
 * caller that itself recognizes some OTHER valid, already-demultiplexed
 * frame from the sleep-request client (any operational or SleepCMD
 * request this library's own dispatch already routed to that client's
 * addr, not specifically a WakeUp echo) should pass echoed=true for it
 * too, satisfying step (b) exactly as the specification requires. There
 * is deliberately no separate repeat limit on TC18's own side -- the
 * wakeup_repeat_limit field and its FAILED outcome are this
 * implementation's own added safety bound, not spec-derived; TC18
 * itself defines no limit and expects the repetition to continue until
 * a valid response actually arrives. */
bool rcp_pwrmode_handshake_wakeup_attempt(rcp_pwrmode_handshake_t *hs, bool echoed);

/* Step (c). Requires hs->step == RCP_PWRMODE_HANDSHAKE_ECHOED; returns
 * false (leaving hs unchanged) otherwise. On success, advances hs->step
 * to RCP_PWRMODE_HANDSHAKE_COMPLETE and returns true. */
bool rcp_pwrmode_handshake_resume_queues(rcp_pwrmode_handshake_t *hs);

/* True iff hs->step == RCP_PWRMODE_HANDSHAKE_COMPLETE. */
bool rcp_pwrmode_handshake_is_complete(const rcp_pwrmode_handshake_t *hs);

/* True iff hs->step == RCP_PWRMODE_HANDSHAKE_FAILED. */
bool rcp_pwrmode_handshake_has_failed(const rcp_pwrmode_handshake_t *hs);

/* Attempts to wake *mode from Sleep back to Normal via the given path,
 * reporting the resulting start kind via *out_start_kind (if non-NULL).
 * Fails with RCP_PWRMODE_ERR_INVALID_TRANSITION (leaving *mode unchanged)
 * if *mode != RCP_PWRMODE_SLEEP.
 *
 * As of REQ-PWRMODE-020, path no longer changes this function's own
 * classification rule -- rcp_pwrmode_hotstart_required() is
 * unconditionally true for both RCP_PWRMODE_WAKE_VIA_PIN and
 * RCP_PWRMODE_WAKE_VIA_NETWORK (see that function's own doc comment for
 * the primary-source correction). Succeeds with *out_start_kind =
 * RCP_PWRMODE_START_HOT iff handshake is non-NULL and
 * rcp_pwrmode_handshake_is_complete(handshake); otherwise still succeeds
 * (the wake itself is never refused by this function -- only its
 * hot/cold classification depends on the handshake, including a NULL
 * handshake) but with *out_start_kind = RCP_PWRMODE_START_COLD, the
 * module's documented safe default whenever hot-start preconditions are
 * not met. */
rcp_pwrmode_errc_t rcp_pwrmode_wake_from_sleep(rcp_pwrmode_t *mode, rcp_pwrmode_wake_path_t path,
                                                const rcp_pwrmode_handshake_t *handshake,
                                                rcp_pwrmode_start_kind_t *out_start_kind);

/* ── Entry-refusal gating (StandBy and Sleep requests alike) ────────────────── */

typedef struct {
    bool wup_status_clear;     /* the wakeup endpoint's own wup_status has been cleared */
    bool endpoint_idle;        /* REQ-PWRMODE-025: server-wide -- true iff EVERY
                                   endpoint on this RC Server is idle, not just the
                                   one the request arrived on. A caller ANDs every
                                   endpoint's own idleness into this single bool
                                   before calling rcp_pwrmode_check_entry()/
                                   rcp_pwrmode_commit_entry(); this module reads no
                                   endpoint table of its own. */
    bool response_queue_empty; /* REQ-PWRMODE-025: server-wide -- true iff EVERY
                                   responder queue on this RC Server is empty, not
                                   just one. A caller ANDs every queue's own
                                   emptiness into this single bool the same way. */
} rcp_pwrmode_entry_gate_t;

typedef enum {
    RCP_PWRMODE_ENTRY_OK      = 0,
    RCP_PWRMODE_ENTRY_REFUSED = 1, /* the roadmap's REQUEST_CANCELED */
} rcp_pwrmode_entry_result_t;

/* The shared StandBy/Sleep entry-refusal rule -- see the file header.
 * Returns RCP_PWRMODE_ENTRY_REFUSED iff any of gate's three fields is
 * false (!wup_status_clear, !endpoint_idle, or !response_queue_empty);
 * RCP_PWRMODE_ENTRY_OK iff all three are true. gate == NULL is treated as
 * "nothing known" and therefore refused (fail-safe, mirroring
 * lifecycle.h's own NULL-snapshot-is-inconsistent convention).
 *
 * This is the FIRST of two gate evaluations a caller admitting a
 * StandBy/Sleep request runs -- see rcp_pwrmode_commit_entry() for the
 * second, authoritative one immediately before the mode actually
 * changes, and why a single check here is not sufficient by itself. */
rcp_pwrmode_entry_result_t rcp_pwrmode_check_entry(const rcp_pwrmode_entry_gate_t *gate);

/* The authoritative StandBy/Sleep-entry commit -- REQ-PWRMODE-024,
 * REQ-PWRMODE-026 (TC18 §12.5).
 *
 * A single rcp_pwrmode_check_entry() call at request-arrival time is not
 * enough: TC18 §12.5 requires that "if an event defined as wake-up source
 * in the wake-up endpoint occurs during the processing of the
 * sleep-request, the request is not executed" (REQ-PWRMODE-024 -- the
 * processing window between an initial admission check and the actual
 * mode change is exactly where such an event would otherwise be lost),
 * and separately that "the RC Server enters standby or sleep mode, as
 * soon as the go to sleep/standby response... has been sent"
 * (REQ-PWRMODE-026 -- the response must already be on the wire before the
 * mode changes, not merely queued to be sent).
 *
 * A caller therefore runs a two-step sequence, not one check:
 *   1. rcp_pwrmode_check_entry() against a gate sampled when the request
 *      arrives, to decide whether to encode+send an OK
 *      (RCP_PWRMODE_ENTRY_OK) or a REQUEST_CANCELED
 *      (RCP_PWRMODE_ENTRY_REFUSED) response at all.
 *   2. Only once step 1 said OK AND that response has actually been
 *      transmitted, call this function with target, a FRESHLY re-sampled
 *      gate (a new sample, not the same struct reused from step 1 --
 *      reusing it would silently reopen the very race this function
 *      exists to close), and response_sent = true.
 *
 * gate is re-validated internally via rcp_pwrmode_check_entry() itself
 * (closing REQ-PWRMODE-024's race by construction: any wake-source event
 * the caller's re-sample picked up refuses this call exactly as it would
 * have refused step 1). response_sent is this library's own caller-
 * supplied proof of REQ-PWRMODE-026's ordering precondition -- mirroring
 * rcp_pwrmode_handshake_iface_reenabled()'s own network_available
 * argument, this module does no I/O of its own and cannot observe
 * transmission completion directly, so a caller that has not yet sent the
 * response must pass false (or simply not call this function yet).
 *
 * On success (*mode is RCP_PWRMODE_NORMAL or RCP_PWRMODE_STANDBY, target
 * is RCP_PWRMODE_STANDBY or RCP_PWRMODE_SLEEP, gate is non-NULL and
 * passes rcp_pwrmode_check_entry(), and response_sent is true), *mode is
 * updated to target, *out_start_kind (if non-NULL) is set via the same
 * rule rcp_pwrmode_transition() itself uses, and RCP_PWRMODE_OK is
 * returned.
 *
 * Otherwise *mode is left unchanged: RCP_PWRMODE_ERR_ENTRY_REFUSED is
 * returned if gate is NULL, or fails rcp_pwrmode_check_entry(), or if
 * response_sent is false; RCP_PWRMODE_ERR_INVALID_TRANSITION is returned
 * if *mode/target fall outside the admitted StandBy/Sleep-entry set
 * (mirroring rcp_pwrmode_transition()'s own fail-closed convention for
 * structurally invalid moves) -- checked only once the gate/response_sent
 * preconditions above have already passed. */
rcp_pwrmode_errc_t rcp_pwrmode_commit_entry(rcp_pwrmode_t *mode, rcp_pwrmode_t target,
                                             const rcp_pwrmode_entry_gate_t *gate,
                                             bool response_sent,
                                             rcp_pwrmode_start_kind_t *out_start_kind);

#ifdef __cplusplus
}
#endif

#endif /* RCP_POWER_H */
