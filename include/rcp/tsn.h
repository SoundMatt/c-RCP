/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-TSN-001
//cfusa:req REQ-TSN-002
//cfusa:req REQ-TSN-003
//cfusa:req REQ-TSN-004
//cfusa:req REQ-TSN-005
//cfusa:req REQ-TSN-006
//cfusa:req REQ-TSN-007
/*
 * tsn.h -- IEEE 802.1p PCP tagging for the TC18 Remote Control Protocol
 * wire layer (ROADMAP.md Phase 21, "Satellite Package Rework", milestone
 * 78, "Transport satellites").
 *
 * ADAPTs the predecessor module's own 802.1p PCP-tagging/SO_PRIORITY
 * mechanism: the mechanism itself (map a message to a PCP value, apply
 * SO_PRIORITY to a socket before send() so the egress qdisc places the
 * frame in the right 802.1p traffic class) is unchanged. What moves is
 * its priority *source*. The retired rcp_priority_t enum
 * (Normal/High/Critical, a client-assigned per-command label) is gone;
 * this module now classifies the outgoing AVTPDU frame itself, via
 * scheduler.h's rcp_sched_kind_t (Phase 17, milestone 69) -- the
 * protocol-defined, server-side execution-priority ordering
 * (cancellation > triggered > timed > compound > compound-wait > chained
 * > standard) every request already carries in its own encoding, not a
 * value the client separately assigns.
 *
 * This module wraps an inner rcp_avtp_transport_t (avtp.h, milestone 59)
 * rather than the retired rcp_controller_t: send() peeks the outgoing
 * frame's AVTPDU subtype and, for an ACF_GBB payload, its repurposed
 * request_type opcode (acf.h / request_compound.h's shared
 * message_timestamp-repurposing convention) to classify it via
 * scheduler.h's rcp_sched_classify(), tags socket_fd with the resulting
 * PCP value, then delegates unchanged to inner's own send(). recv()/
 * close()/destroy() are pure passthroughs -- PCP tagging is an egress-only
 * concept.
 *
 * Full 802.1Qbv gate scheduling requires a TSN-capable NIC and kernel
 * >= 4.15. On standard hardware, or any non-Linux platform, this
 * provides best-effort priority mapping only -- the setsockopt() call is
 * compiled out elsewhere, and send() still delegates to the inner
 * transport.
 */
#ifndef RCP_TSN_H
#define RCP_TSN_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/scheduler.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maps each rcp_sched_kind_t to an IEEE 802.1p PCP value (0-7), indexed
 * directly by the enum value. */
typedef struct {
    uint8_t pcp[7]; /* RCP_SCHED_KIND_STANDARD .. RCP_SCHED_KIND_CANCELLATION */
} rcp_tsn_pcp_map_t;

/* Default map: pcp[kind] = rcp_sched_kind_rank(kind) (scheduler.h), i.e.
 * PCP mirrors the protocol's own execution-priority rank directly --
 * Standard maps to PCP 0, Cancellation (the highest-priority kind) to
 * PCP 6. PCP 7 is left unused by the default map, available to a
 * caller-supplied override that wants to reserve it for something
 * outside this module's own scope (e.g. a network-wide highest-priority
 * class shared with non-RCP traffic). */
rcp_tsn_pcp_map_t rcp_tsn_default_pcp_map(void);

/* Returns the PCP value m assigns to kind. Returns
 * m->pcp[RCP_SCHED_KIND_STANDARD] for any kind value outside
 * rcp_sched_kind_t's own defined range -- fail-safe: never a fabricated
 * higher class for unrecognized input, mirroring scheduler.h's own
 * rcp_sched_classify() convention. */
uint8_t rcp_tsn_pcp_for(const rcp_tsn_pcp_map_t *m, rcp_sched_kind_t kind);

typedef struct {
    rcp_tsn_pcp_map_t pcp_map;
    int                vlan_id;  /* 0 = untagged */
    int                cycle_ns; /* 802.1Qbv gate cycle in nanoseconds (0 = disabled) */
} rcp_tsn_config_t;

/* { pcp_map = rcp_tsn_default_pcp_map(), vlan_id = 0, cycle_ns = 0 }. */
rcp_tsn_config_t rcp_tsn_default_config(void);

/* Classifies an already-encoded AVTPDU frame (as produced by avtp.c's own
 * rcp_avtp_encode_ntscf()/_tscf()) into its rcp_sched_kind_t -- the same
 * way rcp_tsn_avtp_transport_new()'s own send() classifies its outgoing
 * frame before tagging. Exposed directly so a caller can predict the PCP
 * a given frame will receive without constructing a wrapper transport
 * first. Malformed input (a short frame, an unrecognized AVTPDU subtype,
 * an unrecognized ACF message type, or an ACF_GBB message whose mtv is
 * not the "repurposed" value) classifies as RCP_SCHED_KIND_STANDARD --
 * the fail-safe default, never a fabricated higher class. */
rcp_sched_kind_t rcp_tsn_classify_frame(const uint8_t *frame, size_t frame_len);

/* Wraps inner (retains it) with SO_PRIORITY tagging applied to socket_fd
 * before each send(), so the egress qdisc places the frame in the
 * correct 802.1p traffic class. Pass socket_fd = -1 to skip the
 * setsockopt() call (e.g. when inner does not own a raw socket, such as
 * an rcp_avtp_loopback_transport_t or one side of an
 * rcp_shmem_avtp_pair_new() pair) -- send() still forwards to inner. The
 * returned transport's own base.time_sync_supported mirrors inner's.
 * Returned with refcount 1; release with rcp_avtp_transport_release(),
 * which also releases this wrapper's reference to inner. */
rcp_avtp_transport_t *rcp_tsn_avtp_transport_new(rcp_avtp_transport_t *inner, int socket_fd,
                                                  rcp_tsn_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_TSN_H */
