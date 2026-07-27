#include "rcp/rcp.h"

#include <stdlib.h>
#include <string.h>

#include "platform.h"

/* ── relay error strings ───────────────────────────────────────────────────── */

const char *relay_strerror(relay_errc_t e)
{
    switch (e) {
    case RELAY_ERRC_CLOSED:            return "relay: closed";
    case RELAY_ERRC_NOT_CONNECTED:     return "relay: not connected";
    case RELAY_ERRC_TIMEOUT:           return "relay: timeout";
    case RELAY_ERRC_PAYLOAD_TOO_LARGE: return "relay: payload too large";
    default:                           return "relay: unknown";
    }
}

/* ── rcp error strings ─────────────────────────────────────────────────────── */

const char *rcp_strerror(rcp_errc_t e)
{
    switch (e) {
    case RCP_OK:                 return "rcp: success";
    case RCP_ERR_CLOSED:         return "rcp: controller closed";
    case RCP_ERR_NOT_FOUND:      return "rcp: zone not found";
    case RCP_ERR_ALREADY_EXISTS: return "rcp: zone already registered";
    case RCP_ERR_TIMEOUT:        return "rcp: command timeout";
    case RCP_ERR_BUSY:           return "rcp: zone controller busy";
    case RCP_ERR_ZONE_MISMATCH:  return "rcp: zone mismatch";
    default:                     return "rcp: unknown error";
    }
}

/* ── Zone / status strings ─────────────────────────────────────────────────── */

//cfusa:req REQ-ZONE-001
const char *rcp_zone_string(rcp_zone_t z)
{
    switch (z) {
    case RCP_ZONE_FRONT_LEFT:  return "front-left";
    case RCP_ZONE_FRONT_RIGHT: return "front-right";
    case RCP_ZONE_REAR_LEFT:   return "rear-left";
    case RCP_ZONE_REAR_RIGHT:  return "rear-right";
    case RCP_ZONE_CENTRAL:     return "central";
    default:                   return "unknown";
    }
}

//cfusa:req REQ-STATUS-001
const char *rcp_response_status_string(rcp_response_status_t s)
{
    switch (s) {
    case RCP_RESPONSE_OK:      return "OK";
    case RCP_RESPONSE_ERROR:   return "error";
    case RCP_RESPONSE_TIMEOUT: return "timeout";
    case RCP_RESPONSE_BUSY:    return "busy";
    default:                   return "unknown";
    }
}

/* ── Byte buffers ──────────────────────────────────────────────────────────── */

//cfusa:req REQ-CTRL-026
//cfusa:req REQ-CTRL-027
//cfusa:req REQ-STAT-004
rcp_bytes_t rcp_bytes_dup(const uint8_t *data, size_t len)
{
    rcp_bytes_t b;
    b.data = NULL;
    b.len  = 0;
    if (len == 0) return b;

    b.data = (uint8_t *)malloc(len);
    if (!b.data) return b; /* len stays 0: allocation failure yields an empty buffer */

    memcpy(b.data, data, len);
    b.len = len;
    return b;
}

void rcp_bytes_free(rcp_bytes_t *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len  = 0;
}

void rcp_response_free(rcp_response_t *r)
{
    if (!r) return;
    rcp_bytes_free(&r->payload);
}

void rcp_status_free(rcp_status_t *s)
{
    if (!s) return;
    rcp_bytes_free(&s->payload);
}

/* ── StatusChannel ─────────────────────────────────────────────────────────── */

struct rcp_status_channel {
    rcp_mutex_t   mu;
    rcp_cond_t    cv;
    rcp_status_t *items; /* circular buffer, cap entries */
    size_t        head;
    size_t        count;
    size_t        cap;
    bool          closed;
    int           refcount;
};

rcp_status_channel_t *rcp_status_channel_new(size_t capacity)
{
    rcp_status_channel_t *ch;

    if (capacity == 0) capacity = 1;

    ch = (rcp_status_channel_t *)malloc(sizeof(rcp_status_channel_t));
    if (!ch) return NULL;

    ch->items = (rcp_status_t *)calloc(capacity, sizeof(rcp_status_t));
    if (!ch->items) {
        free(ch);
        return NULL;
    }

    rcp_mutex_init(&ch->mu);
    rcp_cond_init(&ch->cv);
    ch->head     = 0;
    ch->count    = 0;
    ch->cap      = capacity;
    ch->closed   = false;
    ch->refcount = 1;
    return ch;
}

rcp_status_channel_t *rcp_status_channel_retain(rcp_status_channel_t *ch)
{
    if (ch) rcp_atomic_inc(&ch->refcount);
    return ch;
}

void rcp_status_channel_release(rcp_status_channel_t *ch)
{
    size_t i;

    if (!ch) return;
    if (rcp_atomic_dec(&ch->refcount) != 0) return;

    for (i = 0; i < ch->count; i++) {
        rcp_status_free(&ch->items[(ch->head + i) % ch->cap]);
    }
    rcp_mutex_destroy(&ch->mu);
    rcp_cond_destroy(&ch->cv);
    free(ch->items);
    free(ch);
}

//cfusa:req REQ-CTRL-010
//cfusa:req REQ-CTRL-021
bool rcp_status_channel_push(rcp_status_channel_t *ch, const rcp_status_t *st)
{
    rcp_status_t *slot;
    size_t tail;

    rcp_mutex_lock(&ch->mu);
    if (ch->closed || ch->count >= ch->cap) {
        rcp_mutex_unlock(&ch->mu);
        return false;
    }

    tail = (ch->head + ch->count) % ch->cap;
    slot = &ch->items[tail];
    slot->zone    = st->zone;
    slot->seq     = st->seq;
    slot->healthy = st->healthy;
    slot->payload = rcp_bytes_dup(st->payload.data, st->payload.len);
    ch->count++;

    rcp_cond_signal(&ch->cv);
    rcp_mutex_unlock(&ch->mu);
    return true;
}

//cfusa:req REQ-CTRL-006
//cfusa:req REQ-CTRL-012
bool rcp_status_channel_recv(rcp_status_channel_t *ch, rcp_status_t *out)
{
    rcp_mutex_lock(&ch->mu);
    while (ch->count == 0 && !ch->closed) {
        rcp_cond_wait(&ch->cv, &ch->mu);
    }
    if (ch->count == 0) {
        rcp_mutex_unlock(&ch->mu);
        return false;
    }
    *out = ch->items[ch->head];
    ch->head = (ch->head + 1) % ch->cap;
    ch->count--;
    rcp_mutex_unlock(&ch->mu);
    return true;
}

bool rcp_status_channel_try_recv(rcp_status_channel_t *ch, rcp_status_t *out)
{
    rcp_mutex_lock(&ch->mu);
    if (ch->count == 0) {
        rcp_mutex_unlock(&ch->mu);
        return false;
    }
    *out = ch->items[ch->head];
    ch->head = (ch->head + 1) % ch->cap;
    ch->count--;
    rcp_mutex_unlock(&ch->mu);
    return true;
}

//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-011
void rcp_status_channel_close(rcp_status_channel_t *ch)
{
    rcp_mutex_lock(&ch->mu);
    ch->closed = true;
    rcp_cond_broadcast(&ch->cv);
    rcp_mutex_unlock(&ch->mu);
}

bool rcp_status_channel_is_closed(rcp_status_channel_t *ch)
{
    bool r;
    rcp_mutex_lock(&ch->mu);
    r = ch->closed;
    rcp_mutex_unlock(&ch->mu);
    return r;
}

/* ── Controller base refcounting ───────────────────────────────────────────── */

//cfusa:req REQ-CTRL-005
rcp_controller_t *rcp_controller_retain(rcp_controller_t *c)
{
    if (c) rcp_atomic_inc(&c->refcount);
    return c;
}

void rcp_controller_release(rcp_controller_t *c)
{
    if (!c) return;
    if (rcp_atomic_dec(&c->refcount) == 0) {
        c->vt->destroy(c);
    }
}

/* ── Registry ──────────────────────────────────────────────────────────────── */

void rcp_registry_destroy(rcp_registry_t *r)
{
    if (!r) return;
    r->vt->destroy(r);
}
