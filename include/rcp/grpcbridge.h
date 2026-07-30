/* SPDX-License-Identifier: MPL-2.0 */
/*
 * grpcbridge.h -- gRPC protocol bridge interface stub (SG-006) for the
 * TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 21,
 * "Satellite Package Rework", milestone 81, "Protocol bridges").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: framing an RCP request
 * for an external gRPC endpoint is exactly as useful a job under TC18 as
 * it was before -- this module's config shape (rcp_grpc_config_t) and its
 * compile-time-stub nature are unchanged. What changes is only the call
 * shape it's framed around: the old rcp_grpc_controller_new() wrapped a
 * whole rcp_controller_t's send()/subscribe()/close() vtable, a choke
 * point that no longer exists (ROADMAP.md's Protocol Replacement Notice
 * retires rcp_controller_t's vtable along with rcp_zone_t/rcp_command_t/
 * rcp_response_t; Phase 16/19 instead built 13 heterogeneous,
 * independently-typed endpoint modules, each with its own encode/apply
 * function pairs). There is no single generic send() left to wrap, so
 * this module drops the controller wrapper entirely: rcp_grpc_bridge_send()
 * is now the whole entry point, called directly by the caller in place of
 * whichever endpoint-specific encode/send call it would otherwise be
 * driving, the same caller-driven, "sends no wire traffic and owns no
 * transport of its own until a backend is linked" shape milestone 80's
 * authz.c/faultinject.c/observe.c already established for this kind of
 * decorator.
 *
 * A request frame is identified the same way every other rebound module
 * in this codebase identifies one: avtp.h's rcp_avtp_addr_t (stream_id +
 * byte_bus_id) plus a caller-supplied, deliberately opaque request_type
 * byte this module never itself interprets (see authz.h's own file
 * header for the fuller rationale). payload/payload_len are the already-
 * encoded ACF request body the caller would otherwise hand to whichever
 * native ep_*.h/request_*.h send path applies -- this module's only job
 * is carrying that opaque byte string to/from the external gRPC endpoint
 * cfg describes, never decoding it.
 *
 * This is still a compile-time interface stub: no generated gRPC stub
 * (from an rcp.proto) is linked, so rcp_grpc_bridge_send() always returns
 * RCP_ERR_NOT_SUPPORTED rather than attempting an RPC over an
 * unconfigured channel, leaving *out_response untouched -- mirrors
 * cpp-RCP's own grpcbridge.hpp, which ships the same stub absent a linked
 * gRPC backend. Wiring in a real gRPC backend is future work (see
 * ROADMAP.md).
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_GRPCBRIDGE_H
#define RCP_GRPCBRIDGE_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stddef.h>
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

/* Bridges a single already-encoded RCP request (payload/payload_len,
 * exactly what the caller would otherwise hand to a native endpoint's own
 * send path) for the endpoint at addr to a gRPC remote endpoint described
 * by cfg. request_type is passed through opaque, unexamined, purely so a
 * real backend could one day route/label the call. On success, populates
 * *out_response with a freshly heap-allocated response the caller frees
 * with rcp_bytes_free(); on failure, *out_response is left untouched.
 * Currently always returns RCP_ERR_NOT_SUPPORTED (no gRPC backend
 * linked). */
int rcp_grpc_bridge_send(rcp_grpc_config_t cfg, rcp_avtp_addr_t addr,
                          uint8_t request_type,
                          const uint8_t *payload, size_t payload_len,
                          rcp_bytes_t *out_response);

#ifdef __cplusplus
}
#endif

#endif /* RCP_GRPCBRIDGE_H */
