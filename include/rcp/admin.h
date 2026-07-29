/*
 * admin.h -- In-process Admin API (endpoint listing, SSE-style events,
 * Prometheus metrics) for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 21, "Satellite Package Rework", milestone 80,
 * "Generic decorators, batch 1").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: the SSE-style event
 * push channel and Prometheus-text metrics snapshot were already
 * protocol-agnostic (neither ever read a zone or a command out of
 * rcp_command_t) and are unchanged here. The one piece that did reach
 * into the retired core is the listing surface: the old
 * rcp_admin_server_new(rcp_registry_t *reg) wrapped rcp.h's
 * rcp_registry_t and enumerated its registered rcp_controller_t
 * instances via rcp_registry_controllers(). rcp_registry_t has no TC18
 * counterpart -- there is no single generic registry of "every
 * controller" left to introspect (ROADMAP.md's Protocol Replacement
 * Notice; discovery.h's own native broadcast discovery, Phase 15, is the
 * closest analogue, but it is a wire-level mechanism a caller drives, not
 * an in-process object this module could borrow a reference to and poll).
 *
 * This module therefore drops the registry dependency entirely and
 * becomes caller-driven, the same shape milestone 79's watchdog.c/
 * deadline.c/powerstate.c already established: whatever code in an
 * application actually discovers or configures RC Server endpoints (via
 * discovery.h, a static manifest, or any other means) tells this module
 * directly via rcp_admin_server_register_endpoint()/
 * _deregister_endpoint(), and rcp_admin_server_endpoints() reports
 * exactly that caller-maintained membership back -- replacing
 * rcp_zone_info_t/rcp_admin_server_zones() with rcp_endpoint_info_t/
 * rcp_admin_server_endpoints(), keyed on avtp.h's rcp_avtp_addr_t
 * (stream_id + byte_bus_id) in place of the retired rcp_zone_t.
 *
 * Deviation from cpp-RCP, unchanged from this module's pre-rebind
 * version: cpp-RCP's emit() holds its mutex for the duration of every
 * subscriber callback invocation. This port invokes callbacks outside the
 * lock instead, matching the established convention from watchdog.c/
 * deadline.c/powerstate.c -- a subscriber that calls back into the same
 * server (subscribe()/emit()/record_counter()) cannot deadlock. Event
 * timestamps use rcp_monotonic_ms() rather than cpp-RCP's
 * std::chrono::system_clock (wall-clock time) -- this project has no
 * wall-clock primitive anywhere else.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_ADMIN_H
#define RCP_ADMIN_H

#include "rcp/avtp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    rcp_avtp_addr_t addr;
    bool             registered;
    char              extra[128]; /* optional metadata blob (e.g. JSON), empty string if unused */
} rcp_endpoint_info_t;

typedef enum {
    RCP_ADMIN_EVT_ENDPOINT_REGISTERED   = 1,
    RCP_ADMIN_EVT_ENDPOINT_DEREGISTERED = 2,
    RCP_ADMIN_EVT_STATUS_UPDATE         = 3,
} rcp_admin_event_type_t;

typedef struct {
    rcp_admin_event_type_t type;
    rcp_avtp_addr_t          addr;
    uint64_t                  ts_ms; /* a rcp_monotonic_ms() timestamp */
} rcp_admin_event_t;

typedef void (*rcp_admin_event_fn)(const rcp_admin_event_t *ev, void *user_data);

typedef struct rcp_admin_server rcp_admin_server_t;

/* Creates an admin server with no endpoints registered yet -- see
 * rcp_admin_server_register_endpoint(). Returns NULL on allocation
 * failure. */
rcp_admin_server_t *rcp_admin_server_new(void);

void rcp_admin_server_destroy(rcp_admin_server_t *srv);

/* Adds addr to srv's registered-endpoint set (extra = ""). Returns true
 * iff addr was not already registered (state changed); a redundant
 * registration of an already-registered addr is a no-op that returns
 * false. Does not itself call rcp_admin_server_emit() -- callers wanting
 * an RCP_ADMIN_EVT_ENDPOINT_REGISTERED event delivered to subscribers do
 * so themselves, the same caller-driven split rcp_admin_server_emit()
 * already uses for every other event type. Returns false without
 * changing anything on allocation failure. */
bool rcp_admin_server_register_endpoint(rcp_admin_server_t *srv, rcp_avtp_addr_t addr);

/* Removes addr from srv's registered-endpoint set. Returns true iff addr
 * was registered (state changed); false if it was not (no-op). */
bool rcp_admin_server_deregister_endpoint(rcp_admin_server_t *srv, rcp_avtp_addr_t addr);

/* Fills out[0..min(count,cap)) with a snapshot of every endpoint
 * currently registered on srv (all marked registered = true), and
 * returns the total count (which may exceed cap; callers needing all of
 * them should re-call with a larger buffer sized to the returned
 * count). */
size_t rcp_admin_server_endpoints(rcp_admin_server_t *srv, rcp_endpoint_info_t *out, size_t cap);

/* Registers cb to be invoked (with user_data) on every subsequent
 * rcp_admin_server_emit() call, in registration order. Returns false on
 * allocation failure (cb not added). */
bool rcp_admin_server_subscribe(rcp_admin_server_t *srv, rcp_admin_event_fn cb, void *user_data);

/* Invokes every registered subscriber with ev, in registration order. */
void rcp_admin_server_emit(rcp_admin_server_t *srv, rcp_admin_event_t ev);

/* Adds delta to the running total of the counter identified by (name,
 * labels) -- a distinct running total is kept per unique (name, labels)
 * pair. labels may be "" (no labels). Thread-safe. Returns false on
 * allocation failure (delta not recorded). */
bool rcp_admin_server_record_counter(rcp_admin_server_t *srv, const char *name, const char *labels, double delta);

/* Renders every recorded counter as Prometheus text-format lines
 * ("# TYPE <name> counter\n<name>{<labels>} <value>\n", omitting "{...}"
 * when labels is empty) into out, NUL-terminated if cap > 0. Returns the
 * total length that would be written if cap were unlimited (excluding the
 * NUL terminator), matching snprintf()'s return-value convention -- pass
 * out = NULL, cap = 0 to just measure the required buffer size. */
size_t rcp_admin_server_metrics_text(rcp_admin_server_t *srv, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ADMIN_H */
