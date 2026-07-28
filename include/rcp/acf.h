//cfusa:req REQ-ACF-001
//cfusa:req REQ-ACF-002
//cfusa:req REQ-ACF-003
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
 * byte_message_info field layout (this module's own byte-level design --
 * exact bit positions are an original engineering choice by this
 * implementation, not reproduced from the confidential OPEN Alliance TC18
 * Remote Control Protocol Specification v0.5.1_RC; only the field names,
 * their high-level semantics, and the two acf_msg_type values above come
 * from that specification by reference):
 *
 *   Offset  Bytes  Field
 *   0       1      acf_msg_type   (0x0E ABB / 0x0D GBB; implied by which
 *                                   encode/decode function is used, not
 *                                   read from the caller-supplied header)
 *   1-2     2      acf_msg_length (payload length in octets, big-endian;
 *                                   recomputed from payload_len on encode,
 *                                   mirroring avtp.c's *_data_length
 *                                   convention)
 *   3       1      byte_bus_id    (rcp_byte_bus_id_t; same addressing
 *                                   role as in avtp.h)
 *   4       1      pad[7:6] | mtv[5:4] | hs[3] | cs[2] | rsp[1] | err[0]
 *   5       1      op[7:5] | evt[4:1] | ms[0]
 *   6       1      transaction_num
 *   7       1      read_size_or_segment_num
 *
 * ACF_GBB appends an 8-byte big-endian message_timestamp at offset 8,
 * making RCP_ACF_GBB_HEADER_LEN exactly RCP_ACF_ABB_HEADER_LEN + 8 -- the
 * presence/absence of that field is the only structural difference
 * between the two variants.
 *
 * Fields hs, cs, rsp, ms, and pad belong to functionality this milestone
 * deliberately does not implement (conditional requests & sequencers,
 * compound bundles, and quadlet padding respectively) -- they are encoded
 * and decoded unmodified (round-tripped), mirroring the treatment
 * avtp.c's TSCF codec already gives its own mr field. Only mtv, evt, op,
 * and err carry behavior in this milestone (the timestamp-folding rule
 * and the four response-semantics identification rules below).
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

/* Largest payload the encoders below can represent, bounded by the width
 * of acf_msg_length (16 bits). Encoding a larger payload fails. */
#define RCP_ACF_MAX_PAYLOAD ((size_t)65535u)

typedef enum {
    RCP_ACF_OK               = 0,
    RCP_ACF_ERR_SHORT_FRAME   = 1,
    RCP_ACF_ERR_BAD_MSG_TYPE  = 2,
} rcp_acf_errc_t;

/* Human-readable message for an rcp_acf_errc_t value. Never returns NULL. */
const char *rcp_acf_strerror(rcp_acf_errc_t e);

/* ── mtv: message-timestamp validity (ACF_GBB only) ───────────────────────── */

/* mtv states. UNTIMED and UNCERTAIN both fold into "not confidently timed"
 * for scheduling purposes -- see rcp_acf_gbb_is_timed() below. Only VALID
 * is treated as a trustworthy timestamp. */
typedef enum {
    RCP_ACF_MTV_UNTIMED   = 0,
    RCP_ACF_MTV_VALID     = 1,
    RCP_ACF_MTV_UNCERTAIN = 2,
} rcp_acf_mtv_t;

/* ── op: the operation this message's byte_bus_id addresses ───────────────── */

typedef enum {
    RCP_ACF_OP_NONE  = 0, /* no read/write data operation (e.g. a plain Acknowledge) */
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

/* ── byte_message_info: the shared header ─────────────────────────────────── */

typedef struct {
    uint8_t           acf_msg_type;   /* RCP_ACF_MSG_TYPE_ABB/_GBB on decode;
                                         ignored on encode (implied by which
                                         encode_* function is called) */
    uint16_t          acf_msg_length; /* payload length in octets; ignored
                                         on encode, recomputed from
                                         payload_len */
    uint8_t           pad;            /* 0-3; round-tripped unmodified */
    uint8_t           mtv;            /* rcp_acf_mtv_t; ABB always encodes/
                                         decodes this as RCP_ACF_MTV_UNTIMED,
                                         since ABB has no timestamp field to
                                         validate */
    rcp_byte_bus_id_t byte_bus_id;
    uint8_t           evt;            /* 0-15; see rcp_acf_hdr_ack_has_event() */
    uint8_t           hs;             /* 0/1; round-tripped unmodified */
    uint8_t           cs;             /* 0/1; round-tripped unmodified */
    uint8_t           transaction_num;
    uint8_t           op;             /* rcp_acf_op_t */
    uint8_t           rsp;            /* 0/1; round-tripped unmodified */
    uint8_t           err;            /* 0/1 */
    uint8_t           ms;             /* 0/1; round-tripped unmodified */
    uint8_t           read_size_or_segment_num; /* meaning depends on op:
                                         read_size when op == RCP_ACF_OP_READ,
                                         otherwise segment_num (unused by
                                         this milestone's Standard-request-
                                         only scope, but round-tripped) */
} rcp_acf_byte_message_info_t;

/* Classifies a decoded byte_message_info header into one of the four
 * response semantics: err takes priority (any message with err set is an
 * Error response regardless of op), otherwise op selects between Write
 * response, Read response, and -- when op carries no data operation --
 * Acknowledge. Use rcp_acf_hdr_ack_has_event() to further distinguish a
 * plain Acknowledge from one tagged with an asynchronous event via evt. */
rcp_acf_response_kind_t rcp_acf_classify_response(const rcp_acf_byte_message_info_t *hdr);

/* true iff hdr classifies as RCP_ACF_RESP_ACKNOWLEDGE and evt != 0, i.e.
 * this Acknowledge is tagged with an asynchronous event code rather than
 * being a plain acknowledgement of a Standard request. */
bool rcp_acf_hdr_ack_has_event(const rcp_acf_byte_message_info_t *hdr);

/* ── ACF_ABB ───────────────────────────────────────────────────────────────── */

/* Encodes hdr plus a payload of payload_len octets (payload may be NULL
 * iff payload_len == 0) into a freshly heap-allocated ACF_ABB message.
 * hdr's own acf_msg_type and acf_msg_length are ignored on encode.
 * Returns a zeroed rcp_bytes_t (data=NULL) if payload_len exceeds
 * RCP_ACF_MAX_PAYLOAD or on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_acf_encode_abb(const rcp_acf_byte_message_info_t *hdr,
                                const uint8_t *payload, size_t payload_len);

/* Decodes an ACF_ABB message from b[0..len). On RCP_ACF_OK, *out_hdr is
 * populated (with mtv forced to RCP_ACF_MTV_UNTIMED, ABB having no
 * timestamp to validate) and *out_payload / *out_payload_len point into
 * b (borrowed, not copied -- matching avtp.c's decode_* convention).
 * Returns RCP_ACF_ERR_SHORT_FRAME if b is shorter than the fixed header
 * or than header-plus-declared-payload length. Returns
 * RCP_ACF_ERR_BAD_MSG_TYPE if byte 0 is not RCP_ACF_MSG_TYPE_ABB. */
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

/* Same conventions as rcp_acf_encode_abb(). If hdr->info.mtv ==
 * RCP_ACF_MTV_UNTIMED, the encoded message_timestamp region is forced to
 * all-zero on the wire regardless of hdr->message_timestamp -- an
 * untimed ACF_GBB message always carries a zeroed timestamp region. */
rcp_bytes_t rcp_acf_encode_gbb(const rcp_acf_gbb_header_t *hdr,
                                const uint8_t *payload, size_t payload_len);

/* Same conventions as rcp_acf_decode_abb(). Returns
 * RCP_ACF_ERR_BAD_MSG_TYPE if byte 0 is not RCP_ACF_MSG_TYPE_GBB. */
rcp_acf_errc_t rcp_acf_decode_gbb(const uint8_t *b, size_t len,
                                  rcp_acf_gbb_header_t *out_hdr,
                                  const uint8_t **out_payload, size_t *out_payload_len);

/* The timestamp-validity folding rule: true only when info.mtv ==
 * RCP_ACF_MTV_VALID. Both RCP_ACF_MTV_UNTIMED (mtv=0, zeroed timestamp
 * region) and RCP_ACF_MTV_UNCERTAIN ("tu", a timestamp present but not
 * trustworthy) fold into the same "not confidently timed" treatment --
 * callers must not schedule against message_timestamp in either case. */
bool rcp_acf_gbb_is_timed(const rcp_acf_gbb_header_t *hdr);

/* ── Message-type dispatch ─────────────────────────────────────────────────── */

/* Reads just the acf_msg_type byte (offset 0) from a received ACF
 * message, so a caller can decide which of rcp_acf_decode_abb()/_gbb() to
 * invoke without a full decode attempt first. Returns
 * RCP_ACF_ERR_SHORT_FRAME if len == 0. */
rcp_acf_errc_t rcp_acf_peek_msg_type(const uint8_t *b, size_t len, uint8_t *out_msg_type);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ACF_H */
