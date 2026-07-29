#include "rcp/recorder.h"

#include "platform.h"

#include <rcp/clock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rcp_recorder {
    rcp_mutex_t            mu; /* protects entries[] */
    rcp_recorder_entry_t  *entries;
    size_t                  len;
    size_t                  cap;
};

rcp_recorder_t *rcp_recorder_new(void)
{
    rcp_recorder_t *r = (rcp_recorder_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    rcp_mutex_init(&r->mu);
    return r;
}

//cfusa:req REQ-REC-011
void rcp_recorder_destroy(rcp_recorder_t *r)
{
    size_t i;

    if (!r) return;
    for (i = 0; i < r->len; i++) rcp_bytes_free(&r->entries[i].frame);
    rcp_mutex_destroy(&r->mu);
    free(r->entries);
    free(r);
}

size_t rcp_recorder_size(rcp_recorder_t *r)
{
    size_t n;
    rcp_mutex_lock(&r->mu);
    n = r->len;
    rcp_mutex_unlock(&r->mu);
    return n;
}

//cfusa:req REQ-REC-009
size_t rcp_recorder_entries(rcp_recorder_t *r, rcp_recorder_entry_t *out, size_t cap)
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
//cfusa:req REQ-REC-007
//cfusa:req REQ-REC-008
bool rcp_recorder_capture(rcp_recorder_t *r, uint64_t timestamp_ms, rcp_avtp_addr_t addr,
                           bool inbound, const uint8_t *frame, size_t frame_len)
{
    rcp_recorder_entry_t e;
    bool ok = true;

    e.timestamp_ms = timestamp_ms;
    e.addr         = addr;
    e.inbound      = inbound;
    e.frame        = rcp_bytes_dup(frame, frame_len);
    if (frame_len > 0 && !e.frame.data) return false; /* rcp_bytes_dup() allocation failure */

    rcp_mutex_lock(&r->mu);
    if (r->len == r->cap) {
        size_t new_cap = (r->cap == 0) ? 16 : r->cap * 2;
        rcp_recorder_entry_t *grown = (rcp_recorder_entry_t *)realloc(r->entries, new_cap * sizeof(*grown));
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

    if (!ok) rcp_bytes_free(&e.frame);
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
//cfusa:req REQ-REC-010
int rcp_recorder_write_binary(rcp_recorder_t *r, const char *path)
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
        const rcp_recorder_entry_t *e = &r->entries[i];
        uint64_t ts        = e->timestamp_ms;
        uint64_t stream_id = rcp_stream_id_to_u64(e->addr.stream_id);
        uint8_t  byte_bus  = e->addr.byte_bus_id;
        uint8_t  inbound   = e->inbound ? 1u : 0u;
        uint32_t flen      = (uint32_t)e->frame.len;

        ok = write_field(f, &ts, sizeof(ts))
          && write_field(f, &stream_id, sizeof(stream_id))
          && write_field(f, &byte_bus, sizeof(byte_bus))
          && write_field(f, &inbound, sizeof(inbound))
          && write_field(f, &flen, sizeof(flen))
          && write_bytes(f, e->frame.data, flen);
    }

    if (fclose(f) != 0) ok = false;
    rcp_mutex_unlock(&r->mu);
    return ok ? RCP_OK : RCP_ERR_BUSY;
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
int rcp_playback_run_all(rcp_recorder_t *rec, rcp_playback_deliver_fn deliver, void *user_data,
                          rcp_playback_config_t cfg)
{
    size_t n = rcp_recorder_size(rec);
    rcp_recorder_entry_t *snapshot;
    size_t i;
    uint64_t prev_ts;

    if (n == 0) return RCP_OK;

    snapshot = (rcp_recorder_entry_t *)malloc(n * sizeof(*snapshot));
    if (!snapshot) return RCP_ERR_BUSY;
    n = rcp_recorder_entries(rec, snapshot, n);

    prev_ts = n > 0 ? snapshot[0].timestamp_ms : 0;
    for (i = 0; i < n; i++) {
        if (snapshot[i].timestamp_ms > prev_ts && cfg.speed_factor > 0.0) {
            uint64_t gap_ms = snapshot[i].timestamp_ms - prev_ts;
            uint64_t delay_ms = (uint64_t)((double)gap_ms / cfg.speed_factor);
            if (delay_ms > 1) rcp_sleep_ms((unsigned)delay_ms);
        }
        prev_ts = snapshot[i].timestamp_ms;

        deliver(&snapshot[i], user_data);
    }

    free(snapshot);
    return RCP_OK;
}
