/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/loan.h"
#include "rcp/alloc.h"

#include "alloc_overflow.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-LOAN-004
//cfusa:req REQ-LOAN-006
void rcp_loan_return(rcp_loan_t *loan)
{
    if (loan->release_fn) {
        loan->release_fn(loan->release_ctx);
        loan->release_fn  = NULL;
        loan->release_ctx = NULL;
    }
}

//cfusa:req REQ-LOAN-005
void rcp_loan_release(rcp_loan_t *loan)
{
    if (!loan) return;
    rcp_loan_return(loan);
    rcp_free(loan);
    loan = NULL;
}

/* ── Pool ──────────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t *data;
    size_t   cap;
} pool_entry_t;

struct rcp_loan_pool {
    rcp_mutex_t    mu; /* protects entries[] */
    pool_entry_t  *entries;
    size_t         entries_len;
    size_t         entries_cap;
};

typedef struct {
    rcp_loan_pool_t *pool; /* not retained: a loan must not outlive its pool, see loan.h */
    uint8_t          *data;
    size_t             cap;
} loan_release_ctx_t;

//cfusa:req REQ-LOAN-009
rcp_loan_pool_t *rcp_loan_pool_new(void)
{
    rcp_loan_pool_t *pool = (rcp_loan_pool_t *)rcp_calloc(1, sizeof(*pool));
    if (!pool) return NULL;
    rcp_mutex_init(&pool->mu);
    return pool;
}

static bool pool_append(rcp_loan_pool_t *pool, uint8_t *data, size_t cap)
{
    if (pool->entries_len == pool->entries_cap) {
        size_t new_cap = (pool->entries_cap == 0) ? 4 : pool->entries_cap * 2;
        size_t alloc_bytes = rcp_alloc_checked_size(new_cap, sizeof(*pool->entries));
        pool_entry_t *grown = alloc_bytes == 0
            ? NULL
            : (pool_entry_t *)rcp_realloc(pool->entries, alloc_bytes);
        if (!grown) return false;
        pool->entries     = grown;
        pool->entries_cap = new_cap;
    }
    pool->entries[pool->entries_len].data = data;
    pool->entries[pool->entries_len].cap  = cap;
    pool->entries_len++;
    return true;
}

static void loan_release_to_pool(void *ctx_v)
{
    loan_release_ctx_t *ctx = (loan_release_ctx_t *)ctx_v;

    rcp_mutex_lock(&ctx->pool->mu);
    if (!pool_append(ctx->pool, ctx->data, ctx->cap)) {
        /* Pool bookkeeping allocation failed: no reuse possible, just free
         * the buffer outright rather than leaking it. */
        rcp_mutex_unlock(&ctx->pool->mu);
        rcp_free(ctx->data);
        ctx->data = NULL;
        rcp_free(ctx);
        ctx = NULL;
        return;
    }
    rcp_mutex_unlock(&ctx->pool->mu);
    rcp_free(ctx);
    ctx = NULL;
}

//cfusa:req REQ-LOAN-001
//cfusa:req REQ-LOAN-002
//cfusa:req REQ-LOAN-003
//cfusa:req REQ-LOAN-007
rcp_loan_t *rcp_loan_pool_acquire(rcp_loan_pool_t *pool, size_t size)
{
    uint8_t *data = NULL;
    size_t   cap = 0;
    rcp_loan_t *loan;
    loan_release_ctx_t *release_ctx;
    size_t i;

    rcp_mutex_lock(&pool->mu);
    for (i = 0; i < pool->entries_len; i++) {
        if (pool->entries[i].cap >= size) {
            data = pool->entries[i].data;
            cap  = pool->entries[i].cap;
            pool->entries[i] = pool->entries[pool->entries_len - 1];
            pool->entries_len--;
            break;
        }
    }
    rcp_mutex_unlock(&pool->mu);

    if (data) {
        memset(data, 0, size);
    } else {
        data = (uint8_t *)rcp_calloc(size > 0 ? size : 1, 1);
        if (!data) return NULL;
        cap = size;
    }

    loan        = (rcp_loan_t *)rcp_calloc(1, sizeof(*loan));
    release_ctx = (loan_release_ctx_t *)rcp_malloc(sizeof(*release_ctx));
    if (!loan || !release_ctx) {
        rcp_free(loan);
        loan = NULL;
        rcp_free(release_ctx);
        release_ctx = NULL;
        rcp_free(data);
        data = NULL;
        return NULL;
    }
    release_ctx->pool = pool;
    release_ctx->data = data;
    release_ctx->cap  = cap;

    loan->payload.data = data;
    loan->payload.len  = size;
    loan->release_fn   = loan_release_to_pool;
    loan->release_ctx  = release_ctx;

    return loan;
}

//cfusa:req REQ-LOAN-008
void rcp_loan_pool_destroy(rcp_loan_pool_t *pool)
{
    size_t i;

    if (!pool) return;
    for (i = 0; i < pool->entries_len; i++) {
        rcp_free(pool->entries[i].data);
        pool->entries[i].data = NULL;
    }
    rcp_free(pool->entries);
    pool->entries = NULL;
    rcp_mutex_destroy(&pool->mu);
    rcp_free(pool);
    pool = NULL;
}
