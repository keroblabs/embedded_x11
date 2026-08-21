/*
 * fiber_example.c - Cooperative fiber demo for the Linux hosted port.
 *
 * Three fibers print a message on their own period (1000 ms, 800 ms
 * and 500 ms) for five seconds.  Switching is purely cooperative: no
 * interrupt, timer or signal drives the scheduler, every switch comes
 * from an explicit fiber_yield().
 */
#include "fiber.h"
#include "hal_time.h"

#include <stdio.h>
#include <time.h>

#define DEMO_RUN_MS         5000u
#define DEMO_TICKER_COUNT   3

typedef struct
{
    const char *name;
    uint32_t    period_ms;
    uint32_t    due_ms;
    uint32_t    ticks;

} ticker_t;

static ticker_t g_tickers[DEMO_TICKER_COUNT] =
{
    { "ticker-1000ms", 1000u, 0u, 0u },
    { "ticker-800ms",   800u, 0u, 0u },
    { "ticker-500ms",   500u, 0u, 0u },
};

/* Hands the CPU back to the OS once per scheduling round; without it
 * the cooperative ring would spin a core flat out for five seconds. */
static void host_idle_1ms(void)
{
    struct timespec ts = { 0, 1000000L };
    (void)nanosleep(&ts, NULL);
}

static void ticker_entry(void *arg)
{
    ticker_t *ticker = (ticker_t *)arg;

    ticker->due_ms = hal_tick_ms() + ticker->period_ms;

    for (;;)
    {
        uint32_t now_ms = hal_tick_ms();

        if ((int32_t)(now_ms - ticker->due_ms) >= 0)
        {
            ticker->ticks++;
            printf("[%5u ms] %-13s tick %u\n", now_ms, ticker->name, ticker->ticks);
            fflush(stdout);
            ticker->due_ms += ticker->period_ms;
        }

        fiber_yield();
    }
}

int main(void)
{
    fiber_t *fibers[DEMO_TICKER_COUNT];
    int i;

    /* The Linux port has no preemption source; the period is accepted
     * for API parity with the bare-metal ports and never used.      */
    fiber_runtime_init(DEFAULT_PREEMPTION_TIME_MS);

    for (i = 0; i < DEMO_TICKER_COUNT; i++)
    {
        fibers[i] = fiber_create(ticker_entry, &g_tickers[i], 0u);
        if (fibers[i] == NULL)
        {
            fprintf(stderr, "fiber_create failed for %s\n", g_tickers[i].name);
            return 1;
        }
    }

    printf("cooperative fiber demo: %u fibers, %u ms run time\n",
           (uint32_t)DEMO_TICKER_COUNT, (uint32_t)DEMO_RUN_MS);
    fflush(stdout);

    uint32_t start_ms = hal_tick_ms();
    while ((hal_tick_ms() - start_ms) < DEMO_RUN_MS)
    {
        fiber_yield();
        host_idle_1ms();
    }

    for (i = 0; i < DEMO_TICKER_COUNT; i++)
    {
        fiber_destroy(fibers[i]);
    }
    fiber_yield();      /* scheduler pass that reclaims the ZOMBIEs */

    printf("finished after %u ms\n", hal_tick_ms() - start_ms);
    for (i = 0; i < DEMO_TICKER_COUNT; i++)
    {
        printf("  %-13s %u ticks (period %u ms)\n",
               g_tickers[i].name, g_tickers[i].ticks, g_tickers[i].period_ms);
    }

    return 0;
}
