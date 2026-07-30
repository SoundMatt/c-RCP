/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-FRAG-001
//cfusa:req REQ-FRAG-002
//cfusa:req REQ-FRAG-003
//cfusa:req REQ-FRAG-004
//cfusa:req REQ-FRAG-005
//cfusa:req REQ-FRAG-006
//cfusa:req REQ-FRAG-007
//cfusa:req REQ-FRAG-008
//cfusa:req REQ-FRAG-009
//cfusa:req REQ-FRAG-010
//cfusa:req REQ-FRAG-011
//cfusa:req REQ-FRAG-012
//cfusa:req REQ-FRAG-013
//cfusa:req REQ-FRAG-014
//cfusa:req REQ-FRAG-015
//cfusa:req REQ-FRAG-016
//cfusa:req REQ-FRAG-017
//cfusa:req REQ-FRAG-018
/*
 * fragment.h -- Multi-AVTPDU message fragmentation and reassembly for the
 * TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 20,
 * "Fragmentation", milestone 76).
 *
 * This is new, additive protocol-core surface layered on top of the ACF
 * message format (acf.h/acf.c, milestone 60): acf.h's own
 * byte_message_info header already reserves the wire slot this module
 * interprets (the dual-purpose read_size_or_segment_num field, and the ms
 * bit) but, per acf.h's own file header, has deliberately left that
 * interpretation unimplemented until this milestone. Nothing in rcp.h,
 * wire.c, avtp.h/avtp.c, acf.h/acf.c, lifecycle.h/lifecycle.c,
 * regmap.h/regmap.c, e2e.h/e2e.c, discovery.h/discovery.c, or any ep_*
 * endpoint module is touched here -- this module operates purely on
 * caller-supplied byte buffers and already-decoded ms/segment_num values,
 * the same "own small pure helpers, operate on caller-owned data, don't
 * reach into sibling modules" layering discipline e2e.h and scheduler.h
 * already established. A caller (an endpoint module's own encode/decode
 * path) is responsible for actually invoking acf.c's
 * rcp_acf_encode_abb()/_encode_gbb() (or their decode counterparts) once
 * per fragment, using the segment plan or reassembly state this module
 * produces.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── Why a dedicated module, not per-endpoint duplication ────────────────────
 *
 * Three already-shipped features -- CAN XL's up to 2054-byte captured
 * frames (ep_can.h, milestone 72), UART's read_size/uart_timeout short
 * reads (ep_uart.h, milestone 66), and discovery's general-register-slice
 * reads (discovery.h, milestone 63) -- each explicitly deferred true
 * multi-AVTPDU support to this milestone rather than inventing their own
 * ad hoc size-capping workaround. Since every one of them shares the same
 * underlying wire mechanism (acf.h's ms bit / read_size_or_segment_num
 * field), this module implements that mechanism exactly once, generically,
 * for any caller assembling or reassembling an ACF_ABB/ACF_GBB payload --
 * not duplicated three times against three different endpoint shapes.
 *
 * ── Wire semantics this module encodes/decodes against ──────────────────────
 *
 * A single logical ACF payload -- what an endpoint module would otherwise
 * have handed straight to rcp_acf_encode_abb()/_encode_gbb() as one
 * contiguous buffer -- is instead split into an ordered sequence of one or
 * more ACF messages ("fragments"), each sharing the same acf_msg_type,
 * byte_bus_id, op, and transaction_num as the logical message they jointly
 * carry. Every fragment but the last sets ms=1 and carries, in
 * read_size_or_segment_num, this fragment's own zero-based index within
 * the sequence (segment_num) -- see rcp_fragment_plan() below. The final
 * fragment sets ms=0; per acf.h's own field comment, that reverts
 * read_size_or_segment_num to its ordinary non-fragmentation meaning (e.g.
 * read_size for a read-classified message), so this module never writes a
 * segment number into that field for the final fragment -- a caller fills
 * it in with whatever value that field ordinarily carries for the message
 * kind involved. A message that never needed fragmenting in the first
 * place is simply a one-fragment sequence: ms=0 on its only (and
 * therefore also final) fragment, indistinguishable on the wire from
 * every prior milestone's own single-frame traffic -- this module's
 * mechanism is a strict superset of "no fragmentation", not a parallel
 * wire format.
 *
 * Because segment_num is one octet wide, at most
 * RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS (256) ms=1 fragments can precede
 * the one final ms=0 fragment -- see that macro's own comment for the
 * arithmetic. rcp_fragment_plan_count() reports 0 (a value no valid plan
 * ever produces) when a payload cannot be represented within that bound,
 * or when fragmentation is disabled outright (max_fragment_payload == 0,
 * matching regmap.h's rx_stream_max_request_size convention: "0 =
 * fragmentation unsupported for that stream").
 *
 * ── Safe-point CRC interaction: this module defers to e2e.h ─────────────────
 *
 * e2e.h's rcp_e2e_fragment_carries_crc() already models, since milestone
 * 70, the rule that only a multi-segment message's final fragment may
 * carry an E2E safe-point CRC (computed across the fully reassembled
 * payload, per e2e.h's own file header) -- this module does not
 * re-implement or call that function itself (the same "don't duplicate a
 * sibling module's own small pure helper" precedent e2e.h's own
 * rcp_e2e_is_safety_request() sets against request_compound.c's
 * near-identical rcp_request_type_is_safety()). A caller wraps only the
 * final fragment's encoded bytes with rcp_e2e_wrap() (and only that
 * fragment's decoded bytes with rcp_e2e_unwrap()), driven by
 * rcp_e2e_fragment_carries_crc(!segment.ms) against the segment this
 * module's own rcp_fragment_plan() (encode side) or
 * rcp_fragment_reassembler_feed() (decode side) is currently handling.
 *
 * ── Reassembly: a small caller-owned accumulator, not global state ──────────
 *
 * rcp_fragment_reassembler_t mirrors e2e.h's own
 * rcp_e2e_stream_fault_t -- a small, caller-owned, explicitly-initialized
 * piece of mutable state living alongside this module's otherwise-pure
 * functions, one instance per request stream a caller is reassembling
 * fragments for (matching regmap.h's own per-stream
 * rcp_regmap_request_stream_cfg_t granularity). It bounds the reassembled
 * payload to a caller-supplied max_total_len (typically
 * rx_stream_max_request_size itself, or another caller-chosen ceiling),
 * failing closed with RCP_FRAGMENT_REASM_ERR_TOO_LARGE rather than
 * growing without bound -- the same fail-toward-blocking-not-permitting
 * engineering choice e2e.h's own rcp_e2e_endpoint_in_safe_state() already
 * documents for its own unrecognized-configuration case.
 *
 * Segment ordering is enforced strictly: the first ms=1 fragment fed to a
 * freshly-initialized-or-reset reassembler must carry segment_num == 0,
 * and every subsequent ms=1 fragment must carry exactly one more than the
 * previous fragment's segment_num, or RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER
 * is returned and the fragment is not appended. This is a deliberate,
 * original engineering choice by this implementation (a strictly
 * monotonic, zero-based counter is the simplest contract a sender and
 * receiver can agree on without further negotiation) matching
 * rcp_fragment_plan()'s own encode-side numbering -- not a value taken
 * from the specification.
 */
#ifndef RCP_FRAGMENT_H
#define RCP_FRAGMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Planning (encode side) ────────────────────────────────────────────────── */

typedef enum {
    RCP_FRAGMENT_OK                     = 0,
    RCP_FRAGMENT_ERR_DISABLED           = 1, /* max_fragment_payload == 0 and
                                                  the payload does not fit in
                                                  a single fragment */
    RCP_FRAGMENT_ERR_TOO_MANY_SEGMENTS  = 2, /* the split would need more
                                                  intermediate segments than
                                                  segment_num's one-octet
                                                  width can address */
    RCP_FRAGMENT_ERR_BAD_SEGMENT_COUNT  = 3, /* segment_count passed to
                                                  rcp_fragment_plan() does not
                                                  match rcp_fragment_plan_count()'s
                                                  answer for the same
                                                  payload_len/max_fragment_payload */
} rcp_fragment_errc_t;

/* Human-readable message for an rcp_fragment_errc_t value. Never returns
 * NULL. */
const char *rcp_fragment_strerror(rcp_fragment_errc_t e);

/* The largest number of ms=1 (intermediate) fragments a single
 * reassembled message can be split into: segment_num is one octet wide
 * (acf.h's read_size_or_segment_num), giving 256 distinct values (0..255)
 * -- every one of them usable by an intermediate fragment, since (unlike
 * the final fragment) an intermediate fragment's segment_num is always
 * this module's own sequence index, never anything else. A representable
 * message therefore spans at most this many intermediate fragments plus
 * exactly one final (ms=0) fragment. */
#define RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS ((size_t)256u)

/* One planned fragment: which slice [offset, offset+len) of the original
 * payload it carries, whether it is an intermediate (ms=true) or the
 * final (ms=false) fragment, and -- meaningful only when ms is true --
 * this fragment's own segment_num. See the file header for why a final
 * fragment's segment_num is left at 0 here rather than populated: that
 * wire slot means something else once ms=0, and it is the caller's job to
 * fill it in appropriately for the message kind involved. */
typedef struct {
    size_t  offset;
    size_t  len;
    bool    ms;
    uint8_t segment_num;
} rcp_fragment_segment_t;

/* The number of fragments rcp_fragment_plan() would produce for payload_len
 * octets split into fragments of at most max_fragment_payload octets each
 * (every fragment but the last carries exactly max_fragment_payload
 * octets). A payload_len of 0 always plans to exactly one (empty, ms=false)
 * fragment, regardless of max_fragment_payload -- an empty payload never
 * needs to fragment, so fragmentation being disabled
 * (max_fragment_payload == 0) is not itself an error in that case. A
 * payload_len that already fits within max_fragment_payload likewise
 * always plans to exactly one (ms=false) fragment -- this function (and
 * rcp_fragment_plan()) is the one thing a caller needs to consult to
 * decide whether fragmentation is actually necessary at all, rather than
 * dead-reckoning that decision itself. Returns 0 -- a value no valid plan
 * ever produces -- if max_fragment_payload == 0 and payload_len exceeds
 * it (RCP_FRAGMENT_ERR_DISABLED, see rcp_fragment_plan()), or if the
 * resulting split would need more than
 * RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS intermediate fragments to
 * represent (RCP_FRAGMENT_ERR_TOO_MANY_SEGMENTS). */
size_t rcp_fragment_plan_count(size_t payload_len, size_t max_fragment_payload);

/* Fills out_segments[0..segment_count) with this module's own greedy,
 * fixed-size splitting plan for payload_len octets split into fragments of
 * at most max_fragment_payload octets each -- see rcp_fragment_plan_count()
 * and the file header for the full numbering rule. segment_count must
 * equal rcp_fragment_plan_count(payload_len, max_fragment_payload) exactly
 * (RCP_FRAGMENT_ERR_BAD_SEGMENT_COUNT otherwise, out_segments left
 * untouched); out_segments is caller-allocated, sized by calling
 * rcp_fragment_plan_count() first. Returns RCP_FRAGMENT_ERR_DISABLED or
 * RCP_FRAGMENT_ERR_TOO_MANY_SEGMENTS under the same conditions
 * rcp_fragment_plan_count() returns 0 for (checked before the segment_count
 * match, so either can be diagnosed from the same call). On RCP_FRAGMENT_OK,
 * a caller assembles fragment i's own ACF payload as the original
 * payload's [out_segments[i].offset, out_segments[i].offset +
 * out_segments[i].len) slice and encodes it (via acf.c) with ms =
 * out_segments[i].ms and, iff out_segments[i].ms, read_size_or_segment_num
 * = out_segments[i].segment_num. */
rcp_fragment_errc_t rcp_fragment_plan(size_t payload_len, size_t max_fragment_payload,
                                       rcp_fragment_segment_t *out_segments,
                                       size_t segment_count);

/* ── Reassembly (decode side) ─────────────────────────────────────────────── */

typedef enum {
    RCP_FRAGMENT_REASM_CONTINUE         = 0, /* fragment accepted; more expected */
    RCP_FRAGMENT_REASM_COMPLETE         = 1, /* fragment accepted; reassembly
                                                 finished -- call
                                                 rcp_fragment_reassembler_get() */
    RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER = 2, /* an ms=true fragment's
                                                 segment_num was not the
                                                 expected next value; the
                                                 fragment was not appended */
    RCP_FRAGMENT_REASM_ERR_TOO_LARGE    = 3, /* appending this fragment would
                                                 exceed max_total_len; the
                                                 fragment was not appended */
    RCP_FRAGMENT_REASM_ERR_ALLOC        = 4, /* internal buffer growth failed
                                                 (allocation failure); the
                                                 fragment was not appended */
} rcp_fragment_reasm_result_t;

/* Human-readable message for an rcp_fragment_reasm_result_t value,
 * including the two non-error outcomes. Never returns NULL. */
const char *rcp_fragment_reasm_result_string(rcp_fragment_reasm_result_t r);

/* Caller-owned reassembly accumulator for one request stream's worth of
 * in-flight fragmented messages -- see the file header. Zero-initialized
 * by rcp_fragment_reassembler_init(); every field is private to this
 * module's own implementation and must not be read or written directly by
 * a caller. */
typedef struct {
    bool     collecting;
    uint8_t  expected_segment_num;
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    size_t   max_total_len;
} rcp_fragment_reassembler_t;

/* Initializes r as empty/not-collecting, bounding the eventual reassembled
 * payload to max_total_len octets -- see the file header. */
void rcp_fragment_reassembler_init(rcp_fragment_reassembler_t *r, size_t max_total_len);

/* Discards any in-progress reassembly and returns r to the same
 * freshly-initialized state rcp_fragment_reassembler_init() would (same
 * max_total_len it already had) -- safe to call between logical messages
 * to reuse one rcp_fragment_reassembler_t for a whole stream's lifetime,
 * and safe to call at any point (mid-reassembly or not) to abandon
 * whatever has been collected so far. */
void rcp_fragment_reassembler_reset(rcp_fragment_reassembler_t *r);

/* Frees r's internal storage entirely and zeroes it; r must not be used
 * again without a fresh rcp_fragment_reassembler_init() call. */
void rcp_fragment_reassembler_destroy(rcp_fragment_reassembler_t *r);

/* Feeds one already-decoded ACF fragment's ms bit and payload into r.
 * segment_num is read_size_or_segment_num's raw wire value; it is
 * consulted only when ms is true (see the file header's wire-semantics
 * section) -- pass whatever value the frame actually carried when ms is
 * false, it is ignored. payload may be NULL iff payload_len == 0.
 *
 * A message that was never fragmented in the first place is fed as a
 * single ms=false fragment to a freshly-initialized-or-reset r: this
 * yields RCP_FRAGMENT_REASM_COMPLETE immediately, with payload as the
 * whole reassembled result, without ever entering the collecting state.
 * Otherwise, the first fragment fed to a freshly-initialized-or-reset r
 * must be an ms=true fragment carrying segment_num == 0 (any other
 * segment_num yields RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER, r left
 * untouched); every subsequent ms=true fragment must carry exactly one
 * more than the previous fragment's segment_num (same error otherwise);
 * a final ms=false fragment completes the sequence regardless of its own
 * segment_num field value, per the file header. RCP_FRAGMENT_REASM_ERR_TOO_LARGE
 * is returned, and r is left in its pre-call state (this fragment is not
 * appended), if accepting payload_len more octets would exceed
 * max_total_len; RCP_FRAGMENT_REASM_ERR_ALLOC is returned, r likewise
 * left untouched, if this module's internal buffer growth fails. */
rcp_fragment_reasm_result_t rcp_fragment_reassembler_feed(rcp_fragment_reassembler_t *r,
                                                            bool ms, uint8_t segment_num,
                                                            const uint8_t *payload,
                                                            size_t payload_len);

/* True iff r currently has a fragment sequence in progress (has accepted
 * at least one ms=true fragment since the last completed reassembly or
 * reset). A pure query: does not mutate r. */
bool rcp_fragment_reassembler_is_collecting(const rcp_fragment_reassembler_t *r);

/* Valid only immediately after rcp_fragment_reassembler_feed() has
 * returned RCP_FRAGMENT_REASM_COMPLETE on this r (undefined otherwise).
 * *out_payload is a pointer *owned by r* -- valid until the next
 * _feed()/_reset()/_destroy() call on the same r; it is not transferred to
 * the caller, unlike acf.c's/every ep_*.c decode function's own
 * *borrowed-into-the-original-frame-buffer* convention, since this payload
 * was assembled by r itself out of possibly-several original frame
 * buffers and has no single one of them to borrow from. *out_payload may
 * be NULL iff *out_payload_len == 0 (an empty reassembled message). */
void rcp_fragment_reassembler_get(const rcp_fragment_reassembler_t *r,
                                   const uint8_t **out_payload, size_t *out_payload_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_FRAGMENT_H */
