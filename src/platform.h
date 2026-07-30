/* SPDX-License-Identifier: MPL-2.0 */
/* Internal, not installed: portable mutex/condvar/thread/atomic primitives.
 * rcp.h's public API is a plain C99 vtable interface; this header exists so
 * concrete implementations (mock.c today; UDP/TLS/shmem etc. in later
 * milestones) don't each reinvent cross-platform threading. */
#ifndef RCP_INTERNAL_PLATFORM_H
#define RCP_INTERNAL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
typedef CRITICAL_SECTION rcp_mutex_t;
typedef CONDITION_VARIABLE rcp_cond_t;
typedef HANDLE rcp_thread_t;
#else
#include <pthread.h>
typedef pthread_mutex_t rcp_mutex_t;
typedef pthread_cond_t  rcp_cond_t;
typedef pthread_t       rcp_thread_t;
#endif

void rcp_mutex_init(rcp_mutex_t *m);
void rcp_mutex_lock(rcp_mutex_t *m);
void rcp_mutex_unlock(rcp_mutex_t *m);
void rcp_mutex_destroy(rcp_mutex_t *m);

void rcp_cond_init(rcp_cond_t *c);
void rcp_cond_wait(rcp_cond_t *c, rcp_mutex_t *m);
/* Waits until signaled or rcp_monotonic_ms() reaches deadline_ms, whichever
 * comes first. Returns true if signaled/broadcast, false on timeout. Caller
 * must still re-check its own predicate on return (spurious wakeups). */
bool rcp_cond_timedwait_until(rcp_cond_t *c, rcp_mutex_t *m, uint64_t deadline_ms);
void rcp_cond_signal(rcp_cond_t *c);
void rcp_cond_broadcast(rcp_cond_t *c);
void rcp_cond_destroy(rcp_cond_t *c);

/* Starts fn(arg) running detached in a new thread. Returns 0 on success,
 * nonzero on failure (thread not started; caller retains ownership of arg). */
int rcp_thread_start_detached(void (*fn)(void *arg), void *arg);

/* Starts fn(arg) in a new joinable thread, writing its handle to *out on
 * success (0). Caller must eventually call rcp_thread_join(*out) exactly
 * once. Use this (not the detached variant) whenever the caller needs to
 * know the thread has fully stopped before freeing state it touches. */
int rcp_thread_start(rcp_thread_t *out, void (*fn)(void *arg), void *arg);
void rcp_thread_join(rcp_thread_t t);

/* Sleeps at least ms milliseconds. */
void rcp_sleep_ms(unsigned ms);

/* Atomic increment/decrement returning the new value. Used for refcounting;
 * C99 has no <stdatomic.h> (that's C11), so this wraps compiler builtins. */
int rcp_atomic_inc(volatile int *v);
int rcp_atomic_dec(volatile int *v);

#endif /* RCP_INTERNAL_PLATFORM_H */
