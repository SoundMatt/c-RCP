//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
//cfusa:req REQ-SRV-004
//cfusa:req REQ-SRV-005
//cfusa:req REQ-SRV-006
//cfusa:req REQ-SRV-007
//cfusa:req REQ-SRV-008
//cfusa:req REQ-SRV-009
//cfusa:req REQ-SRV-010
//cfusa:req REQ-SRV-011
//cfusa:req REQ-SRV-012
//cfusa:req REQ-SRV-013
//cfusa:req REQ-SRV-014
//cfusa:req REQ-SRV-015
//cfusa:req REQ-SRV-016
//cfusa:req REQ-SRV-017
//cfusa:req REQ-SRV-018
//cfusa:req REQ-SRV-019
//cfusa:req REQ-SRV-020
//cfusa:req REQ-SRV-021
//cfusa:req REQ-SRV-022
//cfusa:req REQ-SRV-023
//cfusa:req REQ-SRV-024
/*
 * server.h -- RC Server lifecycle state machine for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 14, "RC Server Lifecycle & Register
 * Map", milestone 61).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59) and ACF message format (acf.h/acf.c,
 * milestone 60). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, or any
 * satellite package is touched here.
 *
 * An RC Server's own bring-up progresses through exactly three lifecycle
 * states (this module's own original engineering design for representing
 * that progression -- only the state names, their high-level meaning, and
 * their three on-wire byte values are taken by reference from the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC; no spec text is reproduced here):
 *
 *   - HW_UNCONFIGURED (0x00): the server's boot default. No endpoint has a
 *     usable hardware mapping yet. Only the discovery/bootstrap channel is
 *     reachable.
 *   - HW_CONFIGURED (0x55): every endpoint marked in-use now has a plausible
 *     hardware pin mapping and at least one configured request stream.
 *     Hardware-level configuration becomes read-only from this point on.
 *   - RCP_CONFIGURED (0xAA): every in-use endpoint additionally has a
 *     stream/byte_bus_id association, and every configured request stream
 *     has an associated response stream. Only functional (not hardware)
 *     configuration remains writable, and only through an endpoint's own
 *     registered stream(s) or the root client via EP0.
 *
 * The full register-map layout backing "hardware pin mapping", "request
 * stream", "response stream", EP0, and the root-client model is milestone
 * 62's job (ROADMAP.md, "Register-map model"). This module only needs a
 * minimal, self-contained stand-in surface -- the
 * rcp_server_plausibility_snapshot_t below -- sufficient to make its own
 * transition guards and per-state filtering testable; it deliberately does
 * not reach ahead into milestone 62's register tables.
 */
#ifndef RCP_SERVER_H
#define RCP_SERVER_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle states ──────────────────────────────────────────────────────── */

/* The three RC Server lifecycle states and their defined on-wire byte
 * values. Ordering here follows the server's normal forward bring-up
 * progression; see rcp_server_lifecycle_transition() for which transitions
 * (forward and demotion) are actually permitted. */
typedef enum {
    RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED = 0x00,
    RCP_SERVER_LIFECYCLE_HW_CONFIGURED   = 0x55,
    RCP_SERVER_LIFECYCLE_RCP_CONFIGURED  = 0xAA,
} rcp_server_lifecycle_t;

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_SERVER_OK                      = 0,
    RCP_SERVER_ERR_HW_CFG_INCONSISTENT  = 1,
    RCP_SERVER_ERR_RCP_CFG_INCONSISTENT = 2,
    RCP_SERVER_ERR_INVALID_TRANSITION   = 3,
} rcp_server_errc_t;

/* Human-readable message for an rcp_server_errc_t value. Never returns NULL. */
const char *rcp_server_strerror(rcp_server_errc_t e);

/* ── Plausibility snapshot (transition-guard input) ────────────────────────── */

/* One endpoint's configuration state, as far as this milestone's transition
 * guards need to see it. The full per-endpoint register layout (ep_type,
 * generic-vs-functional config split, ...) is milestone 62's job; this
 * struct is deliberately a minimal stand-in carrying only the four facts
 * the two plausibility checks below actually consult. */
typedef struct {
    bool ep_used;            /* this endpoint slot is in use */
    bool hw_pin_mapped;      /* a valid HW pin mapping is present */
    bool has_request_stream; /* at least one configured request stream exists
                                 for this endpoint */
    bool has_stream_assoc;   /* a stream/byte_bus_id association exists for
                                 this endpoint */
} rcp_server_endpoint_plausibility_t;

/* One request stream's configuration state, as far as the RCP_CFG_INCONSISTENT
 * guard needs to see it. */
typedef struct {
    bool configured;          /* this request stream slot is configured */
    bool has_response_stream; /* an associated response stream exists */
} rcp_server_request_stream_plausibility_t;

/* A read-only view over every endpoint and request stream slot, passed to
 * the plausibility checks and to rcp_server_lifecycle_transition(). Neither
 * array is copied or retained beyond the call. */
typedef struct {
    const rcp_server_endpoint_plausibility_t *endpoints;
    size_t                                     endpoint_count;
    const rcp_server_request_stream_plausibility_t *request_streams;
    size_t                                           request_stream_count;
} rcp_server_plausibility_snapshot_t;

/* The HW_CFG_INCONSISTENT plausibility check: returns RCP_SERVER_OK iff
 * every endpoint with ep_used set has both hw_pin_mapped and
 * has_request_stream set. Endpoints with ep_used == false are ignored.
 * snap == NULL is treated as inconsistent (fail-safe: a transition attempt
 * with no configuration evidence at all must not be treated as vacuously
 * plausible). */
rcp_server_errc_t rcp_server_check_hw_cfg(const rcp_server_plausibility_snapshot_t *snap);

/* The RCP_CFG_INCONSISTENT plausibility check: returns RCP_SERVER_OK iff
 * every endpoint with ep_used set has has_stream_assoc set, and every
 * request stream with configured set has has_response_stream set. snap ==
 * NULL is treated as inconsistent, for the same fail-safe reason as above. */
rcp_server_errc_t rcp_server_check_rcp_cfg(const rcp_server_plausibility_snapshot_t *snap);

/* ── Lifecycle transitions ─────────────────────────────────────────────────── */

/* Attempts to move *state to target. On success, *state is updated to
 * target and RCP_SERVER_OK is returned; on failure *state is left
 * unchanged and the failure reason is returned. Permitted transitions:
 *
 *   - HW_UNCONFIGURED -> HW_CONFIGURED: guarded by rcp_server_check_hw_cfg();
 *     failure returns RCP_SERVER_ERR_HW_CFG_INCONSISTENT.
 *   - HW_CONFIGURED -> RCP_CONFIGURED: guarded by rcp_server_check_rcp_cfg();
 *     failure returns RCP_SERVER_ERR_RCP_CFG_INCONSISTENT.
 *   - HW_CONFIGURED -> HW_UNCONFIGURED and RCP_CONFIGURED ->
 *     HW_UNCONFIGURED: unconditional demotion (the discovery-stream/
 *     root-client reset path); snap is ignored for these two.
 *   - state -> the same state: always a no-op success; snap is ignored.
 *
 * Any other requested transition (e.g. skipping straight from
 * HW_UNCONFIGURED to RCP_CONFIGURED, or downgrading from RCP_CONFIGURED to
 * HW_CONFIGURED without first returning all the way to HW_UNCONFIGURED) is
 * rejected with RCP_SERVER_ERR_INVALID_TRANSITION. */
rcp_server_errc_t rcp_server_lifecycle_transition(rcp_server_lifecycle_t *state,
                                                   rcp_server_lifecycle_t target,
                                                   const rcp_server_plausibility_snapshot_t *snap);

/* ── Per-state request filtering ───────────────────────────────────────────── */

/* The discovery byte_bus_id: the one address reachable while the server is
 * still HW_UNCONFIGURED. */
#define RCP_SERVER_DISCOVERY_BYTE_BUS_ID ((rcp_byte_bus_id_t)0u)

/* The per-state request-filtering rule, as its own directly-tested
 * function (mirroring avtp.c's rcp_avtp_should_drop_tscf() convention):
 *
 *   - Whatever the state, a TSCF-headed frame is first subject to
 *     rcp_avtp_should_drop_tscf()'s ordinary time-sync rule.
 *   - While HW_UNCONFIGURED: a TSCF-headed frame is dropped outright
 *     regardless of time_sync_supported (presentation-time semantics
 *     presuppose a configured request stream, which cannot exist yet), and
 *     an NTSCF-headed frame is accepted only if it carries an ACF_ABB
 *     message addressed to RCP_SERVER_DISCOVERY_BYTE_BUS_ID; everything
 *     else is silently dropped.
 *   - While HW_CONFIGURED or RCP_CONFIGURED: acceptance beyond the
 *     time-sync rule already applied above is unrestricted at this
 *     milestone -- fine-grained per-field write access is
 *     rcp_server_field_writable()'s job below, and full endpoint/stream
 *     routing is milestone 62's register-map job.
 *
 * avtp_subtype is one of RCP_AVTP_SUBTYPE_NTSCF/_TSCF (see avtp.h);
 * acf_msg_type is one of RCP_ACF_MSG_TYPE_ABB/_GBB (see acf.h), or any
 * other value for a message type this filtering rule does not special-case. */
bool rcp_server_lifecycle_should_accept(rcp_server_lifecycle_t state,
                                        bool time_sync_supported,
                                        uint8_t avtp_subtype,
                                        uint8_t acf_msg_type,
                                        rcp_byte_bus_id_t byte_bus_id);

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

/* Which broad category a register field falls into for locking purposes.
 * HW_GENERIC covers HW-pin-mapping and other generic (vendor-agnostic)
 * configuration; FUNCTIONAL_W and FUNCTIONAL_W_STAR both cover functional
 * configuration but differ in what happens once RCP_CONFIGURED is reached
 * -- modeled as two distinct enum values rather than one writability bit,
 * per this milestone's explicit scope. */
typedef enum {
    RCP_SERVER_FIELD_HW_GENERIC        = 0,
    RCP_SERVER_FIELD_FUNCTIONAL_W      = 1,
    RCP_SERVER_FIELD_FUNCTIONAL_W_STAR = 2,
} rcp_server_field_kind_t;

/* Identifies who is attempting a functional-config write, for the
 * once-RCP_CONFIGURED authorization rule. Both members may be true
 * (e.g. the root client happens to also be the owning stream); only one
 * needs to be true for the write to be authorized. */
typedef struct {
    bool via_root_client_ep0; /* request arrived via EP0 from the root client */
    bool via_owning_stream;   /* request arrived via the endpoint's own
                                  registered request stream */
} rcp_server_writer_ctx_t;

/* True iff a field of the given kind is writable while the server is in
 * state, by the given writer:
 *
 *   - RCP_SERVER_FIELD_HW_GENERIC: writable only in HW_UNCONFIGURED --
 *     read-only from the moment the server reaches HW_CONFIGURED, for any
 *     writer.
 *   - RCP_SERVER_FIELD_FUNCTIONAL_W: not writable in HW_UNCONFIGURED
 *     (functional configuration presupposes a hardware mapping); writable
 *     by any writer while HW_CONFIGURED; once RCP_CONFIGURED, writable
 *     only when writer indicates the endpoint's own stream or the root
 *     client via EP0.
 *   - RCP_SERVER_FIELD_FUNCTIONAL_W_STAR: same as FUNCTIONAL_W through
 *     HW_CONFIGURED, but permanently locked (unwritable by any writer, not
 *     just an unauthorized one) once RCP_CONFIGURED is reached -- this is
 *     the distinction the roadmap requires be modeled explicitly rather
 *     than collapsed into a single writability bit. */
bool rcp_server_field_writable(rcp_server_lifecycle_t state,
                               rcp_server_field_kind_t kind,
                               rcp_server_writer_ctx_t writer);

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

/* A single endpoint's enable flag and its pending-request queue. A disabled
 * endpoint still accepts (queues) incoming requests without executing them;
 * queued requests drain out, in FIFO order, once the endpoint is
 * re-enabled. This struct owns the byte copies held in its queue. */
typedef struct {
    bool         ep_enable;
    rcp_bytes_t *queue;
    size_t       queue_len;
    size_t       queue_cap;
} rcp_server_endpoint_t;

/* Initializes ep with an empty queue and the given initial ep_enable value. */
void rcp_server_endpoint_init(rcp_server_endpoint_t *ep, bool ep_enable);

/* Frees every queued request and ep's internal queue storage. Safe to call
 * on an endpoint with an empty queue. Does not free ep itself. */
void rcp_server_endpoint_destroy(rcp_server_endpoint_t *ep);

/* Submits one already-framed request of frame_len octets (frame may be NULL
 * iff frame_len == 0) to ep. If ep->ep_enable is true, this is a no-op on
 * the queue and the function returns true, meaning the caller must execute
 * the request itself right now. If ep->ep_enable is false, a copy of the
 * request is appended to ep's queue and the function returns false,
 * meaning the request has been queued rather than executed. Returns false
 * (still meaning "queued") without actually growing the queue if the
 * internal reallocation fails -- callers relying on eventual delivery under
 * allocation failure must check rcp_server_endpoint_queue_len() themselves. */
bool rcp_server_endpoint_submit(rcp_server_endpoint_t *ep,
                                const uint8_t *frame, size_t frame_len);

/* Sets ep->ep_enable. Toggling it does not itself execute or discard
 * anything queued; call rcp_server_endpoint_drain_one() afterward to pull
 * queued requests back out once re-enabled. */
void rcp_server_endpoint_set_enable(rcp_server_endpoint_t *ep, bool enable);

/* If ep->ep_enable is true and ep's queue is non-empty, dequeues the
 * oldest queued request into *out_frame (caller takes ownership; free with
 * rcp_bytes_free()) and returns true. Otherwise returns false and leaves
 * *out_frame untouched -- including while ep->ep_enable is false, so a
 * disabled endpoint's queue can never be silently drained out from under it. */
bool rcp_server_endpoint_drain_one(rcp_server_endpoint_t *ep, rcp_bytes_t *out_frame);

/* Number of requests currently queued (awaiting drain) on ep. */
size_t rcp_server_endpoint_queue_len(const rcp_server_endpoint_t *ep);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SERVER_H */
