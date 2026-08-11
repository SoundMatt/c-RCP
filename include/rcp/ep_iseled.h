/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-ISELED-001
//cfusa:req REQ-ISELED-002
//cfusa:req REQ-ISELED-003
//cfusa:req REQ-ISELED-004
//cfusa:req REQ-ISELED-005
//cfusa:req REQ-ISELED-006
//cfusa:req REQ-ISELED-007
//cfusa:req REQ-ISELED-008
//cfusa:req REQ-ISELED-009
//cfusa:req REQ-ISELED-010
//cfusa:req REQ-ISELED-011
//cfusa:req REQ-ISELED-012
//cfusa:req REQ-ISELED-013
//cfusa:req REQ-ISELED-014
//cfusa:req REQ-ISELED-015
//cfusa:req REQ-ISELED-016
//cfusa:req REQ-ISELED-017
//cfusa:req REQ-ISELED-018
//cfusa:req REQ-ISELED-019
//cfusa:req REQ-ISELED-020
//cfusa:req REQ-ISELED-021
//cfusa:req REQ-ISELED-022
//cfusa:req REQ-ISELED-023
//cfusa:req REQ-ISELED-024

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-ISELED-025
//cfusa:req REQ-ISELED-026
//cfusa:req REQ-ISELED-027
//cfusa:req REQ-ISELED-028
//cfusa:req REQ-ISELED-029
/*
 * ep_iseled.h -- ISELED endpoint for the TC18 Remote Control Protocol wire
 * layer (ROADMAP.md Phase 19, "Remaining Endpoint Types", milestone 73).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (lifecycle.h/
 * lifecycle.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, lifecycle.h/
 * lifecycle.c, server.h/server.c, regmap.h/regmap.c, discovery.h/
 * discovery.c, or any prior endpoint file
 * (ep_gpio.h/.c, ep_spi.h/.c, ep_i2c.h/.c, ep_uart.h/.c, ep_pwm.h/.c,
 * ep_adc.h/.c, ep_lin.h/.c, ep_can.h/.c) is touched here -- the same
 * layering discipline every endpoint type since milestone 64 has
 * established, followed structurally throughout by this module too.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 * ISELED itself (Inova Semiconductor's addressable-LED interconnect) is a
 * separate, independently-documented industry protocol; the concrete
 * bit-framing and CRC choices below are this module's own original design,
 * not values taken from either that ecosystem's own specification or the
 * confidential TC18 extraction -- they exist to give this endpoint type a
 * concrete, testable behavior for the general capability the roadmap
 * names, not to reproduce any third party's exact wire format.
 *
 * ── Requirement-id naming note ──────────────────────────────────────────────
 *
 * Unlike ep_lin.h's REQ-LINEP-* (vs. linbr.c's pre-existing REQ-LIN-*) and
 * ep_can.h's REQ-CANEP-* (vs. canbr.c's pre-existing REQ-CAN-*), this
 * codebase has never carried a pre-replacement ISELED bridge/stub module of
 * any kind -- there is no REQ-ISELED-* prefix already claimed in
 * `.fusa-reqs.json` for this module's requirements to collide with.
 * Verified directly (`grep`) against `.fusa-reqs.json` before picking this
 * module's own prefix, this module's requirements are therefore tagged
 * plain `REQ-ISELED-*`, with no "-EP" collision-avoidance suffix needed --
 * there being nothing else to disambiguate from.
 *
 * ── Framing level ────────────────────────────────────────────────────────────
 *
 * As with every endpoint type before it, an ISELED command request/response
 * is ordinary endpoint traffic: whether it rides an NTSCF or TSCF AVTPDU is
 * a transport/scheduling choice made by the caller (avtp.h), not a property
 * of this endpoint itself. This module therefore operates at the ACF level
 * only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their decode
 * counterparts) -- a caller wraps (or unwraps) the frames this module
 * produces (or consumes) in NTSCF/TSCF using avtp.h directly.
 *
 * ── ACF-level payload: raw plain content, never pre-encoded symbols ────────
 *
 * A command request's ACF-level payload is the *raw, plain* instruction/
 * address/data content the caller wants sent to the ISELED chain -- not
 * pre-encoded ISELED symbols. This module never inspects, strips, or
 * reformats a byte of it at the ACF layer (the same "dumb pass-through for
 * the data content" convention ep_lin.h/ep_can.h already established for
 * their own raw-byte content): rcp_ep_iseled_encode_command_request()/
 * _decode_command_request() and their response counterparts move exactly
 * this plain content across the RCP wire, encoded as ACF_OP_WRITE/
 * ACF_OP_READ the same way every prior raw-byte-stream endpoint's own
 * request/response pair already does.
 *
 * The *native ISELED bit-framing* this module's own name promises --
 * turning that plain content into the endpoint's own 4-bit/5-bit symbol
 * stream for transmission over the physical two-wire ISP_P/ISP_N
 * differential pair -- is therefore a *separate* concern from the ACF
 * codec above, deliberately: it is this endpoint's own job to perform when
 * it actually drives the bus, not something a client ever constructs or
 * sees on the RCP wire. rcp_ep_iseled_symbol_encode()/_symbol_decode() and
 * rcp_ep_iseled_encode_bitframe()/_decode_bitframe() below model exactly
 * that job as a small, pure, independently-testable transform, the same
 * separation-of-concerns ep_can.h's own header draws between "ordinary
 * endpoint traffic" (this module's ACF codec) and "how CAN(FD/XL) might
 * one day carry the AVTPDU itself" (avtp.h, a different job entirely) --
 * here, the two jobs are "the ACF-level RCP exchange" and "the endpoint's
 * own physical-layer bit framing," never to be confused with one another.
 *
 * ── Native 4-bit/5-bit bit framing: an even-parity line code ────────────────
 *
 * Per this module's own design, each 4-bit data nibble is framed onto the
 * ISP_P/ISP_N pair as a 5-bit symbol: the low 4 bits carry the nibble
 * unchanged, and bit 4 carries that nibble's own even-parity bit
 * (rcp_ep_iseled_symbol_encode()). Decoding (rcp_ep_iseled_symbol_decode())
 * recomputes that same parity from the received low 4 bits and rejects any
 * symbol whose bit 4 disagrees -- a single-symbol integrity check intrinsic
 * to the physical bit-framing itself, independent of (and far narrower
 * than) either CRC layer discussed below. A full content buffer is framed
 * two symbols per octet (rcp_ep_iseled_encode_bitframe()/
 * _decode_bitframe()), high nibble first, one symbol value (0-31) stored
 * per output octet -- this module's own choice of a byte-addressable
 * symbol buffer rather than a tightly bit-packed stream, the same kind of
 * "this module's own wire-layout choice, favoring a straightforward
 * software representation over a hardware transceiver's exact bit-packed
 * timing" ep_uart.h's own bit-padding scheme already established as this
 * codebase's convention for representing a structured concept the spec
 * names but does not itself lay out bit-for-bit.
 *
 * ── ISELED-level CRC: a second, independent, optional integrity layer ──────
 *
 * `rcp_ep_iseled_functional_cfg_t.iseled_crc_enable` gates a second,
 * distinct integrity mechanism this module's own rcp_ep_iseled_crc8()
 * implements: a one-octet CRC (poly 0x07, init 0x00, no reflection -- a
 * standard, publicly documented small CRC, not derived from either the
 * confidential TC18 extraction or ISELED's own specification, chosen the
 * same way e2e.c's own file header already documents its CRC-16/
 * CCITT-FALSE choice as "a standard checksum algorithm," not a
 * spec-derived one) computed over the plain content and, when enabled,
 * appended as one extra trailing octet *before* bit-framing
 * (rcp_ep_iseled_encode_bitframe()'s own append_crc parameter) -- so the
 * trailer is itself carried inside the same parity-protected symbol
 * stream as the rest of the content, not a separately-framed field.
 *
 * This is **explicitly separate from, and additional to**, the Phase 18
 * (v0.70.0) RCP-level end-to-end integrity mechanism now living in e2e.c
 * (rcp_e2e_wrap()/_unwrap(), its own sequence-counter-plus-CRC header
 * wrapping an entire outer RCP payload for ISO 26262 Part 7 E2E
 * protection). The two must never be conflated: e2e.c's own wrap, if a
 * caller chooses to apply it, covers the whole ACF frame this module's own
 * codecs produce, from *outside* this module and with no knowledge of
 * ISELED at all; this module's own optional CRC-8 covers only the plain
 * ISELED instruction/address/data content itself, applied (if at all) only
 * at the point this endpoint's own bit-framing engine drives it onto the
 * physical bus. Both can be present on the same command at once, entirely
 * independently -- enabling `iseled_crc_enable` neither requires nor
 * precludes a caller separately choosing to wrap the same command's ACF
 * frame with e2e.c's own mechanism, and disabling one has no bearing on
 * the other. Anyone extending this file later who finds themselves
 * tempted to route this module's own CRC-8 through e2e.c (or vice versa)
 * should stop and re-read this paragraph first: it is this milestone's
 * deliberate two-independent-layers scope, not a duplication to be
 * "simplified" away.
 *
 * ── Recovered-clock mode: iseled_use_rcv_clk and the unwired ISP_N case ─────
 *
 * `rcp_ep_iseled_functional_cfg_t.iseled_use_rcv_clk` names TC18 §13.7.12.2
 * Table 55's own 0x0007.4 register bit directly: "Use clock provided by
 * ISELED 1st device instead of FreqSync pattern". CORRECTED 2026-08-10
 * (c-RCP-AUDIT-06, issue #256 Group G): this file previously described the
 * opposite polarity (claiming true meant *recovering* the clock via
 * Freq_Sync and needing no ISP_N wiring) -- backwards from what Table 55's
 * own bit description and §13.7.12.2's own prose both say. True selects
 * the *device*-provided clock (arriving on ISP_N, the "clock provided on
 * the ISP_N pin" §13.7.12.2 names); false selects the Freq_Sync pattern
 * instead, the one case where §13.7.12.2 says "it is not necessary to
 * connect the ISP_N of the EP to a physical Pin". ISP_N is therefore
 * required exactly when iseled_use_rcv_clk is true --
 * rcp_ep_iseled_requires_isp_n() is this module's own small, pure,
 * directly-testable statement of that invariant (true iff
 * iseled_use_rcv_clk is true), provided for any later pin-mapping
 * validation logic to consult rather than re-derive. The Freq_Sync
 * pattern's own clock recovery (the iseled_use_rcv_clk == false case) is
 * what the same even-parity bit framing above guarantees the transition
 * density for, since a symbol's all-zero and all-one nibble values (0x0
 * and 0xF) are given *different* parity bits and therefore never encode
 * to the same repeated bit pattern back to back. This mode governs
 * *receive* clock recovery only: `iseled_bit_clk_divider` remains
 * meaningful regardless of which clock source feeds reception, since this
 * endpoint's own outbound transmission is always locally clocked
 * regardless of how it derives a clock for what it receives back.
 *
 * ── The EP_func register block (evt[2:0] == 111b) ──────────────────────────
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-ISELED-026/027/
 * 029): rcp_ep_iseled_decode_command_request() already correctly rejected
 * evt[2:0] = 111b (RCP_EP_ISELED_ERR_BAD_EVT, via acf.h's
 * rcp_acf_evt_row2_is_plain()), but no counterpart implemented that TC18
 * §12.7.1 configuration-write path -- the same class of gap SPI's/I2C's/
 * UART's/LIN's/ADC's/PWM_IN's own earlier fixes closed.
 *
 * TC18 §13.7.12.2 Table 55 defines the register block, but -- like GPIO's,
 * I2C's, and PWM_OUT's own source tables -- with a genuine address-
 * collision editorial defect (visually confirmed on the PDF, not an
 * extraction artifact): iseled_base_clk (16 bit, R) is printed at relative
 * address 0x0001, one octet after iseled_ep_len, with no reserved octet at
 * 0x0001 the way every other endpoint type's own Table prints one -- so
 * iseled_base_clk's own two octets (0x0001-0x0002) collide with
 * iseled_ep_enable&clr, itself separately printed at 0x0002. Resolved via
 * the same cross-table structural precedent already used for
 * ep_pwm.h's/ep_gpio.h's/ep_i2c.h's own analogous defects: every endpoint
 * type's own common EP_func prefix places EP_LEN/reserved/enable&clr/
 * options/a 16-bit base_clk at the identical address sequence
 * (0x0000/0x0001/0x0002/0x0003/0x0004-0x0005), so iseled_base_clk moves to
 * 0x0004-0x0005, pushing every field the table lists after it (in the
 * table's own row order, each keeping its own width) down by three octets:
 * iseled_ep_status to 0x0006-0x0007, iseled_clk_divider to 0x0008, the
 * bitfield octet (iseled_collect_resp bit 3, iseled_use_rcv_clk bit 4) to
 * 0x0009, iseled_nr_leds to 0x000A-0x000B, iseled_rcv_timeout to
 * 0x000C-0x000D (RCP_EP_ISELED_EP_FUNC_LEN = 0x000E).
 *
 * rcp_ep_iseled_functional_cfg_t gains ep_status/wire_clk_divider/
 * collect_resp/nr_leds/rcv_timeout. `iseled_use_rcv_clk` is reused
 * directly for the register block's own 0x0009.4 bit -- it already names
 * that exact wire bit (see the "Recovered-clock mode" section above,
 * fixed for the correct polarity in Group G). `iseled_bit_clk_divider`
 * (uint32_t, unit-unspecified, this module's own outbound-transmission
 * clock choice -- see the "Bit-time clock" discussion elsewhere in this
 * header) is kept deliberately distinct from the new, uint8_t
 * `wire_clk_divider` field carrying the real 0x0008 wire register, per
 * this audit's established "don't silently redefine an existing field
 * whose own documented semantics diverge from the real wire register"
 * rule (SPI's baud_rate_kbps-vs-clock_divider split, UART's/LIN's own
 * equivalents). `iseled_crc_enable` is explicitly NOT part of this
 * register block -- it gates this module's own original, second,
 * independent CRC-8 integrity layer (see the "ISELED-level CRC" section
 * above), which Table 55 does not define a register for at all, so it is
 * never rendered onto or parsed from the wire here.
 *
 * New rcp_ep_iseled_render_registers()/_apply_reconfig()/
 * _reconfig_strerror()/_encode_reconfig_request() mirror the established
 * pattern exactly. REQ-ISELED-026 (iseled_collect_resp) and REQ-ISELED-027
 * (iseled_nr_leds/iseled_rcv_timeout) -- previously honest, not-implemented
 * struct-field gaps -- are now implemented; REQ-ISELED-025 (the
 * response-*aggregation behavior* §13.7.12.1 describes, i.e. actually
 * splitting a chain read across multiple ACF messages) is a distinct,
 * deeper runtime-behavior gap this batch does not close and remains
 * honestly not-implemented, same distinction Group I's own ADC batch drew
 * between REQ-ADC-035/036 (register modeling) and REQ-ADC-037 (cadence
 * orchestration behavior).
 *
 * ── Single trigger: transmission-complete ───────────────────────────────────
 *
 * rcp_ep_iseled_trigger_t names this endpoint type's one asynchronous-event
 * trigger mode (transmission-complete), plus NONE -- a single fixed
 * trigger, unlike ep_gpio.h's per-pin trigger table or ep_spi.h's
 * multi-value trigger table, and unlike ep_can.h's documented *absence* of
 * any trigger table at all (extraction §7's own gap for that endpoint
 * type). This endpoint's own trigger table is real but deliberately
 * narrow: exactly the one event a whole-command ISELED transmission over
 * the two-wire bus actually produces, the same kind of narrowing
 * ep_lin.h's own single TX_DONE trigger already established for a
 * commander-only serial push. rcp_ep_iseled_trigger_fires() is the pure,
 * directly-testable evaluation of that event against a selected trigger
 * mode.
 *
 * ── Functional configuration ─────────────────────────────────────────────────
 *
 * rcp_ep_iseled_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that
 * module's documented convention, same as every endpoint type before it)
 * and adds this endpoint's own four runtime-adjustable fields:
 * iseled_bit_clk_divider (this endpoint's own outbound bit-time clock
 * divider -- this module's own unit choice, a raw divider value rather
 * than a derived frequency, matching ep_lin.h's own lin_clk_divider field
 * shape), iseled_use_rcv_clk, iseled_crc_enable, and trigger
 * (rcp_ep_iseled_trigger_t). rcp_ep_iseled_functional_cfg_writable() is,
 * likewise, a thin, named wrapper over lifecycle.h's
 * rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W), and every
 * rcp_ep_iseled_set_*() mutator consults it before ever touching cfg --
 * reusing, never duplicating, lifecycle.h's/regmap.h's existing authorization
 * logic, per the roadmap's explicit instruction (the same rule every prior
 * endpoint type's own setters already follow).
 */
#ifndef RCP_EP_ISELED_H
#define RCP_EP_ISELED_H

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

/* ── Native 4-bit/5-bit bit framing ──────────────────────────────────────── */

/* Encodes nibble's low 4 bits into this module's own 5-bit even-parity
 * symbol (bit 4 = even parity of bits [3:0]; see the file header). Any
 * bits of nibble above bit 3 are ignored (masked with 0x0F first). Return
 * value is always in 0..31. */
uint8_t rcp_ep_iseled_symbol_encode(uint8_t nibble);

/* Decodes symbol (only bits [4:0] are inspected; higher bits are ignored)
 * back into a 4-bit nibble. Returns true and sets *out_nibble to bits
 * [3:0] of symbol iff bit 4 equals the even parity of bits [3:0] -- see
 * the file header. Returns false (leaving *out_nibble untouched) for any
 * symbol whose parity bit does not match, this module's own detection of
 * a corrupted or non-ISELED symbol on the physical bus. */
bool rcp_ep_iseled_symbol_decode(uint8_t symbol, uint8_t *out_nibble);

/* Number of framed octets rcp_ep_iseled_encode_bitframe() produces for
 * data_len octets of plain content, optionally with the one-octet CRC-8
 * trailer appended before framing: 2 * (data_len + (append_crc ? 1 : 0)).
 * Two symbols (one per nibble) per content octet -- see the file header. */
size_t rcp_ep_iseled_bitframe_encoded_len(size_t data_len, bool append_crc);

/* Bit-frames data[0..data_len) into a newly allocated symbol buffer of
 * rcp_ep_iseled_bitframe_encoded_len(data_len, append_crc) octets, each
 * holding one rcp_ep_iseled_symbol_encode() result (0-31) in its low 5
 * bits. When append_crc is true, rcp_ep_iseled_crc8(data, data_len) is
 * computed and framed as one extra trailing content octet before the two
 * symbols per octet expansion -- see the file header. High nibble framed
 * before low nibble for every content octet, including the trailing CRC
 * octet. data may be NULL iff data_len == 0. Returns a zeroed rcp_bytes_t
 * (data=NULL) if rcp_ep_iseled_bitframe_encoded_len() would be 0 (nothing
 * to frame) or on allocation failure. Caller frees the result with
 * rcp_bytes_free(). */
rcp_bytes_t rcp_ep_iseled_encode_bitframe(const uint8_t *data, size_t data_len, bool append_crc);

/* ── ISELED-level CRC (distinct from e2e.c; see the file header) ──────────── */

/* This module's own standard CRC-8 (poly 0x07, init 0x00, no input/output
 * reflection) over data[0..len) -- see the file header for why this is a
 * second, independent integrity layer from e2e.c's own CRC-16. data may
 * be NULL iff len == 0. */
uint8_t rcp_ep_iseled_crc8(const uint8_t *data, size_t len);

/* ── Recovered-clock mode ──────────────────────────────────────────────────── */

/* True iff the ISP_N pin must be wired/mapped for this endpoint to
 * operate -- true iff use_rcv_clk is true (device-provided clock, which
 * arrives on ISP_N; see the file header), false iff the Freq_Sync pattern
 * is used instead. */
bool rcp_ep_iseled_requires_isp_n(bool use_rcv_clk);

/* ── Transmission-complete trigger ─────────────────────────────────────────── */

typedef enum {
    RCP_EP_ISELED_TRIGGER_NONE          = 0,
    RCP_EP_ISELED_TRIGGER_TX_COMPLETE   = 1,
} rcp_ep_iseled_trigger_t;

/* True iff tx_complete_event satisfies trigger: never for NONE; for
 * TX_COMPLETE iff tx_complete_event is true. */
bool rcp_ep_iseled_trigger_fires(rcp_ep_iseled_trigger_t trigger, bool tx_complete_event);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint32_t                       iseled_bit_clk_divider; /* outbound
                                                                bit-time clock
                                                                divider; see
                                                                the file
                                                                header */
    bool                           iseled_use_rcv_clk;     /* recovered-clock
                                                                mode; see the
                                                                file header */
    bool                           iseled_crc_enable;       /* gates the
                                                                 native ISELED
                                                                 CRC-8 trailer;
                                                                 see the file
                                                                 header */
    uint8_t                        trigger;                 /* rcp_ep_iseled_trigger_t */
    uint16_t                       base_clk;         /* 0x0004, R   */
    uint16_t                       ep_status;        /* 0x0006, R/W */
    uint8_t                        wire_clk_divider; /* 0x0008, R/W; see the
                                                          file header */
    bool                           collect_resp;     /* 0x0009.3, R/W */
    uint16_t                       nr_leds;          /* 0x000A, R/W */
    uint16_t                       rcv_timeout;      /* 0x000C, R/W */
} rcp_ep_iseled_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; iseled_bit_clk_divider
 * 0; iseled_use_rcv_clk and iseled_crc_enable false; trigger
 * RCP_EP_ISELED_TRIGGER_NONE; every EP_func register 0). */
void rcp_ep_iseled_functional_cfg_init(rcp_ep_iseled_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (lifecycle.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_iseled_functional_cfg_writable(rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->iseled_bit_clk_divider to divider iff
 * rcp_ep_iseled_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_iseled_set_bit_clk_divider(rcp_ep_iseled_functional_cfg_t *cfg, uint32_t divider,
                                        rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_iseled_set_bit_clk_divider(), for
 * cfg->iseled_use_rcv_clk. */
bool rcp_ep_iseled_set_use_rcv_clk(rcp_ep_iseled_functional_cfg_t *cfg, bool use_rcv_clk,
                                    rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule, for cfg->iseled_crc_enable. */
bool rcp_ep_iseled_set_crc_enable(rcp_ep_iseled_functional_cfg_t *cfg, bool enable,
                                   rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule, for cfg->trigger. */
bool rcp_ep_iseled_set_trigger(rcp_ep_iseled_functional_cfg_t *cfg,
                                rcp_ep_iseled_trigger_t trigger, rcp_lifecycle_state_t state,
                                rcp_lifecycle_writer_ctx_t writer);

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

/* Relative octet offsets of the registers making up an ISELED endpoint's
 * own EP_func block, at the widths TC18 §13.7.12.2 Table 55 assigns them,
 * corrected for the address-collision editorial defect -- see the file
 * header. */
#define RCP_EP_ISELED_REG_EP_LEN        ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_ISELED_REG_RESERVED_01   ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_ISELED_REG_EP_ENABLE_CLR ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_ISELED_REG_EP_OPTIONS    ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_ISELED_REG_BASE_CLK      ((uint16_t)0x0004u) /* 16 bit, R   */
#define RCP_EP_ISELED_REG_EP_STATUS     ((uint16_t)0x0006u) /* 16 bit, R/W */
#define RCP_EP_ISELED_REG_CLK_DIVIDER   ((uint16_t)0x0008u) /*  8 bit, R/W */
#define RCP_EP_ISELED_REG_FLAGS         ((uint16_t)0x0009u) /*  8 bit, R/W */
#define RCP_EP_ISELED_REG_NR_LEDS       ((uint16_t)0x000Au) /* 16 bit, R/W */
#define RCP_EP_ISELED_REG_RCV_TIMEOUT   ((uint16_t)0x000Cu) /* 16 bit, R/W */

/* The block's own length in octets -- one past the last assigned offset. */
#define RCP_EP_ISELED_EP_FUNC_LEN       ((uint16_t)0x000Eu)

/* Bit masks within the RCP_EP_ISELED_REG_FLAGS octet -- Table 55's own two
 * named single-bit parameters (iseled_collect_resp, iseled_use_rcv_clk),
 * at the same relative bit positions the table's own row order assigns
 * (0x0007.3/0x0007.4 in the table's own uncorrected numbering, shifted to
 * 0x0009.3/0x0009.4 by the address-collision fix above); the remaining
 * bits are reserved and always read 0. */
#define RCP_EP_ISELED_FLAG_COLLECT_RESP ((uint8_t)(1u << 3))
#define RCP_EP_ISELED_FLAG_USE_RCV_CLK  ((uint8_t)(1u << 4))

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- see
 * RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN's own identical note (ep_pwm.h). */
#define RCP_EP_ISELED_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_ISELED_RECONFIG_OK               = 0,
    RCP_EP_ISELED_RECONFIG_ERR_SHORT        = 1, /* payload carries no
                                                      address prefix, or an
                                                      address prefix with no
                                                      data octet after it */
    RCP_EP_ISELED_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data
                                                      length exceeds
                                                      RCP_EP_ISELED_EP_FUNC_LEN
                                                      -- the whole write is
                                                      ignored, per the
                                                      specification's own
                                                      rule */
} rcp_ep_iseled_reconfig_errc_t;

/* Human-readable message for an rcp_ep_iseled_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_iseled_reconfig_strerror(rcp_ep_iseled_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into
 * out[0..RCP_EP_ISELED_EP_FUNC_LEN) exactly as a configuration *read* of
 * the whole block would report them -- the inverse of
 * rcp_ep_iseled_apply_reconfig()'s own parse step. iseled_crc_enable is
 * NOT part of this block (see the file header) and is never touched
 * here. */
void rcp_ep_iseled_render_registers(const rcp_ep_iseled_functional_cfg_t *cfg,
                                     uint8_t out[RCP_EP_ISELED_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is an
 * addressed write into this endpoint's own EP_func block -- a 16-bit
 * big-endian relative start address followed by the configuration data
 * octets to write from that address onward (extraction §3.7.1). See
 * rcp_ep_pwm_out_apply_reconfig()'s own doc comment (ep_pwm.h) for the
 * read-only-offset-skipping, octet-granularity-patch, and
 * out-of-range-ignores-the-whole-write rules -- identical here.
 *
 * A caller routing a decoded request here is responsible for having
 * checked that evt[2:0] really was 111b, e.g. via
 * !rcp_acf_evt_row2_is_plain(). */
rcp_ep_iseled_reconfig_errc_t
rcp_ep_iseled_apply_reconfig(rcp_ep_iseled_functional_cfg_t *cfg,
                              const uint8_t *payload, size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_iseled_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                   uint16_t start_address,
                                                   const uint8_t *data, size_t data_len,
                                                   uint8_t transaction_num);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_ISELED_OK                  = 0,
    RCP_EP_ISELED_ERR_SHORT_FRAME     = 1,
    RCP_EP_ISELED_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_ISELED_ERR_WRONG_BUS       = 3,
    RCP_EP_ISELED_ERR_WRONG_OP        = 4,
    RCP_EP_ISELED_ERR_BAD_SYMBOL      = 5,
    RCP_EP_ISELED_ERR_CRC_MISMATCH    = 6,
    RCP_EP_ISELED_ERR_ODD_SYMBOL_COUNT = 7,
    RCP_EP_ISELED_ERR_ALLOC           = 8,
    /* evt[2:0] is not 0b000, TC18 §13.5 Table 30's only legal value for a
     * plain (non-configuration) request in ISELED's endpoint-type row --
     * caller shall respond with error code UNSUPPORTED_CMD (see
     * rcp_acf_evt_row2_is_plain()). */
    RCP_EP_ISELED_ERR_BAD_EVT         = 9,
} rcp_ep_iseled_errc_t;

/* Human-readable message for an rcp_ep_iseled_errc_t value. Never returns NULL. */
const char *rcp_ep_iseled_strerror(rcp_ep_iseled_errc_t e);

/* ── Bit-frame decode ──────────────────────────────────────────────────────── */

/* Reverses rcp_ep_iseled_encode_bitframe(): decodes symbols[0..symbol_count)
 * back into plain content. Fails with RCP_EP_ISELED_ERR_ODD_SYMBOL_COUNT if
 * symbol_count is odd (every content octet frames to exactly two symbols);
 * RCP_EP_ISELED_ERR_SHORT_FRAME if expect_crc is true and symbol_count
 * yields fewer than one content octet (no room for the CRC trailer
 * itself); RCP_EP_ISELED_ERR_BAD_SYMBOL if any symbol fails
 * rcp_ep_iseled_symbol_decode(); RCP_EP_ISELED_ERR_CRC_MISMATCH if
 * expect_crc is true and the trailing octet does not equal
 * rcp_ep_iseled_crc8() of the preceding content; RCP_EP_ISELED_ERR_ALLOC
 * on allocation failure. On RCP_EP_ISELED_OK, *out_data is an owned
 * rcp_bytes_t (caller frees with rcp_bytes_free()) holding the decoded
 * plain content -- the trailing CRC octet is verified but *not* included
 * in *out_data when expect_crc is true. *out_data is zeroed on any
 * failure. */
rcp_ep_iseled_errc_t rcp_ep_iseled_decode_bitframe(const uint8_t *symbols, size_t symbol_count,
                                                    bool expect_crc, rcp_bytes_t *out_data);

/* ── Command request ───────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB command request addressed to byte_bus_id: the payload
 * is exactly tx_data[0..tx_len), the raw plain instruction/address/data
 * content -- never pre-encoded ISELED symbols, see the file header.
 * tx_data may be NULL iff tx_len == 0. Returns a zeroed rcp_bytes_t
 * (data=NULL) if tx_len exceeds RCP_ACF_MAX_PAYLOAD or on allocation
 * failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_iseled_encode_command_request(rcp_byte_bus_id_t byte_bus_id,
                                                  const uint8_t *tx_data, size_t tx_len,
                                                  uint8_t transaction_num);

/* Decodes and validates an ACF-level ISELED command request from
 * b[0..len). Fails with RCP_EP_ISELED_ERR_SHORT_FRAME if b is shorter than
 * the ACF_ABB fixed header or its declared payload length;
 * RCP_EP_ISELED_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_ISELED_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_ISELED_ERR_WRONG_OP if its op is not RCP_ACF_OP_WRITE;
 * RCP_EP_ISELED_ERR_BAD_EVT if its evt[2:0] is not 0b000
 * (rcp_acf_evt_row2_is_plain(), TC18 §13.5 Table 30 -- the caller shall
 * respond with error code UNSUPPORTED_CMD). On
 * RCP_EP_ISELED_OK, *out_transaction_num is populated, and *out_tx_data /
 * *out_tx_len are set to a *borrowed* view into b (not copied) of the raw
 * plain content. */
rcp_ep_iseled_errc_t rcp_ep_iseled_decode_command_request(const uint8_t *b, size_t len,
                                                            rcp_byte_bus_id_t expected_bus_id,
                                                            const uint8_t **out_tx_data,
                                                            size_t *out_tx_len,
                                                            uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes an ISELED response carrying rx_data[0..rx_len) (the raw plain
 * content received back from the ISELED chain; rx_data may be NULL iff
 * rx_len == 0) as its payload, echoing transaction_num. Encoded as
 * ACF_ABB when timed is false; as ACF_GBB (with message_timestamp set to
 * timestamp, mtv = RCP_ACF_MTV_VALID) when timed is true -- see every
 * prior endpoint type's own timed/untimed convention. Returns a zeroed
 * rcp_bytes_t (data=NULL) if rx_len exceeds RCP_ACF_MAX_PAYLOAD or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_iseled_encode_response(rcp_byte_bus_id_t byte_bus_id, const uint8_t *rx_data,
                                           size_t rx_len, uint8_t transaction_num, bool timed,
                                           uint64_t timestamp);

/* Decodes an ISELED response from either an ACF_ABB or ACF_GBB message
 * (this function peeks the ACF message type itself, unlike the request
 * decoder above, since a response's encoding depends on the responding
 * endpoint's own timed/untimed choice). Fails with
 * RCP_EP_ISELED_ERR_SHORT_FRAME (frame too short for the applicable fixed
 * header or its declared payload length) or RCP_EP_ISELED_ERR_WRONG_BUS
 * (byte_bus_id != expected_bus_id). On RCP_EP_ISELED_OK,
 * *out_transaction_num is populated; *out_rx_data / *out_rx_len are set to
 * a *borrowed* view into b (not copied) of the raw plain content;
 * *out_timed and *out_timestamp report whether the message was ACF_GBB
 * with a valid (rcp_acf_gbb_is_timed()) timestamp, and that timestamp's
 * value (0 when !*out_timed). */
rcp_ep_iseled_errc_t rcp_ep_iseled_decode_response(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    const uint8_t **out_rx_data,
                                                    size_t *out_rx_len, bool *out_timed,
                                                    uint64_t *out_timestamp,
                                                    uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_ISELED_H */
