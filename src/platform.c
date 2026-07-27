#include "platform.h"

#include <stdlib.h>

#if defined(_WIN32)

//cfusa:req REQ-CTRL-018
//cfusa:req REQ-CTRL-019
void rcp_mutex_init(rcp_mutex_t *m)    { InitializeCriticalSection(m); }
void rcp_mutex_lock(rcp_mutex_t *m)    { EnterCriticalSection(m); }
void rcp_mutex_unlock(rcp_mutex_t *m)  { LeaveCriticalSection(m); }
void rcp_mutex_destroy(rcp_mutex_t *m) { DeleteCriticalSection(m); }

void rcp_cond_init(rcp_cond_t *c)                       { InitializeConditionVariable(c); }
void rcp_cond_wait(rcp_cond_t *c, rcp_mutex_t *m)        { SleepConditionVariableCS(c, m, INFINITE); }
void rcp_cond_signal(rcp_cond_t *c)                      { WakeConditionVariable(c); }
void rcp_cond_broadcast(rcp_cond_t *c)                   { WakeAllConditionVariable(c); }
void rcp_cond_destroy(rcp_cond_t *c)                     { (void)c; /* no-op on Windows */ }

typedef struct {
    void (*fn)(void *);
    void *arg;
} thread_thunk_t;

static DWORD WINAPI thread_trampoline(LPVOID param)
{
    thread_thunk_t *t = (thread_thunk_t *)param;
    void (*fn)(void *) = t->fn;
    void *arg = t->arg;
    free(t);
    fn(arg);
    return 0;
}

//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-011
int rcp_thread_start_detached(void (*fn)(void *arg), void *arg)
{
    thread_thunk_t *t = (thread_thunk_t *)malloc(sizeof(thread_thunk_t));
    HANDLE h;
    if (!t) return -1;
    t->fn  = fn;
    t->arg = arg;
    h = CreateThread(NULL, 0, thread_trampoline, t, 0, NULL);
    if (!h) {
        free(t);
        return -1;
    }
    CloseHandle(h); /* detach: we never join, just don't leak the handle */
    return 0;
}

int rcp_atomic_inc(volatile int *v)
{
    return (int)InterlockedIncrement((volatile LONG *)v);
}

int rcp_atomic_dec(volatile int *v)
{
    return (int)InterlockedDecrement((volatile LONG *)v);
}

void rcp_sleep_ms(unsigned ms)
{
    Sleep(ms);
}

#else /* POSIX */

#include <time.h>

//cfusa:req REQ-CTRL-018
//cfusa:req REQ-CTRL-019
void rcp_mutex_init(rcp_mutex_t *m)    { pthread_mutex_init(m, NULL); }
void rcp_mutex_lock(rcp_mutex_t *m)    { pthread_mutex_lock(m); }
void rcp_mutex_unlock(rcp_mutex_t *m)  { pthread_mutex_unlock(m); }
void rcp_mutex_destroy(rcp_mutex_t *m) { pthread_mutex_destroy(m); }

void rcp_cond_init(rcp_cond_t *c)                 { pthread_cond_init(c, NULL); }
void rcp_cond_wait(rcp_cond_t *c, rcp_mutex_t *m)  { pthread_cond_wait(c, m); }
void rcp_cond_signal(rcp_cond_t *c)                { pthread_cond_signal(c); }
void rcp_cond_broadcast(rcp_cond_t *c)             { pthread_cond_broadcast(c); }
void rcp_cond_destroy(rcp_cond_t *c)               { pthread_cond_destroy(c); }

typedef struct {
    void (*fn)(void *);
    void *arg;
} thread_thunk_t;

static void *thread_trampoline(void *param)
{
    thread_thunk_t *t = (thread_thunk_t *)param;
    void (*fn)(void *) = t->fn;
    void *arg = t->arg;
    free(t);
    fn(arg);
    return NULL;
}

//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-011
int rcp_thread_start_detached(void (*fn)(void *arg), void *arg)
{
    pthread_t tid;
    thread_thunk_t *t = (thread_thunk_t *)malloc(sizeof(thread_thunk_t));
    int rc;
    if (!t) return -1;
    t->fn  = fn;
    t->arg = arg;
    rc = pthread_create(&tid, NULL, thread_trampoline, t);
    if (rc != 0) {
        free(t);
        return -1;
    }
    pthread_detach(tid);
    return 0;
}

int rcp_atomic_inc(volatile int *v)
{
    return __atomic_add_fetch(v, 1, __ATOMIC_ACQ_REL);
}

int rcp_atomic_dec(volatile int *v)
{
    return __atomic_sub_fetch(v, 1, __ATOMIC_ACQ_REL);
}

void rcp_sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000u;
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

#endif
