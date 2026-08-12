/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-WAKEUP-001
//cfusa:req REQ-WAKEUP-002
//cfusa:req REQ-WAKEUP-003
//cfusa:req REQ-WAKEUP-004
//cfusa:req REQ-WAKEUP-005
//cfusa:req REQ-WAKEUP-006
//cfusa:req REQ-WAKEUP-007
//cfusa:req REQ-WAKEUP-008
//cfusa:req REQ-WAKEUP-009
//cfusa:req REQ-WAKEUP-010
//cfusa:req REQ-WAKEUP-011
//cfusa:req REQ-WAKEUP-012
//cfusa:req REQ-WAKEUP-013
//cfusa:req REQ-WAKEUP-014
//cfusa:req REQ-WAKEUP-015
//cfusa:req REQ-WAKEUP-016

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-WAKEUP-017
//cfusa:req REQ-WAKEUP-018
//cfusa:req REQ-WAKEUP-019
//cfusa:req REQ-WAKEUP-020
//cfusa:req REQ-WAKEUP-021
//cfusa:req REQ-WAKEUP-022
/*
 * ep_wakeup.h -- the dedicated power-management endpoint (ep_type=0x01)
 * for the TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 19,
 * "Remaining Endpoint Types", milestone 75).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (lifecycle.h/
 * lifecycle.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and this same milestone's own power-mode state machine
 * (power.h/power.c). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c,
 * lifecycle.h/lifecycle.c, server.h/server.c, regmap.h/regmap.c,
 * discovery.h/discovery.c, or any prior endpoint file (ep_gpio.h/.c,
 * ep_spi.h/.c, ep_i2c.h/.c, ep_uart.h/.c, ep_pwm.h/.c, ep_adc.h/.c,
 * ep_lin.h/.c, ep_can.h/.c, ep_iseled.h/.c, ep_mdio.h/.c) is touched here --
 * the same layering discipline every endpoint type since milestone 64 has
 * established, followed structurally throughout by this module too.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced,
 * with one exception: the SleepCMD marker byte 0xA5 is this codebase's own
 * already-established roadmap-level design fixture (ROADMAP.md's own
 * milestone-75 scope text), reused verbatim here as this module's own
 * wire-level opcode, not freshly derived from spec content by this
 * module.
 *
 * ── Requirement-id naming note ──────────────────────────────────────────────
 *
 * Verified directly (`grep`) against `.fusa-reqs.json` before picking this
 * module's own prefix, the same check every prior endpoint milestone has
 * made: this codebase has never carried a pre-replacement wakeup/wake-
 * source module of any kind, and no `REQ-WAKEUP-*` id existed before this
 * milestone -- no collision-avoidance suffix needed (contrast power.h's
 * own file header, which *does* need one, against the pre-existing
 * `REQ-PWR-*` group).
 *
 * ── ep_type=0x01 ─────────────────────────────────────────────────────────────
 *
 * regmap.h's rcp_regmap_ep_generic_cfg_t::ep_type field takes its concrete
 * meaning from each endpoint type added in Phase 16/19 (regmap.h's own
 * words); this endpoint type's own assigned value is 0x01, per the
 * roadmap's own milestone-75 scope. This module does not itself validate
 * or enforce that value against a live register map (no endpoint type in
 * this codebase does that from within its own request/response codec,
 * matching ep_mdio.h's own precedent) -- RCP_EP_WAKEUP_EP_TYPE is provided
 * purely as a named, documented constant for a caller's own regmap.h
 * population code to use.
 *
 * ── SleepCMD: a fixed request kind, not the general taxonomy ────────────────
 *
 * request_compound.h/request_triggered.h/request_chained.h/
 * request_timed.h/request_cancel.h (milestones 68-69) share one general
 * request_type taxonomy, byte-packed into ACF_GBB's message_timestamp
 * region when untimed (see request_compound.h's own file header). SleepCMD
 * is deliberately *not* a member of that taxonomy -- the roadmap names it
 * as a request kind fixed and distinct from it. This module therefore
 * gives SleepCMD its own dedicated wire encoding entirely outside that
 * shared byte-repurposing convention: an ordinary ACF_ABB message (op =
 * RCP_ACF_OP_NONE) whose payload is exactly the fixed
 * `RCP_EP_WAKEUP_SLEEPCMD_OPCODE` (0xA5) marker byte, then padding to the
 * next quadlet boundary -- TC18 §13.7.2.3 Figure 22 ("Endpoint sleep
 * request") shows a single-byte SleepCMD followed by a plain, generic
 * "padding" region with no further structure, directly confirmed against
 * the rendered PDF page image (not just its text extraction). CORRECTED
 * 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group E): this codec previously
 * encoded/required a second payload byte selecting a target rcp_pwrmode_t
 * (STANDBY vs SLEEP) that Figure 22 does not define at all -- a real
 * interop-breaking bug, since a genuinely conformant peer's own
 * Figure-22-shaped SleepCMD request (opcode + zero-padding) would decode
 * as RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE against the old, invented field.
 * SleepCMD's own request/response text (§13.7.2.3) never once mentions
 * Standby -- only "bring the RC Server implementation to sleep mode",
 * twice -- so this wire message is now modeled as meaning Sleep
 * unconditionally; TC18's separate §12.5 ("Goto Sleep / Goto Standby
 * behavior") does describe a general RC-Client-initiated Standby-entry
 * mechanism, but no text found in this codebase's own copy of the
 * specification defines that mechanism's own wire encoding (a genuine,
 * separately-tracked gap -- see rcp_powerstate_manager_encode_entry_request()
 * in powerstate.h, REQ-PWR-001, which now honestly fails for a
 * RCP_PWRMODE_STANDBY target rather than routing it through this
 * sleep-only wire message as before).
 *
 * A SleepCMD response carries the same marker byte followed by one byte
 * encoding the power.h rcp_pwrmode_entry_result_t outcome of
 * rcp_pwrmode_check_entry() -- RCP_PWRMODE_ENTRY_OK or
 * RCP_PWRMODE_ENTRY_REFUSED (the roadmap's REQUEST_CANCELED) -- letting a
 * caller apply that gate (power.h) and report its outcome back over this
 * same dedicated request/response pair, entirely independent of
 * request_cancel.h's own, differently-shaped cancellation-result
 * reporting. Unlike the request, TC18 defines NO wire format at all for
 * this acknowledge -- §13.7.2.3 says only "send an acknowledge to sleep
 * request" in prose, with no figure or table of its own -- so this
 * response byte layout remains this module's own original design choice,
 * not a TC18-derived one, same as before this correction.
 *
 * ── WakeUp-message emission replaces the generic trigger-signal table ───────
 *
 * Every other endpoint type built so far models an asynchronous signal
 * either as a per-pin/per-channel trigger table (ep_gpio.h) or a single
 * fixed trigger (ep_lin.h, ep_iseled.h), or documents having none at all
 * (ep_can.h, ep_mdio.h). This endpoint type has neither: per the roadmap's
 * own phrasing, WakeUp-message emission *replaces* the generic trigger-
 * signal mechanism for this endpoint specifically, so there is no
 * `rcp_ep_wakeup_trigger_t` anywhere in this file, and
 * rcp_ep_wakeup_functional_cfg_t carries no trigger field. Instead,
 * rcp_ep_wakeup_encode_wakeup_message() is this endpoint's own dedicated,
 * always-available emission path -- a fixed-opcode
 * (`RCP_EP_WAKEUP_WAKEUP_OPCODE`, this module's own original marker byte
 * value, distinct from SleepCMD's own 0xA5 and from the general
 * request_type taxonomy) ACF_ABB message a server emits on a wake-source
 * assertion, and which power.h's hot-start-from-Sleep handshake (step
 * (b)) expects to see echoed back -- rcp_ep_wakeup_is_wakeup_echo() is the
 * small, pure-over-its-inputs helper a caller feeds power.h's
 * rcp_pwrmode_handshake_wakeup_attempt()'s own `echoed` argument from.
 *
 * ── Wake-source pin configuration/monitoring ─────────────────────────────────
 *
 * rcp_ep_wakeup_functional_cfg_t composes regmap.h's shared functional-
 * config prefix (like every endpoint type) and adds its own
 * `sources[RCP_EP_WAKEUP_MAX_SOURCES]` table -- each entry an `enabled`
 * flag plus an `active_high` polarity bit, this module's own minimal
 * per-wake-source configuration shape (deliberately not reusing
 * regmap.h's own GPIO-signal-index enumeration, since a wake-source pin
 * is this endpoint's own concept, addressed by table index, not by the
 * shared named-signal index every other endpoint type's pin references
 * use). `RCP_EP_WAKEUP_MAX_SOURCES` (8) is this module's own chosen
 * upper bound, not a spec-derived number.
 * rcp_ep_wakeup_source_asserted()/_any_source_asserted() are small, pure,
 * directly-testable statements of "does this source's configured polarity
 * match its current raw pin level" and "does any configured source
 * currently indicate a wake condition", given raw pin levels a caller
 * samples elsewhere (this module owns no pin-sampling of its own, mirror-
 * ing every prior endpoint type's "structural fields only" scope).
 *
 * rcp_ep_wakeup_wup_status_t is this module's own minimal latch modeling
 * the roadmap's `wup_status` register: `_latch()` sets it (a caller drives
 * this on a wake-source assertion edge, via
 * rcp_ep_wakeup_any_source_asserted() detecting a 0->1 transition itself),
 * and `_clear()` clears it (a caller drives this from an explicit client
 * register write). power.h's rcp_pwrmode_check_entry() gate consumes
 * `rcp_ep_wakeup_wup_status_is_clear()`'s result as its own
 * `wup_status_clear` field -- this module does not include power.h itself
 * for that wiring, keeping the dependency one-directional (ep_wakeup.h
 * depends on power.h for rcp_pwrmode_t/rcp_pwrmode_entry_result_t in the
 * SleepCMD codec above; power.h does not depend back on ep_wakeup.h).
 *
 * ── The EP_func register block (evt[2:0] == 111b), added 2026-08-11 ────────
 *
 * FIXED/ADDED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I dedicated
 * investigation, task #95): this endpoint type had no §12.7.1 register-
 * block wire mapping at all until now -- deferred initially because its
 * own TC18 §13.7.2.2 table (unlike every other endpoint's own fixed-width
 * register block) has a genuine, literal address collision between
 * `wup_status` and the wake-source array's own first entry, both printed
 * at the same relative address. Confirmed via the RENDERED page image on
 * both the 0.5.1_RC baseline and the 0.5.1_RC5 revision (identical on
 * both -- not an extraction artifact, and not independently corrected by
 * the spec committee the way the MDIO Table 56/59 collision was) that
 * `wup_status` (a fixed, single 16-bit register) is meant to occupy its
 * own slot before the variable-length wake-source array begins,
 * resolved via this session's own established cross-table pattern:
 * `wup_status` at its own printed address, the array shifted to start
 * immediately after it, one slot per RCP_EP_WAKEUP_MAX_SOURCES (this
 * module's own pre-existing upper bound, matching the wire's own
 * `wup_nr_io_pins_max` register exactly).
 *
 * Two further gaps, beyond the address collision itself, are handled
 * conservatively rather than with an from-scratch redesign:
 *   1. `wup_ep_status` (a 16-bit R/W register TC18 documents only as
 *      "Status of WUP-endpoint", no further structure given) is added as
 *      a new opaque `ep_status` field -- same treatment every other
 *      endpoint type's own `ep_status`/`svr_ep_status` field gets.
 *   2. Each wake-source's own wire register encodes a 5-bit IO_SRC
 *      behavior code (TC18's own Table 37/40: inactive, rising edge,
 *      falling edge, both edges, high level, low level, else reserved) --
 *      this module's own pre-existing `rcp_ep_wakeup_source_cfg_t` only
 *      ever modeled 2 of those 6 states (`active_high`, a level-only
 *      predicate `rcp_ep_wakeup_source_asserted()` already implements and
 *      is already tested). Rather than redesigning that detection logic
 *      to add edge-triggering (which would need previous-level state
 *      this module's own existing, tested API doesn't carry, and would
 *      ripple into every caller's own calling convention), the register
 *      block below renders/parses exactly the 2 states this module can
 *      already represent (io_src 0x04 = high level, 0x05 = low level,
 *      0x00 = disabled) faithfully, and a configuration write encoding
 *      an edge-triggered or reserved io_src value (0x01-0x03, 0x06-0x1F)
 *      leaves that source's own `enabled`/`active_high` fields UNCHANGED
 *      (its `pin_number` still updates) -- honestly representing "this
 *      implementation cannot act on that request" rather than silently
 *      misinterpreting an edge-triggered request as a level-triggered
 *      one, which would be actively wrong instead of merely incomplete.
 *      `rcp_ep_wakeup_source_asserted()`'s own existing signature,
 *      semantics, and tests are entirely unchanged by this addition.
 *
 * `wup_status` itself keeps its own pre-existing, already-tested
 * single-aggregate-latch-bit API and shape entirely unchanged too (see
 * above) -- TC18's own register is a 16-bit bitmask, "each bit represents
 * a wake-up source", but this module has never modeled a per-source
 * latch (only "has ANY source woken the device"). The register block
 * renders/parses only bit 0 of the wire's wup_status word (1 = latched,
 * matching this module's own existing meaning); bits [15:1] always read
 * 0 and a write's bit 0 clears the latch (matching the spec's own
 * "writing '1' clears the flag" rule) while any other written bit
 * pattern is a no-op -- an honestly disclosed simplification, not a
 * silently wrong one, exactly like the IO_SRC treatment above.
 */
#ifndef RCP_EP_WAKEUP_H
#define RCP_EP_WAKEUP_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/lifecycle.h"
#include "rcp/power.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This endpoint type's assigned regmap.h ep_type value -- see the file header. */
#define RCP_EP_WAKEUP_EP_TYPE ((uint8_t)0x01u)

/* REQ-WAKEUP-020: TC18 §13.7.2.1 ("The WakeUp endpoint is a special
 * endpoint and as this fixed to the endpoint nr 1, as it is the only
 * endpoint which stays active in Sleep mode") -- the fixed EP_Nr/ep_id
 * a WakeUp-type row in EP_ID_config (regmap.h's own
 * rcp_regmap_ep_id_map_entry_t::ep_id, TC18 §12.7.8 Table 23) must
 * carry, distinct from RCP_EP_WAKEUP_EP_TYPE above (this codebase's own
 * internal ep_type tag, a different field on a different table --
 * their numeric value both being 1 is this codebase's own coincidence,
 * not a TC18 identity). byte_bus_id itself (the wire routing address
 * every rcp_ep_wakeup_* entry point below still takes as a plain
 * caller-supplied parameter, matching every other endpoint type in
 * this codebase) is unaffected -- TC18 §13.7.2.2 states byte_bus_id is
 * "also defined via the EP_ID_map" the same way as any other endpoint,
 * so it is not, and must not be, additionally pinned by this constant.
 * See rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id() (regmap.h) for the
 * dedicated diagnostic checking a whole EP_ID_config table against
 * this invariant -- a read-only recommendation check, not enforcement,
 * matching every other TC18 §12.7.8 recommendation this codebase
 * already models this way (REQ-RMAP-057/058). */
#define RCP_EP_WAKEUP_ENDPOINT_NUM ((uint16_t)1u)

/* ── Wake-source pin configuration/monitoring ────────────────────────────────── */

#define RCP_EP_WAKEUP_MAX_SOURCES ((size_t)8u)

/* Moved above rcp_ep_wakeup_functional_cfg_t (2026-08-11) so that struct
 * can embed one as a field -- see the file header's own register-block
 * note. Function declarations for this type stay in their own "wup_status
 * latch" section below, unmoved. */
typedef struct {
    bool latched;
} rcp_ep_wakeup_wup_status_t;

typedef struct {
    bool     enabled;     /* this wake-source slot participates in wake detection */
    bool     active_high; /* true: a high pin level asserts wake; false: a low level does */
    uint16_t pin_number;  /* ADDED 2026-08-11: wup_io_scrN's own [10:0] wire
                              field -- the physical IO pin this slot
                              observes. 0 is the wire's own "unconfigured/
                              end of table" value for a slot; see the file
                              header's own register-block note. Does not
                              itself affect rcp_ep_wakeup_source_asserted()
                              (which only ever consults enabled/
                              active_high), it is purely the wire-visible
                              identity of which physical pin enabled/
                              active_high describe. */
} rcp_ep_wakeup_source_cfg_t;

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix -- see the file
                                               header */
    rcp_ep_wakeup_source_cfg_t     sources[RCP_EP_WAKEUP_MAX_SOURCES];
    uint16_t                       ep_status;  /* ADDED 2026-08-11: wup_ep_status
                                                    -- see the file header's own
                                                    register-block note */
    rcp_ep_wakeup_wup_status_t     wup_status; /* ADDED 2026-08-11: embeds the
                                                    pre-existing, unchanged
                                                    rcp_ep_wakeup_wup_status_t
                                                    latch so the whole register
                                                    block lives in one struct,
                                                    matching every other
                                                    endpoint type's own
                                                    convention -- see the file
                                                    header's own register-block
                                                    note. The standalone type
                                                    remains usable on its own
                                                    exactly as before; this is
                                                    an additional way to reach
                                                    it, not a replacement. */
} rcp_ep_wakeup_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; every source entry
 * disabled with active_high == false and pin_number == 0; ep_status == 0;
 * wup_status cleared). */
void rcp_ep_wakeup_functional_cfg_init(rcp_ep_wakeup_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (lifecycle.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see every
 * prior endpoint type's own identical wrapper. */
bool rcp_ep_wakeup_functional_cfg_writable(rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer);

/* True iff cfg is enabled and pin_level matches cfg's own active_high
 * polarity -- i.e. this one source currently indicates a wake condition. */
bool rcp_ep_wakeup_source_asserted(rcp_ep_wakeup_source_cfg_t cfg, bool pin_level);

/* True iff any of the first (RCP_EP_WAKEUP_MAX_SOURCES min pin_level_count)
 * entries of fcfg->sources is currently asserted per
 * rcp_ep_wakeup_source_asserted(), given pin_level_count raw pin levels in
 * pin_levels (pin_levels[i] corresponds to fcfg->sources[i]). fcfg == NULL
 * or pin_levels == NULL (with pin_level_count > 0) returns false. */
bool rcp_ep_wakeup_any_source_asserted(const rcp_ep_wakeup_functional_cfg_t *fcfg,
                                        const bool *pin_levels, size_t pin_level_count);

/* ── wup_status latch ─────────────────────────────────────────────────────────── */

/* Initializes *s to cleared (latched = false). */
void rcp_ep_wakeup_wup_status_init(rcp_ep_wakeup_wup_status_t *s);

/* Sets *s to latched -- a caller drives this on a wake-source assertion
 * edge (see the file header). */
void rcp_ep_wakeup_wup_status_latch(rcp_ep_wakeup_wup_status_t *s);

/* Clears *s -- a caller drives this from an explicit client register
 * write. */
void rcp_ep_wakeup_wup_status_clear(rcp_ep_wakeup_wup_status_t *s);

/* True iff *s is not latched. */
bool rcp_ep_wakeup_wup_status_is_clear(const rcp_ep_wakeup_wup_status_t *s);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_WAKEUP_OK                  = 0,
    RCP_EP_WAKEUP_ERR_SHORT_FRAME     = 1,
    RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_WAKEUP_ERR_WRONG_BUS       = 3,
    RCP_EP_WAKEUP_ERR_BAD_OPCODE      = 4,
    /* RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE = 5 retired 2026-08-10
     * (c-RCP-AUDIT-06, issue #256 Group E): the target-mode byte it
     * guarded never existed on the wire per TC18 Figure 22 -- see the
     * file header. Value 5 is not reused, to avoid silently changing the
     * meaning of any already-serialized error code a caller might have
     * logged or compared against. */
} rcp_ep_wakeup_errc_t;

/* Human-readable message for an rcp_ep_wakeup_errc_t value. Never returns NULL. */
const char *rcp_ep_wakeup_strerror(rcp_ep_wakeup_errc_t e);

/* ── SleepCMD request/response (0xA5) ────────────────────────────────────────── */

/* This endpoint type's fixed SleepCMD marker byte -- see the file header;
 * value taken directly from this codebase's own pre-existing
 * ROADMAP.md milestone-75 scope, not freshly derived here. */
#define RCP_EP_WAKEUP_SLEEPCMD_OPCODE ((uint8_t)0xA5u)

/* True iff writer is authorized to request StandBy/Sleep entry via a
 * SleepCMD -- REQ-PWRMODE-023 (TC18 §12.5): "The RC Client that is
 * allowed to access the RC Server endpoint can request the entire RC
 * Server implementation to enter standby or sleep mode." This codebase's
 * only concept of "an RC Client allowed to access [an] endpoint" is
 * lifecycle.h's own root-client/discovery-stream writer classification
 * (rcp_lifecycle_writer_ctx_t) -- true iff writer.via_root_client_ep0,
 * mirroring rcp_lifecycle_transition()'s own RCP_CONFIGURED-state
 * authorization rule (REQ-LIFECYCLE-037) for the same reason: once
 * RCP_CONFIGURED (the only state a SleepCMD is meaningful in -- entering
 * a low-power mode presupposes a configured server), only the root
 * client, not an unqualified discovery-stream sender, may act on
 * server-wide power state. A caller checks this BEFORE
 * power.h's rcp_pwrmode_check_entry()/rcp_pwrmode_commit_entry() --
 * an unauthorized writer's request never reaches that gate at all. */
bool rcp_ep_wakeup_sleepcmd_writable(rcp_lifecycle_writer_ctx_t writer);

/* Encodes an ACF_ABB SleepCMD request addressed to byte_bus_id: a 1-byte
 * payload of RCP_EP_WAKEUP_SLEEPCMD_OPCODE, padded to the next quadlet
 * boundary by rcp_acf_encode_abb() itself -- see the file header's
 * wire-layout discussion (TC18 §13.7.2.3 Figure 22). Unconditionally
 * requests Sleep entry; there is no target-mode selector on this wire
 * message (corrected 2026-08-10, c-RCP-AUDIT-06, issue #256 Group E --
 * see the file header). Returns a zeroed rcp_bytes_t (data=NULL) only on
 * allocation failure. */
rcp_bytes_t rcp_ep_wakeup_encode_sleepcmd_request(rcp_byte_bus_id_t byte_bus_id,
                                                   uint8_t transaction_num);

/* Decodes and validates an ACF-level SleepCMD request from b[0..len).
 * Fails with RCP_EP_WAKEUP_ERR_SHORT_FRAME if b is shorter than the
 * ACF_ABB fixed header or its declared 1-byte payload;
 * RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_WAKEUP_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_WAKEUP_ERR_BAD_OPCODE if the payload's first byte is not
 * RCP_EP_WAKEUP_SLEEPCMD_OPCODE. On RCP_EP_WAKEUP_OK, *out_transaction_num
 * is populated. Corrected 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group
 * E): no target-mode byte is validated or returned anymore -- see the
 * file header. */
rcp_ep_wakeup_errc_t rcp_ep_wakeup_decode_sleepcmd_request(const uint8_t *b, size_t len,
                                                            rcp_byte_bus_id_t expected_bus_id,
                                                            uint8_t *out_transaction_num);

/* Encodes an ACF_ABB SleepCMD response for result (power.h's
 * rcp_pwrmode_entry_result_t). RCP_PWRMODE_ENTRY_OK encodes this
 * module's own positive-form payload (RCP_EP_WAKEUP_SLEEPCMD_OPCODE
 * followed by the result byte), echoing transaction_num, exactly as
 * before. RCP_PWRMODE_ENTRY_REFUSED instead returns a genuine ACF
 * Error Response carrying RCP_ERROR_REQUEST_CANCELED (via
 * rcp_acf_build_error_response()) -- REQ-WAKEUP-019, TC18 §12.5: "The
 * RC Server will reject requests to enter sleep or standby mode and
 * send an error message with error code = REQUEST_CANCELED." A
 * conformant RC Client watching for an error response (not a
 * this-module-specific positive-form byte it has no reason to expect)
 * now sees the refusal. Returns a zeroed rcp_bytes_t (data=NULL) on
 * allocation failure either way. */
rcp_bytes_t rcp_ep_wakeup_encode_sleepcmd_response(rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_pwrmode_entry_result_t result,
                                                    uint8_t transaction_num);

/* Decodes and validates an ACF-level SleepCMD response from b[0..len).
 * REQ-WAKEUP-019: an Error Response (hdr.err set) is now recognized as
 * the refused-entry half of this pair -- *out_result is set to
 * RCP_PWRMODE_ENTRY_REFUSED iff its payload carries
 * RCP_ERROR_REQUEST_CANCELED (the only code this function's own encode
 * counterpart ever builds); any other err payload is
 * RCP_EP_WAKEUP_ERR_BAD_OPCODE, not silently reinterpreted. A non-error
 * response is decoded exactly as before -- the same fixed-opcode/
 * short-frame/wrong-bus/wrong-message-type failure modes as
 * rcp_ep_wakeup_decode_sleepcmd_request(), and any second payload byte
 * other than RCP_PWRMODE_ENTRY_OK/_REFUSED's own raw values decodes as
 * RCP_PWRMODE_ENTRY_REFUSED (fail-safe: an unrecognized result byte is
 * never treated as an admitted entry) -- kept for tolerance of a
 * non-conformant peer's own old-style positive-form refusal, though
 * this module's own encode side no longer produces one. On
 * RCP_EP_WAKEUP_OK, *out_result and *out_transaction_num are
 * populated. */
rcp_ep_wakeup_errc_t rcp_ep_wakeup_decode_sleepcmd_response(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t expected_bus_id,
                                                             rcp_pwrmode_entry_result_t *out_result,
                                                             uint8_t *out_transaction_num);

/* ── WakeUp-message emission ─────────────────────────────────────────────────── */

/* This module's own fixed WakeUp-message marker byte -- see the file
 * header; this module's own original design choice, not spec-derived. */
#define RCP_EP_WAKEUP_WAKEUP_OPCODE ((uint8_t)0x5Au)

/* Encodes an ACF_ABB WakeUp message addressed to byte_bus_id: a 1-byte
 * payload of RCP_EP_WAKEUP_WAKEUP_OPCODE. Returns a zeroed rcp_bytes_t
 * (data=NULL) on allocation failure. */
rcp_bytes_t rcp_ep_wakeup_encode_wakeup_message(rcp_byte_bus_id_t byte_bus_id,
                                                 uint8_t transaction_num);

/* Decodes and validates an ACF-level WakeUp message from b[0..len), with
 * the same short-frame/wrong-bus/wrong-message-type/bad-opcode failure
 * modes as rcp_ep_wakeup_decode_sleepcmd_request() above (checked against
 * RCP_EP_WAKEUP_WAKEUP_OPCODE rather than SleepCMD's own opcode). On
 * RCP_EP_WAKEUP_OK, *out_transaction_num is populated. */
rcp_ep_wakeup_errc_t rcp_ep_wakeup_decode_wakeup_message(const uint8_t *b, size_t len,
                                                          rcp_byte_bus_id_t expected_bus_id,
                                                          uint8_t *out_transaction_num);

/* True iff b[0..len) decodes (rcp_ep_wakeup_decode_wakeup_message()) as a
 * valid WakeUp message addressed to expected_bus_id whose transaction
 * number equals sent_transaction_num -- this module's own small,
 * directly-testable "is this the echo of the WakeUp message I sent"
 * predicate, meant to feed power.h's
 * rcp_pwrmode_handshake_wakeup_attempt()'s own `echoed` argument during
 * the hot-start-from-Sleep handshake's step (b). False for any decode
 * failure or transaction-number mismatch. */
bool rcp_ep_wakeup_is_wakeup_echo(const uint8_t *b, size_t len, rcp_byte_bus_id_t expected_bus_id,
                                   uint8_t sent_transaction_num);

/* ── The EP_func register block (the evt[2:0] == 111b target) ──────────────── */

/* Relative octet offsets of this endpoint's own EP_func block -- see the
 * file header's own "register block" note. Every multi-octet register is
 * big-endian, like every other multi-octet field this codebase encodes.
 * Offsets marked R are read-only: a configuration write covering them
 * leaves them unchanged (see rcp_ep_wakeup_apply_reconfig()). */
#define RCP_EP_WAKEUP_REG_EP_LEN         ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_WAKEUP_REG_NR_IO_PINS_MAX ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_WAKEUP_REG_EP_STATUS      ((uint16_t)0x0002u) /* 16 bit, R/W */
#define RCP_EP_WAKEUP_REG_WUP_STATUS     ((uint16_t)0x0004u) /* 16 bit, R/W */

/* Wake-source slot i's own 2-octet wup_io_scrN register lives at
 * RCP_EP_WAKEUP_REG_SOURCE_BASE + i * RCP_EP_WAKEUP_REG_SOURCE_SPAN,
 * i in [0, RCP_EP_WAKEUP_MAX_SOURCES). Encodes IO_SRC[15:11] (5 bit) in
 * the high bits, pin_number[10:0] (11 bit) in the low bits -- see the
 * RCP_EP_WAKEUP_IO_SRC_* values below. */
#define RCP_EP_WAKEUP_REG_SOURCE_BASE ((uint16_t)0x0006u)
#define RCP_EP_WAKEUP_REG_SOURCE_SPAN ((uint16_t)0x0002u)

/* The 3 IO_SRC[15:11] values this module can represent -- see the file
 * header's own register-block note for why the other TC18-defined values
 * (rising/falling/both-edges, 0x01-0x03) and the reserved range
 * (0x06-0x1F) are not representable here. */
#define RCP_EP_WAKEUP_IO_SRC_INACTIVE   ((uint8_t)0x00u)
#define RCP_EP_WAKEUP_IO_SRC_HIGH_LEVEL ((uint8_t)0x04u)
#define RCP_EP_WAKEUP_IO_SRC_LOW_LEVEL  ((uint8_t)0x05u)

/* The block's own length in octets -- one past the last assigned offset,
 * i.e. the value this endpoint reports at RCP_EP_WAKEUP_REG_EP_LEN and
 * the bound the "write beyond EP_LEN is ignored" rule (§12.7.1) is
 * applied against: the 6-octet common prefix (len/nr_pins_max/ep_status/
 * wup_status) plus RCP_EP_WAKEUP_MAX_SOURCES 2-octet source registers. */
#define RCP_EP_WAKEUP_EP_FUNC_LEN \
    ((uint16_t)(RCP_EP_WAKEUP_REG_SOURCE_BASE + \
                (uint16_t)RCP_EP_WAKEUP_MAX_SOURCES * RCP_EP_WAKEUP_REG_SOURCE_SPAN))

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- the address is a 16-bit
 * big-endian field, followed by the configuration data octets to write
 * from that address onward (§12.7.1). */
#define RCP_EP_WAKEUP_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_WAKEUP_RECONFIG_OK               = 0,
    RCP_EP_WAKEUP_RECONFIG_ERR_SHORT        = 1, /* payload carries no address
                                                     prefix, or an address
                                                     prefix with no data octet
                                                     after it */
    RCP_EP_WAKEUP_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data length
                                                     exceeds
                                                     RCP_EP_WAKEUP_EP_FUNC_LEN --
                                                     the whole write is ignored,
                                                     per the specification's own
                                                     rule */
} rcp_ep_wakeup_reconfig_errc_t;

/* Human-readable message for an rcp_ep_wakeup_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_wakeup_reconfig_strerror(rcp_ep_wakeup_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_WAKEUP_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_wakeup_apply_reconfig()'s own parse step, and
 * the same rendering that function patches in place. wup_status renders
 * only bit 0 (bits [15:1] always 0); each source slot renders exactly one
 * of RCP_EP_WAKEUP_IO_SRC_INACTIVE/_HIGH_LEVEL/_LOW_LEVEL, derived from
 * enabled/active_high -- see the file header's own register-block note
 * for both simplifications. */
void rcp_ep_wakeup_render_registers(const rcp_ep_wakeup_functional_cfg_t *cfg,
                                     uint8_t out[RCP_EP_WAKEUP_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is a
 * 16-bit big-endian relative start address followed by the configuration
 * data octets to write from that address onward (§12.7.1). This is a real
 * register write, reaching every R/W register the block defines (ep_status,
 * wup_status, and every source slot's own wup_io_scrN).
 *
 * Returns RCP_EP_WAKEUP_RECONFIG_ERR_SHORT when payload_len is not at
 * least RCP_EP_WAKEUP_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_WAKEUP_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would
 * extend past RCP_EP_WAKEUP_EP_FUNC_LEN; in both cases cfg is left
 * entirely unchanged, per the specification's own "such a payload is to
 * be ignored" rule. Octets landing on a read-only register (EP_LEN or
 * NR_IO_PINS_MAX) are left at their current values while the rest of the
 * span is still applied. A write to wup_status sets bit 0 clear iff the
 * written value's own bit 0 is set (write-1-to-clear, matching §13.7.2.2's
 * own rule); any other written bit pattern in that register is a no-op.
 * A write to a source slot encoding an IO_SRC value this module cannot
 * represent (see RCP_EP_WAKEUP_IO_SRC_* above) leaves that slot's own
 * enabled/active_high unchanged while still updating its pin_number --
 * see the file header's own register-block note for why. Partially-
 * covered multi-octet registers are handled correctly: the write is
 * applied at octet granularity over the block's rendered image. */
rcp_ep_wakeup_reconfig_errc_t rcp_ep_wakeup_apply_reconfig(rcp_ep_wakeup_functional_cfg_t *cfg,
                                                            const uint8_t *payload,
                                                            size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_wakeup_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                   uint16_t start_address, const uint8_t *data,
                                                   size_t data_len, uint8_t transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_WAKEUP_H */
