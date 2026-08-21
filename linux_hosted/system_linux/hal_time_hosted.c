/*
 * hal_time_hosted.c - hosted implementation of hal_time.h.
 *
 * docs/12-platform-contract.md section 10: clock_gettime(CLOCK_MONOTONIC).
 */
#include "hal_time.h"

#if defined(UNIX)
#include <time.h>
#else
#include <windows.h>
#endif

///////////////////////////////////////////////////////////////////////////

#if defined(UNIX)
static struct timespec origin;
#else
static LARGE_INTEGER perf_freq;
static LARGE_INTEGER perf_origin;
#endif

static int is_init = 0;

///////////////////////////////////////////////////////////////////////////

/* The contract exposes no init call, so the origin is latched lazily. */
static void hal_time_start(void)
{
    if (is_init)
    {
        return;
    }
    is_init = 1;

    #if defined(UNIX)
    clock_gettime(CLOCK_MONOTONIC, &origin);
    #else
    QueryPerformanceFrequency(&perf_freq);
    QueryPerformanceCounter(&perf_origin);
    #endif
}

uint64_t hal_tick_us(void)
{
    hal_time_start();

    #if defined(UNIX)
    struct timespec now;
    int64_t seconds;
    int64_t nseconds;

    clock_gettime(CLOCK_MONOTONIC, &now);

    seconds  = (int64_t)now.tv_sec  - (int64_t)origin.tv_sec;
    nseconds = (int64_t)now.tv_nsec - (int64_t)origin.tv_nsec;

    return (uint64_t)((seconds * 1000000) + (nseconds / 1000));
    #else
    LARGE_INTEGER now;

    QueryPerformanceCounter(&now);

    return (uint64_t)(((now.QuadPart - perf_origin.QuadPart) * 1000000LL) / perf_freq.QuadPart);
    #endif
}

uint32_t hal_tick_ms(void)
{
    return (uint32_t)(hal_tick_us() / 1000u);
}
