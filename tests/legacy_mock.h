//cfusa:req REQ-CTRL-001
//cfusa:req REQ-CTRL-002
//cfusa:req REQ-CTRL-003
//cfusa:req REQ-CTRL-004
//cfusa:req REQ-CTRL-005
//cfusa:req REQ-CTRL-006
//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-008
//cfusa:req REQ-CTRL-009
//cfusa:req REQ-CTRL-010
//cfusa:req REQ-CTRL-011
//cfusa:req REQ-CTRL-012
//cfusa:req REQ-CTRL-013
//cfusa:req REQ-CTRL-014
//cfusa:req REQ-CTRL-015
//cfusa:req REQ-CTRL-016
//cfusa:req REQ-CTRL-017
//cfusa:req REQ-CTRL-018
//cfusa:req REQ-CTRL-019
//cfusa:req REQ-CTRL-020
//cfusa:req REQ-CTRL-021
//cfusa:req REQ-CTRL-022
//cfusa:req REQ-CTRL-023
//cfusa:req REQ-CTRL-024
//cfusa:req REQ-CTRL-025
//cfusa:req REQ-CTRL-026
//cfusa:req REQ-CTRL-027
//cfusa:req REQ-REG-001
//cfusa:req REQ-REG-002
//cfusa:req REQ-REG-003
//cfusa:req REQ-REG-004
//cfusa:req REQ-REG-005
//cfusa:req REQ-REG-006
//cfusa:req REQ-REG-007
//cfusa:req REQ-REG-008
//cfusa:req REQ-REG-009
//cfusa:req REQ-REG-010
//cfusa:req REQ-REG-011
//cfusa:req REQ-REG-012
//cfusa:req REQ-REG-013
//cfusa:req REQ-RESP-001
//cfusa:req REQ-RESP-002
//cfusa:req REQ-STAT-001
//cfusa:req REQ-STAT-002
//cfusa:req REQ-STAT-003
//cfusa:req REQ-STAT-004
//cfusa:req REQ-ERR-011
/*
 * legacy_mock.h -- test-only relocation of the pre-TC18 in-process
 * rcp_controller_t/rcp_registry_t reference implementation (formerly
 * include/rcp/mock.h + src/mock.c). See ROADMAP.md's "Foundational
 * test/config satellites (v0.77.0)" entry and include/rcp/mock.h's own
 * file header for why this moved: milestone 77 replaced mock.h/mock.c
 * with a TC18-shaped RC-Server/endpoint test double that has no
 * Command/Response shape left to double the legacy vtables with.
 * (tsn.c, deadline.c, watchdog.c, and powerstate.c moved off this double
 * at milestones 78 and 79; authz.c, ratelimit.c, loan.c, observe.c,
 * faultinject.c, admin.c, and recorder.c moved off it at milestone 80;
 * proxy.c, redundancy.c, federation.c, zonegroup.c, prioqueue.c, and
 * firmware.c were DEPRECATE-removed outright at milestone 83 rather than
 * migrated; adapt.c REPLACEd its own rcp_controller_t dependency at
 * milestone 84 -- none of their test binaries link this file any more,
 * see tests/CMakeLists.txt.)
 *
 * This file is a pure relocation, not a rewrite: same struct layout, same
 * function/type names, same REQ-CTRL-*, REQ-REG-*, REQ-RESP-*, REQ-STAT-*,
 * REQ-ERR-011 requirement coverage (now exercised by
 * tests/test_legacy_mock.c, itself a renamed copy of the old
 * tests/test_mock.c). Deliberately not installed/shipped as part of the
 * public rcp library. With milestone 84 landed, no satellite decorates the
 * legacy vtables any more -- this file's only remaining consumers are its
 * own test (tests/test_legacy_mock.c) and the bench_mock/
 * command_latency_test benchmarking tools (see tests/CMakeLists.txt).
 * Whether to retire this file (and those two benchmarks) outright, versus
 * keeping it as a still-useful reference-implementation exercise, is left
 * as an open follow-up call rather than decided unilaterally here -- it
 * is not part of milestone 84's own "REPLACE relay.h + adapt.c" scope.
 *
 * All operations execute synchronously in memory — no I/O, minimal
 * threading (one watcher thread per subscription, to auto-expire the
 * returned channel on context timeout or controller close). Safe for
 * concurrent use.
 */
#ifndef RCP_LEGACY_MOCK_H
#define RCP_LEGACY_MOCK_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User-supplied function producing a Response for a Command. If NULL is
 * passed to rcp_mock_controller_new(), the controller returns
 * RCP_RESPONSE_OK with an empty payload. user_data is the opaque pointer
 * passed to rcp_mock_controller_new(); *out is zeroed before the handler is
 * invoked. If the handler wants a response payload, it must allocate it
 * (e.g. via rcp_bytes_dup) — ownership transfers to the response. */
typedef void (*rcp_mock_handler_fn)(const rcp_command_t *cmd, rcp_response_t *out, void *user_data);

/* Creates a mock zone controller. Returned with refcount 1 (release with
 * rcp_controller_release()). handler may be NULL. */
rcp_controller_t *rcp_mock_controller_new(rcp_zone_t zone, rcp_mock_handler_fn handler, void *user_data);

/* Pushes a Status update (zone = ctrl's own zone, seq = incrementing,
 * healthy = !closed) to all active subscribers. payload may be NULL iff
 * len==0. Safe to call after close() (a no-op in that case: the subscriber
 * list is already empty). ctrl must have been created by
 * rcp_mock_controller_new(). */
void rcp_mock_controller_publish(rcp_controller_t *ctrl, const uint8_t *payload, size_t len);

/* Creates an in-process registry pre-populated with the five standard zones
 * (front-left/front-right/rear-left/rear-right/central), each backed by a
 * default (no-handler) mock controller. Release with rcp_registry_close()
 * then rcp_registry_destroy(). */
rcp_registry_t *rcp_mock_registry_new(void);

#ifdef __cplusplus
}
#endif

#endif /* RCP_LEGACY_MOCK_H */
