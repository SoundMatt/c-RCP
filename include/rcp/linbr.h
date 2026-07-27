/*
 * LIN (Local Interconnect Network) bus bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no SocketCAN LIN driver or
 * dedicated LIN hardware API backend is linked, so every RPC-facing call
 * returns RCP_ERR_NOT_SUPPORTED rather than attempting a master-frame
 * request over an unconfigured bus -- mirrors cpp-RCP's own linbr.hpp,
 * which ships the same stub absent a linked LIN backend. Wiring in a real
 * LIN backend is future work (see ROADMAP.md).
 */
#ifndef RCP_LINBR_H
#define RCP_LINBR_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  frame_id;   /* default: 0x10 */
    uint64_t timeout_ms; /* default: 50 */
} rcp_lin_config_t;

/* { frame_id = 0x10, timeout_ms = 50 }. */
rcp_lin_config_t rcp_lin_default_config(void);

/* Bridges RCP commands for zone to LIN master-frame requests described by
 * cfg. Every operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_lin_controller_new(rcp_zone_t zone, rcp_lin_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_LINBR_H */
