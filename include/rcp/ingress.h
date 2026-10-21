/* SPDX-License-Identifier: MPL-2.0 */
/*
 * ingress.h -- pure, allocation-free stages of the RC Server receive path
 * (issue #618, "one ingress pipeline").
 *
 * Before this module, nothing in the library parsed an AVTPDU on the
 * server side: mock.c's ten rcp_mock_server_dispatch*() entry points each
 * took subtype/stream_id/byte_bus_id/acf_msg_type/tv/timestamp as caller
 * arguments, and each one applied a different subset of the receive
 * rules. This header holds the parts of the receive path that are pure
 * functions of the received bytes plus a few configuration values:
 *
 *   1. rcp_ingress_parse_avtpdu()       -- subtype peek, header decode
 *                                           (version must be 0), raw
 *                                           header_octet1 kept for the
 *                                           safe-command-mode CRC;
 *   2. rcp_ingress_resolve_timing()     -- the once-per-AVTPDU timing
 *                                           decision (§13.3 rules,
 *                                           presentation-time gate), which
 *                                           then applies to every member
 *                                           (TC18-12.9.1.1-003);
 *   3. rcp_ingress_member_iter_*()      -- an unbounded, streaming walk over
 *                                           the ACF members of the payload
 *                                           (no fixed-size offset array);
 *   4. rcp_ingress_request_fixed_fields_valid()
 *                                       -- the TC18-11.2.1-005 fixed
 *                                           request-header fields.
 *
 * The stateful composition (lifecycle filtering, stream gates,
 * reassembly, CRC, admission, response routing) is
 * rcp_mock_server_receive_avtpdu() in mock.h, which calls these stages in
 * a fixed order. An integrator's own I/O loop can call the same
 * functions directly instead of re-implementing them. Nothing here
 * allocates, and nothing here depends on mock.h.
 */
#ifndef RCP_INGRESS_H
#define RCP_INGRESS_H

#include "rcp/acf.h"
#include "rcp/avtp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Parse ─────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_INGRESS_OK              = 0,
    RCP_INGRESS_ERR_SHORT_FRAME = 1, /* shorter than the fixed header, or
                                         the declared data length runs past
                                         the end of the buffer */
    RCP_INGRESS_ERR_BAD_SUBTYPE = 2, /* neither NTSCF nor TSCF */
    RCP_INGRESS_ERR_BAD_VERSION = 3, /* header version field != 0 */
} rcp_ingress_errc_t;

/* Human-readable message for an rcp_ingress_errc_t value. Never returns
 * NULL. */
const char *rcp_ingress_strerror(rcp_ingress_errc_t e);

/* One received AVTPDU, decoded once. payload borrows from the buffer
 * passed to rcp_ingress_parse_avtpdu(). */
typedef struct {
    uint8_t        subtype;        /* RCP_AVTP_SUBTYPE_NTSCF / _TSCF */
    uint8_t        header_octet1;  /* raw octet 1, byte-for-byte as received
                                       -- the value the safe-command-mode
                                       CRC covers (TC18-13.6-004/-005) */
    uint8_t        sv;
    uint8_t        version;        /* always 0 after a successful parse */
    uint8_t        mr;             /* TSCF only; 0 for NTSCF */
    uint8_t        tv;             /* TSCF only; 0 for NTSCF */
    uint8_t        tu;             /* TSCF only; 0 for NTSCF */
    uint8_t        sequence_num;
    uint64_t       stream_id;      /* rcp_stream_id_to_u64() of the header's
                                       stream_id */
    uint32_t       avtp_timestamp; /* 0 for NTSCF */
    bool           tscf_reserved_all_zero; /* rcp_avtp_tscf_reserved_all_zero();
                                               false for NTSCF */
    const uint8_t *payload;
    size_t         payload_len;
} rcp_ingress_avtpdu_t;

/* Parses b[0..len) as one AVTPDU. Peeks the subtype (RCP_INGRESS_ERR_
 * SHORT_FRAME for len == 0, RCP_INGRESS_ERR_BAD_SUBTYPE for anything that
 * is neither NTSCF nor TSCF), then hands the buffer to rcp_avtp_decode_
 * ntscf()/_tscf() and maps their errors one-for-one (short frame or
 * data length past the end -> SHORT_FRAME, version != 0 -> BAD_VERSION).
 * On RCP_INGRESS_OK every field of *out is written; on any error *out is
 * left untouched. */
//cfusa:req REQ-INGRESS-002
rcp_ingress_errc_t rcp_ingress_parse_avtpdu(const uint8_t *b, size_t len,
                                            rcp_ingress_avtpdu_t *out);

/* ── Frame timing ──────────────────────────────────────────────────────────── */

typedef enum {
    RCP_INGRESS_TIMING_IMMEDIATE    = 0, /* admit with no presentation-time
                                             gate */
    RCP_INGRESS_TIMING_PRESENTATION = 1, /* admit with the presentation-time
                                             gate (effective tv = true) */
    RCP_INGRESS_TIMING_DROP         = 2, /* drop the whole AVTPDU silently */
} rcp_ingress_timing_t;

/* The once-per-AVTPDU timing decision. Precedence, first match wins:
 *
 *   1. f->subtype is not TSCF -> IMMEDIATE.
 *   2. TSCF and !time_sync_supported -> §13.3 rule 1 via unsupported_policy:
 *      RCP_AVTP_TSCF_FALLBACK_DROP -> DROP; RCP_AVTP_TSCF_FALLBACK_IGNORE ->
 *      IMMEDIATE (the subtype stays TSCF, only the presentation time is
 *      ignored).
 *   3. TSCF and f->tscf_reserved_all_zero -> TC18-13.3-003 via
 *      reserved_zero_policy: _DROP -> DROP; _AS_NTSCF -> IMMEDIATE with the
 *      effective subtype replaced by NTSCF; _TIME_GATE (the default) ->
 *      continue to rule 4.
 *   4. f->tv == 0 -> IMMEDIATE.
 *   5. Otherwise PRESENTATION. tu = 1 is treated exactly like tu = 0
 *      (REQ-AVTP-023): it is never consulted.
 *
 * *out_effective_subtype is the subtype the frame is timed as (NTSCF after
 * an _AS_NTSCF relabelling); *out_effective_tv is the tv admission must
 * use. Both are written on every return, including DROP (where they
 * carry f->subtype and false). The relabelling is a timing decision only:
 * the state-dependent subtype rules (rcp_lifecycle_should_accept_subtype()
 * / rcp_lifecycle_should_accept(): NTSCF only in HW_UNCONFIGURED, TSCF
 * dropped in HW_CONFIGURED -- TC18-12.3.1.1-002/12.3.1.2-002) must be
 * given the wire subtype f->subtype, or a conformant TSCF AVTPDU would
 * get past them under _AS_NTSCF (REQ-INGRESS-036). The CRC stage also
 * uses the real f->subtype/header_octet1, never the effective ones. */
//cfusa:req REQ-INGRESS-003
//cfusa:req REQ-INGRESS-004
rcp_ingress_timing_t rcp_ingress_resolve_timing(const rcp_ingress_avtpdu_t *f,
                                                bool time_sync_supported,
                                                rcp_avtp_tscf_fallback_t unsupported_policy,
                                                rcp_avtp_tscf_reserved_zero_policy_t
                                                    reserved_zero_policy,
                                                uint8_t *out_effective_subtype,
                                                bool *out_effective_tv);

/* ── Member iterator ───────────────────────────────────────────────────────── */

/* Streaming walk over the ACF members packed into one AVTPDU payload
 * (TC18-12.9.1.1-002). Each member's extent comes from its own
 * acf_msg_length (quadlets), so there is no limit on how many members a
 * frame may carry and no offset table to size. Plain struct: initialize
 * with rcp_ingress_member_iter_init(), then call rcp_ingress_member_next()
 * until it returns false. */
typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         offset;    /* start of the next member */
    size_t         index;     /* members returned so far */
    bool           malformed; /* set when the remaining bytes do not form a
                                  whole ACF_ABB/ACF_GBB member */
} rcp_ingress_member_iter_t;

/* Starts a walk over payload[0..payload_len). payload may be NULL iff
 * payload_len == 0. */
//cfusa:req REQ-INGRESS-008
void rcp_ingress_member_iter_init(rcp_ingress_member_iter_t *it, const uint8_t *payload,
                                  size_t payload_len);

/* Returns the next member as a borrowed slice, advancing the iterator.
 * Returns false when the payload is exhausted (malformed stays false) or
 * when the remaining bytes are not a whole member -- fewer than 2 octets,
 * an acf_msg_type that is neither ABB nor GBB, fewer octets than that
 * type's header, a declared acf_msg_length shorter than the header, or a
 * declared length past the end of the payload. In that second case
 * it->malformed is set and every later call also returns false; members
 * already returned stay valid. */
//cfusa:req REQ-INGRESS-008
bool rcp_ingress_member_next(rcp_ingress_member_iter_t *it, const uint8_t **out_member,
                             size_t *out_member_len);

/* ── Table 4 fixed request fields ──────────────────────────────────────────── */

/* True iff the request header in member[0..8) carries the fixed values
 * TC18-11.2.1-005 requires of a request: rcp_acf_request_header_
 * constraints_valid() (hs = 0, rsp = 0, err = 0, cs = 0 unless
 * cs_has_meaning), rcp_acf_header_reserved_bits_zero() (both rsv fields
 * 0), and -- for ACF_ABB only -- mtv = 0. ACF_GBB's mtv is a real field
 * and is not checked. cs_has_meaning is supplied by the caller (see
 * rcp_ingress_cs_has_meaning()); its exact per-request-type semantics are
 * tracked separately (#625). member must point at least 8 readable
 * octets. */
//cfusa:req REQ-INGRESS-010
bool rcp_ingress_request_fixed_fields_valid(const uint8_t member[8], bool cs_has_meaning);

/* Whether a request's cs bit carries meaning, i.e. whether
 * rcp_ingress_request_fixed_fields_valid() must not insist on cs = 0:
 * true iff member[0..member_len) is a repurposed ACF_GBB
 * (rcp_compound_peek_request_type() succeeds) whose request_type is a
 * compound (rcp_request_type_is_compound()) or chained request -- the two
 * request kinds this library encodes with a meaningful cs bit. */
//cfusa:req REQ-INGRESS-010
bool rcp_ingress_cs_has_meaning(const uint8_t *member, size_t member_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_INGRESS_H */
