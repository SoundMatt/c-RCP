//cfusa:req REQ-ZONE-001
//cfusa:req REQ-ZONE-002
//cfusa:req REQ-ZONE-003
//cfusa:req REQ-ZONE-004
//cfusa:req REQ-ZONE-005
//cfusa:req REQ-ZONE-006
//cfusa:req REQ-ZONE-007
//cfusa:req REQ-ZONE-008
//cfusa:req REQ-PRI-001
//cfusa:req REQ-PRI-002
//cfusa:req REQ-PRI-003
//cfusa:req REQ-CMD-001
//cfusa:req REQ-CMD-002
//cfusa:req REQ-CMD-003
//cfusa:req REQ-CMD-004
//cfusa:req REQ-CMD-005
//cfusa:req REQ-CMD-006
//cfusa:req REQ-STATUS-001
//cfusa:req REQ-STATUS-002
//cfusa:req REQ-STATUS-003
//cfusa:req REQ-STATUS-004
//cfusa:req REQ-STATUS-005
//cfusa:req REQ-STATUS-006
//cfusa:req REQ-ERR-001
//cfusa:req REQ-ERR-002
//cfusa:req REQ-ERR-003
//cfusa:req REQ-ERR-004
//cfusa:req REQ-ERR-005
//cfusa:req REQ-ERR-006
//cfusa:req REQ-ERR-007
//cfusa:req REQ-ERR-008
//cfusa:req REQ-ERR-009
//cfusa:req REQ-ERR-010
//cfusa:req REQ-ERR-011
//cfusa:req REQ-CMDSTRUCT-001
//cfusa:req REQ-CMDSTRUCT-002
//cfusa:req REQ-RESP-001
//cfusa:req REQ-RESP-002
//cfusa:req REQ-RESP-003
//cfusa:req REQ-STAT-001
//cfusa:req REQ-STAT-002
//cfusa:req REQ-STAT-003
//cfusa:req REQ-STAT-004
//cfusa:req REQ-STAT-005
//cfusa:req REQ-CTRL-001
//cfusa:req REQ-CTRL-002
//cfusa:req REQ-CTRL-003
//cfusa:req REQ-CTRL-004
//cfusa:req REQ-CTRL-005
//cfusa:req REQ-CTRL-006
//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-008
//cfusa:req REQ-CTRL-009
//cfusa:req REQ-CTRL-010
//cfusa:req REQ-CTRL-011
//cfusa:req REQ-CTRL-012
//cfusa:req REQ-CTRL-013
//cfusa:req REQ-CTRL-014
//cfusa:req REQ-CTRL-015
//cfusa:req REQ-CTRL-016
//cfusa:req REQ-CTRL-017
//cfusa:req REQ-CTRL-018
//cfusa:req REQ-CTRL-019
//cfusa:req REQ-CTRL-020
//cfusa:req REQ-CTRL-021
//cfusa:req REQ-CTRL-022
//cfusa:req REQ-CTRL-023
//cfusa:req REQ-CTRL-024
//cfusa:req REQ-CTRL-025
//cfusa:req REQ-CTRL-026
//cfusa:req REQ-CTRL-027
//cfusa:req REQ-REG-001
//cfusa:req REQ-REG-002
//cfusa:req REQ-REG-003
//cfusa:req REQ-REG-004
//cfusa:req REQ-REG-005
//cfusa:req REQ-REG-006
//cfusa:req REQ-REG-007
//cfusa:req REQ-REG-008
//cfusa:req REQ-REG-009
//cfusa:req REQ-REG-010
//cfusa:req REQ-REG-011
//cfusa:req REQ-REG-012
//cfusa:req REQ-REG-013
/*
 * rcp.h provides the Remote Control Protocol for automotive zonal architecture.
 *
 * A central high-performance computer uses a rcp_registry_t to discover zone
 * controllers, then dispatches rcp_command_t values to each rcp_controller_t
 * and receives rcp_response_t and rcp_status_t telemetry in return.
 *
 * Controller and Registry are vtable-based interfaces (C has no virtual
 * classes): a concrete implementation embeds rcp_controller_t/rcp_registry_t
 * as the first member of its own struct and fills in the vtable. See mock.h
 * for the reference (in-process, zero-I/O) implementation.
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

/* Opaque forward declaration: the full definition (and the loaning
 * extension's public API) lives in loan.h, which includes this header —
 * only a pointer to it is needed here, in the optional loan/send_loaned
 * vtable slots below. */
typedef struct rcp_loan rcp_loan_t;

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_OK                  = 0,
    RCP_ERR_CLOSED          = 1,
    RCP_ERR_NOT_FOUND       = 2,
    RCP_ERR_ALREADY_EXISTS  = 3,
    RCP_ERR_TIMEOUT         = 4,
    RCP_ERR_BUSY            = 5,
    RCP_ERR_ZONE_MISMATCH   = 6,
    /* A transport compiled without its optional backend (e.g. tls.h without
     * an OpenSSL build) returns this rather than silently falling back to
     * an insecure/unimplemented path. Mirrors cpp-RCP's use of the generic
     * std::errc::function_not_supported for the same stub contract. */
    RCP_ERR_NOT_SUPPORTED   = 7,
    /* Returned by the authz decorator (authz.h) when the caller's identity
     * is not permitted to send the given command type to the given zone. */
    RCP_ERR_FORBIDDEN       = 8,
} rcp_errc_t;

/* Human-readable message for an rcp_errc_t value. Never returns NULL. */
const char *rcp_strerror(rcp_errc_t e);

/* ── Zone ──────────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_ZONE_UNKNOWN     = 0,
    RCP_ZONE_FRONT_LEFT  = 1,
    RCP_ZONE_FRONT_RIGHT = 2,
    RCP_ZONE_REAR_LEFT   = 3,
    RCP_ZONE_REAR_RIGHT  = 4,
    RCP_ZONE_CENTRAL     = 5,
} rcp_zone_t;

/* Unique, non-empty, human-readable name for a zone. Never returns NULL. */
const char *rcp_zone_string(rcp_zone_t z);

/* ── Priority ──────────────────────────────────────────────────────────────── */

typedef enum {
    RCP_PRIORITY_NORMAL   = 0,
    RCP_PRIORITY_HIGH     = 1,
    RCP_PRIORITY_CRITICAL = 2,
} rcp_priority_t;

/* ── CommandType ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_CMD_NOOP     = 0,
    RCP_CMD_SET      = 1,
    RCP_CMD_GET      = 2,
    RCP_CMD_RESET    = 3,
    RCP_CMD_WATCHDOG = 4,
    RCP_CMD_SLEEP    = 5,
    RCP_CMD_WAKE     = 6,
} rcp_command_type_t;

/* ── ResponseStatus ────────────────────────────────────────────────────────── */

typedef enum {
    RCP_RESPONSE_OK      = 0,
    RCP_RESPONSE_ERROR   = 1,
    RCP_RESPONSE_TIMEOUT = 2,
    RCP_RESPONSE_BUSY    = 3,
    RCP_RESPONSE_UNKNOWN = 4,
} rcp_response_status_t;

/* Unique, non-empty, human-readable name for a response status. Never returns NULL. */
const char *rcp_response_status_string(rcp_response_status_t s);

/* ── Byte buffers ──────────────────────────────────────────────────────────── */

/* A simple owned-or-borrowed byte buffer. Ownership is documented per field
 * that holds one (see rcp_command_t / rcp_response_t / rcp_status_t below);
 * there is no reference counting or implicit copy-on-write. */
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

/* ── Data structures ───────────────────────────────────────────────────────── */

/* rcp_command_t.payload is borrowed: the caller retains ownership and is
 * responsible for its lifetime. Implementations that need to retain the
 * payload beyond the send() call (e.g. mock's handler dispatch) must copy it
 * with rcp_bytes_dup() themselves (REQ-CTRL-026). */
typedef struct {
    uint32_t            id;
    rcp_zone_t          zone;
    rcp_command_type_t  type;
    rcp_priority_t      priority;
    rcp_bytes_t         payload;
} rcp_command_t;

/* rcp_response_t.payload is owned by the response: the caller must call
 * rcp_response_free() when done with it. */
typedef struct {
    uint32_t               command_id;
    rcp_zone_t             zone;
    rcp_response_status_t  status;
    rcp_bytes_t            payload;
} rcp_response_t;

/* Frees r->payload and zeroes *r. */
void rcp_response_free(rcp_response_t *r);

/* rcp_status_t.payload is owned by the status: the caller must call
 * rcp_status_free() when done with it. */
typedef struct {
    rcp_zone_t  zone;
    uint32_t    seq;
    bool        healthy;
    rcp_bytes_t payload;
} rcp_status_t;

/* Frees s->payload and zeroes *s. */
void rcp_status_free(rcp_status_t *s);

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

/* ── StatusChannel — bounded queue of rcp_status_t ────────────────────────── *
 *
 * A concrete (non-generic) channel, since C has no templates and RCP only
 * ever channels rcp_status_t (relay::Channel<Status> in cpp-RCP's binding).
 * Reference-counted: created with refcount 1; rcp_controller_subscribe()
 * implementations return a new reference, callers release it when done.
 */
typedef struct rcp_status_channel rcp_status_channel_t;

rcp_status_channel_t *rcp_status_channel_new(size_t capacity);
rcp_status_channel_t *rcp_status_channel_retain(rcp_status_channel_t *ch);
void                  rcp_status_channel_release(rcp_status_channel_t *ch);

/* Deep-copies *st into the channel. Returns false if the channel is full or closed. */
bool rcp_status_channel_push(rcp_status_channel_t *ch, const rcp_status_t *st);

/* Blocks until an item is available or the channel closes. Returns false
 * (leaving *out untouched) if the channel closed with no item pending. */
bool rcp_status_channel_recv(rcp_status_channel_t *ch, rcp_status_t *out);

/* Non-blocking; returns false immediately if nothing is queued. */
bool rcp_status_channel_try_recv(rcp_status_channel_t *ch, rcp_status_t *out);

void rcp_status_channel_close(rcp_status_channel_t *ch);
bool rcp_status_channel_is_closed(rcp_status_channel_t *ch);

/* ── Controller — vtable-based interface ──────────────────────────────────── */

typedef struct rcp_controller rcp_controller_t;

typedef struct {
    rcp_zone_t (*zone)(rcp_controller_t *self);

    /* Dispatches a command and waits for the response.
     * Returns RCP_ERR_CLOSED if the controller has been closed.
     * Returns RCP_ERR_TIMEOUT if ctx is done before a response arrives.
     * Returns RCP_ERR_ZONE_MISMATCH if cmd->zone != zone(self). */
    int (*send)(rcp_controller_t *self, const rcp_context_t *ctx,
                const rcp_command_t *cmd, rcp_response_t *out);

    /* Returns (via *out) a new reference to a channel of periodic Status
     * updates. The channel is closed when ctx becomes done or the
     * controller closes. Returns RCP_ERR_CLOSED if already closed. */
    int (*subscribe)(rcp_controller_t *self, const rcp_context_t *ctx,
                      rcp_status_channel_t **out);

    /* Releases all resources. Safe to call multiple times. */
    int (*close)(rcp_controller_t *self);

    /* Frees the concrete implementation once its refcount reaches 0. Never
     * called directly; invoked by rcp_controller_release(). */
    void (*destroy)(rcp_controller_t *self);

    /* Optional — NULL for controllers that don't support loaning (most
     * don't). Mirrors cpp-RCP's LoaningController, a Controller subtype
     * with two extra methods; C has no subtyping, so capability is
     * signalled by a nullable vtable slot instead of a downcast. See
     * loan.h for the wrapper that implements these. */
    int (*loan)(rcp_controller_t *self, int size, rcp_loan_t **out);
    int (*send_loaned)(rcp_controller_t *self, const rcp_context_t *ctx,
                        rcp_command_t *cmd, rcp_response_t *out);
} rcp_controller_vtable_t;

/* Base "class": concrete implementations embed this as their first member
 * (e.g. `struct rcp_mock_controller { rcp_controller_t base; ...; }`) so a
 * `rcp_controller_t *` can be cast back to the concrete type. */
struct rcp_controller {
    const rcp_controller_vtable_t *vt;
    int                             refcount;
};

rcp_controller_t *rcp_controller_retain(rcp_controller_t *c);
void              rcp_controller_release(rcp_controller_t *c);

static inline rcp_zone_t rcp_controller_zone(rcp_controller_t *c)
{
    return c->vt->zone(c);
}

static inline int rcp_controller_send(rcp_controller_t *c, const rcp_context_t *ctx,
                                       const rcp_command_t *cmd, rcp_response_t *out)
{
    return c->vt->send(c, ctx, cmd, out);
}

static inline int rcp_controller_subscribe(rcp_controller_t *c, const rcp_context_t *ctx,
                                            rcp_status_channel_t **out)
{
    return c->vt->subscribe(c, ctx, out);
}

static inline int rcp_controller_close(rcp_controller_t *c)
{
    return c->vt->close(c);
}

/* Both return RCP_ERR_NOT_SUPPORTED if c's vtable doesn't implement the
 * loaning extension (vt->loan / vt->send_loaned is NULL) — see loan.h. */
static inline int rcp_controller_loan(rcp_controller_t *c, int size, rcp_loan_t **out)
{
    if (!c->vt->loan) return RCP_ERR_NOT_SUPPORTED;
    return c->vt->loan(c, size, out);
}

static inline int rcp_controller_send_loaned(rcp_controller_t *c, const rcp_context_t *ctx,
                                              rcp_command_t *cmd, rcp_response_t *out)
{
    if (!c->vt->send_loaned) return RCP_ERR_NOT_SUPPORTED;
    return c->vt->send_loaned(c, ctx, cmd, out);
}

/* ── Registry — vtable-based interface ────────────────────────────────────── */

typedef struct rcp_registry rcp_registry_t;

typedef struct {
    /* Adds a controller. Takes a reference (calls rcp_controller_retain).
     * Returns RCP_ERR_ALREADY_EXISTS if a controller for the same zone is
     * already registered. Returns RCP_ERR_CLOSED if the registry is closed. */
    int (*register_ctrl)(rcp_registry_t *self, rcp_controller_t *ctrl);

    /* Removes and closes the controller for the given zone.
     * Returns RCP_ERR_NOT_FOUND if the zone is not registered. */
    int (*deregister)(rcp_registry_t *self, rcp_zone_t zone);

    /* Returns (via *out) a new reference to the controller for the given
     * zone. Returns RCP_ERR_NOT_FOUND if unregistered, RCP_ERR_CLOSED if
     * the registry itself is closed. */
    int (*lookup)(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out);

    /* Fills out[0..min(count,cap)) with new references to every registered
     * controller and returns the total count currently registered (which
     * may exceed cap; callers needing all of them should re-call with a
     * larger buffer sized to the returned count). */
    size_t (*controllers)(rcp_registry_t *self, rcp_controller_t **out, size_t cap);

    /* Closes all registered controllers and releases registry resources.
     * Safe to call multiple times. */
    int (*close)(rcp_registry_t *self);

    /* Frees the concrete implementation. Never called directly; invoked by
     * rcp_registry_destroy(). */
    void (*destroy)(rcp_registry_t *self);
} rcp_registry_vtable_t;

struct rcp_registry {
    const rcp_registry_vtable_t *vt;
};

static inline int rcp_registry_register(rcp_registry_t *r, rcp_controller_t *ctrl)
{
    return r->vt->register_ctrl(r, ctrl);
}

static inline int rcp_registry_deregister(rcp_registry_t *r, rcp_zone_t zone)
{
    return r->vt->deregister(r, zone);
}

static inline int rcp_registry_lookup(rcp_registry_t *r, rcp_zone_t zone, rcp_controller_t **out)
{
    return r->vt->lookup(r, zone, out);
}

static inline size_t rcp_registry_controllers(rcp_registry_t *r, rcp_controller_t **out, size_t cap)
{
    return r->vt->controllers(r, out, cap);
}

static inline int rcp_registry_close(rcp_registry_t *r)
{
    return r->vt->close(r);
}

/* Frees the registry itself. Call after rcp_registry_close(). Not the same
 * as close(): close() releases the registry's own controller references and
 * may be called multiple times; destroy() frees the registry object once. */
void rcp_registry_destroy(rcp_registry_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RCP_H */
