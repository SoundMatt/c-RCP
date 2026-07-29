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
 * The eighth,
 * RCP_EP_PWM_OUT_WRITE_RECONFIG (value 7), is this endpoint type's own
 * single-flag analogue of ep_gpio.h's per-pin reconfiguration escape
 * hatch: rather than writing the period/active-duration registers, it
 * reinterprets the same 4-byte payload as a single enable/disable
 * selector (bit 0 of the payload's 32-bit-wide view; see
 * rcp_ep_pwm_out_apply_reconfig() below) -- adapted, by this module's own
 * original design, from a 32-pin bitmask selector (GPIO has 32
 * independently addressable pins) down to PWM_OUT's own single output
 * channel (this endpoint type addresses none).
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
 * (rising/falling), plus NONE.
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
 * that violates the "never RECONFIG here" contract. */
rcp_ep_pwm_value_t rcp_ep_pwm_out_apply_write(rcp_ep_pwm_value_t current,
                                               rcp_ep_pwm_value_t request,
                                               rcp_ep_pwm_out_write_semantics_t evt);

/* Applies the reconfiguration escape hatch (evt[2:0] == 7): iff bit 0 of
 * reconfig_word (this module's own reinterpretation of the same 4-byte
 * write payload as a 32-bit selector, big-endian, period-then-
 * active_duration -- i.e. (uint32_t)period << 16 | active_duration) is
 * set, toggles *enabled; bits 1..31 are reserved and ignored. *enabled is
 * left unchanged when bit 0 is clear -- this module's own single-flag
 * analogue of ep_gpio.h's per-pin reconfiguration toggle, adapted to a
 * PWM_OUT endpoint's single output channel (see the file header). */
void rcp_ep_pwm_out_apply_reconfig(bool *enabled, uint32_t reconfig_word);

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

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint8_t                        trigger; /* rcp_ep_pwm_out_trigger_t */
    bool                           enabled; /* toggled by the write-request
                                                reconfiguration escape hatch,
                                                or directly by
                                                rcp_ep_pwm_out_set_enabled() */
} rcp_ep_pwm_out_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, trigger
 * RCP_EP_PWM_OUT_TRIGGER_NONE, enabled false). */
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
 * cfg->enabled. */
bool rcp_ep_pwm_out_set_enabled(rcp_ep_pwm_out_functional_cfg_t *cfg, bool enabled,
                                 rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

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
    rcp_regmap_ep_functional_cfg_t common; /* see the file header */
    uint8_t                        trigger; /* rcp_ep_pwm_in_trigger_t */
} rcp_ep_pwm_in_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, trigger
 * RCP_EP_PWM_IN_TRIGGER_NONE). */
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

/* ── PWM_IN: error codes ───────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_PWM_IN_OK                  = 0,
    RCP_EP_PWM_IN_ERR_SHORT_FRAME     = 1,
    RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_PWM_IN_ERR_WRONG_BUS       = 3,
    RCP_EP_PWM_IN_ERR_WRONG_OP        = 4,
    RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN = 5,
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
 * Same failure modes as rcp_ep_pwm_out_decode_read_request(). On
 * RCP_EP_PWM_IN_OK, *out_transaction_num is populated. */
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
 * threshold using the >= or <= operator the mode selects. Returns false
 * (never an error code -- this module's fail-safe treatment, mirroring
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
