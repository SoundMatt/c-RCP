/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-MOCK-001
//cfusa:req REQ-MOCK-002
//cfusa:req REQ-MOCK-003
//cfusa:req REQ-MOCK-004
//cfusa:req REQ-MOCK-005
//cfusa:req REQ-MOCK-006
//cfusa:req REQ-MOCK-007
//cfusa:req REQ-MOCK-008
//cfusa:req REQ-MOCK-009
//cfusa:req REQ-MOCK-010
//cfusa:req REQ-MOCK-011
//cfusa:req REQ-MOCK-012
//cfusa:req REQ-MOCK-013
//cfusa:req REQ-MOCK-014
//cfusa:req REQ-MOCK-015
//cfusa:req REQ-MOCK-016
//cfusa:req REQ-MOCK-017
//cfusa:req REQ-MOCK-018
//cfusa:req REQ-MOCK-019
//cfusa:req REQ-MOCK-020
/*
 * mock.h -- in-process RC-Server/endpoint test double for the TC18 Remote
 * Control Protocol wire layer (ROADMAP.md Phase 21, "Satellite Package
 * Rework", milestone 77, "Foundational test/config satellites").
 *
 * This replaces the pre-TC18 zone-controller mock (rcp_mock_controller_t /
 * rcp_mock_registry_t, the rcp_controller_t/rcp_registry_t vtables'
 * reference implementation) with a double shaped around the actual TC18
 * core this repo has built since: the RC Server lifecycle state machine
 * (lifecycle.h, milestone 61), the register-map model (regmap.h, milestone
 * 62), and the per-endpoint ep_enable request queue (server.h, milestone
 * 61). There is no Command/Response shape left to double for -- every
 * Phase 16+ endpoint type (ep_gpio.h and its siblings) and discovery.h
 * already operate on raw, already-framed AVTPDU/ACF request and response
 * bytes, so this module's dispatch surface does too.
 *
 * The old zone-controller mock (tests/legacy_mock.h, and before it this
 * file's own pre-TC18 predecessor) has since been removed outright, along
 * with the rcp_controller_t/rcp_registry_t vtables and the rest of the
 * retired rcp.h object model it doubled -- per RELAY spec §15.5, with no
 * compatibility shim. Every satellite that used to decorate those vtables
 * had already moved off them before the removal: tsn.c, deadline.c,
 * watchdog.c, and powerstate.c at milestones 78-79; authz.c, ratelimit.c,
 * loan.c, observe.c, faultinject.c, admin.c, and recorder.c at milestone
 * 80; proxy.c, redundancy.c, federation.c, zonegroup.c, prioqueue.c, and
 * firmware.c were DEPRECATE-removed outright at milestone 83 rather than
 * migrated; adapt.c REPLACEd its own rcp_controller_t dependency at
 * milestone 84 (rcp/adapt.h/adapt.c wrap rcp_avtp_transport_t instead).
 *
 * ── What this double actually is ─────────────────────────────────────────────
 *
 * rcp_mock_server_t bundles exactly the three TC18-core building blocks a
 * caller needs to exercise an endpoint module end-to-end without any real
 * transport: an rcp_lifecycle_state_t, an rcp_regmap_general_t, and a small
 * fixed-capacity table of endpoint slots, each pairing an
 * rcp_server_endpoint_t (server.h's ep_enable/queue) with a caller-supplied
 * rcp_mock_endpoint_handler_fn. This module owns none of the per-endpoint
 * wire semantics itself (it never calls into ep_gpio.c or any sibling
 * directly) -- a caller registers one handler per byte_bus_id, and that
 * handler is free to use whichever ep_*.h/discovery.h encode/decode
 * functions it is testing. This is the same "own small pure helpers,
 * operate on caller-owned data" layering discipline lifecycle.h, regmap.h,
 * and fragment.h already established; the callback shape itself is this
 * module's own direct descendant of the old rcp_mock_handler_fn.
 *
 * rcp_mock_server_dispatch() is the entry point a caller drives with one
 * already-framed request: it runs lifecycle.h's own
 * rcp_lifecycle_should_accept() admission check first (a dropped frame
 * never reaches an endpoint at all, matching a real server), then looks up
 * the addressed byte_bus_id's slot and submits the frame to its
 * rcp_server_endpoint_t queue -- exactly the same pre-load-then-drain
 * behavior a real disabled endpoint exhibits. rcp_mock_server_drain_endpoint()
 * is the other half: pulling a queued frame back out (once re-enabled) and
 * running it through the same registered handler.
 *
 * rcp_mock_server_dispatch_frame() is the TC18 §12.9.1.1 multi-request-
 * per-frame entry point: given a raw, not-yet-split NTSCF/TSCF payload
 * (potentially several ACF messages concatenated back to back), it uses
 * scheduler.h's rcp_sched_split_frame_members() to enumerate them, peeks
 * each member's own byte_bus_id (decoding just its shared byte_message_info
 * header -- acf.h), and dispatches each one to rcp_mock_server_dispatch()
 * individually, addressed to its own endpoint, exactly as if a caller had
 * split and dispatched them by hand. A single-member frame behaves
 * identically to calling rcp_mock_server_dispatch() once directly -- this
 * is a strict superset, not a parallel code path.
 */
#ifndef RCP_MOCK_H
#define RCP_MOCK_H

#include "rcp/lifecycle.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Endpoint handler ──────────────────────────────────────────────────────── */

/* Handler registered for one endpoint slot (see
 * rcp_mock_server_add_endpoint() below). request/request_len is one
 * already-framed request (e.g. built by an ep_*.h _encode_*_request()
 * function, or discovery.h's rcp_discovery_encode_request() for a
 * byte_bus_id 0/EP0 handler); the handler decodes it with that same
 * module's own _decode_*() function and encodes a result into
 * *out_response with that module's own _encode_response(). *out_response
 * is zeroed before the handler runs; leaving it zeroed means "no response
 * frame" (a fire-and-forget request). Ownership of any allocated
 * *out_response transfers to the dispatcher's own caller
 * (rcp_mock_server_dispatch()'s or rcp_mock_server_drain_endpoint()'s
 * out_response parameter); free it with rcp_bytes_free(). user_data is the
 * opaque pointer passed to rcp_mock_server_add_endpoint(). */
typedef void (*rcp_mock_endpoint_handler_fn)(const uint8_t *request, size_t request_len,
                                              rcp_bytes_t *out_response, void *user_data);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_MOCK_OK                   = 0,
    RCP_MOCK_ERR_DUPLICATE_BUS_ID = 1,
    RCP_MOCK_ERR_CAPACITY         = 2,
    RCP_MOCK_ERR_NOT_FOUND        = 3,
} rcp_mock_errc_t;

/* Human-readable message for an rcp_mock_errc_t value. Never returns NULL. */
const char *rcp_mock_strerror(rcp_mock_errc_t e);

/* The fixed number of endpoint slots an rcp_mock_server_t can hold --
 * comfortably above svr_ep_count for any test fixture built by this
 * milestone's own config.c manifest loader or by hand. A fixed table
 * (rather than a growable one) keeps this test double's own memory
 * behavior trivial to reason about, matching regmap.h's
 * rcp_regmap_table_ref_t capacity-bounded-table convention. */
#define RCP_MOCK_MAX_ENDPOINTS ((size_t)64u)

/* ── The server double ─────────────────────────────────────────────────────── */

typedef struct rcp_mock_server rcp_mock_server_t;

/* Allocates a new server double: lifecycle state RCP_LIFECYCLE_HW_UNCONFIGURED,
 * an rcp_regmap_general_init()-initialized register map, and no endpoints.
 * Returns NULL on allocation failure. Release with rcp_mock_server_destroy(). */
rcp_mock_server_t *rcp_mock_server_new(void);

/* Frees srv and every endpoint's queued (undelivered) requests. Safe to
 * call with srv == NULL. */
void rcp_mock_server_destroy(rcp_mock_server_t *srv);

/* The server's current lifecycle state. */
rcp_lifecycle_state_t rcp_mock_server_state(const rcp_mock_server_t *srv);

/* Thin passthrough to lifecycle.h's rcp_lifecycle_transition(), operating
 * on srv's own state. See lifecycle.h for the permitted-transition table
 * and snap's role. */
rcp_lifecycle_errc_t rcp_mock_server_transition(rcp_mock_server_t *srv,
                                                 rcp_lifecycle_state_t target,
                                                 const rcp_lifecycle_plausibility_snapshot_t *snap);

/* Mutable access to srv's own register map: a test or this milestone's own
 * config.c manifest loader may freely set magic/vendor_id/device_id/
 * svr_root_client_index/svr_implemented_options and the rest directly.
 * svr_ep_count is server-owned (see rcp_mock_server_add_endpoint()/
 * _remove_endpoint()) -- writing it directly here is not itself rejected,
 * but rcp_mock_server_add_endpoint()/_remove_endpoint() overwrite it with
 * the true slot count on their own next call. The returned pointer is
 * valid for srv's own lifetime; never NULL for a non-NULL srv. */
rcp_regmap_general_t *rcp_mock_server_regmap(rcp_mock_server_t *srv);

/* ── Endpoint registration ─────────────────────────────────────────────────── */

/* Adds one endpoint slot addressed at byte_bus_id, with generic config
 * ep_type/ep_used=true (regmap.h's rcp_regmap_ep_generic_cfg_t; every other
 * generic-cfg field left at its rcp_regmap_ep_generic_cfg_init() default)
 * and an rcp_server_endpoint_t queue starting at ep_enable. handler may be
 * NULL (dispatched/drained requests then produce no response, as if the
 * endpoint accepted but never replied). On success, srv's own
 * svr_ep_count is updated to the new total slot count. Returns
 * RCP_MOCK_ERR_DUPLICATE_BUS_ID if byte_bus_id is already registered,
 * RCP_MOCK_ERR_CAPACITY if srv already holds RCP_MOCK_MAX_ENDPOINTS
 * endpoints. */
rcp_mock_errc_t rcp_mock_server_add_endpoint(rcp_mock_server_t *srv,
                                              rcp_byte_bus_id_t byte_bus_id,
                                              uint8_t ep_type, bool ep_enable,
                                              rcp_mock_endpoint_handler_fn handler,
                                              void *user_data);

/* Removes the endpoint slot at byte_bus_id, freeing any requests still
 * queued on it. Returns true iff a slot was found and removed (and
 * svr_ep_count decremented); false, leaving srv unchanged, if byte_bus_id
 * was never registered. */
bool rcp_mock_server_remove_endpoint(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id);

/* Sets the ep_enable flag of the endpoint at byte_bus_id (server.h's
 * rcp_server_endpoint_set_enable()). Returns true iff byte_bus_id names a
 * registered endpoint. */
bool rcp_mock_server_set_endpoint_enable(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                          bool enable);

/* Number of requests currently queued (awaiting drain) on the endpoint at
 * byte_bus_id, or 0 if byte_bus_id names no registered endpoint. */
size_t rcp_mock_server_endpoint_queue_len(const rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id);

/* ── Dispatch ───────────────────────────────────────────────────────────────── */

typedef enum {
    /* The addressed endpoint's queue was enabled: its handler ran
     * synchronously and *out_response (possibly still zeroed, meaning "no
     * response") reflects its result. */
    RCP_MOCK_DISPATCH_OK              = 0,
    /* The addressed endpoint's queue was disabled: request queued
     * (server.h ep_enable semantics), no handler ran, *out_response is left
     * zeroed. Call rcp_mock_server_drain_endpoint() once the endpoint is
     * re-enabled to actually run it. */
    RCP_MOCK_DISPATCH_QUEUED          = 1,
    /* rcp_lifecycle_should_accept() rejected the frame outright for srv's
     * current lifecycle state; *out_response is left zeroed. Mirrors a real
     * server silently dropping the frame -- this is not itself an error. */
    RCP_MOCK_DISPATCH_DROPPED         = 2,
    /* byte_bus_id names no registered endpoint. *out_response is left
     * zeroed. */
    RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS = 3,
} rcp_mock_dispatch_result_t;

/* Runs one already-framed request through srv: first
 * rcp_lifecycle_should_accept(srv's own state, time_sync_supported,
 * avtp_subtype, acf_msg_type, byte_bus_id) (lifecycle.h); if accepted, the
 * addressed endpoint's rcp_server_endpoint_submit() decides whether the
 * request runs now (its own registered handler, synchronously) or is
 * queued. *out_response is zeroed on entry and left zeroed whenever no
 * handler actually ran or the handler chose not to produce a response;
 * caller frees any populated response with rcp_bytes_free(). */
rcp_mock_dispatch_result_t rcp_mock_server_dispatch(rcp_mock_server_t *srv,
                                                     rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                     bool time_sync_supported,
                                                     const uint8_t *request, size_t request_len,
                                                     rcp_bytes_t *out_response);

/* Drains and runs the oldest queued request on the endpoint at byte_bus_id
 * (server.h's rcp_server_endpoint_drain_one() -- a no-op unless that
 * endpoint's own ep_enable is currently true and its queue is non-empty).
 * On a successful drain, *out_response (zeroed on entry) is populated the
 * same way rcp_mock_server_dispatch() populates it, and true is returned.
 * Returns false (out_response left zeroed) if byte_bus_id names no
 * registered endpoint, or its queue has nothing to drain right now. */
bool rcp_mock_server_drain_endpoint(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                     rcp_bytes_t *out_response);

/* ── Multi-request-per-frame dispatch (TC18 §12.9.1.1) ─────────────────────── */

/* The fixed number of ACF members rcp_mock_server_dispatch_frame() can
 * enumerate/dispatch from a single frame -- mirrors RCP_MOCK_MAX_ENDPOINTS'
 * "fixed table, trivial memory behavior" rationale. A real frame's member
 * count is bounded far below this by AVTPDU payload limits (avtp.h) in
 * practice. */
#define RCP_MOCK_MAX_FRAME_MEMBERS ((size_t)32u)

/* One frame member's own dispatch outcome, as populated by
 * rcp_mock_server_dispatch_frame(). */
typedef struct {
    rcp_mock_dispatch_result_t result;
    /* The member's own decoded byte_bus_id. Meaningless (0) when result
     * could not be determined at all -- see result's own doc comment for
     * when that happens. */
    rcp_byte_bus_id_t          byte_bus_id;
    /* Same convention as rcp_mock_server_dispatch()'s own out_response:
     * zeroed unless a handler actually ran and produced one. Caller frees
     * every populated response with rcp_bytes_free(). */
    rcp_bytes_t                response;
} rcp_mock_frame_member_result_t;

/* Splits frame[0..frame_len) into its constituent ACF messages
 * (scheduler.h's rcp_sched_split_frame_members()) and dispatches each one
 * individually via rcp_mock_server_dispatch(), addressed to its own
 * decoded byte_bus_id, sharing avtp_subtype/time_sync_supported across
 * every member (both are properties of the enclosing NTSCF/TSCF frame,
 * not of an individual ACF message). Writes up to out_cap results into
 * out_results, in the same order the members appear in frame, and returns
 * how many were actually dispatched (and thus how many of out_results are
 * populated) -- 0 if frame does not parse as a well-formed sequence of ACF
 * messages at all (rcp_sched_split_frame_members() itself returned 0).
 * If more members are found than out_cap allows, only the first out_cap
 * are dispatched (mirroring rcp_sched_split_frame_members()'s own
 * out_offsets truncation) -- pass an out_results/out_cap pair of at least
 * RCP_MOCK_MAX_FRAME_MEMBERS to be certain no real frame is truncated. If
 * a member's own byte_message_info header fails to decode (e.g. its
 * byte_bus_id exceeds this build's 8-bit rcp_byte_bus_id_t range --
 * RCP_ACF_ERR_BUS_ID_OVERFLOW, acf.h), that member's result is
 * RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS with byte_bus_id left at 0 and no
 * handler run, rather than dispatching against a bogus address. */
size_t rcp_mock_server_dispatch_frame(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                       bool time_sync_supported, const uint8_t *frame,
                                       size_t frame_len,
                                       rcp_mock_frame_member_result_t *out_results,
                                       size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* RCP_MOCK_H */
