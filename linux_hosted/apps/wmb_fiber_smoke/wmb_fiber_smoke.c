/*
 * wmb_fiber_smoke.c - Runtime gates for the fiber layer.
 *
 * Closes docs/44 section 4.3:
 *   --pingpong   V-P2-1  100k fiber_yield() calls < 100 ms
 *   --sync       V-P2-3  fiber_mutex_t and fiber_cond_t work between fibers
 *   --overflow   V-P2-4  a stack overflow SIGSEGVs on the guard page
 *
 * Each subcommand runs one scenario and exits.  This lets the shell
 * (or a make target) run them separately, and lets a SIGSEGV-terminating
 * test not disturb the others.
 */
#include "fiber.h"
#include "fiber_sync.h"
#include "hal_time.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* --------------------------------------------------------------------
 * V-P2-1: ping-pong
 * -------------------------------------------------------------------- */

#define PP_YIELD_TARGET 100000u
#define PP_DEADLINE_MS  100u

static volatile uint32_t g_pp_yields;

static void pp_worker(void *arg)
{
    (void)arg;
    while (g_pp_yields < PP_YIELD_TARGET) {
        g_pp_yields++;
        fiber_yield();
    }
}

static int run_pingpong(void)
{
    fiber_runtime_init(0);
    g_pp_yields = 0;

    fiber_t *ping = fiber_create(pp_worker, NULL, 0);
    fiber_t *pong = fiber_create(pp_worker, NULL, 0);
    if (!ping || !pong) {
        fputs("V-P2-1 FAIL: fiber_create returned NULL\n", stderr);
        return 1;
    }

    uint32_t start = hal_tick_ms();
    /* Uses a wall-clock deadline so a broken runtime cannot wedge CI. */
    uint32_t hard_deadline = start + 5000u;
    while (g_pp_yields < PP_YIELD_TARGET) {
        fiber_yield();
        if (hal_tick_ms() > hard_deadline) {
            fputs("V-P2-1 FAIL: hard deadline exceeded (fiber_yield stuck?)\n",
                  stderr);
            return 1;
        }
    }
    uint32_t elapsed = hal_tick_ms() - start;

    printf("V-P2-1: %u fiber_yield() calls in %u ms\n",
           PP_YIELD_TARGET, elapsed);
    if (elapsed >= PP_DEADLINE_MS) {
        printf("  FAIL: expected < %u ms\n", PP_DEADLINE_MS);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* --------------------------------------------------------------------
 * V-P2-3: mutex + cond var
 * -------------------------------------------------------------------- */

#define MU_ITERS         100
#define SYNC_DEADLINE_MS 5000u

static fiber_mutex_t   *g_mu;
static fiber_cond_t    *g_cv;
static int              g_mu_counter;
static volatile int     g_mu_a_done;
static volatile int     g_mu_b_done;
static volatile int     g_cv_waiter_stage;   /* 0 init, 1 parked, 3 woken */
static volatile int     g_cv_signaler_done;

static void mu_inc(void *arg)
{
    volatile int *done = (volatile int *)arg;
    int i;

    for (i = 0; i < MU_ITERS; i++) {
        fiber_mutex_lock(g_mu);
        int t = g_mu_counter;
        /* Provoking a race: if the mutex fails to block the peer, the
         * peer will read `t` again and write t+1 (losing our update). */
        fiber_yield();
        g_mu_counter = t + 1;
        fiber_mutex_unlock(g_mu);
    }
    *done = 1;
}

static void cv_waiter(void *arg)
{
    (void)arg;
    /* g_cv_waiter_stage=1 published to the signaler.  Because the
     * scheduler is cooperative there is no yield between this store
     * and fiber_cond_wait's internal park - the signaler seeing 1
     * means the snapshot has been taken. */
    g_cv_waiter_stage = 1;
    fiber_cond_wait(g_cv);
    g_cv_waiter_stage = 3;
}

static void cv_signaler(void *arg)
{
    (void)arg;
    while (g_cv_waiter_stage < 1) {
        fiber_yield();
    }
    fiber_cond_signal(g_cv);
    g_cv_signaler_done = 1;
}

static int poll_until(volatile int *a, volatile int *b, uint32_t deadline_ms)
{
    uint32_t deadline = hal_tick_ms() + deadline_ms;
    while (!(*a && *b)) {
        fiber_yield();
        if (hal_tick_ms() > deadline) return -1;
    }
    return 0;
}

static int run_sync(void)
{
    fiber_runtime_init(0);
    g_mu = fiber_mutex_create();
    g_cv = fiber_cond_create();
    if (!g_mu || !g_cv) {
        fputs("V-P2-3 FAIL: mutex/cond create returned NULL\n", stderr);
        return 1;
    }

    /* Mutex correctness --------------------------------------------- */
    g_mu_counter = 0;
    g_mu_a_done  = 0;
    g_mu_b_done  = 0;
    fiber_t *a = fiber_create(mu_inc, (void *)&g_mu_a_done, 0);
    fiber_t *b = fiber_create(mu_inc, (void *)&g_mu_b_done, 0);
    if (!a || !b) {
        fputs("V-P2-3 FAIL: fiber_create returned NULL\n", stderr);
        return 1;
    }
    if (poll_until(&g_mu_a_done, &g_mu_b_done, SYNC_DEADLINE_MS) != 0) {
        fputs("V-P2-3 FAIL: mutex test deadline exceeded\n", stderr);
        return 1;
    }
    int mu_ok = (g_mu_counter == 2 * MU_ITERS);
    printf("V-P2-3 mutex: counter=%d expected=%d %s\n",
           g_mu_counter, 2 * MU_ITERS, mu_ok ? "PASS" : "FAIL");

    /* Cond var correctness ------------------------------------------ */
    g_cv_waiter_stage  = 0;
    g_cv_signaler_done = 0;
    fiber_t *w = fiber_create(cv_waiter,   NULL, 0);
    fiber_t *s = fiber_create(cv_signaler, NULL, 0);
    if (!w || !s) {
        fputs("V-P2-3 FAIL: fiber_create returned NULL\n", stderr);
        return 1;
    }
    volatile int waiter_woken = 0;
    uint32_t deadline = hal_tick_ms() + SYNC_DEADLINE_MS;
    while (!g_cv_signaler_done || g_cv_waiter_stage != 3) {
        fiber_yield();
        if (g_cv_waiter_stage == 3) waiter_woken = 1;
        if (hal_tick_ms() > deadline) {
            fputs("V-P2-3 FAIL: cond test deadline exceeded\n", stderr);
            return 1;
        }
    }
    int cv_ok = (waiter_woken && g_cv_waiter_stage == 3);
    printf("V-P2-3 cond:  waiter stage=%d %s\n",
           g_cv_waiter_stage, cv_ok ? "PASS" : "FAIL");

    if (!mu_ok || !cv_ok) return 1;
    printf("  PASS\n");
    return 0;
}

/* --------------------------------------------------------------------
 * V-P2-4: guard-page stack overflow
 *
 * A fiber recurses through frames big enough to keep the compiler from
 * folding them.  When SP crosses into the port's PROT_NONE guard page,
 * the kernel delivers SIGSEGV with si_code == SEGV_ACCERR (permission
 * violation, not "not mapped").  The handler distinguishes and reports.
 * -------------------------------------------------------------------- */

static volatile unsigned g_overflow_depth;

static void overflow_recurse(void)
{
    volatile char blocker[512];
    memset((void *)blocker, 0xAA, sizeof(blocker));
    g_overflow_depth++;
    overflow_recurse();
}

static void overflow_entry(void *arg)
{
    (void)arg;
    overflow_recurse();
}

/* Async-signal-safe: write() and _exit() only.  The handler runs on
 * an alternate stack (SA_ONSTACK) because the fiber stack is toast. */
static void overflow_sigsegv(int sig, siginfo_t *info, void *ctx)
{
    (void)sig;
    (void)ctx;

    if (info->si_code == SEGV_ACCERR) {
        static const char msg[] =
            "V-P2-4: SIGSEGV SEGV_ACCERR at guard page\n  PASS\n";
        (void)!write(1, msg, sizeof(msg) - 1u);
        _exit(0);
    }
    static const char msg[] =
        "V-P2-4: SIGSEGV but si_code != SEGV_ACCERR\n  FAIL\n";
    (void)!write(2, msg, sizeof(msg) - 1u);
    _exit(2);
}

static int run_overflow(void)
{
    /* Alt stack sized well above SIGSTKSZ so the handler's frames -
     * including glibc/libc runtime helpers - always fit. */
    static char altstack[64 * 1024];
    stack_t ss = {
        .ss_sp    = altstack,
        .ss_size  = sizeof(altstack),
        .ss_flags = 0,
    };
    if (sigaltstack(&ss, NULL) != 0) {
        perror("sigaltstack");
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = overflow_sigsegv;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    fiber_runtime_init(0);
    g_overflow_depth = 0;

    fiber_t *of = fiber_create(overflow_entry, NULL, 0);
    if (!of) {
        fputs("V-P2-4 FAIL: fiber_create returned NULL\n", stderr);
        return 1;
    }

    /* Spin scheduler until either the SIGSEGV handler exits us, or a
     * safety deadline says the overflow never triggered. */
    uint32_t deadline = hal_tick_ms() + 2000u;
    while (hal_tick_ms() < deadline) {
        fiber_yield();
    }

    printf("V-P2-4 FAIL: recursion reached depth %u without SIGSEGV\n",
           g_overflow_depth);
    return 1;
}

/* --------------------------------------------------------------------
 * Dispatch
 * -------------------------------------------------------------------- */

static void usage(void)
{
    fputs("usage: wmb_fiber_smoke --pingpong | --sync | --overflow\n", stderr);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage();
        return 1;
    }
    if (strcmp(argv[1], "--pingpong") == 0) return run_pingpong();
    if (strcmp(argv[1], "--sync")     == 0) return run_sync();
    if (strcmp(argv[1], "--overflow") == 0) return run_overflow();

    usage();
    return 1;
}
