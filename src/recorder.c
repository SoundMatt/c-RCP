#include "rcp/record.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rcp_record {
    rcp_mutex_t          mu; /* protects entries[] */
    rcp_record_entry_t *entries;
    size_t                len;
    size_t                cap;
};

rcp_record_t *rcp_record_new(void)
{
    rcp_record_t *r = (rcp_record_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    rcp_mutex_init(&r->mu);
    return r;
}

void rcp_record_destroy(rcp_record_t *r)
{
    size_t i;

    if (!r) return;
    for (i = 0; i < r->len; i++) {
        rcp_bytes_free(&r->entries[i].cmd.payload);
        rcp_bytes_free(&r->entries[i].resp.payload);
    }
    rcp_mutex_destroy(&r->mu);
    free(r->entries);
    free(r);
}

size_t rcp_record_size(rcp_record_t *r)
{
    size_t n;
    rcp_mutex_lock(&r->mu);
    n = r->len;
    rcp_mutex_unlock(&r->mu);
    return n;
}

size_t rcp_record_entries(rcp_record_t *r, rcp_record_entry_t *out, size_t cap)
{
    size_t i, n;

    rcp_mutex_lock(&r->mu);
    n = r->len;
    for (i = 0; i < n && i < cap; i++) out[i] = r->entries[i];
    rcp_mutex_unlock(&r->mu);
    return n;
}

//cfusa:req REQ-REC-001
//cfusa:req REQ-REC-002
//cfusa:req REQ-REC-006
//cfusa:req REQ-REC-008
static bool record_append(rcp_record_t *r, uint64_t ts_ms, const rcp_command_t *cmd,
                           const rcp_response_t *resp, int error)
{
    rcp_record_entry_t e;
    bool ok = true;

    e.timestamp_ms = ts_ms;
    e.cmd          = *cmd;
    e.cmd.payload  = rcp_bytes_dup(cmd->payload.data, cmd->payload.len);
    e.resp         = *resp;
    e.resp.payload = rcp_bytes_dup(resp->payload.data, resp->payload.len);
    e.error        = error;

    rcp_mutex_lock(&r->mu);
    if (r->len == r->cap) {
        size_t new_cap = (r->cap == 0) ? 16 : r->cap * 2;
        rcp_record_entry_t *grown = (rcp_record_entry_t *)realloc(r->entries, new_cap * sizeof(*grown));
        if (!grown) {
            ok = false;
        } else {
            r->entries = grown;
            r->cap     = new_cap;
        }
    }
    if (ok) {
        r->entries[r->len] = e;
        r->len++;
    }
    rcp_mutex_unlock(&r->mu);

    if (!ok) {
        rcp_bytes_free(&e.cmd.payload);
        rcp_bytes_free(&e.resp.payload);
    }
    return ok;
}

/* Writes exactly one element of size `size` bytes; returns false (leaving
 * the stream's error indicator set) if the underlying fwrite() wrote
 * fewer than one full element -- CERT-C ERR33-C requires checking this
 * return value rather than assuming disk I/O always succeeds. */
static bool write_field(FILE *f, const void *data, size_t size)
{
    return fwrite(data, size, 1, f) == 1;
}

static bool write_bytes(FILE *f, const void *data, size_t len)
{
    return len == 0 || fwrite(data, 1, len, f) == len;
}

//cfusa:req REQ-REC-003
int rcp_record_write_binary(rcp_record_t *r, const char *path)
{
    FILE *f;
    size_t i;
    bool ok = true;

    rcp_mutex_lock(&r->mu);

    f = fopen(path, "wb");
    if (!f) {
        rcp_mutex_unlock(&r->mu);
        return RCP_ERR_BUSY;
    }

    for (i = 0; i < r->len && ok; i++) {
        const rcp_record_entry_t *e = &r->entries[i];
        uint64_t ts     = e->timestamp_ms;
        uint16_t type   = (uint16_t)e->cmd.type;
        uint8_t  zone   = (uint8_t)e->cmd.zone;
        uint8_t  prio   = (uint8_t)e->cmd.priority;
        uint32_t clen   = (uint32_t)e->cmd.payload.len;
        uint8_t  status = (uint8_t)e->resp.status;
        uint32_t rlen   = (uint32_t)e->resp.payload.len;

        ok = write_field(f, &ts, sizeof(ts))
          && write_field(f, &type, sizeof(type))
          && write_field(f, &zone, sizeof(zone))
          && write_field(f, &prio, sizeof(prio))
          && write_field(f, &clen, sizeof(clen))
          && write_bytes(f, e->cmd.payload.data, clen)
          && write_field(f, &status, sizeof(status))
          && write_field(f, &rlen, sizeof(rlen))
          && write_bytes(f, e->resp.payload.data, rlen);
    }

    if (fclose(f) != 0) ok = false;
    rcp_mutex_unlock(&r->mu);
    return ok ? RCP_OK : RCP_ERR_BUSY;
}

/* ── RecordingController ───────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t  base;
    rcp_controller_t *inner; /* retained */
    rcp_record_t      *rec;   /* borrowed; must outlive this controller, see record.h */
} record_controller_t;

//cfusa:req REQ-REC-009
static rcp_zone_t record_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((record_controller_t *)self)->inner);
}

//cfusa:req REQ-REC-007
static int record_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                             const rcp_command_t *cmd, rcp_response_t *out)
{
    record_controller_t *rc = (record_controller_t *)self;
    int ec = rcp_controller_send(rc->inner, ctx, cmd, out);

    record_append(rc->rec, rcp_monotonic_ms(), cmd, out, ec);
    return ec;
}

//cfusa:req REQ-REC-010
static int record_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    record_controller_t *rc = (record_controller_t *)self;
    return rcp_controller_subscribe(rc->inner, ctx, out);
}

//cfusa:req REQ-REC-011
static int record_ctrl_close(rcp_controller_t *self)
{
    record_controller_t *rc = (record_controller_t *)self;
    return rcp_controller_close(rc->inner);
}

static void record_ctrl_destroy(rcp_controller_t *self)
{
    record_controller_t *rc = (record_controller_t *)self;
    rcp_controller_release(rc->inner);
    free(rc);
}

static const rcp_controller_vtable_t record_controller_vtable = {
    record_ctrl_zone,
    record_ctrl_send,
    record_ctrl_subscribe,
    record_ctrl_close,
    record_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_record_controller_new(rcp_controller_t *inner, rcp_record_t *rec)
{
    record_controller_t *rc = (record_controller_t *)calloc(1, sizeof(*rc));
    if (!rc) return NULL;
    rc->base.vt       = &record_controller_vtable;
    rc->base.refcount = 1;
    rc->inner         = rcp_controller_retain(inner);
    rc->rec           = rec;
    return &rc->base;
}

/* ── Playback ──────────────────────────────────────────────────────────────── */

rcp_playback_config_t rcp_playback_default_config(void)
{
    rcp_playback_config_t c;
    c.speed_factor = 1.0;
    return c;
}

//cfusa:req REQ-REC-004
//cfusa:req REQ-REC-005
int rcp_playback_run_all(rcp_controller_t *target, rcp_record_t *rec, const rcp_context_t *ctx,
                          rcp_playback_config_t cfg)
{
    size_t n = rcp_record_size(rec);
    rcp_record_entry_t *snapshot;
    size_t i;
    uint64_t prev_ts;

    if (n == 0) return RCP_OK;

    snapshot = (rcp_record_entry_t *)malloc(n * sizeof(*snapshot));
    if (!snapshot) return RCP_ERR_BUSY;
    n = rcp_record_entries(rec, snapshot, n);

    prev_ts = n > 0 ? snapshot[0].timestamp_ms : 0;
    for (i = 0; i < n; i++) {
        rcp_response_t out = {0};

        if (snapshot[i].timestamp_ms > prev_ts && cfg.speed_factor > 0.0) {
            uint64_t gap_ms = snapshot[i].timestamp_ms - prev_ts;
            uint64_t delay_ms = (uint64_t)((double)gap_ms / cfg.speed_factor);
            if (delay_ms > 1) rcp_sleep_ms((unsigned)delay_ms);
        }
        prev_ts = snapshot[i].timestamp_ms;

        (void)rcp_controller_send(target, ctx, &snapshot[i].cmd, &out);
        rcp_response_free(&out);
    }

    free(snapshot);
    return RCP_OK;
}
