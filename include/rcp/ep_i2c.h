//cfusa:req REQ-I2C-001
//cfusa:req REQ-I2C-002
//cfusa:req REQ-I2C-003
//cfusa:req REQ-I2C-004
//cfusa:req REQ-I2C-005
//cfusa:req REQ-I2C-006
//cfusa:req REQ-I2C-007
//cfusa:req REQ-I2C-008
//cfusa:req REQ-I2C-009
//cfusa:req REQ-I2C-010
//cfusa:req REQ-I2C-011
//cfusa:req REQ-I2C-012
//cfusa:req REQ-I2C-013
//cfusa:req REQ-I2C-014
//cfusa:req REQ-I2C-015
//cfusa:req REQ-I2C-016
/*
 * ep_i2c.h -- I2C endpoint for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 16, "Basic Endpoints", milestone 66).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any prior endpoint file
 * (ep_gpio.h/.c, ep_spi.h/.c) is touched here -- the same layering
 * discipline those two modules established, followed structurally
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
 * As with ep_gpio.h/ep_spi.h, an I2C transfer request/response is ordinary
 * endpoint traffic: whether it rides an NTSCF or TSCF frame is a
 * transport/scheduling choice made by the caller (avtp.h), not a property
 * of the I2C endpoint itself. This module therefore operates at the ACF
 * level only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their decode
 * counterparts) -- a caller wraps (or unwraps) the frames this module
 * produces (or consumes) in NTSCF/TSCF using avtp.h directly.
 *
 * ── Controller-only, single bus per endpoint ────────────────────────────────
 *
 * This endpoint models an I2C *controller* only (no peripheral/target
 * mode). Unlike ep_spi.h's up-to-six pre-configured channels sharing one
 * byte_bus_id, this endpoint type addresses exactly one I2C bus (one
 * SCL/SDA pair, regmap.h's RCP_REGMAP_SIGNAL_I2C_SCL/_SDA) per
 * byte_bus_id -- there is no channel selector on the wire, and the ACF
 * header's evt field is always encoded/decoded as 0.
 *
 * ── Raw byte-stream transfers: no protocol-level address parsing ───────────
 *
 * A transfer request's payload is the *raw* sequence of bytes this
 * endpoint places on the bus for that transaction, target-device address
 * byte(s) included -- this module never itself inspects, validates, or
 * strips an address byte from the payload it encodes or decodes; it is a
 * dumb byte pipe, exactly the same design philosophy ep_spi.h already
 * commits to for its own PICO-out/POCI-in bytes. A response's payload is
 * whatever raw bytes were captured back from the bus during that same
 * transaction (empty for a pure write). Decoded payloads are *borrowed*
 * pointers into the caller-supplied frame buffer, matching acf.c's own
 * decode_abb()/decode_gbb() convention, for the same reason ep_spi.h's
 * transfer payloads are borrowed rather than copied: a variable-length
 * payload has no natural fixed-size out-parameter to copy into.
 *
 * This is a deliberate, early validation of that same raw-byte-stream/
 * no-framing-help design against real endpoint code, ahead of the LIN
 * endpoint (ROADMAP.md Phase 19, milestone 71) that the roadmap says will
 * commit to the identical philosophy -- the roadmap's explicit intent is
 * that this gets validated once here, not re-argued when LIN lands.
 *
 * Encoded as ACF_OP_WRITE (request, carrying the raw outgoing bytes) and
 * ACF_OP_READ (response, carrying the raw captured bytes), mirroring
 * ep_gpio.h's/ep_spi.h's own request-carries-op-WRITE /
 * response-classifies-as-a-read convention. A response is encoded as
 * ACF_ABB when untimed, or ACF_GBB (carrying a message_timestamp) when the
 * endpoint's ep_response_ts_enable functional-config flag (regmap.h's
 * rcp_regmap_ep_functional_cfg_t, composed into rcp_ep_i2c_functional_cfg_t
 * below) is set -- that flag's value is a caller-supplied bool here, this
 * module never itself reaches into a register map to read it, matching
 * ep_gpio.h's/ep_spi.h's own convention of consuming already-classified
 * inputs.
 *
 * ── i2c_mode: bus-speed presets, and a flagged spec ambiguity ──────────────
 *
 * rcp_ep_i2c_mode_t names this endpoint's four bus-speed presets
 * (extraction §5.7, §7). The specification extraction available to this
 * implementation carries two internally inconsistent numberings for where
 * its highest-speed preset sits relative to the "Fast mode plus" preset
 * immediately below it -- an apparent drafting inconsistency in the
 * source material, not a deliberate reserved gap between the two. Rather
 * than silently pick one reading, this module deliberately implements the
 * *lower*-numbered of the two candidate positions (RCP_EP_I2C_MODE_HIGH_SPEED
 * = 3, immediately following RCP_EP_I2C_MODE_FAST_PLUS with no reserved
 * value skipped between them) as the more conservative reading -- flagged
 * here, explicitly, as pending resolution by spec errata rather than
 * guessed at. A future errata resolution that assigns a different numeric
 * value to this preset will need this enum (and any wire-compatibility
 * shims built on top of it) revisited; see rcp_ep_i2c_mode_valid().
 *
 * ── Functional configuration ────────────────────────────────────────────────
 *
 * rcp_ep_i2c_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that
 * module's documented convention, same as ep_gpio.h/ep_spi.h) and adds
 * this endpoint's one runtime-adjustable field: i2c_mode.
 * rcp_ep_i2c_functional_cfg_writable() is, likewise, a thin, named wrapper
 * over server.h's rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W),
 * and rcp_ep_i2c_set_mode() consults it before ever touching cfg -- reusing,
 * never duplicating, server.h's/regmap.h's existing authorization logic,
 * per the roadmap's explicit instruction (the same rule ep_gpio.h's/
 * ep_spi.h's own setters already follow).
 */
#ifndef RCP_EP_I2C_H
#define RCP_EP_I2C_H

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

/* ── i2c_mode: bus-speed presets ────────────────────────────────────────────── */

/* See the file header's high-speed-numbering ambiguity note. */
typedef enum {
    RCP_EP_I2C_MODE_STANDARD   = 0, /* ~100 kHz-class preset */
    RCP_EP_I2C_MODE_FAST       = 1, /* ~400 kHz-class preset */
    RCP_EP_I2C_MODE_FAST_PLUS  = 2, /* ~1 MHz-class preset */
    RCP_EP_I2C_MODE_HIGH_SPEED = 3, /* conservative, lower-numbered reading;
                                        pending spec errata -- see the file
                                        header */
} rcp_ep_i2c_mode_t;

/* True iff v (a raw i2c_mode value, e.g. as decoded from a register) is
 * one of the four defined presets, i.e. v <= 3. */
bool rcp_ep_i2c_mode_valid(uint8_t v);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint8_t                        i2c_mode; /* rcp_ep_i2c_mode_t */
} rcp_ep_i2c_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, i2c_mode
 * RCP_EP_I2C_MODE_STANDARD). */
void rcp_ep_i2c_functional_cfg_init(rcp_ep_i2c_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (server.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_i2c_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->i2c_mode to mode iff rcp_ep_i2c_functional_cfg_writable()
 * authorizes the write for state/writer and mode is rcp_ep_i2c_mode_valid();
 * returns whether the write was applied. cfg is left entirely unchanged
 * when it returns false. */
bool rcp_ep_i2c_set_mode(rcp_ep_i2c_functional_cfg_t *cfg, rcp_ep_i2c_mode_t mode,
                          rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_I2C_OK               = 0,
    RCP_EP_I2C_ERR_SHORT_FRAME  = 1,
    RCP_EP_I2C_ERR_BAD_MSG_TYPE = 2,
    RCP_EP_I2C_ERR_WRONG_BUS    = 3,
    RCP_EP_I2C_ERR_WRONG_OP     = 4,
} rcp_ep_i2c_errc_t;

/* Human-readable message for an rcp_ep_i2c_errc_t value. Never returns NULL. */
const char *rcp_ep_i2c_strerror(rcp_ep_i2c_errc_t e);

/* ── Transfer request ──────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB transfer request addressed to byte_bus_id: the
 * payload is exactly tx_data[0..tx_len), the raw bytes to place on the
 * bus for this transaction -- target-device address byte(s) included, and
 * never parsed or validated by this module (see the file header). evt is
 * always encoded as 0 (this endpoint type has no channel selector).
 * tx_data may be NULL iff tx_len == 0. Returns a zeroed rcp_bytes_t
 * (data=NULL) if tx_len exceeds RCP_ACF_MAX_PAYLOAD or on allocation
 * failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_i2c_encode_transfer_request(rcp_byte_bus_id_t byte_bus_id,
                                                const uint8_t *tx_data, size_t tx_len,
                                                uint8_t transaction_num);

/* Decodes and validates an ACF-level I2C transfer request from b[0..len).
 * Fails with RCP_EP_I2C_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_I2C_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_I2C_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_I2C_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_WRITE. On RCP_EP_I2C_OK, *out_transaction_num is populated,
 * and *out_tx_data / *out_tx_len are set to a *borrowed* view into b (not
 * copied -- see the file header) of the raw outgoing payload. */
rcp_ep_i2c_errc_t rcp_ep_i2c_decode_transfer_request(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      const uint8_t **out_tx_data,
                                                      size_t *out_tx_len,
                                                      uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes an I2C response carrying rx_data[0..rx_len) (the raw bytes
 * captured back from the bus during the transaction; empty for a pure
 * write; rx_data may be NULL iff rx_len == 0) as its payload, echoing
 * transaction_num. Encoded as ACF_ABB when timed is false; as ACF_GBB
 * (with message_timestamp set to timestamp, mtv = RCP_ACF_MTV_VALID) when
 * timed is true -- see the file header. Returns a zeroed rcp_bytes_t
 * (data=NULL) if rx_len exceeds RCP_ACF_MAX_PAYLOAD or on allocation
 * failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_i2c_encode_response(rcp_byte_bus_id_t byte_bus_id, const uint8_t *rx_data,
                                        size_t rx_len, uint8_t transaction_num, bool timed,
                                        uint64_t timestamp);

/* Decodes an I2C response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoder
 * above, since a response's encoding depends on the responding endpoint's
 * own timed/untimed choice). Fails with RCP_EP_I2C_ERR_SHORT_FRAME (frame
 * too short for the applicable fixed header or its declared payload
 * length) or RCP_EP_I2C_ERR_WRONG_BUS (byte_bus_id != expected_bus_id). On
 * RCP_EP_I2C_OK, *out_transaction_num is populated; *out_rx_data /
 * *out_rx_len are set to a *borrowed* view into b (not copied) of the raw
 * captured payload; *out_timed and *out_timestamp report whether the
 * message was ACF_GBB with a valid (rcp_acf_gbb_is_timed()) timestamp, and
 * that timestamp's value (0 when !*out_timed). */
rcp_ep_i2c_errc_t rcp_ep_i2c_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              const uint8_t **out_rx_data, size_t *out_rx_len,
                                              bool *out_timed, uint64_t *out_timestamp,
                                              uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_I2C_H */
