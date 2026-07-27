/*
 * IEEE 802.1Qbv-aware transport adapter for hard real-time Ethernet.
 *
 * Maps rcp_priority_t values to IEEE 802.1p PCP (Priority Code Point)
 * classes and applies SO_PRIORITY on a UDP socket so the Linux traffic
 * shaper routes frames into the correct egress queue.
 *
 * Full 802.1Qbv gate scheduling requires a TSN-capable NIC and kernel
 * >= 4.15. On standard hardware, or any non-Linux platform, this provides
 * best-effort priority mapping only — the setsockopt() call is compiled
 * out elsewhere, and send() still delegates to the inner controller.
 */
#ifndef RCP_TSN_H
#define RCP_TSN_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maps each rcp_priority_t to an IEEE 802.1p PCP value (0-7). PCP 7 is the
 * highest priority (used for RCP_PRIORITY_CRITICAL). */
typedef struct {
    uint8_t normal;
    uint8_t high;
    uint8_t critical;
} rcp_tsn_pcp_map_t;

/* { normal = 2, high = 5, critical = 7 }, matching cpp-RCP's defaults. */
rcp_tsn_pcp_map_t rcp_tsn_default_pcp_map(void);

/* Returns the PCP value m assigns to priority p. */
uint8_t rcp_tsn_pcp_for(const rcp_tsn_pcp_map_t *m, rcp_priority_t p);

typedef struct {
    rcp_tsn_pcp_map_t pcp_map;
    int vlan_id;  /* 0 = untagged */
    int cycle_ns; /* 802.1Qbv gate cycle in nanoseconds (0 = disabled) */
} rcp_tsn_config_t;

/* { pcp_map = rcp_tsn_default_pcp_map(), vlan_id = 0, cycle_ns = 0 }. */
rcp_tsn_config_t rcp_tsn_default_config(void);

/* Wraps inner (retains it) with SO_PRIORITY tagging applied to socket_fd
 * before each send(), so the egress qdisc places the frame in the correct
 * 802.1p traffic class. Pass socket_fd = -1 to skip the setsockopt() call
 * (e.g. when inner does not own a raw socket) — send() still forwards to
 * inner. Returned with refcount 1; release with rcp_controller_release(),
 * which also releases this wrapper's reference to inner. */
rcp_controller_t *rcp_tsn_controller_new(rcp_controller_t *inner, int socket_fd,
                                          rcp_tsn_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_TSN_H */
