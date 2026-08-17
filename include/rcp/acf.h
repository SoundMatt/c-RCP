/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-ACF-001
//cfusa:req REQ-ACF-002
//cfusa:req REQ-ACF-004
//cfusa:req REQ-ACF-005
//cfusa:req REQ-ACF-006
//cfusa:req REQ-ACF-007
//cfusa:req REQ-ACF-008
//cfusa:req REQ-ACF-009
//cfusa:req REQ-ACF-010
//cfusa:req REQ-ACF-011
//cfusa:req REQ-ACF-012
//cfusa:req REQ-ACF-013
//cfusa:req REQ-ACF-014
//cfusa:req REQ-ACF-015
//cfusa:req REQ-ACF-016

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-ACF-017
//cfusa:req REQ-ACF-018
//cfusa:req REQ-ACF-019
//cfusa:req REQ-ACF-020
//cfusa:req REQ-ACF-021
//cfusa:req REQ-ACF-022
/*
 * acf.h -- ACF message format + byte_message_info header for the TC18
 * Remote Control Protocol wire layer (ROADMAP.md Phase 13, "Wire Format
 * Core", milestone 60).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing added in avtp.h/avtp.c (milestone 59): an ACF message is exactly
 * what an NTSCF or TSCF frame's payload carries. Nothing in rcp.h, wire.c,
 * or any satellite package is touched here, and this header depends on
 * avtp.h only for rcp_byte_bus_id_t -- the same endpoint-addressing type
 * every module built against this wire layer reuses.
 *
 * Two ACF message variants are modeled, both sharing one
 * byte_message_info header layout:
 *
 *   - ACF_ABB (acf_msg_type = 0x0E): carries no timestamp field.
 *   - ACF_GBB (acf_msg_type = 0x0D): carries a 64-bit message_timestamp
 *     immediately after the shared header.
 *
 * byte_message_info field layout -- this now reproduces the OPEN Alliance
 * TC18 Remote Control Protocol Specification v0.5.1_RC's own Figure 7 /
 * Table 4 bit-for-bit (pixel-verified against the rendered spec page; see
 * ROADMAP.md/CHANGELOG.md for the v0.100.0 conformance-fix entry this
 * header layout landed in). Earlier revisions of this file used a
 * different, self-invented byte layout that was never wire-compatible
 * with a real TC18 peer or with this project's own go-RCP/cpp-RCP ports;
 * that layout has been fully replaced, and this is a breaking wire-format
 * change from every prior tagged release.
 *
 *   Octet   Bits    Field
 *   0       7:1     acf_msg_type
 *           0       acf_msg_length[8] (MSB)
 *   1       7:0     acf_msg_length[7:0]
 *   2       7:6     pad        (count of trailing zero pad octets, 0-3)
 *           5       mtv        (message_timestamp valid; ACF_ABB has no
 *                                timestamp field, so always encodes 0)
 *           4:3     rsv        (always 0b on encode; not interpreted on
 *                                decode -- see the file header's decode
 *                                leniency note below)
 *           2:0     byte_bus_id[10:8]
 *   3       7:0     byte_bus_id[7:0]
 *   4       7:4     evt
 *           3:2     rsv        (always 0b on encode; see above)
 *           1       hs
 *           0       cs
 *   5       7:0     transaction_num
 *   6       7       op         (0 = read/expects a data response,
 *                                1 = write/no data response expected --
 *                                see rcp_acf_op_t's own doc comment for
 *                                how this project's 3-state application
 *                                convenience enum maps onto this single
 *                                wire bit)
 *           6       rsp
 *           5       err
 *           4       ms
 *           3:0     read_size_or_segment_num[11:8]
 *   7       7:0     read_size_or_segment_num[7:0]
 *
 * acf_msg_length is a QUADLET count (not an octet count) spanning the
 * entire ACF message this header belongs to: this 8-byte header (plus the
 * 8-byte message_timestamp for ACF_GBB) + payload + the pad octets pad
 * counts, rounding the whole thing up to a whole number of quadlets --
 * confirmed against the specification's own Figure 19 (ACF_ABB) and
 * Figure 20 (ACF_GBB) worked examples (see acf.c's encode functions and
 * tests/test_acf.c's golden-vector tests pinning both). It deliberately
 * does NOT include any trailing CRC32 safe-point e2e.c may append --
 * e2e.c's rcp_e2e_wrap()/_unwrap() adapt this field by +-1 quadlet
 * themselves when adding/removing that trailer, since the CRC32 append is
 * a layer above this one and this module has no knowledge of it.
 *
 * ACF_GBB appends an 8-byte big-endian message_timestamp at offset 8,
 * making RCP_ACF_GBB_HEADER_LEN exactly RCP_ACF_ABB_HEADER_LEN + 8 -- the
 * presence/absence of that field is the only structural difference
 * between the two variants.
 *
 * rcp_acf_pack_header()/rcp_acf_unpack_header() below expose this header's
 * bit-packing directly (not just through rcp_acf_encode_abb()/_gbb()) so
 * that other modules which must build or inspect a raw ACF_GBB header of
 * their own -- request_compound.c, request_cancel.c, request_timed.c,
 * request_triggered.c, request_chained.c, all of which repurpose the
 * message_timestamp region when mtv=0 and therefore cannot go through
 * rcp_acf_encode_gbb()'s own "zero the timestamp when untimed" rule --
 * never have to duplicate this bit layout themselves. This is the single
 * source of truth for byte_message_info's wire representation.
 *
 * Fields hs, cs, rsp, ms, and pad belong to functionality this milestone
 * deliberately does not implement in full (conditional requests &
 * sequencers and compound bundles respectively) -- they are encoded and
 * decoded unmodified (round-tripped) except pad, which
 * rcp_acf_encode_abb()/_gbb() now compute and overwrite themselves (see
 * rcp_acf_pad_len()) so the encoded length is always a whole number of
 * quadlets, matching acf_msg_length's own unit. Only mtv, evt, op, and err
 * carry behavior in this milestone (the timestamp-folding rule and the
 * four response-semantics identification rules below).
 *
 * Scope: this milestone models the Standard request kind only
 * (best-effort, unconditional, mandatory). Conditional requests,
 * cancellation, and fragmentation are out of scope here and are tracked
 * for their own later phases (17 and 20 respectively) -- read_size and
 * segment_num share the same on-wire slot for exactly this reason: which
 * interpretation applies is a function of hdr->op (read vs. not), and
 * fragmentation's own use of a segment counter is deliberately left
 * unimplemented rather than smuggled into this milestone.
 */
#ifndef RCP_ACF_H
#define RCP_ACF_H

#include "rcp/avtp.h"
#include "rcp/errors.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* acf_msg_type byte values for the two variants modeled by this milestone. */
#define RCP_ACF_MSG_TYPE_ABB ((uint8_t)0x0Eu)
#define RCP_ACF_MSG_TYPE_GBB ((uint8_t)0x0Du)

/* Fixed header lengths in octets, excluding the variable-length payload
 * that follows. RCP_ACF_GBB_HEADER_LEN is exactly RCP_ACF_ABB_HEADER_LEN
 * plus the 8-byte message_timestamp field. */
#define RCP_ACF_ABB_HEADER_LEN ((size_t)8u)
#define RCP_ACF_GBB_HEADER_LEN ((size_t)16u)

/* acf_msg_length is a 9-bit quadlet count (Table 4) -- the largest ACF
 * message (header/timestamp + payload + pad) this codec can represent. */
#define RCP_ACF_MAX_QUADLETS ((uint16_t)0x1FFu)

/* Largest payload rcp_acf_encode_abb()/_gbb() can represent, derived from
 * RCP_ACF_MAX_QUADLETS*4 minus the fixed header (and, for GBB, timestamp)
 * length -- both figures already land on a quadlet boundary, so no
 * additional pad headroom is lost. Encoding a larger payload fails. */
#define RCP_ACF_ABB_MAX_PAYLOAD ((size_t)(RCP_ACF_MAX_QUADLETS * 4u - RCP_ACF_ABB_HEADER_LEN))
#define RCP_ACF_GBB_MAX_PAYLOAD ((size_t)(RCP_ACF_MAX_QUADLETS * 4u - RCP_ACF_GBB_HEADER_LEN))

/* Deprecated: kept for source compatibility with existing call sites that
 * used a single bound for either variant. Equal to the more conservative
 * (smaller) of the two variant-specific bounds above -- callers that need
 * the full ABB-specific headroom should use RCP_ACF_ABB_MAX_PAYLOAD
 * instead. This is dramatically smaller than the pre-conformance-fix
 * value (65535): that old figure came from an invented 16-bit octet-count
 * length field this module no longer has -- see the file header. */
#define RCP_ACF_MAX_PAYLOAD RCP_ACF_GBB_MAX_PAYLOAD

/* RCP_ACF_ERR_BUS_ID_OVERFLOW (formerly =3, decoded byte_bus_id[10:8]
 * nonzero exceeding what rcp_byte_bus_id_t could hold) is RETIRED as of
 * REQ-RMAP-053/REQ-ACF-020's own fix batch -- see .fusa-reqs.json for
 * both entries' full retirement/rewrite text. rcp_byte_bus_id_t
 * (avtp.h) now holds the full 11-bit wire range, so the condition this
 * code represented can no longer occur: busid_full's own bit-extraction
 * (rcp_acf_unpack_header(), acf.c) is mathematically bounded to 0x7FF,
 * a value the widened type always represents. Removed outright, not
 * deprecated-then-removed -- nothing in this codebase (including
 * errors.c's TC18 Table 30 wire-error mapping, checked) referenced it
 * for anything but this now-impossible condition, and it was this
 * enum's last value, so no other member's numeric value changes. */
typedef enum {
    RCP_ACF_OK               = 0,
    RCP_ACF_ERR_SHORT_FRAME  = 1,
    RCP_ACF_ERR_BAD_MSG_TYPE = 2,
} rcp_acf_errc_t;

/* Human-readable message for an rcp_acf_errc_t value. Never returns NULL. */
const char *rcp_acf_strerror(rcp_acf_errc_t e);

/* ── mtv: message-timestamp validity (ACF_GBB only) ───────────────────────── */

/* mtv is a single wire bit: whether the message_timestamp region (only
 * present for ACF_GBB) holds a genuinely valid timestamp. There is no third
 * "uncertain" wire state -- Table 17 (general responses usage, §11.3)
 * spells out both encodings in full ("0b - message_timestamp not valid ...
 * / 1b - message_timestamp valid ..."), and every other GBB-usage table in
 * the specification (Table 4 included) is consistent with that same
 * two-value field. An earlier revision of this header modeled a third
 * state (RCP_ACF_MTV_UNCERTAIN) in a 2-bit field of its own invention;
 * that field never had a real TC18 encoding and has been removed. Callers
 * that need to represent "timestamp present but not trustworthy" must fold
 * that into RCP_ACF_MTV_UNTIMED, the same "not confidently timed"
 * treatment rcp_acf_gbb_is_timed() already gives it. */
typedef enum {
    RCP_ACF_MTV_UNTIMED = 0,
    RCP_ACF_MTV_VALID   = 1,
} rcp_acf_mtv_t;

/* ── op: the operation this message's byte_bus_id addresses ───────────────── */

/* op is a SINGLE wire bit (Table 4 octet 6 bit 7): 0 selects the
 * read/expects-a-data-response sense, 1 selects write/no-data-response.
 * There is no third wire state. This project's application-facing
 * rcp_acf_op_t nonetheless keeps a third value, RCP_ACF_OP_NONE, as a
 * convenience for endpoints that send a command-shaped message carrying
 * no register read/write semantics of its own (e.g. ep_wakeup.c's SleepCMD
 * and wakeup messages): rcp_acf_encode_abb()/_gbb() encode
 * RCP_ACF_OP_NONE identically to RCP_ACF_OP_WRITE on the wire (op=1,
 * "no data response expected"), since that is the only one of the two
 * real wire states it can be without inventing a bit TC18 does not have.
 * Decoding therefore never produces RCP_ACF_OP_NONE -- a decoded header's
 * op is always RCP_ACF_OP_READ or RCP_ACF_OP_WRITE, reflecting what a
 * real TC18 peer can actually distinguish on the wire. */
typedef enum {
    RCP_ACF_OP_NONE  = 0, /* encode-only: no read/write data operation of
                              its own (e.g. a command message); encodes
                              identically to RCP_ACF_OP_WRITE on the wire */
    RCP_ACF_OP_WRITE = 1,
    RCP_ACF_OP_READ  = 2,
} rcp_acf_op_t;

/* ── The four response semantics ───────────────────────────────────────────── */

typedef enum {
    RCP_ACF_RESP_ACKNOWLEDGE = 0,
    RCP_ACF_RESP_WRITE       = 1,
    RCP_ACF_RESP_READ        = 2,
    RCP_ACF_RESP_ERROR       = 3,
} rcp_acf_response_kind_t;

/* TC18 Table 17's evt[3:0] = 0xF value, the wire marker for an Acknowledge
 * response -- see rcp_acf_classify_response()'s doc comment for why this
 * must be checked before op/err. */
#define RCP_ACF_EVT_ACKNOWLEDGE ((uint8_t)0x0Fu)

/* ── byte_message_info: the shared header ─────────────────────────────────── */

typedef struct {
    uint8_t           acf_msg_type;   /* RCP_ACF_MSG_TYPE_ABB/_GBB on decode;
                                         ignored by rcp_acf_encode_abb()/
                                         _gbb() (implied by which function
                                         is called) */
    uint16_t          acf_msg_length; /* quadlet count over the whole ACF
                                         message (see the file header);
                                         ignored by rcp_acf_encode_abb()/
                                         _gbb(), which recompute it */
    uint8_t           pad;            /* 0-3 trailing pad octets; ignored
                                         by rcp_acf_encode_abb()/_gbb(),
                                         which recompute it via
                                         rcp_acf_pad_len() */
    uint8_t           mtv;            /* rcp_acf_mtv_t; ABB always encodes/
                                         decodes this as RCP_ACF_MTV_UNTIMED,
                                         since ABB has no timestamp field to
                                         validate */
    rcp_byte_bus_id_t byte_bus_id;
    uint8_t           evt;            /* 0-15; see rcp_acf_classify_response() */
    uint8_t           hs;             /* 0/1; round-tripped unmodified */
    uint8_t           cs;             /* 0/1; round-tripped unmodified */
    uint8_t           transaction_num;
    uint8_t           op;             /* rcp_acf_op_t; see its own doc
                                         comment for the encode/decode
                                         asymmetry around RCP_ACF_OP_NONE */
    uint8_t           rsp;            /* 0/1; round-tripped unmodified */
    uint8_t           err;            /* 0/1 */
    uint8_t           ms;             /* 0/1; round-tripped unmodified */
    uint16_t          read_size_or_segment_num; /* 0-4095 (12 bits); meaning
                                         depends on op: read_size when
                                         op == RCP_ACF_OP_READ, otherwise
                                         segment_num (unused by this
                                         milestone's Standard-request-only
                                         scope, but round-tripped) */
} rcp_acf_byte_message_info_t;

/* Classifies a decoded byte_message_info header into one of the four
 * response semantics per TC18 Table 17/§11.3.1-§11.3.4: evt[3:0] ==
 * RCP_ACF_EVT_ACKNOWLEDGE (0xF) takes priority over everything else --
 * an Acknowledge stays an Acknowledge whether err is 0 (request filed) or
 * 1 (request rejected); the spec's Error Response kind (§11.3.4) is a
 * distinct, evt[3:0] < 0x9 case, not what a rejected Acknowledge becomes.
 * Below that, err takes priority (any remaining message with err set is
 * an Error response regardless of op), otherwise op selects between Write
 * response and Read response. There is no further sub-classification of
 * an Acknowledge by evt value: TC18's own evt[3:0] field table (§11.3)
 * defines exactly one Acknowledge encoding (0xF) -- 0x0 is
 * simple/data/error response, 0x1-0x8 is a repetitive-response counter,
 * and 0x9-0xE is reserved, none of which apply once evt[3:0] == 0xF has
 * already matched. The op-based ACKNOWLEDGE fallback below (hdr->op ==
 * RCP_ACF_OP_NONE) is unreachable from decoded input (see rcp_acf_op_t's
 * doc comment) -- it remains only for callers that construct hdr by hand
 * with op left at its zero value and no evt set either. */
rcp_acf_response_kind_t rcp_acf_classify_response(const rcp_acf_byte_message_info_t *hdr);

/* ── REQ-ACF-018: read_size_or_segment_num's dual interpretation ──────────── */

/* Which interpretation the 12-bit read_size_or_segment_num field (Table 4
 * octet 6:7) carries -- a function of op alone, per TC18 §11.2.1 Table 4 /
 * §11.2.2.1 Table 6: the read sense (RCP_ACF_OP_READ, wire op=0, "expects
 * a data response") means the field is read_size; every other op value
 * means it is segment_num instead. Previously left for every caller to
 * reimplement this same op check itself (or, worse, to read the field
 * without checking op at all) -- this is the single, shared, directly-
 * testable classification. */
typedef enum {
    RCP_ACF_RSS_READ_SIZE   = 0,
    RCP_ACF_RSS_SEGMENT_NUM = 1,
} rcp_acf_rss_kind_t;

/* hdr->read_size_or_segment_num is read_size iff hdr->op ==
 * RCP_ACF_OP_READ; otherwise it is segment_num (RCP_ACF_OP_WRITE and the
 * encode-only RCP_ACF_OP_NONE both select the write sense on the wire --
 * see rcp_acf_op_t's own doc comment -- so both classify as segment_num
 * here, matching what a real decoded peer would see). */
rcp_acf_rss_kind_t rcp_acf_read_size_or_segment_num_kind(const rcp_acf_byte_message_info_t *hdr);

/* ── REQ-ACF-021: fixed-value fields on an encoded request ─────────────────── */

/* True iff hdr's hs/rsp/err fields are the fixed value TC18 requires on an
 * encoded REQUEST (rsv is already always forced to 0 by
 * rcp_acf_pack_header() itself; see the file header): hs=0b, rsp=0b,
 * err=0b unconditionally, and cs=0b UNLESS cs_has_meaning is true --
 * compound-wait (TC18 §11.2.2.3 Table 8) and chained (§11.2.2.6 Table 11)
 * are the only two request kinds that assign cs a meaning of its own, so a
 * caller building one of those two kinds passes true; every other request
 * kind passes false.
 *
 * This is a pure, directly-testable validator, not an encode-time
 * enforcement rcp_acf_encode_abb()/_gbb() perform themselves -- those two
 * functions are shared by request AND response encoding (e.g.
 * rcp_acf_build_error_response() deliberately sets rsp=1/err=1), so they
 * cannot force these fields to their request-only fixed values
 * unconditionally. A caller building a request -- the conditional-request
 * modules (request_compound.c/_triggered.c/_chained.c/_timed.c/_cancel.c)
 * and every endpoint's own request encoder -- is the one place that knows
 * it is building a request rather than a response, and can assert this
 * before encoding. */
bool rcp_acf_request_header_constraints_valid(const rcp_acf_byte_message_info_t *hdr,
                                               bool cs_has_meaning);

/* True iff hdr's rsp bit is 0 -- TC18's own rsp field description (Table 4)
 * states rsp=1b identifies a response; a decoded message with rsp=1 set
 * must not be admitted as a request. A caller decoding an inbound frame it
 * intends to treat as a request (as opposed to a response it sent itself
 * and is now receiving back, e.g. over a loopback or multicast-style
 * transport) should call this before admission and refuse the frame if it
 * returns false, rather than silently processing what TC18 itself labels
 * a response as though it were a request. */
bool rcp_acf_header_is_request(const rcp_acf_byte_message_info_t *hdr);

/* TC18 §13.5 Table 33's shared rule for the {ADC, PWM_IN, I2C, LIN, CAN,
 * UART, ISELED, MDIO} endpoint-type row: evt[2:0] = 000b is the only
 * value a plain (non-configuration) request in this row may carry --
 * every other value is either reserved (001b-110b, request shall be
 * rejected with error code UNSUPPORTED_CMD) or selects an entirely
 * different, configuration-write-shaped request (111b, TC18 §12.7.1
 * Figure 18) that a plain read/write decoder should never accept. This
 * row's endpoint types must call this on a decoded request header's evt
 * before treating it as a plain request; a decoder that skips this check
 * silently accepts wire values TC18 requires it to refuse.
 *
 * Returns true iff (evt & 0x7) == 0. Not meaningful for SPI (its own
 * dedicated Table 33 row, a real 6-value channel selector) or GPIO/
 * PWM_OUT (their own dedicated Table 33 row, the 8-value write-semantics
 * selector) -- see rcp_ep_gpio_write_semantics_valid()/
 * rcp_ep_spi_channel_valid() for those rows' own rules instead. */
bool rcp_acf_evt_row2_is_plain(uint8_t evt);

/* ── TC18 §13.5.1: compound-wait's own, endpoint-type-independent evt[2:0] rule ─ */

/* TC18 §13.5.1 gives evt[2:0] an entirely different meaning for a
 * compound-wait request than Table 33 gives it for a Standard request:
 * here it selects one of eight ways to compare that request's own
 * byte_msg_payload against the addressed endpoint's current status, and
 * this rule is the SAME across every endpoint type -- unlike Table 33,
 * there is no per-endpoint-type row. rcp_acf_evt_row2_is_plain() and
 * rcp_ep_spi_channel_valid()/rcp_ep_gpio_write_semantics_valid() (Table
 * 33's own per-row rules) do not apply to a compound-wait request's evt;
 * use these two functions instead.
 *
 * true iff (evt & 0x7) != 0x3 -- every value except the reserved 011b,
 * which callers must reject with error code UNSUPPORTED_CMD rather than
 * passing to rcp_acf_compound_wait_match() below (that function's return
 * value for a reserved evt is not a meaningful "never matches" result --
 * see its own doc comment). */
bool rcp_acf_compound_wait_evt_valid(uint8_t evt);

/* TC18 §13.5.1: evaluates whether payload[0..payload_len) matches
 * status[0..status_len) under the comparison mode evt[2:0] selects.
 * Callers must call rcp_acf_compound_wait_evt_valid(evt) first and reject
 * a false result (UNSUPPORTED_CMD) rather than calling this function --
 * its own return value for evt[2:0] == 011b is unconditionally false, not
 * a meaningful "reserved" signal distinct from a real non-match.
 *
 * Length rule (applies before any mode-specific comparison, per the
 * specification's own wording and its own SPI example -- "only the first
 * four out of 20 received bytes will be checked when the byte_msg_payload
 * in the compound wait has only four bytes"): if status_len < payload_len
 * the condition never matches (false, regardless of mode, and regardless
 * of the two buffers' actual contents); otherwise status is compared only
 * against its own first payload_len bytes, and any bytes beyond that are
 * never read or considered. This rule is NOT specific to any one endpoint
 * type or to a fixed comparison width -- it is payload_len itself, of
 * whatever length that request happens to carry.
 *
 * Modes (evt[2:0]):
 *   000b exact match:        payload[0..n) == status[0..n), byte for byte.
 *   001b AND-with-1s-mask:   for every byte i, (payload[i] & status[i]) |
 *                            ~payload[i] == 0xFF -- every payload bit that
 *                            is 1 must also be 1 in status; payload bits
 *                            that are 0 place no constraint on status.
 *   010b AND-with-0s-mask:   for every byte i, payload[i] & status[i] ==
 *                            0x00 -- every payload bit that is 1 must be 0
 *                            in status.
 *   100b/101b: the first two bytes of payload's own leading quadlet,
 *              read big-endian, are >= (100b) or <= (101b) the same two
 *              bytes of status. Returns false (never reads OOB) if
 *              payload_len < 4 -- the specification assumes a whole
 *              leading quadlet exists; this module's own fail-safe
 *              default for when it does not, matching this project's
 *              established too-short-buffer convention.
 *   110b/111b: same as 100b/101b, but the LAST two bytes of the leading
 *              quadlet (payload[2..4)), and the same payload_len < 4
 *              fail-safe.
 *
 * status/payload may be NULL iff their respective length is 0. */
bool rcp_acf_compound_wait_match(uint8_t evt, const uint8_t *payload, size_t payload_len,
                                  const uint8_t *status, size_t status_len);

/* ── byte_message_info bit packing (single source of truth) ───────────────── */

/* Packs the 8-byte byte_message_info header (Table 4, see the file header)
 * into out[0..8), given the already-computed acf_msg_type and
 * acf_msg_length (in quadlets). hdr supplies every other field, including
 * pad -- callers that need rcp_acf_pad_len()'s automatic pad/quadlet
 * accounting (i.e. everything except the request_* modules' own repurposed-
 * timestamp builders) should prefer rcp_acf_encode_abb()/_gbb() instead of
 * calling this directly. */
void rcp_acf_pack_header(uint8_t out[8], uint8_t acf_msg_type, uint16_t acf_msg_length,
                          const rcp_acf_byte_message_info_t *hdr);

/* Unpacks the 8-byte byte_message_info header from in[0..8) into *out_hdr
 * (every field, including acf_msg_type and acf_msg_length). Reserved bits
 * (rsv at octet 2 bits 4:3 and octet 4 bits 3:2) are not validated -- a
 * peer that sets them is tolerated, not rejected, per this module's
 * decode-leniency convention. Always succeeds (returns RCP_ACF_OK): the
 * full 11-bit byte_bus_id[10:8]+[7:0] decodes into *out_hdr->byte_bus_id
 * unconditionally -- rcp_byte_bus_id_t (avtp.h) is wide enough to hold
 * the entire wire range, so there is no overflow case left to reject
 * (REQ-RMAP-053/REQ-ACF-020; this function used to return
 * RCP_ACF_ERR_BUS_ID_OVERFLOW here, now retired). */
rcp_acf_errc_t rcp_acf_unpack_header(const uint8_t in[8], rcp_acf_byte_message_info_t *out_hdr);

/* ── Peeking a GBB frame's own request_type without a full kind-specific decode ── */
/*
 * Every conditional-request module (request_compound.h/_triggered.h/
 * _chained.h/_timed.h) places its own request_type opcode at the SAME
 * fixed offset -- octet 0 of the 8-byte message_timestamp region this
 * file header's own "ACF_GBB appends an 8-byte big-endian message_
 * timestamp at offset 8" already establishes, repurposed identically by
 * every one of those modules' own "shared repurposing trick" (see each
 * module's own doc comment). This module previously had no shared way to
 * read that one byte without a caller already knowing (and correctly
 * calling into) one specific kind's own decoder -- a real, recurring gap
 * that blocked more than one fix needing only to classify a GBB frame,
 * not fully decode it (e.g. REQ-SRV-015's own GBB half, below).
 *
 * Returns true and sets *out_request_type to frame[8] iff frame_len >= 9
 * and the header's own acf_msg_type is RCP_ACF_MSG_TYPE_GBB; returns
 * false (*out_request_type left unchanged) for an ABB frame (no
 * request_type concept exists on that wire shape at all) or a frame too
 * short to hold byte_message_info(8) + the request_type octet(1). Does
 * NOT itself validate that the returned byte is one of the six currently-
 * defined request_type values -- callers compare it against each
 * module's own RCP_REQUEST_TYPE_* constants or is-this-kind predicate
 * (e.g. rcp_request_type_is_compound_wait(), request_compound.h). */
bool rcp_acf_peek_gbb_request_type(const uint8_t *frame, size_t frame_len,
                                    uint8_t *out_request_type);

/* Returns the number of zero pad octets (0-3) needed to bring unpadded_len
 * octets of header(+timestamp)+payload up to a whole number of quadlets --
 * the unit acf_msg_length is expressed in. Exposed so any module building
 * a raw ACF_GBB header of its own (the request_* modules) can compute the
 * same pad/quadlet accounting rcp_acf_encode_abb()/_gbb() use internally. */
uint8_t rcp_acf_pad_len(size_t unpadded_len);

/* The effective number of octets to be written by an EP0 register-write
 * request -- REQ-RMAP-069 (TC18 §13.7.1.2): "Effective number of bytes to
 * be written to register map = (acf_msg_length - 3) x 4 - pad - 2."
 * acf_msg_length is the decoded header field of the same name (this
 * function does no decoding of its own -- a caller passes
 * hdr.acf_msg_length/hdr.pad straight through, mirroring
 * rcp_acf_pad_len()'s own "small pure arithmetic helper" scope).
 *
 * FIXED 2026-08-11 (spec rebaseline to TC18 0.5.1_RC5): the old baseline
 * spec's formula (0.5.1_RC, 2026-07-14) omitted a trailing "- 2" term,
 * and this function matched that omission exactly. Spec revision RC5
 * corrects the formula and, per its own updated Figure 22, the "- 2"
 * subtracts the 2-octet register start address that leads the byte
 * payload -- the 3-quadlet region this formula already subtracts is the
 * fixed 2-quadlet ACF byte_message_info header plus one further quadlet
 * of framing, and does NOT itself include the address (unlike this
 * function's own pre-fix doc comment incorrectly assumed). This function
 * does not attempt to additionally exclude any trailing CRC octets from
 * a safe-command-mode write -- RC5 also newly clarifies EP0 is *always*
 * accessed in safe command mode (no longer conditional on the request),
 * which may mean CRC octets need their own accounting here too; that
 * question is flagged, not yet resolved, since no production caller of
 * this function exists yet to depend on either answer (see REQ-RMAP-069
 * and this session's own spec-rebaseline project notes for the open
 * question).
 *
 * Returns 0, never underflowing to a huge size_t, if acf_msg_length is
 * too small to contain the fixed 3-quadlet region at all (< 3), or if
 * pad plus the 2-octet address exceeds what remains after subtracting
 * it -- both cases describe a malformed or adversarial frame, and 0
 * effective data octets is this function's own fail-safe reading of
 * that, not an out-of-band error code (this is a pure arithmetic helper,
 * like rcp_acf_pad_len(); a caller wanting frame-level rejection uses
 * rcp_acf_unpack_header()'s own validation first). */
size_t rcp_acf_reg_write_len(uint16_t acf_msg_length, uint8_t pad);

/* ── ACF_ABB ───────────────────────────────────────────────────────────────── */

/* Encodes hdr plus a payload of payload_len octets (payload may be NULL
 * iff payload_len == 0) into a freshly heap-allocated ACF_ABB message.
 * hdr's own acf_msg_type, acf_msg_length, and pad are ignored on encode --
 * pad is recomputed via rcp_acf_pad_len() and acf_msg_length from the
 * resulting total quadlet count, and between payload and the trailing
 * pad|CRC region a peer expects, zero pad octets are appended so the
 * encoded length is always a whole number of quadlets. Returns a zeroed
 * rcp_bytes_t (data=NULL) if payload_len exceeds RCP_ACF_ABB_MAX_PAYLOAD
 * or on allocation failure. Caller frees the result with
 * rcp_bytes_free(). */
/* Builds a TC18 §12.9.6 error response for a request already known to
 * carry byte_bus_id and transaction_num: "The error response shall
 * contain the byte_bus_id and transaction number of the request. The
 * error response shall contain a byte_msg_payload with an error code."
 * error_code is encoded as the payload's single octet, matching Table
 * 30's own small (1-17) numeric range. hdr fields not explicitly listed
 * here are set to produce a header §11.3.4 ("evt[3:0] < 0x9 and err = 1
 * is an Error Response") classifies correctly when decoded: evt = 0 (any
 * value other than RCP_ACF_EVT_ACKNOWLEDGE classifies the same way once
 * err is set -- see rcp_acf_classify_response()'s own doc comment --
 * 0 is simply the simplest choice), err = 1, rsp = 1, op =
 * RCP_ACF_OP_NONE (op does not affect classification once err is set).
 * Encoded as ACF_ABB (no timestamp) -- a caller needing a timestamped
 * error response builds its own ACF_GBB header with these same field
 * values via rcp_acf_pack_header()/rcp_acf_encode_gbb(), matching this
 * codebase's existing ABB/GBB split convention elsewhere. Returns a
 * zeroed rcp_bytes_t (data=NULL) only on allocation failure -- a single
 * payload octet is always within RCP_ACF_ABB_MAX_PAYLOAD. Caller frees
 * the result with rcp_bytes_free(). */
rcp_bytes_t rcp_acf_build_error_response(rcp_byte_bus_id_t byte_bus_id,
                                          uint8_t transaction_num,
                                          rcp_wire_error_t error_code);

/* FIXED 2026-08-12 (issue #201, REQ-SRV-016): TC18 §13.5's own opening
 * statement, before its per-endpoint-type evt[2:0] table: "evt[3] is used
 * to request an acknowledge. I.e. evt[3]=1 requests acknowledge." This is
 * universal across every endpoint type (unlike evt[2:0], which Table 33
 * gives a different meaning per row) -- a caller anywhere in the request-
 * admission path can check it without knowing which endpoint type it is
 * looking at. Returns (evt & 0x08u) != 0. */
bool rcp_acf_evt_requests_acknowledge(uint8_t evt);

/* FIXED 2026-08-12 (issue #201, REQ-SRV-016): builds a genuine
 * Acknowledge response -- rcp_acf_classify_response()'s own
 * RCP_ACF_RESP_ACKNOWLEDGE, evt = RCP_ACF_EVT_ACKNOWLEDGE (0x0F) -- for a
 * request already known to carry byte_bus_id and transaction_num, and
 * whose own evt[3] asked for one (rcp_acf_evt_requests_acknowledge()).
 * hdr fields not explicitly listed here: err = 0 (the request was
 * accepted, just not executed yet -- see TC18 §12.3.1.3's "if requested
 * an acknowledge is sent after storing the request", the specific case
 * this function exists for), rsp = 1, op = RCP_ACF_OP_NONE (op does not
 * affect classification once evt[3:0] == 0x0F -- see
 * rcp_acf_classify_response()'s own doc comment, which checks evt before
 * op). Encoded as ACF_ABB (no timestamp), mirroring
 * rcp_acf_build_error_response()'s own ABB/GBB-split convention. Returns
 * a zeroed rcp_bytes_t (data=NULL) only on allocation failure -- there is
 * no payload, always within RCP_ACF_ABB_MAX_PAYLOAD. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_acf_build_acknowledge_response(rcp_byte_bus_id_t byte_bus_id,
                                                uint8_t transaction_num);

/* FIXED 2026-08-14 (issue #430, REQ-ACF-033): TC18 §11.3.1's OTHER
 * Acknowledge shape, distinct from rcp_acf_build_acknowledge_response()
 * above -- same evt = RCP_ACF_EVT_ACKNOWLEDGE (0x0F), but for a request
 * that was never filed into EP request storage at all: "err = 1
 * indicates that the request has been rejected. The byte_msg_payload
 * contains an error code." This is NOT the same wire shape as
 * rcp_acf_build_error_response()'s §11.3.4 Error Response (evt[3:0] < 0x9,
 * err = 1) -- that shape is for a request already accepted into storage
 * whose later EXECUTION fails; this one is for admission itself refusing
 * to file the request in the first place (e.g. request-store full, a
 * malformed opcode, or any other rcp_server_endpoint_admit()
 * RCP_SERVER_ADMIT_REJECTED outcome). Both shapes decode identically
 * through rcp_acf_unpack_header() into evt/err, but only THIS function's
 * evt=0xF lets rcp_acf_classify_response() recognize the response as an
 * Acknowledge (still RCP_ACF_RESP_ACKNOWLEDGE, per that function's own
 * "err=1 Acknowledge is still an Acknowledge, not an Error Response" doc
 * comment) rather than RCP_ACF_RESP_ERROR.
 *
 * hdr fields not explicitly listed here: err = 1, rsp = 1, op =
 * RCP_ACF_OP_NONE (op does not affect classification once evt[3:0] ==
 * 0x0F). error_code is encoded as the payload's single octet, the same
 * convention rcp_acf_build_error_response() uses for its own Table 30
 * code. Encoded as ACF_ABB (no timestamp), mirroring both sibling
 * builders' own ABB/GBB-split convention. Returns a zeroed rcp_bytes_t
 * (data=NULL) only on allocation failure -- a single payload octet is
 * always within RCP_ACF_ABB_MAX_PAYLOAD. Caller frees the result with
 * rcp_bytes_free(). */
rcp_bytes_t rcp_acf_build_acknowledge_rejected_response(rcp_byte_bus_id_t byte_bus_id,
                                                          uint8_t transaction_num,
                                                          rcp_wire_error_t error_code);

rcp_bytes_t rcp_acf_encode_abb(const rcp_acf_byte_message_info_t *hdr,
                                const uint8_t *payload, size_t payload_len);

/* Decodes an ACF_ABB message from b[0..len). On RCP_ACF_OK, *out_hdr is
 * populated (with mtv forced to RCP_ACF_MTV_UNTIMED, ABB having no
 * timestamp to validate) and *out_payload / *out_payload_len point into
 * b (borrowed, not copied -- matching avtp.c's decode_* convention);
 * *out_payload_len excludes hdr->pad's trailing pad octets. Returns
 * RCP_ACF_ERR_SHORT_FRAME if b is shorter than the fixed header, if the
 * decoded acf_msg_length*4 is shorter than the fixed header, if b is
 * shorter than that quadlet-derived total length, or if pad exceeds the
 * payload region it is declared to trail. Returns
 * RCP_ACF_ERR_BAD_MSG_TYPE if the decoded acf_msg_type is not
 * RCP_ACF_MSG_TYPE_ABB. */
rcp_acf_errc_t rcp_acf_decode_abb(const uint8_t *b, size_t len,
                                  rcp_acf_byte_message_info_t *out_hdr,
                                  const uint8_t **out_payload, size_t *out_payload_len);

/* ── ACF_GBB ───────────────────────────────────────────────────────────────── */

typedef struct {
    rcp_acf_byte_message_info_t info;
    /* Presentation-time-like timestamp this message is associated with;
     * meaningful only when info.mtv == RCP_ACF_MTV_VALID (see
     * rcp_acf_gbb_is_timed()). */
    uint64_t                    message_timestamp;
} rcp_acf_gbb_header_t;

/* Same conventions as rcp_acf_encode_abb(), against RCP_ACF_GBB_MAX_PAYLOAD.
 * If hdr->info.mtv == RCP_ACF_MTV_UNTIMED, the encoded message_timestamp
 * region is forced to all-zero on the wire regardless of
 * hdr->message_timestamp -- an untimed ACF_GBB message always carries a
 * zeroed timestamp region. (Callers that need to repurpose that region
 * for a nonzero value while mtv=0 -- request_compound.c and its siblings
 * -- build their own raw header with rcp_acf_pack_header() instead of
 * calling this function, precisely to avoid this zeroing rule.) */
rcp_bytes_t rcp_acf_encode_gbb(const rcp_acf_gbb_header_t *hdr,
                                const uint8_t *payload, size_t payload_len);

/* Same conventions as rcp_acf_decode_abb(). Returns
 * RCP_ACF_ERR_BAD_MSG_TYPE if the decoded acf_msg_type is not
 * RCP_ACF_MSG_TYPE_GBB. */
rcp_acf_errc_t rcp_acf_decode_gbb(const uint8_t *b, size_t len,
                                  rcp_acf_gbb_header_t *out_hdr,
                                  const uint8_t **out_payload, size_t *out_payload_len);

/* The timestamp-validity folding rule: true only when info.mtv ==
 * RCP_ACF_MTV_VALID. RCP_ACF_MTV_UNTIMED (mtv=0, zeroed timestamp region)
 * is the only other wire state -- see rcp_acf_mtv_t's doc comment for why
 * there is no longer a distinct "uncertain" state to also fold in here. */
bool rcp_acf_gbb_is_timed(const rcp_acf_gbb_header_t *hdr);

/* ── Message-type dispatch ─────────────────────────────────────────────────── */

/* Reads just the acf_msg_type field (the top 7 bits of octet 0) from a
 * received ACF message, so a caller can decide which of
 * rcp_acf_decode_abb()/_gbb() to invoke without a full decode attempt
 * first. Returns RCP_ACF_ERR_SHORT_FRAME if len == 0. */
rcp_acf_errc_t rcp_acf_peek_msg_type(const uint8_t *b, size_t len, uint8_t *out_msg_type);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ACF_H */
