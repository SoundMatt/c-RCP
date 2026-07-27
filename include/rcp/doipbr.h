/*
 * DoIP (Diagnostics over IP / ISO 13400) bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no DoIP stack integration is
 * linked, so every RPC-facing call returns RCP_ERR_NOT_SUPPORTED rather
 * than encapsulating a UDS request inside a DoIP diagnostic message over
 * TCP/IP -- mirrors cpp-RCP's own doipbr.hpp, which ships the same stub
 * absent a linked DoIP backend. Wiring in a real DoIP backend is future
 * work (see ROADMAP.md).
 */
#ifndef RCP_DOIPBR_H
#define RCP_DOIPBR_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *server_ip;      /* must be set by caller -- no default IP,
                                  * NULL if unset */
    uint16_t    server_port;    /* default: 13400 */
    uint16_t    logical_addr;   /* default: 0x0001 */
    uint64_t    tcp_timeout_ms; /* default: 2000 */
} rcp_doip_config_t;

/* { server_ip = NULL, server_port = 13400, logical_addr = 0x0001,
 * tcp_timeout_ms = 2000 }. */
rcp_doip_config_t rcp_doip_default_config(void);

/* Bridges RCP commands for zone to DoIP diagnostic messages described by
 * cfg. Every operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_doip_controller_new(rcp_zone_t zone, rcp_doip_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_DOIPBR_H */
