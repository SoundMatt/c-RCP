/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-SPI-001
//cfusa:req REQ-SPI-002
//cfusa:req REQ-SPI-003
//cfusa:req REQ-SPI-004
//cfusa:req REQ-SPI-005
//cfusa:req REQ-SPI-006
//cfusa:req REQ-SPI-007
//cfusa:req REQ-SPI-008
//cfusa:req REQ-SPI-009
//cfusa:req REQ-SPI-010
//cfusa:req REQ-SPI-011
//cfusa:req REQ-SPI-012
//cfusa:req REQ-SPI-013
//cfusa:req REQ-SPI-014
//cfusa:req REQ-SPI-015
//cfusa:req REQ-SPI-016
//cfusa:req REQ-SPI-017
//cfusa:req REQ-SPI-018
//cfusa:req REQ-SPI-019
//cfusa:req REQ-SPI-020
//cfusa:req REQ-SPI-021
//cfusa:req REQ-SPI-022
//cfusa:req REQ-SPI-023
//cfusa:req REQ-SPI-024
//cfusa:req REQ-SPI-025
//cfusa:req REQ-SPI-026
//cfusa:req REQ-SPI-027
//cfusa:req REQ-SPI-028
//cfusa:req REQ-SPI-029
//cfusa:req REQ-SPI-030

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-SPI-033
//cfusa:req REQ-SPI-034
//cfusa:req REQ-SPI-035
//cfusa:req REQ-SPI-036
//cfusa:req REQ-SPI-037
/*
 * ep_spi.h -- SPI endpoint for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 16, "Basic Endpoints", milestone 65).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any satellite package is
 * touched here -- exactly the same layering discipline ep_gpio.h/ep_gpio.c
 * (milestone 64) already established, which this module follows structurally
 * throughout.
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
 * As with ep_gpio.h, an SPI transfer request/response is ordinary endpoint
 * traffic: whether it rides an NTSCF or TSCF frame is a transport/scheduling
 * choice made by the caller (avtp.h), not a property of the SPI endpoint
 * itself. This module therefore operates at the ACF level only (acf.h's
 * rcp_acf_encode_abb()/_encode_gbb() and their decode counterparts) -- a
 * caller wraps (or unwraps) the frames this module produces (or consumes)
 * in NTSCF/TSCF using avtp.h directly.
 *
 * ── Controller-only, up to six pre-configured channels ─────────────────────
 *
 * This endpoint models an SPI *controller* only (no peripheral/target
 * mode). Up to RCP_EP_SPI_MAX_CHANNELS (6) independently pre-configured
 * channels are addressable, selected via the ACF byte_message_info header's
 * evt field (acf.h; a 4-bit field on the wire) -- unlike ep_gpio.h, where
 * evt[2:0] carries write *semantics*, here evt[2:0] directly carries the
 * *channel number* (0-5, extraction §4.5 Group A); values 6 and 7 select no
 * defined channel and are rejected on decode (RCP_EP_SPI_ERR_BAD_CHANNEL).
 *
 * INVESTIGATED 2026-08-11 (spec rebaseline to TC18 0.5.1_RC5, c-RCP-AUDIT-06,
 * task #98): a competing BBID-based channel-selection concept exists in the
 * 0.5.1_RC5 draft -- a new Table 26 "BBID control bits" defines a real,
 * complete, footnoted Channel_selection[3:0] field per BBID row (in
 * EP_ID_config's own new Ctrl1/Ctrl2 field), explicitly footnoted as being
 * for SPI's own channel selection, and §13.7.3.1's own running prose has
 * already been edited in place (RC4) to describe byte_bus_id-based
 * selection replacing evt-bits entirely. CONFIRMED, via the table that is
 * this endpoint type's own authoritative evt[2:0] reference (§13.5's own
 * per-endpoint-type table, "The detailed behavior of each endpoint based
 * on the evt[2:0] is described in the following table"), that this evt-bit
 * channel-selection scheme is still current and NOT superseded: that
 * table's own SPI row is completely unchanged (still "selects channel
 * 0...5"), and its own attached tracked-change comment reads, verbatim,
 * "051RC4: new concept: selection of SPI channel via Stream_id(index)/
 * byte_bus_id (not via evt-bits) => this becomes obsolete IF new concept
 * is accepted" -- explicitly conditional, and as of RC5 that condition has
 * not been met. §13.7.3.1's own already-edited prose is running ahead of
 * this still-undecided proposal, a real (if minor) internal inconsistency
 * in the RC5 draft itself, not an extraction error on this codebase's own
 * part. This module's existing evt-bits implementation remains correct and
 * fully conformant; no code change is warranted by this investigation. If
 * a future spec revision resolves the proposal as accepted, this
 * conclusion (and the whole channel-selection model below) will need
 * revisiting.
 *
 * ── Request/response payload: a full-duplex byte-for-byte transfer ─────────
 *
 * Unlike ep_gpio.h's fixed 4-byte bitmask shape, an SPI transfer's payload
 * is raw, variable-length, and symmetric: a transfer request's payload is
 * the PICO-out (controller-to-peripheral) bytes to shift out, and the
 * matching response's payload is the same-length POCI-in
 * (peripheral-to-controller) bytes captured during that same transfer --
 * this module's own original modeling of a full-duplex SPI exchange as one
 * ACF request/response pair. Both halves are encoded as ACF_OP_READ: a
 * transfer request carries the PICO-out bytes *and* asks for the POCI-in
 * bytes back, which is the read direction (the specification's own worked
 * SPI example -- write N bytes, get a response with M -- carries op=0 with
 * a non-zero read_size; extraction §5.3.3), and the response carries the
 * POCI-in bytes. A response is encoded as
 * ACF_ABB when untimed, or ACF_GBB (carrying a message_timestamp) when the
 * endpoint's ep_response_ts_enable functional-config flag (regmap.h's
 * rcp_regmap_ep_functional_cfg_t, composed into rcp_ep_spi_functional_cfg_t
 * below) is set -- that flag's value is a caller-supplied bool here, this
 * module never itself reaches into a register map to read it, matching
 * ep_gpio.h's own convention of consuming already-classified inputs.
 *
 * Decoded transfer/response payloads are *borrowed* pointers into the
 * caller-supplied frame buffer (matching acf.c's own decode_abb()/
 * decode_gbb() convention for the same reason: a variable-length payload
 * has no natural fixed-size out-parameter to copy into, unlike ep_gpio.h's
 * fixed 4-byte bitmask).
 *
 * ── Per-channel functional configuration ────────────────────────────────────
 *
 * rcp_ep_spi_channel_cfg_t models, per channel: clock mode (one of the four
 * standard CPOL/CPHA combinations, rcp_ep_spi_mode_t --
 * rcp_ep_spi_mode_cpol()/_cpha() are this module's own pure derivation of
 * the two underlying clock-polarity/-phase bits from that mode, directly
 * testable in isolation), bit order (MSB-first / LSB-first), a clock
 * divider, chip-select active-polarity, and inter-byte/inter-transfer
 * timing delays (nanoseconds; this module's own unit choice). NOTED
 * 2026-08-10 (c-RCP-AUDIT-06, issue #256): bit order has no counterpart
 * in TC18 §13.7.3.2 Table 39/42 at all -- that table's per-channel register
 * block defines clock polarity/phase, CS polarity, CS-sharing, lead/
 * trail timing, max-consecutive-bits, and inter-transfer pause, but no
 * MSB-first/LSB-first selector of any kind; this field is this module's
 * own original addition, not derived from or wire-mapped to any TC18
 * register (this module implements no functional-config register
 * render/parse path at all -- see the trigger-signals note, below, for
 * the same point about `trigger`). Composing
 * regmap.h's rcp_regmap_ep_functional_cfg_t as its own first member follows
 * that module's documented convention (and ep_gpio.h's precedent);
 * rcp_ep_spi_functional_cfg_writable() is, likewise, a thin, named wrapper
 * over server.h's rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W),
 * and every rcp_ep_spi_set_channel_*() mutator consults it (and channel
 * validity) before ever touching cfg -- reusing, never duplicating,
 * server.h's/regmap.h's existing authorization logic, per the roadmap's
 * explicit instruction (the same rule ep_gpio.h's own setters already
 * follow).
 *
 * ── Per-channel trigger signals ─────────────────────────────────────────────
 *
 * rcp_ep_spi_trigger_t names the three asynchronous-event trigger modes a
 * channel's functional config may select (transfer-done, CS-assert-edge,
 * CS-deassert-edge), plus NONE -- this endpoint type's own analogue of
 * ep_gpio.h's any-change/rising/falling pin triggers, adapted to the events
 * an SPI controller channel actually produces. rcp_ep_spi_trigger_fires()
 * is the pure, directly-testable evaluation of one such event against a
 * selected trigger mode.
 *
 * CLARIFIED 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group C): TC18
 * §13.7.3.1 Table 38/41 names 14 distinct, always-on hardware trigger
 * signals per SPI endpoint (execution-done, plus an assert/de-assert
 * pair for each of CS0 through CS5) -- Table 39/42 (this endpoint's own
 * per-channel functional-config register block) defines no register
 * field that selects among them, so nothing in the specification
 * suggests a client configures which one(s) are active. This module's
 * `rcp_ep_spi_trigger_t` collapses that 14-signal, per-CS-channel table
 * into 4 generic values (NONE + transfer-done + a single CS-assert/
 * CS-deassert pair with no per-channel distinction) -- an original
 * simplification, not a literal reproduction of Table 38/41, and one that
 * cannot distinguish "CS2 asserted" from "CS5 asserted" the way TC18's
 * own signal set can. `cfg->channels[i].trigger` is never rendered onto
 * the wire (this module implements no functional-config register
 * render/parse path at all), so this simplification has no wire-format
 * consequence.
 *
 * ── The EP_func register block (evt[2:0] == 111b) ──────────────────────────
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-CFG-011/012):
 * TC18 §13.5 Table 33's own SPI row is a *third*, distinct evt[2:0] grouping
 * -- neither the "ADC/PWM_IN/I2C/LIN/CAN/UART/ISELED/MDIO" group (evt[2:0]
 * 000b-110b all reserved, 111b = config-write) nor the "GPIO/PWM_OUT" group
 * (evt[2:0] carries write semantics). SPI's own row: evt[2:0] 000b-101b
 * (0-5, exactly rcp_ep_spi_channel_valid()'s existing range) selects one of
 * the six pre-configured channels and asserts that channel's CSN pin --
 * this module's existing channel-selection design was already correct, not
 * a deviation. evt[2:0] = 110b is reserved (request rejected with error
 * code UNSUPPORTED_CMD). evt[2:0] = 111b is the same generic §12.7.1
 * EP_func addressed-configuration-write mechanism PWM_OUT
 * (rcp_ep_pwm_out_apply_reconfig(), ep_pwm.h) and GPIO
 * (rcp_ep_gpio_apply_reconfig(), ep_gpio.h) already implement -- until now,
 * SPI had no counterpart at all: rcp_ep_spi_channel_valid() rejected evt=6
 * and evt=7 identically as RCP_EP_SPI_ERR_BAD_CHANNEL, so an evt=111b
 * request had no path through this module and TC18 Table 39/42's own SPI
 * functional-configuration register block (§13.7.3.2) was reachable
 * nowhere in the API even though rcp_ep_spi_channel_cfg_t already stored
 * most of the same information in a different (non-wire-mapped) shape --
 * this is exactly REQ-SPI-035's previously-recorded "modeled only in
 * reduced form" gap, closed by the register block below.
 *
 * Table 39/42's layout (channel c's block starts at
 * RCP_EP_SPI_REG_CHANNEL_BASE + c * RCP_EP_SPI_REG_CHANNEL_SPAN):
 *
 *   0x0000     spi_ep_len        8 bit  R    RCP_EP_SPI_EP_FUNC_LEN (0x36)
 *   0x0001.3:0 spi_nr_cs         4 bit  R    RCP_EP_SPI_MAX_CHANNELS-1 (5)
 *   0x0001.7:4 reserved          4 bit  R    Reads: 0000b
 *   0x0002     spi_ep_enable&clr 8 bit  R/W  Table 35 common entries
 *   0x0003     spi_ep_options    8 bit  R/W* Table 35 common entries
 *   0x0004     spi_ep_status    16 bit  R/W
 *   0x0006     channel 0's own 8-octet block (channel 1's own starts at
 *              0x000E, and so on through channel 5's at 0x002E):
 *     +0x00 spi_baud_rateN   16 bit  R/W  kbit/s
 *     +0x02 bit 0 spi_clk_polarityN, bit 1 spi_clk_phaseN, bit 2
 *           spi_cs_polarityN, bit 3 spi_use_csN, bit 4
 *           spi_deassert_cs_pauseN, bits 5-7 reserved
 *                              8 bit  R/W
 *     +0x03 spi_cs_clk_leadtimeN     8 bit  R/W  spi_clk cycles
 *     +0x04 spi_clk_cs_trailtimeN    8 bit  R/W  spi_clk cycles
 *     +0x05 spi_bits_maxN            8 bit  R/W
 *     +0x06 spi_pause_minN           8 bit  R/W  spi_clk cycles
 *     +0x07 reserved                 8 bit  R
 *
 * FIXED 2026-08-11 (spec rebaseline to TC18 0.5.1_RC5, c-RCP-AUDIT-06):
 * two real deltas found via the PDF's own front-matter revision-history
 * table (0.5.1_RC4's own entry, cross-referenced against the RC4/RC5
 * tracked-change markers in the document body):
 *   1. spi_nr_cs (0x0001) was, in the 0.5.1_RC baseline this codebase
 *      originally read against, a plain 8-bit count -- rendered here as
 *      the full RCP_EP_SPI_MAX_CHANNELS byte (6). RC4 narrows it to a
 *      4-bit "(count - 1)" field (tagged "this standard limits the
 *      number of CS line per EP to 32"), leaving the upper nibble
 *      reserved. Fixed to render (RCP_EP_SPI_MAX_CHANNELS - 1) & 0xF
 *      (0x05) in the low nibble, 0 in the high.
 *   2. spi_deassert_cs_pauseN (bit 4 of the +0x02 octet) is a new bit
 *      (RC5, ticket NXP_100) with no counterpart at all in the baseline
 *      this module was built against -- "0b: no de-assertion during
 *      break / 1b: de-assertion during break" (during the pause window
 *      spi_cs_clk_leadtimeN/spi_pause_minN/spi_clk_cs_trailtimeN
 *      define). Added as a new rcp_ep_spi_channel_cfg_t field,
 *      following the same "new field, existing field left untouched"
 *      rule already established for every other Group I register-block
 *      fix this session (never silently redefine an existing field).
 *      The SPI channel-selection mechanism itself (evt-bits vs.
 *      byte_bus_id) is a separate, larger, and still internally
 *      inconsistent question across the document as of RC5 -- flagged
 *      for its own dedicated investigation, deliberately NOT resolved
 *      by this fix, which touches only register content, not routing.
 *
 * clk_polarityN/clk_phaseN round-trip through this module's existing
 * rcp_ep_spi_mode_t (rcp_ep_spi_mode_cpol()/_cpha() render the two bits; a
 * local inverse on parse recovers the mode -- the mapping is bijective, so
 * nothing is lost either direction). spi_cs_clk_leadtimeN/
 * spi_clk_cs_trailtimeN/spi_pause_minN are new, distinct fields from this
 * module's own pre-existing inter_byte_delay_ns/inter_transfer_delay_ns --
 * those remain this module's own nanosecond-denominated original addition
 * (per the file header note on `bit_order`/`trigger`, below) while the new
 * fields carry the wire's own spi_clk-cycle-denominated values. bit_order
 * and trigger, like PWM_OUT's/GPIO's own non-wire-mapped fields, are never
 * rendered onto the wire and are left untouched by a configuration write.
 *
 * ── Compound-wait against an SPI endpoint ───────────────────────────────────
 *
 * v0.111.0 removed this file's own rcp_ep_spi_compound_wait_status_equal()
 * and its RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN constant: both modeled the
 * comparison-length rule as an SPI-specific, hardcoded 4-byte truncation.
 * That was wrong -- TC18 §13.5.1's own length rule (status is capped to
 * byte_msg_payload's own length, whatever that request happens to carry)
 * is universal across every endpoint type, and the specification's own
 * worked example ("only the first four out of 20 received bytes will be
 * checked when byte_msg_payload has only four bytes") illustrates that
 * general rule using SPI, rather than stating an SPI-specific rule of its
 * own. RCP_EP_SPI_STATUS_MAX_LEN below is unaffected (it bounds this
 * endpoint type's own transfer-done status-report width, unrelated to
 * compound-wait's comparison length); the comparison itself now goes
 * through acf.h's rcp_acf_compound_wait_evt_valid()/_match() directly,
 * exactly like every other endpoint type.
 */
#ifndef RCP_EP_SPI_H
#define RCP_EP_SPI_H

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

/* ── Channel addressing ────────────────────────────────────────────────────── */

/* The largest number of pre-configured SPI channels this endpoint type
 * addresses via evt[2:0]. */
#define RCP_EP_SPI_MAX_CHANNELS ((uint8_t)6u)

/* True iff channel is a valid channel index (0..RCP_EP_SPI_MAX_CHANNELS-1),
 * i.e. one of the 6 evt[2:0] values this endpoint type actually assigns a
 * channel to (values 6 and 7 select no defined channel). */
bool rcp_ep_spi_channel_valid(uint8_t channel);

/* ── Clock mode: the 4 standard CPOL/CPHA combinations ─────────────────────── */

typedef enum {
    RCP_EP_SPI_MODE_0 = 0, /* CPOL=0, CPHA=0 */
    RCP_EP_SPI_MODE_1 = 1, /* CPOL=0, CPHA=1 */
    RCP_EP_SPI_MODE_2 = 2, /* CPOL=1, CPHA=0 */
    RCP_EP_SPI_MODE_3 = 3, /* CPOL=1, CPHA=1 */
} rcp_ep_spi_mode_t;

/* True iff v (a raw clock-mode value, e.g. as decoded from a register) is
 * one of the four defined modes, i.e. v <= 3. */
bool rcp_ep_spi_mode_valid(uint8_t v);

/* The clock-polarity (CPOL) bit implied by mode: false for MODE_0/MODE_1,
 * true for MODE_2/MODE_3. An invalid mode value is treated as CPOL false
 * (fail-safe default, mirroring this project's convention of never
 * fabricating a "true" safety-relevant bit for undefined input). */
bool rcp_ep_spi_mode_cpol(rcp_ep_spi_mode_t mode);

/* The clock-phase (CPHA) bit implied by mode: false for MODE_0/MODE_2, true
 * for MODE_1/MODE_3. Same fail-safe treatment of an invalid mode value as
 * rcp_ep_spi_mode_cpol(). */
bool rcp_ep_spi_mode_cpha(rcp_ep_spi_mode_t mode);

/* ── Bit order and chip-select active-polarity ─────────────────────────────── */

typedef enum {
    RCP_EP_SPI_BIT_ORDER_MSB_FIRST = 0,
    RCP_EP_SPI_BIT_ORDER_LSB_FIRST = 1,
} rcp_ep_spi_bit_order_t;

typedef enum {
    RCP_EP_SPI_CS_ACTIVE_LOW  = 0,
    RCP_EP_SPI_CS_ACTIVE_HIGH = 1,
} rcp_ep_spi_cs_polarity_t;

/* ── Per-channel trigger signals ────────────────────────────────────────────── */

typedef enum {
    RCP_EP_SPI_TRIGGER_NONE          = 0,
    RCP_EP_SPI_TRIGGER_TRANSFER_DONE = 1,
    RCP_EP_SPI_TRIGGER_CS_ASSERT     = 2,
    RCP_EP_SPI_TRIGGER_CS_DEASSERT   = 3,
} rcp_ep_spi_trigger_t;

/* The three asynchronous events a channel's trigger mode may be evaluated
 * against -- see rcp_ep_spi_trigger_fires(). */
typedef enum {
    RCP_EP_SPI_EVENT_TRANSFER_DONE = 0,
    RCP_EP_SPI_EVENT_CS_ASSERT     = 1,
    RCP_EP_SPI_EVENT_CS_DEASSERT   = 2,
} rcp_ep_spi_event_t;

/* True iff event satisfies trigger: never for NONE; for TRANSFER_DONE iff
 * event == RCP_EP_SPI_EVENT_TRANSFER_DONE; for CS_ASSERT iff event ==
 * RCP_EP_SPI_EVENT_CS_ASSERT; for CS_DEASSERT iff event ==
 * RCP_EP_SPI_EVENT_CS_DEASSERT. */
bool rcp_ep_spi_trigger_fires(rcp_ep_spi_trigger_t trigger, rcp_ep_spi_event_t event);

/* REQ-SPI-034: TC18 §13.7.3.1's own Table 41 "spi trigger outputs" (RC5;
 * the .fusa-reqs.json record's own citation of "Table 38" is stale --
 * RC5's own Table 38 is the unrelated RC-Server worked example, confirmed
 * directly against the current RC5 baseline PDF; the real trigger-outputs
 * table is Table 41, running "signal 0: SPI execution done / signal 1:
 * reserved / signal 2+2n: CSn asserted / signal 3+2n: CSn de-asserted (0
 * <= n < 16)"). Signal 0 is a whole-endpoint trigger (analogous to
 * ep_gpio.h's own signal 0) this per-channel function deliberately does
 * not model. This module's own RCP_EP_SPI_MAX_CHANNELS (6) narrows
 * Table 41's own n < 16 ceiling to this endpoint's real channel count,
 * the same way ep_gpio.h's RCP_EP_GPIO_MAX_PINS (32) narrows Table 43's
 * IOn range.
 *
 * This is a pure numbering computation, entirely independent of
 * rcp_ep_spi_trigger_t's own deliberately-collapsed, non-wire-rendered
 * per-channel trigger mode (see the file header's "Per-channel trigger
 * signals" section) -- adding it does not touch that design decision or
 * this module's "no wire-format consequence" property, it only lets a
 * caller resolve a Table 41 signal number for a (channel, CS-edge) pair
 * that names one.
 *
 * Returns true and populates *out_signal_number iff channel <
 * RCP_EP_SPI_MAX_CHANNELS and trigger is CS_ASSERT or CS_DEASSERT (never
 * TRANSFER_DONE, which is signal 0's whole-endpoint concept and has no
 * per-channel Table 41 entry, nor NONE, which names no trigger event and
 * therefore no signal number); returns false (*out_signal_number left
 * unchanged) otherwise. */
bool rcp_ep_spi_trigger_signal_number(uint8_t channel, rcp_ep_spi_trigger_t trigger,
                                       uint8_t *out_signal_number);

/* ── Functional config ─────────────────────────────────────────────────────── */

/* One channel's runtime-adjustable functional configuration -- see the
 * file header. Timing delays are in nanoseconds (this module's own unit
 * choice). */
typedef struct {
    uint8_t  mode;                     /* rcp_ep_spi_mode_t */
    uint8_t  bit_order;                /* rcp_ep_spi_bit_order_t */
    uint8_t  cs_polarity;              /* rcp_ep_spi_cs_polarity_t */
    uint8_t  trigger;                  /* rcp_ep_spi_trigger_t */
    uint32_t clock_divider;
    uint32_t inter_byte_delay_ns;
    uint32_t inter_transfer_delay_ns;
    uint16_t baud_rate_kbps;           /* spi_baud_rateN, Table 39/42 -- see the
                                           "EP_func register block" section
                                           of the file header */
    bool     use_common_cs;            /* spi_use_csN: false = this
                                           channel's own CSN is used (the
                                           wire's 0b default), true = the
                                           common CS0 is used instead */
    uint8_t  cs_clk_leadtime;          /* spi_cs_clk_leadtimeN, spi_clk
                                           cycles */
    uint8_t  clk_cs_trailtime;         /* spi_clk_cs_trailtimeN, spi_clk
                                           cycles */
    uint8_t  bits_max;                 /* spi_bits_maxN */
    uint8_t  pause_min;                /* spi_pause_minN, spi_clk cycles */
    bool     deassert_cs_pause;        /* spi_deassert_cs_pauseN (added TC18
                                           spec revision 0.5.1_RC5, ticket
                                           NXP_100 -- see the file header's
                                           own "FIXED 2026-08-11" note):
                                           false = no de-assertion during
                                           the pause (the wire's 0b
                                           default), true = CS is
                                           de-asserted during the pause
                                           window between
                                           spi_cs_clk_leadtimeN and
                                           spi_cs_clk_leadtimeN +
                                           spi_pause_minN */
} rcp_ep_spi_channel_cfg_t;

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    rcp_ep_spi_channel_cfg_t       channels[RCP_EP_SPI_MAX_CHANNELS];
    uint16_t                       ep_status; /* spi_ep_status, Table 39/42 */
} rcp_ep_spi_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; every channel's mode
 * MODE_0, bit_order MSB_FIRST, cs_polarity ACTIVE_LOW, trigger NONE,
 * clock_divider/inter_byte_delay_ns/inter_transfer_delay_ns/
 * baud_rate_kbps/cs_clk_leadtime/clk_cs_trailtime/bits_max/pause_min all 0,
 * use_common_cs false, deassert_cs_pause false; ep_status 0). */
void rcp_ep_spi_functional_cfg_init(rcp_ep_spi_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (server.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_spi_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->channels[channel].mode to mode iff channel is
 * rcp_ep_spi_channel_valid() and rcp_ep_spi_functional_cfg_writable()
 * authorizes the write for state/writer; returns whether the write was
 * applied. cfg is left entirely unchanged when it returns false. */
bool rcp_ep_spi_set_channel_mode(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                  rcp_ep_spi_mode_t mode, rcp_lifecycle_state_t state,
                                  rcp_lifecycle_writer_ctx_t writer);

/* Same authorization/validity rule as rcp_ep_spi_set_channel_mode(), for
 * cfg->channels[channel].bit_order. */
bool rcp_ep_spi_set_channel_bit_order(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                       rcp_ep_spi_bit_order_t bit_order,
                                       rcp_lifecycle_state_t state,
                                       rcp_lifecycle_writer_ctx_t writer);

/* Same authorization/validity rule, for cfg->channels[channel].cs_polarity. */
bool rcp_ep_spi_set_channel_cs_polarity(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                         rcp_ep_spi_cs_polarity_t cs_polarity,
                                         rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer);

/* Same authorization/validity rule, for cfg->channels[channel].clock_divider. */
bool rcp_ep_spi_set_channel_clock_divider(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                           uint32_t clock_divider, rcp_lifecycle_state_t state,
                                           rcp_lifecycle_writer_ctx_t writer);

/* Same authorization/validity rule, for cfg->channels[channel]'s
 * inter_byte_delay_ns and inter_transfer_delay_ns together (one setter for
 * both timing fields, since they are always reconfigured as a pair on the
 * wire). */
bool rcp_ep_spi_set_channel_timing(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                    uint32_t inter_byte_delay_ns,
                                    uint32_t inter_transfer_delay_ns,
                                    rcp_lifecycle_state_t state,
                                    rcp_lifecycle_writer_ctx_t writer);

/* Same authorization/validity rule, for cfg->channels[channel].trigger. */
bool rcp_ep_spi_set_channel_trigger(rcp_ep_spi_functional_cfg_t *cfg, uint8_t channel,
                                     rcp_ep_spi_trigger_t trigger, rcp_lifecycle_state_t state,
                                     rcp_lifecycle_writer_ctx_t writer);

/* ── The EP_func register block (the evt[2:0] == 111b target) ──────────────── */

/* Relative octet offsets of the registers making up the common (non-
 * per-channel) prefix of an SPI endpoint's own EP_func block -- see the
 * file header. Every multi-octet register is big-endian, like every other
 * multi-octet field this codebase encodes. Offsets marked R are read-only:
 * a configuration write covering them leaves them unchanged (see
 * rcp_ep_spi_apply_reconfig()). */
#define RCP_EP_SPI_REG_EP_LEN        ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_SPI_REG_NR_CS         ((uint16_t)0x0001u) /*  8 bit, R -- only
                                                              bits [3:0] carry
                                                              spi_nr_cs
                                                              (count - 1);
                                                              [7:4] reserved,
                                                              see the file
                                                              header's own
                                                              "FIXED
                                                              2026-08-11"
                                                              note */
#define RCP_EP_SPI_REG_EP_ENABLE_CLR ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_SPI_REG_EP_OPTIONS    ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_SPI_REG_EP_STATUS     ((uint16_t)0x0004u) /* 16 bit, R/W */

/* Channel c's own 8-octet block starts at RCP_EP_SPI_REG_CHANNEL_BASE + c *
 * RCP_EP_SPI_REG_CHANNEL_SPAN; the RCP_EP_SPI_CHREG_* offsets below are
 * relative to that channel's own base. */
#define RCP_EP_SPI_REG_CHANNEL_BASE ((uint16_t)0x0006u)
#define RCP_EP_SPI_REG_CHANNEL_SPAN ((uint16_t)0x0008u)

#define RCP_EP_SPI_CHREG_BAUD_RATE    ((uint16_t)0x00u) /* 16 bit, R/W */
#define RCP_EP_SPI_CHREG_CFG          ((uint16_t)0x02u) /*  8 bit, R/W --
                                                             clk_polarity(0)/
                                                             clk_phase(1)/
                                                             cs_polarity(2)/
                                                             use_cs(3)/
                                                             deassert_cs_pause(4),
                                                             bits 5-7
                                                             reserved */
#define RCP_EP_SPI_CHREG_CS_LEADTIME  ((uint16_t)0x03u) /*  8 bit, R/W */
#define RCP_EP_SPI_CHREG_CS_TRAILTIME ((uint16_t)0x04u) /*  8 bit, R/W */
#define RCP_EP_SPI_CHREG_BITS_MAX     ((uint16_t)0x05u) /*  8 bit, R/W */
#define RCP_EP_SPI_CHREG_PAUSE_MIN    ((uint16_t)0x06u) /*  8 bit, R/W */
#define RCP_EP_SPI_CHREG_RESERVED     ((uint16_t)0x07u) /*  8 bit, R   */

/* Bit masks within a channel's RCP_EP_SPI_CHREG_CFG octet. */
#define RCP_EP_SPI_CFG_BIT_CLK_POLARITY      ((uint8_t)(1u << 0))
#define RCP_EP_SPI_CFG_BIT_CLK_PHASE         ((uint8_t)(1u << 1))
#define RCP_EP_SPI_CFG_BIT_CS_POLARITY       ((uint8_t)(1u << 2))
#define RCP_EP_SPI_CFG_BIT_USE_CS            ((uint8_t)(1u << 3))
/* Added TC18 spec revision 0.5.1_RC5, ticket NXP_100 -- see the file
 * header's own "FIXED 2026-08-11" note. */
#define RCP_EP_SPI_CFG_BIT_DEASSERT_CS_PAUSE ((uint8_t)(1u << 4))

/* The block's own length in octets -- one past the last assigned offset,
 * i.e. the value the endpoint reports at RCP_EP_SPI_REG_EP_LEN and the
 * bound the "write beyond EP_LEN is ignored" rule (§12.7.1) is applied
 * against: the 6-octet common prefix plus RCP_EP_SPI_MAX_CHANNELS 8-octet
 * per-channel blocks (0x36 = 54 octets). Unlike PWM_OUT's/GPIO's own source
 * tables, Table 39/42's own elided per-channel rows (channel 1's
 * spi_baud_rate1 explicitly at 0x000E = 0x0006 + 8) are internally
 * consistent with this arithmetic, so there is no editorial defect to
 * resolve here. */
#define RCP_EP_SPI_EP_FUNC_LEN \
    ((uint16_t)(RCP_EP_SPI_REG_CHANNEL_BASE + \
                (uint16_t)RCP_EP_SPI_MAX_CHANNELS * RCP_EP_SPI_REG_CHANNEL_SPAN))

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- the address is a 16-bit
 * big-endian field, followed by the configuration data octets to write
 * from that address onward (§12.7.1). */
#define RCP_EP_SPI_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_SPI_RECONFIG_OK               = 0,
    RCP_EP_SPI_RECONFIG_ERR_SHORT        = 1, /* payload carries no address
                                                  prefix, or an address
                                                  prefix with no data octet
                                                  after it */
    RCP_EP_SPI_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data length
                                                  exceeds
                                                  RCP_EP_SPI_EP_FUNC_LEN --
                                                  the whole write is ignored,
                                                  per the specification's own
                                                  rule */
} rcp_ep_spi_reconfig_errc_t;

/* Human-readable message for an rcp_ep_spi_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_spi_reconfig_strerror(rcp_ep_spi_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_SPI_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_spi_apply_reconfig()'s own parse step, and the
 * same rendering that function patches in place. mode's CPOL/CPHA bits are
 * rendered via rcp_ep_spi_mode_cpol()/_cpha(); bit_order and trigger have
 * no wire counterpart in Table 39/42 (see the file header) and are not
 * rendered. */
void rcp_ep_spi_render_registers(const rcp_ep_spi_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_SPI_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is NOT
 * presented at the interface but interpreted as an addressed write into
 * this endpoint's own EP_func block -- a 16-bit big-endian relative start
 * address followed by the configuration data octets to write from that
 * address onward (§12.7.1). This is a real register write, reaching every
 * R/W register the block defines for every channel (enable/options,
 * status, per-channel baud rate/clock config/CS timing/pause), not merely
 * the channel-selection this module already supported.
 *
 * Returns RCP_EP_SPI_RECONFIG_ERR_SHORT when payload_len is not at least
 * RCP_EP_SPI_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_SPI_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would extend
 * past RCP_EP_SPI_EP_FUNC_LEN; in both cases cfg is left entirely
 * unchanged, per the specification's own "such a payload is to be ignored"
 * rule. Octets of the addressed span that land on a read-only register
 * (EP_LEN, NR_CS, or a channel's own reserved octet) are left at their
 * current values while the rest of the span is still applied. Partially-
 * covered multi-octet registers are handled correctly: the write is
 * applied at octet granularity over the block's rendered image.
 *
 * A caller routing a decoded request here is responsible for having
 * checked that evt[2:0] really was 111b -- this endpoint type's other
 * decoders (rcp_ep_spi_decode_transfer_request()/_decode_response()) both
 * already reject evt values 6 and 7 as RCP_EP_SPI_ERR_BAD_CHANNEL, so a
 * misrouted request cannot reach either path by accident. */
rcp_ep_spi_reconfig_errc_t rcp_ep_spi_apply_reconfig(rcp_ep_spi_functional_cfg_t *cfg,
                                                      const uint8_t *payload,
                                                      size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_spi_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint16_t start_address, const uint8_t *data,
                                                size_t data_len, uint8_t transaction_num);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_SPI_OK               = 0,
    RCP_EP_SPI_ERR_SHORT_FRAME  = 1,
    RCP_EP_SPI_ERR_BAD_MSG_TYPE = 2,
    RCP_EP_SPI_ERR_WRONG_BUS    = 3,
    RCP_EP_SPI_ERR_WRONG_OP     = 4,
    RCP_EP_SPI_ERR_BAD_CHANNEL  = 5,
} rcp_ep_spi_errc_t;

/* Human-readable message for an rcp_ep_spi_errc_t value. Never returns NULL. */
const char *rcp_ep_spi_strerror(rcp_ep_spi_errc_t e);

/* ── Transfer request ──────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB transfer request addressed to byte_bus_id: evt's low
 * three bits carry channel (0..RCP_EP_SPI_MAX_CHANNELS-1; any other bits of
 * the ACF header's evt field are left 0), the payload is exactly
 * tx_data[0..tx_len) (the PICO-out bytes to shift out; tx_data may be NULL
 * iff tx_len == 0), and read_size carries the ACF header's own
 * read_size_or_segment_num field (TC18 §13.7.3.3's own read_size -- see
 * rcp_ep_spi_transfer_length()'s own doc comment for what it means for the
 * actual bus transfer length). Returns a zeroed rcp_bytes_t (data=NULL) if
 * tx_len exceeds RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller
 * frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_spi_encode_transfer_request(rcp_byte_bus_id_t byte_bus_id, uint8_t channel,
                                                const uint8_t *tx_data, size_t tx_len,
                                                uint16_t read_size, uint8_t transaction_num);

/* Decodes and validates an ACF-level SPI transfer request from b[0..len).
 * Fails with RCP_EP_SPI_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_SPI_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_SPI_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_SPI_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_READ; RCP_EP_SPI_ERR_BAD_CHANNEL if evt[2:0] is not
 * rcp_ep_spi_channel_valid(). On RCP_EP_SPI_OK, *out_channel,
 * *out_read_size, *out_transaction_num are populated, and *out_tx_data /
 * *out_tx_len are set to a *borrowed* view into b (not copied -- see the
 * file header) of the PICO-out payload. See rcp_ep_spi_transfer_length()
 * for combining *out_tx_len and *out_read_size into the actual bus
 * transfer length TC18 §13.7.3.3 requires. */
rcp_ep_spi_errc_t rcp_ep_spi_decode_transfer_request(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      uint8_t *out_channel,
                                                      const uint8_t **out_tx_data,
                                                      size_t *out_tx_len,
                                                      uint16_t *out_read_size,
                                                      uint8_t *out_transaction_num);

/* FIXED 2026-08-12 (issue #201, REQ-SPI-036): computes the actual SPI bus
 * transfer length in octets for a transfer request carrying tx_len bytes
 * of PICO-out payload and a read_size of read_size, per TC18 §13.7.3.3's
 * own rule: "The SPI EP shall append zeros in case the read_size is
 * larger than the number of bytes in the byte_msg_payload. The
 * byte_msg_payload will be presented on PICO in full, even if the
 * read_size is less than the number of bytes in the byte_msg_payload." A
 * caller driving real SPI hardware clocks exactly this many octets:
 * tx_data[0..tx_len) verbatim, followed by (return value - tx_len) zero
 * octets when read_size > tx_len; POCI is captured for the same length.
 * Equivalently, max(tx_len, read_size) -- but expressed as its own named
 * primitive (not inlined at each call site) both for readability and
 * because tx_len is a size_t while read_size is the ACF header's own
 * 12-bit-wide uint16_t, two different-width types a bare max() macro
 * would silently promote past their own domains' intent. */
size_t rcp_ep_spi_transfer_length(size_t tx_len, uint16_t read_size);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes an SPI response carrying rx_data[0..rx_len) (the POCI-in bytes
 * captured during the transfer; rx_data may be NULL iff rx_len == 0) as its
 * payload, with evt's low three bits carrying channel, echoing
 * transaction_num. Encoded as ACF_ABB when timed is false; as ACF_GBB (with
 * message_timestamp set to timestamp, mtv = RCP_ACF_MTV_VALID) when timed
 * is true -- see the file header. Returns a zeroed rcp_bytes_t (data=NULL)
 * if rx_len exceeds RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller
 * frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_spi_encode_response(rcp_byte_bus_id_t byte_bus_id, uint8_t channel,
                                        const uint8_t *rx_data, size_t rx_len,
                                        uint8_t transaction_num, bool timed, uint64_t timestamp);

/* Decodes an SPI response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoder
 * above, since a response's encoding depends on the responding endpoint's
 * own timed/untimed choice). Fails with RCP_EP_SPI_ERR_SHORT_FRAME (frame
 * too short for the applicable fixed header or its declared payload
 * length), RCP_EP_SPI_ERR_WRONG_BUS (byte_bus_id != expected_bus_id), or
 * RCP_EP_SPI_ERR_BAD_CHANNEL (evt[2:0] is not rcp_ep_spi_channel_valid()).
 * On RCP_EP_SPI_OK, *out_channel and *out_transaction_num are populated;
 * *out_rx_data / *out_rx_len are set to a *borrowed* view into b (not
 * copied) of the POCI-in payload; *out_timed and *out_timestamp report
 * whether the message was ACF_GBB with a valid (rcp_acf_gbb_is_timed())
 * timestamp, and that timestamp's value (0 when !*out_timed). */
rcp_ep_spi_errc_t rcp_ep_spi_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              uint8_t *out_channel,
                                              const uint8_t **out_rx_data, size_t *out_rx_len,
                                              bool *out_timed, uint64_t *out_timestamp,
                                              uint8_t *out_transaction_num);

/* ── SPI status-report width ─────────────────────────────────────────────── */

/* The largest status-report width this endpoint type's transfer-done status
 * may carry -- this module's own bound, matching extraction §4.6's "up to
 * 20 status bytes" ceiling. Not itself enforced by any encode/decode
 * function above (no status-report codec is in this milestone's scope);
 * provided so callers building that status-report representation size it
 * consistently. Unrelated to compound-wait's own comparison length, which
 * is driven by byte_msg_payload's length (see the file header) via acf.h's
 * rcp_acf_compound_wait_evt_valid()/_match(). */
#define RCP_EP_SPI_STATUS_MAX_LEN ((size_t)20u)

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_SPI_H */
