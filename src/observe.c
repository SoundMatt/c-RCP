/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/observe.h"
#include "rcp/alloc.h"

#include "alloc_overflow.h"
#include "platform.h"

#include <stdlib.h>

//cfusa:req REQ-OBS-013
uint64_t rcp_span_duration_ms(const rcp_span_t *span)
{
    return span->end_ms - span->start_ms;
}

/* ── Noop sink ─────────────────────────────────────────────────────────────── */

//cfusa:req REQ-OBS-005
static void noop_record_span(const rcp_span_t *span, void *ctx) { (void)span; (void)ctx; }
//cfusa:req REQ-OBS-012
//cfusa:req REQ-OBS-020
static void noop_record_gauge(const rcp_metric_t *metric, void *ctx) { (void)metric; (void)ctx; }
//cfusa:req REQ-OBS-021
static void noop_record_counter(const char *name, rcp_avtp_addr_t addr, double delta, void *ctx)
{
    (void)name; (void)addr; (void)delta; (void)ctx;
}

static const rcp_metrics_sink_vtable_t noop_sink_vtable = {
    noop_record_span,
    noop_record_gauge,
    noop_record_counter,
};

//cfusa:req REQ-OBS-014
rcp_metrics_sink_t rcp_noop_metrics_sink(void)
{
    rcp_metrics_sink_t sink;
    sink.vt  = &noop_sink_vtable;
    sink.ctx = NULL;
    return sink;
}

/* ── InMemorySink ──────────────────────────────────────────────────────────── */

struct rcp_in_memory_sink {
    rcp_mutex_t   mu;
    rcp_span_t   *spans;
    size_t         len;
    size_t         cap;
};

//cfusa:req REQ-OBS-015
rcp_in_memory_sink_t *rcp_in_memory_sink_new(void)
{
    rcp_in_memory_sink_t *s = (rcp_in_memory_sink_t *)rcp_calloc(1, sizeof(*s));
    if (!s) return NULL;
    rcp_mutex_init(&s->mu);
    return s;
}

//cfusa:req REQ-OBS-002
//cfusa:req REQ-OBS-006
static void in_memory_record_span(const rcp_span_t *span, void *ctx)
{
    rcp_in_memory_sink_t *s = (rcp_in_memory_sink_t *)ctx;

    rcp_mutex_lock(&s->mu);
    if (s->len == s->cap) {
        size_t new_cap = (s->cap == 0) ? 16 : s->cap * 2;
        size_t alloc_bytes = rcp_alloc_checked_size(new_cap, sizeof(*s->spans));
        rcp_span_t *grown = alloc_bytes == 0
            ? NULL
            : (rcp_span_t *)rcp_realloc(s->spans, alloc_bytes);
        if (grown) {
            s->spans = grown;
            s->cap   = new_cap;
        }
    }
    if (s->len < s->cap) {
        s->spans[s->len] = *span;
        s->len++;
    }
    rcp_mutex_unlock(&s->mu);
}

//cfusa:req REQ-OBS-012
static void in_memory_record_gauge(const rcp_metric_t *metric, void *ctx) { (void)metric; (void)ctx; }
static void in_memory_record_counter(const char *name, rcp_avtp_addr_t addr, double delta, void *ctx)
{
    (void)name; (void)addr; (void)delta; (void)ctx;
}

static const rcp_metrics_sink_vtable_t in_memory_sink_vtable = {
    in_memory_record_span,
    in_memory_record_gauge,
    in_memory_record_counter,
};

//cfusa:req REQ-OBS-016
rcp_metrics_sink_t rcp_in_memory_sink_as_sink(rcp_in_memory_sink_t *s)
{
    rcp_metrics_sink_t sink;
    sink.vt  = &in_memory_sink_vtable;
    sink.ctx = s;
    return sink;
}

//cfusa:req REQ-OBS-017
size_t rcp_in_memory_sink_span_count(rcp_in_memory_sink_t *s)
{
    size_t n;
    rcp_mutex_lock(&s->mu);
    n = s->len;
    rcp_mutex_unlock(&s->mu);
    return n;
}

//cfusa:req REQ-OBS-018
size_t rcp_in_memory_sink_spans(rcp_in_memory_sink_t *s, rcp_span_t *out, size_t cap)
{
    size_t i, n;
    rcp_mutex_lock(&s->mu);
    n = s->len;
    for (i = 0; i < n && i < cap; i++) out[i] = s->spans[i];
    rcp_mutex_unlock(&s->mu);
    return n;
}

//cfusa:req REQ-OBS-019
//cfusa:req REQ-OBS-022
void rcp_in_memory_sink_destroy(rcp_in_memory_sink_t *s)
{
    if (!s) return;
    rcp_mutex_destroy(&s->mu);
    rcp_free(s->spans);
    s->spans = NULL;
    rcp_free(s);
    s = NULL;
}

/* ── Recording a request ──────────────────────────────────────────────────── */

//cfusa:req REQ-OBS-001
//cfusa:req REQ-OBS-003
//cfusa:req REQ-OBS-004
//cfusa:req REQ-OBS-007
//cfusa:req REQ-OBS-008
//cfusa:req REQ-OBS-009
//cfusa:req REQ-OBS-010
//cfusa:req REQ-OBS-011
void rcp_observe_record(rcp_metrics_sink_t sink, const char *name,
                         rcp_avtp_addr_t addr, uint8_t request_type,
                         uint64_t start_ms, uint64_t end_ms, int result)
{
    rcp_span_t span;

    span.name         = name;
    span.addr         = addr;
    span.request_type = request_type;
    span.start_ms     = start_ms;
    span.end_ms       = end_ms;
    span.result       = result;

    sink.vt->record_span(&span, sink.ctx);
    sink.vt->record_counter("rcp.requests.total", addr, 1.0, sink.ctx);
    if (result != RCP_OK) {
        sink.vt->record_counter("rcp.requests.errors", addr, 1.0, sink.ctx);
    }
}
