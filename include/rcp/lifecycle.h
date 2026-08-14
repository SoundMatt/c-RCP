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
    size_t request_stream_index; /* REQ-LIFECYCLE-038 (issue #201): which
                                 snap->request_streams[] slot this
                                 endpoint's own has_stream_assoc refers to
                                 -- meaningless while has_stream_assoc is
                                 false. Placed as this struct's own LAST
                                 field, matching this codebase's
                                 established convention (see e.g.
                                 rcp_regmap_ep_id_map_entry_t's own
                                 request_stream_index field comment,
                                 regmap.h) so every existing positional-
                                 initializer test call site keeps
                                 compiling unchanged -- a brace-list
                                 initializer shorter than this struct's
                                 own field count zero-initializes this
                                 trailing field, and 0 is a safe default
                                 (it only matters when has_stream_assoc
                                 is also explicitly set true, which no
                                 pre-existing call site combines with an
                                 intentionally-nonzero stream index; see
                                 rcp_lifecycle_check_rcp_cfg()'s own new
                                 bullet-2 cross-reference this field
                                 exists to feed). */
} rcp_lifecycle_endpoint_plausibility_t;

/* One request stream's configuration state, as far as the RCP_CFG_INCONSISTENT
 * guard needs to see it. */
typedef struct {
    bool configured;          /* this request stream slot is configured */
    bool has_response_stream; /* an associated response stream exists */
    size_t response_stream_index; /* REQ-RMAP-049 (issue #338): which
                                 snap->own response_stream_count-space
                                 slot this stream's own has_response_
                                 stream refers to -- meaningless while
                                 has_response_stream is false. This is
                                 rx_resp_stream_index's real-slot
                                 counterpart, the caller-supplied cross-
                                 reference REQ-RMAP-049's own "STILL
                                 PARTIAL" text named as missing: knowing
                                 SOME response stream exists
                                 (has_response_stream) is not the same as
                                 knowing it names a real, present one.
                                 0-based direct indexing, the SAME
                                 translation rcp_lifecycle_endpoint_
                                 plausibility_t's own request_stream_index
                                 field already establishes (REQ-LIFECYCLE-
                                 038) -- deliberately NOT rx_resp_stream_
                                 index's own 1-based/0-sentinel wire
                                 encoding (regmap.h); a caller building
                                 this snapshot from a live register map
                                 performs that translation itself (e.g.
                                 rx_resp_stream_index - 1 once it has
                                 already confirmed rx_resp_stream_index is
                                 nonzero), the same way it already must
                                 for request_stream_index. Placed as this
                                 struct's own LAST field, matching that
                                 same established convention, so every
                                 existing positional-initializer test call
                                 site keeps compiling unchanged -- a
                                 brace-list initializer shorter than this
                                 struct's own field count zero-initializes
                                 this trailing field, and 0 is a safe
                                 default only when has_response_stream is
                                 also left false (the same "only matters
                                 when the sibling bool is true" caveat
                                 request_stream_index's own field comment
                                 already states). */
} rcp_lifecycle_request_stream_plausibility_t;

/* A read-only view over every endpoint and request stream slot, passed to
 * the plausibility checks and to rcp_lifecycle_transition(). Neither
 * array is copied or retained beyond the call. */
typedef struct {
    const rcp_lifecycle_endpoint_plausibility_t *endpoints;
    size_t                                     endpoint_count;
    const rcp_lifecycle_request_stream_plausibility_t *request_streams;
    size_t                                           request_stream_count;
    size_t response_stream_count; /* REQ-RMAP-049 (issue #338): how many
                                 real response/ack-queue slots exist --
                                 the space rcp_lifecycle_request_stream_
                                 plausibility_t's own new response_stream_
                                 index field (above) is validated against.
                                 Placed as this struct's own LAST field for
                                 the same positional-initializer-
                                 compatibility reason as that field; a
                                 4-value legacy initializer (e.g. this
                                 codebase's own EMPTY_SNAP test constants)
                                 zero-initializes it, matching
                                 response_stream_count == 0's own correct
                                 meaning ("no response streams exist"). */
} rcp_lifecycle_plausibility_snapshot_t;

/* The HW_CFG_INCONSISTENT plausibility check: returns RCP_LIFECYCLE_OK iff
 * every endpoint with ep_used set has both hw_pin_mapped and
 * has_request_stream set. Endpoints with ep_used == false are ignored.
 * snap == NULL is treated as inconsistent (fail-safe: a transition attempt
 * with no configuration evidence at all must not be treated as vacuously
 * plausible). */
rcp_lifecycle_errc_t rcp_lifecycle_check_hw_cfg(const rcp_lifecycle_plausibility_snapshot_t *snap);

/* The RCP_CFG_INCONSISTENT plausibility check: returns RCP_LIFECYCLE_OK iff
 * (1) every endpoint with ep_used set has has_stream_assoc set, (2) every
 * request stream with configured set has has_response_stream set, and (3)
 * every request stream with configured set is referenced by at least one
 * endpoint's own request_stream_index (only endpoints with has_stream_assoc
 * set are consulted for (3) -- an endpoint with has_stream_assoc false has
 * no meaningful request_stream_index to reference anything with). snap ==
 * NULL is treated as inconsistent, for the same fail-safe reason as above.
 *
 * FIXED 2026-08-12 (issue #201, REQ-LIFECYCLE-038): TC18 §12.3.1.2 names a
 * THIRD RCP_CFG_INCONSISTENT bullet, "For each configured stream at least
 * one stream_id/byte_bus_id is configured" -- the mirror-image of check
 * (1) above (that check catches a used endpoint with no stream; this one
 * catches a configured stream with no endpoint using it, an orphaned,
 * unused stream slot). rcp_lifecycle_endpoint_plausibility_t's own new
 * request_stream_index field (see that struct's own declaration, above)
 * is the caller-supplied cross-reference this check needed and did not
 * have before this fix.
 *
 * WIDENED 2026-08-13 (issue #338, REQ-RMAP-049): check (2) above now also
 * requires that a stream's own response_stream_index (rcp_lifecycle_
 * request_stream_plausibility_t's own new field, see that struct's own
 * declaration above) actually names a real response-stream slot --
 * response_stream_index < snap->response_stream_count -- not merely that
 * has_response_stream is set. REQ-RMAP-049's own text named this the
 * still-open half of its own gap: has_response_stream alone only proves
 * SOME association was recorded, not that rx_resp_stream_index resolves
 * to a response/ack queue that actually exists. */
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
    bool via_valid_stream_association; /* request arrived via A stream_id/
                                    byte_bus_id combination that is a real,
                                    currently-configured EP_ID_config
                                    association -- not necessarily the
                                    specific endpoint's own owning one, and
                                    ONLY ever true when no root client is
                                    configured at all (TC18 §12.3.1.2:
                                    "the request needs to come either via
                                    the discovery stream or via a valid
                                    stream_id/byte_bus_id combination. If a
                                    root client is configured only the root
                                    client's stream_id/byte_bus_id is
                                    accepted"). REQ-LIFECYCLE-025/031: see
                                    regmap.h's rcp_regmap_writer_ctx() for
                                    how this member is derived -- it bakes
                                    the "no root client configured"
                                    condition in directly, the same
                                    pattern via_root_client_ep0 already
                                    uses for its own root-client-index
                                    check, so a consumer of this struct
                                    never separately re-checks root-client
                                    state before trusting it. Defaults to
                                    false on a plain {0}/partial-brace
                                    initializer, the same convention every
                                    other member here already follows --
                                    every writer_ctx literal already in
                                    this codebase before this member's
                                    introduction continues to mean exactly
                                    what it meant before. */
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
 *   - HW_CONFIGURED -> HW_UNCONFIGURED: the discovery-stream/root-client
 *     reset path, guarded by the same writer authorization as the advance
 *     above and by idleness (see below); snap is ignored once authorized
 *     and idle.
 *   - RCP_CONFIGURED -> HW_UNCONFIGURED: the same reset path, but from
 *     RCP_CONFIGURED specifically -- guarded by writer.via_root_client_ep0
 *     ALONE (REQ-LIFECYCLE-037, TC18 §12.7.4) and by idleness; snap is
 *     ignored once authorized and idle. See the writer-authorization
 *     paragraph below for why this differs from the HW_CONFIGURED case.
 *   - state -> the same state: always a no-op success; writer, snap and
 *     all_other_eps_idle are all ignored.
 *
 * Writer authorization (REQ-LIFECYCLE-031, TC18 §12.3.1.2): the
 * HW_CONFIGURED -> RCP_CONFIGURED advance and the HW_CONFIGURED ->
 * HW_UNCONFIGURED reset both require writer.via_discovery_stream,
 * writer.via_root_client_ep0, or writer.via_valid_stream_association --
 * an unauthorized writer's request is rejected with
 * RCP_LIFECYCLE_ERR_UNAUTHORIZED, *state left unchanged, before either
 * transition's own plausibility guard runs. TC18's exact text further
 * narrows the non-discovery-stream case: "If a root client is configured
 * only the root client's stream_id/byte_bus_id is accepted", implying any
 * valid stream_id/byte_bus_id combination suffices only when no root
 * client is configured at all -- RESOLVED 2026-08-14 (issue #341 lineage):
 * writer.via_valid_stream_association (rcp_lifecycle_writer_ctx_t, above)
 * already bakes that "no root client configured" condition in at its own
 * construction site (regmap.h's rcp_regmap_writer_ctx(), built on the new
 * rcp_regmap_ep_id_map_is_valid_association() query over TC18 §12.7.8's
 * own EP_ID_config table), so simply OR-ing it into this function's own
 * authorization check is sufficient and cannot wrongly widen access when
 * a root client IS configured (the member is always false in that case,
 * by construction at its source). See REQ-LIFECYCLE-025/034's own
 * architecture findings -- a related but textually distinct question
 * (RCP_CONFIGURED's own request-filtering behavior for an unrecognized
 * stream/byte_bus_id, not this function's HW_CONFIGURED-state writer
 * authorization) -- for why that specific finding remains open spec
 * silence rather than closed by this same primitive.
 *
 * The RCP_CONFIGURED -> HW_UNCONFIGURED reset is narrower still
 * (REQ-LIFECYCLE-037, TC18 §12.7.4): "Changes in configuration via a
 * discovery request are no longer allowed" once RCP_CONFIGURED, so
 * writer.via_discovery_stream alone -- sufficient for the
 * HW_CONFIGURED -> HW_UNCONFIGURED reset above, and for the
 * HW_CONFIGURED -> RCP_CONFIGURED advance (still HW_CONFIGURED at the
 * time of THAT request, where §12.7.3 explicitly permits the discovery
 * stream) -- no longer suffices once already RCP_CONFIGURED. Only
 * writer.via_root_client_ep0 authorizes this specific reset.
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

/* rcp_lifecycle_should_accept()'s own three-way outcome (REQ-LIFECYCLE-033):
 * a frame is either fully admitted, silently dropped with no response at
 * all, or admitted far enough to answer with an error response but no
 * further processing. TC18 distinguishes these explicitly -- §12.7's own
 * "Other valid requests to EP0 will be rejected with an error response
 * with error code REQUEST_REJECTED" is a different outcome than
 * §12.3.1.1's/§12.3.1.2's "dropped without further response" -- so a plain
 * bool cannot represent this function's full contract. */
typedef enum {
    RCP_LIFECYCLE_ACCEPT = 0, /* admit the frame for normal processing */
    RCP_LIFECYCLE_DROP   = 1, /* silently discard, no response at all */
    RCP_LIFECYCLE_REJECT = 2, /* answer with RCP_ERROR_REQUEST_REJECTED,
                                  process no further */
} rcp_lifecycle_accept_t;

/* The per-state request-filtering rule, as its own directly-tested
 * function (mirroring avtp.c's rcp_avtp_should_drop_tscf() convention):
 *
 *   - Whatever the state, a TSCF-headed frame is first subject to
 *     rcp_avtp_should_drop_tscf()'s own time-sync rule (RCP_LIFECYCLE_DROP
 *     iff that call returns true) -- REQ-AVTP-021/TC18 §13.3: passing
 *     unsupported_time_sync_policy straight through means this function's
 *     own caller controls whether an unsupported-time-sync TSCF frame is
 *     dropped here (RCP_AVTP_TSCF_FALLBACK_DROP, the default/original
 *     behavior) or reaches the state-specific checks below un-dropped
 *     (RCP_AVTP_TSCF_FALLBACK_IGNORE) -- see rcp_avtp_should_drop_tscf()'s
 *     own doc comment (avtp.h) for the full §13.3-vs-§11.1 citation. A
 *     caller taking the IGNORE path is still responsible for actually
 *     ignoring the presentation time once this function returns
 *     RCP_LIFECYCLE_ACCEPT/_REJECT for such a frame -- this function only
 *     ever decides whether to admit, never how an admitted TSCF frame's
 *     own tv/avtp_timestamp are subsequently used.
 *   - While HW_UNCONFIGURED: a TSCF-headed frame is dropped outright
 *     regardless of time_sync_supported (presentation-time semantics
 *     presuppose a configured request stream, which cannot exist yet).
 *     An NTSCF-headed frame not addressed to
 *     RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID is dropped too. Addressed there,
 *     an ACF_ABB (STANDARD) message is accepted; any other message type
 *     (in particular ACF_GBB, this codebase's wire encoding for every
 *     conditional request kind -- compound/compound-wait/triggered/
 *     chained/timed/cancel, see request_compound.h and siblings) is
 *     REQ-LIFECYCLE-033's own REJECT outcome, per TC18 §12.7: "As long as
 *     the RC PHY is either in HW_UNCONFIGURED state or no valid
 *     stream_id/byte_bus_id combinations have been defined only...
 *     unconditional STANDARD requests are allowed... Other valid requests
 *     to EP0 will be rejected with an error response with error code
 *     REQUEST_REJECTED."
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
 *     current scope, and applies before the STANDARD-vs-other check below
 *     (RCP_LIFECYCLE_DROP, not REJECT -- §12.3.1.2's own "ignored and
 *     dropped without response" text for non-EP0 requests, a general rule
 *     TC18 §12.7's more specific EP0-scoped REJECT does not override).
 *     Addressed to EP0 itself, the same ACF_ABB-vs-other split as
 *     HW_UNCONFIGURED applies: ACF_ABB is accepted, anything else
 *     (REQ-LIFECYCLE-033) is REQ_LIFECYCLE_REJECT -- this library's own
 *     "no valid stream_id/byte_bus_id combinations have been defined"
 *     condition from TC18 §12.7's own text is permanently true throughout
 *     HW_CONFIGURED, since no wire-level regmap read/write exists yet to
 *     ever establish one (see REQ-LIFECYCLE-025/034's own architecture
 *     finding for the same underlying gap), so this REJECT rule applies
 *     for the whole of HW_CONFIGURED, not just an initial sub-window.
 *     This reading reconciles what would otherwise be a direct conflict
 *     with §12.3.1.2's own separate "requests in ACF_GBB format[are
 *     dropped]" sentence (REQ-LIFECYCLE-029) by treating that as the
 *     general, non-EP0-scoped rule and §12.7's REJECT as the more
 *     specific, EP0-scoped override -- the same specific-overrides-
 *     general reading TC18 uses throughout (e.g. Table 24's own W*
 *     legend narrowing the general W rule).
 *   - While RCP_CONFIGURED: acceptance beyond the general time-sync rule
 *     already applied above is unrestricted at this milestone -- the
 *     validated mapping HW_CONFIGURED's rules are guarding against now
 *     exists.
 *
 * avtp_subtype is one of RCP_AVTP_SUBTYPE_NTSCF/_TSCF (see avtp.h);
 * acf_msg_type is one of RCP_ACF_MSG_TYPE_ABB/_GBB (see acf.h), or any
 * other value for a message type this filtering rule does not special-case. */
rcp_lifecycle_accept_t rcp_lifecycle_should_accept(rcp_lifecycle_state_t state,
                                                   bool time_sync_supported,
                                                   uint8_t avtp_subtype,
                                                   uint8_t acf_msg_type,
                                                   rcp_byte_bus_id_t byte_bus_id,
                                                   rcp_avtp_tscf_fallback_t
                                                       unsupported_time_sync_policy);

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

/* Which broad category a register field falls into for locking purposes.
 * HW_GENERIC covers HW-pin-mapping (TC18 Figure 16's "HW_CONFIG") and
 * every other block Figure 16 groups under the identical HW_UNCONFIGURED-
 * only locking rule and identical LOCKED_CONFIG_ACCESS response --
 * REQ-LIFECYCLE-023: this includes the endpoint-generic configuration
 * (rcp_regmap_ep_generic_cfg_t, Figure 16's "EP_GEN_CFG") and the
 * response-queue/request-stream configuration (rcp_regmap_response_
 * queue_cfg_t / rcp_regmap_request_stream_cfg_t not already covered by
 * Table 24's own separate W* legend, Figure 16's "QUEUE_CFG") -- these
 * are deliberately NOT modeled as distinct enum values, since their
 * writability rule is identical to HW_GENERIC's own, and this codebase's
 * own convention (see FUNCTIONAL_W vs. FUNCTIONAL_W_STAR immediately
 * below) is to add a new enum value only when behavior actually differs,
 * not to mirror the register map's own struct boundaries one-for-one.
 * FUNCTIONAL_W and FUNCTIONAL_W_STAR both cover functional configuration
 * but differ in what happens once RCP_CONFIGURED is reached -- modeled
 * as two distinct enum values rather than one writability bit, per this
 * milestone's explicit scope.
 *
 * READ_ONLY (REQ-RMAP-025, TC18 §12.7.5 Table 20): the RC Server general
 * (static) register map's own access type, "R" -- unwritable
 * unconditionally, in every lifecycle state, by every writer, unlike
 * every kind above (each of which is writable by SOME writer in SOME
 * state). Genuinely different behavior from the other three kinds
 * (state- and writer-INDEPENDENT denial, not just a narrower
 * state/writer condition), so per this enum's own stated convention it
 * gets its own explicit value rather than folding into an existing one
 * or relying on the switch's own defensive default case. */
typedef enum {
    RCP_LIFECYCLE_FIELD_HW_GENERIC        = 0,
    RCP_LIFECYCLE_FIELD_FUNCTIONAL_W      = 1,
    RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR = 2,
    RCP_LIFECYCLE_FIELD_READ_ONLY         = 3,
} rcp_lifecycle_field_kind_t;

/* True iff a field of the given kind is writable while the server is in
 * state, by the given writer:
 *
 *   - RCP_LIFECYCLE_FIELD_HW_GENERIC: writable only in HW_UNCONFIGURED, and
 *     only when writer indicates the discovery stream (TC18
 *     §12.3.1.1/§12.7.2 -- "All configurations must be run via the stream
 *     which was used for discovery"; REQ-LIFECYCLE-026/035) -- no root
 *     client or owning stream can exist yet this early in bring-up, so
 *     via_discovery_stream is the only authorizing condition available.
 *     Read-only from the moment the server reaches HW_CONFIGURED, for any
 *     writer.
 *   - RCP_LIFECYCLE_FIELD_FUNCTIONAL_W: not writable in HW_UNCONFIGURED
 *     (functional configuration presupposes a hardware mapping); while
 *     HW_CONFIGURED, writable only when writer indicates the root client
 *     via EP0, the endpoint's own owning stream, or the discovery stream
 *     (TC18 §12.3.1.2/§12.7.3 -- REQ-LIFECYCLE-030/036); once
 *     RCP_CONFIGURED, writable only when writer indicates the endpoint's
 *     own stream or the root client via EP0 -- the discovery stream no
 *     longer suffices on its own at this state (TC18 §12.7.4;
 *     REQ-LIFECYCLE-037 closes the matching gap in
 *     rcp_lifecycle_transition()'s own reset-to-HW_UNCONFIGURED path,
 *     see that function's own header doc comment).
 *   - RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR: writable unconditionally
 *     while HW_UNCONFIGURED; the same root-client/owning-stream/
 *     discovery-stream authorization as FUNCTIONAL_W above while
 *     HW_CONFIGURED; permanently locked (unwritable by any writer, not
 *     just an unauthorized one) once RCP_CONFIGURED is reached -- this
 *     last distinction is the one the roadmap requires be modeled
 *     explicitly rather than collapsed into a single writability bit.
 *   - RCP_LIFECYCLE_FIELD_READ_ONLY (REQ-RMAP-025): never writable, in
 *     any state, by any writer -- TC18 §12.7.5 Table 20's own access
 *     type "R" for the RC Server general (static) register map. A
 *     remote write must not take effect regardless of authorization;
 *     rcp_lifecycle_field_write_error() therefore always reports
 *     RCP_ERROR_LOCKED_MEM_ACCESS for this kind (its own re-evaluation
 *     against a maximally-privileged writer still finds it unwritable,
 *     since no writer condition is consulted at all), never
 *     RCP_ERROR_UNAUTHORIZED_ACCESS.
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

/* REQ-RMAP-055: TC18's own W+ (explicitly lockable) access type
 * (§12.7.8 Table 23 -- every EP_ID_config row; §12.7.9 Table 24 --
 * STREAM_UID/flush_on_count/Flush_time) -- distinct from every kind
 * rcp_lifecycle_field_kind_t already models. A W+ field follows the
 * SAME lifecycle-state/writer rule as RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR
 * (writable in HW_UNCONFIGURED; authorized-writer-only in HW_CONFIGURED;
 * permanently locked once RCP_CONFIGURED), PLUS an INDEPENDENT lock the
 * configuring instance may set at any time to protect the table from
 * further modification, "independently of the lifecycle state that
 * governs W and W*" (TC18's own words, §12.7.8, TC18.txt L2977).
 * `locked` is that additional, caller-supplied bit -- true always
 * overrides whatever the state/writer rule below would otherwise
 * permit, in any lifecycle state.
 *
 * Deliberately a SEPARATE function, not a new rcp_lifecycle_field_kind_t
 * enum value threaded through rcp_lifecycle_field_writable()'s own
 * signature: that function has roughly 90 existing call sites (12
 * endpoint types' own writability gates, 2 internal uses, regmap.c,
 * and every test exercising any of them) that would each need a
 * mechanical-but-wide edit for a lock concept that, per REQ-RMAP-055's
 * own text, has no wire-write dispatch path calling it yet anywhere --
 * EP_ID_config and the Table 24 queue registers are both still
 * content-modeling-only, same deferred-ACF_ABB-wrapper gap as every
 * other Group 5 table (REQ-RMAP-052/054/061/065). A standalone
 * function delivers the same TC18-conformant primitive with none of
 * that blast radius, matching this phase's own established additive,
 * zero-risk-to-existing-callers precedent (e.g.
 * rcp_regmap_hw_pin_map_render(), rcp_regmap_wd_timeout_ms_to_ticks())
 * -- it reuses (not re-derives) rcp_lifecycle_field_writable()'s own
 * FUNCTIONAL_W_STAR branch internally. */
bool rcp_lifecycle_field_writable_w_plus(rcp_lifecycle_state_t state,
                                          rcp_lifecycle_writer_ctx_t writer,
                                          bool locked);

/* The REQ-WIREERR-004-style error classification for a W+ field,
 * matching rcp_lifecycle_field_write_error()'s own two-code split, with
 * a third input (`locked`) folded in: RCP_ERROR_LOCKED_MEM_ACCESS when
 * `locked` is true (this field's own explicit lock always wins,
 * unconditionally, the same way RCP_LIFECYCLE_FIELD_READ_ONLY's state-
 * and writer-independent denial does) OR the underlying
 * FUNCTIONAL_W_STAR state rule would deny even a maximally-privileged
 * writer; RCP_ERROR_UNAUTHORIZED_ACCESS when the underlying state
 * would otherwise permit the write but writer specifically does not;
 * RCP_ERROR_NONE when writable. */
rcp_wire_error_t rcp_lifecycle_field_write_error_w_plus(rcp_lifecycle_state_t state,
                                                          rcp_lifecycle_writer_ctx_t writer,
                                                          bool locked);

#ifdef __cplusplus
}
#endif

#endif /* RCP_LIFECYCLE_H */
