/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-SHMEM-001
//cfusa:req REQ-SHMEM-002
//cfusa:req REQ-SHMEM-003
//cfusa:req REQ-SHMEM-004
//cfusa:req REQ-SHMEM-005
//cfusa:req REQ-SHMEM-006
//cfusa:req REQ-SHMEM-007
//cfusa:req REQ-SHMEM-008
//cfusa:req REQ-SHMEM-009
/*
 * shmem.h -- in-process AVTPDU-frame loopback transport for the TC18
 * Remote Control Protocol wire layer (ROADMAP.md Phase 21, "Satellite
 * Package Rework", milestone 78, "Transport satellites").
 *
 * REPLACEs the pre-TC18 in-process Command/Response double
 * (rcp_shmem_zone_server_t / rcp_shmem_controller_t) with a two-ended
 * pair of rcp_avtp_transport_t (avtp.h, milestone 59) implementations:
 * whatever one side send()s, the other side's recv() receives, entirely
 * in-process (no real I/O) -- the same "shared memory" role the
 * predecessor module played for the old Command/Response shape, rebuilt
 * against the new AVTPDU-frame wire layer instead.
 *
 * ── Relationship to avtp.c's own loopback transport, and to mock.c ─────────
 *
 * avtp.c already ships rcp_avtp_loopback_transport_new(): a single
 * rcp_avtp_transport_t instance that echoes its own send()s back to its
 * own recv() -- a reference implementation of the vtable contract and a
 * test double for code that only needs *a* transport, not a second,
 * distinct party on its other end. This module solves the complementary
 * problem the old rcp_shmem_zone_server_t/rcp_shmem_controller_t pair
 * actually solved: two distinct in-process endpoints (e.g. a client-side
 * handle and a server-side handle) that exchange AVTPDUs with each
 * other without any real transport, so a test (or a genuinely in-process
 * deployment) can hold one end while whatever it is testing holds the
 * other. A single self-looping instance cannot play both roles at once
 * without one side's own sends corrupting the other side's expected
 * inbound stream, so this is new, additive surface -- not a thin rename
 * of avtp.c's own loopback type. See rcp_shmem_avtp_pair_new() below.
 *
 * mock.c (milestone 77) solves a third, still-different problem: it
 * doubles the *dispatch* logic an RC Server applies to an already-framed
 * request (lifecycle admission, per-endpoint ep_enable queueing, a
 * caller-registered handler) -- it never moves bytes across anything,
 * because a caller drives it directly with already-decoded parameters,
 * not a transport. Consolidating this module into mock.c would hand
 * mock.c an entire second responsibility (byte-level transport
 * plumbing) it has no other reason to own, and would leave this
 * module's own transport-vtable-conformance role (exercising the same
 * close()/recv()-timeout/refcount contract avtp.c's loopback and udp.c's
 * real socket transport both exercise) with no home. Per ROADMAP.md's
 * own instruction to evaluate consolidation rather than assume
 * separateness: these three modules stay separate because each answers
 * a genuinely different question (echo a single node's own traffic;
 * connect two distinct in-process nodes; double a server's dispatch
 * behavior) -- not because nobody checked.
 */
#ifndef RCP_SHMEM_H
#define RCP_SHMEM_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RCP_SHMEM_OK        = 0,
    RCP_SHMEM_ERR_ALLOC = 1,
} rcp_shmem_errc_t;

/* Creates two cross-wired rcp_avtp_transport_t endpoints sharing one pair
 * of bounded FIFOs (capacity queue_capacity each way, minimum 1): whatever
 * *out_a send()s, *out_b's recv() receives, and vice versa. Both share
 * time_sync_supported (avtp.h's own base field), since they model two
 * ends of the same in-process association rather than two independently
 * time-sync-capable nodes. Both are returned with refcount 1; release
 * each independently with rcp_avtp_transport_release() -- the shared
 * internal state they point at is itself refcounted and freed only once
 * both sides have released it, so releasing (or closing) one side first
 * is safe and leaves the other side's own send()/recv() usable until it
 * is itself closed (recv() on the still-open side then drains whatever
 * was already queued before reporting RCP_ERR_CLOSED once empty, the
 * same "closed but still has queued items" behavior avtp.c's own
 * loopback transport exhibits). Returns RCP_SHMEM_ERR_ALLOC (leaving
 * *out_a and *out_b untouched) on allocation failure. */
rcp_shmem_errc_t rcp_shmem_avtp_pair_new(bool time_sync_supported, size_t queue_capacity,
                                          rcp_avtp_transport_t **out_a,
                                          rcp_avtp_transport_t **out_b);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SHMEM_H */
