/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-PWM-001
//cfusa:req REQ-PWM-002
//cfusa:req REQ-PWM-003
//cfusa:req REQ-PWM-004
//cfusa:req REQ-PWM-005
//cfusa:req REQ-PWM-006
//cfusa:req REQ-PWM-007
//cfusa:req REQ-PWM-008
//cfusa:req REQ-PWM-009
//cfusa:req REQ-PWM-010
//cfusa:req REQ-PWM-011
//cfusa:req REQ-PWM-012
//cfusa:req REQ-PWM-013
//cfusa:req REQ-PWM-014
//cfusa:req REQ-PWM-015
//cfusa:req REQ-PWM-016
//cfusa:req REQ-PWM-017
//cfusa:req REQ-PWM-018
//cfusa:req REQ-PWM-019
//cfusa:req REQ-PWM-020
//cfusa:req REQ-PWM-021
//cfusa:req REQ-PWM-022
//cfusa:req REQ-PWM-023
//cfusa:req REQ-PWM-024
//cfusa:req REQ-PWM-025
//cfusa:req REQ-PWM-026
//cfusa:req REQ-PWM-027
//cfusa:req REQ-PWM-028
//cfusa:req REQ-PWM-029
//cfusa:req REQ-PWM-030
//cfusa:req REQ-PWM-031
//cfusa:req REQ-PWM-032
//cfusa:req REQ-PWM-033
//cfusa:req REQ-PWM-034
//cfusa:req REQ-PWM-035
//cfusa:req REQ-PWM-036
//cfusa:req REQ-PWM-037
//cfusa:req REQ-PWM-038
//cfusa:req REQ-PWM-039
//cfusa:req REQ-PWM-040
//cfusa:req REQ-PWM-041
//cfusa:req REQ-PWM-042
//cfusa:req REQ-PWM-043
//cfusa:req REQ-PWM-044
//cfusa:req REQ-PWM-045
//cfusa:req REQ-PWM-046
//cfusa:req REQ-PWM-047
//cfusa:req REQ-PWM-048
//cfusa:req REQ-PWM-049
//cfusa:req REQ-PWM-050
//cfusa:req REQ-PWM-051
//cfusa:req REQ-PWM-052
//cfusa:req REQ-PWM-053
//cfusa:req REQ-PWM-054

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-PWM-055
//cfusa:req REQ-PWM-056
//cfusa:req REQ-PWM-057
//cfusa:req REQ-PWM-058
//cfusa:req REQ-PWM-059
/*
 * ep_pwm.h -- PWM_OUT + PWM_IN endpoints for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 16, "Basic Endpoints", milestone
 * 67).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any prior endpoint file
 * (ep_gpio.h/.c, ep_spi.h/.c, ep_i2c.h/.c, ep_uart.h/.c) is touched here --
 * the same layering discipline those modules established, followed
 * structurally throughout by this module too.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── Framing level ────────────────────────────────────────────────────────────
 *
 * As with every prior endpoint type, PWM_OUT/PWM_IN request/response
 * traffic is ordinary endpoint traffic: whether it rides an NTSCF or TSCF
 * frame is a transport/scheduling choice made by the caller (avtp.h), not a
 * property of either endpoint type. This module therefore operates at the
 * ACF level only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their
 * decode counterparts) -- a caller wraps (or unwraps) the frames this
 * module produces (or consumes) in NTSCF/TSCF using avtp.h directly.
 *
 * ── Single channel per byte_bus_id, shared 4-byte {period, active_duration}
 *    payload shape ─────────────────────────────────────────────────────────
 *
 * Unlike ep_spi.h's up-to-six evt-selected channels, this module models one
 * PWM_OUT output (and, independently, one PWM_IN input capture) per
 * byte_bus_id -- there is no channel selector on the wire for either
 * endpoint type. Both share the same on-wire payload shape,
 * rcp_ep_pwm_value_t: a big-endian 16-bit period followed by a big-endian
 * 16-bit active-duration, RCP_EP_PWM_PAYLOAD_LEN (4) octets total -- this
 * module's own wire-layout choice, directly mirroring the width and
 * ordering ep_gpio.h's 4-byte bitmask already establishes for a
 * fixed-shape endpoint payload.
 *
 * ── PWM_OUT: the eight GPIO write-semantics variants, reused ────────────────
 *
 * A PWM_OUT write request's ACF byte_message_info.evt field (acf.h; a 4-bit
 * field on the wire) carries write semantics in its low three bits,
 * evt[2:0], via rcp_ep_pwm_out_write_semantics_t -- deliberately the exact
 * same eight values ep_gpio.h's rcp_ep_gpio_write_semantics_t already
 * enumerates (extraction §4.5 Group C), per the roadmap's explicit
 * instruction to reuse GPIO's write-semantics design as PWM_OUT's own
 * direct template rather than reinventing it. Six of the eight
 * (replace/OR/AND/XOR/add/subtract) apply independently to each of the two
 * 16-bit fields making up rcp_ep_pwm_value_t -- this module's own
 * extension of ep_gpio.h's single-32-bit-register write-semantics rule to
 * a pair of independent 16-bit registers sharing one wire payload, with
 * add/subtract saturating at each field's own 0x0000/0xFFFF boundary
 * rather than wrapping (or, worse, carrying between the two fields, which
 * this module deliberately avoids by applying arithmetic per field, never
 * on the two fields' 32-bit concatenation). RCP_EP_PWM_OUT_WRITE_RESERVED4
 * (value 4) is, like GPIO's own RESERVED4, a documented no-op for a wire
 * value with no assigned write behavior. This ordering (4=reserved,
 * 5=add, 6=subtract) matches ep_gpio.h's own correction -- see issue #104.
 * Subtract computes request minus current (the payload value is the
 * minuend, the current register value the subtrahend), the operand order
 * the single evt[2:0]=110b row covering GPIO and PWM_OUT jointly states
 * normatively; the row's parenthetical remark about decreasing a duty
 * cycle is an illustrative note, not a competing definition.
 *
 * ── PWM_OUT: evt[2:0] == 111b is an addressed EP_func register write ───────
 *
 * The eighth value, RCP_EP_PWM_OUT_WRITE_RECONFIG (value 7), is not a data
 * write at all: the payload is not presented at the interface but is
 * instead an *addressed write into this endpoint's own EP_func
 * configuration block* -- a 16-bit big-endian relative start address
 * followed by the configuration data octets to write from that address
 * onward (extraction §3.7.1). The block itself (extraction §5.7.2) holds
 * the endpoint's real configuration registers: enable&clr, options, base
 * clock, status, clock divider, the output signal flags, duty-cycle min
 * and max, and the output skew. See the "PWM_OUT: the EP_func register
 * block" section below for the offsets, widths, access classes, and
 * rcp_ep_pwm_out_apply_reconfig()/_render_registers()/
 * _encode_reconfig_request().
 *
 * ── PWM_OUT triggers: cycle-start / mid-pulse / done ────────────────────────
 *
 * rcp_ep_pwm_out_trigger_t names the three asynchronous-event trigger
 * modes a PWM_OUT channel's functional config may select, plus NONE --
 * this endpoint type's own analogue of ep_spi.h's
 * rcp_ep_spi_trigger_fires() pattern, adapted to the events a PWM output
 * channel actually produces. These are noted by the roadmap as useful for
 * phase-locking ADC sampling to PWM output cadence; no code coupling
 * between this module and ep_adc.h is mandated or present here -- each
 * endpoint type's triggers are independently evaluated, and any
 * phase-locking is a caller-level (RC Server) concern outside this
 * milestone's scope.
 *
 * CLARIFIED 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group C): TC18
 * §13.7.5.1 Table 42 names its three trigger signals (exec-done,
 * cycle-start, mid-pulse) as fixed hardware output lines an endpoint
 * "creates" -- Table 43 (this endpoint's own functional-config register
 * block) defines no register field that selects among them, so nothing
 * in the specification suggests a client ever configures which one is
 * active; the natural reading is that a real implementation exposes all
 * three simultaneously. rcp_ep_pwm_out_trigger_t's single, mutually-
 * exclusive `trigger` field (plus a NONE/off state neither Table 42 nor
 * 43 defines) is this module's own original simplification, letting a
 * caller name the one event it cares about rather than modeling three
 * independent always-on signals. `cfg->trigger` is never rendered onto
 * the wire (rcp_ep_pwm_out_render_registers() does not touch it) -- this
 * simplification has no wire-format consequence, unlike the WAKEUP
 * SleepCMD case (issue #256 Group E) where an analogous simplification
 * did.
 *
 * ── PWM_IN: response-only capture, PWM_IN_NO_SIGNAL on timeout ─────────────
 *
 * PWM_IN has no write request of its own -- a caller only ever issues a
 * read request (rcp_ep_pwm_in_encode_read_request(), payload-free, mirroring
 * ep_gpio.h's own read request shape) and receives back the most recently
 * captured rcp_ep_pwm_value_t. RCP_EP_PWM_IN_NO_SIGNAL is the sentinel
 * value (this module's own numeric choice, not reproduced from the
 * specification) either field of a PWM_IN response payload carries when no
 * valid edge-to-edge measurement completed within the endpoint's
 * applicable timeout window -- named here for PWM_IN (extraction §5.6)
 * even though the roadmap's own milestone-67 scope text places this
 * sentinel's first mention inside its ADC bullet; ep_adc.h reuses this
 * exact constant (by including this header) for its own raw-sample
 * timeout marker rather than declaring a second, potentially
 * inconsistent, sentinel of its own -- see ep_adc.h's file header.
 * rcp_ep_pwm_in_trigger_t names PWM_IN's own two edge-trigger modes
 * (rising/falling), plus NONE. CLARIFIED 2026-08-10 (c-RCP-AUDIT-06,
 * issue #256 Group C): TC18 §13.7.6.1 Table 44 names these as PWM_IN's
 * two fixed, always-on hardware trigger signals, not a client-selectable
 * register field (PWM_IN has no functional-config register block for
 * triggers at all in this codebase, matching TC18's own silence on any
 * such register) -- the exclusive-select `trigger` field (plus a
 * NONE/off state Table 44 doesn't define) is this module's own original
 * simplification, exactly like PWM_OUT's (see that section's own note,
 * above). Never wire-serialized.
 *
 * ── PWM_IN: the EP_func register block (evt[2:0] == 111b) ──────────────────
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I): this endpoint type
 * was entirely missing from every prior batch's own "N of 11 endpoint
 * types" accounting for the generic §12.7.1 mechanism -- REQ-CFG-011's/
 * REQ-CFG-012's own text enumerated PWM_OUT, GPIO, SPI, I2C, UART, LIN, and
 * ADC as "seven of eleven", counting the pwm.c module as fully done once
 * PWM_OUT's own fix (issue #256, earlier milestone) landed, without ever
 * separately checking whether PWM_IN -- a functionally distinct endpoint
 * type sharing this same source file, with its own TC18 table -- needed the
 * identical fix. It did: rcp_ep_pwm_in_functional_cfg_t modeled only
 * `trigger` (this module's own original, non-wire field -- see above), with
 * no counterpart for any of TC18 §13.7.6.2 Table 45's real registers.
 *
 * Worse than every prior batch's own finding in this Group: this was not
 * merely an unreachable path (like ADC's own decode_read_request()
 * correctly rejecting evt=111b with no counterpart implementing it) but a
 * live conformance bug. rcp_ep_pwm_in_decode_read_request() never checked
 * evt[2:0] at all -- neither rcp_acf_evt_row2_is_plain() nor any BAD_EVT
 * error code existed for this endpoint type -- so a real evt=111b
 * configuration-write request from a conforming peer would have been
 * silently misinterpreted as an ordinary read request instead of being
 * rejected or routed to a reconfiguration handler. Fixed by adding the same
 * rcp_acf_evt_row2_is_plain() check every other endpoint type in this
 * endpoint-type group (ADC/I2C/LIN/CAN/UART/ISELED/MDIO) already has, plus
 * a new RCP_EP_PWM_IN_ERR_BAD_EVT value.
 *
 * TC18 §13.7.6.2 Table 45 defines a clean, ten-entry register block with no
 * address-collision editorial defect (unlike GPIO's/I2C's own source
 * tables): 0x0000 pwmi_ep_len(R), 0x0001 reserved(R), 0x0002/0x0003 common
 * entries, 0x0004 16-bit pwmi_base_clk(R), 0x0006 16-bit pwmi_ep_status
 * (R/W), 0x0008 8-bit pwmi_clk_divider(R/W), 0x0009 a bitfield octet
 * (pwmi_polarity bit 0, pwmi_err_on_max_period bit 1, pwmi_continuous_mode
 * bit 2, 5 reserved bits -- see RCP_EP_PWM_IN_FLAG_* below), 0x000A 16-bit
 * pwmi_max_period(R/W); EP_FUNC_LEN=0x000C. rcp_ep_pwm_in_functional_cfg_t
 * gains ep_status/clk_divider/flags/max_period; `trigger` is untouched,
 * still never wire-serialized, per the section above. New
 * rcp_ep_pwm_in_render_registers()/_apply_reconfig()/_reconfig_strerror()/
 * _encode_reconfig_request() mirror PWM_OUT's/ADC's own pattern exactly.
 *
 * ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ────────────
 *
 * A future compound-wait request (generic compound-wait plumbing itself
 * lands at Phase 17 milestone 69) that targets a PWM_IN endpoint compares
 * one of its two captured sub-fields (period or active-duration, this
 * module's own reading of "duty-cycle sub-field" as the raw
 * active-duration tick count PWM_IN already reports, requiring no
 * additional percentage computation) against a caller-supplied threshold
 * using one of four numeric comparison modes selected by evt[2:0] =
 * 4..7 (100b..111b, extraction §4.6) -- a property of this endpoint type
 * itself, not of the compound-wait mechanism, and therefore implemented
 * and unit-tested here, now, via rcp_ep_pwm_in_compound_wait_compare():
 * this module's own comparison-mode helper, following the exact
 * "isolated precedent" the roadmap calls out at ep_spi.h's
 * rcp_ep_spi_compound_wait_status_equal() (milestone 65).
 *
 * ── Functional configuration ────────────────────────────────────────────────
 *
 * rcp_ep_pwm_out_functional_cfg_t and rcp_ep_pwm_in_functional_cfg_t each
 * compose regmap.h's rcp_regmap_ep_functional_cfg_t as their own first
 * member (per that module's documented convention, same as every prior
 * endpoint type). rcp_ep_pwm_out_functional_cfg_writable()/
 * rcp_ep_pwm_in_functional_cfg_writable() are, likewise, thin, named
 * wrappers over server.h's rcp_lifecycle_field_writable()
 * (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W), and every setter below consults the
 * applicable one before ever touching cfg -- reusing, never duplicating,
 * server.h's/regmap.h's existing authorization logic, per the roadmap's
 * explicit instruction (the same rule every prior endpoint type's own
 * setters already follow).
 */
#ifndef RCP_EP_PWM_H
#define RCP_EP_PWM_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/lifecycle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Shared {period, active_duration} payload shape ─────────────────────────── */

/* The fixed payload length (octets) of a PWM_OUT write request/response or
 * a PWM_IN response -- see the file header. */
#define RCP_EP_PWM_PAYLOAD_LEN ((size_t)4u)

/* One period/active-duration measurement or setpoint, in this module's own
 * tick units (the specification's unit choice is out of scope for this
 * milestone's wire-format concern). Shared, byte-for-byte identical on the
 * wire, by both PWM_OUT (setpoint) and PWM_IN (captured measurement). */
typedef struct {
    uint16_t period;
    uint16_t active_duration;
} rcp_ep_pwm_value_t;

/* The sentinel value either field of a PWM_IN response -- or, by the same
 * reuse, an ADC raw sample (ep_adc.h) -- reports when no valid measurement
 * completed within the applicable timeout window. See the file header. */
#define RCP_EP_PWM_IN_NO_SIGNAL ((uint16_t)0xFFFFu)

/* ── PWM_OUT: evt[2:0], the eight write-semantics variants (reused from
 *    ep_gpio.h -- see the file header) ──────────────────────────────────────── */

typedef enum {
    RCP_EP_PWM_OUT_WRITE_REPLACE   = 0,
    RCP_EP_PWM_OUT_WRITE_OR        = 1,
    RCP_EP_PWM_OUT_WRITE_AND       = 2,
    RCP_EP_PWM_OUT_WRITE_XOR       = 3,
    RCP_EP_PWM_OUT_WRITE_RESERVED4 = 4, /* documented no-op; see the file header */
    RCP_EP_PWM_OUT_WRITE_ADD       = 5,
    RCP_EP_PWM_OUT_WRITE_SUB       = 6,
    RCP_EP_PWM_OUT_WRITE_RECONFIG  = 7, /* the reconfiguration escape hatch */
} rcp_ep_pwm_out_write_semantics_t;

/* True iff v (a raw evt[2:0] value as decoded off the wire) is one of the
 * eight defined write-semantics values, i.e. v <= 7. */
bool rcp_ep_pwm_out_write_semantics_valid(uint8_t v);

/* Computes the new {period, active_duration} pair from current and a write
 * request of value request under evt's semantics, applying each of the six
 * ordinary data-write semantics independently to period and to
 * active_duration (each its own 16-bit register -- see the file header).
 * evt must be one of RCP_EP_PWM_OUT_WRITE_REPLACE .. _SUB or _RESERVED4 --
 * never RCP_EP_PWM_OUT_WRITE_RECONFIG (use rcp_ep_pwm_out_apply_reconfig()
 * for that one instead). RCP_EP_PWM_OUT_WRITE_ADD/_SUB saturate each field
 * at 0xFFFF/0x0000 respectively rather than wrapping or carrying into the
 * other field. RCP_EP_PWM_OUT_WRITE_RESERVED4 returns current unchanged;
 * so, fail-safe, does RCP_EP_PWM_OUT_WRITE_RECONFIG itself, for a caller
 * that violates the "never RECONFIG here" contract.
 *
 * FIXED 2026-08-12 (issue #201, REQ-PWM-056): duty_cycle_min/duty_cycle_max
 * are the endpoint's own rcp_ep_pwm_out_functional_cfg_t fields of the
 * same name (TC18 Table 43: "Min/Max value of PWM active in clock cycles,
 * requests with lower/higher values will be capped to this limit"). The
 * resulting active_duration -- after evt's own write semantics have
 * already been applied -- is CAPPED into [duty_cycle_min, duty_cycle_max]
 * rather than rejected or applied verbatim, per that table's own wording.
 * period is not affected -- Table 43 names only "PWM active" (the active
 * phase duration), not the whole period. Applied unconditionally,
 * including for RCP_EP_PWM_OUT_WRITE_RESERVED4/_RECONFIG's own "current
 * unchanged" cases: idempotent if current already satisfied the limits,
 * and self-correcting (rather than silently leaving a now-out-of-range
 * value in place) if the limits themselves changed since active_duration
 * was last written. A caller passing duty_cycle_min > duty_cycle_max (an
 * inverted, malformed configuration this function does not itself
 * validate) gets duty_cycle_min applied last and so wins -- the same
 * "later cap always wins" fail-safe behavior a caller relying on either
 * limit alone would see from a single clamp. */
rcp_ep_pwm_value_t rcp_ep_pwm_out_apply_write(rcp_ep_pwm_value_t current,
                                               rcp_ep_pwm_value_t request,
                                               rcp_ep_pwm_out_write_semantics_t evt,
                                               uint16_t duty_cycle_min,
                                               uint16_t duty_cycle_max);

/* ── PWM_OUT: triggers ──────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_PWM_OUT_TRIGGER_NONE        = 0,
    RCP_EP_PWM_OUT_TRIGGER_CYCLE_START = 1,
    RCP_EP_PWM_OUT_TRIGGER_MID_PULSE   = 2,
    RCP_EP_PWM_OUT_TRIGGER_DONE        = 3,
} rcp_ep_pwm_out_trigger_t;

/* The three asynchronous events a PWM_OUT channel's trigger mode may be
 * evaluated against -- see rcp_ep_pwm_out_trigger_fires(). */
typedef enum {
    RCP_EP_PWM_OUT_EVENT_CYCLE_START = 0,
    RCP_EP_PWM_OUT_EVENT_MID_PULSE   = 1,
    RCP_EP_PWM_OUT_EVENT_DONE        = 2,
} rcp_ep_pwm_out_event_t;

/* True iff event satisfies trigger: never for NONE; for CYCLE_START iff
 * event == RCP_EP_PWM_OUT_EVENT_CYCLE_START; for MID_PULSE iff event ==
 * RCP_EP_PWM_OUT_EVENT_MID_PULSE; for DONE iff event ==
 * RCP_EP_PWM_OUT_EVENT_DONE. */
bool rcp_ep_pwm_out_trigger_fires(rcp_ep_pwm_out_trigger_t trigger, rcp_ep_pwm_out_event_t event);

/* ── PWM_OUT: functional config ─────────────────────────────────────────────── */

/* The PWM_OUT endpoint's functional config: regmap.h's shared prefix
 * (which carries the two EP-common register octets, enable&clr and
 * options) plus the PWM_OUT-specific registers of the EP_func block --
 * see the register-block section below for each field's own relative
 * offset, width and access class. */
typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header. Carries the block's
                                               enable&clr (0x0002) and
                                               options (0x0003) octets. */
    uint8_t                        trigger; /* rcp_ep_pwm_out_trigger_t; this
                                                module's own field, not part
                                                of the EP_func block */
    uint16_t                       base_clk;       /* 0x0004, R   */
    uint16_t                       ep_status;      /* 0x0006, R/W */
    uint8_t                        clk_divider;    /* 0x0008, R/W */
    uint8_t                        signal_flags;   /* 0x0009, R/W; see the
                                                       RCP_EP_PWM_OUT_FLAG_*
                                                       masks */
    uint16_t                       duty_cycle_min; /* 0x000A, R/W */
    uint16_t                       duty_cycle_max; /* 0x000C, R/W */
    uint8_t                        skew;           /* 0x000E, R/W */
} rcp_ep_pwm_out_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false -- including
 * common.ep_enable, this endpoint's own enable bit -- trigger
 * RCP_EP_PWM_OUT_TRIGGER_NONE, and every EP_func register 0). */
void rcp_ep_pwm_out_functional_cfg_init(rcp_ep_pwm_out_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (server.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_pwm_out_functional_cfg_writable(rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->trigger to trigger iff rcp_ep_pwm_out_functional_cfg_writable()
 * authorizes the write for state/writer; returns whether the write was
 * applied. cfg is left entirely unchanged when it returns false. */
bool rcp_ep_pwm_out_set_trigger(rcp_ep_pwm_out_functional_cfg_t *cfg,
                                 rcp_ep_pwm_out_trigger_t trigger,
                                 rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_pwm_out_set_trigger(), for
 * cfg->common.ep_enable -- the EP-common enable bit regmap.h already
 * models, which is what the EP_func block's own enable&clr register
 * carries (this endpoint type does not have a second, separate enable
 * flag of its own). */
bool rcp_ep_pwm_out_set_enabled(rcp_ep_pwm_out_functional_cfg_t *cfg, bool enabled,
                                 rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* ── PWM_OUT: the EP_func register block (the evt[2:0] == 111b target) ────── */

/* Relative octet offsets of the registers making up a PWM_OUT endpoint's
 * own EP_func block, at the widths and in the order the specification's
 * PWM_OUT functional-configuration register table assigns them
 * (extraction §5.7.2). Every multi-octet register is big-endian, like
 * every other multi-octet field this codebase encodes. Offsets marked R
 * are read-only: a configuration write covering them leaves them
 * unchanged (see rcp_ep_pwm_out_apply_reconfig()). */
#define RCP_EP_PWM_OUT_REG_EP_LEN         ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_PWM_OUT_REG_RESERVED_01    ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_PWM_OUT_REG_EP_ENABLE_CLR  ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_PWM_OUT_REG_EP_OPTIONS     ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_PWM_OUT_REG_BASE_CLK       ((uint16_t)0x0004u) /* 16 bit, R   */
#define RCP_EP_PWM_OUT_REG_EP_STATUS      ((uint16_t)0x0006u) /* 16 bit, R/W */
#define RCP_EP_PWM_OUT_REG_CLK_DIVIDER    ((uint16_t)0x0008u) /*  8 bit, R/W */
#define RCP_EP_PWM_OUT_REG_SIGNAL_FLAGS   ((uint16_t)0x0009u) /*  8 bit, R/W */
#define RCP_EP_PWM_OUT_REG_DUTY_CYCLE_MIN ((uint16_t)0x000Au) /* 16 bit, R/W */
#define RCP_EP_PWM_OUT_REG_DUTY_CYCLE_MAX ((uint16_t)0x000Cu) /* 16 bit, R/W */
#define RCP_EP_PWM_OUT_REG_SKEW           ((uint16_t)0x000Eu) /*  8 bit, R/W */

/* The block's own length in octets -- one past the last assigned offset,
 * i.e. the value the endpoint reports at RCP_EP_PWM_OUT_REG_EP_LEN and
 * the bound the "write beyond EP_LEN is ignored" rule (extraction §3.7.1)
 * is applied against.
 *
 * Note a known editorial defect in the source table: its own description
 * of the EP_LEN register quotes a length shorter than the extent of the
 * very table it heads (the table assigns registers well past that
 * quoted value). The table's assigned offsets are the authoritative
 * statement of the block's shape, so the block length is derived from
 * them here rather than from the inconsistent quoted constant. */
#define RCP_EP_PWM_OUT_EP_FUNC_LEN        ((uint16_t)0x000Fu)

/* Bit masks within the RCP_EP_PWM_OUT_REG_SIGNAL_FLAGS octet.
 *
 * Second known editorial defect in the same source table: it assigns
 * *two* different one-bit parameters (the primary output's idle state and
 * the inverted output's idle state) to the same bit position .1, which
 * cannot be what is meant -- the two are independent settings and the
 * table lists them as separate rows. The three flags are therefore packed
 * here at the only self-consistent positions the table's own ordering
 * admits: bit 0, bit 1, bit 2 in row order. */
#define RCP_EP_PWM_OUT_FLAG_INV_POLARITY   ((uint8_t)(1u << 0))
#define RCP_EP_PWM_OUT_FLAG_IDLE_STATE     ((uint8_t)(1u << 1))
#define RCP_EP_PWM_OUT_FLAG_IDLE_STATE_INV ((uint8_t)(1u << 2))

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- the address is a 16-bit
 * big-endian field, followed by the configuration data octets to write
 * from that address onward (extraction §3.7.1). */
#define RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_PWM_OUT_RECONFIG_OK              = 0,
    RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT       = 1, /* payload carries no
                                                     address prefix, or an
                                                     address prefix with no
                                                     data octet after it */
    RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data
                                                      length exceeds
                                                      RCP_EP_PWM_OUT_EP_FUNC_LEN
                                                      -- the whole write is
                                                      ignored, per the
                                                      specification's own
                                                      rule */
} rcp_ep_pwm_out_reconfig_errc_t;

/* Human-readable message for an rcp_ep_pwm_out_reconfig_errc_t value.
 * Never returns NULL. */
const char *rcp_ep_pwm_out_reconfig_strerror(rcp_ep_pwm_out_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_PWM_OUT_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_pwm_out_apply_reconfig()'s own parse step, and
 * the same rendering that function patches in place. */
void rcp_ep_pwm_out_render_registers(const rcp_ep_pwm_out_functional_cfg_t *cfg,
                                      uint8_t out[RCP_EP_PWM_OUT_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is
 * NOT presented at the interface but interpreted as an addressed write
 * into this endpoint's own EP_func block -- a 16-bit big-endian relative
 * start address followed by the configuration data octets to write from
 * that address onward (extraction §3.7.1). This is a real register write,
 * reaching every R/W register the block defines (enable/options, status,
 * clock divider, signal flags, duty-cycle min/max, skew), not a single
 * enable toggle.
 *
 * Returns RCP_EP_PWM_OUT_RECONFIG_ERR_SHORT when payload_len is not at
 * least RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would
 * extend past RCP_EP_PWM_OUT_EP_FUNC_LEN; in both cases cfg is left
 * entirely unchanged, per the specification's own "such a payload is to be
 * ignored" rule. Octets of the addressed span that land on a read-only
 * register (EP_LEN, the reserved octet, base_clk) are left at their
 * current values while the rest of the span is still applied -- writes to
 * a read-only register are ignored, not treated as an error, matching this
 * codebase's fail-safe convention. Partially-covered multi-octet registers
 * are handled correctly: the write is applied at octet granularity over
 * the block's rendered image, so writing only the high octet of, say,
 * duty_cycle_max changes only that octet's contribution.
 *
 * A caller routing a decoded write request here is responsible for having
 * checked that evt[2:0] really was RCP_EP_PWM_OUT_WRITE_RECONFIG;
 * rcp_ep_pwm_out_apply_write() deliberately no-ops for that evt value so a
 * misrouted request cannot silently corrupt the data registers. */
rcp_ep_pwm_out_reconfig_errc_t
rcp_ep_pwm_out_apply_reconfig(rcp_ep_pwm_out_functional_cfg_t *cfg,
                               const uint8_t *payload, size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_pwm_out_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                    uint16_t start_address,
                                                    const uint8_t *data, size_t data_len,
                                                    uint8_t transaction_num);

/* ── PWM_OUT: error codes ──────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_PWM_OUT_OK                  = 0,
    RCP_EP_PWM_OUT_ERR_SHORT_FRAME     = 1,
    RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_PWM_OUT_ERR_WRONG_BUS       = 3,
    RCP_EP_PWM_OUT_ERR_WRONG_OP        = 4,
    RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN = 5,
} rcp_ep_pwm_out_errc_t;

/* Human-readable message for an rcp_ep_pwm_out_errc_t value. Never returns
 * NULL. */
const char *rcp_ep_pwm_out_strerror(rcp_ep_pwm_out_errc_t e);

/* ── PWM_OUT: read request ─────────────────────────────────────────────────── */

/* Encodes an ACF_ABB read request addressed to byte_bus_id, with no
 * payload -- mirrors ep_gpio.h's own read request shape. Returns a zeroed
 * rcp_bytes_t (data=NULL) on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_pwm_out_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint8_t transaction_num);

/* Decodes and validates an ACF-level PWM_OUT read request from b[0..len).
 * Fails with RCP_EP_PWM_OUT_ERR_SHORT_FRAME if b is shorter than the
 * ACF_ABB fixed header or its declared payload length;
 * RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_PWM_OUT_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_PWM_OUT_ERR_WRONG_OP if its op is not RCP_ACF_OP_READ. On
 * RCP_EP_PWM_OUT_OK, *out_transaction_num is populated. */
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_read_request(const uint8_t *b, size_t len,
                                                          rcp_byte_bus_id_t expected_bus_id,
                                                          uint8_t *out_transaction_num);

/* ── PWM_OUT: write request ────────────────────────────────────────────────── */

/* Encodes an ACF_ABB write request addressed to byte_bus_id: evt's low
 * three bits carry evt (an rcp_ep_pwm_out_write_semantics_t value, 0-7;
 * any other bits of the ACF header's evt field are left 0), and the
 * payload is value as RCP_EP_PWM_PAYLOAD_LEN big-endian octets (period
 * then active_duration). Returns a zeroed rcp_bytes_t (data=NULL) on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_pwm_out_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                                 rcp_ep_pwm_value_t value,
                                                 rcp_ep_pwm_out_write_semantics_t evt,
                                                 uint8_t transaction_num);

/* Decodes and validates a PWM_OUT write request. Same ACF-level failure
 * modes as rcp_ep_pwm_out_decode_read_request() (short frame / bad msg
 * type / wrong bus), except RCP_EP_PWM_OUT_ERR_WRONG_OP is returned when
 * op is not RCP_ACF_OP_WRITE, and RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN is
 * returned when the payload is not exactly RCP_EP_PWM_PAYLOAD_LEN octets.
 * On RCP_EP_PWM_OUT_OK, *out_value, *out_evt (evt[2:0] of the header's evt
 * field; see rcp_ep_pwm_out_write_semantics_valid()), and
 * *out_transaction_num are populated. */
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_write_request(const uint8_t *b, size_t len,
                                                           rcp_byte_bus_id_t expected_bus_id,
                                                           rcp_ep_pwm_value_t *out_value,
                                                           uint8_t *out_evt,
                                                           uint8_t *out_transaction_num);

/* ── PWM_OUT: response ─────────────────────────────────────────────────────── */

/* Encodes a PWM_OUT response carrying value as its RCP_EP_PWM_PAYLOAD_LEN
 * big-endian payload, echoing transaction_num -- answers either a read or
 * a write request, mirroring ep_gpio.h's own single-response-codec
 * convention. Encoded as ACF_ABB when timed is false; as ACF_GBB (with
 * message_timestamp set to timestamp, mtv = RCP_ACF_MTV_VALID) when timed
 * is true. Returns a zeroed rcp_bytes_t (data=NULL) on allocation
 * failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_pwm_out_encode_response(rcp_byte_bus_id_t byte_bus_id, rcp_ep_pwm_value_t value,
                                            uint8_t transaction_num, bool timed, uint64_t timestamp);

/* Decodes a PWM_OUT response from either an ACF_ABB or ACF_GBB message
 * (this function peeks the ACF message type itself, unlike the request
 * decoders above, since a response's encoding depends on the responding
 * endpoint's own timed/untimed choice). Fails with
 * RCP_EP_PWM_OUT_ERR_SHORT_FRAME (frame too short for the applicable
 * fixed header or a full RCP_EP_PWM_PAYLOAD_LEN payload),
 * RCP_EP_PWM_OUT_ERR_WRONG_BUS (byte_bus_id != expected_bus_id), or
 * RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN (payload present but not exactly
 * RCP_EP_PWM_PAYLOAD_LEN octets). On RCP_EP_PWM_OUT_OK, *out_value and
 * *out_transaction_num are populated; *out_timed and *out_timestamp
 * report whether the message was ACF_GBB with a valid
 * (rcp_acf_gbb_is_timed()) timestamp, and that timestamp's value (0 when
 * !*out_timed). */
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_response(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      rcp_ep_pwm_value_t *out_value,
                                                      bool *out_timed, uint64_t *out_timestamp,
                                                      uint8_t *out_transaction_num);

/* ── PWM_IN: triggers ───────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_PWM_IN_TRIGGER_NONE    = 0,
    RCP_EP_PWM_IN_TRIGGER_RISING  = 1,
    RCP_EP_PWM_IN_TRIGGER_FALLING = 2,
} rcp_ep_pwm_in_trigger_t;

/* True iff a level transition from prev_level to new_level satisfies
 * trigger: never for NONE; for RISING iff prev_level is false and
 * new_level is true; for FALLING iff prev_level is true and new_level is
 * false. */
bool rcp_ep_pwm_in_trigger_fires(rcp_ep_pwm_in_trigger_t trigger, bool prev_level, bool new_level);

/* ── PWM_IN: functional config ─────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header. Carries the block's
                                               enable&clr (0x0002) and
                                               options (0x0003) octets. */
    uint8_t                        trigger; /* rcp_ep_pwm_in_trigger_t; this
                                                module's own field, not part
                                                of the EP_func block */
    uint16_t                       base_clk;        /* 0x0004, R   */
    uint16_t                       ep_status;       /* 0x0006, R/W */
    uint8_t                        clk_divider;     /* 0x0008, R/W */
    uint8_t                        flags;           /* 0x0009, R/W; see the
                                                        RCP_EP_PWM_IN_FLAG_*
                                                        masks */
    uint16_t                       max_period;      /* 0x000A, R/W */
} rcp_ep_pwm_in_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, trigger
 * RCP_EP_PWM_IN_TRIGGER_NONE, and every EP_func register 0). */
void rcp_ep_pwm_in_functional_cfg_init(rcp_ep_pwm_in_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- same convention as rcp_ep_pwm_out_functional_cfg_writable(). */
bool rcp_ep_pwm_in_functional_cfg_writable(rcp_lifecycle_state_t state,
                                           rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->trigger to trigger iff rcp_ep_pwm_in_functional_cfg_writable()
 * authorizes the write for state/writer; returns whether the write was
 * applied. cfg is left entirely unchanged when it returns false. */
bool rcp_ep_pwm_in_set_trigger(rcp_ep_pwm_in_functional_cfg_t *cfg,
                                rcp_ep_pwm_in_trigger_t trigger, rcp_lifecycle_state_t state,
                                rcp_lifecycle_writer_ctx_t writer);

/* ── PWM_IN: the EP_func register block (evt[2:0] == 111b) ────────────────── */

/* Relative octet offsets of the registers making up a PWM_IN endpoint's own
 * EP_func block, at the widths and in the order TC18 §13.7.6.2 Table 45
 * assigns them. See the file header. */
#define RCP_EP_PWM_IN_REG_EP_LEN        ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_PWM_IN_REG_RESERVED_01   ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_PWM_IN_REG_EP_ENABLE_CLR ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_PWM_IN_REG_EP_OPTIONS    ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_PWM_IN_REG_BASE_CLK      ((uint16_t)0x0004u) /* 16 bit, R   */
#define RCP_EP_PWM_IN_REG_EP_STATUS     ((uint16_t)0x0006u) /* 16 bit, R/W */
#define RCP_EP_PWM_IN_REG_CLK_DIVIDER   ((uint16_t)0x0008u) /*  8 bit, R/W */
#define RCP_EP_PWM_IN_REG_FLAGS         ((uint16_t)0x0009u) /*  8 bit, R/W */
#define RCP_EP_PWM_IN_REG_MAX_PERIOD    ((uint16_t)0x000Au) /* 16 bit, R/W */

/* The block's own length in octets -- one past the last assigned offset. */
#define RCP_EP_PWM_IN_EP_FUNC_LEN       ((uint16_t)0x000Cu)

/* Bit masks within the RCP_EP_PWM_IN_REG_FLAGS octet -- Table 45's own three
 * named single-bit parameters (polarity, err_on_max_period,
 * continuous_mode), packed at the offsets the table's own row order
 * assigns; the remaining 5 bits are reserved and always read 0. */
#define RCP_EP_PWM_IN_FLAG_POLARITY          ((uint8_t)(1u << 0))
#define RCP_EP_PWM_IN_FLAG_ERR_ON_MAX_PERIOD ((uint8_t)(1u << 1))
#define RCP_EP_PWM_IN_FLAG_CONTINUOUS_MODE   ((uint8_t)(1u << 2))

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- see
 * RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN's own identical note. */
#define RCP_EP_PWM_IN_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_PWM_IN_RECONFIG_OK               = 0,
    RCP_EP_PWM_IN_RECONFIG_ERR_SHORT        = 1, /* payload carries no
                                                      address prefix, or an
                                                      address prefix with no
                                                      data octet after it */
    RCP_EP_PWM_IN_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data
                                                      length exceeds
                                                      RCP_EP_PWM_IN_EP_FUNC_LEN
                                                      -- the whole write is
                                                      ignored, per the
                                                      specification's own
                                                      rule */
} rcp_ep_pwm_in_reconfig_errc_t;

/* Human-readable message for an rcp_ep_pwm_in_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_pwm_in_reconfig_strerror(rcp_ep_pwm_in_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_PWM_IN_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them --
 * the inverse of rcp_ep_pwm_in_apply_reconfig()'s own parse step. */
void rcp_ep_pwm_in_render_registers(const rcp_ep_pwm_in_functional_cfg_t *cfg,
                                     uint8_t out[RCP_EP_PWM_IN_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is an
 * addressed write into this endpoint's own EP_func block -- a 16-bit
 * big-endian relative start address followed by the configuration data
 * octets to write from that address onward (extraction §3.7.1). See
 * rcp_ep_pwm_out_apply_reconfig()'s own doc comment for the read-only-
 * offset-skipping, octet-granularity-patch, and out-of-range-ignores-the-
 * whole-write rules -- identical here.
 *
 * A caller routing a decoded request here is responsible for having checked
 * that evt[2:0] really was 111b, e.g. via !rcp_acf_evt_row2_is_plain(). */
rcp_ep_pwm_in_reconfig_errc_t
rcp_ep_pwm_in_apply_reconfig(rcp_ep_pwm_in_functional_cfg_t *cfg,
                              const uint8_t *payload, size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_pwm_in_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                   uint16_t start_address,
                                                   const uint8_t *data, size_t data_len,
                                                   uint8_t transaction_num);

/* ── PWM_IN: error codes ───────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_PWM_IN_OK                  = 0,
    RCP_EP_PWM_IN_ERR_SHORT_FRAME     = 1,
    RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_PWM_IN_ERR_WRONG_BUS       = 3,
    RCP_EP_PWM_IN_ERR_WRONG_OP        = 4,
    RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN = 5,
    RCP_EP_PWM_IN_ERR_BAD_EVT         = 6, /* evt[2:0] is not one of the
                                               plain-request values
                                               rcp_acf_evt_row2_is_plain()
                                               accepts -- FIXED 2026-08-11,
                                               issue #256 Group I: this
                                               endpoint type previously never
                                               checked evt[2:0] at all, so a
                                               real evt=111b configuration
                                               request was silently
                                               misinterpreted as an ordinary
                                               read. See the file header. */
} rcp_ep_pwm_in_errc_t;

/* Human-readable message for an rcp_ep_pwm_in_errc_t value. Never returns
 * NULL. */
const char *rcp_ep_pwm_in_strerror(rcp_ep_pwm_in_errc_t e);

/* ── PWM_IN: read request ──────────────────────────────────────────────────── */

/* Encodes an ACF_ABB read request addressed to byte_bus_id, with no
 * payload -- see the file header's "response-only capture" note. Returns
 * a zeroed rcp_bytes_t (data=NULL) on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_pwm_in_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                               uint8_t transaction_num);

/* Decodes and validates an ACF-level PWM_IN read request from b[0..len).
 * Same failure modes as rcp_ep_pwm_out_decode_read_request(), plus
 * RCP_EP_PWM_IN_ERR_BAD_EVT when hdr.evt is not one of the plain-request
 * values rcp_acf_evt_row2_is_plain() accepts -- FIXED 2026-08-11, issue
 * #256 Group I; see the file header. On RCP_EP_PWM_IN_OK,
 * *out_transaction_num is populated. */
rcp_ep_pwm_in_errc_t rcp_ep_pwm_in_decode_read_request(const uint8_t *b, size_t len,
                                                        rcp_byte_bus_id_t expected_bus_id,
                                                        uint8_t *out_transaction_num);

/* ── PWM_IN: response ──────────────────────────────────────────────────────── */

/* Encodes a PWM_IN response carrying value (the most recently captured
 * period/active-duration pair; either or both fields may legitimately be
 * RCP_EP_PWM_IN_NO_SIGNAL -- see the file header) as its
 * RCP_EP_PWM_PAYLOAD_LEN big-endian payload, echoing transaction_num.
 * Same ACF_ABB/ACF_GBB timed/untimed choice as
 * rcp_ep_pwm_out_encode_response(). Returns a zeroed rcp_bytes_t
 * (data=NULL) on allocation failure. Caller frees the result with
 * rcp_bytes_free(). */
rcp_bytes_t rcp_ep_pwm_in_encode_response(rcp_byte_bus_id_t byte_bus_id, rcp_ep_pwm_value_t value,
                                           uint8_t transaction_num, bool timed, uint64_t timestamp);

/* Decodes a PWM_IN response. Same failure modes and *out_timed/
 * *out_timestamp semantics as rcp_ep_pwm_out_decode_response(). On
 * RCP_EP_PWM_IN_OK, *out_value and *out_transaction_num are populated;
 * *out_value's fields may legitimately equal RCP_EP_PWM_IN_NO_SIGNAL,
 * round-tripped exactly like any other uint16_t value -- this decoder
 * assigns that sentinel no special decode-time meaning of its own. */
rcp_ep_pwm_in_errc_t rcp_ep_pwm_in_decode_response(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    rcp_ep_pwm_value_t *out_value, bool *out_timed,
                                                    uint64_t *out_timestamp,
                                                    uint8_t *out_transaction_num);

/* ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ────────────── */

typedef enum {
    RCP_EP_PWM_IN_CMP_PERIOD_GE = 4, /* 100b */
    RCP_EP_PWM_IN_CMP_PERIOD_LE = 5, /* 101b */
    RCP_EP_PWM_IN_CMP_DUTY_GE   = 6, /* 110b */
    RCP_EP_PWM_IN_CMP_DUTY_LE   = 7, /* 111b */
} rcp_ep_pwm_in_compound_wait_mode_t;

/* True iff v is one of the four defined compound-wait comparison modes
 * against PWM_IN, i.e. v in 4..7. */
bool rcp_ep_pwm_in_compound_wait_mode_valid(uint8_t v);

/* Raw comparison-mode helper for a future compound-wait request (Phase 17
 * milestone 69), following the exact precedent set by ep_spi.h's
 * rcp_ep_spi_compound_wait_status_equal() (milestone 65) -- see the file
 * header. Compares captured.period (modes PERIOD_GE/PERIOD_LE) or
 * captured.active_duration (modes DUTY_GE/DUTY_LE, this module's own
 * reading of "duty-cycle sub-field" -- see the file header) against
 * threshold, per TC18 §13.5.1's own GE/LE naming (evt[2:0]=100b/110b,
 * 101b/111b): "GE" means the wire's byte_msg_payload (threshold) is >=
 * the current interface status (captured) -- i.e. captured <= threshold
 * -- and "LE" is the mirror, captured >= threshold, matching src/acf.c's
 * rcp_acf_compound_wait_match() reference implementation of this same
 * rule (COMPOUND_WAIT_MODE_HI_GE/_LE). Returns false (never an error
 * code -- this module's fail-safe treatment, mirroring
 * rcp_ep_spi_compound_wait_status_equal()'s own too-short-buffer case) for
 * an invalid mode, and equally false whenever the relevant captured
 * sub-field itself equals RCP_EP_PWM_IN_NO_SIGNAL -- a "no signal"
 * measurement is never treated as satisfying (or failing to satisfy) a
 * numeric comparison, it is simply never a match. */
bool rcp_ep_pwm_in_compound_wait_compare(rcp_ep_pwm_value_t captured,
                                          rcp_ep_pwm_in_compound_wait_mode_t mode,
                                          uint16_t threshold);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_PWM_H */
