/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/shmem.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

/* ── Shared pair state ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_mutex_t mu;
    rcp_cond_t  cv;
    int         refcount; /* 2 initially: one per side; freed at 0 */

    bool a_closed;
    bool b_closed;

    /* a_to_b: frames A has sent, awaiting B's recv(). b_to_a: the reverse
     * direction. Each is its own bounded circular buffer of owned
     * rcp_bytes_t copies, mirroring avtp.c's own loopback transport's
     * internal ring-buffer shape (this module's own copy -- see other
     * modules' "this TU's own copy" convention for small shared-shape
     * helpers not worth a cross-module utility). */
    rcp_bytes_t *a_to_b_items;
    size_t       a_to_b_head, a_to_b_count, a_to_b_cap;
    rcp_bytes_t *b_to_a_items;
    size_t       b_to_a_head, b_to_a_count, b_to_a_cap;
} rcp_shmem_pair_core_t;

typedef struct {
    rcp_avtp_transport_t   base; /* first member: rcp_avtp_transport_t* <-> this cast */
    rcp_shmem_pair_core_t *core;
    bool                    is_a;
} rcp_shmem_side_t;

static void ring_push(rcp_bytes_t *items, size_t cap, size_t *head, size_t *count, rcp_bytes_t item)
{
    size_t tail = (*head + *count) % cap;
    items[tail] = item;
    (*count)++;
}

/* ── Transport vtable ──────────────────────────────────────────────────────── */

//cfusa:req REQ-SHMEM-002
//cfusa:req REQ-SHMEM-003
//cfusa:req REQ-SHMEM-005
//cfusa:req REQ-SHMEM-007
static int shmem_side_send(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len)
{
    rcp_shmem_side_t      *s = (rcp_shmem_side_t *)self;
    rcp_shmem_pair_core_t *c = s->core;
    bool        *own_closed = s->is_a ? &c->a_closed : &c->b_closed;
    rcp_bytes_t *items      = s->is_a ? c->a_to_b_items : c->b_to_a_items;
    size_t       cap        = s->is_a ? c->a_to_b_cap : c->b_to_a_cap;
    size_t      *head       = s->is_a ? &c->a_to_b_head : &c->b_to_a_head;
    size_t      *count      = s->is_a ? &c->a_to_b_count : &c->b_to_a_count;

    rcp_mutex_lock(&c->mu);
    if (*own_closed) {
        rcp_mutex_unlock(&c->mu);
        return RCP_ERR_CLOSED;
    }
    if (*count >= cap) {
        rcp_mutex_unlock(&c->mu);
        return RCP_ERR_BUSY;
    }
    ring_push(items, cap, head, count, rcp_bytes_dup(frame, frame_len));
    rcp_cond_broadcast(&c->cv);
    rcp_mutex_unlock(&c->mu);
    return RCP_OK;
}

//cfusa:req REQ-SHMEM-002
//cfusa:req REQ-SHMEM-003
//cfusa:req REQ-SHMEM-004
//cfusa:req REQ-SHMEM-005
//cfusa:req REQ-SHMEM-006
//cfusa:req REQ-SHMEM-007
static int shmem_side_recv(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                            uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    rcp_shmem_side_t      *s = (rcp_shmem_side_t *)self;
    rcp_shmem_pair_core_t *c = s->core;
    /* A's recv() drains what B sent (b_to_a); B's recv() drains what A
     * sent (a_to_b) -- the mirror image of shmem_side_send() above. */
    bool        *own_closed  = s->is_a ? &c->a_closed : &c->b_closed;
    bool        *peer_closed = s->is_a ? &c->b_closed : &c->a_closed;
    rcp_bytes_t *items       = s->is_a ? c->b_to_a_items : c->a_to_b_items;
    size_t       cap         = s->is_a ? c->b_to_a_cap : c->a_to_b_cap;
    size_t      *head        = s->is_a ? &c->b_to_a_head : &c->a_to_b_head;
    size_t      *count       = s->is_a ? &c->b_to_a_count : &c->a_to_b_count;
    rcp_bytes_t  item;
    int          rc = RCP_OK;

    rcp_mutex_lock(&c->mu);
    while (*count == 0 && !*own_closed && !*peer_closed && !rcp_context_done(ctx)) {
        if (ctx->has_deadline) {
            (void)rcp_cond_timedwait_until(&c->cv, &c->mu, ctx->deadline_ms);
        } else {
            rcp_cond_wait(&c->cv, &c->mu);
        }
    }

    if (*count == 0) {
        rc = (*own_closed || *peer_closed) ? RCP_ERR_CLOSED : RCP_ERR_TIMEOUT;
        rcp_mutex_unlock(&c->mu);
        return rc;
    }

    item = items[*head];
    if (item.len > buf_cap) {
        /* Left queued, matching avtp.c's own loopback_recv() -- rejecting
         * into too-small a buffer must not silently drop it. */
        rcp_mutex_unlock(&c->mu);
        return RCP_ERR_BUSY;
    }

    if (item.len > 0) memcpy(buf, item.data, item.len);
    *out_len = item.len;
    rcp_bytes_free(&items[*head]);
    *head = (*head + 1) % cap;
    (*count)--;

    rcp_mutex_unlock(&c->mu);
    return RCP_OK;
}

//cfusa:req REQ-SHMEM-004
//cfusa:req REQ-SHMEM-005
static int shmem_side_close(rcp_avtp_transport_t *self)
{
    rcp_shmem_side_t      *s = (rcp_shmem_side_t *)self;
    rcp_shmem_pair_core_t *c = s->core;
    bool                   *own_closed = s->is_a ? &c->a_closed : &c->b_closed;

    rcp_mutex_lock(&c->mu);
    *own_closed = true;
    rcp_cond_broadcast(&c->cv);
    rcp_mutex_unlock(&c->mu);
    return RCP_OK;
}

//cfusa:req REQ-SHMEM-007
//cfusa:req REQ-SHMEM-009
static void shmem_side_destroy(rcp_avtp_transport_t *self)
{
    rcp_shmem_side_t      *s = (rcp_shmem_side_t *)self;
    rcp_shmem_pair_core_t *c = s->core;
    bool                    free_core;

    rcp_mutex_lock(&c->mu);
    c->refcount--;
    free_core = (c->refcount == 0);
    rcp_mutex_unlock(&c->mu);

    if (free_core) {
        size_t i;
        for (i = 0; i < c->a_to_b_count; i++) {
            rcp_bytes_free(&c->a_to_b_items[(c->a_to_b_head + i) % c->a_to_b_cap]);
        }
        for (i = 0; i < c->b_to_a_count; i++) {
            rcp_bytes_free(&c->b_to_a_items[(c->b_to_a_head + i) % c->b_to_a_cap]);
        }
        rcp_mutex_destroy(&c->mu);
        rcp_cond_destroy(&c->cv);
        free(c->a_to_b_items);
        free(c->b_to_a_items);
        free(c);
    }
    free(s);
}

static const rcp_avtp_transport_vtable_t shmem_side_vtable = {
    shmem_side_send,
    shmem_side_recv,
    shmem_side_close,
    shmem_side_destroy,
};

/* ── Construction ──────────────────────────────────────────────────────────── */

//cfusa:req REQ-SHMEM-001
//cfusa:req REQ-SHMEM-008
rcp_shmem_errc_t rcp_shmem_avtp_pair_new(bool time_sync_supported, size_t queue_capacity,
                                          rcp_avtp_transport_t **out_a,
                                          rcp_avtp_transport_t **out_b)
{
    rcp_shmem_pair_core_t *c;
    rcp_shmem_side_t      *a;
    rcp_shmem_side_t      *b;
    rcp_bytes_t            *a_to_b_items;
    rcp_bytes_t            *b_to_a_items;

    if (queue_capacity == 0) queue_capacity = 1;

    c = (rcp_shmem_pair_core_t *)calloc(1, sizeof(*c));
    if (!c) return RCP_SHMEM_ERR_ALLOC;

    /* Checked locally before ever being stored through c, rather than
     * assigning calloc()'s result straight into c->a_to_b_items/
     * c->b_to_a_items and checking afterward -- matches this codebase's
     * own "check an allocation before dereferencing/storing it" house
     * convention (see e.g. avtp.c's loopback constructor). */
    a_to_b_items = (rcp_bytes_t *)calloc(queue_capacity, sizeof(*a_to_b_items));
    b_to_a_items = (rcp_bytes_t *)calloc(queue_capacity, sizeof(*b_to_a_items));
    if (!a_to_b_items || !b_to_a_items) {
        free(a_to_b_items);
        free(b_to_a_items);
        free(c);
        return RCP_SHMEM_ERR_ALLOC;
    }
    c->a_to_b_items = a_to_b_items;
    c->b_to_a_items = b_to_a_items;
    c->a_to_b_cap = queue_capacity;
    c->b_to_a_cap = queue_capacity;
    c->refcount   = 2;
    rcp_mutex_init(&c->mu);
    rcp_cond_init(&c->cv);

    a = (rcp_shmem_side_t *)calloc(1, sizeof(*a));
    b = (rcp_shmem_side_t *)calloc(1, sizeof(*b));
    if (!a || !b) {
        free(a);
        free(b);
        free(c->a_to_b_items);
        free(c->b_to_a_items);
        rcp_mutex_destroy(&c->mu);
        rcp_cond_destroy(&c->cv);
        free(c);
        return RCP_SHMEM_ERR_ALLOC;
    }

    a->base.vt                  = &shmem_side_vtable;
    a->base.refcount             = 1;
    a->base.time_sync_supported = time_sync_supported;
    a->core                     = c;
    a->is_a                     = true;

    b->base.vt                  = &shmem_side_vtable;
    b->base.refcount             = 1;
    b->base.time_sync_supported = time_sync_supported;
    b->core                     = c;
    b->is_a                     = false;

    *out_a = &a->base;
    *out_b = &b->base;
    return RCP_SHMEM_OK;
}
