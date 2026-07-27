/*
 * SOME/IP protocol bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no vsomeip (or equivalent)
 * backend is linked, so every RPC-facing call returns
 * RCP_ERR_NOT_SUPPORTED rather than attempting a service call over an
 * unconfigured channel -- mirrors cpp-RCP's own someipbr.hpp, which ships
 * the same stub absent a linked SOME/IP backend. Wiring in a real vsomeip
 * backend is future work (see ROADMAP.md).
 */
#ifndef RCP_SOMEIPBR_H
#define RCP_SOMEIPBR_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t service_id;   /* default: 0x0100 */
    uint16_t instance_id;  /* default: 0x0001 */
    uint16_t method_id;    /* default: 0x0001 */
    uint64_t timeout_ms;   /* default: 500 */
} rcp_someip_config_t;

/* { service_id = 0x0100, instance_id = 0x0001, method_id = 0x0001,
 * timeout_ms = 500 }. */
rcp_someip_config_t rcp_someip_default_config(void);

/* Bridges RCP commands for zone to a SOME/IP service method described by
 * cfg. Every operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_someip_controller_new(rcp_zone_t zone, rcp_someip_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SOMEIPBR_H */
