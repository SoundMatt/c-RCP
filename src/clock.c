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

#endif
