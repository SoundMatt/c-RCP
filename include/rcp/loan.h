/*
 * LoaningController extension — wraps any rcp_controller_t with zero-copy*
 * payload loaning via a pool of pre-allocated buffers.
 *
 * rcp_controller_loan() (declared in rcp.h; a no-op returning
 * RCP_ERR_NOT_SUPPORTED unless the controller was created by
 * rcp_loan_controller_new()) obtains a zeroed buffer from the pool.
 * rcp_controller_send_loaned() forwards to the inner controller's send()
 * — like cpp-RCP's own send_loaned(), it does not itself do anything
 * special with the loan; ownership/zero-copy discipline for the payload is
 * on the caller.
 *
 * *"Zero-copy" describes the buffer hand-off from loan() through to
 * whatever the caller does with it before calling send_loaned() — this
 * module itself does not copy the loaned bytes. Ported from cpp-RCP's
 * loan.hpp, with one deliberate improvement: cpp-RCP's own pool is
 * write-only (returned buffers accumulate in a vector that loan() never
 * reads back from, so it never actually reuses anything — an unbounded
 * accumulation for a long-lived controller). This port's pool is a real
 * free-list: loan() pops a same-or-larger buffer if one is available
 * before allocating fresh.
 */
#ifndef RCP_LOAN_H
#define RCP_LOAN_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A payload buffer borrowed from a loaning controller's pool (see
 * rcp_loan_controller_new()). The caller must eventually call
 * rcp_loan_release() exactly once — whether or not the buffer was also
 * passed to rcp_controller_send_loaned() first (send_loaned() does not
 * consume the rcp_loan_t itself, mirroring cpp-RCP's LoaningController,
 * whose send_loaned() takes a plain Command, not the Loan). Call
 * rcp_loan_return() to release the buffer back to the pool early without
 * sending; rcp_loan_release() is safe to call afterward too (a no-op).
 *
 * Lifetime note (same assumption cpp-RCP's own Loan makes via its raw
 * `this`-capturing release lambda): a loan must not outlive the loaning
 * controller that issued it. */
struct rcp_loan {
    rcp_bytes_t payload;
    void (*release_fn)(void *release_ctx);
    void *release_ctx;
};

/* Releases the loan's buffer back to its pool without sending. Safe to
 * call more than once (a no-op after the first call). Does not free the
 * rcp_loan_t itself — see rcp_loan_release(). */
void rcp_loan_return(rcp_loan_t *loan);

/* RAII-destructor equivalent: returns the buffer to the pool (same effect
 * as rcp_loan_return(), safe to call after it) and frees the rcp_loan_t.
 * Call exactly once. */
void rcp_loan_release(rcp_loan_t *loan);

/* Extends inner with loan()/send_loaned() (via rcp_controller_loan() /
 * rcp_controller_send_loaned() in rcp.h) using a pool of pre-allocated
 * buffers. Takes its own reference to inner (retains it) — release your
 * own reference to inner separately if you still need it. Returned with
 * refcount 1; release with rcp_controller_release(), which also releases
 * this wrapper's reference to inner. */
rcp_controller_t *rcp_loan_controller_new(rcp_controller_t *inner);

#ifdef __cplusplus
}
#endif

#endif /* RCP_LOAN_H */
