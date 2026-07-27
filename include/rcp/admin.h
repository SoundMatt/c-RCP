/*
 * In-process Admin API: zone listing, SSE-style events, Prometheus metrics.
 *
 * rcp_admin_server_t is a lightweight in-process interface: callers can
 * query zone state, subscribe to events (SSE-style push channel), and
 * snapshot Prometheus-format text metrics. An actual HTTP binding is out
 * of scope; use a real HTTP server library to expose the HTTP surface.
 *
 * Deviation from cpp-RCP: cpp-RCP's emit() holds its mutex for the
 * duration of every subscriber callback invocation. This port invokes
 * callbacks outside the lock instead, matching the established convention
 * from watchdog.c/deadline.c/powerstate.c -- a subscriber that calls back
 * into the same server (subscribe()/emit()/record_counter()) cannot
 * deadlock.
 *
 * Event timestamps use rcp_monotonic_ms() rather than cpp-RCP's
 * std::chrono::system_clock (wall-clock time) -- this project has no
 * wall-clock primitive anywhere else.
 */
#ifndef RCP_ADMIN_H
#define RCP_ADMIN_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    rcp_zone_t zone;
    bool        registered;
    char         extra[128]; /* optional metadata blob (e.g. JSON), empty string if unused */
} rcp_zone_info_t;

typedef enum {
    RCP_ADMIN_EVT_ZONE_REGISTERED   = 1,
    RCP_ADMIN_EVT_ZONE_DEREGISTERED = 2,
    RCP_ADMIN_EVT_STATUS_UPDATE     = 3,
} rcp_admin_event_type_t;

typedef struct {
    rcp_admin_event_type_t type;
    rcp_zone_t               zone;
    uint64_t                  ts_ms; /* a rcp_monotonic_ms() timestamp */
} rcp_admin_event_t;

typedef void (*rcp_admin_event_fn)(const rcp_admin_event_t *ev, void *user_data);

typedef struct rcp_admin_server rcp_admin_server_t;

/* Creates an admin server over reg (a borrowed, non-owning reference --
 * reg must outlive srv, matching cpp-RCP's own AdminServer(Registry&)).
 * Returns NULL on allocation failure. */
rcp_admin_server_t *rcp_admin_server_new(rcp_registry_t *reg);

void rcp_admin_server_destroy(rcp_admin_server_t *srv);

/* Fills out[0..min(count,cap)) with a snapshot of every controller
 * currently registered on the wrapped registry (all marked registered =
 * true, extra = ""), and returns the total count (which may exceed cap;
 * callers needing all of them should re-call with a larger buffer sized
 * to the returned count), matching rcp_registry_controllers()'s
 * convention. */
size_t rcp_admin_server_zones(rcp_admin_server_t *srv, rcp_zone_info_t *out, size_t cap);

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
