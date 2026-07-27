/*
 * UDS (Unified Diagnostic Services / ISO 14229) bridge interface stub
 * (SG-006).
 *
 * This is a compile-time interface stub: no UDS stack integration is
 * linked, so every RPC-facing call returns RCP_ERR_NOT_SUPPORTED rather
 * than attempting a RoutineControl (SID 0x31) request over an
 * unconfigured stack -- mirrors cpp-RCP's own udsbr.hpp, which ships the
 * same stub absent a linked UDS backend. Wiring in a real UDS backend is
 * future work (see ROADMAP.md).
 */
#ifndef RCP_UDSBR_H
#define RCP_UDSBR_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t routine_id;        /* default: 0x0100 */
    uint64_t p2_timeout_ms;     /* default P2 server timeout, default: 50 */
    uint64_t p2ext_timeout_ms;  /* extended P2* timeout, default: 5000 */
} rcp_uds_config_t;

/* { routine_id = 0x0100, p2_timeout_ms = 50, p2ext_timeout_ms = 5000 }. */
rcp_uds_config_t rcp_uds_default_config(void);

/* Bridges RCP commands for zone to UDS RoutineControl requests described
 * by cfg. Every operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_uds_controller_new(rcp_zone_t zone, rcp_uds_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_UDSBR_H */
