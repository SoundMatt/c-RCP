/*
 * Transparent zone proxy for cascaded zonal topologies.
 *
 * rcp_proxy_controller_new() forwards commands through an upstream proxy
 * hop. A latency budget is enforced at the proxy boundary: if the budget
 * is exceeded the command returns RCP_ERR_TIMEOUT rather than propagating
 * a slow response to the caller.
 *
 * A rcp_proxy_registry_t builds routes (zone -> upstream controller) and
 * exposes a standard rcp_registry_t interface.
 */
#ifndef RCP_PROXY_H
#define RCP_PROXY_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t latency_budget_ms; /* max added hop latency (default: 50) */
} rcp_proxy_config_t;

/* { latency_budget_ms = 50 }. */
rcp_proxy_config_t rcp_proxy_default_config(void);

/* Wraps upstream (retains it) and enforces cfg.latency_budget_ms on every
 * send(): derives a deadline of now + latency_budget_ms and uses it (or
 * the caller's own ctx deadline, if tighter) when forwarding. Returned
 * with refcount 1; release with rcp_controller_release(), which also
 * releases this wrapper's reference to upstream. */
rcp_controller_t *rcp_proxy_controller_new(rcp_controller_t *upstream, rcp_proxy_config_t cfg);

/* Creates an empty proxy registry (no routes). Release with
 * rcp_registry_close() then rcp_registry_destroy(). */
rcp_registry_t *rcp_proxy_registry_new(void);

/* Wraps upstream in a rcp_proxy_controller_new() (with cfg) and registers
 * it on reg under upstream's own zone. reg must have been created by
 * rcp_proxy_registry_new(). Returns RCP_ERR_ALREADY_EXISTS if that zone
 * already has a route, RCP_ERR_CLOSED if reg is closed. */
int rcp_proxy_registry_add_route(rcp_registry_t *reg, rcp_controller_t *upstream, rcp_proxy_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_PROXY_H */
