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
//cfusa:req REQ-I2C-021
//cfusa:req REQ-I2C-022
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
 * rcp_ep_i2c_mode_t names this endpoint's five bus-speed presets
 * (extraction §5.7, §7 / TC18 §13.7.7.2 Table 46). The specification
 * extraction available to this implementation carries two internally
 * inconsistent numberings for where its highest-speed preset sits
 * relative to the "Fast mode plus" preset immediately below it -- an
 * apparent drafting inconsistency in the source material, not a
 * deliberate reserved gap between the two. Rather than silently pick one
 * reading, this module deliberately implements the *lower*-numbered of
 * the two candidate positions (RCP_EP_I2C_MODE_HIGH_SPEED = 3,
 * immediately following RCP_EP_I2C_MODE_FAST_PLUS with no reserved value
 * skipped between them) as the more conservative reading -- flagged
 * here, explicitly, as pending resolution by spec errata rather than
 * guessed at. A future errata resolution that assigns a different numeric
 * value to this preset will need this enum (and any wire-compatibility
 * shims built on top of it) revisited; see rcp_ep_i2c_mode_valid().
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-I2C-019):
 * Table 46's own fifth row, RCP_EP_I2C_MODE_ULTRA_FAST (value 4, ~5
 * MHz-class), carries no such ambiguity -- it was simply missing from
 * this enum and unconditionally rejected by rcp_ep_i2c_mode_valid()
 * entirely, a separate, uncontroversial gap now closed alongside the
 * register-block fix below.
 *
 * ── The EP_func register block (evt[2:0] == 111b) ──────────────────────────
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-I2C-021):
 * rcp_ep_i2c_decode_transfer_request() already correctly rejects evt !=
 * 0b000 (RCP_EP_I2C_ERR_BAD_EVT, via acf.h's rcp_acf_evt_row2_is_plain())
 * -- that includes evt[2:0] = 111b, TC18 §12.7.1/Table 30's generic
 * configuration-write value, exactly as it should (a config-write request
 * is not a plain transfer). What was missing, matching SPI's own
 * before-this-fix gap, was any counterpart function actually implementing
 * that 111b path: this endpoint type's Table 46 functional-configuration
 * register block was modeled only in reduced form (a bare `i2c_mode`
 * field, no wire render/parse path at all).
 *
 * A second, distinct genuine spec-table editorial defect, resolved the
 * same way `ep_pwm.h`'s EP_LEN defect and `ep_gpio.h`'s debounce address
 * defect already established as precedent: TC18 §13.7.7.2 Table 46's own
 * printed relative-address column collides two entries at 0x0002
 * (`i2c_ep_enable&clr`, 8 bit, and `i2c_base_clk`, 16 bit) and two more at
 * 0x0004 (`i2c_base_clk`'s own second octet and `i2c_ep_status`, 16 bit)
 * -- confirmed by direct visual inspection of the source PDF (physical
 * page 99), not a text-extraction artifact. PWM_OUT's, GPIO's, and now
 * SPI's own common EP_func prefixes all place their table's exact same
 * five conceptual fields (EP_LEN / a reserved-or-count octet / enable&clr
 * / options / a 16-bit read-only base-clock register) at the identical
 * address sequence 0x0000/0x0001/0x0002/0x0003/0x0004-0x0005, followed by
 * a 16-bit ep_status at 0x0006-0x0007 -- that cross-table pattern is
 * authoritative here too, so `i2c_base_clk` is placed at 0x0004-0x0005
 * (not the table's own printed 0x0002), pushing `i2c_ep_status` to
 * 0x0006-0x0007, `i2c_clock_divider` to 0x0008, `i2c_mode` to 0x0009, and
 * `i2c_trail` to 0x000A (RCP_EP_I2C_EP_FUNC_LEN = 0x000B, 11 octets
 * total) rather than the table's own colliding addresses. `i2c_base_clk`
 * is not itself stored (it has no setter, no meaningful value this module
 * can derive, and always renders 0 -- the same "no real clock source
 * modelled" honesty ep_gpio.h's own `gpio_base_clk` already commits to);
 * the reserved octet at 0x0001 likewise always renders 0, matching
 * ep_pwm.h's own reserved-octet convention.
 *
 * ── Functional configuration ────────────────────────────────────────────────
 *
 * rcp_ep_i2c_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that
 * module's documented convention, same as ep_gpio.h/ep_spi.h) and adds
 * this endpoint's runtime-adjustable fields: i2c_mode, ep_status,
 * clock_divider, and trail (the last three new, added for the register
 * block above).
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
    RCP_EP_I2C_MODE_ULTRA_FAST = 4, /* ~5 MHz-class preset -- Table 46's own
                                        fifth row, unambiguous (no numbering
                                        clash unlike RCP_EP_I2C_MODE_HIGH_SPEED
                                        above); FIXED 2026-08-11
                                        (c-RCP-AUDIT-06, issue #256 Group I,
                                        REQ-I2C-019) -- previously rejected
                                        by rcp_ep_i2c_mode_valid() entirely */
} rcp_ep_i2c_mode_t;

/* True iff v (a raw i2c_mode value, e.g. as decoded from a register) is
 * one of the five defined presets, i.e. v <= 4. */
bool rcp_ep_i2c_mode_valid(uint8_t v);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint8_t                        i2c_mode; /* rcp_ep_i2c_mode_t */
    uint16_t                       ep_status;      /* i2c_ep_status, Table 46 */
    uint8_t                        clock_divider;  /* i2c_clock_divider, Table 46 */
    uint8_t                        trail;          /* i2c_trail, Table 46 */
} rcp_ep_i2c_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, i2c_mode
 * RCP_EP_I2C_MODE_STANDARD, ep_status/clock_divider/trail all 0). */
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

/* ── The EP_func register block (the evt[2:0] == 111b target) ──────────────── */

/* Relative octet offsets of the registers making up this endpoint's own
 * EP_func block, at the corrected addresses -- see the file header's
 * note on Table 46's own printed address collisions. Every multi-octet
 * register is big-endian, like every other multi-octet field this
 * codebase encodes. Offsets marked R are read-only: a configuration
 * write covering them leaves them unchanged (see
 * rcp_ep_i2c_apply_reconfig()). */
#define RCP_EP_I2C_REG_EP_LEN         ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_I2C_REG_RESERVED_01    ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_I2C_REG_EP_ENABLE_CLR  ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_I2C_REG_EP_OPTIONS     ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_I2C_REG_BASE_CLK       ((uint16_t)0x0004u) /* 16 bit, R   */
#define RCP_EP_I2C_REG_EP_STATUS      ((uint16_t)0x0006u) /* 16 bit, R/W */
#define RCP_EP_I2C_REG_CLOCK_DIVIDER  ((uint16_t)0x0008u) /*  8 bit, R/W */
#define RCP_EP_I2C_REG_MODE           ((uint16_t)0x0009u) /*  8 bit, R/W */
#define RCP_EP_I2C_REG_TRAIL          ((uint16_t)0x000Au) /*  8 bit, R/W */

/* The block's own length in octets -- one past the last assigned offset,
 * i.e. the value the endpoint reports at RCP_EP_I2C_REG_EP_LEN and the
 * bound the "write beyond EP_LEN is ignored" rule (§12.7.1) is applied
 * against. */
#define RCP_EP_I2C_EP_FUNC_LEN ((uint16_t)0x000Bu)

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- the address is a 16-bit
 * big-endian field, followed by the configuration data octets to write
 * from that address onward (§12.7.1). */
#define RCP_EP_I2C_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_I2C_RECONFIG_OK               = 0,
    RCP_EP_I2C_RECONFIG_ERR_SHORT        = 1, /* payload carries no address
                                                  prefix, or an address
                                                  prefix with no data octet
                                                  after it */
    RCP_EP_I2C_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data length
                                                  exceeds
                                                  RCP_EP_I2C_EP_FUNC_LEN --
                                                  the whole write is ignored,
                                                  per the specification's own
                                                  rule */
} rcp_ep_i2c_reconfig_errc_t;

/* Human-readable message for an rcp_ep_i2c_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_i2c_reconfig_strerror(rcp_ep_i2c_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_I2C_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_i2c_apply_reconfig()'s own parse step, and the
 * same rendering that function patches in place. i2c_base_clk (read-only)
 * always renders 0 -- see the file header. */
void rcp_ep_i2c_render_registers(const rcp_ep_i2c_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_I2C_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is NOT
 * presented at the interface but interpreted as an addressed write into
 * this endpoint's own EP_func block -- a 16-bit big-endian relative start
 * address followed by the configuration data octets to write from that
 * address onward (§12.7.1). This is a real register write, reaching every
 * R/W register the block defines (enable/options, status, clock divider,
 * mode, trail), not merely i2c_mode.
 *
 * Returns RCP_EP_I2C_RECONFIG_ERR_SHORT when payload_len is not at least
 * RCP_EP_I2C_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_I2C_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would extend
 * past RCP_EP_I2C_EP_FUNC_LEN; in both cases cfg is left entirely
 * unchanged, per the specification's own "such a payload is to be ignored"
 * rule. Octets of the addressed span that land on a read-only register
 * (EP_LEN, the reserved octet, base_clk) are left at their current values
 * while the rest of the span is still applied. Note that a write covering
 * RCP_EP_I2C_REG_MODE with a value that is not rcp_ep_i2c_mode_valid() is
 * still applied verbatim (this function has no i2c_mode-specific
 * validation of its own, matching every other register's plain-octet
 * treatment); callers reading i2c_mode back out should still apply
 * rcp_ep_i2c_mode_valid() themselves if that distinction matters to them.
 *
 * A caller routing a decoded request here is responsible for having
 * checked that evt[2:0] really was 111b -- rcp_ep_i2c_decode_transfer_request()
 * already rejects it (RCP_EP_I2C_ERR_BAD_EVT) so a misrouted request
 * cannot reach that path by accident. */
rcp_ep_i2c_reconfig_errc_t rcp_ep_i2c_apply_reconfig(rcp_ep_i2c_functional_cfg_t *cfg,
                                                      const uint8_t *payload,
                                                      size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_i2c_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint16_t start_address, const uint8_t *data,
                                                size_t data_len, uint8_t transaction_num);

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
