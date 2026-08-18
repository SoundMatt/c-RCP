/* SPDX-License-Identifier: MPL-2.0 */
/*
 * loan.h -- Zero-copy* pooled payload buffers for the TC18 Remote Control
 * Protocol wire layer (ROADMAP.md Phase 21, "Satellite Package Rework",
 * milestone 80, "Generic decorators, batch 1").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: a free-list pool of
 * pre-allocated payload buffers is, if anything, *more* valuable under
 * TC18 than it was before -- several endpoint types now have large,
 * fixed-shape payloads worth avoiding a fresh heap allocation for on every
 * request (CAN XL up to RCP_EP_CAN_XL_MAX_ENCODED_LEN octets, ep_can.h;
 * UART RX FIFO drains, ep_uart.h; SPI transfers, ep_spi.h). What changes
 * is only the wrapping shape: the old rcp_loan_controller_new() decorated
 * a whole rcp_controller_t with loan()/send_loaned() vtable slots that no
 * longer exist (ROADMAP.md's Protocol Replacement Notice retires
 * rcp_controller_t's vtable along with rcp_zone_t/rcp_command_t). There is
 * no longer a single generic send() choke point to attach a loan/
 * send_loaned pair to, so this module drops the controller wrapper
 * entirely and becomes a standalone pool type: rcp_loan_pool_acquire()
 * replaces rcp_controller_loan(), and there is no send_loaned()
 * counterpart at all any more -- a caller obtains a buffer, fills it with
 * whichever ep_*.h encoder produces, and drives that endpoint's own
 * request-sending path directly, exactly as milestone 79's watchdog.c/
 * deadline.c/powerstate.c already do for their own caller-driven APIs.
 *
 * *"Zero-copy" describes the buffer hand-off from acquire() through to
 * whatever the caller does with it before releasing it -- this module
 * itself does not copy the loaned bytes. The pool is a real free-list
 * (matching this module's own prior improvement over cpp-RCP's loan.hpp,
 * whose write-only pool never actually reused anything): acquire() pops a
 * same-or-larger buffer if one is available before allocating fresh.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_LOAN_H
#define RCP_LOAN_H

#include "rcp/rcp.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A payload buffer borrowed from a loan pool (see
 * rcp_loan_pool_acquire()). The caller must eventually call
 * rcp_loan_release() exactly once. Call rcp_loan_return() to release the
 * buffer back to the pool early without freeing the rcp_loan_t itself;
 * rcp_loan_release() is safe to call afterward too (a no-op on the
 * buffer, still frees the rcp_loan_t).
 *
 * Lifetime note: a loan must not outlive the pool that issued it. */
struct rcp_loan {
    rcp_bytes_t payload;
    void (*release_fn)(void *release_ctx);
    void *release_ctx;
};

/* Releases the loan's buffer back to its pool without freeing the
 * rcp_loan_t. Safe to call more than once (a no-op after the first
 * call). */
void rcp_loan_return(rcp_loan_t *loan);

/* RAII-destructor equivalent: returns the buffer to the pool (same effect
 * as rcp_loan_return(), safe to call after it) and frees the rcp_loan_t.
 * Call exactly once. */
void rcp_loan_release(rcp_loan_t *loan);

/* Fixed capacity of a pool's own internal free-list (issue #521,
 * ASIL-D-oriented no-dynamic-allocation push): the free-list bookkeeping
 * array (which buffer-and-capacity pair is available for reuse) is a
 * fixed-size array embedded in rcp_loan_pool_t, not realloc()-grown heap
 * storage. Once the free list already holds this many returned buffers,
 * a further rcp_loan_return()/rcp_loan_release() simply frees the
 * returned buffer outright instead of pooling it (rcp_loan_pool_acquire()
 * falls back to a fresh allocation next time, exactly as it already does
 * whenever the free list is empty) -- this never leaks or corrupts
 * state, it only forfeits reuse for the buffer that didn't fit. Matches
 * the "realistic bound" convention this codebase already uses for every
 * other fixed-capacity free-list/table (RCP_RESPQUEUE_MAX_ENTRIES,
 * RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES, all 64). Each individual pooled
 * buffer's own DATA bytes remain heap-allocated regardless (see this
 * header's own file comment: acquire()'s `size` argument is caller/
 * runtime-chosen, not a compile-time protocol constant, so the buffers
 * themselves cannot be embedded fixed-size storage without changing
 * what this module's API contract promises). */
#define RCP_LOAN_POOL_MAX_ENTRIES ((size_t)64u)

typedef struct rcp_loan_pool rcp_loan_pool_t;

/* Creates an empty pool. Returns NULL on allocation failure. */
rcp_loan_pool_t *rcp_loan_pool_new(void);

/* Obtains a zeroed buffer of at least `size` octets from pool: reuses a
 * pooled buffer whose own capacity is >= size if one is available,
 * otherwise allocates fresh. Returns NULL on allocation failure. Release
 * the result with rcp_loan_return()/rcp_loan_release() exactly once. */
rcp_loan_t *rcp_loan_pool_acquire(rcp_loan_pool_t *pool, size_t size);

/* Frees every buffer currently held in pool's free list (whether never
 * loaned or returned via rcp_loan_return()/rcp_loan_release()), then
 * pool itself. Any rcp_loan_t not yet returned at the time of this call
 * must not be used afterward (see the lifetime note above). Call exactly
 * once. */
void rcp_loan_pool_destroy(rcp_loan_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* RCP_LOAN_H */
