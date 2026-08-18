/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-DISC-001
//cfusa:req REQ-DISC-002
//cfusa:req REQ-DISC-003
//cfusa:req REQ-DISC-004
//cfusa:req REQ-DISC-005
//cfusa:req REQ-DISC-006
//cfusa:req REQ-DISC-007
//cfusa:req REQ-DISC-008
//cfusa:req REQ-DISC-009
//cfusa:req REQ-DISC-010
//cfusa:req REQ-DISC-011
//cfusa:req REQ-DISC-012
//cfusa:req REQ-DISC-013
//cfusa:req REQ-DISC-014
//cfusa:req REQ-DISC-015
//cfusa:req REQ-DISC-016
//cfusa:req REQ-DISC-017
//cfusa:req REQ-DISC-018
//cfusa:req REQ-DISC-019
//cfusa:req REQ-DISC-020
//cfusa:req REQ-DISC-021
//cfusa:req REQ-DISC-022
//cfusa:req REQ-DISC-023
//cfusa:req REQ-DISC-024

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-DISC-029
/*
 * discovery.h -- Discovery for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 15, "Discovery", milestone 63).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), and the register-map model (regmap.h/regmap.c,
 * milestone 62). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c,
 * server.h/server.c, regmap.h/regmap.c, or any satellite package is touched
 * here.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── The discovery request/response exchange ────────────────────────────────
 *
 * Discovery is a single ACF_ABB (milestone 60) read request addressed to
 * RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID (server.h, milestone 61) -- the same
 * numeric address as RCP_REGMAP_EP0_INDEX (regmap.h, milestone 62) -- and
 * is the one request an RC Server answers regardless of its current
 * rcp_lifecycle_state_t. rcp_discovery_encode_request()/_decode_request()
 * build and parse that request; rcp_discovery_encode_response()/
 * _decode_response() build and parse the reply, a register-map slice
 * starting at address 0 of the requested read_size, populated from
 * rcp_regmap_general_t's own generic-recognition fields (magic,
 * svr_version, vendor_id, device_id, svr_ep_count) -- see
 * RCP_DISCOVERY_GENERAL_SLICE_LEN below for exactly how many of those
 * octets this milestone actually defines. Anything vendor/device-specific
 * beyond that generic slice is explicitly out of this exchange's scope
 * (e.g. carried in a datasheet, not on the wire here).
 *
 * ── NTSCF-only ───────────────────────────────────────────────────────────────
 *
 * A discovery request or response never rides on a TSCF-headed frame, in
 * any lifecycle state, independent of an RC Node's own time-sync
 * capability. This is a dedicated, directly-testable rule
 * (rcp_discovery_should_drop(), mirroring avtp.c's own
 * rcp_avtp_should_drop_tscf() convention) layered on top of -- not a
 * replacement for -- avtp.h's general time-sync-gated TSCF drop rule and
 * server.h's per-state rcp_lifecycle_should_accept() filtering.
 *
 * ── Discovery-stream claiming ───────────────────────────────────────────────
 *
 * rcp_discovery_claim_t is this module's own small piece of server-side
 * mutable state for the claim/timeout/re-open behavior described by the
 * roadmap: the first discovery request an RC Server receives while
 * HW_UNCONFIGURED or HW_CONFIGURED (rcp_discovery_claim_note_request())
 * reserves the discovery stream as the sole client authorized to make
 * configuration writes, until Discovery_TimeOut elapses with no follow-up
 * configuration write (rcp_discovery_claim_note_config_write() extends the
 * reservation on each one). The timeout is a configurable field of this
 * struct (mirroring regmap.h's rcp_regmap_request_stream_cfg_t's own
 * rx_wd_timeout_ms convention), defaulted by callers to
 * RCP_DISCOVERY_DEFAULT_TIMEOUT_MS, not hardcoded by this module. A lapsed
 * or never-held claim is "open" and may be granted to a new requester;
 * ordinary read-only discovery from any client is unaffected by claim
 * state (it is answered per the paragraph above regardless), only a
 * configuration *write*'s authorization consults the claim. This module
 * deliberately does not itself decide whether a given register field is
 * writable in the server's current lifecycle state -- that remains
 * rcp_lifecycle_field_writable()'s and rcp_regmap_writer_ctx()'s job; a
 * caller combines rcp_discovery_claim_is_claimant()'s answer with those as
 * one more input to an overall write-authorization decision, the same way
 * rcp_regmap_writer_ctx() already combines root-client and owning-stream
 * facts.
 *
 * Every function below that reasons about elapsed time takes an explicit
 * now_ms parameter (clock.h's rcp_monotonic_ms(), read by the caller) --
 * matching this codebase's standing convention (see avtp.h/acf.h/server.h)
 * of functions here consuming already-classified/already-read inputs
 * rather than reaching for global or hidden state themselves. This also
 * makes the claim/timeout state machine fully deterministic to test.
 *
 * ── Client-side discovery result persistence ────────────────────────────────
 *
 * rcp_discovery_cache_t is an explicitly thin convenience API, not a
 * protocol requirement: nothing in this module consults it to decide
 * whether to (re-)issue a discovery request, and a client that never
 * touches it remains fully conformant. It exists purely so a client on a
 * known, stable topology is not forced to rediscover every power cycle,
 * should it choose to persist (or simply cache in-process) a prior
 * rcp_discovery_result_t.
 */
#ifndef RCP_DISCOVERY_H
#define RCP_DISCOVERY_H

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

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_DISCOVERY_OK              = 0,
    RCP_DISCOVERY_ERR_SHORT_FRAME = 1,
    RCP_DISCOVERY_ERR_NOT_NTSCF   = 2, /* TSCF-headed (or unrecognized-subtype)
                                          frame; the NTSCF-only rule dropped it */
    RCP_DISCOVERY_ERR_BAD_MSG_TYPE = 3, /* not an ACF_ABB message */
    RCP_DISCOVERY_ERR_WRONG_BUS    = 4, /* byte_bus_id != RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID */
    RCP_DISCOVERY_ERR_WRONG_OP     = 5, /* acf op is not a read */
} rcp_discovery_errc_t;

/* Human-readable message for an rcp_discovery_errc_t value. Never returns NULL. */
const char *rcp_discovery_strerror(rcp_discovery_errc_t e);

/* ── NTSCF-only rule ────────────────────────────────────────────────────────── */

/* True iff avtp_subtype is anything other than RCP_AVTP_SUBTYPE_NTSCF --
 * in particular, true for RCP_AVTP_SUBTYPE_TSCF regardless of lifecycle
 * state or time-sync capability. See the file header. */
bool rcp_discovery_should_drop(uint8_t avtp_subtype);

/* ── Discovery request ──────────────────────────────────────────────────────── */

/* A decoded discovery request: who asked (the NTSCF header's own
 * stream_id, since NTSCF's stream_id is always sender-assigned), how many
 * octets of the general register slice they want back, and the
 * transaction number to echo in the response. */
typedef struct {
    rcp_stream_id_t requester;
    uint8_t         read_size;
    uint8_t         transaction_num;
} rcp_discovery_request_t;

/* Encodes a full NTSCF-framed ACF_ABB discovery read request: byte_bus_id
 * RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, op READ, an empty payload,
 * read_size_or_segment_num set to read_size. requester_stream_id becomes
 * the NTSCF header's own stream_id (the requesting client's identity, per
 * NTSCF's sender-assigns-stream_id convention -- see avtp.h). Returns a
 * zeroed rcp_bytes_t (data=NULL) on allocation failure. Caller frees the
 * result with rcp_bytes_free(). */
rcp_bytes_t rcp_discovery_encode_request(rcp_stream_id_t requester_stream_id,
                                          uint8_t read_size,
                                          uint8_t transaction_num);

/* Decodes and validates a discovery request from a full AVTPDU frame
 * b[0..len). On RCP_DISCOVERY_OK, *out_req is populated. Failure modes:
 *
 *   - RCP_DISCOVERY_ERR_SHORT_FRAME: b is shorter than either the AVTP or
 *     ACF fixed header, or than their declared payload lengths.
 *   - RCP_DISCOVERY_ERR_NOT_NTSCF: the frame is TSCF-headed (or an
 *     unrecognized subtype) -- see rcp_discovery_should_drop(). Checked
 *     before any ACF-level parsing is attempted.
 *   - RCP_DISCOVERY_ERR_BAD_MSG_TYPE: the NTSCF payload is not an ACF_ABB
 *     message.
 *   - RCP_DISCOVERY_ERR_WRONG_BUS: the ACF header's byte_bus_id is not
 *     RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID.
 *   - RCP_DISCOVERY_ERR_WRONG_OP: the ACF header's op is not
 *     RCP_ACF_OP_READ.
 */
rcp_discovery_errc_t rcp_discovery_decode_request(const uint8_t *b, size_t len,
                                                   rcp_discovery_request_t *out_req);

/* ── Discovery response ─────────────────────────────────────────────────────── */

/* The number of leading octets of the general register slice this
 * milestone actually populates: magic (4) + svr_version (4) + vendor_id
 * (2) + device_id (2) + svr_ep_count (2), each big-endian, in that order
 * -- the field widths and their order are the specification's own for the
 * leading, device-recognition part of the RC Server general register map
 * (extraction §3.5); only the field set rcp_regmap_general_t already
 * models is drawn from it, by reference. svr_version is FOUR octets wide,
 * not two: a two-octet svr_version shifts vendor_id, device_id and
 * svr_ep_count each two octets earlier than a conforming peer reads them,
 * so every one of those three fields is misparsed. A response's payload
 * length always equals the request's read_size exactly (per the roadmap's
 * own wording): the leading min(read_size,
 * RCP_DISCOVERY_GENERAL_SLICE_LEN) octets carry this slice, and any
 * remaining octets (a read_size greater than this constant) are
 * zero-filled reserved space for fields a future milestone may define. */
#define RCP_DISCOVERY_GENERAL_SLICE_LEN ((size_t)14u)

/* Encodes the discovery response: a full NTSCF-framed ACF_ABB message
 * whose payload is exactly read_size octets long (see
 * RCP_DISCOVERY_GENERAL_SLICE_LEN above), echoing transaction_num from
 * the originating request. server_stream_id becomes the NTSCF header's
 * own stream_id -- the responding RC Server's identity. Addressing the
 * underlying carrier frame back to the *requester's* MAC (e.g. as an
 * Ethernet destination address) is a transport-level concern this module
 * does not model, matching avtp.h's transport-independent design: a
 * caller wiring this to a real carrier uses the requester field this
 * module's own rcp_discovery_request_t already carries (its stream_id.mac)
 * as that destination. Returns a zeroed rcp_bytes_t (data=NULL) on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_discovery_encode_response(const rcp_regmap_general_t *map,
                                           uint8_t read_size,
                                           uint8_t transaction_num,
                                           rcp_stream_id_t server_stream_id);

/* A decoded, client-side discovery result: the responding server's own
 * identity (from the response frame's NTSCF stream_id) plus the generic
 * compatible-device-recognition fields carried in the response's leading
 * RCP_DISCOVERY_GENERAL_SLICE_LEN octets. valid is always true when this
 * struct is populated by rcp_discovery_decode_response() on
 * RCP_DISCOVERY_OK; it exists so rcp_discovery_cache_find() can hand back
 * a pointer to a "no result" cache slot without that ever being confused
 * with a genuine all-zero discovery response. */
typedef struct {
    bool             valid;
    rcp_stream_id_t  server_stream_id;
    uint32_t         magic;
    uint32_t         svr_version; /* 32 bit on the wire -- see
                                      RCP_DISCOVERY_GENERAL_SLICE_LEN */
    uint16_t         vendor_id;
    uint16_t         device_id;
    uint16_t         svr_ep_count;
} rcp_discovery_result_t;

/* Decodes a discovery response frame. On RCP_DISCOVERY_OK, *out_result is
 * populated with valid = true. Failure modes mirror
 * rcp_discovery_decode_request()'s AVTP/ACF-level ones
 * (RCP_DISCOVERY_ERR_SHORT_FRAME, _ERR_NOT_NTSCF, _ERR_BAD_MSG_TYPE); a
 * response payload shorter than RCP_DISCOVERY_GENERAL_SLICE_LEN also
 * yields RCP_DISCOVERY_ERR_SHORT_FRAME, since a genuinely useful result
 * cannot be extracted from it. RCP_DISCOVERY_ERR_WRONG_BUS/_WRONG_OP are
 * checked the same way as for a request, since a response is expected to
 * echo the same byte_bus_id and a read-classified op. */
rcp_discovery_errc_t rcp_discovery_decode_response(const uint8_t *b, size_t len,
                                                    rcp_discovery_result_t *out_result);

/* ── Fragmented response (Phase 20, fragment.h) ────────────────────────────── */

/* read_size is one octet wide, so a discovery response's payload (always
 * exactly read_size octets, per RCP_DISCOVERY_GENERAL_SLICE_LEN's own
 * comment) is always well under any plausible max_fragment_payload --
 * this endpoint's traffic, like ep_uart.h's read responses, never
 * actually needs fragment.h's ms/segment_num mechanism (Phase 20,
 * ROADMAP.md milestone 76) in real-world use. The functions below are
 * nonetheless provided for API consistency across every Phase 20 target
 * endpoint and are exercised end-to-end in this module's own test suite
 * against a deliberately small max_fragment_payload, closing the deferred
 * single-AVTPDU-worst-case test milestone 63 left open. See
 * rcp_ep_uart_read_response_fragment_count()'s own comment for the same
 * reasoning applied there.
 *
 * Issue #521 (ASIL-D-oriented no-dynamic-allocation push), round 4: unlike
 * ep_uart.h's rx_len (a plain size_t with no small compile-time ceiling --
 * REQ-UART-034's own fix means a genuine UART read response can carry up
 * to 4095 octets, per that header's file comment -- so ep_uart.c's
 * fragment-plan array stays heap-allocated, the same "no small ceiling"
 * reasoning ep_iseled.h's own file comment gives), read_size HERE is
 * actually typed uint8_t, not size_t -- a hard compile-time guarantee that
 * the payload_len rcp_discovery_encode_response_fragmented() ever plans
 * against cannot exceed 255, regardless of max_fragment_payload. That
 * previous paragraph's "like ep_uart.h's read responses" comparison is
 * about real-world traffic patterns, not about the two functions' actual
 * worst-case bounds, which differ: this module's is a genuine, provable
 * small constant (RCP_DISCOVERY_MAX_FRAGMENT_SEGMENTS below), ep_uart's is
 * not. See RCP_DISCOVERY_MAX_FRAGMENT_SEGMENTS's own comment. */
size_t rcp_discovery_response_fragment_count(uint8_t read_size, size_t max_fragment_payload);

/* Issue #521 (ASIL-D-oriented no-dynamic-allocation push), round 4: fixed
 * capacity of rcp_discovery_encode_response_fragmented()'s own internal
 * fragment-plan array. rcp_fragment_plan_count(payload_len,
 * max_fragment_payload) never returns more than payload_len segments for
 * any max_fragment_payload >= 1 (each segment carries at least one
 * octet), and payload_len here is read_size, an actual uint8_t parameter
 * -- not merely a value this module happens to keep small in practice --
 * so 255 is a mathematically exact ceiling, not a "realistic" one the way
 * RCP_EP_CAN_MAX_FRAGMENT_SEGMENTS (ep_can.h) is. Same array-too-large-
 * for-a-safe-stack-frame concern that constant's own comment discusses
 * does not apply here: 255 entries of rcp_fragment_segment_t (fragment.h)
 * is the same order of magnitude as that already-accepted 256-entry
 * array. A caller-supplied max_fragment_payload of 0 already yields 0
 * (fragment.h's own "disabled" convention) before this ceiling is ever
 * relevant. */
#define RCP_DISCOVERY_MAX_FRAGMENT_SEGMENTS ((size_t)255u)

/* Encodes the discovery response as one or more full NTSCF-framed
 * ACF_ABB messages, fragmenting via fragment.h's ms/segment_num mechanism
 * whenever read_size exceeds max_fragment_payload octets -- into
 * out_frames[0..rcp_discovery_response_fragment_count(read_size,
 * max_fragment_payload)) (caller-allocated, sized by calling that
 * function first). Same conventions as rcp_discovery_encode_response()
 * otherwise (every fragment echoes transaction_num and is addressed via
 * server_stream_id); only the ms flag, read_size_or_segment_num
 * (meaningful only on an ms=true fragment; the final fragment carries
 * read_size itself, exactly as rcp_discovery_encode_response() always
 * does), and each fragment's own payload slice differ. When read_size
 * already fits in one fragment, this produces exactly one frame identical
 * to what rcp_discovery_encode_response() itself would have. Returns the
 * number of frames written on success, or 0 (out_frames left untouched)
 * under the same conditions rcp_discovery_response_fragment_count()
 * returns 0 for, or on allocation failure partway through (any
 * already-written out_frames entries are freed before returning). Caller
 * frees each successfully returned out_frames[i] with rcp_bytes_free(). */
size_t rcp_discovery_encode_response_fragmented(const rcp_regmap_general_t *map,
                                                 uint8_t read_size, uint8_t transaction_num,
                                                 rcp_stream_id_t server_stream_id,
                                                 size_t max_fragment_payload,
                                                 rcp_bytes_t *out_frames);

/* Decodes one fragment of a (possibly multi-fragment) discovery response
 * frame -- the same AVTP/ACF-level validation rcp_discovery_decode_response()
 * applies (see that function's own failure-mode list), but surfaces the
 * fragment's own ms bit and read_size_or_segment_num (as
 * *out_segment_num, meaningful only when *out_ms) alongside the raw ACF
 * payload slice (*out_payload / *out_payload_len, borrowed into b), for a
 * caller to feed straight into a rcp_fragment_reassembler_t (fragment.h).
 * *out_server_stream_id is populated the same way
 * rcp_discovery_decode_response()'s own result.server_stream_id is. */
rcp_discovery_errc_t rcp_discovery_decode_response_fragment(const uint8_t *b, size_t len,
                                                             rcp_stream_id_t *out_server_stream_id,
                                                             bool *out_ms,
                                                             uint8_t *out_segment_num,
                                                             const uint8_t **out_payload,
                                                             size_t *out_payload_len);

/* Once fragment.h reports RCP_FRAGMENT_REASM_COMPLETE, applies this
 * module's own general-register-slice parsing (see
 * RCP_DISCOVERY_GENERAL_SLICE_LEN's own comment) to
 * rcp_fragment_reassembler_get()'s output -- the second half of what
 * rcp_discovery_decode_response() does in one step for a single,
 * unfragmented frame. server_stream_id is whatever
 * rcp_discovery_decode_response_fragment() reported for (any of) that
 * sequence's own fragments (round-tripped identically on every fragment
 * of one logical response, the same NTSCF sender-assigns-stream_id
 * convention every fragment shares). Returns RCP_DISCOVERY_ERR_SHORT_FRAME
 * if reassembled_len is shorter than RCP_DISCOVERY_GENERAL_SLICE_LEN,
 * same as rcp_discovery_decode_response() does for an unfragmented
 * response. On RCP_DISCOVERY_OK, *out_result is populated with valid =
 * true. */
rcp_discovery_errc_t rcp_discovery_decode_reassembled_response(const uint8_t *reassembled,
                                                                size_t reassembled_len,
                                                                rcp_stream_id_t server_stream_id,
                                                                rcp_discovery_result_t *out_result);

/* ── Discovery-stream claiming ──────────────────────────────────────────────── */

/* This module's own suggested default Discovery_TimeOut, in milliseconds.
 * Never used internally by this module unless a caller passes it to
 * rcp_discovery_claim_init() -- see the file header. */
#define RCP_DISCOVERY_DEFAULT_TIMEOUT_MS ((uint32_t)20u)

/* The discovery-stream claim/timeout/re-open state for one RC Server
 * instance. There is exactly one claim per server (not per endpoint) --
 * see the file header. */
typedef struct {
    bool            held;
    rcp_stream_id_t claimant;
    uint64_t        deadline_ms; /* meaningless while !held */
    uint32_t        timeout_ms;  /* Discovery_TimeOut; caller-configured */
} rcp_discovery_claim_t;

/* Initializes claim as unheld, with the given Discovery_TimeOut. */
void rcp_discovery_claim_init(rcp_discovery_claim_t *claim, uint32_t timeout_ms);

/* True iff claim is available to be granted to a new requester as of
 * now_ms -- either never held, or held but past its deadline_ms. */
bool rcp_discovery_claim_is_open(const rcp_discovery_claim_t *claim, uint64_t now_ms);

/* Call when a discovery request arrives while the server's lifecycle
 * state is HW_UNCONFIGURED or HW_CONFIGURED (the caller is responsible
 * for that state check -- this function does not consult
 * rcp_lifecycle_state_t itself, matching this codebase's convention of
 * taking already-classified inputs). If the claim is open, grants it to
 * requester, starts a fresh Discovery_TimeOut window, and returns true --
 * the caller answers with an ordinary discovery response. If the claim is
 * already held by a not-yet-lapsed claimant (REQ-DISC-029), this is a
 * no-op on claim's own state and returns false: TC18 Figure 17's own two
 * "Discovery request received" transitions read literally --
 * "& no discovery stream assigned -> assign discovery stream -> send
 * discovery response" versus "& discovery stream assigned -> send error
 * response DISCOVERY_STREAM_OCCUPIED" -- with no carve-out for requester
 * identity, so this refusal applies uniformly whether requester is a
 * different claimant or the current one re-requesting (re-requesting
 * does not itself refresh the deadline either way -- only an actual
 * configuration write does, via rcp_discovery_claim_note_config_write()).
 *
 * DISCOVERY_STREAM_OCCUPIED is a Figure-16-diagram-only label: TC18
 * §12.9.6 Table 30's own 17 numbered wire error codes (rcp_wire_error_t,
 * errors.h) do not include it, and unlike LOCKED_CONFIG_ACCESS (which
 * cleanly maps onto RCP_ERROR_LOCKED_MEM_ACCESS, the only numbered code
 * with a semantically matching name), no numbered code here has an
 * obviously corresponding meaning -- flagged as a genuine, unresolved
 * ambiguity (same class as REQ-ACF-012's RCP_ACF_MTV_UNCERTAIN), not
 * force-mapped. This function's own bool return is therefore as far as
 * this codebase goes: which numbered wire error code (if any) a caller
 * should send for the false case is not decided here. */
bool rcp_discovery_claim_note_request(rcp_discovery_claim_t *claim,
                                       rcp_stream_id_t requester, uint64_t now_ms);

/* True iff claim is currently held by writer specifically (not lapsed) as
 * of now_ms. A pure query: does not mutate claim. One input a caller
 * combines with rcp_lifecycle_field_writable()/rcp_regmap_writer_ctx() to
 * decide whether a configuration write is authorized -- see the file
 * header. */
bool rcp_discovery_claim_is_claimant(const rcp_discovery_claim_t *claim,
                                     rcp_stream_id_t writer, uint64_t now_ms);

/* Call when a configuration write request arrives via the discovery
 * stream. If writer currently holds the claim (rcp_discovery_claim_is_claimant()
 * would return true), extends the claim's deadline by another
 * Discovery_TimeOut window from now_ms and returns true. Otherwise leaves
 * claim untouched and returns false -- including when the claim has
 * already lapsed, which this function never resurrects. */
bool rcp_discovery_claim_note_config_write(rcp_discovery_claim_t *claim,
                                           rcp_stream_id_t writer, uint64_t now_ms);

/* Unconditionally releases claim (held = false), e.g. on the server's
 * demotion back to HW_UNCONFIGURED (server.h's reset path). */
void rcp_discovery_claim_release(rcp_discovery_claim_t *claim);

/* ── Client-side discovery result persistence (thin convenience API) ──────── */

/* A growable list of previously discovered results, keyed by each
 * result's server_stream_id. See the file header: purely a client-side
 * convenience, never itself consulted by this module to decide whether to
 * (re-)issue a discovery request. */
typedef struct {
    rcp_discovery_result_t *entries;
    size_t                   len;
    size_t                   cap;
} rcp_discovery_cache_t;

/* Initializes cache as empty. */
void rcp_discovery_cache_init(rcp_discovery_cache_t *cache);

/* Frees cache's internal storage and zeroes it. Safe to call on an
 * already-empty or already-destroyed cache. */
void rcp_discovery_cache_destroy(rcp_discovery_cache_t *cache);

/* Records result in cache. An existing entry whose server_stream_id
 * matches result's is overwritten in place (last-discovered-wins for that
 * server; no history is retained). Otherwise a new entry is appended.
 * Returns true on success; returns false only when appending a genuinely
 * new entry and the internal reallocation fails (cache is left unchanged
 * in that case). */
bool rcp_discovery_cache_put(rcp_discovery_cache_t *cache,
                             const rcp_discovery_result_t *result);

/* Looks up a previously cached result for stream_id. Returns NULL if none
 * is on record. The returned pointer is borrowed: it is invalidated by any
 * subsequent rcp_discovery_cache_put()/_destroy() call on the same cache. */
const rcp_discovery_result_t *rcp_discovery_cache_find(const rcp_discovery_cache_t *cache,
                                                        rcp_stream_id_t stream_id);

/* Number of entries currently held in cache. */
size_t rcp_discovery_cache_len(const rcp_discovery_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif /* RCP_DISCOVERY_H */
