/* Must precede any system header: exposes clock_gettime()/CLOCK_MONOTONIC
 * on glibc under strict -std=c99 (glibc gates POSIX.1-2008 declarations
 * behind this feature-test macro; BSD/Apple libc does not, which is why
 * this was only caught on the Linux CI legs). */
#define _POSIX_C_SOURCE 200809L

#include "rcp/clock.h"

#if defined(_WIN32)

#include <windows.h>

//cfusa:req REQ-CTRL-004
//cfusa:req REQ-CTRL-011
uint64_t rcp_monotonic_ms(void)
{
    static LARGE_INTEGER freq;
    static volatile LONG freq_ready = 0;
    LARGE_INTEGER counter;

    if (!freq_ready) {
        QueryPerformanceFrequency(&freq);
        InterlockedExchange(&freq_ready, 1);
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000) / freq.QuadPart);
}

//cfusa:req REQ-RELAY-001
uint64_t rcp_wallclock_ms(void)
{
    FILETIME ft;
    ULARGE_INTEGER ull;
    /* FILETIME is 100ns ticks since 1601-01-01; 116444736000000000 is the
     * number of such ticks between 1601-01-01 and the Unix epoch. */
    const uint64_t EPOCH_DIFF_100NS = 116444736000000000ULL;

    GetSystemTimeAsFileTime(&ft);
    ull.LowPart  = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    return (uint64_t)((ull.QuadPart - EPOCH_DIFF_100NS) / 10000ULL);
}

#else

#include <time.h>

//cfusa:req REQ-CTRL-004
//cfusa:req REQ-CTRL-011
uint64_t rcp_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

//cfusa:req REQ-RELAY-001
uint64_t rcp_wallclock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

#endif
