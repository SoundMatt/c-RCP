/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-LINEP-001
//cfusa:req REQ-LINEP-002
//cfusa:req REQ-LINEP-003
//cfusa:req REQ-LINEP-004
//cfusa:req REQ-LINEP-005
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
 * ── evt[2:0]: the response-generation comparison rule ───────────────────────
 *
 * The roadmap's own scope for this milestone requires that "a response
 * [be] generated when a received message's data matches the payload under
 * the evt[2:0] comparison rule" -- i.e. some received-bus-data comparison
 * mode, selected via the ACF byte_message_info header's evt field (acf.h;
 * a 4-bit field on the wire), governs whether this endpoint emits a
 * response to a command request at all. The roadmap names the mechanism
 * (an evt[2:0]-selected comparison) but does not itself enumerate which
 * comparison modes occupy those three bits -- the eight-value low-three-
 * bit selector below (rcp_ep_lin_compare_mode_t) is this module's own
 * original design filling that gap, modeled directly on ep_gpio.h's
 * existing evt[2:0]-as-eight-value-mode-selector precedent (there,
 * write-semantics; here, a response comparison rule) rather than on any
 * spec-derived enumeration -- there being no such enumeration cited by the
 * roadmap to derive one from.
 *
 * Four modes are given concrete behavior (rcp_ep_lin_compare_fires()):
 * RCP_EP_LIN_COMPARE_EXACT (received data length and content must equal
 * the outgoing request payload exactly), RCP_EP_LIN_COMPARE_PREFIX
 * (received data must begin with the request payload's bytes, but may be
 * longer), RCP_EP_LIN_COMPARE_ANY (any received bus message after the
 * request is driven satisfies the rule, content ignored -- e.g. a plain
 * transmit-and-capture-whatever-comes-back use), and
 * RCP_EP_LIN_COMPARE_NEVER (this command request never auto-generates a
 * response; a pure fire-and-forget transmit). The remaining four
 * (RCP_EP_LIN_COMPARE_RESERVED4..7) are documented no-ops -- treated
 * identically to _NEVER by rcp_ep_lin_compare_fires() -- this module's own
 * placeholder for evt[2:0] values not otherwise assigned meaning by this
 * milestone's scope, the same fail-safe treatment ep_gpio.h's own
 * RESERVED6 write-semantics value already established as this codebase's
 * convention for an unassigned low-bit-field value (never fabricate a
 * "true"/data-generating outcome for one).
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

/* ── evt[2:0]: the response-generation comparison rule ──────────────────────── */

typedef enum {
    RCP_EP_LIN_COMPARE_EXACT     = 0, /* received data equals the request
                                          payload exactly, length and content */
    RCP_EP_LIN_COMPARE_PREFIX    = 1, /* received data begins with the
                                          request payload's bytes */
    RCP_EP_LIN_COMPARE_ANY       = 2, /* any received message matches,
                                          content ignored */
    RCP_EP_LIN_COMPARE_NEVER     = 3, /* fire-and-forget: never auto-generates
                                          a response */
    RCP_EP_LIN_COMPARE_RESERVED4 = 4, /* documented no-op; see the file header */
    RCP_EP_LIN_COMPARE_RESERVED5 = 5,
    RCP_EP_LIN_COMPARE_RESERVED6 = 6,
    RCP_EP_LIN_COMPARE_RESERVED7 = 7,
} rcp_ep_lin_compare_mode_t;

/* True iff v (a raw evt[2:0] value as decoded off the wire) is one of the
 * eight defined comparison-mode values, i.e. v <= 7. */
bool rcp_ep_lin_compare_mode_valid(uint8_t v);

/* True iff a received-bus-data buffer rx_data[0..rx_len) satisfies mode
 * against the outgoing request's own request_data[0..request_len):
 * RCP_EP_LIN_COMPARE_EXACT iff rx_len == request_len and every byte
 * matches; RCP_EP_LIN_COMPARE_PREFIX iff rx_len >= request_len and the
 * leading request_len bytes of rx_data match request_data byte for byte;
 * RCP_EP_LIN_COMPARE_ANY always true (rx_data/rx_len are not inspected);
 * RCP_EP_LIN_COMPARE_NEVER and every RESERVED value always false -- this
 * module's fail-safe treatment of an unassigned evt[2:0] value, see the
 * file header. request_data/rx_data may be NULL iff their respective
 * length is 0. */
bool rcp_ep_lin_compare_fires(rcp_ep_lin_compare_mode_t mode, const uint8_t *request_data,
                               size_t request_len, const uint8_t *rx_data, size_t rx_len);

/* ── Transmission-done trigger ─────────────────────────────────────────────── */

typedef enum {
    RCP_EP_LIN_TRIGGER_NONE     = 0,
    RCP_EP_LIN_TRIGGER_TX_DONE  = 1,
} rcp_ep_lin_trigger_t;

/* True iff tx_done_event satisfies trigger: never for NONE; for TX_DONE
 * iff tx_done_event is true. */
bool rcp_ep_lin_trigger_fires(rcp_ep_lin_trigger_t trigger, bool tx_done_event);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint32_t                       lin_clk_divider; /* bit-time clock divider;
                                                         see the file header */
    uint8_t                        trigger;         /* rcp_ep_lin_trigger_t */
} rcp_ep_lin_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, lin_clk_divider 0,
 * trigger RCP_EP_LIN_TRIGGER_NONE). */
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

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_LIN_OK               = 0,
    RCP_EP_LIN_ERR_SHORT_FRAME  = 1,
    RCP_EP_LIN_ERR_BAD_MSG_TYPE = 2,
    RCP_EP_LIN_ERR_WRONG_BUS    = 3,
    RCP_EP_LIN_ERR_WRONG_OP     = 4,
} rcp_ep_lin_errc_t;

/* Human-readable message for an rcp_ep_lin_errc_t value. Never returns NULL. */
const char *rcp_ep_lin_strerror(rcp_ep_lin_errc_t e);

/* ── Command request ───────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB command request addressed to byte_bus_id: the payload
 * is exactly tx_data[0..tx_len), the raw bytes driven directly onto the
 * bus for this transaction -- every LIN-frame semantic (identifier/PID,
 * checksum, schedule position) already constructed into these bytes by the
 * caller, and never parsed or validated by this module (see the file
 * header). evt's low three bits carry compare_mode (any other bits of the
 * ACF header's evt field are left 0). tx_data may be NULL iff tx_len == 0.
 * Returns a zeroed rcp_bytes_t (data=NULL) if tx_len exceeds
 * RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_lin_encode_command_request(rcp_byte_bus_id_t byte_bus_id,
                                               const uint8_t *tx_data, size_t tx_len,
                                               rcp_ep_lin_compare_mode_t compare_mode,
                                               uint8_t transaction_num);

/* Decodes and validates an ACF-level LIN command request from b[0..len).
 * Fails with RCP_EP_LIN_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_LIN_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_LIN_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_LIN_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_READ. On RCP_EP_LIN_OK, *out_compare_mode (evt[2:0] of the
 * header's evt field; see rcp_ep_lin_compare_mode_valid()) and
 * *out_transaction_num are populated, and *out_tx_data / *out_tx_len are
 * set to a *borrowed* view into b (not copied -- see the file header) of
 * the raw outgoing payload. */
rcp_ep_lin_errc_t rcp_ep_lin_decode_command_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     const uint8_t **out_tx_data,
                                                     size_t *out_tx_len,
                                                     uint8_t *out_compare_mode,
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
