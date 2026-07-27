/*
 * DDS (Data Distribution Service) protocol bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no OMG DDS implementation (e.g.
 * FastDDS, Cyclone DDS) is linked, so every RPC-facing call returns
 * RCP_ERR_NOT_SUPPORTED rather than publishing to an unconfigured topic --
 * mirrors cpp-RCP's own ddsbr.hpp, which ships the same stub absent a
 * linked DDS backend. Wiring in a real DDS backend is future work (see
 * ROADMAP.md).
 */
#ifndef RCP_DDSBR_H
#define RCP_DDSBR_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *topic_prefix;  /* DDS topic names: {prefix}/command,
                                 * {prefix}/response. Default: "rcp" */
    int         domain_id;      /* default: 0 */
    uint64_t    timeout_ms;     /* default: 500 */
} rcp_dds_config_t;

/* { topic_prefix = "rcp", domain_id = 0, timeout_ms = 500 }. */
rcp_dds_config_t rcp_dds_default_config(void);

/* Bridges RCP commands for zone to DDS typed topics described by cfg.
 * Every operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_dds_controller_new(rcp_zone_t zone, rcp_dds_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_DDSBR_H */
