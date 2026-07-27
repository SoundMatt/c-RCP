#include "rcp/observe.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdlib.h>

uint64_t rcp_span_duration_ms(const rcp_span_t *span)
{
    return span->end_ms - span->start_ms;
}

/* ── Noop sink ─────────────────────────────────────────────────────────────── */

static void noop_record_span(const rcp_span_t *span, void *ctx) { (void)span; (void)ctx; }
static void noop_record_gauge(const rcp_metric_t *metric, void *ctx) { (void)metric; (void)ctx; }
static void noop_record_counter(const char *name, rcp_zone_t zone, double delta, void *ctx)
{
    (void)name; (void)zone; (void)delta; (void)ctx;
}

static const rcp_metrics_sink_vtable_t noop_sink_vtable = {
    noop_record_span,
    noop_record_gauge,
    noop_record_counter,
};

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

rcp_in_memory_sink_t *rcp_in_memory_sink_new(void)
{
    rcp_in_memory_sink_t *s = (rcp_in_memory_sink_t *)calloc(1, sizeof(*s));
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
        rcp_span_t *grown = (rcp_span_t *)realloc(s->spans, new_cap * sizeof(*grown));
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

static void in_memory_record_gauge(const rcp_metric_t *metric, void *ctx) { (void)metric; (void)ctx; }
static void in_memory_record_counter(const char *name, rcp_zone_t zone, double delta, void *ctx)
{
    (void)name; (void)zone; (void)delta; (void)ctx;
}

static const rcp_metrics_sink_vtable_t in_memory_sink_vtable = {
    in_memory_record_span,
    in_memory_record_gauge,
    in_memory_record_counter,
};

rcp_metrics_sink_t rcp_in_memory_sink_as_sink(rcp_in_memory_sink_t *s)
{
    rcp_metrics_sink_t sink;
    sink.vt  = &in_memory_sink_vtable;
    sink.ctx = s;
    return sink;
}

size_t rcp_in_memory_sink_span_count(rcp_in_memory_sink_t *s)
{
    size_t n;
    rcp_mutex_lock(&s->mu);
    n = s->len;
    rcp_mutex_unlock(&s->mu);
    return n;
}

size_t rcp_in_memory_sink_spans(rcp_in_memory_sink_t *s, rcp_span_t *out, size_t cap)
{
    size_t i, n;
    rcp_mutex_lock(&s->mu);
    n = s->len;
    for (i = 0; i < n && i < cap; i++) out[i] = s->spans[i];
    rcp_mutex_unlock(&s->mu);
    return n;
}

void rcp_in_memory_sink_destroy(rcp_in_memory_sink_t *s)
{
    if (!s) return;
    rcp_mutex_destroy(&s->mu);
    free(s->spans);
    free(s);
}

/* ── ObservingController ───────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t   base;
    rcp_controller_t  *inner; /* retained */
    rcp_metrics_sink_t  sink;
} observe_controller_t;

//cfusa:req REQ-OBS-009
static rcp_zone_t observe_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((observe_controller_t *)self)->inner);
}

//cfusa:req REQ-OBS-001
//cfusa:req REQ-OBS-003
//cfusa:req REQ-OBS-004
//cfusa:req REQ-OBS-005
//cfusa:req REQ-OBS-007
//cfusa:req REQ-OBS-008
static int observe_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                              const rcp_command_t *cmd, rcp_response_t *out)
{
    observe_controller_t *oc = (observe_controller_t *)self;
    rcp_span_t span;
    int ec;

    span.name     = "rcp.send";
    span.zone     = cmd->zone;
    span.cmd_type = cmd->type;
    span.start_ms = rcp_monotonic_ms();

    ec = rcp_controller_send(oc->inner, ctx, cmd, out);

    span.end_ms = rcp_monotonic_ms();
    span.result = ec;

    oc->sink.vt->record_span(&span, oc->sink.ctx);
    oc->sink.vt->record_counter("rcp.commands.total", cmd->zone, 1.0, oc->sink.ctx);
    if (ec != RCP_OK) {
        oc->sink.vt->record_counter("rcp.commands.errors", cmd->zone, 1.0, oc->sink.ctx);
    }

    return ec;
}

//cfusa:req REQ-OBS-010
static int observe_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    observe_controller_t *oc = (observe_controller_t *)self;
    return rcp_controller_subscribe(oc->inner, ctx, out);
}

//cfusa:req REQ-OBS-011
static int observe_ctrl_close(rcp_controller_t *self)
{
    observe_controller_t *oc = (observe_controller_t *)self;
    return rcp_controller_close(oc->inner);
}

static void observe_ctrl_destroy(rcp_controller_t *self)
{
    observe_controller_t *oc = (observe_controller_t *)self;
    rcp_controller_release(oc->inner);
    free(oc);
}

static const rcp_controller_vtable_t observe_controller_vtable = {
    observe_ctrl_zone,
    observe_ctrl_send,
    observe_ctrl_subscribe,
    observe_ctrl_close,
    observe_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_observe_controller_new(rcp_controller_t *inner, rcp_metrics_sink_t sink)
{
    observe_controller_t *oc = (observe_controller_t *)calloc(1, sizeof(*oc));
    if (!oc) return NULL;
    oc->base.vt       = &observe_controller_vtable;
    oc->base.refcount = 1;
    oc->inner         = rcp_controller_retain(inner);
    oc->sink          = sink;
    return &oc->base;
}
