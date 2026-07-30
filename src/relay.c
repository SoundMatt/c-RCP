/* SPDX-License-Identifier: MPL-2.0 */
#include "relay/relay.h"

#include <stdlib.h>
#include <string.h>

#include "platform.h"

//cfusa:req REQ-RELAY-002
const char *relay_protocol_string(relay_protocol_t p)
{
    switch (p) {
    case RELAY_PROTOCOL_CAN:    return "CAN";
    case RELAY_PROTOCOL_DDS:    return "DDS";
    case RELAY_PROTOCOL_LIN:    return "LIN";
    case RELAY_PROTOCOL_MQTT:   return "MQTT";
    case RELAY_PROTOCOL_RCP:    return "RCP";
    case RELAY_PROTOCOL_SOMEIP: return "SOMEIP";
    default:                    return "unknown";
    }
}

/* ── relay_bytes_t ─────────────────────────────────────────────────────────── */

relay_bytes_t relay_bytes_dup(const uint8_t *data, size_t len)
{
    relay_bytes_t b;
    b.data = NULL;
    b.len  = 0;
    if (len == 0) return b;

    b.data = (uint8_t *)malloc(len);
    if (!b.data) return b;

    memcpy(b.data, data, len);
    b.len = len;
    return b;
}

void relay_bytes_free(relay_bytes_t *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len  = 0;
}

/* ── relay_message_t ───────────────────────────────────────────────────────── */

//cfusa:req REQ-RELAY-003
void relay_message_init(relay_message_t *m)
{
    m->protocol     = (relay_protocol_t)0;
    m->id           = NULL;
    m->payload.data = NULL;
    m->payload.len  = 0;
    m->timestamp_ms = 0;
    m->seq          = 0;
    m->meta         = NULL;
    m->meta_len     = 0;
}

//cfusa:req REQ-RELAY-003
void relay_message_free(relay_message_t *m)
{
    size_t i;

    if (!m) return;

    free(m->id);
    relay_bytes_free(&m->payload);
    for (i = 0; i < m->meta_len; i++) {
        free(m->meta[i].key);
        free(m->meta[i].value);
    }
    free(m->meta);

    relay_message_init(m);
}

//cfusa:req REQ-RELAY-003
bool relay_message_set_id(relay_message_t *m, const char *id)
{
    char *copy = NULL;

    if (id) {
        size_t n = strlen(id) + 1;
        copy = (char *)malloc(n);
        if (!copy) return false;
        memcpy(copy, id, n);
    }

    free(m->id);
    m->id = copy;
    return true;
}

//cfusa:req REQ-RELAY-004
bool relay_message_set_meta(relay_message_t *m, const char *key, const char *value)
{
    size_t i;
    char *key_copy, *value_copy;
    size_t key_n, value_n;
    relay_meta_entry_t *grown;

    for (i = 0; i < m->meta_len; i++) {
        if (strcmp(m->meta[i].key, key) == 0) {
            value_n    = strlen(value) + 1;
            value_copy = (char *)malloc(value_n);
            if (!value_copy) return false;
            memcpy(value_copy, value, value_n);
            free(m->meta[i].value);
            m->meta[i].value = value_copy;
            return true;
        }
    }

    key_n    = strlen(key) + 1;
    value_n  = strlen(value) + 1;
    key_copy = (char *)malloc(key_n);
    if (!key_copy) return false;
    value_copy = (char *)malloc(value_n);
    if (!value_copy) {
        free(key_copy);
        return false;
    }
    memcpy(key_copy, key, key_n);
    memcpy(value_copy, value, value_n);

    grown = (relay_meta_entry_t *)realloc(m->meta, (m->meta_len + 1) * sizeof(*grown));
    if (!grown) {
        free(key_copy);
        free(value_copy);
        return false;
    }
    m->meta = grown;
    m->meta[m->meta_len].key   = key_copy;
    m->meta[m->meta_len].value = value_copy;
    m->meta_len++;
    return true;
}

//cfusa:req REQ-RELAY-004
const char *relay_message_get_meta(const relay_message_t *m, const char *key)
{
    size_t i;
    for (i = 0; i < m->meta_len; i++) {
        if (strcmp(m->meta[i].key, key) == 0) return m->meta[i].value;
    }
    return NULL;
}

/* ── SubscriberOptions ─────────────────────────────────────────────────────── */

relay_subscriber_options_t relay_subscriber_options_default(void)
{
    relay_subscriber_options_t opts;
    opts.channel_depth = 64;
    opts.back_pressure = RELAY_BACKPRESSURE_DROP_NEWEST;
    opts.topic_name    = NULL;
    return opts;
}

/* ── Message channel ───────────────────────────────────────────────────────── *
 *
 * Mirrors rcp_status_channel_t's exact circular-buffer/mutex/condvar pattern
 * (src/rcp.c) -- the only difference is the item type and its free function.
 */

struct relay_message_channel {
    rcp_mutex_t      mu;
    rcp_cond_t       cv;
    relay_message_t *items; /* circular buffer, cap entries */
    size_t           head;
    size_t           count;
    size_t           cap;
    bool             closed;
    int              refcount;
};

relay_message_channel_t *relay_message_channel_new(size_t capacity)
{
    relay_message_channel_t *ch;

    if (capacity == 0) capacity = 1;

    ch = (relay_message_channel_t *)malloc(sizeof(relay_message_channel_t));
    if (!ch) return NULL;

    ch->items = (relay_message_t *)calloc(capacity, sizeof(relay_message_t));
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

relay_message_channel_t *relay_message_channel_retain(relay_message_channel_t *ch)
{
    if (ch) rcp_atomic_inc(&ch->refcount);
    return ch;
}

void relay_message_channel_release(relay_message_channel_t *ch)
{
    size_t i;

    if (!ch) return;
    if (rcp_atomic_dec(&ch->refcount) != 0) return;

    for (i = 0; i < ch->count; i++) {
        relay_message_free(&ch->items[(ch->head + i) % ch->cap]);
    }
    rcp_mutex_destroy(&ch->mu);
    rcp_cond_destroy(&ch->cv);
    free(ch->items);
    free(ch);
}

/* Deep-copies msg's owned fields into *dst (dst assumed zeroed/uninitialized
 * storage, e.g. a fresh channel slot). Returns false (leaving *dst
 * unspecified) on allocation failure. */
static bool message_copy_into(relay_message_t *dst, const relay_message_t *src)
{
    size_t i;

    relay_message_init(dst);
    dst->protocol     = src->protocol;
    dst->timestamp_ms = src->timestamp_ms;
    dst->seq          = src->seq;

    if (!relay_message_set_id(dst, src->id)) return false;
    dst->payload = relay_bytes_dup(src->payload.data, src->payload.len);
    for (i = 0; i < src->meta_len; i++) {
        if (!relay_message_set_meta(dst, src->meta[i].key, src->meta[i].value)) {
            relay_message_free(dst);
            return false;
        }
    }
    return true;
}

bool relay_message_channel_push(relay_message_channel_t *ch, const relay_message_t *msg)
{
    relay_message_t *slot;
    size_t tail;

    rcp_mutex_lock(&ch->mu);
    if (ch->closed || ch->count >= ch->cap) {
        rcp_mutex_unlock(&ch->mu);
        return false;
    }

    tail = (ch->head + ch->count) % ch->cap;
    slot = &ch->items[tail];
    if (!message_copy_into(slot, msg)) {
        rcp_mutex_unlock(&ch->mu);
        return false;
    }
    ch->count++;

    rcp_cond_signal(&ch->cv);
    rcp_mutex_unlock(&ch->mu);
    return true;
}

bool relay_message_channel_recv(relay_message_channel_t *ch, relay_message_t *out)
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

bool relay_message_channel_try_recv(relay_message_channel_t *ch, relay_message_t *out)
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

void relay_message_channel_close(relay_message_channel_t *ch)
{
    rcp_mutex_lock(&ch->mu);
    ch->closed = true;
    rcp_cond_broadcast(&ch->cv);
    rcp_mutex_unlock(&ch->mu);
}

//cfusa:req REQ-RELAY-016
bool relay_message_channel_is_closed(relay_message_channel_t *ch)
{
    bool c;
    rcp_mutex_lock(&ch->mu);
    c = ch->closed;
    rcp_mutex_unlock(&ch->mu);
    return c;
}

/* ── Caller — generic retain/release ──────────────────────────────────────── */

//cfusa:req REQ-RELAY-015
rcp_relay_caller_t *rcp_relay_caller_retain(rcp_relay_caller_t *c)
{
    if (c) rcp_atomic_inc(&c->refcount);
    return c;
}

void rcp_relay_caller_release(rcp_relay_caller_t *c)
{
    if (!c) return;
    if (rcp_atomic_dec(&c->refcount) != 0) return;
    c->vt->destroy(c);
}
