#include "rcp/prioqueue.h"

#include "platform.h"

#include <stdlib.h>

/* ── Internal queue entry ──────────────────────────────────────────────────── */

typedef struct {
    rcp_context_t  ctx;
    rcp_command_t  cmd;
    uint64_t       seq;

    /* Rendezvous refcount, protected by the owning controller's mu: starts
     * at 2 (one share for queue membership, one for the sending thread's
     * local reference). Whichever side finishes last -- the dispatch
     * thread completing the entry, or the sender giving up on a context
     * timeout -- brings this to 0 and frees the entry. Exactly mirrors the
     * udp.c pending-request rendezvous pattern used elsewhere in this
     * codebase. */
    int            refcount;
    bool           done;
    int            ec;
    rcp_response_t resp;
} pq_entry_t;

/* ── Binary max-heap ordered by (priority desc, seq asc) ──────────────────── */

typedef struct {
    rcp_controller_t  base;
    rcp_controller_t *inner; /* retained */
    rcp_mutex_t        mu;
    rcp_cond_t          cv; /* broadcast on enqueue and on any entry completion */
    pq_entry_t        **heap;
    size_t              heap_len;
    size_t              heap_cap;
    uint64_t            next_seq;
    bool                closed;
    rcp_thread_t        dispatch_thread;
    bool                have_dispatch_thread;
} prioqueue_controller_t;

//cfusa:req REQ-PQ-001
//cfusa:req REQ-PQ-002
/* True if a must be dispatched before b: higher priority first, and among
 * equal priority, lower seq (older) first -- FIFO. */
static bool higher(const pq_entry_t *a, const pq_entry_t *b)
{
    if (a->cmd.priority != b->cmd.priority) return a->cmd.priority > b->cmd.priority;
    return a->seq < b->seq;
}

static void heap_swap(prioqueue_controller_t *pc, size_t i, size_t j)
{
    pq_entry_t *tmp = pc->heap[i];
    pc->heap[i] = pc->heap[j];
    pc->heap[j] = tmp;
}

static void heap_sift_up(prioqueue_controller_t *pc, size_t i)
{
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (!higher(pc->heap[i], pc->heap[parent])) break;
        heap_swap(pc, i, parent);
        i = parent;
    }
}

static void heap_sift_down(prioqueue_controller_t *pc, size_t i)
{
    for (;;) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t largest = i;

        if (left < pc->heap_len && higher(pc->heap[left], pc->heap[largest])) largest = left;
        if (right < pc->heap_len && higher(pc->heap[right], pc->heap[largest])) largest = right;
        if (largest == i) break;
        heap_swap(pc, i, largest);
        i = largest;
    }
}

static bool heap_push(prioqueue_controller_t *pc, pq_entry_t *e)
{
    if (pc->heap_len == pc->heap_cap) {
        size_t new_cap = (pc->heap_cap == 0) ? 8 : pc->heap_cap * 2;
        pq_entry_t **grown = (pq_entry_t **)realloc(pc->heap, new_cap * sizeof(*grown));
        if (!grown) return false;
        pc->heap     = grown;
        pc->heap_cap = new_cap;
    }
    pc->heap[pc->heap_len] = e;
    pc->heap_len++;
    heap_sift_up(pc, pc->heap_len - 1);
    return true;
}

static pq_entry_t *heap_pop(prioqueue_controller_t *pc)
{
    pq_entry_t *top;

    if (pc->heap_len == 0) return NULL;
    top = pc->heap[0];
    pc->heap_len--;
    pc->heap[0] = pc->heap[pc->heap_len];
    if (pc->heap_len > 0) heap_sift_down(pc, 0);
    return top;
}

/* ── Completion ────────────────────────────────────────────────────────────── */

/* Marks e complete with the given result. Must be called with pc->mu held;
 * unlocks it before returning. Frees e (and its response, if unconsumed) if
 * this completion is the second and final release of e's rendezvous
 * refcount -- i.e. the sender already gave up on a context timeout. */
static void complete_entry(prioqueue_controller_t *pc, pq_entry_t *e, int ec, rcp_response_t resp)
{
    bool free_now;

    e->ec   = ec;
    e->resp = resp;
    e->done = true;
    e->refcount--;
    free_now = (e->refcount == 0);
    rcp_cond_broadcast(&pc->cv);
    rcp_mutex_unlock(&pc->mu);

    if (free_now) {
        rcp_response_free(&e->resp);
        free(e);
    }
}

//cfusa:req REQ-PQ-004
static void dispatch_thread_fn(void *arg)
{
    prioqueue_controller_t *pc = (prioqueue_controller_t *)arg;

    for (;;) {
        pq_entry_t *e;

        rcp_mutex_lock(&pc->mu);
        while (!pc->closed && pc->heap_len == 0) {
            rcp_cond_wait(&pc->cv, &pc->mu);
        }
        if (pc->closed && pc->heap_len == 0) {
            rcp_mutex_unlock(&pc->mu);
            break;
        }
        e = heap_pop(pc);
        rcp_mutex_unlock(&pc->mu);

        if (rcp_context_done(&e->ctx)) {
            rcp_response_t empty = {0};
            rcp_mutex_lock(&pc->mu);
            complete_entry(pc, e, RCP_ERR_TIMEOUT, empty);
        } else {
            rcp_response_t resp = {0};
            int ec = rcp_controller_send(pc->inner, &e->ctx, &e->cmd, &resp);
            rcp_mutex_lock(&pc->mu);
            complete_entry(pc, e, ec, resp);
        }
    }

    /* Drain remaining entries with RCP_ERR_CLOSED. */
    rcp_mutex_lock(&pc->mu);
    for (;;) {
        pq_entry_t *e = heap_pop(pc);
        rcp_response_t empty = {0};
        if (!e) break;
        complete_entry(pc, e, RCP_ERR_CLOSED, empty);
        rcp_mutex_lock(&pc->mu);
    }
    rcp_mutex_unlock(&pc->mu);
}

static rcp_zone_t pq_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((prioqueue_controller_t *)self)->inner);
}

//cfusa:req REQ-PQ-003
//cfusa:req REQ-PQ-008
static int pq_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                         const rcp_command_t *cmd, rcp_response_t *out)
{
    prioqueue_controller_t *pc = (prioqueue_controller_t *)self;
    pq_entry_t *e;
    int ec;

    rcp_mutex_lock(&pc->mu);
    if (pc->closed) {
        rcp_mutex_unlock(&pc->mu);
        return RCP_ERR_CLOSED;
    }

    e = (pq_entry_t *)calloc(1, sizeof(*e));
    if (!e) {
        rcp_mutex_unlock(&pc->mu);
        return RCP_ERR_BUSY;
    }
    e->ctx      = *ctx;
    e->cmd      = *cmd;
    e->seq      = ++pc->next_seq;
    e->refcount = 2;

    if (!heap_push(pc, e)) {
        rcp_mutex_unlock(&pc->mu);
        free(e);
        return RCP_ERR_BUSY;
    }
    rcp_cond_broadcast(&pc->cv);

    if (ctx->has_deadline) {
        while (!e->done) {
            if (!rcp_cond_timedwait_until(&pc->cv, &pc->mu, ctx->deadline_ms) && !e->done) {
                bool free_now;
                e->refcount--;
                free_now = (e->refcount == 0);
                rcp_mutex_unlock(&pc->mu);
                if (free_now) free(e); /* dispatch already completed it before we noticed */
                return RCP_ERR_TIMEOUT;
            }
        }
    } else {
        while (!e->done) rcp_cond_wait(&pc->cv, &pc->mu);
    }

    ec = e->ec;
    if (ec == RCP_OK) *out = e->resp; else rcp_response_free(&e->resp);
    e->refcount--; /* dispatch already released its share when it set done=true */
    rcp_mutex_unlock(&pc->mu);
    free(e);
    return ec;
}

//cfusa:req REQ-PQ-006
static int pq_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    prioqueue_controller_t *pc = (prioqueue_controller_t *)self;
    return rcp_controller_subscribe(pc->inner, ctx, out);
}

//cfusa:req REQ-PQ-007
static int pq_ctrl_close(rcp_controller_t *self)
{
    prioqueue_controller_t *pc = (prioqueue_controller_t *)self;

    rcp_mutex_lock(&pc->mu);
    pc->closed = true;
    rcp_mutex_unlock(&pc->mu);
    rcp_cond_broadcast(&pc->cv);

    if (pc->have_dispatch_thread) {
        rcp_thread_join(pc->dispatch_thread);
        pc->have_dispatch_thread = false;
    }
    return rcp_controller_close(pc->inner);
}

static void pq_ctrl_destroy(rcp_controller_t *self)
{
    prioqueue_controller_t *pc = (prioqueue_controller_t *)self;
    (void)pq_ctrl_close(self); /* idempotent; drains the queue and joins the thread */
    rcp_controller_release(pc->inner);
    free(pc->heap);
    rcp_cond_destroy(&pc->cv);
    rcp_mutex_destroy(&pc->mu);
    free(pc);
}

static const rcp_controller_vtable_t prioqueue_controller_vtable = {
    pq_ctrl_zone,
    pq_ctrl_send,
    pq_ctrl_subscribe,
    pq_ctrl_close,
    pq_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_prioqueue_controller_new(rcp_controller_t *inner)
{
    prioqueue_controller_t *pc = (prioqueue_controller_t *)calloc(1, sizeof(*pc));
    if (!pc) return NULL;
    pc->base.vt       = &prioqueue_controller_vtable;
    pc->base.refcount = 1;
    pc->inner         = rcp_controller_retain(inner);
    rcp_mutex_init(&pc->mu);
    rcp_cond_init(&pc->cv);

    if (rcp_thread_start(&pc->dispatch_thread, dispatch_thread_fn, pc) == 0) {
        pc->have_dispatch_thread = true;
    }

    return &pc->base;
}
