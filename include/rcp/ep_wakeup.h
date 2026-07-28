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
 * RCP_ACF_OP_NONE) whose payload is exactly two octets -- the fixed
 * `RCP_EP_WAKEUP_SLEEPCMD_OPCODE` (0xA5) marker byte, then one byte
 * carrying the requested target rcp_pwrmode_t (power.h) value, which must
 * be RCP_PWRMODE_STANDBY or RCP_PWRMODE_SLEEP (SleepCMD's own purpose is
 * requesting entry into one of the two low-power modes; it is not a
 * general-purpose mode-set command -- Normal/Unpowered targets are
 * rejected with RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE). A SleepCMD response
 * carries the same marker byte followed by one byte encoding the
 * power.h rcp_pwrmode_entry_result_t outcome of
 * rcp_pwrmode_check_entry() -- RCP_PWRMODE_ENTRY_OK or
 * RCP_PWRMODE_ENTRY_REFUSED (the roadmap's REQUEST_CANCELED) -- letting a
 * caller apply that gate (power.h) and report its outcome back over this
 * same dedicated request/response pair, entirely independent of
 * request_cancel.h's own, differently-shaped cancellation-result
 * reporting.
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

/* ── Wake-source pin configuration/monitoring ────────────────────────────────── */

#define RCP_EP_WAKEUP_MAX_SOURCES ((size_t)8u)

typedef struct {
    bool enabled;     /* this wake-source slot participates in wake detection */
    bool active_high; /* true: a high pin level asserts wake; false: a low level does */
} rcp_ep_wakeup_source_cfg_t;

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix -- see the file
                                               header */
    rcp_ep_wakeup_source_cfg_t     sources[RCP_EP_WAKEUP_MAX_SOURCES];
} rcp_ep_wakeup_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; every source entry
 * disabled with active_high == false). */
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

typedef struct {
    bool latched;
} rcp_ep_wakeup_wup_status_t;

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
    RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE = 5,
} rcp_ep_wakeup_errc_t;

/* Human-readable message for an rcp_ep_wakeup_errc_t value. Never returns NULL. */
const char *rcp_ep_wakeup_strerror(rcp_ep_wakeup_errc_t e);

/* ── SleepCMD request/response (0xA5) ────────────────────────────────────────── */

/* This endpoint type's fixed SleepCMD marker byte -- see the file header;
 * value taken directly from this codebase's own pre-existing
 * ROADMAP.md milestone-75 scope, not freshly derived here. */
#define RCP_EP_WAKEUP_SLEEPCMD_OPCODE ((uint8_t)0xA5u)

/* Encodes an ACF_ABB SleepCMD request addressed to byte_bus_id: a 2-byte
 * payload of RCP_EP_WAKEUP_SLEEPCMD_OPCODE followed by target_mode's own
 * raw rcp_pwrmode_t value -- see the file header's wire-layout
 * discussion. Returns a zeroed rcp_bytes_t (data=NULL) if target_mode is
 * neither RCP_PWRMODE_STANDBY nor RCP_PWRMODE_SLEEP, or on allocation
 * failure. */
rcp_bytes_t rcp_ep_wakeup_encode_sleepcmd_request(rcp_byte_bus_id_t byte_bus_id,
                                                   rcp_pwrmode_t target_mode,
                                                   uint8_t transaction_num);

/* Decodes and validates an ACF-level SleepCMD request from b[0..len).
 * Fails with RCP_EP_WAKEUP_ERR_SHORT_FRAME if b is shorter than the
 * ACF_ABB fixed header or its declared 2-byte payload;
 * RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_WAKEUP_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_WAKEUP_ERR_BAD_OPCODE if the payload's first byte is not
 * RCP_EP_WAKEUP_SLEEPCMD_OPCODE; RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE if the
 * decoded target mode is neither RCP_PWRMODE_STANDBY nor
 * RCP_PWRMODE_SLEEP. On RCP_EP_WAKEUP_OK, *out_target_mode and
 * *out_transaction_num are populated. */
rcp_ep_wakeup_errc_t rcp_ep_wakeup_decode_sleepcmd_request(const uint8_t *b, size_t len,
                                                            rcp_byte_bus_id_t expected_bus_id,
                                                            rcp_pwrmode_t *out_target_mode,
                                                            uint8_t *out_transaction_num);

/* Encodes an ACF_ABB SleepCMD response carrying result (power.h's
 * rcp_pwrmode_entry_result_t) as its second payload byte, echoing
 * transaction_num. Returns a zeroed rcp_bytes_t (data=NULL) on allocation
 * failure. */
rcp_bytes_t rcp_ep_wakeup_encode_sleepcmd_response(rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_pwrmode_entry_result_t result,
                                                    uint8_t transaction_num);

/* Decodes and validates an ACF-level SleepCMD response from b[0..len),
 * with the same fixed-opcode/short-frame/wrong-bus/wrong-message-type
 * failure modes as rcp_ep_wakeup_decode_sleepcmd_request(). Any second
 * payload byte other than RCP_PWRMODE_ENTRY_OK/_REFUSED's own raw values
 * decodes as RCP_PWRMODE_ENTRY_REFUSED (fail-safe: an unrecognized result
 * byte is never treated as an admitted entry). On RCP_EP_WAKEUP_OK,
 * *out_result and *out_transaction_num are populated. */
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

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_WAKEUP_H */
