/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-LIFECYCLE-001
//cfusa:req REQ-LIFECYCLE-002
//cfusa:req REQ-LIFECYCLE-003
//cfusa:req REQ-LIFECYCLE-004
//cfusa:req REQ-LIFECYCLE-005
//cfusa:req REQ-LIFECYCLE-006
//cfusa:req REQ-LIFECYCLE-007
//cfusa:req REQ-LIFECYCLE-008
//cfusa:req REQ-LIFECYCLE-009
//cfusa:req REQ-LIFECYCLE-010
//cfusa:req REQ-LIFECYCLE-011
//cfusa:req REQ-LIFECYCLE-012
//cfusa:req REQ-LIFECYCLE-013
//cfusa:req REQ-LIFECYCLE-014
//cfusa:req REQ-LIFECYCLE-015
//cfusa:req REQ-LIFECYCLE-016
//cfusa:req REQ-LIFECYCLE-017
//cfusa:req REQ-LIFECYCLE-018
//cfusa:req REQ-LIFECYCLE-019
//cfusa:req REQ-LIFECYCLE-020
//cfusa:req REQ-LIFECYCLE-021

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-LIFECYCLE-022
//cfusa:req REQ-LIFECYCLE-023
//cfusa:req REQ-LIFECYCLE-024
//cfusa:req REQ-LIFECYCLE-025
//cfusa:req REQ-LIFECYCLE-026
//cfusa:req REQ-LIFECYCLE-027
//cfusa:req REQ-LIFECYCLE-028
//cfusa:req REQ-LIFECYCLE-029
//cfusa:req REQ-LIFECYCLE-030
//cfusa:req REQ-LIFECYCLE-031
//cfusa:req REQ-LIFECYCLE-032
//cfusa:req REQ-LIFECYCLE-033
//cfusa:req REQ-LIFECYCLE-034
//cfusa:req REQ-LIFECYCLE-035
//cfusa:req REQ-LIFECYCLE-036
//cfusa:req REQ-LIFECYCLE-037
/*
 * lifecycle.h -- RC Server lifecycle state machine for the TC18 Remote
 * Control Protocol wire layer (ROADMAP.md Phase 14, "RC Server Lifecycle &
 * Register Map", milestone 61).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59) and ACF message format (acf.h/acf.c,
 * milestone 60). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, or any
 * satellite package is touched here.
 *
 * Split out of the original server.h/server.c (milestone 61) by the
 * module-naming reconciliation tracked at github.com/SoundMatt/c-RCP/issues/87,
 * which brought this repo in line with RELAY spec v1.14's expanded §13.7.2
 * module-name registry: the registry's `lifecycle` entry names "RC Server
 * lifecycle state machine" specifically, distinct from the per-endpoint
 * ep_enable pre-load-then-drain queue mechanism server.h/server.c also used
 * to bundle -- that queue mechanism has no registry entry of its own (it
 * remains in server.h/server.c, unconstrained) and is not a lifecycle
 * concern. This split is a pure rename/relocation: no transition rule,
 * plausibility check, or field-writability policy changed. Requirement ids
 * were renumbered from the original REQ-SRV-001..020,024 to
 * REQ-LIFECYCLE-001..021 (same relative order) to match the new module
 * name, mirroring cpp-RCP's own REQ-LIFECYCLE-* prefix for its
 * (differently-shaped but same-concern) lifecycle.hpp; REQ-SRV-021..023
 * (the endpoint-queue requirements) stayed behind in server.h, renumbered
 * REQ-SRV-001..003 since they are now that file's entire requirement set.
 *
 * An RC Server's own bring-up progresses through exactly three lifecycle
 * states (this module's own original engineering design for representing
 * that progression -- only the state names, their high-level meaning, and
 * their three on-wire byte values are taken by reference from the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC; no spec text is reproduced here):
 *
 *   - HW_UNCONFIGURED (0x00): the server's boot default. No endpoint has a
 *     usable hardware mapping yet. Only the discovery/bootstrap channel is
 *     reachable.
 *   - HW_CONFIGURED (0x55): every endpoint marked in-use now has a plausible
 *     hardware pin mapping and at least one configured request stream.
 *     Hardware-level configuration becomes read-only from this point on.
 *   - RCP_CONFIGURED (0xAA): every in-use endpoint additionally has a
 *     stream/byte_bus_id association, and every configured request stream
 *     has an associated response stream. Only functional (not hardware)
 *     configuration remains writable, and only through an endpoint's own
 *     registered stream(s) or the root client via EP0.
 *
 * The full register-map layout backing "hardware pin mapping", "request
 * stream", "response stream", EP0, and the root-client model is milestone
 * 62's job (ROADMAP.md, "Register-map model"). This module only needs a
 * minimal, self-contained stand-in surface -- the
 * rcp_lifecycle_plausibility_snapshot_t below -- sufficient to make its own
 * transition guards and per-state filtering testable; it deliberately does
 * not reach ahead into milestone 62's register tables.
 */
#ifndef RCP_LIFECYCLE_H
#define RCP_LIFECYCLE_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/errors.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle states ──────────────────────────────────────────────────────── */

/* The three RC Server lifecycle states and their defined on-wire byte
 * values. Ordering here follows the server's normal forward bring-up
 * progression; see rcp_lifecycle_transition() for which transitions
 * (forward and demotion) are actually permitted. */
typedef enum {
    RCP_LIFECYCLE_HW_UNCONFIGURED = 0x00,
    RCP_LIFECYCLE_HW_CONFIGURED   = 0x55,
    RCP_LIFECYCLE_RCP_CONFIGURED  = 0xAA,
} rcp_lifecycle_state_t;

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_LIFECYCLE_OK                      = 0,
    RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT  = 1,
    RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT = 2,
    RCP_LIFECYCLE_ERR_INVALID_TRANSITION   = 3,
    RCP_LIFECYCLE_ERR_UNAUTHORIZED         = 4, /* REQ-LIFECYCLE-031: writer
                                                    not permitted to request
                                                    this svr_lifecycle_state
                                                    change */
    RCP_LIFECYCLE_ERR_EPS_NOT_IDLE         = 5, /* REQ-LIFECYCLE-022: another
                                                    endpoint still has an
                                                    in-flight or queued
                                                    request; TC18 Figure 16's
                                                    own diagram-only "EPs_NOT_
                                                    IDLE" outcome, which maps
                                                    to none of the seventeen
                                                    numbered wire error codes
                                                    (errors.h) -- a genuine
                                                    spec inconsistency, not
                                                    something this library
                                                    can resolve by inventing
                                                    a mapping; this is a
                                                    local-only error code
                                                    until/unless TC18
                                                    clarifies one */
} rcp_lifecycle_errc_t;

/* Human-readable message for an rcp_lifecycle_errc_t value. Never returns NULL. */
const char *rcp_lifecycle_strerror(rcp_lifecycle_errc_t e);

/* ── Plausibility snapshot (transition-guard input) ────────────────────────── */

/* One endpoint's configuration state, as far as this milestone's transition
 * guards need to see it. The full per-endpoint register layout (ep_type,
 * generic-vs-functional config split, ...) is milestone 62's job; this
 * struct is deliberately a minimal stand-in carrying only the four facts
 * the two plausibility checks below actually consult. */
typedef struct {
    bool ep_used;            /* this endpoint slot is in use */
    bool hw_pin_mapped;      /* a valid HW pin mapping is present */
    bool has_request_stream; /* at least one configured request stream exists
                                 for this endpoint */
    bool has_stream_assoc;   /* a stream/byte_bus_id association exists for
                                 this endpoint */
} rcp_lifecycle_endpoint_plausibility_t;

/* One request stream's configuration state, as far as the RCP_CFG_INCONSISTENT
 * guard needs to see it. */
typedef struct {
    bool configured;          /* this request stream slot is configured */
    bool has_response_stream; /* an associated response stream exists */
} rcp_lifecycle_request_stream_plausibility_t;

/* A read-only view over every endpoint and request stream slot, passed to
 * the plausibility checks and to rcp_lifecycle_transition(). Neither
 * array is copied or retained beyond the call. */
typedef struct {
    const rcp_lifecycle_endpoint_plausibility_t *endpoints;
    size_t                                     endpoint_count;
    const rcp_lifecycle_request_stream_plausibility_t *request_streams;
    size_t                                           request_stream_count;
} rcp_lifecycle_plausibility_snapshot_t;

/* The HW_CFG_INCONSISTENT plausibility check: returns RCP_LIFECYCLE_OK iff
 * every endpoint with ep_used set has both hw_pin_mapped and
 * has_request_stream set. Endpoints with ep_used == false are ignored.
 * snap == NULL is treated as inconsistent (fail-safe: a transition attempt
 * with no configuration evidence at all must not be treated as vacuously
 * plausible). */
rcp_lifecycle_errc_t rcp_lifecycle_check_hw_cfg(const rcp_lifecycle_plausibility_snapshot_t *snap);

/* The RCP_CFG_INCONSISTENT plausibility check: returns RCP_LIFECYCLE_OK iff
 * every endpoint with ep_used set has has_stream_assoc set, and every
 * request stream with configured set has has_response_stream set. snap ==
 * NULL is treated as inconsistent, for the same fail-safe reason as above. */
rcp_lifecycle_errc_t rcp_lifecycle_check_rcp_cfg(const rcp_lifecycle_plausibility_snapshot_t *snap);

/* Identifies who is attempting a write -- a functional-config write (the
 * once-RCP_CONFIGURED/HW_CONFIGURED authorization rule, see
 * rcp_lifecycle_field_writable()) or a svr_lifecycle_state change (see
 * rcp_lifecycle_transition()). Declared here, ahead of both consumers,
 * since rcp_lifecycle_transition() now needs it too (REQ-LIFECYCLE-031).
 * Any combination of members may be true (e.g. the root client happens
 * to also be the owning stream); only one authorizing condition needs to
 * be true for a given call's own authorization rule to be satisfied.
 *
 * via_non_unicast_frame and via_discovery_stream both default to false
 * (the common, compliant-or-inapplicable case) on a plain {0}/partial-
 * brace initializer, so every writer_ctx literal already in this
 * codebase before their respective introduction (REQ-LIFECYCLE-027 and
 * REQ-LIFECYCLE-030/036) continues to mean exactly what it meant before
 * -- only a caller that actually needs to exercise the new rule has to
 * set the relevant member explicitly. See rcp_lifecycle_field_writable()'s
 * own doc comment and l2.h's rcp_l2_mac_is_unicast() for the primitive an
 * integrator uses to classify a real frame's destination MAC before
 * setting via_non_unicast_frame. */
typedef struct {
    bool via_root_client_ep0;   /* request arrived via EP0 from the root client */
    bool via_owning_stream;     /* request arrived via the endpoint's own
                                    registered request stream */
    bool via_non_unicast_frame; /* true iff the request frame's destination
                                    MAC was multicast or broadcast, not
                                    unicast (REQ-LIFECYCLE-027) */
    bool via_discovery_stream;  /* request arrived via the discovery stream
                                    (REQ-LIFECYCLE-030/031/036) */
} rcp_lifecycle_writer_ctx_t;

/* ── Lifecycle transitions ─────────────────────────────────────────────────── */

/* Attempts to move *state to target on behalf of writer, given whether
 * all_other_eps_idle. On success, *state is updated to target and
 * RCP_LIFECYCLE_OK is returned; on failure *state is left unchanged and
 * the failure reason is returned. Permitted transitions:
 *
 *   - HW_UNCONFIGURED -> HW_CONFIGURED: guarded by idleness (see below),
 *     then by rcp_lifecycle_check_hw_cfg(); a plausibility failure
 *     returns RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT. writer is not
 *     consulted for this transition -- TC18 §12.3.1.1 requires only that
 *     the request travel via the discovery stream, already enforced one
 *     layer up by rcp_lifecycle_should_accept()'s HW_UNCONFIGURED branch
 *     (no root client can exist yet at this point in bring-up).
 *   - HW_CONFIGURED -> RCP_CONFIGURED: guarded first by writer authorization
 *     (see below), then by rcp_lifecycle_check_rcp_cfg(); a plausibility
 *     failure returns RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT. NOT
 *     idle-gated -- see idleness paragraph below.
 *   - HW_CONFIGURED -> HW_UNCONFIGURED and RCP_CONFIGURED ->
 *     HW_UNCONFIGURED: the discovery-stream/root-client reset path,
 *     guarded by the same writer authorization as the advance above and
 *     by idleness (see below); snap is ignored for these two once
 *     authorized and idle.
 *   - state -> the same state: always a no-op success; writer, snap and
 *     all_other_eps_idle are all ignored.
 *
 * Writer authorization (REQ-LIFECYCLE-031, TC18 §12.3.1.2): both the
 * HW_CONFIGURED -> RCP_CONFIGURED advance and either reset-to-
 * HW_UNCONFIGURED transition require writer.via_discovery_stream or
 * writer.via_root_client_ep0 -- an unauthorized writer's request is
 * rejected with RCP_LIFECYCLE_ERR_UNAUTHORIZED, *state left unchanged,
 * before either transition's own plausibility guard runs. TC18's exact
 * text additionally narrows the non-discovery-stream case further --
 * "If a root client is configured only the root client's stream_id/
 * byte_bus_id is accepted", implying any valid stream_id/byte_bus_id
 * combination suffices only when no root client is configured at all --
 * a distinction this library cannot yet express (no wire-level concept
 * of "a valid stream_id/byte_bus_id combination" independent of a
 * specific endpoint's own owning stream exists for a server-level field
 * like svr_lifecycle_state; see REQ-LIFECYCLE-025/034's own architecture
 * finding for the same underlying gap). This function conservatively
 * requires via_root_client_ep0 in both cases until that classification
 * exists, rather than accepting an unqualified stream this library
 * cannot actually validate.
 *
 * Idleness (REQ-LIFECYCLE-022, TC18 Figure 16): the HW_UNCONFIGURED ->
 * HW_CONFIGURED advance and either reset-to-HW_UNCONFIGURED transition
 * both require all_other_eps_idle -- an in-flight or queued request on
 * another endpoint rejects the transition with
 * RCP_LIFECYCLE_ERR_EPS_NOT_IDLE, *state left unchanged, checked before
 * (for the advance) or after (for the reset, alongside writer
 * authorization) each transition's own other guards. The
 * HW_CONFIGURED -> RCP_CONFIGURED advance is deliberately NOT idle-
 * gated: Figure 16's own label for that transition ("...& RCP_config
 * consistent -> send positive response") makes no mention of endpoint
 * idleness, unlike the two transitions above/below it. Figure 16 itself
 * names this outcome "EPs_NOT_IDLE" but that name maps to none of the
 * seventeen numbered wire error codes errors.h models (§12.9.6's own
 * table) -- a genuine TC18 inconsistency this library cannot resolve by
 * inventing a mapping, so RCP_LIFECYCLE_ERR_EPS_NOT_IDLE is a
 * local-only error code for now.
 *
 * Any other requested transition (e.g. skipping straight from
 * HW_UNCONFIGURED to RCP_CONFIGURED, or downgrading from RCP_CONFIGURED to
 * HW_CONFIGURED without first returning all the way to HW_UNCONFIGURED) is
 * rejected with RCP_LIFECYCLE_ERR_INVALID_TRANSITION, regardless of
 * writer or idleness. */
rcp_lifecycle_errc_t rcp_lifecycle_transition(rcp_lifecycle_state_t *state,
                                               rcp_lifecycle_state_t target,
                                               const rcp_lifecycle_plausibility_snapshot_t *snap,
                                               rcp_lifecycle_writer_ctx_t writer,
                                               bool all_other_eps_idle);

/* ── Per-state request filtering ───────────────────────────────────────────── */

/* The discovery byte_bus_id: the one address reachable while the server is
 * still HW_UNCONFIGURED. */
#define RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID ((rcp_byte_bus_id_t)0u)

/* The per-state request-filtering rule, as its own directly-tested
 * function (mirroring avtp.c's rcp_avtp_should_drop_tscf() convention):
 *
 *   - Whatever the state, a TSCF-headed frame is first subject to
 *     rcp_avtp_should_drop_tscf()'s ordinary time-sync rule.
 *   - While HW_UNCONFIGURED: a TSCF-headed frame is dropped outright
 *     regardless of time_sync_supported (presentation-time semantics
 *     presuppose a configured request stream, which cannot exist yet), and
 *     an NTSCF-headed frame is accepted only if it carries an ACF_ABB
 *     message addressed to RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID; everything
 *     else is silently dropped.
 *   - While HW_CONFIGURED: a TSCF-headed frame is dropped outright too,
 *     for the same reason and regardless of time_sync_supported --
 *     TSCF's presentation-time semantics still presuppose a validated
 *     stream/byte_bus_id mapping and response queues, which do not exist
 *     until RCP_CONFIGURED (TC18 §12.3.1.2). Acceptance is further
 *     restricted to RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID (EP0): TC18
 *     §12.3.1.2 requires "requests to EPs other than EP0 that are not
 *     config requests" to be dropped, and this library has no wire-level
 *     encode/decode pair for a functional-configuration read/write
 *     request at all yet (regmap.h's own file header defers that to a
 *     later phase), so every currently-decodable non-EP0 request is, by
 *     construction, operational -- the EP0-only restriction is the
 *     honestly-achievable form of that rule given this library's real
 *     current scope. This does not (yet) also drop ACF_GBB-format
 *     requests addressed to EP0 itself, which TC18 §12.3.1.2 separately
 *     requires: every conditional request kind is wire-encoded as
 *     ACF_GBB unconditionally (see request_compound.h and siblings), and
 *     with the byte_bus_id restriction above already excluding every
 *     other target, this residual only matters for a conditional request
 *     literally addressed to EP0 -- tracked as its own follow-up, not
 *     this function's current scope (see rcp_lifecycle_should_accept()'s
 *     own .c-file comment for the full reasoning).
 *   - While RCP_CONFIGURED: acceptance beyond the general time-sync rule
 *     already applied above is unrestricted at this milestone -- the
 *     validated mapping HW_CONFIGURED's rules are guarding against now
 *     exists.
 *
 * avtp_subtype is one of RCP_AVTP_SUBTYPE_NTSCF/_TSCF (see avtp.h);
 * acf_msg_type is one of RCP_ACF_MSG_TYPE_ABB/_GBB (see acf.h), or any
 * other value for a message type this filtering rule does not special-case. */
bool rcp_lifecycle_should_accept(rcp_lifecycle_state_t state,
                                  bool time_sync_supported,
                                  uint8_t avtp_subtype,
                                  uint8_t acf_msg_type,
                                  rcp_byte_bus_id_t byte_bus_id);

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

/* Which broad category a register field falls into for locking purposes.
 * HW_GENERIC covers HW-pin-mapping (TC18 Figure 16's "HW_CONFIG") and
 * every other block Figure 16 groups under the identical HW_UNCONFIGURED-
 * only locking rule and identical LOCKED_CONFIG_ACCESS response --
 * REQ-LIFECYCLE-023: this includes the endpoint-generic configuration
 * (rcp_regmap_ep_generic_cfg_t, Figure 16's "EP_GEN_CFG") and the
 * response-queue/request-stream configuration (rcp_regmap_response_
 * queue_cfg_t / rcp_regmap_request_stream_cfg_t not already covered by
 * Table 22's own separate W* legend, Figure 16's "QUEUE_CFG") -- these
 * are deliberately NOT modeled as distinct enum values, since their
 * writability rule is identical to HW_GENERIC's own, and this codebase's
 * own convention (see FUNCTIONAL_W vs. FUNCTIONAL_W_STAR immediately
 * below) is to add a new enum value only when behavior actually differs,
 * not to mirror the register map's own struct boundaries one-for-one.
 * FUNCTIONAL_W and FUNCTIONAL_W_STAR both cover functional configuration
 * but differ in what happens once RCP_CONFIGURED is reached -- modeled
 * as two distinct enum values rather than one writability bit, per this
 * milestone's explicit scope. */
typedef enum {
    RCP_LIFECYCLE_FIELD_HW_GENERIC        = 0,
    RCP_LIFECYCLE_FIELD_FUNCTIONAL_W      = 1,
    RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR = 2,
} rcp_lifecycle_field_kind_t;

/* True iff a field of the given kind is writable while the server is in
 * state, by the given writer:
 *
 *   - RCP_LIFECYCLE_FIELD_HW_GENERIC: writable only in HW_UNCONFIGURED --
 *     read-only from the moment the server reaches HW_CONFIGURED, for any
 *     writer.
 *   - RCP_LIFECYCLE_FIELD_FUNCTIONAL_W: not writable in HW_UNCONFIGURED
 *     (functional configuration presupposes a hardware mapping); while
 *     HW_CONFIGURED, writable only when writer indicates the root client
 *     via EP0, the endpoint's own owning stream, or the discovery stream
 *     (TC18 §12.3.1.2/§12.7.3 -- REQ-LIFECYCLE-030/036); once
 *     RCP_CONFIGURED, writable only when writer indicates the endpoint's
 *     own stream or the root client via EP0 (the discovery stream no
 *     longer suffices on its own at this state -- a distinct, still-open
 *     concern tracked separately, see test_tc18_gaps_server.c's own
 *     §12.7.4 deviation pin).
 *   - RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR: writable unconditionally
 *     while HW_UNCONFIGURED; the same root-client/owning-stream/
 *     discovery-stream authorization as FUNCTIONAL_W above while
 *     HW_CONFIGURED; permanently locked (unwritable by any writer, not
 *     just an unauthorized one) once RCP_CONFIGURED is reached -- this
 *     last distinction is the one the roadmap requires be modeled
 *     explicitly rather than collapsed into a single writability bit.
 *
 * Independently of all cases above: TC18 §12.3.1.1, §12.3.1.2 and
 * §12.3.1.3 each state (once per lifecycle state) that a write request is
 * accepted only when sent in a unicast frame. This is ANDed in uniformly
 * across every kind/state rather than duplicated per-branch above --
 * whatever else applies, a field otherwise writable is unwritable when
 * writer.via_non_unicast_frame is true (REQ-LIFECYCLE-027). */
bool rcp_lifecycle_field_writable(rcp_lifecycle_state_t state,
                                   rcp_lifecycle_field_kind_t kind,
                                   rcp_lifecycle_writer_ctx_t writer);

/* REQ-LIFECYCLE-024: distinguishes WHY rcp_lifecycle_field_writable()
 * denied a write, mapping to the wire error code TC18 actually assigns
 * each reason -- RCP_ERROR_NONE if it did not deny it.
 *
 * Two distinct TC18 sources, both verified directly against the
 * primary-source PDF (initially only §13.7.1.2's prose was checked,
 * which is necessary but not sufficient -- Figure 16's own diagram
 * gives a second, more specific rule the prose alone does not surface):
 *
 *   - RCP_ERROR_LOCKED_MEM_ACCESS: state alone forbids the write -- even
 *     a maximally-privileged writer (root client, owning stream,
 *     discovery stream, unicast frame) would still be denied. This is
 *     Figure 16's own HW_CONFIGURED-box transition: "Request on
 *     discovery stream or known stream/bb_id for configuration to
 *     HW_CONFIG or QUEUE_CFG or EP_GEN_CFG -> send error response
 *     LOCKED_CONFIG_ACCESS" -- a diagram-only name that does not
 *     literally match any of the seventeen numbered wire error codes'
 *     own strings (§12.9.6's table), but is unambiguously the same
 *     concept as RCP_ERROR_LOCKED_MEM_ACCESS (4), the only numbered
 *     code with a semantically matching name -- the same kind of
 *     prose-vs-table naming variance already documented for
 *     POCI_FAILURE (rcp_e2e_wire_error()'s own doc comment). Every
 *     RCP_LIFECYCLE_FIELD_HW_GENERIC denial is exactly this case (its
 *     own writability rule has no authorization concept at all, only a
 *     state one -- REQ-LIFECYCLE-023's HW_CONFIG/QUEUE_CFG/EP_GEN_CFG
 *     blocks all fall under this kind, per rcp_lifecycle_field_kind_t's
 *     own doc comment); FUNCTIONAL_W_STAR once RCP_CONFIGURED is the
 *     same case for a different kind.
 *   - RCP_ERROR_UNAUTHORIZED_ACCESS: state would otherwise permit the
 *     write, but writer specifically is not authorized for it (an
 *     unauthorized writer identity, or a non-unicast frame,
 *     REQ-LIFECYCLE-027/030/031/036) -- TC18 §13.7.1.2's own separate,
 *     narrower example: "Writing to a write prohibited register (e.g.
 *     lock bit for map set) creates a response with err=1 and an error
 *     code UNAUTHORIZED_ACCESS."
 *
 * Implemented via two calls to rcp_lifecycle_field_writable() itself
 * (the real writer, then a maximally-privileged one) rather than a
 * second, separately-maintained copy of its state/kind table -- the two
 * can never drift out of sync with each other by construction. */
rcp_wire_error_t rcp_lifecycle_field_write_error(rcp_lifecycle_state_t state,
                                                  rcp_lifecycle_field_kind_t kind,
                                                  rcp_lifecycle_writer_ctx_t writer);

#ifdef __cplusplus
}
#endif

#endif /* RCP_LIFECYCLE_H */
