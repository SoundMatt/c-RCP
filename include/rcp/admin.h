/* SPDX-License-Identifier: MPL-2.0 */
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

/* [c-RCP-17] Fixed capacities for rcp_admin_server_t's internal endpoint
 * set, subscriber list, and counter table -- backs compile-time-sized
 * embedded arrays, not heap allocations growable without bound. None of
 * the three is TC18 wire-bounded (this module is RELAY-envelope/admin
 * glue, not protocol data -- see the file header's own "ADAPT-class
 * rebind" note), so each is this module's own conventional choice:
 * RCP_ADMIN_MAX_ENDPOINTS matches mock.h's RCP_MOCK_MAX_ENDPOINTS and
 * regmap.h's RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES/RCP_REGMAP_HW_PIN_MAP_MAX_
 * ENTRIES precedent (64, a plausible real-device total-endpoint-count
 * ceiling -- this set genuinely tracks one entry per endpoint, the same
 * scale those constants already use). RCP_ADMIN_MAX_SUBSCRIBERS matches
 * watchdog.h's/deadline.h's/powerstate.h's own RCP_WATCHDOG_MAX_
 * CALLBACKS/RCP_DEADLINE_MAX_CALLBACKS/RCP_POWERSTATE_MAX_CALLBACKS
 * precedent (16, a conventional integrator-subscriber cap -- it counts
 * function pointers, not protocol entities). RCP_ADMIN_MAX_COUNTERS (256)
 * is new: a Prometheus-style counter is keyed on a caller-chosen (name,
 * labels) pair, so its cardinality scales with distinct metric series,
 * not endpoint count directly -- 256 gives headroom for several counters
 * per endpoint at RCP_ADMIN_MAX_ENDPOINTS' own scale without inventing an
 * unrelated number. See rcp_admin_server_register_endpoint()'s,
 * rcp_admin_server_subscribe()'s, and rcp_admin_server_record_counter()'s
 * own doc comments for the resulting capacity-exceeded failure modes. */
#define RCP_ADMIN_MAX_ENDPOINTS   ((size_t)64u)
#define RCP_ADMIN_MAX_SUBSCRIBERS ((size_t)16u)
#define RCP_ADMIN_MAX_COUNTERS    ((size_t)256u)

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
 * changing anything if srv already holds RCP_ADMIN_MAX_ENDPOINTS
 * endpoints (the endpoint set is a fixed-capacity embedded array, not a
 * heap allocation growable without bound -- see that constant's own doc
 * comment). */
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
 * rcp_admin_server_emit() call, in registration order. Returns false if
 * srv already holds RCP_ADMIN_MAX_SUBSCRIBERS subscribers (cb not added;
 * the subscriber list is a fixed-capacity embedded array, not a heap
 * allocation growable without bound -- see that constant's own doc
 * comment). */
bool rcp_admin_server_subscribe(rcp_admin_server_t *srv, rcp_admin_event_fn cb, void *user_data);

/* Invokes every registered subscriber with ev, in registration order. */
void rcp_admin_server_emit(rcp_admin_server_t *srv, rcp_admin_event_t ev);

/* Adds delta to the running total of the counter identified by (name,
 * labels) -- a distinct running total is kept per unique (name, labels)
 * pair. labels may be "" (no labels). Thread-safe. Returns false without
 * recording delta if (name, labels) is not already tracked and srv
 * already holds RCP_ADMIN_MAX_COUNTERS distinct counters (the counter
 * table is a fixed-capacity embedded array, not a heap allocation
 * growable without bound -- see that constant's own doc comment); an
 * already-tracked (name, labels) pair always succeeds regardless of how
 * many other counters exist. */
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
