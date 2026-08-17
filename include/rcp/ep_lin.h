/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-LINEP-006
//cfusa:req REQ-LINEP-007
//cfusa:req REQ-LINEP-008
//cfusa:req REQ-LINEP-009
//cfusa:req REQ-LINEP-010
//cfusa:req REQ-LINEP-011
//cfusa:req REQ-LINEP-012
//cfusa:req REQ-LINEP-013
//cfusa:req REQ-LINEP-014
//cfusa:req REQ-LINEP-015
//cfusa:req REQ-LINEP-016
//cfusa:req REQ-LINEP-017
//cfusa:req REQ-LINEP-018
//cfusa:req REQ-LINEP-019
//cfusa:req REQ-LINEP-020
//cfusa:req REQ-LINEP-021
//cfusa:req REQ-LINEP-022

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-LINEP-023
//cfusa:req REQ-LINEP-024
//cfusa:req REQ-LINEP-025
//cfusa:req REQ-LINEP-026
//cfusa:req REQ-LINEP-027
/*
 * ep_lin.h -- LIN endpoint for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 19, "Remaining Endpoint Types", milestone 71).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any prior endpoint file
 * (ep_gpio.h/.c, ep_spi.h/.c, ep_i2c.h/.c, ep_uart.h/.c, ep_pwm.h/.c,
 * ep_adc.h/.c) is touched here -- the same layering discipline those
 * modules established, followed structurally throughout by this module
 * too.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── Explicit scope validation: no classic LIN-frame concept at this layer ──
 *
 * Per extraction §5.10, this protocol layer has no classic LIN-frame
 * concept: no checksum-mode selection (classic vs. enhanced), no PID/
 * identifier generation, and no schedule-table mechanism of any kind. This
 * endpoint is a "dumb" raw-byte pusher -- a request's payload is placed
 * directly onto the LIN bus exactly as supplied, byte for byte, with every
 * LIN-frame semantic (identifier/PID byte, checksum byte, inter-frame
 * spacing, schedule ordering) constructed entirely client-side before the
 * request is ever encoded here. This module never itself inspects,
 * generates, or validates a PID, a checksum, or a schedule-table entry.
 *
 * This is a *deliberate, explicit validation* of that scope, not an
 * assumption carried in from this repository's pre-replacement history.
 * The old, unrelated Zone/Command "RCP" protocol this repository carried
 * before the TC18 replacement program (see ROADMAP.md's "Protocol
 * Replacement Notice") had its own `linbr.h`/`linbr.c` LIN bridge stub
 * (`rcp_lin_config_t.frame_id`, a classic-LIN-identifier-shaped field) --
 * that stub models a materially different job (bridging to an *external*
 * LIN segment via a frame-ID-addressed master-frame request) and is left
 * untouched by this milestone (its own disposition is ADAPT/narrowed-role,
 * satellite rework, ROADMAP.md v0.81.0). Nothing about `linbr.h`'s
 * frame-ID-based model -- or about how any older, retired informal LIN
 * handling in this codebase's history worked -- carries forward to this
 * endpoint type. Anyone extending this file later who finds themselves
 * reaching for a PID/checksum/schedule-table concept should stop and
 * re-read this paragraph first: it is not a gap to be "fixed" back in, it
 * is this milestone's stated scope.
 *
 * That same untouched `linbr.c` stub already owns the `REQ-LIN-*`
 * requirement-id prefix in `.fusa-reqs.json` (its RPC-facing stub
 * behaviors), so this module's own requirements are tagged `REQ-LINEP-*`
 * ("LIN endpoint") instead, to stay collision-free rather than overload an
 * id prefix two unrelated modules would otherwise both claim -- a naming
 * seam the roadmap's Phase 21 satellite rework (`linbr.c`'s own eventual
 * disposition) will need to keep in mind, not something this milestone
 * resolves.
 *
 * ── Framing level ────────────────────────────────────────────────────────────
 *
 * As with every endpoint type before it, a LIN command request/response is
 * ordinary endpoint traffic: whether it rides an NTSCF or TSCF frame is a
 * transport/scheduling choice made by the caller (avtp.h), not a property
 * of the LIN endpoint itself. This module therefore operates at the ACF
 * level only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their decode
 * counterparts) -- a caller wraps (or unwraps) the frames this module
 * produces (or consumes) in NTSCF/TSCF using avtp.h directly.
 *
 * ── Commander-only, single bus per endpoint ─────────────────────────────────
 *
 * This endpoint models a LIN *commander* (master) only -- no responder
 * (slave) mode. Like ep_i2c.h/ep_uart.h, exactly one LIN bus is addressed
 * per byte_bus_id; there is no multi-channel selector on the wire.
 *
 * ── Command request: a raw byte-stream push, no protocol-level parsing ─────
 *
 * A command request's payload is the *raw* sequence of bytes this endpoint
 * drives directly onto the bus for that transaction -- see the scope
 * validation above; this module never inspects, strips, or reformats any
 * byte of it. Encoded as ACF_OP_READ -- the read direction is the one
 * that expects a data response, and this endpoint's own reply rule is
 * stated in terms of exactly that direction (extraction §5.10.1): a
 * command request pushes bytes onto the bus *and* asks for what came
 * back, so it is a read-direction request, not a write one. Decoded
 * payloads are *borrowed* pointers into the
 * caller-supplied frame buffer, matching acf.c's own decode_abb()/
 * decode_gbb() convention, for the same reason ep_i2c.h's/ep_uart.h's own
 * raw payloads are borrowed rather than copied: a variable-length payload
 * has no natural fixed-size out-parameter to copy into.
 *
 * ── evt[2:0]: LIN sits in Table 33's plain-request row, like every other
 *    non-SPI/GPIO/PWM_OUT endpoint ─────────────────────────────────────────
 *
 * v0.112.0 REMOVED this module's own invented eight-value
 * rcp_ep_lin_compare_mode_t: an earlier milestone's own file header
 * admitted it was "this module's own original design... rather than on
 * any spec-derived enumeration -- there being no such enumeration cited
 * by the roadmap to derive one from." There is one: TC18 §13.5 Table 33
 * places LIN in the same {ADC, PWM_IN, I2C, LIN, CAN, UART, ISELED, MDIO}
 * row as every endpoint type acf.h's rcp_acf_evt_row2_is_plain() already
 * governs -- evt[2:0] = 000b is the only value a plain (non-configuration)
 * LIN command request may carry; every other value is either reserved
 * (rejected with UNSUPPORTED_CMD) or the config-write shape (111b,
 * §12.7.1, out of this module's scope). LIN is not called out as an
 * exception anywhere in Table 33 (pixel-verified against the rendered
 * specification page, same pass that found CAN's Figure 39 defect).
 *
 * §13.7.10.1's own prose -- "the LIN endpoint checks each received message
 * against the byte_msg_payload and if a match under the conditions given
 * by evt[2:0] is found a reply is sent" -- describes the SAME universal
 * evt[2:0] vocabulary Table 33/§13.5.1 define everywhere else, not a
 * LIN-private one: since Table 33 constrains a plain LIN request's
 * evt[2:0] to 000b, the only "condition" that can ever apply to an
 * ordinary (non-compound-wait) LIN command request is §13.5.1 mode 000b,
 * exact match -- which is exactly what a LIN commander's own physical
 * behavior needs (correlating one of possibly several asynchronous bus
 * replies against the expected payload), unlike ADC/I2C/UART/etc., where
 * evt[2:0] = 000b carries no comparison meaning at all. This module's
 * rcp_ep_lin_response_matches() therefore delegates directly to acf.h's
 * shared rcp_acf_compound_wait_match(evt=0, ...) rather than
 * reimplementing exact-match comparison logic of its own -- the same
 * single-source-of-truth reuse this module's compound-wait dispatch
 * (server.h/request_compound.h) already established for every endpoint
 * type's own current-status comparison.
 *
 * ── Transmission-done trigger ────────────────────────────────────────────────
 *
 * rcp_ep_lin_trigger_t names this endpoint type's one asynchronous-event
 * trigger mode (transmission-done), plus NONE -- this endpoint's own
 * analogue of ep_spi.h's TRANSFER_DONE trigger, narrowed to the one event a
 * commander-only, no-frame-semantics LIN push actually produces.
 * rcp_ep_lin_trigger_fires() is the pure, directly-testable evaluation of
 * that event against a selected trigger mode.
 *
 * NOTED 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group C): unlike SPI/
 * PWM_OUT/PWM_IN, TC18 defines no "lin trigger outputs" table at all --
 * this endpoint type's trigger concept has no TC18 basis whatsoever, not
 * even the fixed-hardware-signal basis SPI/PWM_OUT/PWM_IN each have (see
 * their own file headers). It is entirely this module's own original
 * design, reusing ep_spi.h's evaluation-function *shape* only. Never
 * wire-serialized.
 *
 * ── Functional configuration: lin_clk_divider bit-time clock ───────────────
 *
 * rcp_ep_lin_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that
 * module's documented convention, same as every endpoint type before it)
 * and adds this endpoint's two runtime-adjustable fields: lin_clk_divider
 * (the divider this endpoint's own bit-time clock is derived from -- this
 * module's own unit choice, a raw divider value rather than a derived
 * frequency, matching ep_spi.h's own clock_divider field shape) and
 * trigger (rcp_ep_lin_trigger_t). rcp_ep_lin_functional_cfg_writable() is,
 * likewise, a thin, named wrapper over server.h's
 * rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W), and every
 * rcp_ep_lin_set_*() mutator consults it before ever touching cfg --
 * reusing, never duplicating, server.h's/regmap.h's existing authorization
 * logic, per the roadmap's explicit instruction (the same rule every prior
 * endpoint type's own setters already follow).
 *
 * ── The EP_func register block (evt[2:0] == 111b) ──────────────────────────
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-LINEP-024):
 * rcp_ep_lin_decode_command_request() already correctly rejected evt[2:0]
 * = 111b as not a plain command request (RCP_EP_LIN_ERR_BAD_EVT, via
 * acf.h's rcp_acf_evt_row2_is_plain()), but no counterpart implemented
 * that TC18 §12.7.1 configuration-write path -- the same class of gap
 * SPI's/I2C's/UART's own earlier fixes closed. TC18 §13.7.10.2 Table 55
 * defines a clean, five-entry register block with no address-collision
 * editorial defect (unlike GPIO's/I2C's own source tables):
 *
 *   0x0000  lin_ep_len       8 bit  R    RCP_EP_LIN_EP_FUNC_LEN (0x09)
 *   0x0001  Reserved         8 bit  R    reads 0x00
 *   0x0002  lin_ep_enable&clr 8 bit R/W  Table 32 common entries
 *   0x0003  lin_ep_options   8 bit  R/W* Table 32 common entries
 *   0x0004  lin_base_clk    16 bit  R    LIN system clock
 *   0x0006  lin_ep_status   16 bit  R/W
 *   0x0008  lin_clk_divider  8 bit  R/W  generates the LIN bit time
 *
 * REQ-LINEP-024's own prior text credited the pre-existing
 * lin_clk_divider field (above) with already covering this last
 * register -- imprecisely: that field is this module's own original,
 * uint32_t, unit-unspecified design (explicitly documented, above, as
 * matching ep_spi.h's own non-wire clock_divider shape), not a literal
 * 8-bit wire register. Rather than reinterpret it -- the same
 * "don't silently redefine an existing public field" caution SPI's own
 * baud_rate_kbps-vs-clock_divider split and UART's own
 * baud_rate_kbps-vs-baud_rate split already established -- a new,
 * distinct wire_clk_divider (uint8_t) field carries the real register;
 * lin_base_clk is not itself stored (no setter, no meaningful derivable
 * value) and always renders 0, the same "no real clock source
 * modelled" honesty ep_gpio.h's/ep_i2c.h's own base_clk fields already
 * commit to.
 */
#ifndef RCP_EP_LIN_H
#define RCP_EP_LIN_H

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

/* ── evt[2:0]: exact-match, per Table 33's plain-request row ─────────────── */

/* True iff a received-bus-data buffer rx_data[0..rx_len) matches the
 * outgoing command request's own tx_data[0..tx_len) under TC18 §13.5.1
 * mode 000b (exact match, length-capped) -- the only comparison a plain
 * LIN command request's evt[2:0] can ever select (Table 33 constrains it
 * to 000b). A thin, named wrapper over acf.h's
 * rcp_acf_compound_wait_match(0, ...); see the file header. tx_data/
 * rx_data may be NULL iff their respective length is 0. */
bool rcp_ep_lin_response_matches(const uint8_t *tx_data, size_t tx_len,
                                  const uint8_t *rx_data, size_t rx_len);

/* ── Transmission-done trigger ─────────────────────────────────────────────── */

typedef enum {
    RCP_EP_LIN_TRIGGER_NONE     = 0,
    RCP_EP_LIN_TRIGGER_TX_DONE  = 1,
} rcp_ep_lin_trigger_t;

/* True iff tx_done_event and trailing_time_expired together satisfy
 * trigger: never for NONE; for TX_DONE iff BOTH are true -- TC18
 * §13.7.10.1's own text (REQ-LINEP-023): "The LIN EP issues a trigger
 * when a transmission has been finalized, AND the configured trailing
 * time has expired." Table 55 (§13.7.10.2) defines no dedicated wire
 * register for "the configured trailing time" -- like this endpoint
 * type's own trigger concept as a whole (see the file header: "TC18
 * defines no 'lin trigger outputs' table at all... entirely this
 * module's own original design"), trailing_time_expired is a
 * caller-classified boolean input this module does not itself derive
 * from a clock or a wire-configured duration, matching the same
 * pure-function, caller-supplies-already-classified-inputs convention
 * every other endpoint type's own trigger-evaluation function uses. */
bool rcp_ep_lin_trigger_fires(rcp_ep_lin_trigger_t trigger, bool tx_done_event,
                               bool trailing_time_expired);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint32_t                       lin_clk_divider; /* bit-time clock divider;
                                                         see the file header */
    uint8_t                        trigger;         /* rcp_ep_lin_trigger_t */
    uint16_t                       ep_status;         /* lin_ep_status, Table 55 */
    uint8_t                        wire_clk_divider;  /* lin_clk_divider, Table 55 --
                                                           see the file header */
} rcp_ep_lin_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, lin_clk_divider 0,
 * trigger RCP_EP_LIN_TRIGGER_NONE, ep_status/wire_clk_divider 0). */
void rcp_ep_lin_functional_cfg_init(rcp_ep_lin_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (server.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_lin_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->lin_clk_divider to lin_clk_divider iff
 * rcp_ep_lin_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_lin_set_clk_divider(rcp_ep_lin_functional_cfg_t *cfg, uint32_t lin_clk_divider,
                                 rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_lin_set_clk_divider(), for
 * cfg->trigger. */
bool rcp_ep_lin_set_trigger(rcp_ep_lin_functional_cfg_t *cfg, rcp_ep_lin_trigger_t trigger,
                             rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* ── The EP_func register block (the evt[2:0] == 111b target) ──────────────── */

/* Relative octet offsets of the registers making up this endpoint's own
 * EP_func block -- see the file header. Every multi-octet register is
 * big-endian, like every other multi-octet field this codebase encodes.
 * Offsets marked R are read-only: a configuration write covering them
 * leaves them unchanged (see rcp_ep_lin_apply_reconfig()). */
#define RCP_EP_LIN_REG_EP_LEN        ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_LIN_REG_RESERVED_01   ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_LIN_REG_EP_ENABLE_CLR ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_LIN_REG_EP_OPTIONS    ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_LIN_REG_BASE_CLK      ((uint16_t)0x0004u) /* 16 bit, R   */
#define RCP_EP_LIN_REG_EP_STATUS     ((uint16_t)0x0006u) /* 16 bit, R/W */
#define RCP_EP_LIN_REG_CLK_DIVIDER   ((uint16_t)0x0008u) /*  8 bit, R/W */

/* The block's own length in octets -- one past the last assigned offset,
 * i.e. the value the endpoint reports at RCP_EP_LIN_REG_EP_LEN and the
 * bound the "write beyond EP_LEN is ignored" rule (§12.7.1) is applied
 * against. Table 55's own addressing is internally consistent, so there
 * is no editorial defect to resolve here. */
#define RCP_EP_LIN_EP_FUNC_LEN ((uint16_t)0x0009u)

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- the address is a 16-bit
 * big-endian field, followed by the configuration data octets to write
 * from that address onward (§12.7.1). */
#define RCP_EP_LIN_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_LIN_RECONFIG_OK               = 0,
    RCP_EP_LIN_RECONFIG_ERR_SHORT        = 1, /* payload carries no address
                                                  prefix, or an address
                                                  prefix with no data octet
                                                  after it */
    RCP_EP_LIN_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data length
                                                  exceeds
                                                  RCP_EP_LIN_EP_FUNC_LEN --
                                                  the whole write is ignored,
                                                  per the specification's own
                                                  rule */
} rcp_ep_lin_reconfig_errc_t;

/* Human-readable message for an rcp_ep_lin_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_lin_reconfig_strerror(rcp_ep_lin_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_LIN_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_lin_apply_reconfig()'s own parse step, and the
 * same rendering that function patches in place. lin_base_clk (read-only)
 * always renders 0 -- see the file header. */
void rcp_ep_lin_render_registers(const rcp_ep_lin_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_LIN_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is NOT
 * presented at the interface but interpreted as an addressed write into
 * this endpoint's own EP_func block -- a 16-bit big-endian relative start
 * address followed by the configuration data octets to write from that
 * address onward (§12.7.1). This is a real register write, reaching every
 * R/W register the block defines (enable/options, status, clock
 * divider), not merely lin_clk_divider.
 *
 * Returns RCP_EP_LIN_RECONFIG_ERR_SHORT when payload_len is not at least
 * RCP_EP_LIN_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_LIN_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would
 * extend past RCP_EP_LIN_EP_FUNC_LEN; in both cases cfg is left entirely
 * unchanged, per the specification's own "such a payload is to be ignored"
 * rule. Octets of the addressed span that land on a read-only register
 * (EP_LEN, the reserved octet, base_clk) are left at their current values
 * while the rest of the span is still applied.
 *
 * A caller routing a decoded request here is responsible for having
 * checked that evt[2:0] really was 111b -- rcp_ep_lin_decode_command_request()
 * already rejects it (RCP_EP_LIN_ERR_BAD_EVT) so a misrouted request
 * cannot reach that path by accident. */
rcp_ep_lin_reconfig_errc_t rcp_ep_lin_apply_reconfig(rcp_ep_lin_functional_cfg_t *cfg,
                                                      const uint8_t *payload,
                                                      size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_lin_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint16_t start_address, const uint8_t *data,
                                                size_t data_len, uint8_t transaction_num);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_LIN_OK               = 0,
    RCP_EP_LIN_ERR_SHORT_FRAME  = 1,
    RCP_EP_LIN_ERR_BAD_MSG_TYPE = 2,
    RCP_EP_LIN_ERR_WRONG_BUS    = 3,
    RCP_EP_LIN_ERR_WRONG_OP     = 4,
    /* evt[2:0] != 000b on a decoded command request -- Table 33 reserves
     * every value but 000b (plain) and 111b (config-write, out of this
     * module's scope) for this endpoint-type row; see the file header. */
    RCP_EP_LIN_ERR_BAD_EVT      = 5,
} rcp_ep_lin_errc_t;

/* Human-readable message for an rcp_ep_lin_errc_t value. Never returns NULL. */
const char *rcp_ep_lin_strerror(rcp_ep_lin_errc_t e);

/* ── Command request ───────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB command request addressed to byte_bus_id: the payload
 * is exactly tx_data[0..tx_len), the raw bytes driven directly onto the
 * bus for this transaction -- every LIN-frame semantic (identifier/PID,
 * checksum, schedule position) already constructed into these bytes by the
 * caller, and never parsed or validated by this module (see the file
 * header). evt is always encoded 0 -- Table 33 constrains a plain LIN
 * command request's evt[2:0] to 000b (see the file header); there is no
 * client-selectable comparison mode. tx_data may be NULL iff tx_len == 0.
 * Returns a zeroed rcp_bytes_t (data=NULL) if tx_len exceeds
 * RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_lin_encode_command_request(rcp_byte_bus_id_t byte_bus_id,
                                               const uint8_t *tx_data, size_t tx_len,
                                               uint8_t transaction_num);

/* Decodes and validates an ACF-level LIN command request from b[0..len).
 * Fails with RCP_EP_LIN_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_LIN_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_LIN_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_LIN_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_READ; RCP_EP_LIN_ERR_BAD_EVT if
 * rcp_acf_evt_row2_is_plain(hdr.evt) is false (see the file header). On
 * RCP_EP_LIN_OK, *out_transaction_num is populated, and *out_tx_data /
 * *out_tx_len are set to a *borrowed* view into b (not copied -- see the
 * file header) of the raw outgoing payload. */
rcp_ep_lin_errc_t rcp_ep_lin_decode_command_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     const uint8_t **out_tx_data,
                                                     size_t *out_tx_len,
                                                     uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes a LIN response carrying rx_data[0..rx_len) (the raw bytes
 * captured back from the bus that satisfied the originating request's
 * comparison rule -- see rcp_ep_lin_compare_fires(); rx_data may be NULL
 * iff rx_len == 0) as its payload, echoing transaction_num. Encoded as
 * ACF_ABB when timed is false; as ACF_GBB (with message_timestamp set to
 * timestamp, mtv = RCP_ACF_MTV_VALID) when timed is true -- see the file
 * header. Returns a zeroed rcp_bytes_t (data=NULL) if rx_len exceeds
 * RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_lin_encode_response(rcp_byte_bus_id_t byte_bus_id, const uint8_t *rx_data,
                                        size_t rx_len, uint8_t transaction_num, bool timed,
                                        uint64_t timestamp);

/* Decodes a LIN response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoder
 * above, since a response's encoding depends on the responding endpoint's
 * own timed/untimed choice). Fails with RCP_EP_LIN_ERR_SHORT_FRAME (frame
 * too short for the applicable fixed header or its declared payload
 * length) or RCP_EP_LIN_ERR_WRONG_BUS (byte_bus_id != expected_bus_id). On
 * RCP_EP_LIN_OK, *out_transaction_num is populated; *out_rx_data /
 * *out_rx_len are set to a *borrowed* view into b (not copied) of the raw
 * captured payload; *out_timed and *out_timestamp report whether the
 * message was ACF_GBB with a valid (rcp_acf_gbb_is_timed()) timestamp, and
 * that timestamp's value (0 when !*out_timed). */
rcp_ep_lin_errc_t rcp_ep_lin_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              const uint8_t **out_rx_data, size_t *out_rx_len,
                                              bool *out_timed, uint64_t *out_timestamp,
                                              uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_LIN_H */
