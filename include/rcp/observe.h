/* SPDX-License-Identifier: MPL-2.0 */
/*
 * observe.h -- OpenTelemetry-style observability (spans and counters) for
 * the TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 21,
 * "Satellite Package Rework", milestone 80, "Generic decorators, batch 1").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: "record a latency span
 * plus total/error counters around every request" stays exactly the right
 * shape for TC18 -- only what a span/metric is labeled with changes, from
 * the retired rcp_zone_t/rcp_command_type_t pair to avtp.h's
 * rcp_avtp_addr_t (stream_id + byte_bus_id) plus a caller-supplied
 * request_type byte (see authz.h's file header for why request_type is
 * deliberately left an opaque, caller-classified label this module never
 * itself interprets -- the same reasoning applies here unchanged).
 *
 * There is no longer a single generic rcp_controller_t::send() choke
 * point to wrap automatically (ROADMAP.md's Protocol Replacement
 * Notice). This module drops the old ObservingController vtable wrapper
 * entirely: rcp_observe_record() is now the whole interception point,
 * called directly by the caller immediately after it finishes driving
 * whichever endpoint-specific encode/send call applies, passing the
 * start/end timestamps it measured itself (rcp_monotonic_ms(), clock.h)
 * around that call -- the same caller-driven, "sends no wire traffic and
 * owns no transport itself" shape milestone 79's watchdog.c/deadline.c/
 * powerstate.c already established.
 *
 * rcp_span_t/rcp_metric_t remain intentionally plain data so they can be
 * adapted to any observability backend (OTel gRPC, Prometheus, etc.)
 * without pulling in heavy dependencies. Span/counter names move from
 * "rcp.commands.*" to "rcp.requests.*", matching TC18's own "request" (not
 * "command") vocabulary (avtp.h's file header).
 *
 * Deviation from cpp-RCP, unchanged from this module's pre-rebind
 * version: span timestamps use rcp_monotonic_ms() (millisecond
 * resolution) rather than cpp-RCP's std::chrono::steady_clock
 * (sub-microsecond resolution) -- this project has no higher-resolution
 * clock primitive anywhere else, and every other module's timing already
 * operates at millisecond granularity.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_OBSERVE_H
#define RCP_OBSERVE_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char       *name;         /* a static string literal, e.g. "rcp.request"; never owned/copied */
    rcp_avtp_addr_t   addr;
    uint8_t            request_type;
    uint64_t            start_ms;
    uint64_t            end_ms;
    int                  result;    /* an rcp_errc_t: RCP_OK if the request this span covers succeeded */
} rcp_span_t;

/* end_ms - start_ms; never negative since both are read from the same
 * monotonic clock in increasing order. */
uint64_t rcp_span_duration_ms(const rcp_span_t *span);

typedef struct {
    const char       *name;
    double             value;
    rcp_avtp_addr_t   addr;
} rcp_metric_t;

/* User-implemented sink vtable. ctx is the opaque pointer supplied
 * alongside this vtable in a rcp_metrics_sink_t. */
typedef struct {
    void (*record_span)(const rcp_span_t *span, void *ctx);
    void (*record_gauge)(const rcp_metric_t *metric, void *ctx);
    void (*record_counter)(const char *name, rcp_avtp_addr_t addr, double delta, void *ctx);
} rcp_metrics_sink_vtable_t;

/* A (vtable, ctx) pair, passed by value. vt must not be NULL; ctx may be
 * NULL if the vtable's callbacks don't need it. The caller is responsible
 * for keeping whatever ctx points to alive for as long as any code holds
 * this sink -- rcp_observe_record() copies this struct by value but does
 * not take ownership of ctx (matching the borrowed-callback convention
 * already used by rcp_sim_handler_fn and rcp_watchdog_event_fn elsewhere
 * in this project). */
typedef struct {
    const rcp_metrics_sink_vtable_t *vt;
    void                            *ctx;
} rcp_metrics_sink_t;

/* A sink whose three callbacks are all no-ops. */
rcp_metrics_sink_t rcp_noop_metrics_sink(void);

/* ── InMemorySink: collects spans for test assertions ─────────────────────── */

typedef struct rcp_in_memory_sink rcp_in_memory_sink_t;

/* Returns NULL on allocation failure. */
rcp_in_memory_sink_t *rcp_in_memory_sink_new(void);

/* Returns a rcp_metrics_sink_t view over s (record_gauge/record_counter are
 * no-ops; only spans are collected, matching cpp-RCP's own InMemorySink). */
rcp_metrics_sink_t rcp_in_memory_sink_as_sink(rcp_in_memory_sink_t *s);

size_t rcp_in_memory_sink_span_count(rcp_in_memory_sink_t *s);

/* Fills out[0..min(count,cap)) with the spans recorded so far, in order,
 * and returns the total count (which may exceed cap; callers needing all
 * of them should re-call with a larger buffer sized to the returned
 * count). */
size_t rcp_in_memory_sink_spans(rcp_in_memory_sink_t *s, rcp_span_t *out, size_t cap);

void rcp_in_memory_sink_destroy(rcp_in_memory_sink_t *s);

/* ── Recording a request ──────────────────────────────────────────────────── */

/* Caller calls this once, immediately after finishing whichever
 * endpoint-specific request it just drove for (addr, request_type),
 * passing the start/end rcp_monotonic_ms() timestamps it measured around
 * that call and the resulting status code (result). Builds a rcp_span_t
 * named `name` and forwards it to sink.record_span(), then increments
 * sink's "rcp.requests.total" counter (and, iff result != RCP_OK,
 * "rcp.requests.errors" too) via sink.record_counter() -- the same
 * span-plus-counters shape the old ObservingController's send() wrapper
 * produced automatically, now driven explicitly by the caller (see file
 * header). sink is copied by value (see rcp_metrics_sink_t's lifetime
 * note above). */
void rcp_observe_record(rcp_metrics_sink_t sink, const char *name,
                         rcp_avtp_addr_t addr, uint8_t request_type,
                         uint64_t start_ms, uint64_t end_ms, int result);

#ifdef __cplusplus
}
#endif

#endif /* RCP_OBSERVE_H */
