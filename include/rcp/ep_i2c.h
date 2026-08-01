/* SPDX-License-Identifier: MPL-2.0 */
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
//cfusa:req REQ-I2C-017
//cfusa:req REQ-I2C-018

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-I2C-019
//cfusa:req REQ-I2C-020
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
 * ── Transfer direction: the ACF op bit vs. the address byte's R/W bit ──────
 *
 * An I2C transfer is directional, and that direction appears in two
 * entirely independent places, which this module keeps strictly separate:
 *
 *   - The *I2C-bus-level* R/W bit, which rides inside the target-device
 *     address byte(s) at the head of the payload. This module never looks
 *     at it (see the raw-byte-stream note above): it is the caller's to
 *     set, and the endpoint clocks it onto the bus verbatim.
 *
 *   - The *RCP-level* direction, the ACF header's op bit. It tells the RC
 *     Server what kind of response this transaction expects: the read
 *     sense (RCP_ACF_OP_READ, wire op=0) asks for a response carrying
 *     data read back from the endpoint, and the write sense
 *     (RCP_ACF_OP_WRITE, wire op=1) asks only for a payload-less success
 *     confirmation (the general request-handling rule, extraction §3.9.1,
 *     and the two response classifications built on it -- a read response
 *     has a byte_msg_payload, a write response does not).
 *
 * Both senses are therefore reachable for this endpoint type, and
 * rcp_ep_i2c_dir_t makes the choice an explicit parameter of the request
 * *and* the response codec rather than a constant baked into either. On a
 * read-direction request the ACF header's 12-bit read_size slot carries
 * how many octets to clock back; on a write-direction request that same
 * slot is a segment_num instead (acf.h), so this module leaves it 0 there
 * rather than smuggling a read_size into a field that does not mean that.
 *
 * Revisions of this module before v0.104.0 hard-coded the write sense on
 * every request and rejected the read sense outright as malformed, so an
 * I2C read transaction -- the very direction the payload's own R/W bit
 * exists to express -- could be neither encoded nor accepted, while the
 * module simultaneously offered a data-bearing response encoder that only
 * a read-sense request can lawfully elicit. That is a *different* defect
 * from the inverted-op one ep_lin.c/ep_spi.c carried and which was
 * corrected in v0.103.0: those two endpoints are unconditionally
 * response-bearing (a LIN command always asks for what came back on the
 * bus; an SPI transfer is full duplex and always returns POCI octets), so
 * for them a single constant op *is* correct and was simply the wrong
 * constant. An I2C transfer is half duplex and genuinely either-directional,
 * so no constant is correct for it. The specification's own I2C request
 * figure reflects exactly that: it leaves the op cell blank while showing
 * the R/W bit explicitly inside the address, i.e. it declines to pin op
 * for this endpoint type. See tests/test_ep_i2c.c, which cites and quotes
 * the normative text this rests on.
 *
 * A response is encoded as
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

/* ── Transfer direction ────────────────────────────────────────────────────── */

/* The RCP-level direction of a transfer, i.e. what the ACF header's op bit
 * carries -- NOT the I2C-bus-level R/W bit inside the payload's address
 * byte(s), which this module never inspects. See the file header. */
typedef enum {
    RCP_EP_I2C_DIR_WRITE = 0, /* op=1: octets go out, no data comes back;
                                  the response is a payload-less success
                                  confirmation */
    RCP_EP_I2C_DIR_READ  = 1, /* op=0: the payload's address octet(s) go
                                  out and read_size octets are clocked back
                                  in the response's payload */
} rcp_ep_i2c_dir_t;

/* True iff d is one of the two defined directions. */
bool rcp_ep_i2c_dir_valid(rcp_ep_i2c_dir_t d);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_I2C_OK               = 0,
    RCP_EP_I2C_ERR_SHORT_FRAME  = 1,
    RCP_EP_I2C_ERR_BAD_MSG_TYPE = 2,
    RCP_EP_I2C_ERR_WRONG_BUS    = 3,
    /* Retained for source/ABI stability and still covered by
     * rcp_ep_i2c_strerror(), but no longer produced by any decoder in
     * this module: both op senses are valid on an I2C transfer (see the
     * file header), so there is no longer a "wrong" one to reject. */
    RCP_EP_I2C_ERR_WRONG_OP     = 4,
    /* evt[2:0] is not 0b000, TC18 §13.5 Table 30's only legal value for a
     * plain (non-configuration) request in I2C's endpoint-type row --
     * caller shall respond with error code UNSUPPORTED_CMD (see
     * rcp_acf_evt_row2_is_plain()). */
    RCP_EP_I2C_ERR_BAD_EVT      = 5,
} rcp_ep_i2c_errc_t;

/* Human-readable message for an rcp_ep_i2c_errc_t value. Never returns NULL. */
const char *rcp_ep_i2c_strerror(rcp_ep_i2c_errc_t e);

/* ── Transfer request ──────────────────────────────────────────────────────── */

/* Largest value the ACF header's 12-bit read_size slot can carry. */
#define RCP_EP_I2C_MAX_READ_SIZE ((uint16_t)0x0FFFu)

/* Encodes an ACF_ABB transfer request addressed to byte_bus_id: the
 * payload is exactly tx_data[0..tx_len), the raw bytes to place on the
 * bus for this transaction -- target-device address byte(s) included, and
 * never parsed or validated by this module (see the file header). evt is
 * always encoded as 0 (this endpoint type has no channel selector).
 * tx_data may be NULL iff tx_len == 0.
 *
 * direction selects the RCP-level op sense (see rcp_ep_i2c_dir_t and the
 * file header). read_size is the number of octets the endpoint is asked
 * to clock back, and applies to RCP_EP_I2C_DIR_READ only -- for
 * RCP_EP_I2C_DIR_WRITE it must be 0, because that header slot carries a
 * segment_num rather than a read_size in the write sense.
 *
 * Returns a zeroed rcp_bytes_t (data=NULL) if direction is not
 * rcp_ep_i2c_dir_valid(), if read_size exceeds RCP_EP_I2C_MAX_READ_SIZE,
 * if read_size != 0 with direction RCP_EP_I2C_DIR_WRITE, if tx_len
 * exceeds RCP_ACF_MAX_PAYLOAD, or on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_i2c_encode_transfer_request(rcp_byte_bus_id_t byte_bus_id,
                                                rcp_ep_i2c_dir_t direction,
                                                const uint8_t *tx_data, size_t tx_len,
                                                uint16_t read_size,
                                                uint8_t transaction_num);

/* Decodes and validates an ACF-level I2C transfer request from b[0..len).
 * Fails with RCP_EP_I2C_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_I2C_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_I2C_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_I2C_ERR_BAD_EVT if its evt[2:0]
 * is not 0b000 (rcp_acf_evt_row2_is_plain(), TC18 §13.5 Table 30 -- the
 * caller shall respond with error code UNSUPPORTED_CMD). Both op senses
 * are accepted -- see the file header -- and reported via *out_direction.
 *
 * On RCP_EP_I2C_OK, *out_direction, *out_read_size (the requested
 * read_size for RCP_EP_I2C_DIR_READ, 0 for RCP_EP_I2C_DIR_WRITE, whose
 * header slot is a segment_num this module does not interpret) and
 * *out_transaction_num are populated, and *out_tx_data / *out_tx_len are
 * set to a *borrowed* view into b (not copied -- see the file header) of
 * the raw outgoing payload. */
rcp_ep_i2c_errc_t rcp_ep_i2c_decode_transfer_request(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      rcp_ep_i2c_dir_t *out_direction,
                                                      const uint8_t **out_tx_data,
                                                      size_t *out_tx_len,
                                                      uint16_t *out_read_size,
                                                      uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes the response to a transfer request of the same direction (see
 * the file header): the op sense a response carries is the one its
 * request carried, since that is what distinguishes a read response --
 * which has a byte_msg_payload -- from a write response, which does not.
 *
 * For RCP_EP_I2C_DIR_READ the payload is rx_data[0..rx_len), the raw
 * bytes captured back from the bus during the transaction (rx_data may be
 * NULL iff rx_len == 0). For RCP_EP_I2C_DIR_WRITE there is no payload and
 * rx_len must be 0.
 *
 * Encoded as ACF_ABB when timed is false; as ACF_GBB (with
 * message_timestamp set to timestamp, mtv = RCP_ACF_MTV_VALID) when timed
 * is true -- see the file header. Returns a zeroed rcp_bytes_t
 * (data=NULL) if direction is not rcp_ep_i2c_dir_valid(), if rx_len != 0
 * with direction RCP_EP_I2C_DIR_WRITE, if rx_len exceeds
 * RCP_ACF_MAX_PAYLOAD, or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_i2c_encode_response(rcp_byte_bus_id_t byte_bus_id,
                                        rcp_ep_i2c_dir_t direction, const uint8_t *rx_data,
                                        size_t rx_len, uint8_t transaction_num, bool timed,
                                        uint64_t timestamp);

/* Decodes an I2C response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoder
 * above, since a response's encoding depends on the responding endpoint's
 * own timed/untimed choice). Fails with RCP_EP_I2C_ERR_SHORT_FRAME (frame
 * too short for the applicable fixed header or its declared payload
 * length) or RCP_EP_I2C_ERR_WRONG_BUS (byte_bus_id != expected_bus_id). On
 * RCP_EP_I2C_OK, *out_direction (the op sense this response carries, which
 * is the direction of the request it answers -- see the file header) and
 * *out_transaction_num are populated; *out_rx_data /
 * *out_rx_len are set to a *borrowed* view into b (not copied) of the raw
 * captured payload, empty for a write response; *out_timed and
 * *out_timestamp report whether the
 * message was ACF_GBB with a valid (rcp_acf_gbb_is_timed()) timestamp, and
 * that timestamp's value (0 when !*out_timed). */
rcp_ep_i2c_errc_t rcp_ep_i2c_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              rcp_ep_i2c_dir_t *out_direction,
                                              const uint8_t **out_rx_data, size_t *out_rx_len,
                                              bool *out_timed, uint64_t *out_timestamp,
                                              uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_I2C_H */
