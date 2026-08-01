/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-UART-001
//cfusa:req REQ-UART-002
//cfusa:req REQ-UART-003
//cfusa:req REQ-UART-004
//cfusa:req REQ-UART-005
//cfusa:req REQ-UART-006
//cfusa:req REQ-UART-007
//cfusa:req REQ-UART-008
//cfusa:req REQ-UART-009
//cfusa:req REQ-UART-010
//cfusa:req REQ-UART-011
//cfusa:req REQ-UART-012
//cfusa:req REQ-UART-013
//cfusa:req REQ-UART-014
//cfusa:req REQ-UART-015
//cfusa:req REQ-UART-016
//cfusa:req REQ-UART-017
//cfusa:req REQ-UART-018
//cfusa:req REQ-UART-019
//cfusa:req REQ-UART-020
//cfusa:req REQ-UART-021
//cfusa:req REQ-UART-022
//cfusa:req REQ-UART-023
//cfusa:req REQ-UART-024
//cfusa:req REQ-UART-025
//cfusa:req REQ-UART-026
//cfusa:req REQ-UART-027
//cfusa:req REQ-UART-028

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-UART-032
//cfusa:req REQ-UART-033
//cfusa:req REQ-UART-034
//cfusa:req REQ-UART-035
//cfusa:req REQ-UART-036
//cfusa:req REQ-UART-037
/*
 * ep_uart.h -- UART endpoint for the TC18 Remote Control Protocol wire
 * layer (ROADMAP.md Phase 16, "Basic Endpoints", milestone 66).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any prior endpoint file
 * (ep_gpio.h/.c, ep_spi.h/.c, ep_i2c.h/.c) is touched here -- the same
 * layering discipline those modules established, followed structurally
 * throughout by this module too.
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
 * As with ep_gpio.h/ep_spi.h/ep_i2c.h, UART request/response traffic is
 * ordinary endpoint traffic: whether it rides an NTSCF or TSCF frame is a
 * transport/scheduling choice made by the caller (avtp.h), not a property
 * of the UART endpoint itself. This module therefore operates at the ACF
 * level only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their decode
 * counterparts) -- a caller wraps (or unwraps) the frames this module
 * produces (or consumes) in NTSCF/TSCF using avtp.h directly.
 *
 * ── Independent TX/RX request families, one shared functional-config block ──
 *
 * Unlike ep_spi.h's/ep_i2c.h's single request/response pair per
 * transaction, this endpoint type models transmit and receive as two
 * independent request families -- rcp_ep_uart_encode_write_request()/
 * _decode_write_request() (TX: bytes to transmit) and
 * rcp_ep_uart_encode_read_request()/_decode_read_request() (RX: bytes
 * already received), each with its own response codec -- that a caller is
 * free to issue in either order or interleaved, unlike SPI's inherently
 * paired full-duplex transfer. Both directions nonetheless share exactly
 * one rcp_ep_uart_functional_cfg_t: baud rate, word format
 * (uart_nr_bits/parity/stop_bits), and the RX-specific ep_rx_buffer_size/
 * uart_timeout_ms fields all live in the same block, since they describe
 * one physical UART peripheral, not two.
 *
 * evt is always encoded/decoded as 0 for every request/response this
 * module produces or consumes -- this endpoint type, like ep_i2c.h, has no
 * channel selector.
 *
 * ── TX: raw bytes, echoed back on acceptance ────────────────────────────────
 *
 * A write (TX) request's payload is exactly the raw bytes to transmit,
 * already bit-padded by the caller if uart_nr_bits is not a multiple of 8
 * (see rcp_ep_uart_apply_bit_padding() below) -- this module never itself
 * pads or reformats a request's payload before encoding it, matching
 * ep_spi.h's/ep_i2c.h's convention of consuming already-classified,
 * caller-prepared inputs. The matching write response's payload is
 * whatever prefix of those bytes this endpoint actually accepted into its
 * TX path (all of them, in the ordinary case) -- both payloads are
 * *borrowed* views into the caller-supplied frame buffer on decode,
 * matching acf.c's own decode convention, for the same reason ep_spi.h's/
 * ep_i2c.h's own raw payloads are borrowed rather than copied.
 *
 * ── RX: read_size/uart_timeout race, and Phase 20 fragmentation ─────────────
 *
 * A read (RX) request carries no payload of its own -- see
 * rcp_ep_uart_decode_read_request()'s RCP_EP_UART_ERR_UNKNOWN_CMD case
 * below -- and instead carries its read_size in the ACF byte_message_info
 * header's own read_size_or_segment_num field (acf.h; already defined as
 * "read_size when op == RCP_ACF_OP_READ"), requesting up to that many
 * bytes. The real RC Server races that read_size against the endpoint's
 * uart_timeout_ms functional-config field (whichever completes the read
 * first), which can yield a response shorter than the requested
 * read_size -- a *short read*. rcp_ep_uart_encode_read_request()'s
 * read_size parameter is itself one octet wide, so this endpoint's own
 * largest possible read response (255 bytes) always fits comfortably
 * within a single AVTPDU regardless -- unlike ep_can.h's CAN XL captured
 * frames (up to 2054 bytes), this endpoint's own traffic never actually
 * needs fragment.h's ms/segment_num mechanism (Phase 20, ROADMAP.md
 * milestone 76) in real-world use. rcp_ep_uart_encode_read_response_fragmented()/
 * rcp_ep_uart_decode_read_response_fragment() below are nonetheless
 * provided -- retrofitted uniformly across every Phase 20 target endpoint,
 * per that milestone's own roadmap scope -- and are exercised end-to-end
 * in this module's own test suite against a deliberately small
 * max_fragment_payload, closing the deferred single-AVTPDU-worst-case
 * test milestone 66 left open, even though this endpoint's own read_size
 * width means the mechanism is never actually reachable from genuine
 * UART traffic. rcp_ep_uart_decode_read_response() (the plain,
 * unfragmented codec) is unchanged and remains the ordinary path for
 * every real read response.
 *
 * ── The payload-bearing-read-request rejection: a deliberate asymmetry ─────
 *
 * rcp_ep_uart_decode_read_request() rejects a read request that carries
 * any payload at all with RCP_EP_UART_ERR_UNKNOWN_CMD, treating such a
 * frame as an unrecognized command rather than, say, silently ignoring
 * the extra bytes or reinterpreting them. This is a deliberate asymmetry
 * against ep_gpio.h's write requests and the future PWM_OUT endpoint
 * (ROADMAP.md milestone 67), both of which *do* accept a payload on (some
 * of) their request types -- a UART read request has nothing meaningful a
 * payload could carry (read_size already rides the ACF header itself, per
 * above), so this module treats one arriving anyway as a protocol error
 * rather than tolerating it silently.
 *
 * ── Bit-padding for uart_nr_bits < 8 ─────────────────────────────────────────
 *
 * This module represents one UART word as one payload byte regardless of
 * uart_nr_bits (1..8) -- this module's own original wire-layout choice,
 * the specification itself not defining a byte-stream representation for
 * sub-byte word widths. rcp_ep_uart_apply_bit_padding() clears every bit
 * at or above position uart_nr_bits in each payload byte in place (a
 * no-op when uart_nr_bits == 8), and rcp_ep_uart_bit_pad_mask() is the
 * pure, directly-testable mask that operation applies -- callers use
 * these before encoding a write request's payload (or after decoding a
 * read response's payload) whenever uart_nr_bits < 8.
 *
 * ── Functional configuration ────────────────────────────────────────────────
 *
 * rcp_ep_uart_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that
 * module's documented convention, same as every endpoint type before it)
 * and adds baud_rate, uart_nr_bits/parity/stop_bits (the word format),
 * ep_rx_buffer_size (the RX FIFO's size in octets), and uart_timeout_ms
 * (the read-completion race's timeout half -- see above).
 * rcp_ep_uart_functional_cfg_writable() is, likewise, a thin, named
 * wrapper over server.h's rcp_lifecycle_field_writable()
 * (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W), and every rcp_ep_uart_set_*() mutator
 * consults it before ever touching cfg -- reusing, never duplicating,
 * server.h's/regmap.h's existing authorization logic, per the roadmap's
 * explicit instruction (the same rule every prior endpoint type's own
 * setters already follow).
 */
#ifndef RCP_EP_UART_H
#define RCP_EP_UART_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/fragment.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/lifecycle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Word format: data bits, parity, stop bits ──────────────────────────────── */

/* The narrowest and widest uart_nr_bits values this module's one-byte-
 * per-word wire representation can carry -- see the file header. */
#define RCP_EP_UART_NR_BITS_MIN ((uint8_t)1u)
#define RCP_EP_UART_NR_BITS_MAX ((uint8_t)8u)

/* True iff nr_bits is in RCP_EP_UART_NR_BITS_MIN..RCP_EP_UART_NR_BITS_MAX
 * (1..8) inclusive. */
bool rcp_ep_uart_nr_bits_valid(uint8_t nr_bits);

/* The bit-padding mask rcp_ep_uart_apply_bit_padding() applies to every
 * payload byte for a given nr_bits: (1u << nr_bits) - 1 for
 * rcp_ep_uart_nr_bits_valid(nr_bits), e.g. 0x7F for nr_bits == 7 and 0xFF
 * for nr_bits == 8. Returns 0 (fail-safe -- clears every bit -- mirroring
 * this project's convention of never fabricating data for undefined
 * input) for an nr_bits outside 1..8. */
uint8_t rcp_ep_uart_bit_pad_mask(uint8_t nr_bits);

/* Applies rcp_ep_uart_bit_pad_mask(nr_bits) to every byte of buf[0..len)
 * in place -- see the file header. buf may be NULL iff len == 0. A no-op
 * (every byte left unchanged) when nr_bits == 8; every byte is zeroed
 * when nr_bits is outside 1..8 (same fail-safe mask as
 * rcp_ep_uart_bit_pad_mask()). */
void rcp_ep_uart_apply_bit_padding(uint8_t *buf, size_t len, uint8_t nr_bits);

typedef enum {
    RCP_EP_UART_PARITY_NONE = 0,
    RCP_EP_UART_PARITY_ODD  = 1,
    RCP_EP_UART_PARITY_EVEN = 2,
} rcp_ep_uart_parity_t;

typedef enum {
    RCP_EP_UART_STOP_BITS_ONE = 0,
    RCP_EP_UART_STOP_BITS_TWO = 1,
} rcp_ep_uart_stop_bits_t;

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint32_t                       baud_rate;
    uint8_t                        uart_nr_bits; /* 1..8; see the file header */
    uint8_t                        parity;       /* rcp_ep_uart_parity_t */
    uint8_t                        stop_bits;    /* rcp_ep_uart_stop_bits_t */
    uint16_t                       ep_rx_buffer_size; /* RX FIFO size, octets */
    uint32_t                       uart_timeout_ms;   /* read-completion race
                                                           timeout -- see the
                                                           file header */
} rcp_ep_uart_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, baud_rate 0, parity
 * RCP_EP_UART_PARITY_NONE, stop_bits RCP_EP_UART_STOP_BITS_ONE,
 * ep_rx_buffer_size 0, uart_timeout_ms 0) -- except uart_nr_bits, which is
 * explicitly set to 8 (the only sane power-on default: 0 is not itself a
 * rcp_ep_uart_nr_bits_valid() value, unlike every other zero-valued field
 * above). */
void rcp_ep_uart_functional_cfg_init(rcp_ep_uart_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (server.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_uart_functional_cfg_writable(rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->baud_rate to baud_rate iff rcp_ep_uart_functional_cfg_writable()
 * authorizes the write for state/writer; returns whether the write was
 * applied. cfg is left entirely unchanged when it returns false. */
bool rcp_ep_uart_set_baud_rate(rcp_ep_uart_functional_cfg_t *cfg, uint32_t baud_rate,
                                rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->uart_nr_bits/parity/stop_bits together (one setter for all
 * three, since they are always reconfigured as a pack on the wire) iff
 * nr_bits is rcp_ep_uart_nr_bits_valid() and
 * rcp_ep_uart_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_uart_set_frame_format(rcp_ep_uart_functional_cfg_t *cfg, uint8_t nr_bits,
                                   rcp_ep_uart_parity_t parity, rcp_ep_uart_stop_bits_t stop_bits,
                                   rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_uart_set_baud_rate(), for
 * cfg->ep_rx_buffer_size. */
bool rcp_ep_uart_set_rx_buffer_size(rcp_ep_uart_functional_cfg_t *cfg, uint16_t rx_buffer_size,
                                     rcp_lifecycle_state_t state,
                                     rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_uart_set_baud_rate(), for
 * cfg->uart_timeout_ms. */
bool rcp_ep_uart_set_timeout(rcp_ep_uart_functional_cfg_t *cfg, uint32_t timeout_ms,
                              rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_UART_OK               = 0,
    RCP_EP_UART_ERR_SHORT_FRAME  = 1,
    RCP_EP_UART_ERR_BAD_MSG_TYPE = 2,
    RCP_EP_UART_ERR_WRONG_BUS    = 3,
    RCP_EP_UART_ERR_WRONG_OP     = 4,
    RCP_EP_UART_ERR_UNKNOWN_CMD  = 5, /* payload-bearing read request -- see
                                          the file header */
} rcp_ep_uart_errc_t;

/* Human-readable message for an rcp_ep_uart_errc_t value. Never returns NULL. */
const char *rcp_ep_uart_strerror(rcp_ep_uart_errc_t e);

/* ── TX: write request/response ────────────────────────────────────────────── */

/* Encodes an ACF_ABB write (TX) request addressed to byte_bus_id: the
 * payload is exactly tx_data[0..tx_len), the raw bytes to transmit
 * (already bit-padded by the caller if applicable -- see the file
 * header). tx_data may be NULL iff tx_len == 0. Returns a zeroed
 * rcp_bytes_t (data=NULL) if tx_len exceeds RCP_ACF_MAX_PAYLOAD or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_uart_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                              const uint8_t *tx_data, size_t tx_len,
                                              uint8_t transaction_num);

/* Decodes and validates an ACF-level UART write request from b[0..len).
 * Fails with RCP_EP_UART_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_UART_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_UART_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_UART_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_WRITE. On RCP_EP_UART_OK, *out_transaction_num is populated,
 * and *out_tx_data / *out_tx_len are set to a *borrowed* view into b (not
 * copied -- see the file header) of the raw outgoing payload. */
rcp_ep_uart_errc_t rcp_ep_uart_decode_write_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     const uint8_t **out_tx_data,
                                                     size_t *out_tx_len,
                                                     uint8_t *out_transaction_num);

/* Encodes a write (TX) response carrying accepted_data[0..accepted_len)
 * (the prefix of the original request's tx bytes this endpoint actually
 * accepted into its TX path; accepted_data may be NULL iff accepted_len
 * == 0) as its payload, echoing transaction_num. Encoded as ACF_ABB when
 * timed is false; as ACF_GBB (with message_timestamp set to timestamp,
 * mtv = RCP_ACF_MTV_VALID) when timed is true -- see the file header.
 * Returns a zeroed rcp_bytes_t (data=NULL) if accepted_len exceeds
 * RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_uart_encode_write_response(rcp_byte_bus_id_t byte_bus_id,
                                               const uint8_t *accepted_data, size_t accepted_len,
                                               uint8_t transaction_num, bool timed,
                                               uint64_t timestamp);

/* Decodes a write (TX) response from either an ACF_ABB or ACF_GBB message
 * (this function peeks the ACF message type itself, unlike the request
 * decoder above, since a response's encoding depends on the responding
 * endpoint's own timed/untimed choice). Fails with
 * RCP_EP_UART_ERR_SHORT_FRAME (frame too short for the applicable fixed
 * header or its declared payload length) or RCP_EP_UART_ERR_WRONG_BUS
 * (byte_bus_id != expected_bus_id). On RCP_EP_UART_OK,
 * *out_transaction_num is populated; *out_accepted_data /
 * *out_accepted_len are set to a *borrowed* view into b (not copied) of
 * the accepted payload; *out_timed and *out_timestamp report whether the
 * message was ACF_GBB with a valid (rcp_acf_gbb_is_timed()) timestamp,
 * and that timestamp's value (0 when !*out_timed). */
rcp_ep_uart_errc_t rcp_ep_uart_decode_write_response(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      const uint8_t **out_accepted_data,
                                                      size_t *out_accepted_len, bool *out_timed,
                                                      uint64_t *out_timestamp,
                                                      uint8_t *out_transaction_num);

/* ── RX: read request/response ─────────────────────────────────────────────── */

/* Encodes an ACF_ABB read (RX) request addressed to byte_bus_id, with no
 * payload: read_size rides the ACF byte_message_info header's own
 * read_size_or_segment_num field (acf.h) -- see the file header. */
rcp_bytes_t rcp_ep_uart_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint8_t read_size,
                                             uint8_t transaction_num);

/* Decodes and validates an ACF-level UART read request from b[0..len).
 * Fails with RCP_EP_UART_ERR_SHORT_FRAME if b is shorter than the
 * ACF_ABB fixed header or its declared payload length;
 * RCP_EP_UART_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_UART_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_UART_ERR_WRONG_OP if its op is not RCP_ACF_OP_READ;
 * RCP_EP_UART_ERR_UNKNOWN_CMD if it carries any payload at all -- the
 * deliberate asymmetry documented in the file header. On RCP_EP_UART_OK,
 * *out_read_size and *out_transaction_num are populated. */
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    uint8_t *out_read_size,
                                                    uint8_t *out_transaction_num);

/* Encodes a read (RX) response carrying rx_data[0..rx_len) (the bytes
 * actually received -- possibly fewer than the requesting read request's
 * read_size, i.e. a short read; rx_data may be NULL iff rx_len == 0) as
 * its payload, echoing transaction_num. Encoded as ACF_ABB when timed is
 * false; as ACF_GBB (with message_timestamp set to timestamp, mtv =
 * RCP_ACF_MTV_VALID) when timed is true -- see the file header. Returns a
 * zeroed rcp_bytes_t (data=NULL) if rx_len exceeds RCP_ACF_MAX_PAYLOAD or
 * on allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_uart_encode_read_response(rcp_byte_bus_id_t byte_bus_id,
                                              const uint8_t *rx_data, size_t rx_len,
                                              uint8_t transaction_num, bool timed,
                                              uint64_t timestamp);

/* Decodes a read (RX) response from either an ACF_ABB or ACF_GBB message
 * (peeked, same reasoning as rcp_ep_uart_decode_write_response()). Fails
 * with RCP_EP_UART_ERR_SHORT_FRAME (frame too short for the applicable
 * fixed header or its declared payload length) or
 * RCP_EP_UART_ERR_WRONG_BUS (byte_bus_id != expected_bus_id). On
 * RCP_EP_UART_OK, *out_transaction_num is populated; *out_rx_data /
 * *out_rx_len are set to a *borrowed* view into b (not copied) of the
 * received payload -- possibly shorter than the originating request's
 * read_size, i.e. a short read (see the file header); this endpoint's own
 * read_size width means a response never actually needs the
 * segment_num-based reassembly rcp_ep_uart_decode_read_response_fragment()
 * below provides for API-consistency with every other Phase 20 target
 * endpoint; *out_timed and *out_timestamp report whether the
 * message was ACF_GBB with a valid (rcp_acf_gbb_is_timed()) timestamp,
 * and that timestamp's value (0 when !*out_timed). */
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_response(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     const uint8_t **out_rx_data,
                                                     size_t *out_rx_len, bool *out_timed,
                                                     uint64_t *out_timestamp,
                                                     uint8_t *out_transaction_num);

/* ── Fragmented read response (Phase 20, fragment.h) ───────────────────────── */

/* The number of ACF frames rcp_ep_uart_encode_read_response_fragmented()
 * would produce for rx_len octets of read-response payload split into
 * fragments of at most max_fragment_payload octets each -- see
 * fragment.h's rcp_fragment_plan_count(). Provided for API consistency
 * across every Phase 20 target endpoint (see the file header); this
 * endpoint's own one-octet read_size means a real response is always
 * well under any plausible max_fragment_payload, so this virtually always
 * returns 1 in practice. */
size_t rcp_ep_uart_read_response_fragment_count(size_t rx_len, size_t max_fragment_payload);

/* Encodes a UART read (RX) response as one or more ACF frames, fragmenting
 * via fragment.h's ms/segment_num mechanism whenever rx_len exceeds
 * max_fragment_payload octets -- into
 * out_frames[0..rcp_ep_uart_read_response_fragment_count(rx_len,
 * max_fragment_payload)) (caller-allocated, sized by calling that
 * function first). Every fragment shares byte_bus_id/op(READ)/
 * transaction_num/timed/timestamp with rcp_ep_uart_encode_read_response();
 * only the ms flag, read_size_or_segment_num (meaningful only on an
 * ms=true fragment), and each fragment's own payload slice differ. When
 * rx_len already fits in one fragment, this produces exactly one frame
 * identical to what rcp_ep_uart_encode_read_response() itself would have.
 * Returns the number of frames written on success, or 0 (out_frames left
 * untouched) under the same conditions rcp_ep_uart_read_response_fragment_count()
 * returns 0 for, or on allocation failure partway through (any
 * already-written out_frames entries are freed before returning). Caller
 * frees each successfully returned out_frames[i] with rcp_bytes_free(). */
size_t rcp_ep_uart_encode_read_response_fragmented(rcp_byte_bus_id_t byte_bus_id,
                                                    const uint8_t *rx_data, size_t rx_len,
                                                    uint8_t transaction_num, bool timed,
                                                    uint64_t timestamp,
                                                    size_t max_fragment_payload,
                                                    rcp_bytes_t *out_frames);

/* Decodes one fragment of a (possibly multi-fragment) UART read response
 * from b[0..len) -- the same peek-message-type/byte_bus_id validation
 * rcp_ep_uart_decode_read_response() applies, but surfaces the fragment's
 * own ms bit and read_size_or_segment_num (as *out_segment_num,
 * meaningful only when *out_ms) alongside the raw payload slice
 * (*out_payload / *out_payload_len, borrowed into b), for a caller to
 * feed straight into a rcp_fragment_reassembler_t (fragment.h). Once
 * reassembly reports RCP_FRAGMENT_REASM_COMPLETE,
 * rcp_fragment_reassembler_get()'s output *is* the fully reassembled
 * rx_data directly -- unlike ep_can.h's fragmented response, this
 * endpoint's payload has no further internal structure of its own to
 * parse. Fails with the same RCP_EP_UART_ERR_SHORT_FRAME/
 * _ERR_BAD_MSG_TYPE/_ERR_WRONG_BUS conditions
 * rcp_ep_uart_decode_read_response() does. */
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_response_fragment(const uint8_t *b, size_t len,
                                                              rcp_byte_bus_id_t expected_bus_id,
                                                              bool *out_ms,
                                                              uint8_t *out_segment_num,
                                                              const uint8_t **out_payload,
                                                              size_t *out_payload_len,
                                                              bool *out_timed,
                                                              uint64_t *out_timestamp,
                                                              uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_UART_H */
