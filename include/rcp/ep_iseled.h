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
/*
 * ep_iseled.h -- ISELED endpoint for the TC18 Remote Control Protocol wire
 * layer (ROADMAP.md Phase 19, "Remaining Endpoint Types", milestone 73).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any prior endpoint file
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
 * `rcp_ep_iseled_functional_cfg_t.iseled_use_rcv_clk`, when true, puts this
 * endpoint into a mode where it recovers its own receive bit clock
 * directly from edges on the ISP_P/ISP_N signal itself rather than from a
 * locally divided clock -- the same even-parity bit framing above
 * guarantees the transition density such recovery needs, since a symbol's
 * all-zero and all-one nibble values (0x0 and 0xF) are given *different*
 * parity bits and therefore never encode to the same repeated bit pattern
 * back to back. In this mode the ISP_N pin does not need to be wired or
 * mapped in the hardware pin map (regmap.h's rcp_regmap_hw_pin_map_entry_t
 * table, untouched by this milestone) at all -- rcp_ep_iseled_requires_isp_n()
 * is this module's own small, pure, directly-testable statement of that
 * invariant (false iff iseled_use_rcv_clk is true), provided for any later
 * pin-mapping validation logic to consult rather than re-derive. This mode
 * governs *receive* clock recovery only: `iseled_bit_clk_divider` remains
 * meaningful in both modes, since this endpoint's own outbound
 * transmission is always locally clocked regardless of how it recovers a
 * clock for what it receives back.
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
 * likewise, a thin, named wrapper over server.h's
 * rcp_server_field_writable() (RCP_SERVER_FIELD_FUNCTIONAL_W), and every
 * rcp_ep_iseled_set_*() mutator consults it before ever touching cfg --
 * reusing, never duplicating, server.h's/regmap.h's existing authorization
 * logic, per the roadmap's explicit instruction (the same rule every prior
 * endpoint type's own setters already follow).
 */
#ifndef RCP_EP_ISELED_H
#define RCP_EP_ISELED_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/server.h"

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
 * operate -- false iff use_rcv_clk is true (recovered-clock mode; see the
 * file header), true otherwise. */
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
} rcp_ep_iseled_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; iseled_bit_clk_divider
 * 0; iseled_use_rcv_clk and iseled_crc_enable false; trigger
 * RCP_EP_ISELED_TRIGGER_NONE). */
void rcp_ep_iseled_functional_cfg_init(rcp_ep_iseled_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_server_field_writable()
 * (server.h) with kind RCP_SERVER_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_iseled_functional_cfg_writable(rcp_server_lifecycle_t state,
                                            rcp_server_writer_ctx_t writer);

/* Sets cfg->iseled_bit_clk_divider to divider iff
 * rcp_ep_iseled_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_iseled_set_bit_clk_divider(rcp_ep_iseled_functional_cfg_t *cfg, uint32_t divider,
                                        rcp_server_lifecycle_t state,
                                        rcp_server_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_iseled_set_bit_clk_divider(), for
 * cfg->iseled_use_rcv_clk. */
bool rcp_ep_iseled_set_use_rcv_clk(rcp_ep_iseled_functional_cfg_t *cfg, bool use_rcv_clk,
                                    rcp_server_lifecycle_t state, rcp_server_writer_ctx_t writer);

/* Same authorization rule, for cfg->iseled_crc_enable. */
bool rcp_ep_iseled_set_crc_enable(rcp_ep_iseled_functional_cfg_t *cfg, bool enable,
                                   rcp_server_lifecycle_t state, rcp_server_writer_ctx_t writer);

/* Same authorization rule, for cfg->trigger. */
bool rcp_ep_iseled_set_trigger(rcp_ep_iseled_functional_cfg_t *cfg,
                                rcp_ep_iseled_trigger_t trigger, rcp_server_lifecycle_t state,
                                rcp_server_writer_ctx_t writer);

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
 * RCP_EP_ISELED_ERR_WRONG_OP if its op is not RCP_ACF_OP_WRITE. On
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
