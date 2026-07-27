/*
 * CAN / CAN-FD protocol bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no SocketCAN (Linux) or hardware
 * CAN driver backend is linked, so every RPC-facing call returns
 * RCP_ERR_NOT_SUPPORTED rather than attempting a frame send over an
 * unconfigured channel -- mirrors cpp-RCP's own canbr.hpp, which ships
 * the same stub absent a linked CAN backend. Wiring in a real CAN backend
 * is future work (see ROADMAP.md).
 */
#ifndef RCP_CANBR_H
#define RCP_CANBR_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t can_id_base;  /* base arbitration ID, default: 0x100 */
    bool     fd_mode;      /* CAN-FD frames, default: false */
    uint64_t timeout_ms;   /* default: 100 */
} rcp_can_config_t;

/* { can_id_base = 0x100, fd_mode = false, timeout_ms = 100 }. */
rcp_can_config_t rcp_can_default_config(void);

/* Bridges RCP commands for zone to CAN frames described by cfg. Every
 * operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_can_controller_new(rcp_zone_t zone, rcp_can_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_CANBR_H */
