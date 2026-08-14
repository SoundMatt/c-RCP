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
//cfusa:req REQ-MOCK-021
//cfusa:req REQ-MOCK-022
//cfusa:req REQ-MOCK-023
//cfusa:req REQ-MOCK-024
//cfusa:req REQ-MOCK-025
//cfusa:req REQ-MOCK-026
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
 *
 * rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e() (issue #197,
 * added after this module's original milestone) are the E2E-aware
 * counterparts of the two entry points above: for an endpoint with
 * req_crc_enable set (rcp_mock_server_set_endpoint_req_crc_enable()),
 * a request's e2e.h CRC32 trailer is verified -- and, on mismatch, the
 * request is never even admitted, only a conformant error response
 * built -- before anything else happens. This is deliberately not a
 * change to the plain dispatch()/dispatch_frame() pair, which keep
 * ignoring e2e.h entirely, exactly as before: nothing about this
 * milestone's "own small pure helpers, operate on caller-owned data"
 * layering discipline changes, this is purely additive surface wiring
 * an already-existing pure primitive (e2e.h's rcp_e2e_unwrap_framed())
 * into this double's own dispatch path for the first time.
 */
#ifndef RCP_MOCK_H
#define RCP_MOCK_H

#include "rcp/e2e.h"
#include "rcp/lifecycle.h"
#include "rcp/power.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/request_sequencer.h"
#include "rcp/server.h"
#include "rcp/watchdog.h"

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
 * on srv's own state. See lifecycle.h for the permitted-transition table,
 * snap's role, writer's role (REQ-LIFECYCLE-031), and
 * all_other_eps_idle's role (REQ-LIFECYCLE-022). */
rcp_lifecycle_errc_t rcp_mock_server_transition(rcp_mock_server_t *srv,
                                                 rcp_lifecycle_state_t target,
                                                 const rcp_lifecycle_plausibility_snapshot_t *snap,
                                                 rcp_lifecycle_writer_ctx_t writer,
                                                 bool all_other_eps_idle);

/* REQ-PWRMODE-019: calls power.h's own rcp_pwrmode_handshake_resume_queues(hs)
 * first; power.h deliberately never touches server.h itself (see that
 * module's own file header -- "driving the actual re-init sequence
 * through lifecycle.h remains a caller's job", the same statement applies
 * to server.h), so this srv-aware wrapper is where TC18 §12.4.1's "all
 * used endpoints and response queues will be enabled" actually happens for
 * this test double: every registered endpoint slot's queue is re-enabled
 * (rcp_server_endpoint_set_enable(), matching a disabled endpoint's own
 * pre-load-then-drain semantics) iff the handshake's own resume-queues
 * step reports success. Returns that same bool. Response-queue objects
 * and heartbeat-stream re-emission are NOT modeled by this fix -- neither
 * concept has ANY implementation anywhere in this codebase yet (see
 * test_flush_triggers_and_heartbeat_are_absent(), tests/test_tc18_gaps_
 * regmap.c), a separate, already-tracked architecture gap this function
 * cannot close. */
bool rcp_mock_server_pwrmode_resume(rcp_mock_server_t *srv, rcp_pwrmode_handshake_t *hs);

/* Mutable access to srv's own register map: a test or this milestone's own
 * config.c manifest loader may freely set magic/vendor_id/device_id/
 * svr_root_client_index/svr_implemented_options and the rest directly.
 * svr_ep_count is server-owned (see rcp_mock_server_add_endpoint()/
 * _remove_endpoint()) -- writing it directly here is not itself rejected,
 * but rcp_mock_server_add_endpoint()/_remove_endpoint() overwrite it with
 * the true slot count on their own next call. The returned pointer is
 * valid for srv's own lifetime; never NULL for a non-NULL srv. */
rcp_regmap_general_t *rcp_mock_server_regmap(rcp_mock_server_t *srv);

/* ── HW_config table (REQ-RMAP-040/041) ────────────────────────────────────── */

/* Replaces srv's own HW_config table wholesale with a copy of
 * entries[0..len). Returns false (srv's own table left unchanged) if len
 * exceeds RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES (regmap.h); true otherwise.
 * TC18 §12.7.6's own "only changeable in HW_UNCONFIGURED" rule is NOT
 * enforced here -- this mirrors rcp_mock_server_regmap()'s own existing
 * "caller may freely set... directly" convention for every other
 * general-register-map field, deliberately consistent with it rather
 * than a one-off exception; a real wire-write dispatch path (not yet
 * implemented, see regmap.h's own file-header note on this whole
 * section) is where that lifecycle-state gate belongs, the same way
 * REQ-RMAP-025's own gate lives in rcp_regmap_general_decode_write_
 * request(), not in rcp_mock_server_regmap()'s own direct-pointer
 * access. */
bool rcp_mock_server_set_hw_pin_map(rcp_mock_server_t *srv,
                                     const rcp_regmap_hw_pin_map_entry_t *entries, size_t len);

/* srv's own current HW_config table and its length. The returned pointer
 * is valid for srv's own lifetime (or until the next
 * rcp_mock_server_set_hw_pin_map() call); never NULL for a non-NULL srv,
 * even when *out_len is 0. */
const rcp_regmap_hw_pin_map_entry_t *rcp_mock_server_hw_pin_map(const rcp_mock_server_t *srv,
                                                                  size_t *out_len);

/* Replaces srv's own request-stream-cfg table wholesale with a copy of
 * entries[0..count). Returns false (srv's own table left unchanged) if
 * count exceeds RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES (regmap.h);
 * true otherwise. REQ-SEQ-013 (issue #335): this is srv's own only way
 * to resolve an incoming request's stream_id into the 1-based
 * request_stream_index identity rcp_mock_server_dispatch()'s own
 * sequencer-ownership check (below) needs -- see
 * rcp_regmap_request_stream_cfg_resolve_index()'s own doc comment
 * (regmap.h). Same "caller may freely set... directly, no lifecycle-
 * state gate here" convention rcp_mock_server_set_hw_pin_map() already
 * establishes. */
bool rcp_mock_server_set_request_stream_cfg(rcp_mock_server_t *srv,
                                             const rcp_regmap_request_stream_cfg_t *entries,
                                             size_t count);

/* Replaces srv's own response-queue-cfg table wholesale with a copy of
 * entries[0..count). Returns false (srv's own table left unchanged) if
 * count exceeds RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES (regmap.h);
 * true otherwise. REQ-RMAP-034 (response-stream half): mirrors
 * rcp_mock_server_set_request_stream_cfg()'s own shape exactly, keeping
 * svr_response_stream_cfg_capacity (Table 20) synced to the real table's
 * own length. Same "caller may freely set... directly, no lifecycle-
 * state gate here" convention rcp_mock_server_set_hw_pin_map()/
 * _set_request_stream_cfg() already establish. */
bool rcp_mock_server_set_response_queue_cfg(rcp_mock_server_t *srv,
                                             const rcp_regmap_response_queue_cfg_t *entries,
                                             size_t count);

/* Replaces srv's own EP_ID_config table (TC18 §12.7.8 Table 23) wholesale
 * with a copy of entries[0..count). Returns false (srv's own table left
 * unchanged) if count exceeds RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES (regmap.h);
 * true otherwise. Issue #335 (REQ-E2E-029/030/045): this is srv's own only
 * way to know which byte_bus_ids are bound to a given request stream --
 * rcp_mock_server_broadcast_safe_state()'s own (below) sole data source.
 * Same "caller may freely set... directly, no lifecycle-state gate here"
 * convention rcp_mock_server_set_hw_pin_map()/_set_request_stream_cfg()
 * already establish. Entries are consulted in the order given, up to
 * count -- a caller wanting TC18's own end-of-table sentinel behavior
 * (request_stream_index == 0) applies
 * rcp_regmap_ep_id_map_effective_count() itself before calling this
 * function, the same "caller has already bounded the table" convention
 * regmap.h's own EP_ID_config diagnostics already use. */
bool rcp_mock_server_set_ep_id_map(rcp_mock_server_t *srv,
                                    const rcp_regmap_ep_id_map_entry_t *entries, size_t count);

/* Installs srv's own network-interface-config section (TC18 §12.7.11,
 * REQ-RMAP-039) as a copy of data[0..len). Returns false (srv's own
 * section left unchanged) if len exceeds
 * RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_MAX_OCTETS (regmap.h); true
 * otherwise. Also syncs svr_network_interface_cfg_capacity (Table 20) to
 * len -- the same capacity-sync convention
 * rcp_mock_server_set_hw_pin_map() already established (REQ-RMAP-032).
 * A caller still owns setting svr_network_interface_cfg_ptr itself
 * (rcp_mock_server_regmap(), same "caller may freely set... directly"
 * convention every other pointer register already uses) -- this
 * function only installs the content the pointer will eventually
 * address, not the address itself. len == 0 (or never calling this at
 * all) correctly leaves the section unreachable, matching TC18's own
 * zero-pointer "not supported" default. */
bool rcp_mock_server_set_network_interface_cfg(rcp_mock_server_t *srv, const uint8_t *data,
                                                size_t len);

/* srv's own network-interface-config section. Never NULL for a
 * non-NULL srv. See rcp_mock_server_set_network_interface_cfg()'s own
 * doc comment for what this handle's fields mean and how to wire it
 * into the EP0 dispatcher (rcp_regmap_optional_subsystem_cfg_ptrs_t,
 * regmap.h). */
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_network_interface_cfg(rcp_mock_server_t *srv);

/* Installs srv's own physical-layer-config section (TC18 §12.7.12,
 * REQ-RMAP-039) -- otherwise identical to
 * rcp_mock_server_set_network_interface_cfg() above, syncing
 * svr_physical_layer_cfg_capacity instead. */
bool rcp_mock_server_set_physical_layer_cfg(rcp_mock_server_t *srv, const uint8_t *data,
                                             size_t len);

/* srv's own physical-layer-config section -- see
 * rcp_mock_server_network_interface_cfg()'s own doc comment. */
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_physical_layer_cfg(rcp_mock_server_t *srv);

/* Installs srv's own time-synch-config section (TC18 §12.7.13,
 * REQ-RMAP-039) -- otherwise identical to
 * rcp_mock_server_set_network_interface_cfg() above, syncing
 * svr_time_synch_cfg_capacity instead. */
bool rcp_mock_server_set_time_synch_cfg(rcp_mock_server_t *srv, const uint8_t *data, size_t len);

/* srv's own time-synch-config section -- see
 * rcp_mock_server_network_interface_cfg()'s own doc comment. */
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_time_synch_cfg(rcp_mock_server_t *srv);

/* Installs srv's own security-config section (TC18 §12.7.14,
 * REQ-RMAP-039) -- otherwise identical to
 * rcp_mock_server_set_network_interface_cfg() above, syncing
 * svr_security_cfg_capacity instead. */
bool rcp_mock_server_set_security_cfg(rcp_mock_server_t *srv, const uint8_t *data, size_t len);

/* srv's own security-config section -- see
 * rcp_mock_server_network_interface_cfg()'s own doc comment. */
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_security_cfg(rcp_mock_server_t *srv);

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

/* Sets whether rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e()
 * require a valid e2e.h CRC32 trailer on requests addressed to the
 * endpoint at byte_bus_id -- this test double's own in-process stand-in
 * for TC18 §12.7.1's ep_req_crc_enable (rcp_regmap_ep_functional_cfg_t),
 * which every concrete endpoint type's own cfg struct composes but which
 * this module's type-erased rcp_mock_endpoint_slot_t has no way to read
 * generically (each endpoint type is a different concrete struct). Has
 * no effect on the plain rcp_mock_server_dispatch()/_dispatch_frame() --
 * those never consult it, unchanged from every version before this one.
 * Defaults false ("plain command mode", TC18 §13.6) for every endpoint
 * added via rcp_mock_server_add_endpoint(). Returns true iff byte_bus_id
 * names a registered endpoint. */
bool rcp_mock_server_set_endpoint_req_crc_enable(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id, bool enable);

/* REQ-E2E-021 (issue #201): this test double's own in-process stand-in
 * for TC18 §12.7.7 Table 24's own rx_enforce_e2e -- a per-REQUEST-
 * STREAM config bit in the real spec, kept here as a per-endpoint
 * stand-in for the exact same "type-erased slot has no way to read a
 * real config table generically" reason
 * rcp_mock_server_set_endpoint_req_crc_enable()'s own doc comment
 * already gives. Consulted only when rcp_mock_server_dispatch_e2e()/
 * _dispatch_frame_e2e() detects a CRC mismatch on the addressed
 * endpoint: true means that mismatch also latches the whole stream
 * faulted (rcp_e2e_stream_fault_tracker_on_crc_error(), e2e.h), not
 * just this one request. Has no effect without a stream fault tracker
 * also being set (rcp_mock_server_set_stream_fault_tracker() below).
 * Defaults false, matching req_crc_enable's own default disposition.
 * Returns true iff byte_bus_id names a registered endpoint. */
bool rcp_mock_server_set_endpoint_rx_enforce_e2e(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id, bool enable);

/* REQ-WDG-010 (issue #201): associates keeper with srv so that
 * rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e() call
 * rcp_watchdog_keeper_kick(keeper, stream_id) for every request they
 * receive on that stream -- TC18 §12.7.7: "the watchdog is reset with
 * each request received from this RC Client." Kicked unconditionally at
 * the top of dispatch_e2e(), before "plain command mode" delegation,
 * CRC validation, or admission are even attempted: the rule is about
 * RECEIPT, not successful validation or execution -- a request this
 * server goes on to reject (unknown byte_bus_id, CRC mismatch, full
 * queue) still means the RC Client is alive and talking, which is all
 * the watchdog's own liveness concern is about.
 *
 * Deliberately does NOT touch the plain rcp_mock_server_dispatch()/
 * _dispatch_frame() -- neither takes a stream_id parameter at all (no
 * key to kick by), and widening their own signature would be a much
 * larger, more invasive change across every existing call site in this
 * codebase's own test suite than this fix's own narrow scope justifies.
 * A caller reaching endpoints only through the plain dispatch path gets
 * no watchdog kicking; this is a real, separate, still-open gap,
 * documented rather than silently left implicit (REQ-WDG-010's own
 * .fusa-reqs.json text).
 *
 * keeper may be NULL (the default for every rcp_mock_server_t) to
 * disable kicking entirely -- srv does NOT take ownership of keeper;
 * the caller remains responsible for its own rcp_watchdog_keeper_new()/
 * _destroy() lifecycle, matching every other satellite-package pointer
 * this module holds without owning (see this file's own header). */
void rcp_mock_server_set_watchdog_keeper(rcp_mock_server_t *srv,
                                          rcp_watchdog_keeper_t *keeper);

/* REQ-E2E-021 (issue #201): associates tracker with srv so that
 * rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e() (a) reject EVERY
 * request on a stream tracker reports faulted
 * (rcp_e2e_stream_fault_tracker_is_faulted()) with
 * RCP_MOCK_DISPATCH_STREAM_FAULTED, checked before plain-command-mode
 * delegation, CRC validation, or admission -- TC18 §12.7.7's own "stream
 * is blocked until released" is a whole-STREAM property (this tracker
 * is keyed by stream_id, not byte_bus_id), so the block applies
 * uniformly regardless of the addressed endpoint's own req_crc_enable;
 * and (b) record every CRC mismatch they detect against tracker via
 * rcp_e2e_stream_fault_tracker_on_crc_error(), latching the stream
 * faulted iff the addressed endpoint's own rx_enforce_e2e stand-in is
 * set (rcp_mock_server_set_endpoint_rx_enforce_e2e() above).
 *
 * tracker may be NULL (the default for every rcp_mock_server_t) to
 * disable stream-fault blocking entirely -- srv does NOT take ownership
 * of tracker; the caller remains responsible for its own
 * rcp_e2e_stream_fault_tracker_init() lifecycle (a plain, non-heap-
 * allocated struct -- no separate _destroy() exists), matching every
 * other satellite-package pointer this module holds without owning. */
void rcp_mock_server_set_stream_fault_tracker(rcp_mock_server_t *srv,
                                               rcp_e2e_stream_fault_tracker_t *tracker);

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
    /* A conditional request (compound / compound-wait / triggered / timed
     * / chained): decoded and placed in the addressed endpoint's request
     * store, awaiting its own execution condition. No handler ran and
     * *out_response is left zeroed -- drive rcp_mock_server_tick() to
     * execute it once its condition is satisfied. */
    RCP_MOCK_DISPATCH_PENDING         = 4,
    /* A cancellation request (clear-all / clear-non-safestate /
     * clear-single): applied immediately against the addressed endpoint's
     * request store. *out_response is left zeroed. */
    RCP_MOCK_DISPATCH_CANCELLED       = 5,
    /* The message's own opcode byte claims a conditional request kind that
     * it then fails to decode as, or the addressed endpoint's request store
     * is full. Nothing was stored or executed. */
    RCP_MOCK_DISPATCH_REJECTED        = 6,
    /* Multi-member frames only: this chained member has no predecessor to
     * chain to (it is the frame's first request), so it and every member
     * after it is ignored. */
    RCP_MOCK_DISPATCH_CHAIN_ERROR     = 7,
    /* Multi-member frames only: this chained member's predecessor errored
     * and the member selected RCP_CHAINED_CS_ABORT_ON_ERROR, or an earlier
     * member had already aborted the chain. */
    RCP_MOCK_DISPATCH_CHAIN_ABORTED   = 8,
    /* rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e() only: the
     * addressed endpoint has req_crc_enable set (see
     * rcp_mock_server_set_endpoint_req_crc_enable()) and request's
     * trailing CRC32 (e2e.h) did not validate -- TC18 §13.6: the request
     * is NOT executed. *out_response carries a real Table 30
     * POCI_FAILURE error response when the frame was at least long
     * enough to read a transaction_num back out of (RCP_ERROR_NONE
     * otherwise -- nothing conformant to build a response from). */
    RCP_MOCK_DISPATCH_CRC_ERROR       = 9,
    /* REQ-E2E-021 (issue #201): rcp_mock_server_dispatch_e2e()/
     * _dispatch_frame_e2e() only -- a stream fault tracker is set
     * (rcp_mock_server_set_stream_fault_tracker()) and reports
     * stream_id already latched faulted (TC18 §12.7.7's own "stream is
     * blocked until released" consequence of an earlier CRC error on
     * this stream, with the addressed endpoint's own rx_enforce_e2e
     * stand-in set). Checked, and can fire, BEFORE any CRC validation
     * of THIS request even happens -- distinct from RCP_MOCK_DISPATCH_
     * CRC_ERROR above, which means THIS request's own CRC failed.
     * *out_response carries the same real Table 30 POCI_FAILURE error
     * response CRC_ERROR itself uses (the block's own root cause is a
     * CRC failure), under the same "frame long enough to read a
     * transaction_num back out of" condition. */
    RCP_MOCK_DISPATCH_STREAM_FAULTED  = 10,
    /* REQ-E2E-028 (issue #338): rcp_mock_server_dispatch_frame()/
     * _dispatch_frame_e2e() only -- the frame's own sequence_num failed
     * rcp_e2e_seq_evaluate()'s admission check (TC18 §12.7.7 Table 24
     * rx_enforce_seq: a replayed or reordered AVTPDU). Every member of the
     * frame is rejected together (sequence_num is a property of the whole
     * AVTPDU, not of any one ACF member packed inside it) -- see
     * rcp_mock_server_dispatch_frame()'s own doc comment. *out_response is
     * left zeroed, the same "no per-member wire response" convention
     * RCP_MOCK_DISPATCH_DROPPED already establishes. */
    RCP_MOCK_DISPATCH_SEQ_ERROR       = 11,
} rcp_mock_dispatch_result_t;

/* Runs one already-framed request through srv: first
 * rcp_lifecycle_should_accept(srv's own state, time_sync_supported,
 * avtp_subtype, acf_msg_type, byte_bus_id) (lifecycle.h); if accepted, the
 * addressed endpoint's rcp_server_endpoint_submit() decides whether the
 * request runs now (its own registered handler, synchronously) or is
 * queued. *out_response is zeroed on entry and left zeroed whenever no
 * handler actually ran or the handler chose not to produce a response;
 * caller frees any populated response with rcp_bytes_free().
 *
 * REQ-WDG-010 (TC18 §12.7.7, "the watchdog is reset with each request
 * received from this RC Client"): stream_id is this request's own AVTPDU
 * stream_id -- every RCP message, "plain command mode" (this function)
 * included, is carried inside an NTSCF/TSCF AVTPDU that always has one
 * (§13.6's plain-vs-safe command mode distinction is only about whether
 * CRC32 protection is applied, not about which AVTPDU header fields are
 * present). If srv has an associated watchdog keeper
 * (rcp_mock_server_set_watchdog_keeper()), this call kicks it for
 * stream_id unconditionally, before the lifecycle/admission checks below
 * are even attempted -- receipt, not successful validation, is what TC18
 * ties the reset to, mirroring rcp_mock_server_dispatch_e2e()'s own
 * identical ordering. A caller with no watchdog keeper set passes
 * whatever stream_id its own transport layer reports; the value is
 * simply never looked up. */
rcp_mock_dispatch_result_t rcp_mock_server_dispatch(rcp_mock_server_t *srv,
                                                     rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                     bool time_sync_supported, uint64_t stream_id,
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
 * a member's own byte_message_info header fails to decode as either
 * ACF_ABB or ACF_GBB (e.g. an unrecognized acf_msg_type -- byte_bus_id
 * itself can no longer be the cause, now that rcp_byte_bus_id_t holds
 * the full 11-bit wire range, REQ-RMAP-053/REQ-ACF-020), that member's
 * result is RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS with byte_bus_id left at
 * 0 and no handler run, rather than dispatching against a bogus
 * address.
 *
 * REQ-WDG-010: stream_id is shared across every member (a property of
 * the enclosing frame, same as avtp_subtype/time_sync_supported above)
 * and passed through to each member's own rcp_mock_server_dispatch()
 * call, so a multi-request frame kicks the watchdog once per member --
 * TC18's own rule is per REQUEST, and each member is independently one.
 *
 * REQ-E2E-028/029 (issue #338): sequence_num is this frame's own AVTPDU
 * Sequence_Nr (avtp.h's rcp_avtp_ntscf_header_t/rcp_avtp_tscf_header_t
 * sequence_num field, already decoded by whatever caller demultiplexed
 * this AVTPDU before handing this function its ACF payload) -- a
 * property of the WHOLE frame, evaluated exactly ONCE here, before any
 * member is processed, never per member (a legitimate 2nd+ member of a
 * multi-member frame would otherwise be spuriously rejected as a replay
 * against the 1st member's own just-advanced tracker state). Resolves
 * stream_id to a configured request stream the same way the overflow-
 * safestate check does (rcp_regmap_request_stream_cfg_resolve_index());
 * an unresolvable stream_id skips the check entirely (fail-toward-no-
 * action, same disposition). When resolved, rcp_e2e_seq_evaluate()
 * (e2e.h) is run against that stream's own caller-owned tracker and its
 * own rx_enforce_seq/rx_seq_safestate_enable config bits
 * (rcp_mock_server_set_request_stream_cfg()):
 *
 *   - result.enter_safe_state drives every endpoint bound to the stream
 *     toward its configured safe state (rcp_mock_server_broadcast_
 *     safe_state()), the same escalation shape already proven for
 *     REQ-E2E-030 (overflow) and REQ-E2E-045 (CRC error) -- checked
 *     regardless of result.accept, since a gap (advanced by more than
 *     one) is evidence of a problem even when ordering itself still
 *     held.
 *   - !result.accept (a replay/reorder, rx_enforce_seq only) rejects the
 *     WHOLE frame: every member up to out_cap is written
 *     RCP_MOCK_DISPATCH_SEQ_ERROR (byte_bus_id 0, response zeroed,
 *     mirroring RCP_MOCK_DISPATCH_DROPPED's own "no per-member wire
 *     response" convention) and this function returns immediately,
 *     without decoding or dispatching any member at all. */
size_t rcp_mock_server_dispatch_frame(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                       bool time_sync_supported, uint64_t stream_id,
                                       uint8_t sequence_num,
                                       const uint8_t *frame, size_t frame_len,
                                       rcp_mock_frame_member_result_t *out_results,
                                       size_t out_cap);

/* ── E2E-aware dispatch (TC18 §13.6, issue #197 REQ-E2E-031/033/041) ───────── */

/* rcp_mock_server_dispatch()'s E2E-aware counterpart: identical in every
 * respect (same lifecycle/admission/queueing behavior, same
 * out_response convention) EXCEPT that when the addressed endpoint has
 * req_crc_enable set (rcp_mock_server_set_endpoint_req_crc_enable()),
 * request is first run through e2e.h's rcp_e2e_unwrap_framed() --
 * is_ntscf_framed is derived from avtp_subtype ==
 * RCP_AVTP_SUBTYPE_NTSCF (avtp.h), not a separate parameter, so this
 * function's caller cannot pass an inconsistent combination of the two
 * -- keyed on stream_id/avtp_timestamp, before anything else happens:
 *
 *   - RCP_E2E_ERR_CRC_MISMATCH: the request is NOT executed and NOT
 *     admitted at all (rcp_server_endpoint_admit() is never called) --
 *     RCP_MOCK_DISPATCH_CRC_ERROR is returned, and *out_response carries
 *     a real Table 30 POCI_FAILURE (e2e.h's own spelling of CRC_ERROR;
 *     see errors.h's file header) error response, built the same way
 *     finish_admission() already builds one from a determined
 *     rcp_wire_error_t: byte_bus_id is this call's own parameter,
 *     transaction_num is read back out of request's own header via
 *     rcp_acf_unpack_header() (unaffected by the CRC failure -- only the
 *     trailer, not the header, differs from a valid message).
 *   - RCP_E2E_ERR_SHORT_FRAME: same RCP_MOCK_DISPATCH_CRC_ERROR
 *     outcome, but *out_response is left zeroed -- request was too
 *     short to even carry a CRC32 trailer, so rcp_acf_unpack_header()
 *     is not attempted (nothing conformant to build a response from,
 *     the same "RCP_ERROR_NONE means no determined code" convention
 *     finish_admission() and rcp_server_endpoint_admit() both already
 *     follow).
 *   - RCP_E2E_OK: dispatch proceeds exactly as
 *     rcp_mock_server_dispatch() would, but against the *unwrapped*
 *     header-and-payload region (acf_msg_length already adapted back
 *     down by e2e.h) rather than the original CRC-trailered request --
 *     every other admission/execution/queueing rule is untouched.
 *
 * When the addressed endpoint's req_crc_enable is NOT set (including
 * when byte_bus_id names no registered endpoint at all -- the same
 * RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS path as always), this function
 * delegates to rcp_mock_server_dispatch() unchanged: "plain command
 * mode" (TC18 §13.6) never touches e2e.h at all, matching every
 * pre-existing caller's behavior exactly. */
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_e2e(rcp_mock_server_t *srv,
                                                          rcp_byte_bus_id_t byte_bus_id,
                                                          uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                          bool time_sync_supported,
                                                          uint64_t stream_id, uint32_t avtp_timestamp,
                                                          const uint8_t *request, size_t request_len,
                                                          rcp_bytes_t *out_response);

/* rcp_mock_server_dispatch_frame()'s E2E-aware counterpart -- same
 * member-splitting behavior (TC18 §12.9.1.1), but each member is routed
 * through rcp_mock_server_dispatch_e2e() instead of
 * rcp_mock_server_dispatch(), so each E2E-protected member of a
 * multi-ACF-message frame carries and is verified against its own CRC32
 * individually (TC18 §13.6's "a separate CRC32... for each E2E-protected
 * ACF message" requirement -- REQ-E2E-033), never one CRC across the
 * whole frame. stream_id/avtp_timestamp are shared across every member,
 * the same way avtp_subtype/time_sync_supported already are (both are
 * properties of the enclosing NTSCF/TSCF frame, not of an individual ACF
 * message). Every other parameter and the return value are exactly
 * rcp_mock_server_dispatch_frame()'s own, including the new sequence_num
 * parameter and its own once-per-frame REQ-E2E-028/029 gate -- see that
 * function's own doc comment; this variant's gate runs before any
 * member's own E2E/CRC handling, exactly where the plain variant's runs
 * relative to lifecycle/admission. */
size_t rcp_mock_server_dispatch_frame_e2e(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                           bool time_sync_supported, uint64_t stream_id,
                                           uint32_t avtp_timestamp, uint8_t sequence_num,
                                           const uint8_t *frame,
                                           size_t frame_len,
                                           rcp_mock_frame_member_result_t *out_results,
                                           size_t out_cap);

/* ── Conditional-request execution (TC18 §11.2.2) ─────────────────────────── */

/* Allocates srv's own sequencer-state table (request_sequencer.h) with
 * count registers, replacing and freeing any table it already held. A
 * count of 0 leaves srv with the "compound operations unsupported"
 * table, in which case no compound or compound-wait request stored on any
 * of srv's endpoints ever becomes due. Returns false on allocation
 * failure, leaving srv without a table. */
bool rcp_mock_server_set_sequencer_count(rcp_mock_server_t *srv, uint16_t count);

/* Mutable access to srv's own sequencer-state table, so a test can drive
 * a sequencer into the state a stored compound request is waiting for
 * (rcp_sequencer_set_state()) and read back the state a completed one
 * advanced it to. Never NULL for a non-NULL srv; the table is the
 * unsupported ({NULL,0}) one until
 * rcp_mock_server_set_sequencer_count() is called. */
rcp_sequencer_table_t *rcp_mock_server_sequencers(rcp_mock_server_t *srv);

/* Runs at most one due conditional request on the endpoint at
 * byte_bus_id: selects the highest-priority currently-due stored request
 * (server.h's rcp_server_endpoint_select_due(), i.e. scheduler.h's
 * rcp_sched_compare() ordering with e2e.h's safety-request gate applied),
 * runs it through that endpoint's registered handler exactly as
 * rcp_mock_server_dispatch() would run a standard request, and then
 * finalizes it (rcp_server_endpoint_complete(), which advances a compound
 * request's sequencer and applies the repetition rule).
 *
 * ctx supplies the tick's own time, gPTP state, endpoint-idle and
 * safe-state flags, and the caller's already-evaluated compound-wait
 * comparison result; its sequencers field is ignored and overwritten with
 * srv's own table. Returns true iff a request was selected and run, in
 * which case *out_response (zeroed on entry) carries whatever the handler
 * produced. Returns false, leaving *out_response zeroed, when byte_bus_id
 * names no endpoint or nothing is due. */
bool rcp_mock_server_tick(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                           const rcp_server_tick_ctx_t *ctx, rcp_bytes_t *out_response);

/* Reports one observed trigger occurrence, emitted by endpoint source_ep
 * as its trigger signal number signal_nr, to every endpoint registered on
 * srv -- a triggered request stored on one endpoint routinely waits on a
 * *different* endpoint's trigger signal, which is exactly what
 * trigger_source_ep names. Returns how many stored triggered requests, in
 * total across every endpoint, counted this occurrence. */
size_t rcp_mock_server_notify_trigger(rcp_mock_server_t *srv, uint8_t source_ep,
                                       uint8_t signal_nr);

/* Number of conditional requests currently held in the request store of
 * the endpoint at byte_bus_id, or 0 if byte_bus_id names no registered
 * endpoint. */
size_t rcp_mock_server_pending_count(const rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id);

/* Applies a watchdog overflow to the endpoint at byte_bus_id: purges
 * every non-safety-tagged request from its store (server.h's
 * rcp_server_endpoint_watchdog_purge(), i.e. e2e.h's own
 * keep-only-the-safety-sequence rule), leaving the 0x8x ones to drive the
 * endpoint into its safe state. Returns how many requests were purged. */
size_t rcp_mock_server_watchdog_purge(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id);

/* ── Cross-endpoint safe-state broadcast (issue #335) ────────────────────────
 *
 * The cross-endpoint orchestrator's own actuator half: e2e.h names three
 * per-cause decisions -- rcp_e2e_seq_evaluate()'s own
 * result.enter_safe_state (REQ-E2E-029), rcp_e2e_overflow_should_enter_
 * safe_state() (REQ-E2E-030), rcp_e2e_crc_error_should_enter_safe_state()
 * (REQ-E2E-045) -- that all say the SAME thing once true: "drive every
 * endpoint bound to this stream toward its configured safe state", not
 * just the one endpoint whose own admit()/dispatch() call happened to
 * observe the fault. Until this function, no caller in this codebase
 * could act on that "every endpoint bound to this stream" part at all --
 * see regmap.h's own rcp_regmap_ep_id_map_byte_bus_ids_for_stream()
 * (issue #335) for the query primitive this function is built on, and
 * e2e.h's own file header for the fuller architectural background.
 *
 * "Toward its configured safe state" is, concretely, this codebase's own
 * already-established watchdog-purge action
 * (rcp_mock_server_watchdog_purge() above, itself
 * rcp_server_endpoint_watchdog_purge()/e2e.h's keep-only-the-safety-
 * sequence rule): every non-safety-tagged (0x8x) request currently queued
 * on a bound endpoint is purged, leaving only the safety-tagged ones
 * (rcp_e2e_request_may_execute()'s own gate) to actually drive that
 * endpoint through its safe state once it reaches the front of its own
 * queue. This function is that SAME action, applied to every byte_bus_id
 * srv's own EP_ID_config table (rcp_mock_server_set_ep_id_map()) reports
 * bound to request_stream_index, not merely one.
 *
 * request_stream_index is the 1-based identity
 * rcp_regmap_request_stream_cfg_resolve_index() (regmap.h) already
 * produces from a request's own stream_id -- this function does not
 * re-resolve stream_id itself, matching the "resolve once at the
 * dispatch call site, pass the resolved identity down" precedent
 * dispatch_plain()'s own REQ-SEQ-013 sequencer-ownership check already
 * established in this same module. A request_stream_index naming no row
 * in srv's own EP_ID_config table (including 0, the "stream not
 * resolved" sentinel rcp_regmap_request_stream_cfg_resolve_index() itself
 * returns on a miss) purges nothing and returns 0 -- this function never
 * broadcasts to "every endpoint on srv" as a fallback; an unconfigured or
 * unresolvable stream binds no endpoints, by construction.
 *
 * A bound byte_bus_id naming no endpoint currently registered on srv
 * (rcp_mock_server_add_endpoint() never called for it, or since removed)
 * is silently skipped -- this test double cannot purge a queue that does
 * not exist, the same "type-erased slot, no way to reach what isn't
 * there" boundary every other per-endpoint mock.c function already
 * respects.
 *
 * Returns the total number of requests purged, summed across every bound,
 * currently-registered endpoint (rcp_mock_server_watchdog_purge()'s own
 * per-endpoint return value, added up) -- 0 both for "no endpoints bound"
 * and for "endpoints bound, nothing queued to purge on any of them";
 * these are not distinguished by this return value alone, matching
 * rcp_mock_server_watchdog_purge()'s own single-endpoint convention. */
size_t rcp_mock_server_broadcast_safe_state(rcp_mock_server_t *srv, uint8_t request_stream_index);

#ifdef __cplusplus
}
#endif

#endif /* RCP_MOCK_H */
