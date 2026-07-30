/* SPDX-License-Identifier: MPL-2.0 */
/* Test-only nanosecond-resolution timer, shared by bench_mock.c and
 * command_latency_test.c. The library's own rcp_monotonic_ms() is
 * millisecond-resolution (adequate for context deadlines); these two tests
 * need finer granularity to measure sub-millisecond in-process mock calls. */
#ifndef RCP_TEST_BENCH_UTIL_H
#define RCP_TEST_BENCH_UTIL_H

/* Must precede any system header on POSIX (see clock.c/platform.c for the
 * same fix and rationale): exposes clock_gettime()/CLOCK_MONOTONIC on
 * glibc under strict -std=c99. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>

static inline uint64_t bench_now_ns(void)
{
    static LARGE_INTEGER freq;
    static volatile LONG freq_ready = 0;
    LARGE_INTEGER counter;

    if (!freq_ready) {
        QueryPerformanceFrequency(&freq);
        InterlockedExchange(&freq_ready, 1);
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
}

#else
#include <time.h>

static inline uint64_t bench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#endif

#endif /* RCP_TEST_BENCH_UTIL_H */
