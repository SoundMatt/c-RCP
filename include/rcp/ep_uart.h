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
//cfusa:req REQ-UART-046
//cfusa:req REQ-UART-047
//cfusa:req REQ-UART-048
//cfusa:req REQ-UART-049
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
 * read_size -- a *short read*.
 *
 * CORRECTED 2026-08-12 (issue #201, REQ-UART-034): rcp_ep_uart_encode_
 * read_request()'s read_size parameter is now the ACF header's own full
 * 12-bit width (0-4095, uint16_t) rather than one octet -- this was
 * previously narrowed to uint8_t, on the reasoning that this endpoint's
 * largest possible read response (255 bytes) always fit comfortably
 * within a single AVTPDU regardless, so fragment.h's ms/segment_num
 * mechanism (Phase 20, ROADMAP.md milestone 76) was never actually
 * reachable from genuine UART traffic. That reasoning does not survive
 * TC18 §13.7.8.1's own text, which explicitly contemplates a read_size
 * larger than uart_rx_fifo_size as the THIRD read-completion trigger
 * (alongside read_size-satisfied and uart_timeout expiry), driving a
 * fragmented response via exactly the mechanism this module already
 * provides below -- a conforming peer requesting more than 255 bytes was
 * simply inexpressible before this fix, not merely unreachable in
 * "real-world use." rcp_ep_uart_encode_read_response_fragmented()/
 * rcp_ep_uart_decode_read_response_fragment() below are unchanged by
 * this fix -- retrofitted uniformly across every Phase 20 target
 * endpoint, per that milestone's own roadmap scope, and exercised
 * end-to-end in this module's own test suite against a deliberately
 * small max_fragment_payload -- but ARE now genuinely reachable from a
 * request this module can itself originate, not merely a defensive
 * provision for an unreachable case. rcp_ep_uart_decode_read_response()
 * (the plain, unfragmented codec) is unchanged and remains the ordinary
 * path for every real read response.
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
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-UART-038):
 * the four TC18 §13.7.8.2 Table 51 R/W fields this struct previously had
 * no counterpart for at all -- uart_rts_enable/uart_cts_enable (hardware
 * RTS/CTS flow control), uart_half_duplex (full- vs. half-duplex
 * operation), and uart_trail (inter-transmission trail time in bit
 * times) -- are now modelled (rts_enable/cts_enable/half_duplex/trail),
 * alongside the whole Table 51 register block itself (see "The EP_func
 * register block" below). Unlike GPIO's/I2C's own source tables, Table
 * 48 is internally consistent -- no address-collision editorial defect
 * to resolve here.
 * rcp_ep_uart_functional_cfg_writable() is, likewise, a thin, named
 * wrapper over server.h's rcp_lifecycle_field_writable()
 * (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W), and every rcp_ep_uart_set_*() mutator
 * consults it before ever touching cfg -- reusing, never duplicating,
 * server.h's/regmap.h's existing authorization logic, per the roadmap's
 * explicit instruction (the same rule every prior endpoint type's own
 * setters already follow).
 *
 * ── The EP_func register block (evt[2:0] == 111b) ──────────────────────────
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-UART-038):
 * rcp_ep_uart_decode_write_request()/_decode_read_request() already
 * correctly reject evt[2:0] = 111b as RCP_EP_UART_ERR_BAD_EVT (via acf.h's
 * shared rcp_acf_evt_row2_is_plain()) -- exactly right, since a
 * config-write request is not a plain transfer. What was missing, the
 * same class of gap SPI's and I2C's own earlier fixes closed, was any
 * counterpart implementing that §12.7.1 path: this endpoint's Table 51
 * register block had no wire render/parse path of any kind.
 *
 * Table 51's own layout (unlike GPIO's/I2C's own source tables, this one
 * has no address-collision editorial defect -- its printed addresses are
 * internally consistent throughout):
 *
 *   0x0000  uart_ep_len       8 bit  R    RCP_EP_UART_EP_FUNC_LEN (0x0D)
 *   0x0001  Reserved          8 bit  R    reads 0x00
 *   0x0002  uart_ep_enable&clr 8 bit R/W  Table 35 common entries
 *   0x0003  uart_ep_options   8 bit  R/W* Table 35 common entries
 *   0x0004  uart_ep_status   16 bit  R/W
 *   0x0006  uart_baud_rate   16 bit  R/W  kbit/s
 *   0x0008  uart_nr_bits      8 bit  R/W  number of data bits
 *   0x0009  bit 0 uart_parity_enable, bit 1 uart_parity_pol, bit 2
 *           uart_rts_enable, bit 3 uart_cts_enable, bit 4
 *           uart_half_duplex, bits 5-7 reserved      8 bit  R/W
 *   0x000A  uart_stop_bits    8 bit  R/W  in HALF stop bits (Table 51's
 *           own units: 1 stop bit = 2, 2 stop bits = 4)
 *   0x000B  uart_timeout      8 bit  R/W  receiver timeout, bit times
 *   0x000C  uart_trail        8 bit  R/W  inter-transmission trail time,
 *           bit times
 *
 * Three fields render/parse through a genuinely different representation
 * than this module's own pre-existing, differently-scoped fields of a
 * similar name -- kept as new, separate struct fields rather than
 * reinterpreting the existing ones, the same "don't silently redefine an
 * existing public field's meaning" caution SPI's own baud_rate_kbps-vs-
 * clock_divider split already established:
 *
 *   - baud_rate_kbps (new, 16 bit, kbit/s) is the wire's own uart_baud_rate
 *     register; the pre-existing baud_rate (uint32_t, unit unspecified in
 *     this module's own original design, predating this fix) is left
 *     untouched and is not itself derived from or written to by the
 *     register block.
 *   - wire_timeout_bit_times (new, 8 bit) is the wire's own uart_timeout
 *     register -- a receiver idle-timeout measured in UART bit periods,
 *     genuinely distinct from the pre-existing uart_timeout_ms (a
 *     wall-clock read-completion race timeout at a different layer
 *     entirely -- see this file header's own RX section above). Neither
 *     field is derived from the other.
 *   - stop_bits (rcp_ep_uart_stop_bits_t, now ONE/ONE_HALF/TWO -- see
 *     REQ-UART-037's own CLOSED note below) round-trips through
 *     uart_stop_bits's half-stop-bit units exactly: render emits 2 for
 *     ONE, 3 for ONE_HALF, 4 for TWO; parse maps register value 2 to
 *     ONE, 3 to ONE_HALF, and anything >= 4 (or < 2) to TWO/ONE
 *     respectively, matching this codebase's general fail-safe
 *     convention of not silently under-specifying a timing margin for
 *     an out-of-range register value.
 *
 *     CLOSED 2026-08-14 (tc18-gap post-backlog audit, REQ-UART-037):
 *     ONE_HALF (wire value 3, 1.5 stop bits) is now a real, third,
 *     separately-representable enum member -- previously this enum had
 *     exactly two members (0/1, one and two whole stop bits), so 1.5
 *     stop bits passed the setter unvalidated and round-tripped through
 *     the wire as the DIFFERENT value TWO on parse, an honestly-
 *     documented but real lossy conflation. No existing call site in
 *     this codebase used a `switch` over this enum (grep-confirmed
 *     before adding the new member), so this addition is source-
 *     compatible everywhere it was already used by direct comparison or
 *     assignment.
 *
 * uart_nr_bits (0x0008) maps directly, byte-for-byte, onto the
 * pre-existing uart_nr_bits field -- both are literally "the number of
 * data bits", no scaling needed.
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
    RCP_EP_UART_STOP_BITS_ONE      = 0,
    RCP_EP_UART_STOP_BITS_TWO      = 1,
    /* REQ-UART-037 (tc18-gap post-backlog audit, 2026-08-14): 1.5 stop
     * bits -- Table 51's own uart_stop_bits register value 3 (half-
     * stop-bit units). Appended rather than inserted, so TWO's own
     * existing numeric value (1) is unchanged for any code that stored
     * it as a raw integer before this fix. */
    RCP_EP_UART_STOP_BITS_ONE_HALF = 2,
} rcp_ep_uart_stop_bits_t;

/* ── HW trigger signals (§13.7.8.4 Table 52) ─────────────────────────────────
 *
 * ADDED 2026-08-14 (c-RCP-AUDIT-14, issue #425): TC18 §13.7.8.4 Table 52
 * ("uart trigger signals") defines a real, spec-numbered HW trigger event
 * this module previously modeled nowhere at all -- two output signals,
 * "0: Transmit request finalized" and "1: Read request finalized". This
 * is the same class of concept as ep_spi.h's own per-channel
 * rcp_ep_spi_trigger_t/rcp_ep_spi_trigger_fires() (its TRANSFER_DONE
 * signal, Table 41) -- the reference shape copied here -- and NOT
 * ep_lin.h's rcp_ep_lin_trigger_t, which that file's own header explains
 * has no TC18 basis whatsoever (an entirely original design filling a gap
 * TC18 leaves silent). UART is the opposite case: a genuine, numbered
 * spec table this module had simply never implemented. rcp_ep_uart_trigger_t
 * names Table 52's two signals (RCP_EP_UART_TRIGGER_TX_FINALIZED = signal
 * 0, RCP_EP_UART_TRIGGER_RX_FINALIZED = signal 1) plus a NONE member for
 * "no trigger selected", which this codebase's own enum convention always
 * puts at ordinal 0 (see e.g. rcp_ep_spi_trigger_t's own NONE). CORRECTED
 * 2026-08-14 (c-RCP-AUDIT-28, issue #449): that NONE member means the C
 * enum's own ordinals do NOT directly equal Table 52's signal numbers --
 * RCP_EP_UART_TRIGGER_TX_FINALIZED == 1 (Table 52 signal 0),
 * RCP_EP_UART_TRIGGER_RX_FINALIZED == 2 (Table 52 signal 1), an off-by-one
 * versus the table caused purely by NONE occupying slot 0. This is a
 * documentation-only correction: nothing in this module today renders
 * rcp_ep_uart_trigger_t's ordinal onto the wire (see the next paragraph),
 * so no code depended on the false "already Table 52-numbered" claim this
 * comment previously made. A future wire-rendering of this field WOULD
 * need an explicit ordinal -> signal-number mapping function first,
 * matching the pattern ep_spi.h's own rcp_ep_spi_trigger_signal_number()
 * already uses for SPI's own per-channel trigger field -- do not assume
 * the raw enum value is Table 52-safe to emit directly.
 *
 * rcp_ep_uart_trigger_fires() is the pure, directly-testable evaluation of
 * a caller-classified event
 * against a selected trigger mode -- the same caller-supplies-already-
 * classified-inputs convention every other endpoint type's own trigger-
 * evaluation function already uses (rcp_ep_spi_trigger_fires(),
 * rcp_ep_lin_trigger_fires(), rcp_ep_pwm_out_trigger_fires()/
 * rcp_ep_pwm_in_trigger_fires()).
 *
 * cfg->trigger (rcp_ep_uart_trigger_t, below) is, like ep_spi.h's own
 * channels[i].trigger and ep_pwm.h's PWM_OUT/PWM_IN trigger fields, never
 * rendered onto the wire: Table 51's own EP_func register block (see "The
 * EP_func register block" below) has no trigger-mode register of any
 * kind, the same "no wire-format consequence" status those sibling
 * fields' own file headers already document. rcp_ep_uart_render_registers()/
 * rcp_ep_uart_apply_reconfig() are therefore intentionally left untouched
 * by this addition, matching that established sibling pattern exactly
 * rather than inventing a register Table 51 does not define.
 */

typedef enum {
    RCP_EP_UART_TRIGGER_NONE         = 0,
    RCP_EP_UART_TRIGGER_TX_FINALIZED = 1, /* Table 52 signal 0: "Transmit
                                              request finalized" */
    RCP_EP_UART_TRIGGER_RX_FINALIZED = 2, /* Table 52 signal 1: "Read
                                              request finalized" */
} rcp_ep_uart_trigger_t;

/* The two asynchronous events a UART endpoint's trigger mode may be
 * evaluated against -- see rcp_ep_uart_trigger_fires(). */
typedef enum {
    RCP_EP_UART_EVENT_TX_REQUEST_FINALIZED   = 0,
    RCP_EP_UART_EVENT_READ_REQUEST_FINALIZED = 1,
} rcp_ep_uart_event_t;

/* True iff event satisfies trigger: never for NONE; for TX_FINALIZED iff
 * event == RCP_EP_UART_EVENT_TX_REQUEST_FINALIZED; for RX_FINALIZED iff
 * event == RCP_EP_UART_EVENT_READ_REQUEST_FINALIZED -- TC18 §13.7.8.4
 * Table 52's own two HW trigger signals, verbatim. */
bool rcp_ep_uart_trigger_fires(rcp_ep_uart_trigger_t trigger, rcp_ep_uart_event_t event);

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
    uint16_t                       ep_status;         /* uart_ep_status, Table 51 */
    uint16_t                       baud_rate_kbps;    /* uart_baud_rate, Table 51 --
                                                           see the file header */
    bool                           rts_enable;        /* uart_rts_enable, Table 51 */
    bool                           cts_enable;        /* uart_cts_enable, Table 51 */
    bool                           half_duplex;       /* uart_half_duplex, Table 51 */
    uint8_t                        wire_timeout_bit_times; /* uart_timeout, Table 51 --
                                                                see the file header */
    uint8_t                        trail;             /* uart_trail, Table 51 */
    uint8_t                        trigger;           /* rcp_ep_uart_trigger_t;
                                                            this module's own field,
                                                            not part of the EP_func
                                                            block -- see the file
                                                            header's "HW trigger
                                                            signals" section */
} rcp_ep_uart_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, baud_rate 0, parity
 * RCP_EP_UART_PARITY_NONE, stop_bits RCP_EP_UART_STOP_BITS_ONE,
 * ep_rx_buffer_size 0, uart_timeout_ms 0, ep_status/baud_rate_kbps/
 * wire_timeout_bit_times/trail 0, rts_enable/cts_enable/half_duplex
 * false, trigger RCP_EP_UART_TRIGGER_NONE) -- except uart_nr_bits, which
 * is explicitly set to 8 (the only sane power-on default: 0 is not itself
 * a rcp_ep_uart_nr_bits_valid() value, unlike every other zero-valued
 * field above). */
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

/* Same authorization rule as rcp_ep_uart_set_baud_rate(), for cfg->trigger
 * -- see the file header's "HW trigger signals" section. Never touches
 * the EP_func register block (this field has no wire counterpart). */
bool rcp_ep_uart_set_trigger(rcp_ep_uart_functional_cfg_t *cfg, rcp_ep_uart_trigger_t trigger,
                              rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* ── The EP_func register block (the evt[2:0] == 111b target) ──────────────── */

/* Relative octet offsets of the registers making up this endpoint's own
 * EP_func block -- see the file header. Every multi-octet register is
 * big-endian, like every other multi-octet field this codebase encodes.
 * Offsets marked R are read-only: a configuration write covering them
 * leaves them unchanged (see rcp_ep_uart_apply_reconfig()). */
#define RCP_EP_UART_REG_EP_LEN        ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_UART_REG_RESERVED_01   ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_UART_REG_EP_ENABLE_CLR ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_UART_REG_EP_OPTIONS    ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_UART_REG_EP_STATUS     ((uint16_t)0x0004u) /* 16 bit, R/W */
#define RCP_EP_UART_REG_BAUD_RATE     ((uint16_t)0x0006u) /* 16 bit, R/W */
#define RCP_EP_UART_REG_NR_BITS       ((uint16_t)0x0008u) /*  8 bit, R/W */
#define RCP_EP_UART_REG_FLAGS         ((uint16_t)0x0009u) /*  8 bit, R/W --
                                                               parity_enable(0)/
                                                               parity_pol(1)/
                                                               rts_enable(2)/
                                                               cts_enable(3)/
                                                               half_duplex(4),
                                                               bits 5-7
                                                               reserved */
#define RCP_EP_UART_REG_STOP_BITS     ((uint16_t)0x000Au) /*  8 bit, R/W --
                                                               half stop bits */
#define RCP_EP_UART_REG_TIMEOUT       ((uint16_t)0x000Bu) /*  8 bit, R/W --
                                                               bit times */
#define RCP_EP_UART_REG_TRAIL         ((uint16_t)0x000Cu) /*  8 bit, R/W --
                                                               bit times */

/* Bit masks within RCP_EP_UART_REG_FLAGS. */
#define RCP_EP_UART_FLAG_PARITY_ENABLE ((uint8_t)(1u << 0))
#define RCP_EP_UART_FLAG_PARITY_POL    ((uint8_t)(1u << 1))
#define RCP_EP_UART_FLAG_RTS_ENABLE    ((uint8_t)(1u << 2))
#define RCP_EP_UART_FLAG_CTS_ENABLE    ((uint8_t)(1u << 3))
#define RCP_EP_UART_FLAG_HALF_DUPLEX   ((uint8_t)(1u << 4))

/* The block's own length in octets -- one past the last assigned offset,
 * i.e. the value the endpoint reports at RCP_EP_UART_REG_EP_LEN and the
 * bound the "write beyond EP_LEN is ignored" rule (§12.7.1) is applied
 * against. Table 51's own addressing is internally consistent (unlike
 * GPIO's/I2C's own source tables), so there is no editorial defect to
 * resolve here. */
#define RCP_EP_UART_EP_FUNC_LEN ((uint16_t)0x000Du)

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- the address is a 16-bit
 * big-endian field, followed by the configuration data octets to write
 * from that address onward (§12.7.1). */
#define RCP_EP_UART_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_UART_RECONFIG_OK               = 0,
    RCP_EP_UART_RECONFIG_ERR_SHORT        = 1, /* payload carries no address
                                                   prefix, or an address
                                                   prefix with no data octet
                                                   after it */
    RCP_EP_UART_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data length
                                                   exceeds
                                                   RCP_EP_UART_EP_FUNC_LEN --
                                                   the whole write is ignored,
                                                   per the specification's own
                                                   rule */
} rcp_ep_uart_reconfig_errc_t;

/* Human-readable message for an rcp_ep_uart_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_uart_reconfig_strerror(rcp_ep_uart_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_UART_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_uart_apply_reconfig()'s own parse step, and
 * the same rendering that function patches in place. parity_enable/
 * parity_pol are derived from cfg->parity; uart_stop_bits is derived from
 * cfg->stop_bits via the half-stop-bit mapping documented in the file
 * header. */
void rcp_ep_uart_render_registers(const rcp_ep_uart_functional_cfg_t *cfg,
                                   uint8_t out[RCP_EP_UART_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is NOT
 * presented at the interface but interpreted as an addressed write into
 * this endpoint's own EP_func block -- a 16-bit big-endian relative start
 * address followed by the configuration data octets to write from that
 * address onward (§12.7.1). This is a real register write, reaching every
 * R/W register the block defines (enable/options, status, baud rate,
 * word format, flow-control flags, stop bits, timeout, trail), not merely
 * the fields this module's own setters already exposed.
 *
 * Returns RCP_EP_UART_RECONFIG_ERR_SHORT when payload_len is not at least
 * RCP_EP_UART_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_UART_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would
 * extend past RCP_EP_UART_EP_FUNC_LEN; in both cases cfg is left entirely
 * unchanged, per the specification's own "such a payload is to be ignored"
 * rule. Octets of the addressed span that land on a read-only register
 * (EP_LEN or the reserved octet) are left at their current values while
 * the rest of the span is still applied.
 *
 * A caller routing a decoded request here is responsible for having
 * checked that evt[2:0] really was 111b -- rcp_ep_uart_decode_write_request()/
 * _decode_read_request() both already reject it (RCP_EP_UART_ERR_BAD_EVT)
 * so a misrouted request cannot reach either path by accident. */
rcp_ep_uart_reconfig_errc_t rcp_ep_uart_apply_reconfig(rcp_ep_uart_functional_cfg_t *cfg,
                                                        const uint8_t *payload,
                                                        size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_uart_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                 uint16_t start_address, const uint8_t *data,
                                                 size_t data_len, uint8_t transaction_num);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_UART_OK               = 0,
    RCP_EP_UART_ERR_SHORT_FRAME  = 1,
    RCP_EP_UART_ERR_BAD_MSG_TYPE = 2,
    RCP_EP_UART_ERR_WRONG_BUS    = 3,
    RCP_EP_UART_ERR_WRONG_OP     = 4,
    RCP_EP_UART_ERR_UNKNOWN_CMD  = 5, /* payload-bearing read request -- see
                                          the file header */
    /* evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
     * plain (non-configuration) request in UART's endpoint-type row --
     * caller shall respond with error code UNSUPPORTED_CMD (see
     * rcp_acf_evt_row2_is_plain()). */
    RCP_EP_UART_ERR_BAD_EVT      = 6,
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
 * RCP_ACF_OP_WRITE; RCP_EP_UART_ERR_BAD_EVT if its evt[2:0] is not 0b000
 * (rcp_acf_evt_row2_is_plain(), TC18 §13.5 Table 33 -- the caller shall
 * respond with error code UNSUPPORTED_CMD). On RCP_EP_UART_OK,
 * *out_transaction_num is populated,
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
 * read_size_or_segment_num field (acf.h) -- see the file header.
 *
 * FIXED 2026-08-12 (issue #201, REQ-UART-034): read_size widened from
 * uint8_t to uint16_t -- the ACF header's own read_size_or_segment_num
 * field is 12 bits wide (0-4095, acf.h), and TC18 §13.7.8.1 explicitly
 * contemplates a read_size larger than uart_rx_fifo_size driving a
 * fragmented response (rcp_ep_uart_encode_read_response_fragmented(),
 * above) -- a case this module could not even originate a request for
 * while read_size stayed truncated to 8 bits (0-255). Values above 4095
 * are the caller's own responsibility to avoid; this function does not
 * itself validate the range (matching every other endpoint's own
 * read_size parameter, e.g. rcp_ep_adc_encode_read_request()). */
rcp_bytes_t rcp_ep_uart_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint16_t read_size,
                                             uint8_t transaction_num);

/* Decodes and validates an ACF-level UART read request from b[0..len).
 * Fails with RCP_EP_UART_ERR_SHORT_FRAME if b is shorter than the
 * ACF_ABB fixed header or its declared payload length;
 * RCP_EP_UART_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_UART_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_UART_ERR_WRONG_OP if its op is not RCP_ACF_OP_READ;
 * RCP_EP_UART_ERR_UNKNOWN_CMD if it carries any payload at all -- the
 * deliberate asymmetry documented in the file header; RCP_EP_UART_ERR_BAD_EVT
 * if its evt[2:0] is not 0b000 (rcp_acf_evt_row2_is_plain(), TC18 §13.5
 * Table 33 -- the caller shall respond with error code UNSUPPORTED_CMD).
 * On RCP_EP_UART_OK,
 * *out_read_size and *out_transaction_num are populated.
 *
 * FIXED 2026-08-12 (issue #201, REQ-UART-034): *out_read_size widened
 * from uint8_t to uint16_t -- see the encode side's own doc comment,
 * above, for why. */
rcp_ep_uart_errc_t rcp_ep_uart_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    uint16_t *out_read_size,
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

/* ── Read-completion arbitration (REQ-UART-033) ──────────────────────────────
 *
 * TC18 §13.7.8.1's own three read-completion triggers, verbatim: "A read
 * request will be started as soon as the fifo-rx-buffer is filled with as
 * many bytes as requested in the read_size or when the uart_timeout has
 * expired or in case the read_size is larger than the uart_rx_fifo_size,
 * when the fifo is full. In the latter case the response is fragmented."
 * This module already supplies all three ingredients (read_size in the ACF
 * header, cfg->uart_timeout_ms, and rcp_ep_uart_encode_read_response_
 * fragmented()) but previously never arbitrated between them -- the choice
 * was left entirely to an unspecified caller, so two conforming c-RCP-based
 * servers could answer identical read requests with materially different
 * response cadence and fragmentation (the exact deviation REQ-UART-033
 * used to pin). rcp_ep_uart_read_completion_decision() is that arbitration,
 * as a pure, directly-testable function over caller-tracked counters --
 * this module still does not itself own a real FIFO or a clock, matching
 * every other caller-driven primitive in this codebase (e.g. ep_spi.h's
 * rcp_ep_spi_transfer_length()) rather than inventing timer/buffer state
 * this protocol-codec library has no business owning. */
typedef enum {
    RCP_EP_UART_READ_NOT_YET_COMPLETE   = 0, /* keep waiting */
    RCP_EP_UART_READ_RESPOND_NORMAL     = 1, /* emit a normal (possibly
                                                 short) response now */
    RCP_EP_UART_READ_RESPOND_FRAGMENTED = 2, /* emit via
                                                 rcp_ep_uart_encode_read_
                                                 response_fragmented() now */
} rcp_ep_uart_read_completion_t;

/* REQ-UART-037's own remaining Table 51 divergence (issue #341 lineage):
 * cfg->wire_timeout_bit_times (uart_timeout, Table 51 -- TC18's real
 * register, correctly wire-modeled by rcp_ep_uart_render_registers()/
 * _apply_reconfig() since REQ-UART-038) is expressed in raw UART bit
 * periods, "measured from the last received stop bit" -- a runtime
 * origin (WHEN the countdown starts), not itself a duration this
 * function converts. rcp_ep_uart_read_completion_decision() below, by
 * contrast, has only ever consulted the separate, differently-scoped
 * uart_timeout_ms (this module's own pre-existing, unit-unspecified
 * wall-clock field) -- nothing anywhere in this codebase converted the
 * REAL wire register's own bit-time count into a wall-clock duration a
 * caller could actually use. This function is that conversion: one UART
 * bit period is 1000/baud_rate_kbps microseconds (baud_rate_kbps is
 * cfg->baud_rate_kbps, the SAME correctly-unit'd Table 51 field render/
 * apply_reconfig already use for the wire's own uart_baud_rate
 * register), so wire_timeout_bit_times bit periods is
 * wire_timeout_bit_times * 1000 / baud_rate_kbps microseconds --
 * rounded UP (ceiling), never down, so a caller relying on this value
 * never waits LESS than TC18's own configured timeout actually means
 * (the same "never underestimate a safety-relevant duration" discipline
 * REQ-ADC-033's own tolerance handling already establishes).
 *
 * Fails open (returns 0) when baud_rate_kbps == 0 -- this library never
 * invents a clock rate it has no way to know, the same "this library
 * never invents a value it has no way to know" discipline REQ-ADC-033's
 * own base_clk_hz parameter and REQ-SRV-018's own source_ep parameter
 * already establish; a caller with no configured baud rate cannot derive
 * a real duration and must fall back to its own choice (mirroring
 * uart_timeout_ms's own "0 means no configured interval" convention
 * elsewhere in this module). wire_timeout_bit_times == 0 naturally
 * converts to 0 through the same formula, with no special-casing needed
 * -- consistent with rcp_ep_uart_read_completion_decision()'s own
 * documented "uart_timeout_ms == 0 completes immediately" reading.
 *
 * Does not itself read cfg -- takes baud_rate_kbps/wire_timeout_bit_times
 * as plain parameters, the same explicit-inputs convention
 * rcp_ep_uart_read_completion_decision() below already uses, so a
 * caller may derive the duration for any two values without needing a
 * live rcp_ep_uart_functional_cfg_t on hand. */
uint32_t rcp_ep_uart_wire_timeout_us(uint16_t baud_rate_kbps, uint8_t wire_timeout_bit_times);

/* Decides which of the three TC18 §13.7.8.1 triggers, if any, has fired for
 * a read request in progress: bytes_available is the caller-tracked count
 * currently held in the fifo-rx-buffer; read_size is the request's own
 * requested byte count; elapsed_ms is wall-clock time since the request
 * began; uart_timeout_ms/rx_fifo_size are cfg->uart_timeout_ms/
 * cfg->ep_rx_buffer_size (passed explicitly rather than as a struct
 * pointer, since neither is mutated and a caller may be tracking several
 * in-flight reads against one shared cfg). Checked in the spec's own
 * stated order: the fragmentation trigger (read_size exceeds rx_fifo_size,
 * AND the fifo has actually filled to capacity) is checked first, since it
 * is the more specific condition -- the read_size-satisfied trigger can
 * never itself fire when read_size > rx_fifo_size (bytes_available cannot
 * exceed rx_fifo_size), so the ordering only matters for documentation
 * clarity, not correctness. elapsed_ms >= uart_timeout_ms with
 * uart_timeout_ms == 0 completes immediately (no waiting), matching "as
 * soon as... the uart_timeout has expired" read literally for a
 * zero-length timeout. */
rcp_ep_uart_read_completion_t rcp_ep_uart_read_completion_decision(
    uint16_t bytes_available, uint16_t read_size, uint32_t elapsed_ms,
    uint32_t uart_timeout_ms, uint16_t rx_fifo_size);

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
