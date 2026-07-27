#include "rcp/loan.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

void rcp_loan_return(rcp_loan_t *loan)
{
    if (loan->release_fn) {
        loan->release_fn(loan->release_ctx);
        loan->release_fn  = NULL;
        loan->release_ctx = NULL;
    }
}

void rcp_loan_release(rcp_loan_t *loan)
{
    if (!loan) return;
    rcp_loan_return(loan);
    free(loan);
}

/* ── Controller wrapper ────────────────────────────────────────────────────── */

typedef struct {
    uint8_t *data;
    size_t   cap;
} pool_entry_t;

typedef struct {
    rcp_controller_t   base;
    rcp_controller_t  *inner; /* retained */
    rcp_mutex_t         mu;   /* protects closed + pool */
    bool                closed;
    pool_entry_t       *pool;
    size_t              pool_len;
    size_t              pool_cap;
} loan_controller_t;

typedef struct {
    loan_controller_t *lc; /* not retained: a loan must not outlive its controller, see loan.h */
    uint8_t            *data;
    size_t               cap;
} loan_release_ctx_t;

static bool pool_append(loan_controller_t *lc, uint8_t *data, size_t cap)
{
    if (lc->pool_len == lc->pool_cap) {
        size_t new_cap = (lc->pool_cap == 0) ? 4 : lc->pool_cap * 2;
        pool_entry_t *grown = (pool_entry_t *)realloc(lc->pool, new_cap * sizeof(*grown));
        if (!grown) return false;
        lc->pool     = grown;
        lc->pool_cap = new_cap;
    }
    lc->pool[lc->pool_len].data = data;
    lc->pool[lc->pool_len].cap  = cap;
    lc->pool_len++;
    return true;
}

static void loan_release_to_pool(void *ctx_v)
{
    loan_release_ctx_t *ctx = (loan_release_ctx_t *)ctx_v;

    rcp_mutex_lock(&ctx->lc->mu);
    if (!pool_append(ctx->lc, ctx->data, ctx->cap)) {
        /* Pool bookkeeping allocation failed: no reuse possible, just free
         * the buffer outright rather than leaking it. */
        rcp_mutex_unlock(&ctx->lc->mu);
        free(ctx->data);
        free(ctx);
        return;
    }
    rcp_mutex_unlock(&ctx->lc->mu);
    free(ctx);
}

static rcp_zone_t loan_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((loan_controller_t *)self)->inner);
}

//cfusa:req REQ-LOAN-007
static int loan_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                           const rcp_command_t *cmd, rcp_response_t *out)
{
    loan_controller_t *lc = (loan_controller_t *)self;
    return rcp_controller_send(lc->inner, ctx, cmd, out);
}

//cfusa:req REQ-LOAN-008
static int loan_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    loan_controller_t *lc = (loan_controller_t *)self;
    return rcp_controller_subscribe(lc->inner, ctx, out);
}

static int loan_ctrl_close(rcp_controller_t *self)
{
    loan_controller_t *lc = (loan_controller_t *)self;
    rcp_mutex_lock(&lc->mu);
    lc->closed = true;
    rcp_mutex_unlock(&lc->mu);
    return rcp_controller_close(lc->inner);
}

//cfusa:req REQ-LOAN-001
//cfusa:req REQ-LOAN-002
//cfusa:req REQ-LOAN-003
//cfusa:req REQ-LOAN-005
static int loan_ctrl_loan(rcp_controller_t *self, int size, rcp_loan_t **out)
{
    loan_controller_t *lc = (loan_controller_t *)self;
    bool closed_now;
    uint8_t *data = NULL;
    size_t cap = 0;
    size_t want;
    rcp_loan_t *loan;
    loan_release_ctx_t *release_ctx;
    size_t i;

    rcp_mutex_lock(&lc->mu);
    closed_now = lc->closed;
    rcp_mutex_unlock(&lc->mu);
    if (closed_now) return RCP_ERR_CLOSED;
    if (size < 0) return RCP_ERR_BUSY; /* any error is acceptable for invalid size, mirrors cpp-RCP */

    want = (size_t)size;

    rcp_mutex_lock(&lc->mu);
    for (i = 0; i < lc->pool_len; i++) {
        if (lc->pool[i].cap >= want) {
            data = lc->pool[i].data;
            cap  = lc->pool[i].cap;
            lc->pool[i] = lc->pool[lc->pool_len - 1];
            lc->pool_len--;
            break;
        }
    }
    rcp_mutex_unlock(&lc->mu);

    if (data) {
        memset(data, 0, want);
    } else {
        data = (uint8_t *)calloc(want > 0 ? want : 1, 1);
        if (!data) return RCP_ERR_BUSY;
        cap = want;
    }

    loan        = (rcp_loan_t *)calloc(1, sizeof(*loan));
    release_ctx = (loan_release_ctx_t *)malloc(sizeof(*release_ctx));
    if (!loan || !release_ctx) {
        free(loan);
        free(release_ctx);
        free(data);
        return RCP_ERR_BUSY;
    }
    release_ctx->lc   = lc;
    release_ctx->data = data;
    release_ctx->cap  = cap;

    loan->payload.data = data;
    loan->payload.len  = want;
    loan->release_fn   = loan_release_to_pool;
    loan->release_ctx  = release_ctx;

    *out = loan;
    return RCP_OK;
}

//cfusa:req REQ-LOAN-004
static int loan_ctrl_send_loaned(rcp_controller_t *self, const rcp_context_t *ctx,
                                  rcp_command_t *cmd, rcp_response_t *out)
{
    loan_controller_t *lc = (loan_controller_t *)self;
    bool closed_now;

    rcp_mutex_lock(&lc->mu);
    closed_now = lc->closed;
    rcp_mutex_unlock(&lc->mu);
    if (closed_now) return RCP_ERR_CLOSED;

    return rcp_controller_send(lc->inner, ctx, cmd, out);
}

static void loan_ctrl_destroy(rcp_controller_t *self)
{
    loan_controller_t *lc = (loan_controller_t *)self;
    size_t i;

    for (i = 0; i < lc->pool_len; i++) free(lc->pool[i].data);
    free(lc->pool);
    rcp_mutex_destroy(&lc->mu);
    rcp_controller_release(lc->inner);
    free(lc);
}

static const rcp_controller_vtable_t loan_controller_vtable = {
    loan_ctrl_zone,
    loan_ctrl_send,
    loan_ctrl_subscribe,
    loan_ctrl_close,
    loan_ctrl_destroy,
    loan_ctrl_loan,
    loan_ctrl_send_loaned,
};

//cfusa:req REQ-LOAN-006
rcp_controller_t *rcp_loan_controller_new(rcp_controller_t *inner)
{
    loan_controller_t *lc = (loan_controller_t *)calloc(1, sizeof(*lc));
    if (!lc) return NULL;
    lc->base.vt       = &loan_controller_vtable;
    lc->base.refcount = 1;
    lc->inner         = rcp_controller_retain(inner);
    rcp_mutex_init(&lc->mu);
    return &lc->base;
}
