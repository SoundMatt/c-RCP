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
//cfusa:req REQ-SPI-031
//cfusa:req REQ-SPI-032

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
 * timing delays (nanoseconds; this module's own unit choice). Composing
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
 * ── The SPI compound-wait truncation rule ───────────────────────────────────
 *
 * A future compound-wait request (generic compound-wait plumbing itself
 * lands at Phase 17 milestone 69) that targets an SPI endpoint compares
 * only the first RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN (4) of up to
 * RCP_EP_SPI_STATUS_MAX_LEN (20) status bytes (extraction §4.6) -- a
 * property of this endpoint type itself, not of the compound-wait
 * mechanism, and therefore implemented and unit-tested here, now, via
 * rcp_ep_spi_compound_wait_status_equal(): a raw, milestone-69-independent
 * comparison-mode helper that milestone 67's PWM_IN numeric compound-wait
 * comparison modes are explicitly told (by the roadmap) to follow the
 * precedent of.
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
} rcp_ep_spi_channel_cfg_t;

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    rcp_ep_spi_channel_cfg_t       channels[RCP_EP_SPI_MAX_CHANNELS];
} rcp_ep_spi_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; every channel's mode
 * MODE_0, bit_order MSB_FIRST, cs_polarity ACTIVE_LOW, trigger NONE,
 * clock_divider/inter_byte_delay_ns/inter_transfer_delay_ns all 0). */
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
 * the ACF header's evt field are left 0), and the payload is exactly
 * tx_data[0..tx_len) (the PICO-out bytes to shift out; tx_data may be NULL
 * iff tx_len == 0). Returns a zeroed rcp_bytes_t (data=NULL) if tx_len
 * exceeds RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_spi_encode_transfer_request(rcp_byte_bus_id_t byte_bus_id, uint8_t channel,
                                                const uint8_t *tx_data, size_t tx_len,
                                                uint8_t transaction_num);

/* Decodes and validates an ACF-level SPI transfer request from b[0..len).
 * Fails with RCP_EP_SPI_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_SPI_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_SPI_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_SPI_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_READ; RCP_EP_SPI_ERR_BAD_CHANNEL if evt[2:0] is not
 * rcp_ep_spi_channel_valid(). On RCP_EP_SPI_OK, *out_channel,
 * *out_transaction_num are populated, and *out_tx_data / *out_tx_len are
 * set to a *borrowed* view into b (not copied -- see the file header) of
 * the PICO-out payload. */
rcp_ep_spi_errc_t rcp_ep_spi_decode_transfer_request(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      uint8_t *out_channel,
                                                      const uint8_t **out_tx_data,
                                                      size_t *out_tx_len,
                                                      uint8_t *out_transaction_num);

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

/* ── Compound-wait truncation rule (see the file header) ───────────────────── */

/* The largest status-report width this endpoint type's transfer-done status
 * may carry -- this module's own bound, matching extraction §4.6's "up to
 * 20 status bytes" ceiling for the compound-wait comparison
 * rcp_ep_spi_compound_wait_status_equal() below services. Not itself
 * enforced by any encode/decode function above (no status-report codec is
 * in this milestone's scope); provided so callers building that
 * status-report representation size it consistently with the truncation
 * rule that will apply to it. */
#define RCP_EP_SPI_STATUS_MAX_LEN ((size_t)20u)

/* The number of leading status bytes a compound-wait request compares
 * against an SPI endpoint, regardless of how much of the up-to-
 * RCP_EP_SPI_STATUS_MAX_LEN-byte status report is actually present -- see
 * the file header. */
#define RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN ((size_t)4u)

/* Raw comparison-mode helper for a future compound-wait request (Phase 17
 * milestone 69): true iff the first RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN
 * (4) bytes of status equal the first RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN
 * bytes of target, byte for byte; any bytes beyond the fourth in either
 * buffer are ignored regardless of how much of the endpoint's up-to-
 * RCP_EP_SPI_STATUS_MAX_LEN-byte status report is actually present.
 * Returns false (never an error code -- this module's fail-safe treatment
 * of a too-short pair, mirroring rcp_ep_gpio_pin_get()'s treatment of an
 * invalid pin index) if either status_len or target_len is less than
 * RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN. status/target may be NULL iff their
 * respective length is 0. */
bool rcp_ep_spi_compound_wait_status_equal(const uint8_t *status, size_t status_len,
                                            const uint8_t *target, size_t target_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_SPI_H */
