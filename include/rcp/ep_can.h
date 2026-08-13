/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-CANEP-001
//cfusa:req REQ-CANEP-002
//cfusa:req REQ-CANEP-003
//cfusa:req REQ-CANEP-004
//cfusa:req REQ-CANEP-005
//cfusa:req REQ-CANEP-006
//cfusa:req REQ-CANEP-007
//cfusa:req REQ-CANEP-008
//cfusa:req REQ-CANEP-009
//cfusa:req REQ-CANEP-010
//cfusa:req REQ-CANEP-011
//cfusa:req REQ-CANEP-012
//cfusa:req REQ-CANEP-013
//cfusa:req REQ-CANEP-014
//cfusa:req REQ-CANEP-015
//cfusa:req REQ-CANEP-016
//cfusa:req REQ-CANEP-017
//cfusa:req REQ-CANEP-018
//cfusa:req REQ-CANEP-019
//cfusa:req REQ-CANEP-020
//cfusa:req REQ-CANEP-021
//cfusa:req REQ-CANEP-022

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-CANEP-028
//cfusa:req REQ-CANEP-029
//cfusa:req REQ-CANEP-030
//cfusa:req REQ-CANEP-031
//cfusa:req REQ-CANEP-032
/*
 * ep_can.h -- CAN controller endpoint (Classical/FD/XL) for the TC18 Remote
 * Control Protocol wire layer (ROADMAP.md Phase 19, "Remaining Endpoint
 * Types", milestone 72).
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
 * ep_adc.h/.c, ep_lin.h/.c) is touched here -- the same layering discipline
 * every endpoint type since milestone 64 has established, followed
 * structurally throughout by this module too.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 * Where this header cites "extraction §5.11" or "extraction §7" it cites
 * only those section *numbers* of the confidential internal TC18
 * gap-analysis extraction, as attribution for which general topic area
 * motivated a design choice -- never that document's prose or any specific
 * numeric value from it. Concrete numbers below (register field widths,
 * filter counts, byte-prefix layouts, etc.) that are not independently
 * public knowledge are this module's own original choices, not values
 * taken from that extraction.
 *
 * ── Terminology note carried from Phase 13 (three distinct CAN-shaped things) ──
 *
 * This codebase now has three genuinely distinct things that all happen to
 * share the CAN/CAN-FD/CAN-XL bus technology, and they must never be
 * conflated or reimplemented against each other:
 *
 *   1. This module (ep_can.h/ep_can.c): a *native RCP endpoint* -- an RC
 *      Server exposes a physical CAN/CAN-FD/CAN-XL bus it owns as one of its
 *      own byte_bus_id-addressed endpoints, reached the same way every other
 *      endpoint type in this codebase is reached (ACF request/response over
 *      an AVTPDU).
 *   2. CAN(FD/XL)-as-RCP's-own-underlying-transport: avtp.h's own milestone
 *      59 file header already documents this -- "adding the IEEE1722-over-
 *      UDP/IP (spec Annex J) and CAN(FD/XL)-as-underlying-network carriers
 *      alongside native Ethernet" as an `rcp_avtp_transport_t` vtable
 *      implementation, i.e. CAN(FD/XL) used to *carry AVTPDUs themselves*
 *      between RC Nodes, not to expose a CAN bus as an endpoint. No such
 *      transport ships yet (avtp.h's own header says so); this milestone
 *      does not add one, and this module's request/response codecs have no
 *      dependency on which transport carries the AVTPDU they ride in.
 *   3. `canbr.h`/`canbr.c` (SG-006): the pre-replacement bridge stub to an
 *      *external* CAN segment, under the *old*, unrelated Zone/Command "RCP"
 *      protocol this repository is replacing (see ROADMAP.md's Protocol
 *      Replacement Notice). Its `rcp_can_config_t` (can_id_base/fd_mode/
 *      timeout_ms) models a materially different job -- bridging, not a
 *      native TC18 endpoint -- and every call it exposes currently returns
 *      RCP_ERR_NOT_SUPPORTED (no backend linked). `canbr.c` is left entirely
 *      untouched by this milestone; its own disposition (ADAPT, narrowed
 *      role per the Satellite Package Disposition table) is Phase 21's job,
 *      tracked for Satellite Rework v0.81.0. Nothing about `canbr.h`'s
 *      config shape carries forward to this endpoint type.
 *
 * This paragraph is the cross-reference the roadmap's own Satellite
 * Disposition table calls for "to prevent the three-way duplication" --
 * `avtp.h` and `canbr.h` already carry their own halves of this same
 * distinction in their own file headers (quoted/cited above, not
 * duplicated), so only this module's own header needed a new edit to close
 * the loop; per this milestone's layering discipline, avtp.h and canbr.h
 * themselves are not touched here.
 *
 * ── Requirement-id naming note ──────────────────────────────────────────────
 *
 * The untouched `canbr.c` stub already owns the `REQ-CAN-*` id prefix in
 * `.fusa-reqs.json` (its RPC-facing stub behaviors). This module's own
 * requirements are tagged `REQ-CANEP-*` ("CAN endpoint") instead, the exact
 * collision-avoidance naming seam `ep_lin.h` already established at
 * v0.71.0 for the analogous `REQ-LIN-*` (linbr.c) vs. `REQ-LINEP-*`
 * (ep_lin.c) split.
 *
 * ── Framing level ────────────────────────────────────────────────────────────
 *
 * As with every endpoint type before it, a CAN frame request/response is
 * ordinary endpoint traffic: whether it rides an NTSCF or TSCF AVTPDU is a
 * transport/scheduling choice made by the caller (avtp.h), not a property
 * of this endpoint itself. This module therefore operates at the ACF level
 * only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their decode
 * counterparts) -- a caller wraps (or unwraps) the frames this module
 * produces (or consumes) in NTSCF/TSCF using avtp.h directly.
 *
 * ── Controller-only, one bus per endpoint, data frames only ─────────────────
 *
 * This endpoint models a CAN *controller* only (no separate "monitor-only"
 * mode is exposed at this layer). Exactly one CAN bus is addressed per
 * byte_bus_id, matching ep_i2c.h's/ep_lin.h's own single-bus-per-endpoint
 * precedent. Only *data* frames are modeled -- remote frames are explicitly
 * out of this milestone's scope; there is no wire representation, encode
 * function, or decode outcome for a remote frame anywhere in this module.
 *
 * ── FrameFormat selection (payload prefix, NOT evt[2:0]) ────────────────────
 *
 * TC18 §13.7.11.3 Figure 39 packs FrameFormat into the payload itself, as
 * the top 3 bits of the request/response's leading quadlet, sharing that
 * quadlet with a 29-bit CAN ID (right-aligned for an 11-bit base id, per
 * TC18 §13.7.11.3's own note) -- NOT into evt[2:0], which for CAN (a
 * member of TC18 §13.5 Table 33's {ADC, PWM_IN, I2C, LIN, CAN, UART,
 * ISELED, MDIO} row) has the same ordinary meaning every other endpoint in
 * that row gives it: 000b for a plain request/response, 111b for a
 * configuration write (§12.7.1), every other value rejected with error
 * code UNSUPPORTED_CMD (see rcp_acf_evt_row2_is_plain(), acf.h). An earlier
 * revision of this module packed frame_format into evt[2:0] instead --
 * modeled on ep_spi.h's real, TC18-sanctioned evt[2:0] channel-selector
 * row -- without checking that CAN belongs to the *other* Table 33 row,
 * whose evt[2:0] values TC18 reserves. That was wrong: it accepted
 * evt[2:0] values Table 33 requires this endpoint type to reject, and put
 * FrameFormat at a wire position no compliant peer reads it from. Fixed
 * (v0.109.0) by moving frame_format into the payload's leading quadlet, per
 * Figure 39, and giving evt[2:0] its ordinary Table 33 meaning.
 *
 * rcp_ep_can_frame_format_t names the six frame variants TC18's own Table
 * 54 enumerates -- Classical Base/Extended Frame Format (CBFF/CEFF), FD
 * Base/Extended Frame Format (FBFF/FEFF), and CAN XL over either the
 * classical or the new CAN XL physical layer -- with Table 54's own
 * numeric assignments (0-5); values 6-7 are Table 54's own two reserved
 * codes and select no defined format.
 * rcp_ep_can_frame_format_valid() accepts only 0-5; a decoded FrameFormat
 * of 6 or 7 is rejected with RCP_EP_CAN_ERR_BAD_FRAME_FORMAT.
 *
 * rcp_ep_can_frame_format_id_width() reports whether a format's arbitration
 * identifier is an 11-bit ("base") or 29-bit ("extended") value: CBFF/FBFF
 * use the 11-bit base width; CEFF/FEFF use the 29-bit extended width; both
 * CAN XL variants use the 11-bit base width (CAN XL's own arbitration-phase
 * identifier is a base-width Priority ID, independent of this endpoint's own
 * physical-layer selection) -- an invalid format value is treated fail-safe
 * as the 11-bit base width, this module's own convention (mirroring
 * ep_spi.h's rcp_ep_spi_mode_cpol()/_cpha() fail-safe defaults) for never
 * fabricating the wider, more-permissive answer for undefined input.
 * rcp_ep_can_arbitration_id_valid() bounds a candidate identifier against
 * that width, always false for an invalid format.
 *
 * rcp_ep_can_frame_format_max_data_len() reports each format's own payload
 * ceiling: 8 bytes (Classical), 64 bytes (FD), or
 * RCP_EP_CAN_XL_MAX_DATA_LEN (2048) bytes (XL, either physical layer) --
 * zero for an invalid format.
 *
 * ── CAN XL's extra header fields (RRS/SDT/VCID/AF) ──────────────────────────
 *
 * rcp_ep_can_xl_header_t carries the three CAN XL-specific fields this
 * module's own wire encoding transmits alongside an XL frame's arbitration
 * identifier: sdt (SDU Type), vcid (Virtual CAN Network ID), and af
 * (Acceptance Field) -- serialized by this module as exactly 6 bytes
 * (1 + 1 + 4), matching the roadmap's own "6 extra header bytes" figure.
 * RRS (Remote Request Substitution) is *not* one of those 6 serialized
 * bytes: it is a single arbitration-phase bit whose value is implied
 * entirely by the frame_format selection itself (present, at a fixed
 * value, whenever frame_format is one of the two CAN XL variants; absent
 * otherwise) -- this module therefore never carries it as a separately
 * encoded field, and rcp_ep_can_xl_header_t has no rrs member. xl_header is
 * only meaningful (and only ever populated on decode) when frame_format is
 * RCP_EP_CAN_FRAME_XL_CLASSICAL_PL or RCP_EP_CAN_FRAME_XL_NEW_PL; it is
 * ignored on encode and left untouched on decode for every other format.
 *
 * A CAN XL frame's own data-phase content (this module's own SDT/VCID/AF
 * 6-byte prefix plus up to RCP_EP_CAN_XL_MAX_DATA_LEN (2048) bytes of
 * payload) tops out at 2054 bytes -- the same figure the roadmap itself
 * cites. This module's *own* ACF-level wire encoding of a full CAN XL frame
 * request/response additionally prepends its own 4-byte arbitration-id
 * prefix ahead of that (see "Wire layout" below), for
 * RCP_EP_CAN_XL_MAX_ENCODED_LEN (2058) bytes of worst-case ACF payload --
 * this 4-byte difference is this module's own transport-encoding overhead,
 * not part of the native CAN XL frame's own 2054-byte figure, and is called
 * out explicitly here so the two numbers are never mistaken for a
 * discrepancy.
 *
 * Neither figure fits within RCP_ACF_ABB_MAX_PAYLOAD (2036, acf.h) as of
 * this project's TC18 conformance fix (see CHANGELOG.md): acf_msg_length
 * is now the real 9-bit quadlet count Table 4 specifies, not the
 * 16-bit octet count an earlier, non-conformant revision of acf.c used
 * (which is where this paragraph's old "65535" figure came from -- a
 * bound no real TC18 peer would have honored either). Neither figure
 * fits within RCP_AVTP_NTSCF_MAX_PAYLOAD (2047, avtp.h) either -- and
 * NTSCF is the *only* AVTPDU format an RC Server itself ever sends
 * (avtp.h's own file header). A worst-case CAN XL frame response an RC
 * Server needs to send therefore cannot be carried in a single ACF
 * message *or* a single NTSCF AVTPDU today; it fits within a single TSCF
 * AVTPDU's own RCP_AVTP_TSCF_MAX_PAYLOAD (65535) byte budget, but not
 * within one ACF message's own 2036-byte ceiling, and TSCF is
 * client-to-server only (avtp.h) regardless, so it is not a general
 * answer either. This was exactly the concrete driver ROADMAP.md named
 * for Phase 20's fragmentation go-decision (v0.76.0, the `ms`-bit/
 * segment_num mechanism, fragment.h) -- rcp_ep_can_encode_frame_response_fragmented()/
 * rcp_ep_can_decode_frame_response_fragment() below are that milestone's
 * retrofit of this endpoint, closing the single-AVTPDU-worst-case gap this
 * paragraph used to describe as open, and (after the conformance fix
 * above) the *only* way to send a worst-case CAN XL frame at all.
 * rcp_ep_can_encode_frame_response()/_decode_frame_response() above are
 * unchanged and remain the right choice whenever the caller already
 * knows (or doesn't need to fragment) a response fits in one ACF message
 * -- fragmentation is opt-in per call, not a behavior change to the
 * existing single-frame codec. There is, as of this fix, no equivalent
 * fragmented *request* path (rcp_ep_can_encode_frame_request() has no
 * `_fragmented` counterpart): a worst-case CAN XL new-payload *write*
 * request cannot be sent in one ACF message and has no multi-message
 * alternative yet -- tracked as a follow-up, not this fix's scope.
 *
 * ── Wire layout: TC18 §13.7.11.3 Figure 39 ──────────────────────────────────
 *
 * A frame request/response's ACF payload is a big-endian 4-byte leading
 * quadlet packing frame_format (top 3 bits) and arbitration_id (bottom 29
 * bits, right-aligned for an 11-bit base id -- see rcp_ep_can_frame_format_id_width()),
 * followed -- only when frame_format is one of the two CAN XL variants --
 * by xl_header's big-endian sdt (1 byte), vcid (1 byte), and af (4 bytes)
 * (Figure 39's own "CAN data field includes ... RRS, SDT, VCID, AF" note:
 * these six bytes are the leading bytes of Figure 39's "CAN data" region,
 * not a separate field ahead of it), followed by the raw CAN data bytes
 * exactly as supplied (this module never inspects, generates, or reformats
 * any byte of the data payload itself, the same "dumb pass-through for the
 * data content" philosophy ep_lin.h/ep_i2c.h/ep_uart.h already established
 * for their own raw-byte content -- only the frame's own structural
 * fields, not its data, are modeled here).
 *
 * ── No trigger-signal table: a documented upstream spec gap ─────────────────
 *
 * Unlike every prior endpoint type with an asynchronous-event mechanism
 * (ep_lin.h's single transmission-done trigger; ep_gpio.h's/ep_spi.h's own
 * per-pin/per-channel trigger tables), this module defines *no* trigger
 * enumeration and no `rcp_ep_can_trigger_t`-shaped field anywhere in
 * rcp_ep_can_functional_cfg_t. This is a deliberate reflection of a gap in
 * the specification itself (extraction §7), not an oversight or a
 * placeholder pending a later fix: the spec's own trigger-signal table has
 * no populated entry for this endpoint type. Anyone extending this file
 * later who finds themselves reaching for a CAN trigger concept should stop
 * and re-read this paragraph first -- it is this milestone's documented
 * scope, not a gap to silently invent an answer for the way ep_lin.h's own
 * evt[2:0] comparison-mode enumeration filled an *unenumerated-but-real*
 * mechanism the roadmap did name. Here, by contrast, the roadmap and the
 * spec both agree there is simply nothing to enumerate.
 *
 * ── Functional configuration: three separate bit-timing register sets ──────
 *
 * Per extraction §5.11, this endpoint's functional configuration keeps
 * *separate* bit-timing register sets for the arbitration phase, the FD
 * data phase, and the XL data phase -- rcp_ep_can_bit_timing_t is this
 * module's own reusable shape for all three (prescaler, prop_seg,
 * phase_seg1, phase_seg2, sync_jump_width -- standard, publicly documented
 * Bosch-CAN-style bit-timing register concepts, not values taken from the
 * confidential extraction), composed three times
 * (arbitration_timing/fd_data_timing/xl_data_timing) in
 * rcp_ep_can_functional_cfg_t and set independently via
 * rcp_ep_can_set_arbitration_timing()/_set_fd_data_timing()/
 * _set_xl_data_timing(). Also per extraction §5.11: a delay-compensation
 * control (delay_comp_enable/delay_comp_offset, rcp_ep_can_set_delay_
 * compensation()), CAN-XL acceptance/ID filters (rcp_ep_can_xl_filter_t,
 * up to RCP_EP_CAN_XL_MAX_FILTERS -- this module's own chosen count, not a
 * spec-derived number), and this endpoint's own base-clock/divider register
 * (exec_delay_clk_divider, rcp_ep_can_set_exec_delay_clk_divider()) scoped
 * *only* to this endpoint's own execution-delay timing -- explicitly *not*
 * the bit-timing clock itself, which the three rcp_ep_can_bit_timing_t
 * register sets above already own independently. Composing
 * regmap.h's rcp_regmap_ep_functional_cfg_t as cfg's own first member
 * follows that module's documented convention (every endpoint type's own
 * precedent); rcp_ep_can_functional_cfg_writable() is, likewise, a thin,
 * named wrapper over lifecycle.h's rcp_lifecycle_field_writable()
 * (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W), and every rcp_ep_can_set_*() mutator
 * consults it before ever touching cfg -- reusing, never duplicating,
 * lifecycle.h's/regmap.h's existing authorization logic, per the roadmap's
 * explicit instruction (the same rule every prior endpoint type's own
 * setters already follow).
 *
 * ── TC18 functional-configuration register-block address defect (investigated, no wire model exists) ──
 *
 * TC18's own functional-configuration register table for this endpoint
 * type has a genuine, still-unresolved address defect, independently
 * re-verified against the rendered PDF page image (not just text
 * extraction) on both the baseline revision and the newest available
 * revision: two consecutive acceptance-filter table rows are both printed
 * at the same relative address, with a receive-filter table immediately
 * following at the address one of those two rows would need if it were
 * simply shifted forward by one slot. The primary source gives no way to
 * decide between two readings: either only as many acceptance filters
 * exist as there are non-colliding addresses before the collision (the
 * colliding row being a stray duplicate, and the receive-filter table
 * already correctly addressed right after), or one more acceptance filter
 * really exists than that reading allows (belonging at the colliding
 * address after all), in which case every receive-filter address would
 * also need a uniform shift to make room. This module deliberately does
 * NOT force-resolve that ambiguity by picking one reading -- see below for
 * why it doesn't need to yet.
 *
 * This has zero effect on this module's own conformance today: nothing in
 * ep_can.c serializes or deserializes this register block's byte offsets
 * at all -- rcp_ep_can_functional_cfg_t (bit timings, delay compensation,
 * the execution-delay divider, and the XL filter table above) is a pure
 * in-memory, caller-populated API, never encoded to or decoded from wire
 * bytes at any of this table's addresses. RCP_EP_CAN_XL_MAX_FILTERS (4)
 * stays exactly what it already was -- it is this module's own
 * independently chosen count (see above), not read off this defective
 * table, so nothing about this finding changes it. Anyone who later adds a
 * real register-block codec for this endpoint (the render_registers()/
 * apply_reconfig() shape ep_gpio.c/ep_spi.c/ep_wakeup.c/ep_mdio.c already
 * use) must resolve this ambiguity first, deliberately, rather than
 * silently copying whichever address ordering seems more convenient.
 *
 * Separately, and also independently re-verified against both PDF
 * revisions: this table's three bit-timing registers (arbitration phase,
 * FD data phase, XL data phase) are each a single opaque 32-bit value with
 * no sub-field bit-layout published anywhere near that table in either
 * revision -- unlike this endpoint's own request/response format, which
 * does get a companion field-level figure. This is not an extraction gap;
 * the primary source itself simply never publishes that breakdown, in
 * either revision. rcp_ep_can_bit_timing_t's own five-field shape above is
 * this module's own standard, publicly documented Bosch-CAN-style
 * modeling choice for that reason, not a spec-derived layout -- already
 * noted above, restated here only to tie it to this same investigation.
 */
#ifndef RCP_EP_CAN_H
#define RCP_EP_CAN_H

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

/* ── FrameFormat selection (evt[2:0]) ────────────────────────────────────── */

typedef enum {
    RCP_EP_CAN_FRAME_CBFF            = 0, /* Classical Base Frame Format, 11-bit id */
    RCP_EP_CAN_FRAME_CEFF            = 1, /* Classical Extended Frame Format, 29-bit id */
    RCP_EP_CAN_FRAME_FBFF            = 2, /* FD Base Frame Format, 11-bit id */
    RCP_EP_CAN_FRAME_FEFF            = 3, /* FD Extended Frame Format, 29-bit id */
    RCP_EP_CAN_FRAME_XL_CLASSICAL_PL = 4, /* XL frame, classical CAN physical layer */
    RCP_EP_CAN_FRAME_XL_NEW_PL       = 5, /* XL frame, new CAN XL physical layer */
} rcp_ep_can_frame_format_t;

/* True iff v (a raw evt[2:0] value as decoded off the wire) selects one of
 * the six defined frame formats, i.e. v <= 5. Values 6 and 7 select no
 * defined format -- see the file header. */
bool rcp_ep_can_frame_format_valid(uint8_t v);

/* True iff format is one of the two CAN XL variants (XL_CLASSICAL_PL or
 * XL_NEW_PL). False for every other (valid or invalid) value. */
bool rcp_ep_can_frame_format_is_xl(rcp_ep_can_frame_format_t format);

typedef enum {
    RCP_EP_CAN_ID_WIDTH_BASE_11     = 0,
    RCP_EP_CAN_ID_WIDTH_EXTENDED_29 = 1,
} rcp_ep_can_id_width_t;

/* The arbitration-identifier width format uses: EXTENDED_29 for CEFF/FEFF,
 * BASE_11 for every other defined format (CBFF, FBFF, and both CAN XL
 * variants). An invalid format value fails safe to BASE_11 -- see the file
 * header. */
rcp_ep_can_id_width_t rcp_ep_can_frame_format_id_width(rcp_ep_can_frame_format_t format);

/* True iff id is in range for format's own id width (<= 0x7FF for
 * BASE_11, <= 0x1FFFFFFF for EXTENDED_29); always false for an invalid
 * format value. */
bool rcp_ep_can_arbitration_id_valid(rcp_ep_can_frame_format_t format, uint32_t id);

/* The largest raw CAN data length (octets) format's own frame kind permits:
 * 8 (CBFF/CEFF), 64 (FBFF/FEFF), or RCP_EP_CAN_XL_MAX_DATA_LEN (2048, both
 * XL variants); 0 for an invalid format value. */
size_t rcp_ep_can_frame_format_max_data_len(rcp_ep_can_frame_format_t format);

#define RCP_EP_CAN_CLASSICAL_MAX_DATA_LEN ((size_t)8u)
#define RCP_EP_CAN_FD_MAX_DATA_LEN        ((size_t)64u)
#define RCP_EP_CAN_XL_MAX_DATA_LEN        ((size_t)2048u)

/* Worst-case ACF payload length this module's own encode functions can
 * produce for a CAN XL frame request/response: the 4-byte arbitration-id
 * prefix, this module's own 6-byte SDT/VCID/AF prefix, and
 * RCP_EP_CAN_XL_MAX_DATA_LEN (2048) bytes of data -- see the file header's
 * discussion of why this exceeds RCP_AVTP_NTSCF_MAX_PAYLOAD (avtp.h). */
#define RCP_EP_CAN_XL_MAX_ENCODED_LEN ((size_t)(4u + 6u + RCP_EP_CAN_XL_MAX_DATA_LEN))

/* ── CAN XL's extra header fields (RRS/SDT/VCID/AF) ──────────────────────── */

/* Only meaningful (and only ever populated on decode) when the associated
 * frame_format is RCP_EP_CAN_FRAME_XL_CLASSICAL_PL or
 * RCP_EP_CAN_FRAME_XL_NEW_PL -- see the file header. RRS is deliberately
 * not a member here; its value is implied by frame_format alone. */
typedef struct {
    uint8_t  sdt;  /* SDU Type */
    uint8_t  vcid; /* Virtual CAN Network ID */
    uint32_t af;   /* Acceptance Field */
} rcp_ep_can_xl_header_t;

/* ── Functional config: bit timing ───────────────────────────────────────── */

/* This module's own reusable bit-timing register shape, composed three
 * times (once per phase) in rcp_ep_can_functional_cfg_t -- see the file
 * header. */
typedef struct {
    uint32_t prescaler;
    uint16_t prop_seg;
    uint16_t phase_seg1;
    uint16_t phase_seg2;
    uint8_t  sync_jump_width;
} rcp_ep_can_bit_timing_t;

/* ── Functional config: CAN-XL acceptance/ID filters ─────────────────────── */

/* This module's own chosen filter-table depth -- not a spec-derived
 * number, see the file header. */
#define RCP_EP_CAN_XL_MAX_FILTERS ((uint8_t)4u)

typedef struct {
    uint32_t id;
    uint32_t mask;
    bool     enable;
} rcp_ep_can_xl_filter_t;

/* True iff index is a valid filter-table index
 * (0..RCP_EP_CAN_XL_MAX_FILTERS-1). */
bool rcp_ep_can_xl_filter_index_valid(uint8_t index);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    rcp_ep_can_bit_timing_t        arbitration_timing;
    rcp_ep_can_bit_timing_t        fd_data_timing;
    rcp_ep_can_bit_timing_t        xl_data_timing;
    bool                           delay_comp_enable;
    uint8_t                        delay_comp_offset;
    uint32_t                       exec_delay_clk_divider; /* execution-delay
                                                                timing only --
                                                                NOT bit
                                                                timing, see
                                                                the file
                                                                header */
    rcp_ep_can_xl_filter_t         xl_filters[RCP_EP_CAN_XL_MAX_FILTERS];
    uint16_t                       ep_status;   /* can_ep_status, Table 56
                                                     0x0006 -- REQ-CANEP-028 */
    uint32_t                       status;      /* CAN EP status, Table 56
                                                     0x001C -- REQ-CANEP-028 */
    uint32_t                       fifo_status; /* FIFO status, Table 56
                                                     0x0020 -- REQ-CANEP-028 */
} rcp_ep_can_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false; every bit-timing register
 * set, delay-compensation field, exec_delay_clk_divider, and filter table
 * entry zeroed/disabled). */
void rcp_ep_can_functional_cfg_init(rcp_ep_can_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (lifecycle.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_can_functional_cfg_writable(rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->arbitration_timing to timing iff
 * rcp_ep_can_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_can_set_arbitration_timing(rcp_ep_can_functional_cfg_t *cfg,
                                        rcp_ep_can_bit_timing_t timing,
                                        rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_can_set_arbitration_timing(), for
 * cfg->fd_data_timing. */
bool rcp_ep_can_set_fd_data_timing(rcp_ep_can_functional_cfg_t *cfg,
                                    rcp_ep_can_bit_timing_t timing,
                                    rcp_lifecycle_state_t state,
                                    rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule, for cfg->xl_data_timing. */
bool rcp_ep_can_set_xl_data_timing(rcp_ep_can_functional_cfg_t *cfg,
                                    rcp_ep_can_bit_timing_t timing,
                                    rcp_lifecycle_state_t state,
                                    rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule, for cfg->delay_comp_enable and
 * cfg->delay_comp_offset together (one setter for both, since they are
 * always reconfigured as a pair on the wire, mirroring ep_spi.h's own
 * paired-timing-fields setter convention). */
bool rcp_ep_can_set_delay_compensation(rcp_ep_can_functional_cfg_t *cfg, bool enable,
                                        uint8_t offset, rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule, for cfg->exec_delay_clk_divider -- see the file
 * header for why this is a distinct register from the three bit-timing
 * sets above. */
bool rcp_ep_can_set_exec_delay_clk_divider(rcp_ep_can_functional_cfg_t *cfg, uint32_t divider,
                                            rcp_lifecycle_state_t state,
                                            rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->xl_filters[index] to filter iff index is
 * rcp_ep_can_xl_filter_index_valid() and
 * rcp_ep_can_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_can_set_xl_filter(rcp_ep_can_functional_cfg_t *cfg, uint8_t index,
                               rcp_ep_can_xl_filter_t filter, rcp_lifecycle_state_t state,
                               rcp_lifecycle_writer_ctx_t writer);

/* ── The EP_func register block (the evt[2:0] == 111b target), REQ-CANEP-028 ─
 *
 * TC18 §13.7.11.2 Table 56 ("can functional configuration"; RC1's own
 * Table 53 -- renumbered, same lineage as issue #341):
 *
 *   0x0000  can_ep_len              8 bit  R    RCP_EP_CAN_EP_FUNC_LEN (0x24)
 *   0x0001  Reserved                8 bit  R    reads 0x00
 *   0x0002  can_ep_enable&clr       8 bit  R/W  Table 35 common entries
 *   0x0003  can_ep_options          8 bit  R/W* Table 35 common entries
 *   0x0004  can_base_clk           16 bit  R    CAN system clock
 *   0x0006  can_ep_status          16 bit  R/W
 *   0x0008  can_clk_divider         8 bit  R/W  generates CAN_CLK
 *   0x0009  Reserved                8 bit  R    reads 0x00
 *   0x000A  Reserved               16 bit  R    reads 0x0000
 *   0x000C  CAN bit time register 1  32 bit  R/W  Classical CAN bit times
 *   0x0010  CAN bit time register 2  32 bit  R/W  CAN FD bit times
 *   0x0014  CAN bit time register 3  32 bit  R/W  CAN XL bit times
 *   0x0018  TDCC register            32 bit  R/W  delay compensation control
 *   0x001C  CAN EP status           32 bit  R/W  status of CAN endpoint
 *   0x0020  FIFO status             32 bit  R/W  status of CAN FIFOs
 *
 * closing at 0x0024, immediately before Table 56's own acceptance-filter
 * region (0x0024 onward) -- REQ-CANEP-029's own already-documented,
 * genuine address collision (acceptance filter 3 and 4 both printed at
 * 0x002C, confirmed against the rendered PDF page image on two
 * revisions, not an extraction artifact) blocks modeling that region at
 * all until it is independently resolved; REQ-CANEP-028 is scoped to
 * everything before it, matching this codebase's own established
 * practice of never letting one genuinely unresolved sub-range block an
 * otherwise-tractable register block (see ep_wakeup.h's own precedent,
 * task #95).
 *
 * can_base_clk (read-only) always renders 0 -- no real clock source
 * modelled, the same honesty ep_adc.h's/ep_gpio.h's/ep_i2c.h's/
 * ep_lin.h's own base_clk fields already commit to.
 *
 * The 0x0008-0x001B span (can_clk_divider, two reserved octets, the
 * three "CAN bit time register" fields, and TDCC) is deliberately
 * treated as read-only and renders 0 for now: an earlier investigation
 * (issue #256 Group I) already found Table 56 gives these 32-bit
 * registers no sub-field bit-layout in the specification text, so
 * converting this module's own rcp_ep_can_bit_timing_t (prescaler/
 * prop_seg/phase_seg1/phase_seg2/sync_jump_width) to and from their wire
 * representation is not derivable without inventing an unverified
 * bit-packing scheme -- the same reasoning that already deferred
 * mapping them anywhere else in this codebase. Treating them read-only
 * here is the same fail-safe disposition a too-short or unrecognized
 * write already gets elsewhere: a write is never silently accepted and
 * then discarded, it is visibly rejected for that octet range specifically
 * (reg_offset_read_only(), ep_can.c) while every other octet in the same
 * write still applies. can_clk_divider and the delay-compensation fields
 * this module already carries in memory (exec_delay_clk_divider,
 * delay_comp_enable/_offset) remain genuinely settable via their own
 * existing setters above -- only their WIRE representation in this
 * specific byte range is what stays undecomposed.
 *
 * can_ep_status/the 32-bit CAN EP status (0x001C)/FIFO status (0x0020)
 * are new fields on rcp_ep_can_functional_cfg_t (ep_status/status/
 * fifo_status) -- real, freely settable in-memory state with no
 * meaning this module enforces beyond storing and round-tripping
 * whatever value a caller or a register-map write assigns, the same
 * disposition every other endpoint type's own status register(s)
 * already get. */

#define RCP_EP_CAN_EP_FUNC_LEN ((uint16_t)0x0024u)

#define RCP_EP_CAN_RECONFIG_ADDR_LEN ((size_t)2u)

typedef enum {
    RCP_EP_CAN_RECONFIG_OK               = 0,
    RCP_EP_CAN_RECONFIG_ERR_SHORT        = 1,
    RCP_EP_CAN_RECONFIG_ERR_OUT_OF_RANGE = 2,
} rcp_ep_can_reconfig_errc_t;

/* Human-readable message for an rcp_ep_can_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_can_reconfig_strerror(rcp_ep_can_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_CAN_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_can_apply_reconfig()'s own parse step, and the
 * same rendering that function patches in place. can_base_clk and the
 * 0x0008-0x001B span always render 0 -- see this section's own opening
 * comment. */
void rcp_ep_can_render_registers(const rcp_ep_can_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_CAN_EP_FUNC_LEN]);

/* Applies the configuration escape hatch (evt[2:0] == 111b): payload is NOT
 * presented at the interface but interpreted as an addressed write into
 * this endpoint's own EP_func block -- a 16-bit big-endian relative start
 * address followed by the configuration data octets to write from that
 * address onward (§12.7.1).
 *
 * Returns RCP_EP_CAN_RECONFIG_ERR_SHORT when payload_len is not at least
 * RCP_EP_CAN_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_CAN_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would
 * extend past RCP_EP_CAN_EP_FUNC_LEN; in both cases cfg is left entirely
 * unchanged, per the specification's own "such a payload is to be
 * ignored" rule. Octets of the addressed span that land on a read-only
 * register (EP_LEN, both reserved octets, base_clk, and the whole
 * not-yet-decomposed 0x0008-0x001B span -- see this section's own
 * opening comment) are left at their current values while the rest of
 * the span is still applied. */
rcp_ep_can_reconfig_errc_t rcp_ep_can_apply_reconfig(rcp_ep_can_functional_cfg_t *cfg,
                                                      const uint8_t *payload,
                                                      size_t payload_len);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_CAN_OK                    = 0,
    RCP_EP_CAN_ERR_SHORT_FRAME       = 1,
    RCP_EP_CAN_ERR_BAD_MSG_TYPE      = 2,
    RCP_EP_CAN_ERR_WRONG_BUS         = 3,
    RCP_EP_CAN_ERR_WRONG_OP          = 4,
    /* The payload's leading quadlet's top 3 bits (FrameFormat, TC18
     * Table 54) are not one of Table 54's six defined values -- see the
     * file header's "FrameFormat selection" section. */
    RCP_EP_CAN_ERR_BAD_FRAME_FORMAT  = 5,
    /* evt[2:0] is not 0b000, TC18 §13.5 Table 33's only legal value for a
     * plain (non-configuration) request/response in CAN's endpoint-type
     * row -- caller shall respond with error code UNSUPPORTED_CMD (see
     * rcp_acf_evt_row2_is_plain()). */
    RCP_EP_CAN_ERR_BAD_EVT            = 6,
    /* The decoded arbitration_id does not fit FrameFormat's own id width
     * (TC18 §13.7.11.3: an 11-bit base id must be right-aligned, i.e. the
     * leading quadlet's bits 3-20 must be zero for a base-11 format) -- see
     * rcp_ep_can_arbitration_id_valid(). */
    RCP_EP_CAN_ERR_BAD_ARBITRATION_ID = 7,
} rcp_ep_can_errc_t;

/* Human-readable message for an rcp_ep_can_errc_t value. Never returns NULL. */
const char *rcp_ep_can_strerror(rcp_ep_can_errc_t e);

/* ── Frame request ─────────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB frame request addressed to byte_bus_id: evt is left
 * entirely 0 (TC18 §13.5 Table 33's ordinary "plain request" value for
 * CAN's endpoint-type row), and the payload is TC18 §13.7.11.3 Figure 39's
 * layout -- see the file header -- of frame_format+arbitration_id (the
 * leading quadlet), xl_header (only when frame_format is a CAN XL
 * variant), and tx_data[0..tx_len) (the raw CAN data bytes; tx_data may be
 * NULL iff tx_len == 0). Returns a zeroed rcp_bytes_t (data=NULL) if:
 * frame_format is not rcp_ep_can_frame_format_valid(); arbitration_id is
 * not rcp_ep_can_arbitration_id_valid() for frame_format; tx_len exceeds
 * rcp_ep_can_frame_format_max_data_len(frame_format); xl_header is NULL
 * when rcp_ep_can_frame_format_is_xl(frame_format) is true, or non-NULL
 * when it is false; or on allocation failure. Caller frees the result with
 * rcp_bytes_free(). */
rcp_bytes_t rcp_ep_can_encode_frame_request(rcp_byte_bus_id_t byte_bus_id,
                                             rcp_ep_can_frame_format_t frame_format,
                                             uint32_t arbitration_id,
                                             const rcp_ep_can_xl_header_t *xl_header,
                                             const uint8_t *tx_data, size_t tx_len,
                                             uint8_t transaction_num);

/* Decodes and validates an ACF-level CAN frame request from b[0..len).
 * Fails with RCP_EP_CAN_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header, its declared payload length, the 4-byte frame_format+
 * arbitration_id quadlet, or the full frame_format-dependent prefix
 * length; RCP_EP_CAN_ERR_BAD_MSG_TYPE if b is not an ACF_ABB message;
 * RCP_EP_CAN_ERR_WRONG_BUS if its byte_bus_id != expected_bus_id;
 * RCP_EP_CAN_ERR_WRONG_OP if its op is not RCP_ACF_OP_WRITE;
 * RCP_EP_CAN_ERR_BAD_EVT if its evt[2:0] is not 0b000
 * (rcp_acf_evt_row2_is_plain(), TC18 §13.5 Table 33 -- the caller shall
 * respond with error code UNSUPPORTED_CMD); RCP_EP_CAN_ERR_BAD_FRAME_FORMAT
 * if the decoded frame_format is not rcp_ep_can_frame_format_valid();
 * RCP_EP_CAN_ERR_BAD_ARBITRATION_ID if the decoded arbitration_id is not
 * rcp_ep_can_arbitration_id_valid() for that frame_format. On
 * RCP_EP_CAN_OK, *out_frame_format, *out_arbitration_id, and
 * *out_transaction_num are populated; *out_xl_header is populated iff
 * rcp_ep_can_frame_format_is_xl() (*out_frame_format), left entirely
 * untouched otherwise; *out_tx_data / *out_tx_len are set to a *borrowed*
 * view into b (not copied -- matching every prior endpoint type's own
 * raw-data decode convention) of the raw CAN data bytes following this
 * module's own prefix. */
rcp_ep_can_errc_t rcp_ep_can_decode_frame_request(const uint8_t *b, size_t len,
                                                   rcp_byte_bus_id_t expected_bus_id,
                                                   rcp_ep_can_frame_format_t *out_frame_format,
                                                   uint32_t *out_arbitration_id,
                                                   rcp_ep_can_xl_header_t *out_xl_header,
                                                   const uint8_t **out_tx_data,
                                                   size_t *out_tx_len,
                                                   uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes a CAN frame response with the same frame_format/arbitration_id/
 * xl_header/data-validation rules and prefix-then-data payload layout as
 * rcp_ep_can_encode_frame_request() (rx_data[0..rx_len), the raw CAN data
 * bytes captured off the bus, in place of tx_data), echoing
 * transaction_num. Encoded as ACF_ABB when timed is false; as ACF_GBB
 * (with message_timestamp set to timestamp, mtv = RCP_ACF_MTV_VALID) when
 * timed is true -- see every prior endpoint type's own timed/untimed
 * convention. Returns a zeroed rcp_bytes_t (data=NULL) under the same
 * conditions rcp_ep_can_encode_frame_request() does (substituting rx_data/
 * rx_len for tx_data/tx_len), or on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_can_encode_frame_response(rcp_byte_bus_id_t byte_bus_id,
                                              rcp_ep_can_frame_format_t frame_format,
                                              uint32_t arbitration_id,
                                              const rcp_ep_can_xl_header_t *xl_header,
                                              const uint8_t *rx_data, size_t rx_len,
                                              uint8_t transaction_num, bool timed,
                                              uint64_t timestamp);

/* Decodes a CAN frame response from either an ACF_ABB or ACF_GBB message
 * (this function peeks the ACF message type itself, unlike the request
 * decoder above, since a response's encoding depends on the responding
 * endpoint's own timed/untimed choice). Fails with RCP_EP_CAN_ERR_SHORT_FRAME
 * (frame too short for the applicable fixed header, its declared payload
 * length, the 4-byte frame_format+arbitration_id quadlet, or the full
 * frame_format-dependent prefix length), RCP_EP_CAN_ERR_WRONG_BUS
 * (byte_bus_id != expected_bus_id), RCP_EP_CAN_ERR_BAD_EVT (evt[2:0] is
 * not 0b000, rcp_acf_evt_row2_is_plain()), RCP_EP_CAN_ERR_BAD_FRAME_FORMAT
 * (the decoded frame_format is not rcp_ep_can_frame_format_valid()), or
 * RCP_EP_CAN_ERR_BAD_ARBITRATION_ID (the decoded arbitration_id is not
 * rcp_ep_can_arbitration_id_valid() for that frame_format). On
 * RCP_EP_CAN_OK, *out_frame_format,
 * *out_arbitration_id, and *out_transaction_num are populated;
 * *out_xl_header is populated iff rcp_ep_can_frame_format_is_xl()
 * (*out_frame_format), left entirely untouched otherwise; *out_rx_data /
 * *out_rx_len are set to a *borrowed* view into b (not copied) of the raw
 * captured CAN data bytes; *out_timed and *out_timestamp report whether
 * the message was ACF_GBB with a valid (rcp_acf_gbb_is_timed()) timestamp,
 * and that timestamp's value (0 when !*out_timed). */
rcp_ep_can_errc_t rcp_ep_can_decode_frame_response(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    rcp_ep_can_frame_format_t *out_frame_format,
                                                    uint32_t *out_arbitration_id,
                                                    rcp_ep_can_xl_header_t *out_xl_header,
                                                    const uint8_t **out_rx_data,
                                                    size_t *out_rx_len, bool *out_timed,
                                                    uint64_t *out_timestamp,
                                                    uint8_t *out_transaction_num);

/* ── Fragmented response (Phase 20, fragment.h) ────────────────────────────── */

/* The number of ACF frames rcp_ep_can_encode_frame_response_fragmented()
 * would produce for this response's combined prefix-then-data payload
 * (see the file header's "Wire layout" section) split into fragments of
 * at most max_fragment_payload octets each -- see fragment.h's
 * rcp_fragment_plan_count(). Returns 0 under the same conditions
 * rcp_ep_can_encode_frame_response() already fails encode_preconditions_ok()
 * for, plus rcp_fragment_plan_count()'s own 0-sentinel conditions
 * (max_fragment_payload == 0 with a combined payload that doesn't fit in
 * one fragment; more segments needed than fragment.h's segment_num width
 * can address). A caller uses this to size out_frames before calling
 * rcp_ep_can_encode_frame_response_fragmented(). */
size_t rcp_ep_can_frame_response_fragment_count(rcp_ep_can_frame_format_t frame_format,
                                                 uint32_t arbitration_id,
                                                 const rcp_ep_can_xl_header_t *xl_header,
                                                 size_t rx_len, size_t max_fragment_payload);

/* Encodes a CAN frame response as one or more ACF frames, fragmenting via
 * fragment.h's ms/segment_num mechanism (ROADMAP.md Phase 20, milestone
 * 76) whenever the combined prefix-then-data payload exceeds
 * max_fragment_payload octets -- into
 * out_frames[0..rcp_ep_can_frame_response_fragment_count(...)) (caller-
 * allocated, sized by calling that function first). Every fragment
 * shares byte_bus_id/evt(0)/op(READ)/transaction_num/timed/timestamp with
 * rcp_ep_can_encode_frame_response() -- frame_format itself lives inside
 * the combined payload's own leading quadlet (see the file header), not
 * per-fragment header state, so only the first fragment actually carries
 * it. Only the ms flag,
 * the read_size_or_segment_num field (meaningful only on an ms=true
 * fragment -- see acf.h/fragment.h), and each fragment's own payload
 * slice differ. When the combined payload already fits in one fragment,
 * this produces exactly one frame identical to what
 * rcp_ep_can_encode_frame_response() itself would have produced --
 * fragmentation is a strict superset of the unfragmented path, not a
 * separate wire format. Returns the number of frames written to
 * out_frames on success (equal to
 * rcp_ep_can_frame_response_fragment_count()'s answer), or 0
 * (out_frames left entirely untouched) under the same conditions that
 * function returns 0 for, or on allocation failure partway through (any
 * already-written out_frames entries are freed before returning). Caller
 * frees each successfully returned out_frames[i] with rcp_bytes_free().
 * This function does not itself apply e2e.h's safe-point CRC -- per
 * fragment.h's own file header, a caller wanting E2E protection wraps
 * only the final (ms=false) frame (out_frames[count-1]) with
 * rcp_e2e_wrap() itself, after this function returns. */
size_t rcp_ep_can_encode_frame_response_fragmented(rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_ep_can_frame_format_t frame_format,
                                                    uint32_t arbitration_id,
                                                    const rcp_ep_can_xl_header_t *xl_header,
                                                    const uint8_t *rx_data, size_t rx_len,
                                                    uint8_t transaction_num, bool timed,
                                                    uint64_t timestamp,
                                                    size_t max_fragment_payload,
                                                    rcp_bytes_t *out_frames);

/* Decodes one fragment of a (possibly multi-fragment) CAN frame response
 * from b[0..len) -- the same peek-message-type/byte_bus_id validation
 * rcp_ep_can_decode_frame_response() applies, but this function does
 * *not* strip TC18 Figure 39's leading-quadlet-then-data layout from the
 * payload (a fragment other than the first may not even contain the
 * whole leading quadlet -- fragmentation operates on the flat combined
 * byte sequence, agnostic to its own internal structure, per fragment.h's
 * file header). Since frame_format now lives inside that leading quadlet
 * rather than in evt (see the file header's "FrameFormat selection"
 * section), it is not obtainable per-fragment at all -- unlike the prior
 * evt-carried design, this function does not (and cannot) output it;
 * rcp_ep_can_decode_reassembled_frame_response() recovers it once,
 * *after* reassembly, from the reassembled buffer's own leading quadlet.
 * This function instead surfaces the fragment's own ms bit,
 * read_size_or_segment_num (as *out_segment_num, meaningful only when
 * *out_ms), and raw ACF payload slice (*out_payload / *out_payload_len,
 * borrowed into b, matching every decode function in this module), for a
 * caller to feed straight into a rcp_fragment_reassembler_t
 * (fragment.h). Once reassembly reports RCP_FRAGMENT_REASM_COMPLETE, pass
 * the reassembled buffer to rcp_ep_can_decode_reassembled_frame_response()
 * to extract frame_format/arbitration_id/xl_header/rx_data. Fails with
 * the same RCP_EP_CAN_ERR_SHORT_FRAME/_ERR_BAD_MSG_TYPE/_ERR_WRONG_BUS/
 * _ERR_BAD_EVT conditions rcp_ep_can_decode_frame_response() does; on
 * RCP_EP_CAN_OK, every output parameter is populated. */
rcp_ep_can_errc_t rcp_ep_can_decode_frame_response_fragment(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t expected_bus_id,
                                                             bool *out_ms,
                                                             uint8_t *out_segment_num,
                                                             const uint8_t **out_payload,
                                                             size_t *out_payload_len,
                                                             bool *out_timed,
                                                             uint64_t *out_timestamp,
                                                             uint8_t *out_transaction_num);

/* Applies TC18 Figure 39's leading-quadlet-then-data parsing (see the file
 * header's "Wire layout" section) to a fully reassembled combined payload
 * -- rcp_fragment_reassembler_get()'s output once
 * rcp_fragment_reassembler_feed() has reported RCP_FRAGMENT_REASM_COMPLETE.
 * This is the second half of what rcp_ep_can_decode_frame_response() does
 * in one step for a single, unfragmented frame. Returns
 * RCP_EP_CAN_ERR_SHORT_FRAME if reassembled_len is shorter than the
 * leading quadlet, or shorter than the full frame_format-dependent
 * prefix length once frame_format is known; RCP_EP_CAN_ERR_BAD_FRAME_FORMAT
 * if the decoded frame_format is not rcp_ep_can_frame_format_valid();
 * RCP_EP_CAN_ERR_BAD_ARBITRATION_ID if the decoded arbitration_id is not
 * rcp_ep_can_arbitration_id_valid() for that frame_format. On
 * RCP_EP_CAN_OK, *out_frame_format / *out_arbitration_id / *out_xl_header
 * are populated (the latter only when
 * rcp_ep_can_frame_format_is_xl(*out_frame_format)) and *out_rx_data /
 * *out_rx_len are set to a *borrowed* view into reassembled. */
rcp_ep_can_errc_t rcp_ep_can_decode_reassembled_frame_response(const uint8_t *reassembled,
                                                                size_t reassembled_len,
                                                                rcp_ep_can_frame_format_t *out_frame_format,
                                                                uint32_t *out_arbitration_id,
                                                                rcp_ep_can_xl_header_t *out_xl_header,
                                                                const uint8_t **out_rx_data,
                                                                size_t *out_rx_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_CAN_H */
