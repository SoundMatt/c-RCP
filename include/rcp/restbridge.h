/*
 * REST protocol bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no HTTP client backend is linked,
 * so every RPC-facing call returns RCP_ERR_NOT_SUPPORTED rather than
 * attempting a request over an unconfigured channel -- mirrors cpp-RCP's
 * own restbridge.hpp, which ships the same stub absent a linked HTTP
 * backend. Wiring in a real HTTP client is future work (see ROADMAP.md).
 */
#ifndef RCP_RESTBRIDGE_H
#define RCP_RESTBRIDGE_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *base_url;           /* e.g. "https://localhost:8443" */
    int          max_retries;        /* default: 3 */
    uint64_t     request_timeout_ms; /* default: 1000 */
} rcp_rest_config_t;

/* { base_url = NULL, max_retries = 3, request_timeout_ms = 1000 }. */
rcp_rest_config_t rcp_rest_default_config(void);

/* Bridges RCP commands for zone to a REST remote endpoint described by
 * cfg. Every operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_rest_controller_new(rcp_zone_t zone, rcp_rest_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RESTBRIDGE_H */
