/* SPDX-License-Identifier: MPL-2.0 */
/*
 * RELAY application interface adapter for c-RCP (RELAY spec §10.3, §18.2,
 * §15.7.5) -- REPLACE, ROADMAP.md Phase 21, "Satellite Package Rework",
 * milestone 84, "RELAY adapter rework".
 *
 * ── Why this is a REPLACE, not an ADAPT ──────────────────────────────────────
 *
 * RELAY's ToMessage()/FromMessage() convention (§15.7.5) assumes each
 * protocol adapter maps onto ONE generic request/response shape -- true of
 * the retired rcp_command_t/rcp_response_t/rcp_status_t/rcp_zone_t model
 * this file's own pre-TC18 version wrapped, but not of TC18: Phase 16-19
 * built 13 heterogeneous, independently fixed-shape endpoint types
 * (ep_gpio.h through ep_wakeup.h, plus discovery.h), each with its own
 * request/response struct and its own encode/decode function pair, and no
 * single generic "Command" choke point survives to interpose a wrapper on
 * (see ROADMAP.md's Protocol Replacement Notice). RELAY itself does not yet
 * define a per-endpoint-type translation convention to conform to -- this
 * module is this repository's own interim answer, scoped to c-RCP; the
 * upstream conversation about how RELAY's own spec should eventually define
 * that mapping generically is tracked as a GitHub issue against
 * SoundMatt/RELAY (this project's own "file issues, never edit other
 * repos" policy), not assumed resolved unilaterally here.
 *
 * ── The mapping's shape: a flat operation opcode, not one generic verb ──────
 *
 * rcp_adapt_op_t enumerates one entry per distinct wire operation this
 * mapping supports -- finer-grained than "endpoint type" where a type has
 * more than one operation shape (e.g. RCP_ADAPT_OP_GPIO_READ and
 * _GPIO_WRITE are both "GPIO" but pack/unpack completely different fields).
 * rcp_adapt_op_kind() reports which of the 13 endpoint-type families
 * (rcp_adapt_ep_kind_t) an op belongs to; rcp_adapt() binds a Caller to
 * exactly one family (and one byte_bus_id) at construction time -- mirroring
 * how one TC18 endpoint address is, physically, always exactly one fixed
 * type -- so a caller wanting to reach several endpoints constructs several
 * Callers, exactly as it would have needed several old-model Controllers to
 * reach several zones that disagreed about command shape.
 *
 * A relay_message_t bound for one of these Callers' send()/call() names its
 * specific op via meta["rcp.adapt.op"] (rcp_adapt_op_string()); send()/
 * call() fail with RCP_ADAPT_ERR_ENCODE if that op's rcp_adapt_op_kind()
 * doesn't match the Caller's own bound kind.
 *
 * ── Field table: how each op's request/response maps to relay_message_t ─────
 *
 * Every op reuses the same two conventions throughout, so only the
 * exceptions need calling out per row below:
 *   - msg->payload always carries the op's own natural wire-format data
 *     bytes: the raw tx/rx byte stream for the "any-length raw data"
 *     endpoint types (SPI/I2C/UART/LIN/CAN/ISELED/MDIO), or the fixed-width
 *     big-endian value for the "one scalar register" types (GPIO's 4-byte
 *     bitmask, ADC's 2-byte value, PWM_OUT/PWM_IN's 4-byte period+
 *     active_duration pair) -- i.e. exactly the bytes each ep_*.h module's
 *     own _encode_*()/_decode_*() functions themselves already treat as
 *     "the payload," never re-encoded into meta.
 *   - every other field (channel selectors, write-semantics evt, MDIO
 *     addressing, CAN frame_format/arbitration_id, ...) is a decimal string
 *     in meta, keyed "rcp.<kind>.<field>" (rekeying this file's old
 *     "rcp.priority"/"rcp.cmd_type" convention from zone/command-type to
 *     endpoint-type/field, per ROADMAP.md's own guidance to follow
 *     milestone 80's stream/endpoint/request-type precedent).
 *   - transaction_num, byte_bus_id, and (for RCP_ADAPT_OP_DISCOVERY) the
 *     requester's own stream_id are explicit function parameters, never
 *     meta -- they are addressing/correlation plumbing the vtable-level
 *     Caller (rcp_adapt()) self-manages, not per-request application data.
 *   - a response's meta always gains "rcp.timed" ("true"/"false") and, when
 *     timed, "rcp.timestamp" (decimal rcp_wallclock_ms()-domain), plus
 *     "rcp.transaction_num" (decimal, echoing the responding endpoint's own
 *     transaction_num) -- for every op except the two RCP_ADAPT_OP_WAKEUP_*
 *     ops (whose own ep_wakeup.h codec has no timed/GBB variant at all) and
 *     RCP_ADAPT_OP_DISCOVERY (whose own discovery.h codec has neither).
 *
 * | op                     | kind      | request extra fields                  | response extra fields         |
 * |-------------------------|-----------|----------------------------------------|--------------------------------|
 * | GPIO_READ               | GPIO      | (none)                                 | (none beyond payload)          |
 * | GPIO_WRITE               | GPIO      | meta rcp.gpio.evt (0-7, default 0)     | (none beyond payload)          |
 * | SPI_TRANSFER             | SPI       | meta rcp.spi.channel (0-7, default 0)  | meta rcp.spi.channel           |
 * | I2C_TRANSFER             | I2C       | (none)                                 | (none)                          |
 * | UART_WRITE                | UART      | (none)                                 | (none)                          |
 * | UART_READ                | UART      | meta rcp.uart.read_size (required)     | (none)                          |
 * | ADC_READ                  | ADC       | (none)                                 | (none beyond payload)           |
 * | PWM_OUT_READ              | PWM_OUT   | (none)                                 | (none beyond payload)           |
 * | PWM_OUT_WRITE             | PWM_OUT   | meta rcp.pwm.evt (0-7, default 0)      | (none beyond payload)           |
 * | PWM_IN_READ               | PWM_IN    | (none)                                 | (none beyond payload)           |
 * | LIN_COMMAND               | LIN       | meta rcp.lin.compare_mode (default 0)  | (none)                          |
 * | CAN_FRAME                  | CAN       | meta rcp.can.frame_format (required, out-of-scope for the 2 CAN XL values -- see below), rcp.can.arbitration_id (required) | meta rcp.can.frame_format, rcp.can.arbitration_id |
 * | ISELED_COMMAND             | ISELED    | (none)                                 | (none)                          |
 * | MDIO_READ                  | MDIO      | meta rcp.mdio.clause/prtad/devad/regad, rcp.mdio.word_count (all required) | meta rcp.mdio.clause/prtad/devad/regad |
 * | MDIO_WRITE                  | MDIO      | meta rcp.mdio.clause/prtad/devad/regad (required); payload = packed words (rcp_ep_mdio_word_encode() order) | meta rcp.mdio.clause/prtad/devad/regad |
 * | WAKEUP_SLEEPCMD              | WAKEUP    | meta rcp.wakeup.target_mode (required, rcp_pwrmode_t raw value) | meta rcp.wakeup.result (rcp_pwrmode_entry_result_t raw value); no timed/timestamp |
 * | WAKEUP_WAKEUP                | WAKEUP    | (none)                                 | (none); no timed/timestamp     |
 * | DISCOVERY                     | DISCOVERY | meta rcp.discovery.read_size (default RCP_DISCOVERY_GENERAL_SLICE_LEN) | meta rcp.discovery.magic/svr_version/vendor_id/device_id/svr_ep_count; msg->id = responding server's stream_id as lowercase hex (rcp_stream_id_to_u64()); no timed/timestamp/transaction_num |
 *
 * CAN_FRAME's two CAN XL frame formats (RCP_EP_CAN_FRAME_XL_CLASSICAL_PL/
 * _XL_NEW_PL) are out of this interim mapping's scope: ep_can.h's own
 * encoder already requires a non-NULL xl_header for those two formats and
 * this module never builds one, so packing naturally (not accidentally)
 * fails with RCP_ADAPT_ERR_ENCODE for them -- a caller needing CAN XL uses
 * ep_can.h directly, exactly as ROADMAP.md's own "documented, not silently
 * mishandled, scope boundary" convention elsewhere in this codebase (e.g.
 * ep_gpio.h's RCP_EP_GPIO_WRITE_RESERVED4) already establishes.
 *
 * ── No subscribe() equivalent ────────────────────────────────────────────────
 *
 * TC18 has no generic periodic Status-stream concept for this adapter to
 * forward (the same conclusion ROADMAP.md's own deadline.c/watchdog.c
 * REPLACE at milestone 79 already reached); rcp_adapt()'s returned Caller's
 * subscribe() therefore always fails with RCP_ADAPT_ERR_NOT_SUPPORTED. A
 * caller needing periodic updates polls the appropriate _READ op itself.
 *
 * ── Transport binding ────────────────────────────────────────────────────────
 *
 * There is no single concrete TC18 "connect to one endpoint" type this
 * module can wrap the way the old rcp_controller_t vtable let it -- but
 * avtp.h's rcp_avtp_transport_t (milestone 59) already is exactly the
 * "send/recv one already-framed AVTPDU" primitive this module needs
 * underneath its own per-op ACF/NTSCF framing, and shmem.h's
 * rcp_shmem_avtp_pair_new() (milestone 78) already provides an in-process
 * double for it -- reused here, not reinvented. rcp_adapt()'s send()
 * transmits without waiting for a reply (§10.6); call() transmits and then
 * blocks (subject to ctx) for exactly one reply frame -- a synchronous,
 * one-request-in-flight-at-a-time model, matching the old adapter's own
 * send-then-block-for-response shape.
 *
 * Usage:
 *   rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 8);
 *   rcp_stream_id_t local = rcp_stream_id_make(mac, 1);
 *   rcp_relay_caller_t *caller = rcp_adapt(t, local, 5, RCP_ADAPT_EP_GPIO);
 *   relay_context_t ctx = relay_context_with_timeout_ms(1000);
 *   relay_message_t req, resp = {0};
 *   relay_message_init(&req);
 *   relay_message_set_meta(&req, "rcp.adapt.op", "gpio_read");
 *   rcp_relay_caller_call(caller, &ctx, &req, &resp);
 *   relay_message_free(&req);
 *   relay_message_free(&resp);
 *   rcp_relay_caller_release(caller);  // also releases t
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_ADAPT_H
#define RCP_ADAPT_H

#include "relay/relay.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Operations ─────────────────────────────────────────────────────────── */

typedef enum {
    RCP_ADAPT_OP_GPIO_READ       = 0,
    RCP_ADAPT_OP_GPIO_WRITE      = 1,
    RCP_ADAPT_OP_SPI_TRANSFER    = 2,
    RCP_ADAPT_OP_I2C_TRANSFER    = 3,
    RCP_ADAPT_OP_UART_WRITE      = 4,
    RCP_ADAPT_OP_UART_READ       = 5,
    RCP_ADAPT_OP_ADC_READ        = 6,
    RCP_ADAPT_OP_PWM_OUT_READ    = 7,
    RCP_ADAPT_OP_PWM_OUT_WRITE   = 8,
    RCP_ADAPT_OP_PWM_IN_READ     = 9,
    RCP_ADAPT_OP_LIN_COMMAND     = 10,
    RCP_ADAPT_OP_CAN_FRAME       = 11,
    RCP_ADAPT_OP_ISELED_COMMAND  = 12,
    RCP_ADAPT_OP_MDIO_READ       = 13,
    RCP_ADAPT_OP_MDIO_WRITE      = 14,
    RCP_ADAPT_OP_WAKEUP_SLEEPCMD = 15,
    RCP_ADAPT_OP_WAKEUP_WAKEUP   = 16,
    RCP_ADAPT_OP_DISCOVERY       = 17,
} rcp_adapt_op_t;

/* The endpoint-type family (see the file header) op belongs to. */
typedef enum {
    RCP_ADAPT_EP_GPIO      = 0,
    RCP_ADAPT_EP_SPI       = 1,
    RCP_ADAPT_EP_I2C       = 2,
    RCP_ADAPT_EP_UART      = 3,
    RCP_ADAPT_EP_ADC       = 4,
    RCP_ADAPT_EP_PWM_OUT   = 5,
    RCP_ADAPT_EP_PWM_IN    = 6,
    RCP_ADAPT_EP_LIN       = 7,
    RCP_ADAPT_EP_CAN       = 8,
    RCP_ADAPT_EP_ISELED    = 9,
    RCP_ADAPT_EP_MDIO      = 10,
    RCP_ADAPT_EP_WAKEUP    = 11,
    RCP_ADAPT_EP_DISCOVERY = 12,
} rcp_adapt_ep_kind_t;

/* The endpoint-type family op belongs to -- see the file header's table. */
rcp_adapt_ep_kind_t rcp_adapt_op_kind(rcp_adapt_op_t op);

/* Canonical lower_snake_case name for op (e.g. "gpio_read"), used as the
 * meta["rcp.adapt.op"] value send()/call() read to select an operation.
 * Never returns NULL; returns "unknown" for a value outside rcp_adapt_op_t. */
const char *rcp_adapt_op_string(rcp_adapt_op_t op);

/* Parses name (as produced by rcp_adapt_op_string()) back to an
 * rcp_adapt_op_t. Returns false (leaving *out untouched) for NULL or any
 * unrecognized name. */
bool rcp_adapt_op_from_string(const char *name, rcp_adapt_op_t *out);

/* ── Error codes ────────────────────────────────────────────────────────── */

typedef enum {
    RCP_ADAPT_OK               = RCP_OK,          /* 0 */
    /* Same numeric values as rcp.h's own sentinels, so
     * rcp_errc_to_relay_errc() keeps classifying them correctly when a
     * caller passes one of these ints straight through. */
    RCP_ADAPT_ERR_CLOSED       = RCP_ERR_CLOSED,  /* 1 */
    RCP_ADAPT_ERR_TIMEOUT      = RCP_ERR_TIMEOUT, /* 4 */
    /* This module's own failure modes -- values chosen well outside
     * rcp_errc_t's own 0..8 range so a caller can never mistake one for an
     * rcp.h sentinel. */
    RCP_ADAPT_ERR_ENCODE        = 100, /* msg's payload/meta could not be
                                           packed into a valid request for op
                                           (missing/malformed meta, mismatched
                                           op/kind, oversized payload, an
                                           out-of-scope CAN XL frame format --
                                           see rcp_message_to_request()) */
    RCP_ADAPT_ERR_DECODE        = 101, /* the received frame did not decode as
                                           a valid response for op */
    RCP_ADAPT_ERR_TRANSPORT     = 102, /* the wrapped rcp_avtp_transport_t
                                           itself failed for a reason other
                                           than closed/timeout */
    RCP_ADAPT_ERR_NOT_SUPPORTED = 103, /* subscribe(): no native TC18
                                           periodic Status-equivalent stream
                                           exists -- see the file header */
} rcp_adapt_errc_t;

/* Human-readable message for an rcp_adapt_errc_t value. Never returns NULL. */
const char *rcp_adapt_strerror(rcp_adapt_errc_t e);

/* ── Message <-> wire mapping (interim per-endpoint-type §15.7.5 equivalent,
 *    see the file header's field table) ──────────────────────────────────── */

/* Packs msg into a request for op, addressed to byte_bus_id (ignored for
 * RCP_ADAPT_OP_DISCOVERY, which always addresses
 * RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID -- lifecycle.h) and self-identified as
 * transaction_num. For every op except RCP_ADAPT_OP_DISCOVERY the result is
 * an ACF-level frame -- a caller still wraps it in NTSCF/TSCF via avtp.h,
 * matching every ep_*.h module's own framing-left-to-the-caller convention.
 * RCP_ADAPT_OP_DISCOVERY instead produces a full NTSCF-framed AVTPDU
 * (discovery.h's own NTSCF-only convention), with requester_stream_id as
 * that frame's own sender-assigned stream_id (ignored for every other op).
 * Returns a zeroed rcp_bytes_t (data=NULL) and sets *out_err (if non-NULL)
 * to RCP_ADAPT_ERR_ENCODE on any packing failure. Caller frees a non-empty
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_message_to_request(rcp_adapt_op_t op, rcp_byte_bus_id_t byte_bus_id,
                                    rcp_stream_id_t requester_stream_id,
                                    const relay_message_t *msg, uint8_t transaction_num,
                                    rcp_adapt_errc_t *out_err);

/* Unpacks a response for op from b[0..len) into a relay_message_t --
 * b/len is an ACF-level frame for every op except RCP_ADAPT_OP_DISCOVERY,
 * which instead expects a full NTSCF-framed AVTPDU (see
 * rcp_message_to_request()); byte_bus_id is the address the response must
 * echo (ignored for RCP_ADAPT_OP_DISCOVERY). Returns a zeroed
 * relay_message_t and sets *out_err (if non-NULL) to RCP_ADAPT_ERR_DECODE on
 * any unpacking failure (short/malformed frame, wrong bus, wrong op/kind
 * mismatch). Caller owns a successfully-populated result and must
 * relay_message_free() it. */
relay_message_t rcp_response_to_message(rcp_adapt_op_t op, rcp_byte_bus_id_t byte_bus_id,
                                         const uint8_t *b, size_t len,
                                         rcp_adapt_errc_t *out_err);

/* ── Error wrapping (§5.2) ──────────────────────────────────────────────────
 *
 * Reports whether rcp_ec is equivalent to one of RELAY's four mandatory
 * common-error sentinels (§5.1), writing the corresponding relay_errc_t to
 * *out and returning true if so; returns false (leaving *out untouched) for
 * RCP_OK/RCP_ADAPT_OK or any condition with no RELAY equivalent, including
 * every RCP_ADAPT_ERR_* value this header adds beyond RCP_ADAPT_ERR_CLOSED/
 * _TIMEOUT. See rcp_adapt_errc_t's own doc comment for why
 * RCP_ADAPT_ERR_CLOSED/_TIMEOUT reuse rcp.h's numeric values rather than
 * defining their own -- this function's own switch is unaffected either
 * way, since it matches against rcp.h's RCP_ERR_CLOSED/RCP_ERR_TIMEOUT
 * literals directly. */
bool rcp_errc_to_relay_errc(int rcp_ec, relay_errc_t *out);

/* ── Adapt() (§10.3) ────────────────────────────────────────────────────────
 *
 * Wraps transport (taking a reference via rcp_avtp_transport_retain) as a
 * relay Caller bound to exactly one endpoint: byte_bus_id (ignored when kind
 * is RCP_ADAPT_EP_DISCOVERY) reached over transport, with local_stream_id as
 * the sender-assigned stream_id every outgoing request carries (see the
 * file header's field table).
 *
 * Every relay_message_t passed to the returned Caller's send()/call() must
 * name its op via meta["rcp.adapt.op"] (rcp_adapt_op_string()); send()/
 * call() fail with RCP_ADAPT_ERR_ENCODE if that name doesn't parse
 * (rcp_adapt_op_from_string()) or its rcp_adapt_op_kind() isn't kind.
 * subscribe() always fails with RCP_ADAPT_ERR_NOT_SUPPORTED -- see the file
 * header. close() closes the wrapped transport.
 *
 * Returned with refcount 1; release with rcp_relay_caller_release(), which
 * also releases the wrapped transport. Does NOT block, connect, or send any
 * traffic itself -- it wraps the given transport immediately. Returns NULL
 * on allocation failure.
 */
rcp_relay_caller_t *rcp_adapt(rcp_avtp_transport_t *transport, rcp_stream_id_t local_stream_id,
                               rcp_byte_bus_id_t byte_bus_id, rcp_adapt_ep_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ADAPT_H */
