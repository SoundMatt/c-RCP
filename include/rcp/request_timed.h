/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-TIMED-001
//cfusa:req REQ-TIMED-002
//cfusa:req REQ-TIMED-003
//cfusa:req REQ-TIMED-004
//cfusa:req REQ-TIMED-005
//cfusa:req REQ-TIMED-006
//cfusa:req REQ-TIMED-007
//cfusa:req REQ-TIMED-008
/*
 * request_timed.h -- Timed conditional requests for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 17, "Conditional Requests &
 * Sequencers", milestone 69).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), and the register-map's svr_implemented_options
 * time-sync feature group (regmap.h, milestone 62). Nothing in rcp.h,
 * wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c, regmap.h/regmap.c,
 * or any ep_* endpoint module is touched here -- the same layering
 * discipline every module since milestone 64 has followed.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── A per-request alternative to a TSCF header ───────────────────────────────
 *
 * avtp.h's TSCF header already carries one avtp_timestamp, the "earliest
 * moment" presentation time for every ACF message inside that AVTPDU
 * (avtp.h's own file header). A Timed request (request_type
 * RCP_REQUEST_TYPE_TIMED, 0x0A, no safety-tagged variant exists) instead
 * carries its own presentation_time sub-field directly, via request_compound.h's
 * shared message_timestamp-repurposing convention (see that header's file
 * comment) -- letting a client express the identical "don't execute
 * before this instant" semantics inside a plain NTSCF frame, without
 * needing a TSCF header at all. Like request_triggered.h and request_chained.h, this
 * module reuses that shared wire convention without including or calling
 * into request_compound.h itself.
 *
 * This module's own 7-byte sub-field layout: presentation_time (4 bytes,
 * the same uint32_t width and "gPTP-domain instant" semantics as
 * avtp.h's own avtp_timestamp field -- this module's own deliberate
 * choice to keep the two directly comparable) followed by 3 reserved
 * bytes, always encoded/decoded as zero.
 *
 * ── Admission: PRESENTATION_TIME_TOO_FAR and GPTP_FAIL ──────────────────────
 *
 * rcp_timed_admit() is the pure, directly-testable expression of this
 * milestone's roadmap-required rejection paths: GPTP_FAIL (the server has
 * no locked gPTP time base to evaluate presentation_time against at all)
 * takes priority over PRESENTATION_TIME_TOO_FAR (a presentation_time that
 * *can* be evaluated, but sits further in the future than the caller's
 * own configured admission horizon allows). A presentation_time already
 * in the past is never rejected by either path -- matching avtp.h's own
 * "earliest moment... not a hard deadline" semantics for TSCF's
 * avtp_timestamp, a late server is still conforming if it executes a
 * request whose presentation_time has already passed.
 *
 * ── Feature gating ────────────────────────────────────────────────────────
 *
 * rcp_timed_feature_enabled() reads (but the register map module itself
 * is not modified by this file) regmap.h's already-published
 * RCP_REGMAP_OPT_TIME_SYNC_TSCF/_PRESENTATION bits -- both must be set for
 * a server to accept Timed requests at all, since a Timed request is
 * meaningless without the same underlying time-sync capability TSCF
 * itself depends on.
 */
#ifndef RCP_REQUEST_TIMED_H
#define RCP_REQUEST_TIMED_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── request_type opcode value (no safety-tagged variant) ─────────────────── */

#define RCP_REQUEST_TYPE_TIMED ((uint8_t)0x0Au)

/* ── Errors ─────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_TIMED_OK                 = 0,
    RCP_TIMED_ERR_SHORT_FRAME    = 1,
    RCP_TIMED_ERR_BAD_MSG_TYPE   = 2,
    RCP_TIMED_ERR_NOT_REPURPOSED = 3, /* decoded mtv != RCP_ACF_MTV_UNTIMED */
    RCP_TIMED_ERR_UNKNOWN_TYPE   = 4, /* opcode byte is not RCP_REQUEST_TYPE_TIMED */
} rcp_timed_errc_t;

/* Human-readable message for an rcp_timed_errc_t value. Never returns NULL. */
const char *rcp_timed_strerror(rcp_timed_errc_t e);

/* ── Feature gating ─────────────────────────────────────────────────────────── */

/* True iff both RCP_REGMAP_OPT_TIME_SYNC_TSCF and
 * RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION are set in options -- see the
 * file header. */
bool rcp_timed_feature_enabled(uint32_t options);

/* ── Timed request encode/decode ──────────────────────────────────────────── */

/* Encodes an ACF_GBB-framed timed request addressed to byte_bus_id,
 * packing presentation_time into the repurposed message_timestamp
 * region's first 4 sub-field bytes (the remaining 3 reserved and zeroed)
 * with the leading opcode byte set to RCP_REQUEST_TYPE_TIMED and mtv
 * forced to RCP_ACF_MTV_UNTIMED -- same conventions as
 * rcp_compound_encode_request() (request_compound.h). payload/payload_len is this
 * request's own opaque, endpoint-specific data; payload may be NULL iff
 * payload_len == 0. Returns a zeroed rcp_bytes_t (data=NULL) if
 * payload_len exceeds RCP_ACF_MAX_PAYLOAD or on allocation failure.
 * Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_timed_encode_request(rcp_byte_bus_id_t byte_bus_id, uint32_t presentation_time,
                                      uint8_t transaction_num, const uint8_t *payload,
                                      size_t payload_len);

/* Decodes and validates a timed request from b[0..len). Same failure-mode
 * conventions as rcp_compound_decode_request() (request_compound.h), with
 * RCP_TIMED_ERR_UNKNOWN_TYPE returned whenever the decoded opcode byte is
 * not RCP_REQUEST_TYPE_TIMED. On RCP_TIMED_OK, *out_byte_bus_id,
 * *out_presentation_time, and *out_transaction_num are populated, and
 * *out_payload / *out_payload_len are set to a *borrowed* view into b. */
rcp_timed_errc_t rcp_timed_decode_request(const uint8_t *b, size_t len,
                                           rcp_byte_bus_id_t *out_byte_bus_id,
                                           uint32_t *out_presentation_time,
                                           const uint8_t **out_payload, size_t *out_payload_len,
                                           uint8_t *out_transaction_num);

/* ── Admission ─────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_TIMED_ACCEPT                          = 0,
    RCP_TIMED_REJECT_GPTP_FAIL                = 1,
    RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR = 2,
} rcp_timed_admission_t;

/* True iff presentation_time sits strictly in the future of now (by
 * gPTP-domain wraparound-safe subtraction) by more than max_horizon --
 * i.e. RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR's own pure trigger
 * condition. A presentation_time at or before now is never "too far".
 * Does not consider gPTP lock state -- see rcp_timed_admit(). */
bool rcp_timed_too_far(uint32_t presentation_time, uint32_t now, uint32_t max_horizon);

/* The combined admission decision: RCP_TIMED_REJECT_GPTP_FAIL if
 * !gptp_locked (presentation_time cannot be trusted at all without a
 * locked time base, so this check takes priority over the horizon
 * check), else RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR if
 * rcp_timed_too_far(presentation_time, now, max_horizon), else
 * RCP_TIMED_ACCEPT. */
rcp_timed_admission_t rcp_timed_admit(bool gptp_locked, uint32_t presentation_time, uint32_t now,
                                       uint32_t max_horizon);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_TIMED_H */
