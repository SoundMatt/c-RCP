/*
 * gRPC protocol bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no generated gRPC stub (from an
 * rcp.proto) is linked, so every RPC-facing call returns
 * RCP_ERR_NOT_SUPPORTED rather than attempting an RPC over an
 * unconfigured channel -- mirrors cpp-RCP's own grpcbridge.hpp, which
 * ships the same stub absent a linked gRPC backend. Wiring in a real gRPC
 * backend is future work (see ROADMAP.md).
 */
#ifndef RCP_GRPCBRIDGE_H
#define RCP_GRPCBRIDGE_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *server_address;  /* e.g. "localhost:50051" */
    int          max_retries;     /* default: 3 */
    uint64_t     rpc_timeout_ms;  /* default: 1000 */
} rcp_grpc_config_t;

/* { server_address = NULL, max_retries = 3, rpc_timeout_ms = 1000 }. */
rcp_grpc_config_t rcp_grpc_default_config(void);

/* Bridges RCP commands for zone to a gRPC remote endpoint described by
 * cfg. Every operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_grpc_controller_new(rcp_zone_t zone, rcp_grpc_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_GRPCBRIDGE_H */
