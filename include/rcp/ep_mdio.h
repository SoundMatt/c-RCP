//cfusa:req REQ-MDIO-001
//cfusa:req REQ-MDIO-002
//cfusa:req REQ-MDIO-003
//cfusa:req REQ-MDIO-004
//cfusa:req REQ-MDIO-005
//cfusa:req REQ-MDIO-006
//cfusa:req REQ-MDIO-007
//cfusa:req REQ-MDIO-008
//cfusa:req REQ-MDIO-009
//cfusa:req REQ-MDIO-010
//cfusa:req REQ-MDIO-011
//cfusa:req REQ-MDIO-012
//cfusa:req REQ-MDIO-013
//cfusa:req REQ-MDIO-014
//cfusa:req REQ-MDIO-015
//cfusa:req REQ-MDIO-016
//cfusa:req REQ-MDIO-017
//cfusa:req REQ-MDIO-018
//cfusa:req REQ-MDIO-019
/*
 * ep_mdio.h -- MDIO management endpoint for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 19, "Remaining Endpoint Types",
 * milestone 74).
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
 * ep_adc.h/.c, ep_lin.h/.c, ep_can.h/.c, ep_iseled.h/.c) is touched here --
 * the same layering discipline every endpoint type since milestone 64 has
 * established, followed structurally throughout by this module too.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 * MDIO (Management Data Input/Output) itself is a separate,
 * independently-documented industry-standard management interface (IEEE
 * 802.3 Clause 22/Clause 45); the register-addressing widths this module
 * relies on (a 5-bit port/PHY address, a 5-bit legacy register address, a
 * 5-bit MMD device address, a 16-bit extended register address, and a
 * 16-bit register data word) are independently public knowledge of that
 * standard, not values taken from the confidential TC18 extraction --
 * cited here (extraction §1.2, §5.10-5.13, §3.3-3.4) only as attribution
 * for which general topic area motivated this endpoint type's existence,
 * never as a source of prose or numeric detail. The concrete wire layout
 * below (byte ordering, prefix shape, this module's own burst word-count
 * cap) is this module's own original design.
 *
 * ── Requirement-id naming note ──────────────────────────────────────────────
 *
 * Verified directly (`grep`) against `.fusa-reqs.json` before picking this
 * module's own prefix, the same check every prior endpoint milestone has
 * made: this codebase has never carried a pre-replacement MDIO bridge/stub
 * module of any kind, and none of this repository's satellite packages are
 * named `mdio` -- there is no pre-existing `REQ-MDIO-*` prefix to collide
 * with. This module's requirements are therefore tagged plain
 * `REQ-MDIO-*`, with no "-EP" collision-avoidance suffix needed (the same
 * position ep_iseled.h's own header documents for the identical reason).
 *
 * ── Framing level ────────────────────────────────────────────────────────────
 *
 * As with every endpoint type before it, an MDIO transaction request/
 * response is ordinary endpoint traffic: whether it rides an NTSCF or TSCF
 * AVTPDU is a transport/scheduling choice made by the caller (avtp.h), not
 * a property of this endpoint itself. This module therefore operates at
 * the ACF level only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their
 * decode counterparts) -- a caller wraps (or unwraps) the frames this
 * module produces (or consumes) in NTSCF/TSCF using avtp.h directly.
 *
 * ── Two addressing modes: Clause-22 MMD and Clause-45 MMS ──────────────────
 *
 * rcp_ep_mdio_addr_t models both addressing modes this endpoint type
 * supports, selected by its own `clause` field:
 *
 *   - RCP_EP_MDIO_CLAUSE_22 ("MMD", the legacy addressing mode): a 5-bit
 *     port/PHY address (`prtad`) plus a 5-bit register address (`regad`,
 *     only its low 5 bits significant) select one 16-bit register
 *     directly. There is no MMD device address concept in this mode --
 *     `devad` must be 0, enforced by rcp_ep_mdio_addr_valid() below.
 *   - RCP_EP_MDIO_CLAUSE_45 ("MMS", the extended addressing mode): the same
 *     5-bit port/PHY address (`prtad`) is joined by a 5-bit MMD device
 *     address (`devad`) and a full 16-bit register address (`regad`) to
 *     select one register within that device's own 65536-entry register
 *     space.
 *
 * rcp_ep_mdio_addr_valid() is this module's own pure, directly-testable
 * statement of both modes' field-range invariants at once (an invalid
 * `clause` value, an out-of-range `prtad`/`devad`, or a Clause-22 `regad`
 * above its 5-bit range all fail validation); every decode function below
 * calls it and fails with RCP_EP_MDIO_ERR_BAD_ADDR rather than accepting
 * an address it cannot represent on the physical bus.
 *
 * ── Single-word and burst addressing ─────────────────────────────────────────
 *
 * A request's `word_count` selects between the two addressing widths the
 * roadmap names: word_count == 1 is a single-word transaction (one
 * register read or written); word_count > 1 is a burst transaction, this
 * module's own generalization of the well-known MDIO post-increment
 * addressing idiom to both addressing modes uniformly -- each successive
 * word in a burst is understood to occupy the next register address after
 * the one before it, starting from the request's own `addr.regad`.
 * rcp_ep_mdio_burst_next_regad() is the small, pure, directly-testable
 * statement of that one-step advance, with wraparound at each addressing
 * mode's own register-address width (5 bits for Clause-22, 16 bits for
 * Clause-45) rather than an out-of-range result -- this module's own
 * design choice for a well-defined result at the top of the address
 * space, not a spec-mandated behavior. This module does not itself walk a
 * burst step by step; rcp_ep_mdio_burst_next_regad() is provided for a
 * caller's own transport-layer bus-transaction loop to consult, the same
 * "small, pure, independently-testable transform" role
 * rcp_ep_iseled_requires_isp_n() plays for its own endpoint type.
 *
 * `RCP_EP_MDIO_MAX_BURST_WORDS` (512) is this module's own chosen upper
 * bound on `word_count`, not a spec-derived number -- chosen so that this
 * endpoint's own worst-case encoded read/write request or response
 * (prefix plus 512 packed 16-bit words) stays comfortably inside
 * RCP_AVTP_NTSCF_MAX_PAYLOAD (avtp.h), the same deliberate
 * single-AVTPDU-worst-case scope ep_uart.h's/ep_lin.h's/ep_iseled.h's own
 * request/response pairs already commit to, in contrast to ep_can.h's own
 * CAN XL frame type, which documents exceeding that bound as a deliberate,
 * separately-justified exception. This endpoint type needs no such
 * exception: MDIO register bursts have no analogous "the spec's own frame
 * format already exceeds one AVTPDU" pressure.
 *
 * ── Wire layout: this module's own address-prefix-then-words choice ────────
 *
 * A read request's ACF-level payload is a fixed 7-byte prefix -- 1 byte
 * `clause`, 1 byte `prtad`, 1 byte `devad`, a big-endian 2-byte `regad`,
 * and a big-endian 2-byte `word_count` -- and no further bytes (nothing to
 * read yet; only the reply carries data). A write request's payload is
 * the same 5-byte address prefix (`clause`/`prtad`/`devad`/`regad`, with
 * `word_count` this time implied by the payload's own remaining length
 * rather than encoded again) followed by `word_count` packed big-endian
 * 16-bit words -- see rcp_ep_mdio_pack_words()/_word_count_of()/
 * _unpack_word_at() below. A read or write response's payload is simply
 * the packed words the endpoint actually captured or accepted (no address
 * prefix -- transaction_num already correlates a response back to its
 * request), possibly fewer than the requesting `word_count` on a partial
 * burst, the same "possibly-short accepted/received prefix" partial-
 * completion convention ep_uart.h's own write/read response pair already
 * established. This module never inspects, generates, or reinterprets the
 * *content* of any data word itself -- only this endpoint's own
 * structural addressing fields are modeled here, the same "dumb pass-
 * through for the data content" philosophy ep_can.h's own frame data
 * bytes already commit to.
 *
 * Following ep_uart.h's own TX-write/RX-read two-family precedent (rather
 * than ep_iseled.h's/ep_can.h's single request/response pair), this module
 * exposes two independent request/response families --
 * rcp_ep_mdio_encode_write_request()/_decode_write_request() and
 * rcp_ep_mdio_encode_write_response()/_decode_write_response() (encoded
 * with ACF_OP_WRITE throughout), and
 * rcp_ep_mdio_encode_read_request()/_decode_read_request() and
 * rcp_ep_mdio_encode_read_response()/_decode_read_response() (encoded with
 * ACF_OP_READ throughout) -- because, unlike a single-direction serial
 * push (ep_lin.h) or a single symmetric command/reply exchange
 * (ep_iseled.h, ep_can.h), this endpoint type has two genuinely distinct
 * underlying MDIO operations (register read, register write) that both
 * need their own request and response shape, the same asymmetry
 * ep_uart.h's own TX/RX split already reflects for an analogous reason.
 *
 * ── No type-specific functional config beyond the universal common block ───
 *
 * Unlike ep_can.h's three separate bit-timing register sets or
 * ep_iseled.h's clk-divider/crc-enable/trigger fields,
 * rcp_ep_mdio_functional_cfg_t adds *nothing* of its own beyond composing
 * regmap.h's rcp_regmap_ep_functional_cfg_t as its sole member. This is a
 * deliberate, documented "nothing more to add" finding, not an oversight:
 * per extraction §5.10-5.13, this endpoint type's register-map footprint
 * is fully covered by the common enable/clear/CRC/timestamp/suppress-
 * response flags every endpoint type already shares, with no MDIO-
 * specific runtime-adjustable register of its own. Consequently there are
 * no rcp_ep_mdio_set_*() mutators in this file at all: no endpoint type in
 * this codebase exposes a setter for the common block's own fields either
 * (those are the generic register-map layer's job, not any one endpoint
 * type's), so with no fields of its own to add, this module has nothing
 * left to set. rcp_ep_mdio_functional_cfg_init() and
 * rcp_ep_mdio_functional_cfg_writable() are still provided, matching every
 * other endpoint type's own init/writable pair, purely for that
 * consistency -- rcp_ep_mdio_functional_cfg_writable() is, like every
 * other endpoint type's own version, a thin, named wrapper over
 * lifecycle.h's rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W),
 * reusing rather than duplicating that authorization logic. Anyone
 * extending this file later who finds themselves reaching for an
 * MDIO-specific functional-config field should stop and re-read this
 * paragraph first -- it is this milestone's documented scope, the same
 * "roadmap and spec both agree there is simply nothing to add" position
 * ep_can.h's own file header already states for its own missing trigger
 * table (see below).
 *
 * ── No trigger-signal table ───────────────────────────────────────────────
 *
 * Like ep_can.h (and unlike ep_lin.h's single transmission-done trigger or
 * ep_iseled.h's single transmission-complete trigger), this module defines
 * *no* trigger enumeration and no `rcp_ep_mdio_trigger_t`-shaped field
 * anywhere in rcp_ep_mdio_functional_cfg_t. This mirrors ep_can.h's own
 * documented reflection of a gap in the specification itself (extraction
 * §7) rather than an oversight or a placeholder pending a later fix: the
 * spec's own trigger-signal table has no populated entry for this
 * endpoint type either. Anyone extending this file later who finds
 * themselves reaching for an MDIO trigger concept should stop and re-read
 * this paragraph first -- it is this milestone's documented scope, not a
 * gap to silently invent an answer for.
 *
 * ── Useful with zero physical MDIO pins mapped ──────────────────────────────
 *
 * This endpoint type's register map is meaningful even when no physical
 * MDIO/MDC pin pair is mapped in the hardware pin map (regmap.h's
 * rcp_regmap_hw_pin_map_entry_t table, untouched by this milestone) at
 * all: an RC Server can expose an on-die/integrated PHY's own management
 * registers this way, reached entirely internally, with no external MDIO
 * bus ever driven. This module makes no attempt to itself validate any
 * pin-map entry (no endpoint type in this codebase does that from within
 * its own request/response codec) -- it is named here only because, for
 * this endpoint type specifically, the *normal* case -- physical MDIO/MDC
 * pins actually wired to an external PHY -- is not the *only* legitimate
 * one, unlike, say, ep_lin.h's single-wire bus, which has no equivalent
 * "useful without a bus at all" reading.
 */
#ifndef RCP_EP_MDIO_H
#define RCP_EP_MDIO_H

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

/* ── Addressing: Clause-22 MMD / Clause-45 MMS ───────────────────────────── */

typedef enum {
    RCP_EP_MDIO_CLAUSE_22 = 0, /* legacy 5-bit PHY addr / 5-bit reg addr */
    RCP_EP_MDIO_CLAUSE_45 = 1, /* extended MMD device addr + 16-bit reg addr */
} rcp_ep_mdio_clause_t;

#define RCP_EP_MDIO_PRTAD_MAX          ((uint8_t)0x1Fu)
#define RCP_EP_MDIO_DEVAD_MAX          ((uint8_t)0x1Fu)
#define RCP_EP_MDIO_CLAUSE22_REGAD_MAX ((uint16_t)0x1Fu)

typedef struct {
    rcp_ep_mdio_clause_t clause;
    uint8_t              prtad; /* 5-bit port/PHY address, 0..RCP_EP_MDIO_PRTAD_MAX */
    uint8_t              devad; /* 5-bit MMD device address; meaningful (and
                                    0..RCP_EP_MDIO_DEVAD_MAX) only when clause ==
                                    RCP_EP_MDIO_CLAUSE_45; must be 0 for
                                    RCP_EP_MDIO_CLAUSE_22 -- see the file header */
    uint16_t              regad; /* register address; 0..RCP_EP_MDIO_CLAUSE22_REGAD_MAX
                                     for RCP_EP_MDIO_CLAUSE_22, full 16-bit range for
                                     RCP_EP_MDIO_CLAUSE_45 */
} rcp_ep_mdio_addr_t;

/* True iff addr represents a physically meaningful MDIO register address
 * under its own clause -- see the file header. False for any other
 * `clause` value, any prtad/devad above its own 5-bit range, a nonzero
 * devad under RCP_EP_MDIO_CLAUSE_22, or a regad above
 * RCP_EP_MDIO_CLAUSE22_REGAD_MAX under RCP_EP_MDIO_CLAUSE_22. */
bool rcp_ep_mdio_addr_valid(rcp_ep_mdio_addr_t addr);

/* The next register address one step into a burst starting at regad, for
 * clause's own addressing width -- see the file header's burst-addressing
 * discussion. Wraps to 0 after RCP_EP_MDIO_CLAUSE22_REGAD_MAX
 * (RCP_EP_MDIO_CLAUSE_22) or after 0xFFFF (RCP_EP_MDIO_CLAUSE_45). Returns
 * regad unchanged for any other clause value. */
uint16_t rcp_ep_mdio_burst_next_regad(rcp_ep_mdio_clause_t clause, uint16_t regad);

/* Largest word_count this module's encoders/decoders accept in a single
 * request or response -- see the file header. */
#define RCP_EP_MDIO_MAX_BURST_WORDS ((size_t)512u)

/* ── Register-word packing: this module's own big-endian word layout ────────── */

/* Encodes word into out[0..2) big-endian (out[0] = high byte). */
void rcp_ep_mdio_word_encode(uint16_t word, uint8_t out[2]);

/* Decodes a big-endian 16-bit word from in[0..2) (in[0] = high byte). */
uint16_t rcp_ep_mdio_word_decode(const uint8_t in[2]);

/* Number of octets rcp_ep_mdio_pack_words() produces for word_count words:
 * word_count * 2. */
size_t rcp_ep_mdio_pack_len(size_t word_count);

/* Packs words[0..word_count) into a newly allocated big-endian byte buffer
 * of rcp_ep_mdio_pack_len(word_count) octets (rcp_ep_mdio_word_encode()
 * applied word by word). words may be NULL iff word_count == 0. Returns a
 * zeroed rcp_bytes_t (data=NULL) if word_count == 0 or on allocation
 * failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_mdio_pack_words(const uint16_t *words, size_t word_count);

/* True (with *out_word_count set to byte_len / 2) iff byte_len is even --
 * every packed word occupies exactly 2 octets, so an odd byte_len can
 * never hold a whole number of words. False (leaving *out_word_count
 * untouched) otherwise. */
bool rcp_ep_mdio_word_count_of(size_t byte_len, size_t *out_word_count);

/* Reads the word_index'th packed word out of data via
 * rcp_ep_mdio_word_decode() (i.e. from data[2*word_index..2*word_index+2)).
 * Caller is responsible for having already established, e.g. via
 * rcp_ep_mdio_word_count_of(), that word_index selects a whole word
 * actually present in data -- this function performs no bounds check of
 * its own. */
uint16_t rcp_ep_mdio_unpack_word_at(const uint8_t *data, size_t word_index);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first (and, deliberately, only)
                                               member -- see the file header */
} rcp_ep_mdio_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false). */
void rcp_ep_mdio_functional_cfg_init(rcp_ep_mdio_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (lifecycle.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file
 * header. Reuses, and never duplicates, that function's authorization
 * logic. */
bool rcp_ep_mdio_functional_cfg_writable(rcp_lifecycle_state_t state,
                                          rcp_lifecycle_writer_ctx_t writer);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_MDIO_OK                  = 0,
    RCP_EP_MDIO_ERR_SHORT_FRAME     = 1,
    RCP_EP_MDIO_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_MDIO_ERR_WRONG_BUS       = 3,
    RCP_EP_MDIO_ERR_WRONG_OP        = 4,
    RCP_EP_MDIO_ERR_BAD_ADDR        = 5,
    RCP_EP_MDIO_ERR_BAD_WORD_COUNT  = 6,
    RCP_EP_MDIO_ERR_ALLOC           = 7,
} rcp_ep_mdio_errc_t;

/* Human-readable message for an rcp_ep_mdio_errc_t value. Never returns NULL. */
const char *rcp_ep_mdio_strerror(rcp_ep_mdio_errc_t e);

/* ── Read request/response ─────────────────────────────────────────────────── */

/* Encodes an ACF_ABB read request addressed to byte_bus_id: a 7-byte
 * payload of addr's own clause/prtad/devad/regad fields followed by
 * word_count -- see the file header's wire-layout discussion. Returns a
 * zeroed rcp_bytes_t (data=NULL) if !rcp_ep_mdio_addr_valid(addr), if
 * word_count is 0 or exceeds RCP_EP_MDIO_MAX_BURST_WORDS, or on
 * allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                             rcp_ep_mdio_addr_t addr, size_t word_count,
                                             uint8_t transaction_num);

/* Decodes and validates an ACF-level MDIO read request from b[0..len).
 * Fails with RCP_EP_MDIO_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header, its declared payload length, or the 7-byte request
 * prefix; RCP_EP_MDIO_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_MDIO_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_MDIO_ERR_WRONG_OP if its op is not RCP_ACF_OP_READ;
 * RCP_EP_MDIO_ERR_BAD_ADDR if the decoded address fails
 * rcp_ep_mdio_addr_valid(); RCP_EP_MDIO_ERR_BAD_WORD_COUNT if the decoded
 * word_count is 0 or exceeds RCP_EP_MDIO_MAX_BURST_WORDS. On
 * RCP_EP_MDIO_OK, *out_addr, *out_word_count, and *out_transaction_num
 * are populated. */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    rcp_ep_mdio_addr_t *out_addr,
                                                    size_t *out_word_count,
                                                    uint8_t *out_transaction_num);

/* Encodes a read response carrying rcp_ep_mdio_pack_words(words,
 * word_count) as its payload, echoing transaction_num. Encoded as
 * ACF_ABB when timed is false; as ACF_GBB (with message_timestamp set to
 * timestamp, mtv = RCP_ACF_MTV_VALID) when timed is true -- see every
 * prior endpoint type's own timed/untimed convention. word_count may be
 * fewer than the originating request's own word_count (a short/partial
 * burst read) or 0 (words may be NULL in that case). Returns a zeroed
 * rcp_bytes_t (data=NULL) if word_count exceeds
 * RCP_EP_MDIO_MAX_BURST_WORDS or on allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_read_response(rcp_byte_bus_id_t byte_bus_id,
                                              const uint16_t *words, size_t word_count,
                                              uint8_t transaction_num, bool timed,
                                              uint64_t timestamp);

/* Decodes a read response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoder
 * above, since a response's encoding depends on the responding
 * endpoint's own timed/untimed choice). Fails with
 * RCP_EP_MDIO_ERR_SHORT_FRAME (frame too short for the applicable fixed
 * header or its declared payload length), RCP_EP_MDIO_ERR_WRONG_BUS
 * (byte_bus_id != expected_bus_id), or RCP_EP_MDIO_ERR_BAD_WORD_COUNT (an
 * odd payload length, or more than RCP_EP_MDIO_MAX_BURST_WORDS words). On
 * RCP_EP_MDIO_OK, *out_transaction_num is populated; *out_words_data /
 * *out_word_count are set to a *borrowed* view into b (not copied,
 * matching every prior endpoint type's own raw-payload convention) of the
 * packed word bytes -- rcp_ep_mdio_unpack_word_at() reads individual
 * words out of it; *out_timed and *out_timestamp report whether the
 * message was ACF_GBB with a valid (rcp_acf_gbb_is_timed()) timestamp,
 * and that timestamp's value (0 when !*out_timed). */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_read_response(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     const uint8_t **out_words_data,
                                                     size_t *out_word_count, bool *out_timed,
                                                     uint64_t *out_timestamp,
                                                     uint8_t *out_transaction_num);

/* ── Write request/response ────────────────────────────────────────────────── */

/* Encodes an ACF_ABB write request addressed to byte_bus_id: the 5-byte
 * address prefix (addr's own clause/prtad/devad/regad fields) followed by
 * rcp_ep_mdio_pack_words(words, word_count) -- see the file header's
 * wire-layout discussion. Returns a zeroed rcp_bytes_t (data=NULL) if
 * !rcp_ep_mdio_addr_valid(addr), if word_count is 0 or exceeds
 * RCP_EP_MDIO_MAX_BURST_WORDS, or on allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                              rcp_ep_mdio_addr_t addr, const uint16_t *words,
                                              size_t word_count, uint8_t transaction_num);

/* Decodes and validates an ACF-level MDIO write request from b[0..len).
 * Fails with RCP_EP_MDIO_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header, its declared payload length, or the 5-byte address
 * prefix; RCP_EP_MDIO_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_MDIO_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_MDIO_ERR_WRONG_OP if its op is not RCP_ACF_OP_WRITE;
 * RCP_EP_MDIO_ERR_BAD_ADDR if the decoded address fails
 * rcp_ep_mdio_addr_valid(); RCP_EP_MDIO_ERR_BAD_WORD_COUNT if the words
 * region's own byte length is odd, is 0, or represents more than
 * RCP_EP_MDIO_MAX_BURST_WORDS words. On RCP_EP_MDIO_OK, *out_addr and
 * *out_transaction_num are populated, and *out_words_data /
 * *out_word_count are set to a *borrowed* view into b (not copied) of the
 * packed word bytes following the address prefix. */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_write_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     rcp_ep_mdio_addr_t *out_addr,
                                                     const uint8_t **out_words_data,
                                                     size_t *out_word_count,
                                                     uint8_t *out_transaction_num);

/* Encodes a write response carrying rcp_ep_mdio_pack_words(accepted_words,
 * accepted_word_count) as its payload, echoing transaction_num -- the
 * words this endpoint actually accepted (possibly a prefix of the
 * originating request's own words on a partial burst, or 0/NULL for
 * nothing accepted, mirroring ep_uart.h's own accepted-prefix
 * convention). Encoded as ACF_ABB when timed is false; as ACF_GBB (with
 * message_timestamp set to timestamp, mtv = RCP_ACF_MTV_VALID) when timed
 * is true. Returns a zeroed rcp_bytes_t (data=NULL) if
 * accepted_word_count exceeds RCP_EP_MDIO_MAX_BURST_WORDS or on
 * allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_write_response(rcp_byte_bus_id_t byte_bus_id,
                                               const uint16_t *accepted_words,
                                               size_t accepted_word_count,
                                               uint8_t transaction_num, bool timed,
                                               uint64_t timestamp);

/* Decodes a write response from either an ACF_ABB or ACF_GBB message
 * (peeked, same reasoning as rcp_ep_mdio_decode_read_response()). Fails
 * with RCP_EP_MDIO_ERR_SHORT_FRAME (frame too short for the applicable
 * fixed header or its declared payload length), RCP_EP_MDIO_ERR_WRONG_BUS
 * (byte_bus_id != expected_bus_id), or RCP_EP_MDIO_ERR_BAD_WORD_COUNT (an
 * odd payload length, or more than RCP_EP_MDIO_MAX_BURST_WORDS words). On
 * RCP_EP_MDIO_OK, *out_transaction_num is populated; *out_words_data /
 * *out_word_count are set to a *borrowed* view into b (not copied) of the
 * accepted packed word bytes; *out_timed and *out_timestamp report
 * whether the message was ACF_GBB with a valid (rcp_acf_gbb_is_timed())
 * timestamp, and that timestamp's value (0 when !*out_timed). */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_write_response(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      const uint8_t **out_words_data,
                                                      size_t *out_word_count, bool *out_timed,
                                                      uint64_t *out_timestamp,
                                                      uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_MDIO_H */
