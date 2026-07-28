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
 * ── FrameFormat selection (evt[2:0]) ────────────────────────────────────────
 *
 * rcp_ep_can_frame_format_t names the six frame variants this endpoint
 * selects among -- Classical Base/Extended Frame Format (CBFF/CEFF),
 * FD Base/Extended Frame Format (FBFF/FEFF), and CAN XL over either the
 * classical or the new CAN XL physical layer -- selected via the ACF
 * byte_message_info header's evt field's low three bits (acf.h), the same
 * evt[2:0]-as-selector convention ep_spi.h's channel selector already
 * established (there, a channel number; here, a frame-format code).
 * rcp_ep_can_frame_format_valid() accepts only the six defined values (0-5);
 * values 6-7 select no defined format and are rejected on decode with
 * RCP_EP_CAN_ERR_BAD_FRAME_FORMAT, mirroring ep_spi.h's own
 * RCP_EP_SPI_ERR_BAD_CHANNEL treatment of its own out-of-range evt[2:0]
 * values -- the roadmap does not itself enumerate which 3-bit codes name
 * which frame format; the assignment above is this module's own original
 * design filling that gap, the same kind of gap-filling ep_lin.h's own
 * evt[2:0] comparison-mode enumeration already did for LIN.
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
 * Both figures comfortably fit within RCP_ACF_MAX_PAYLOAD (65535, acf.h).
 * Neither fits within RCP_AVTP_NTSCF_MAX_PAYLOAD (2047, avtp.h) -- and
 * NTSCF is the *only* AVTPDU format an RC Server itself ever sends
 * (avtp.h's own file header). A worst-case CAN XL frame response an RC
 * Server needs to send therefore cannot be carried in a single NTSCF
 * AVTPDU today; it fits in a single TSCF AVTPDU (RCP_AVTP_TSCF_MAX_PAYLOAD,
 * 65535) but TSCF is client-to-server only (avtp.h), so it is not a
 * general answer either. This was exactly the concrete driver ROADMAP.md
 * named for Phase 20's fragmentation go-decision (v0.76.0, the `ms`-bit/
 * segment_num mechanism, fragment.h) -- rcp_ep_can_encode_frame_response_fragmented()/
 * rcp_ep_can_decode_frame_response_fragment() below are that milestone's
 * retrofit of this endpoint, closing the single-AVTPDU-worst-case gap this
 * paragraph used to describe as open. rcp_ep_can_encode_frame_response()/
 * _decode_frame_response() above are unchanged and remain the right choice
 * whenever the caller already knows (or doesn't need to fragment) a
 * response fits in one AVTPDU -- fragmentation is opt-in per call, not a
 * behavior change to the existing single-frame codec.
 *
 * ── Wire layout: this module's own prefix-then-data choice ─────────────────
 *
 * A frame request/response's ACF payload is this module's own fixed-prefix-
 * then-raw-data layout (the same kind of "this module's own wire-layout
 * choice" ep_uart.h's own bit-padding scheme already established as this
 * codebase's convention for representing a structured concept the spec
 * names but does not itself lay out bit-for-bit): a big-endian 4-byte
 * arbitration_id, followed -- only when frame_format is one of the two CAN
 * XL variants -- by xl_header's big-endian sdt (1 byte), vcid (1 byte), and
 * af (4 bytes), followed by the raw CAN data bytes exactly as supplied
 * (this module never inspects, generates, or reformats any byte of the
 * data payload itself, the same "dumb pass-through for the data content"
 * philosophy ep_lin.h/ep_i2c.h/ep_uart.h already established for their own
 * raw-byte content -- only the frame's own structural fields, not its
 * data, are modeled here).
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

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_CAN_OK                   = 0,
    RCP_EP_CAN_ERR_SHORT_FRAME      = 1,
    RCP_EP_CAN_ERR_BAD_MSG_TYPE     = 2,
    RCP_EP_CAN_ERR_WRONG_BUS        = 3,
    RCP_EP_CAN_ERR_WRONG_OP         = 4,
    RCP_EP_CAN_ERR_BAD_FRAME_FORMAT = 5,
} rcp_ep_can_errc_t;

/* Human-readable message for an rcp_ep_can_errc_t value. Never returns NULL. */
const char *rcp_ep_can_strerror(rcp_ep_can_errc_t e);

/* ── Frame request ─────────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB frame request addressed to byte_bus_id: evt's low
 * three bits carry frame_format (any other bits of the ACF header's evt
 * field are left 0), and the payload is this module's own prefix-then-data
 * layout -- see the file header -- of arbitration_id, xl_header (only when
 * frame_format is a CAN XL variant), and tx_data[0..tx_len) (the raw CAN
 * data bytes; tx_data may be NULL iff tx_len == 0). Returns a zeroed
 * rcp_bytes_t (data=NULL) if: frame_format is not
 * rcp_ep_can_frame_format_valid(); arbitration_id is not
 * rcp_ep_can_arbitration_id_valid() for frame_format; tx_len exceeds
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
 * fixed header, its declared payload length, or this module's own
 * frame_format-dependent prefix length; RCP_EP_CAN_ERR_BAD_MSG_TYPE if b is
 * not an ACF_ABB message; RCP_EP_CAN_ERR_WRONG_BUS if its byte_bus_id !=
 * expected_bus_id; RCP_EP_CAN_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_WRITE; RCP_EP_CAN_ERR_BAD_FRAME_FORMAT if evt[2:0] is not
 * rcp_ep_can_frame_format_valid(). On RCP_EP_CAN_OK, *out_frame_format,
 * *out_arbitration_id, and *out_transaction_num are populated;
 * *out_xl_header is populated iff rcp_ep_can_frame_format_is_xl()
 * (*out_frame_format), left entirely untouched otherwise; *out_tx_data /
 * *out_tx_len are set to a *borrowed* view into b (not copied -- matching
 * every prior endpoint type's own raw-data decode convention) of the raw
 * CAN data bytes following this module's own prefix. */
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
 * length, or this module's own frame_format-dependent prefix length),
 * RCP_EP_CAN_ERR_WRONG_BUS (byte_bus_id != expected_bus_id), or
 * RCP_EP_CAN_ERR_BAD_FRAME_FORMAT (evt[2:0] is not
 * rcp_ep_can_frame_format_valid()). On RCP_EP_CAN_OK, *out_frame_format,
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
 * shares byte_bus_id/frame_format(evt)/op(READ)/transaction_num/timed/
 * timestamp with rcp_ep_can_encode_frame_response(); only the ms flag,
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
 * from b[0..len) -- the same peek-message-type/byte_bus_id/frame_format
 * validation rcp_ep_can_decode_frame_response() applies, but this
 * function does *not* strip this module's own prefix-then-data layout
 * from the payload (a fragment other than the first may not even contain
 * the whole prefix -- fragmentation operates on the flat combined byte
 * sequence, agnostic to its own internal structure, per fragment.h's file
 * header). Instead it surfaces the fragment's own ms bit,
 * read_size_or_segment_num (as *out_segment_num, meaningful only when
 * *out_ms), and raw ACF payload slice (*out_payload / *out_payload_len,
 * borrowed into b, matching every decode function in this module), for a
 * caller to feed straight into a rcp_fragment_reassembler_t
 * (fragment.h). Once reassembly reports RCP_FRAGMENT_REASM_COMPLETE, pass
 * the reassembled buffer to rcp_ep_can_decode_reassembled_frame_response()
 * to extract arbitration_id/xl_header/rx_data. Fails with the same
 * RCP_EP_CAN_ERR_SHORT_FRAME/_ERR_BAD_MSG_TYPE/_ERR_WRONG_BUS/
 * _ERR_BAD_FRAME_FORMAT conditions rcp_ep_can_decode_frame_response()
 * does; on RCP_EP_CAN_OK, every output parameter is populated. */
rcp_ep_can_errc_t rcp_ep_can_decode_frame_response_fragment(const uint8_t *b, size_t len,
                                                             rcp_byte_bus_id_t expected_bus_id,
                                                             rcp_ep_can_frame_format_t *out_frame_format,
                                                             bool *out_ms,
                                                             uint8_t *out_segment_num,
                                                             const uint8_t **out_payload,
                                                             size_t *out_payload_len,
                                                             bool *out_timed,
                                                             uint64_t *out_timestamp,
                                                             uint8_t *out_transaction_num);

/* Applies this module's own prefix-then-data parsing (see the file
 * header's "Wire layout" section) to a fully reassembled combined payload
 * -- rcp_fragment_reassembler_get()'s output once
 * rcp_fragment_reassembler_feed() has reported RCP_FRAGMENT_REASM_COMPLETE
 * -- for frame_format as recorded from (any of) that sequence's own
 * fragments (rcp_ep_can_decode_frame_response_fragment()'s *out_frame_format,
 * which is round-tripped identically on every fragment of one logical
 * response). This is the second half of what
 * rcp_ep_can_decode_frame_response() does in one step for a single,
 * unfragmented frame. Returns RCP_EP_CAN_ERR_SHORT_FRAME if reassembled_len
 * is shorter than frame_format's own prefix length; RCP_EP_CAN_ERR_BAD_FRAME_FORMAT
 * if frame_format itself is not rcp_ep_can_frame_format_valid(). On
 * RCP_EP_CAN_OK, *out_arbitration_id / *out_xl_header are populated (the
 * latter only when rcp_ep_can_frame_format_is_xl(frame_format)) and
 * *out_rx_data / *out_rx_len are set to a *borrowed* view into reassembled. */
rcp_ep_can_errc_t rcp_ep_can_decode_reassembled_frame_response(const uint8_t *reassembled,
                                                                size_t reassembled_len,
                                                                rcp_ep_can_frame_format_t frame_format,
                                                                uint32_t *out_arbitration_id,
                                                                rcp_ep_can_xl_header_t *out_xl_header,
                                                                const uint8_t **out_rx_data,
                                                                size_t *out_rx_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_CAN_H */
