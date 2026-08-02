/* SPDX-License-Identifier: MPL-2.0 */
/*
 * rcp.h -- shared, protocol-agnostic primitives used across the whole
 * codebase: the base error sentinel enum, a borrowed/owned byte-buffer
 * type, and the rcp_context_t alias for relay_context_t.
 *
 * This header used to also define a bespoke pre-TC18 object model (Zone,
 * Command, Response, Status, Controller, Registry) describing a
 * central-HPC-to-zone-controller RPC surface. That model has been removed
 * (RELAY spec §15.5: RCP now means the OPEN Alliance TC18 Remote Control
 * Protocol, not the earlier RELAY-internal placeholder protocol; there is
 * no compatibility shim). The TC18 register-map/lifecycle/endpoint core
 * (regmap.h, lifecycle.h, ep_*.h, avtp.h, acf.h) is this project's actual
 * protocol implementation and has no dependency on any of what was
 * removed from this file.
 */
#ifndef RCP_RCP_H
#define RCP_RCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "relay/relay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RELAY spec version this package targets (§17 requirement 12, §19.4).
 * A distinct rcp-namespaced symbol re-exporting relay.h's own constant,
 * mirroring go-RCP's two-symbol pattern (rcp.SpecVersion aliasing
 * relay.SpecVersion) rather than requiring callers to reach into relay.h
 * directly for an RCP-specific answer. */
//cfusa:req REQ-RELAY-013
#define RCP_SPEC_VERSION RELAY_SPEC_VERSION

/* Opaque forward declaration: the full definition (and the loaning
 * extension's public API) lives in loan.h, which includes this header —
 * only a pointer to it is needed here. */
typedef struct rcp_loan rcp_loan_t;

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-ERR-001
//cfusa:req REQ-ERR-002
//cfusa:req REQ-ERR-003
//cfusa:req REQ-ERR-004
//cfusa:req REQ-ERR-005
//cfusa:req REQ-ERR-006
//cfusa:req REQ-ERR-010
typedef enum {
    RCP_OK                  = 0,
    RCP_ERR_CLOSED          = 1,
    RCP_ERR_NOT_FOUND       = 2,
    RCP_ERR_ALREADY_EXISTS  = 3,
    RCP_ERR_TIMEOUT         = 4,
    RCP_ERR_BUSY            = 5,
    /* A transport compiled without its optional backend returns this
     * rather than silently falling back to an insecure/unimplemented
     * path. Mirrors cpp-RCP's use of the generic
     * std::errc::function_not_supported for the same stub contract. */
    RCP_ERR_NOT_SUPPORTED   = 7,
    /* Returned by the authz decorator (authz.h) when the caller's identity
     * is not permitted to send the given command type to the given zone. */
    RCP_ERR_FORBIDDEN       = 8,
} rcp_errc_t;

/* Human-readable message for an rcp_errc_t value. Never returns NULL. */
const char *rcp_strerror(rcp_errc_t e);

/* ── Byte buffers ──────────────────────────────────────────────────────────── */

/* A simple owned-or-borrowed byte buffer. Ownership is documented per
 * call site; there is no reference counting or implicit copy-on-write. */
typedef struct {
    uint8_t *data; /* NULL iff len == 0 */
    size_t   len;
} rcp_bytes_t;

/* Heap-allocates a copy of [data, data+len). Returns a zeroed rcp_bytes_t
 * (data=NULL, len=0) if len==0 or allocation fails. */
rcp_bytes_t rcp_bytes_dup(const uint8_t *data, size_t len);

/* Frees b->data (if any) and zeroes *b. Safe to call on an already-zeroed
 * or NULL-data buffer. */
void rcp_bytes_free(rcp_bytes_t *b);

/* ── Context — relay_context_t alias (§18.2) ──────────────────────────────── */

typedef relay_context_t rcp_context_t;

static inline rcp_context_t rcp_context_background(void)
{
    return relay_context_background();
}

static inline rcp_context_t rcp_context_with_timeout_ms(uint64_t timeout_ms)
{
    return relay_context_with_timeout_ms(timeout_ms);
}

static inline rcp_context_t rcp_context_with_deadline_ms(uint64_t deadline_ms)
{
    return relay_context_with_deadline_ms(deadline_ms);
}

static inline bool rcp_context_done(const rcp_context_t *ctx)
{
    return relay_context_done(ctx);
}

#ifdef __cplusplus
}
#endif

#endif /* RCP_RCP_H */
