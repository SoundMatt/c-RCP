/*
 * OpenTelemetry-style observability: spans and counters.
 *
 * rcp_observe_controller_new() wraps any rcp_controller_t and records a
 * latency span around every send(). Metrics are exported via a
 * caller-supplied rcp_metrics_sink_t; rcp_noop_metrics_sink() is the
 * default (no side effects).
 *
 * rcp_span_t/rcp_metric_t are intentionally plain data so they can be
 * adapted to any observability backend (OTel gRPC, Prometheus, etc.)
 * without pulling in heavy dependencies.
 *
 * Deviation from cpp-RCP: span timestamps use rcp_monotonic_ms()
 * (millisecond resolution) rather than cpp-RCP's std::chrono::steady_clock
 * (sub-microsecond resolution) -- this project has no higher-resolution
 * clock primitive anywhere else, and every other module's timing (deadline
 * budgets, watchdog intervals, etc.) already operates at millisecond
 * granularity, so introducing one just for this module would be
 * inconsistent scope creep.
 */
#ifndef RCP_OBSERVE_H
#define RCP_OBSERVE_H

#include "rcp/rcp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char           *name;     /* a static string literal, e.g. "rcp.send"; never owned/copied */
    rcp_zone_t             zone;
    rcp_command_type_t     cmd_type;
    uint64_t                start_ms;
    uint64_t                end_ms;
    int                     result;  /* an rcp_errc_t: RCP_OK if the wrapped send() succeeded */
} rcp_span_t;

/* end_ms - start_ms; never negative since both are read from the same
 * monotonic clock in increasing order. */
uint64_t rcp_span_duration_ms(const rcp_span_t *span);

typedef struct {
    const char *name;
    double       value;
    rcp_zone_t   zone;
} rcp_metric_t;

/* User-implemented sink vtable. ctx is the opaque pointer supplied
 * alongside this vtable in a rcp_metrics_sink_t. */
typedef struct {
    void (*record_span)(const rcp_span_t *span, void *ctx);
    void (*record_gauge)(const rcp_metric_t *metric, void *ctx);
    void (*record_counter)(const char *name, rcp_zone_t zone, double delta, void *ctx);
} rcp_metrics_sink_vtable_t;

/* A (vtable, ctx) pair, passed by value. vt must not be NULL; ctx may be
 * NULL if the vtable's callbacks don't need it. The caller is responsible
 * for keeping whatever ctx points to alive for as long as any observing
 * controller holds this sink -- rcp_observe_controller_new() copies this
 * struct by value but does not take ownership of ctx (matching the
 * borrowed-callback convention already used by rcp_sim_handler_fn and
 * rcp_watchdog_health_fn elsewhere in this project). */
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
 * count), matching rcp_registry_controllers()'s convention. */
size_t rcp_in_memory_sink_spans(rcp_in_memory_sink_t *s, rcp_span_t *out, size_t cap);

void rcp_in_memory_sink_destroy(rcp_in_memory_sink_t *s);

/* ── ObservingController ───────────────────────────────────────────────────── */

/* Wraps inner (retains it) and records a span plus rcp.commands.total /
 * rcp.commands.errors counters (via sink) around every send(). sink is
 * copied by value (see rcp_metrics_sink_t's lifetime note above). Returned
 * with refcount 1; release with rcp_controller_release(), which also
 * releases this wrapper's reference to inner. */
rcp_controller_t *rcp_observe_controller_new(rcp_controller_t *inner, rcp_metrics_sink_t sink);

#ifdef __cplusplus
}
#endif

#endif /* RCP_OBSERVE_H */
