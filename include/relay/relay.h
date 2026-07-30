/* SPDX-License-Identifier: MPL-2.0 */
/* RELAY C bindings — relay_* types (RELAY spec §18.2, v2.0).
 *
 * Defines the protocol-agnostic subset of the relay spec c-RCP depends on:
 * the mandatory error-condition sentinels (§5.1), Context (§18.2), the
 * canonical Message envelope (§4), and the Node/Caller application
 * interface (§18.2, here just "Caller" since C has no interface
 * inheritance -- rcp_relay_caller_t satisfies both roles). This is a pure-C
 * port of the same subset cpp-RCP's include/relay/relay.hpp exposes; it is
 * not a full RELAY binding.
 *
 * This header itself is protocol-agnostic and structurally unaffected by
 * either ROADMAP.md's Phase 13-21 TC18 protocol replacement or RELAY spec
 * v2.0's own §15.5/§15.7.5 rework (replacing the RCP canonical types this
 * project's own pre-TC18 protocol generation had been keyed to) -- the
 * per-endpoint-type Message-mapping problem those create lives entirely in
 * rcp/adapt.h/adapt.c (milestone 84, and its own file header's discussion of
 * SoundMatt/RELAY issue #66 for the still-open upstream question), not
 * here. Bumping RELAY_SPEC_VERSION to the current released RELAY version
 * below is this milestone's own incidental cleanup while that area was
 * already being touched.
 */
#ifndef RELAY_RELAY_H
#define RELAY_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rcp/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Spec version (§19.4) ──────────────────────────────────────────────────── *
 *
 * Deliberately targets the current released RELAY spec version, not
 * whatever earlier version motivated a prior remediation effort --
 * conforming to a stale target would be self-defeating. v1.13 was a
 * deep-audit fix pass (conformance-gate bugs, tooling fixes, C++/Rust
 * binding parity); v1.14 expanded the §13.7.2 module-name registry this
 * project's own module-naming reconciliation (issue #87) already
 * consulted (see ROADMAP.md's "Module-naming note"). v2.0 (a MAJOR bump,
 * per §19.3's stability guarantee) replaced §15.5's RCP canonical types
 * and §15.7.5's ToMessage()/FromMessage() mapping outright -- see
 * rcp/adapt.h's file header and issue #107 for how that lands here.
 */
#define RELAY_SPEC_VERSION "2.0"

/* ── Error conditions — mandatory sentinels (§5.1) ────────────────────────── */

typedef enum {
    RELAY_ERRC_CLOSED            = 0,
    RELAY_ERRC_NOT_CONNECTED     = 1,
    RELAY_ERRC_TIMEOUT           = 2,
    RELAY_ERRC_PAYLOAD_TOO_LARGE = 3,
} relay_errc_t;

const char *relay_strerror(relay_errc_t e);

/* ── Context (§18.2) ──────────────────────────────────────────────────────── */

typedef struct {
    bool     has_deadline;
    uint64_t deadline_ms; /* rcp_monotonic_ms() timestamp; valid only if has_deadline */
} relay_context_t;

static inline relay_context_t relay_context_background(void)
{
    relay_context_t ctx;
    ctx.has_deadline = false;
    ctx.deadline_ms  = 0;
    return ctx;
}

static inline relay_context_t relay_context_with_timeout_ms(uint64_t timeout_ms)
{
    relay_context_t ctx;
    ctx.has_deadline = true;
    ctx.deadline_ms  = rcp_monotonic_ms() + timeout_ms;
    return ctx;
}

static inline relay_context_t relay_context_with_deadline_ms(uint64_t deadline_ms)
{
    relay_context_t ctx;
    ctx.has_deadline = true;
    ctx.deadline_ms  = deadline_ms;
    return ctx;
}

static inline bool relay_context_done(const relay_context_t *ctx)
{
    if (!ctx->has_deadline) return false;
    return rcp_monotonic_ms() >= ctx->deadline_ms;
}

/* ── Protocol identifiers (§3) ─────────────────────────────────────────────── */

typedef enum {
    RELAY_PROTOCOL_CAN    = 1,
    RELAY_PROTOCOL_DDS    = 2,
    RELAY_PROTOCOL_LIN    = 3,
    RELAY_PROTOCOL_MQTT   = 4,
    RELAY_PROTOCOL_RCP    = 5,
    RELAY_PROTOCOL_SOMEIP = 6,
} relay_protocol_t;

const char *relay_protocol_string(relay_protocol_t p);

/* ── Message — universal envelope (§4) ────────────────────────────────────── *
 *
 * id/payload/meta are owned (heap-allocated) once set via the setter
 * functions below; relay_message_free() releases all three. A
 * zero-initialized relay_message_t (relay_message_init(), or plain {0}) is
 * always safe to pass to relay_message_free().
 *
 * payload uses its own relay_bytes_t rather than rcp.h's rcp_bytes_t: this
 * header must stay protocol-agnostic (relay_message_t is the shared
 * envelope every protocol adapter -- CAN, DDS, LIN, MQTT, RCP, SOMEIP --
 * maps into), and rcp_bytes_t is only defined later, in rcp.h, which
 * itself includes this header first.
 */

typedef struct {
    uint8_t *data; /* owned, may be NULL if len == 0 */
    size_t   len;
} relay_bytes_t;

/* Heap-allocates a copy of [data, data+len). Returns a zeroed relay_bytes_t
 * (data == NULL, len == 0) if len == 0 or on allocation failure. */
relay_bytes_t relay_bytes_dup(const uint8_t *data, size_t len);

/* Frees b->data and zeroes *b. Safe to call on an already-freed/zeroed b. */
void relay_bytes_free(relay_bytes_t *b);

typedef struct {
    char *key;   /* owned copy */
    char *value; /* owned copy */
} relay_meta_entry_t;

typedef struct {
    relay_protocol_t    protocol;
    char               *id;           /* owned copy, may be NULL if unset */
    relay_bytes_t       payload;      /* owned copy */
    uint64_t            timestamp_ms; /* rcp_wallclock_ms() at construction */
    uint64_t            seq;
    relay_meta_entry_t *meta;         /* owned array, may be NULL if empty */
    size_t              meta_len;
} relay_message_t;

/* Zeroes *m (protocol defaults to 0, an intentionally-invalid sentinel until
 * explicitly set -- there is no "default" protocol). */
void relay_message_init(relay_message_t *m);

/* Frees m->id, m->payload, and every meta entry (including the array
 * itself), then zeroes *m. Safe to call on an already-freed/zeroed message. */
void relay_message_free(relay_message_t *m);

/* Frees any existing m->id, then deep-copies id (may be NULL to clear).
 * Returns false (leaving *m unchanged) on allocation failure. */
bool relay_message_set_id(relay_message_t *m, const char *id);

/* Upserts a deep copy of key/value into m->meta (replacing any existing
 * entry with the same key). Returns false (leaving *m unchanged) on
 * allocation failure. */
bool relay_message_set_meta(relay_message_t *m, const char *key, const char *value);

/* Returns a borrowed pointer to the value for key, or NULL if absent. The
 * pointer is valid until the next relay_message_set_meta()/_free() call. */
const char *relay_message_get_meta(const relay_message_t *m, const char *key);

/* ── BackPressurePolicy and SubscriberOptions (§18.2) ─────────────────────── */

typedef enum {
    RELAY_BACKPRESSURE_DROP_NEWEST = 0,
    RELAY_BACKPRESSURE_DROP_OLDEST = 1,
    RELAY_BACKPRESSURE_BLOCK       = 2,
} relay_backpressure_t;

typedef struct {
    size_t               channel_depth; /* default: 64 */
    relay_backpressure_t back_pressure; /* default: RELAY_BACKPRESSURE_DROP_NEWEST */
    const char          *topic_name;    /* DDS-only routing hint; RCP ignores it.
                                          * NULL = unset. Borrowed, not copied. */
} relay_subscriber_options_t;

relay_subscriber_options_t relay_subscriber_options_default(void);

/* ── Message channel — bounded queue of relay_message_t (§18.2) ───────────── *
 *
 * A concrete (non-generic) channel, since C has no templates -- mirrors
 * rcp_status_channel_t's exact locking pattern. push() is safe to call from
 * a single writer thread concurrently with recv(); multiple concurrent
 * writers MUST be serialised externally (§18.2 note).
 */
typedef struct relay_message_channel relay_message_channel_t;

relay_message_channel_t *relay_message_channel_new(size_t capacity);
relay_message_channel_t *relay_message_channel_retain(relay_message_channel_t *ch);
void                     relay_message_channel_release(relay_message_channel_t *ch);

/* Deep-copies *msg into the channel (does not take ownership of *msg; caller
 * still owns and must free it). Returns false if the channel is full or
 * closed. */
bool relay_message_channel_push(relay_message_channel_t *ch, const relay_message_t *msg);

/* Blocks until an item is available or the channel closes. Returns false
 * (leaving *out untouched) if the channel closed with no item pending.
 * Caller owns *out on success and must relay_message_free() it. */
bool relay_message_channel_recv(relay_message_channel_t *ch, relay_message_t *out);

/* Non-blocking; returns false immediately if nothing is queued. */
bool relay_message_channel_try_recv(relay_message_channel_t *ch, relay_message_t *out);

void relay_message_channel_close(relay_message_channel_t *ch);
bool relay_message_channel_is_closed(relay_message_channel_t *ch);

/* ── Caller — vtable-based Node+Caller interface (§18.2) ──────────────────── *
 *
 * cpp-RCP's Node/Caller is a two-level interface (Caller derives from
 * Node); C has no interface inheritance, so rcp_relay_caller_t's single
 * vtable covers both roles directly (protocol/send/close from Node, plus
 * call/subscribe from Caller) -- any implementation satisfies both.
 */
typedef struct rcp_relay_caller rcp_relay_caller_t;

typedef struct {
    relay_protocol_t (*protocol)(rcp_relay_caller_t *self);

    /* Maps msg to a protocol-native request and dispatches it, discarding
     * any reply (§10.6 "send" semantics -- fire-and-forget). */
    int (*send)(rcp_relay_caller_t *self, const relay_context_t *ctx,
                const relay_message_t *msg);

    /* Maps msg to a protocol-native request, dispatches it, and maps the
     * reply back to *out. Caller owns *out on success (RELAY_OK-equivalent
     * 0 return) and must relay_message_free() it. */
    int (*call)(rcp_relay_caller_t *self, const relay_context_t *ctx,
                const relay_message_t *req, relay_message_t *out);

    /* Returns (via *out) a new reference to a channel of Messages mapped
     * from the underlying protocol's periodic status/event stream, per
     * opts. The channel closes when the underlying stream closes. */
    int (*subscribe)(rcp_relay_caller_t *self, const relay_subscriber_options_t *opts,
                      relay_message_channel_t **out);

    int (*close)(rcp_relay_caller_t *self);

    /* Frees the concrete implementation once its refcount reaches 0. Never
     * called directly; invoked by rcp_relay_caller_release(). */
    void (*destroy)(rcp_relay_caller_t *self);
} rcp_relay_caller_vtable_t;

struct rcp_relay_caller {
    const rcp_relay_caller_vtable_t *vt;
    int                               refcount;
};

rcp_relay_caller_t *rcp_relay_caller_retain(rcp_relay_caller_t *c);
void                rcp_relay_caller_release(rcp_relay_caller_t *c);

static inline relay_protocol_t rcp_relay_caller_protocol(rcp_relay_caller_t *c)
{
    return c->vt->protocol(c);
}

static inline int rcp_relay_caller_send(rcp_relay_caller_t *c, const relay_context_t *ctx,
                                         const relay_message_t *msg)
{
    return c->vt->send(c, ctx, msg);
}

static inline int rcp_relay_caller_call(rcp_relay_caller_t *c, const relay_context_t *ctx,
                                         const relay_message_t *req, relay_message_t *out)
{
    return c->vt->call(c, ctx, req, out);
}

static inline int rcp_relay_caller_subscribe(rcp_relay_caller_t *c,
                                              const relay_subscriber_options_t *opts,
                                              relay_message_channel_t **out)
{
    return c->vt->subscribe(c, opts, out);
}

static inline int rcp_relay_caller_close(rcp_relay_caller_t *c)
{
    return c->vt->close(c);
}

#ifdef __cplusplus
}
#endif

#endif /* RELAY_RELAY_H */
