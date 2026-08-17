/* SPDX-License-Identifier: MPL-2.0 */
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

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-MDIO-020
//cfusa:req REQ-MDIO-021
//cfusa:req REQ-MDIO-022
//cfusa:req REQ-MDIO-023
//cfusa:req REQ-MDIO-024
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
 * FIXED 2026-08-12 (REQ-MDIO-021, see the "mdio_mode" section below for the
 * full investigation and its documented assumptions): every request's
 * payload now begins with a 1-byte `mdio_mode` octet (bits[7:2] reserved/0,
 * bits[1:0] = mode) -- the rest of this section describes the payload
 * *following* that new leading octet, unchanged from before this fix.
 *
 * A read request's remaining payload is a fixed 7-byte prefix -- 1 byte
 * `clause`, 1 byte `prtad`, 1 byte `devad`, a big-endian 2-byte `regad`,
 * and a big-endian 2-byte `word_count` -- and no further bytes (nothing to
 * read yet; only the reply carries data). A write request's remaining
 * payload is the same 5-byte address prefix (`clause`/`prtad`/`devad`/
 * `regad`, with `word_count` this time implied by the payload's own
 * remaining length rather than encoded again) followed by `word_count`
 * packed big-endian 16-bit words -- see rcp_ep_mdio_pack_words()/_word_count_of()/
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
 * rcp_ep_mdio_functional_cfg_t adds *nothing type-specific* of its own
 * beyond composing regmap.h's rcp_regmap_ep_functional_cfg_t. This is a
 * deliberate, documented "nothing more to add" finding, not an oversight:
 * per extraction §5.10-5.13, this endpoint type's register-map footprint
 * is fully covered by the common enable/clear/CRC/timestamp/suppress-
 * response flags every endpoint type already shares, with no MDIO-
 * specific runtime-adjustable register of its own -- TC18 §13.7.13.2
 * itself opens with "The MDIO EP does not have any configurable
 * parameters." Consequently there are no rcp_ep_mdio_set_*() mutators in
 * this file at all: no endpoint type in this codebase exposes a setter
 * for the common block's own fields either (those are the generic
 * register-map layer's job, not any one endpoint type's), so with no
 * *configurable* fields of its own to add, this module has nothing left
 * to set. rcp_ep_mdio_functional_cfg_init() and
 * rcp_ep_mdio_functional_cfg_writable() are still provided, matching every
 * other endpoint type's own init/writable pair, purely for that
 * consistency -- rcp_ep_mdio_functional_cfg_writable() is, like every
 * other endpoint type's own version, a thin, named wrapper over
 * lifecycle.h's rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W),
 * reusing rather than duplicating that authorization logic. Anyone
 * extending this file later who finds themselves reaching for an
 * MDIO-specific *configurable* field should stop and re-read this
 * paragraph first -- it is this milestone's documented scope, the same
 * "roadmap and spec both agree there is simply nothing to add" position
 * ep_can.h's own file header already states for its own missing trigger
 * table (see below).
 *
 * ── The EP_func register block IS still exposed, though nothing in it is
 *    configurable ──────────────────────────────────────────────────────────
 *
 * FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-MDIO-020/023):
 * "no configurable parameters" (above) describes what a *write* can
 * change -- it says nothing about whether the block is *readable*. TC18
 * §13.7.13.2 Table 59 still fixes a real register block every endpoint
 * type exposes via evt[2:0]=111b (extraction §12.7.1): mdio_ep_len
 * (0x0000, R), the reserved octet (0x0001, R), the common enable&clr/
 * options octets (0x0002/0x0003, shared with every other endpoint type),
 * and mdio_ep_status (16 bit, R/W, "To be defined" -- the specification
 * itself does not yet define this register's own contents).
 *
 * A genuine address-collision editorial defect, the FIFTH this audit has
 * found (after ep_pwm.h's/ep_gpio.h's/ep_i2c.h's/ep_iseled.h's own):
 * mdio_ep_status is printed at relative address 0x0002 in Table 59 --
 * identical to mdio_ep_enable&clr, separately printed at the same
 * address. Unlike every other endpoint type's own table, Table 59 prints
 * no base_clk row at all (consistent with "MDIO EP does not have any
 * configurable parameters" -- there is genuinely no system clock
 * register to expose here), so the minimal, table-literal-following fix
 * is simply to place mdio_ep_status at the next unclaimed offset after
 * options: 0x0004, one register width narrower than every other endpoint
 * type's common prefix (which reserves 0x0004-0x0005 for a base_clk row
 * this table never lists). RCP_EP_MDIO_EP_FUNC_LEN = 0x0006.
 *
 * rcp_ep_mdio_functional_cfg_t gains ep_status (uint16_t) -- the ONLY
 * field this module adds, still nothing "configurable" in the sense the
 * section above means (no rcp_ep_mdio_set_ep_status() mutator; the
 * register-block's own generic addressed-write mechanism,
 * rcp_ep_mdio_apply_reconfig(), is the only way to change it, exactly
 * like every register-block field in every other endpoint type). New
 * rcp_ep_mdio_render_registers()/_apply_reconfig()/_reconfig_strerror()/
 * _encode_reconfig_request() mirror the established pattern exactly.
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

/* ── mdio_mode: TC18 §13.7.13.3 Figure 43/Table 60, REQ-MDIO-021 ────────────
 *
 * DOCUMENTED ASSUMPTION, user-approved 2026-08-12, see the spec-defects
 * report items 25/26/55 (TC18_spec_defects_report.md, canonical path
 * /Users/matt/Documents/Coding/SoundMatt/, NOT in this repo) for the full
 * primary-source investigation this is based on:
 *
 * Table 60's own 2-bit mdio_mode value list is genuinely broken in the
 * spec itself (confirmed on the rendered PDF page image, not an
 * extraction artifact): `01b` is assigned to BOTH "MMD, single word
 * access" and "MMD, multiple byte access", and `00b` is never assigned
 * to anything (item 25). This module assigns 00b to MMD-single, matching
 * the only reading that gives the field's own natural 00/01/10/11
 * sequence four DISTINCT meanings instead of three: 00b mirrors 10b/11b's
 * own single-vs-multi pairing pattern, and is the only assignment that
 * doesn't leave one bit pattern with two conflicting meanings.
 *
 * A SECOND, larger gap surfaced while designing a conformant encoding
 * (item 55): neither Figure 43 nor Table 60 gives mdio_address an
 * explicit bit width, and "MMS" (Memory Mapped Space) is plausibly the
 * OPEN Alliance 10BASE-T1x SPI protocol's own distinct addressing
 * concept, not IEEE 802.3 Clause 22/45 addressing at all -- a THIRD
 * addressing scheme this module has no verified primary-source basis to
 * design a wire layout for. Rather than inventing that scheme's own byte
 * layout too, this module's own fix is deliberately scoped narrower: it
 * adds the missing mdio_mode field to the wire (closing REQ-MDIO-021's
 * own literal "no mdio_mode field at all" complaint) for the ONE
 * addressing family this module already correctly implements and has
 * always supported -- MMD, i.e. rcp_ep_mdio_addr_t's own Clause-22/
 * Clause-45 addressing (both clauses map to TC18's own "MMD" mode value,
 * since Table 60's own MMD/MMS axis is orthogonal to IEEE 802.3's own
 * Clause-22-vs-45 axis, and this module has no verified basis to further
 * subdivide "MMD" by clause). MMS is a real value mdio_mode CAN decode to
 * (a peer may legitimately send it) but this module cannot yet interpret
 * -- rcp_ep_mdio_decode_read_request()/_decode_write_request() reject an
 * incoming MMS-mode request with the new RCP_EP_MDIO_ERR_UNSUPPORTED_MMS
 * error rather than silently misreading its address field as if it were
 * MMD-shaped. REQ-MDIO-021 flips from not-implemented to PARTIAL, not
 * IMPLEMENTED, for exactly this reason -- MMS support remains a real,
 * precisely-scoped, still-open remainder, not a silently-dropped case.
 *
 * mdio_mode is encoded as a new leading octet at payload offset 0 (bits
 * [7:2] reserved/0, bits[1:0] = mode), placed BEFORE the existing
 * clause/prtad/devad/regad(/word_count) address prefix, which is
 * otherwise completely UNCHANGED -- only shifted one byte later on the
 * wire. This module's own encoders always derive MMD_SINGLE vs MMD_MULTI
 * from word_count (1 vs >1), the same distinction word_count already
 * made before this fix; mdio_mode is now genuinely present on the wire
 * too, not merely implied.
 *
 * REQ-MDIO-022 (16 vs 32-bit data width for MMS0/MMS1) stayed entirely
 * NOT IMPLEMENTED as of this fix: it was unreachable without MMS
 * addressing existing at all, which this fix deliberately did not add.
 * See the "MMS addressing" section below for its own later fix. */
typedef enum {
    RCP_EP_MDIO_MODE_MMD_SINGLE = 0, /* 00b -- ASSUMPTION, see above */
    RCP_EP_MDIO_MODE_MMD_MULTI  = 1, /* 01b */
    RCP_EP_MDIO_MODE_MMS_SINGLE = 2, /* 10b -- see "MMS addressing" below */
    RCP_EP_MDIO_MODE_MMS_MULTI  = 3, /* 11b -- see "MMS addressing" below */
} rcp_ep_mdio_mode_t;

/* True iff word_count selects RCP_EP_MDIO_MODE_MMD_MULTI (word_count > 1)
 * rather than RCP_EP_MDIO_MODE_MMD_SINGLE (word_count == 1) -- the same
 * single-vs-burst distinction this module's own word_count parameter
 * already made before this fix, now also reflected in the wire-encoded
 * mdio_mode octet. Meaningless for word_count == 0, which every encoder
 * below already rejects before this would be consulted. */
rcp_ep_mdio_mode_t rcp_ep_mdio_mode_for_word_count(size_t word_count);

/* True iff mode is RCP_EP_MDIO_MODE_MMS_SINGLE or _MMS_MULTI -- the two
 * mdio_mode values belonging to the *_mms_* function family (below)
 * rather than the MMD family above. Despite its name (kept for source
 * compatibility -- see the "MMS addressing" section below for why MMS is
 * no longer actually unsupported by this module as a whole), still
 * exactly what it always meant: these two mode values are NOT ones the
 * MMD-family functions above can encode or interpret -- a caller
 * decoding an incoming frame of unknown mode should still check this
 * first and route to the *_mms_* family instead, exactly as before. */
bool rcp_ep_mdio_mode_is_unsupported_mms(rcp_ep_mdio_mode_t mode);

/* ── MMS addressing: REQ-MDIO-022/024, FIXED 2026-08-13 ──────────────────────
 *
 * DOCUMENTED ASSUMPTION, user-approved 2026-08-13, informed by a real
 * external specification this time (not invented from nothing): the
 * OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface specification, V1.1
 * (referenced here by name and section only; no prose or figure from it
 * is reproduced), stored at
 * /Users/matt/Documents/Coding/SoundMatt/OPEN_Alliance_10BASE-T1x_MAC-PHY_Serial_Interface_V1.1.pdf
 * -- NOT in this repo, NOT the confidential TC18 document, a publicly
 * available OPEN Alliance specification found via web search and cited
 * as this fix's own external basis.
 *
 * TWO DISTINCT THINGS this fix relies on, with two very different
 * confidence levels:
 *
 * 1. TC18-LITERAL, not an assumption at all (REQ-MDIO-022's own text):
 *    Table 60 states directly that MMS0 and MMS1 use 32-bit data fields
 *    and every other MMS uses 16-bit data fields.
 *    rcp_ep_mdio_mms_uses_32bit_words() below implements exactly this
 *    stated rule -- nothing here is inferred.
 *
 * 2. AN ASSUMPTION (REQ-MDIO-024, new this fix): neither Figure 43 nor
 *    Table 60 gives TC18's own `mdio_address` field a bit width or an
 *    internal layout for MMS mode (TC18_spec_defects_report.md item 55,
 *    still open, still worth the committee's attention -- this fix does
 *    not resolve item 55, it works around it with a documented guess).
 *    The external OA-SPI spec's own control command header (its own
 *    §7.4.1 Table 4) gives the REAL protocol this "MMS" terminology is
 *    almost certainly borrowed from a well-defined shape: a 4-bit MMS
 *    selector (0-15, its own §9.1 Table 6) immediately followed by a
 *    16-bit ADDR field -- 20 bits total. This module ASSUMES TC18's own
 *    `mdio_address` field packs the same two sub-fields in the same
 *    order for its MMS mode, and represents them on ITS OWN wire (not
 *    OA-SPI's -- RCP is not literally SPI) as two whole octets --
 *    `mms` (a full byte, valid range 0..RCP_EP_MDIO_MMS_MAX) then a
 *    big-endian 16-bit `addr` -- the same "give every address sub-field
 *    its own whole octet rather than bit-packing it" convention the
 *    MMD prefix above already uses for its own 5-bit prtad/devad
 *    fields. THIS PART COULD BE WRONG: if the real committee intent
 *    differs (a different field order, a different MMS width, or
 *    `mdio_address` meaning something else for MMS entirely), a peer
 *    built against this assumption will not interoperate with one built
 *    against the real (still unpublished, as far as this fix's own
 *    research found) TC18 committee resolution of item 55. This is why
 *    REQ-MDIO-024 is catalogued as PARTIAL, not IMPLEMENTED, even though
 *    the code path fully exists and is fully tested against ITS OWN
 *    assumed layout.
 *
 * One further honesty note: the external OA-SPI spec's own Table 6
 * marks MMS 1's own register width "implementation dependent", not
 * literally 32-bit -- TC18's own Table 60 is making an RCP-specific
 * overriding convention here (MMS0 AND MMS1 both 32-bit for THIS
 * protocol's purposes), not simply quoting the external spec verbatim.
 * That part is still TC18-literal (see point 1 above); only the
 * ADDRESSING shape (point 2) is this module's own assumption.
 *
 * Everything below in this section is purely additive: the existing MMD
 * family (rcp_ep_mdio_addr_t and every read/write encode/decode function
 * above) is completely unchanged, byte-for-byte, by this fix. */

#define RCP_EP_MDIO_MMS_MAX ((uint8_t)0x0Fu) /* 4-bit MMS selector, 0..15,
                                                 OA-SPI spec Table 6 */

typedef struct {
    uint8_t  mms;  /* Memory Map Selector, 0..RCP_EP_MDIO_MMS_MAX */
    uint16_t addr; /* register address within the selected memory map */
} rcp_ep_mdio_mms_addr_t;

/* True iff addr.mms <= RCP_EP_MDIO_MMS_MAX. addr.addr's full 16-bit range
 * is always valid (no MMS-specific narrower range is known -- see the
 * file header's own honesty note above). */
bool rcp_ep_mdio_mms_addr_valid(rcp_ep_mdio_mms_addr_t addr);

/* True iff mms is 0 or 1 -- REQ-MDIO-022's own TC18-literal rule (Table
 * 60): MMS0 and MMS1 use 32-bit data fields; every other mms (2..15)
 * uses 16-bit data fields. Meaningless (but well-defined: false) for
 * mms > RCP_EP_MDIO_MMS_MAX -- callers should have already validated
 * mms via rcp_ep_mdio_mms_addr_valid() first. */
bool rcp_ep_mdio_mms_uses_32bit_words(uint8_t mms);

/* The next register address one step into an MMS burst starting at addr
 * -- the same one-step-advance idiom rcp_ep_mdio_burst_next_regad()
 * provides for the MMD family, at MMS addressing's own full 16-bit
 * width (wraps at 0xFFFF). This module's own design choice, like its
 * MMD counterpart -- not itself derived from either spec. */
uint16_t rcp_ep_mdio_mms_burst_next_addr(uint16_t addr);

/* True iff word_count selects RCP_EP_MDIO_MODE_MMS_MULTI (word_count > 1)
 * rather than RCP_EP_MDIO_MODE_MMS_SINGLE (word_count == 1) -- the MMS
 * family's own counterpart to rcp_ep_mdio_mode_for_word_count() above. */
rcp_ep_mdio_mode_t rcp_ep_mdio_mms_mode_for_word_count(size_t word_count);

/* ── MMS register-word packing: 16- or 32-bit per rcp_ep_mdio_mms_uses_32bit_words() ──
 *
 * Every word is represented in memory as a uint32_t regardless of its
 * own wire width (the high 16 bits are simply unused/zero for a 16-bit
 * MMS) -- one packing family instead of two width-specific ones, since
 * the width is always a pure function of the already-known `mms` value,
 * not a second independent parameter a caller could get wrong. */

/* Encodes word into out[0..4) big-endian (out[0] = highest byte). */
void rcp_ep_mdio_word32_encode(uint32_t word, uint8_t out[4]);

/* Decodes a big-endian 32-bit word from in[0..4) (in[0] = highest byte). */
uint32_t rcp_ep_mdio_word32_decode(const uint8_t in[4]);

/* Number of octets rcp_ep_mdio_mms_pack_words() produces for word_count
 * words at mms's own width: word_count * (4 or 2). */
size_t rcp_ep_mdio_mms_pack_len(uint8_t mms, size_t word_count);

/* Packs words[0..word_count) into a newly allocated big-endian byte
 * buffer of rcp_ep_mdio_mms_pack_len(mms, word_count) octets, at mms's
 * own word width (rcp_ep_mdio_word32_encode() applied word by word for a
 * 32-bit mms; the low 16 bits of each word via rcp_ep_mdio_word_encode()
 * otherwise). words may be NULL iff word_count == 0. Returns a zeroed
 * rcp_bytes_t (data=NULL) if word_count == 0 or on allocation failure.
 * Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_mdio_mms_pack_words(uint8_t mms, const uint32_t *words, size_t word_count);

/* True (with *out_word_count set) iff byte_len is an exact multiple of
 * mms's own word width (4 or 2 octets) -- the MMS family's own
 * counterpart to rcp_ep_mdio_word_count_of(). */
bool rcp_ep_mdio_mms_word_count_of(uint8_t mms, size_t byte_len, size_t *out_word_count);

/* Reads the word_index'th packed word out of data at mms's own width,
 * zero-extended to uint32_t for a 16-bit mms. No bounds check of its
 * own -- the MMS family's own counterpart to
 * rcp_ep_mdio_unpack_word_at(). */
uint32_t rcp_ep_mdio_mms_unpack_word_at(uint8_t mms, const uint8_t *data, size_t word_index);

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
                                               first member -- see the file
                                               header */
    uint16_t                       ep_status; /* 0x0004, R/W; see the file
                                                  header -- the only field
                                                  this module adds, and not
                                                  "configurable" in the
                                                  sense the file header's
                                                  own "no type-specific
                                                  functional config"
                                                  section means */
} rcp_ep_mdio_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; ep_status 0). */
void rcp_ep_mdio_functional_cfg_init(rcp_ep_mdio_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (lifecycle.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file
 * header. Reuses, and never duplicates, that function's authorization
 * logic. */
bool rcp_ep_mdio_functional_cfg_writable(rcp_lifecycle_state_t state,
                                          rcp_lifecycle_writer_ctx_t writer);

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

/* Relative octet offsets of the registers making up an MDIO endpoint's own
 * EP_func block, at the widths TC18 §13.7.13.2 Table 59 assigns them,
 * corrected for the address-collision editorial defect -- see the file
 * header. Note there is no base_clk row here, unlike every other endpoint
 * type's own common prefix -- Table 59 genuinely defines none. */
#define RCP_EP_MDIO_REG_EP_LEN        ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_MDIO_REG_RESERVED_01   ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_MDIO_REG_EP_ENABLE_CLR ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_MDIO_REG_EP_OPTIONS    ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_MDIO_REG_EP_STATUS     ((uint16_t)0x0004u) /* 16 bit, R/W */

/* The block's own length in octets -- one past the last assigned offset. */
#define RCP_EP_MDIO_EP_FUNC_LEN       ((uint16_t)0x0006u)

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- see
 * RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN's own identical note (ep_pwm.h). */
#define RCP_EP_MDIO_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_MDIO_RECONFIG_OK               = 0,
    RCP_EP_MDIO_RECONFIG_ERR_SHORT        = 1, /* payload carries no address
                                                    prefix, or an address
                                                    prefix with no data
                                                    octet after it */
    RCP_EP_MDIO_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data
                                                    length exceeds
                                                    RCP_EP_MDIO_EP_FUNC_LEN
                                                    -- the whole write is
                                                    ignored, per the
                                                    specification's own
                                                    rule */
} rcp_ep_mdio_reconfig_errc_t;

/* Human-readable message for an rcp_ep_mdio_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_mdio_reconfig_strerror(rcp_ep_mdio_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_MDIO_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_mdio_apply_reconfig()'s own parse step. */
void rcp_ep_mdio_render_registers(const rcp_ep_mdio_functional_cfg_t *cfg,
                                   uint8_t out[RCP_EP_MDIO_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is an
 * addressed write into this endpoint's own EP_func block -- a 16-bit
 * big-endian relative start address followed by the configuration data
 * octets to write from that address onward (extraction §3.7.1). See
 * rcp_ep_pwm_out_apply_reconfig()'s own doc comment (ep_pwm.h) for the
 * read-only-offset-skipping, octet-granularity-patch, and
 * out-of-range-ignores-the-whole-write rules -- identical here.
 *
 * A caller routing a decoded request here is responsible for having
 * checked that evt[2:0] really was 111b, e.g. via
 * !rcp_acf_evt_row2_is_plain(). */
rcp_ep_mdio_reconfig_errc_t
rcp_ep_mdio_apply_reconfig(rcp_ep_mdio_functional_cfg_t *cfg,
                            const uint8_t *payload, size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_mdio_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                 uint16_t start_address,
                                                 const uint8_t *data, size_t data_len,
                                                 uint8_t transaction_num);

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
    /* evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
     * plain (non-configuration) request in MDIO's endpoint-type row --
     * caller shall respond with error code UNSUPPORTED_CMD (see
     * rcp_acf_evt_row2_is_plain()). */
    RCP_EP_MDIO_ERR_BAD_EVT         = 8,
    /* The decoded mdio_mode octet is RCP_EP_MDIO_MODE_MMS_SINGLE or
     * _MMS_MULTI -- REQ-MDIO-021's own still-open remainder, see this
     * file's own "mdio_mode" section above. This module recognizes the
     * value on the wire but has no verified basis to interpret an MMS
     * request's own address/data shape, so it fails closed here rather
     * than misreading the payload as if it were MMD-shaped. */
    RCP_EP_MDIO_ERR_UNSUPPORTED_MMS = 9,
    /* rcp_ep_mdio_mms_addr_valid() failed for the decoded MMS address --
     * see the "MMS addressing" section above. */
    RCP_EP_MDIO_ERR_BAD_MMS_ADDR    = 10,
    /* The decoded mdio_mode octet is RCP_EP_MDIO_MODE_MMD_SINGLE or
     * _MMD_MULTI -- returned by the *_mms_* decoder family (below) when
     * handed a frame using the OTHER (MMD) addressing family. Use
     * rcp_ep_mdio_decode_read_request()/_decode_write_request() (the MMD
     * family, above) instead -- the mirror image of
     * RCP_EP_MDIO_ERR_UNSUPPORTED_MMS. */
    RCP_EP_MDIO_ERR_WRONG_MDIO_MODE = 11,
} rcp_ep_mdio_errc_t;

/* Human-readable message for an rcp_ep_mdio_errc_t value. Never returns NULL. */
const char *rcp_ep_mdio_strerror(rcp_ep_mdio_errc_t e);

/* ── Read request/response ─────────────────────────────────────────────────── */

/* Encodes an ACF_ABB read request addressed to byte_bus_id: a new leading
 * mdio_mode octet (REQ-MDIO-021, see the file header's own "mdio_mode"
 * section -- always MMD_SINGLE or MMD_MULTI, derived from word_count via
 * rcp_ep_mdio_mode_for_word_count()) followed by the existing 7-byte
 * payload of addr's own clause/prtad/devad/regad fields and word_count,
 * unchanged apart from shifting one byte later -- see the file header's
 * wire-layout discussion. Returns a zeroed rcp_bytes_t (data=NULL) if
 * !rcp_ep_mdio_addr_valid(addr), if word_count is 0 or exceeds
 * RCP_EP_MDIO_MAX_BURST_WORDS, or on allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                             rcp_ep_mdio_addr_t addr, size_t word_count,
                                             uint8_t transaction_num);

/* Decodes and validates an ACF-level MDIO read request from b[0..len).
 * Fails with RCP_EP_MDIO_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header, its declared payload length, or the 8-byte (mdio_mode
 * octet + 7-byte address/word_count) request prefix;
 * RCP_EP_MDIO_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_MDIO_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_MDIO_ERR_WRONG_OP if its op is not RCP_ACF_OP_READ;
 * RCP_EP_MDIO_ERR_UNSUPPORTED_MMS if the decoded mdio_mode octet is an MMS
 * value (see the file header's own "mdio_mode" section);
 * RCP_EP_MDIO_ERR_BAD_ADDR if the decoded address fails
 * rcp_ep_mdio_addr_valid(); RCP_EP_MDIO_ERR_BAD_WORD_COUNT if the decoded
 * word_count is 0 or exceeds RCP_EP_MDIO_MAX_BURST_WORDS;
 * RCP_EP_MDIO_ERR_BAD_EVT if its evt[2:0] is not 0b000
 * (rcp_acf_evt_row2_is_plain(), TC18 §13.5 Table 33 -- the caller shall
 * respond with error code UNSUPPORTED_CMD). On
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

/* Encodes an ACF_ABB write request addressed to byte_bus_id: a new leading
 * mdio_mode octet (REQ-MDIO-021, see the file header's own "mdio_mode"
 * section -- always MMD_SINGLE or MMD_MULTI, derived from word_count via
 * rcp_ep_mdio_mode_for_word_count()) followed by the existing 5-byte
 * address prefix (addr's own clause/prtad/devad/regad fields) and
 * rcp_ep_mdio_pack_words(words, word_count), unchanged apart from
 * shifting one byte later -- see the file header's wire-layout
 * discussion. Returns a zeroed rcp_bytes_t (data=NULL) if
 * !rcp_ep_mdio_addr_valid(addr), if word_count is 0 or exceeds
 * RCP_EP_MDIO_MAX_BURST_WORDS, or on allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                              rcp_ep_mdio_addr_t addr, const uint16_t *words,
                                              size_t word_count, uint8_t transaction_num);

/* Decodes and validates an ACF-level MDIO write request from b[0..len).
 * Fails with RCP_EP_MDIO_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header, its declared payload length, or the 6-byte (mdio_mode
 * octet + 5-byte address prefix) request prefix;
 * RCP_EP_MDIO_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_MDIO_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_MDIO_ERR_WRONG_OP if its op is not RCP_ACF_OP_WRITE;
 * RCP_EP_MDIO_ERR_UNSUPPORTED_MMS if the decoded mdio_mode octet is an MMS
 * value (see the file header's own "mdio_mode" section);
 * RCP_EP_MDIO_ERR_BAD_ADDR if the decoded address fails
 * rcp_ep_mdio_addr_valid(); RCP_EP_MDIO_ERR_BAD_WORD_COUNT if the words
 * region's own byte length is odd, is 0, or represents more than
 * RCP_EP_MDIO_MAX_BURST_WORDS words; RCP_EP_MDIO_ERR_BAD_EVT if its
 * evt[2:0] is not 0b000 (rcp_acf_evt_row2_is_plain(), TC18 §13.5
 * Table 33 -- the caller shall respond with error code UNSUPPORTED_CMD).
 * On RCP_EP_MDIO_OK, *out_addr and
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

/* ── MMS read request/response: REQ-MDIO-022/024 ─────────────────────────────
 *
 * The MMS family's own counterpart to the MMD read family above -- see
 * the "MMS addressing" section for the wire layout and its documented
 * assumption. Payload: a leading mdio_mode octet (always MMS_SINGLE or
 * MMS_MULTI, derived from word_count via
 * rcp_ep_mdio_mms_mode_for_word_count()), then a 3-byte address prefix
 * (`mms`, one octet; `addr`, big-endian 16-bit), then (read request
 * only) a big-endian 2-byte word_count -- the same "own word_count
 * field, not spec-derived" choice the MMD family already makes, applied
 * uniformly here too. */

/* Returns a zeroed rcp_bytes_t (data=NULL) if !rcp_ep_mdio_mms_addr_valid(addr),
 * if word_count is 0 or exceeds RCP_EP_MDIO_MAX_BURST_WORDS, or on
 * allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_mms_read_request(rcp_byte_bus_id_t byte_bus_id,
                                                 rcp_ep_mdio_mms_addr_t addr, size_t word_count,
                                                 uint8_t transaction_num);

/* Fails with RCP_EP_MDIO_ERR_SHORT_FRAME/_BAD_MSG_TYPE/_WRONG_BUS/_WRONG_OP/
 * _BAD_EVT the same way rcp_ep_mdio_decode_read_request() does;
 * RCP_EP_MDIO_ERR_WRONG_MDIO_MODE if the decoded mdio_mode octet belongs
 * to the MMD family instead (use rcp_ep_mdio_decode_read_request());
 * RCP_EP_MDIO_ERR_BAD_MMS_ADDR if the decoded address fails
 * rcp_ep_mdio_mms_addr_valid(); RCP_EP_MDIO_ERR_BAD_WORD_COUNT if the
 * decoded word_count is 0 or exceeds RCP_EP_MDIO_MAX_BURST_WORDS. On
 * RCP_EP_MDIO_OK, *out_addr, *out_word_count, and *out_transaction_num
 * are populated. */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_mms_read_request(const uint8_t *b, size_t len,
                                                        rcp_byte_bus_id_t expected_bus_id,
                                                        rcp_ep_mdio_mms_addr_t *out_addr,
                                                        size_t *out_word_count,
                                                        uint8_t *out_transaction_num);

/* Encodes a read response carrying rcp_ep_mdio_mms_pack_words(mms, words,
 * word_count) as its payload -- the caller supplies mms (not carried in
 * the response payload itself; the caller already knows it from the
 * originating request, the same way transaction_num correlation already
 * works for the MMD family). Otherwise identical to
 * rcp_ep_mdio_encode_read_response() (timed/untimed ACF_GBB/ACF_ABB
 * choice, partial-burst word_count, RCP_EP_MDIO_MAX_BURST_WORDS bound). */
rcp_bytes_t rcp_ep_mdio_encode_mms_read_response(rcp_byte_bus_id_t byte_bus_id, uint8_t mms,
                                                  const uint32_t *words, size_t word_count,
                                                  uint8_t transaction_num, bool timed,
                                                  uint64_t timestamp);

/* Decodes an MMS read response. mms is a caller-supplied INPUT (see
 * rcp_ep_mdio_encode_mms_read_response()'s own note) used only to
 * validate the payload's own byte length against mms's own word width
 * via rcp_ep_mdio_mms_word_count_of() -- otherwise identical to
 * rcp_ep_mdio_decode_read_response(). *out_words_data / *out_word_count
 * are a *borrowed* view into b; rcp_ep_mdio_mms_unpack_word_at(mms, ...)
 * reads individual words out of it. */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_mms_read_response(const uint8_t *b, size_t len,
                                                         rcp_byte_bus_id_t expected_bus_id,
                                                         uint8_t mms,
                                                         const uint8_t **out_words_data,
                                                         size_t *out_word_count, bool *out_timed,
                                                         uint64_t *out_timestamp,
                                                         uint8_t *out_transaction_num);

/* ── MMS write request/response: REQ-MDIO-022/024 ────────────────────────────
 *
 * The MMS family's own counterpart to the MMD write family above. Write
 * request payload: mdio_mode octet, then the 3-byte address prefix, then
 * rcp_ep_mdio_mms_pack_words(addr.mms, words, word_count) -- word_count
 * implied by the payload's own remaining length at addr.mms's own word
 * width, the same "not encoded again on a write" MMD convention. */

/* Returns a zeroed rcp_bytes_t (data=NULL) if !rcp_ep_mdio_mms_addr_valid(addr),
 * if word_count is 0 or exceeds RCP_EP_MDIO_MAX_BURST_WORDS, or on
 * allocation failure. */
rcp_bytes_t rcp_ep_mdio_encode_mms_write_request(rcp_byte_bus_id_t byte_bus_id,
                                                  rcp_ep_mdio_mms_addr_t addr,
                                                  const uint32_t *words, size_t word_count,
                                                  uint8_t transaction_num);

/* Fails the same way rcp_ep_mdio_decode_mms_read_request() does (with
 * RCP_EP_MDIO_ERR_WRONG_OP instead of a read-op check, matching
 * rcp_ep_mdio_decode_write_request()'s own convention).
 * RCP_EP_MDIO_ERR_BAD_WORD_COUNT covers a words-region byte length that
 * is not a whole multiple of the decoded addr.mms's own word width, is
 * 0, or represents more than RCP_EP_MDIO_MAX_BURST_WORDS words. On
 * RCP_EP_MDIO_OK, *out_addr and *out_transaction_num are populated, and
 * *out_words_data / *out_word_count are a *borrowed* view into b of the
 * packed word bytes following the address prefix (not copied) --
 * rcp_ep_mdio_mms_unpack_word_at(out_addr->mms, ...) reads individual
 * words out of it. */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_mms_write_request(const uint8_t *b, size_t len,
                                                         rcp_byte_bus_id_t expected_bus_id,
                                                         rcp_ep_mdio_mms_addr_t *out_addr,
                                                         const uint8_t **out_words_data,
                                                         size_t *out_word_count,
                                                         uint8_t *out_transaction_num);

/* Encodes a write response carrying rcp_ep_mdio_mms_pack_words(mms,
 * accepted_words, accepted_word_count) as its payload -- mms is a
 * caller-supplied input, the same convention as
 * rcp_ep_mdio_encode_mms_read_response(). Otherwise identical to
 * rcp_ep_mdio_encode_write_response(). */
rcp_bytes_t rcp_ep_mdio_encode_mms_write_response(rcp_byte_bus_id_t byte_bus_id, uint8_t mms,
                                                   const uint32_t *accepted_words,
                                                   size_t accepted_word_count,
                                                   uint8_t transaction_num, bool timed,
                                                   uint64_t timestamp);

/* Decodes an MMS write response -- mms is a caller-supplied input, the
 * same convention as rcp_ep_mdio_decode_mms_read_response(). Otherwise
 * identical to rcp_ep_mdio_decode_write_response(). */
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_mms_write_response(const uint8_t *b, size_t len,
                                                          rcp_byte_bus_id_t expected_bus_id,
                                                          uint8_t mms,
                                                          const uint8_t **out_words_data,
                                                          size_t *out_word_count, bool *out_timed,
                                                          uint64_t *out_timestamp,
                                                          uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_MDIO_H */
