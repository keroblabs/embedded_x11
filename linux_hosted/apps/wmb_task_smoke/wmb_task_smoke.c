/*
 * wmb_task_smoke.c - Runtime gates for the hal_task_* layer.
 *
 * Closes docs/41 section 4.3:
 *   --timeout    V-P3-1   hal_yield_timeout(50) returns 0 at 50 +- 5 ms
 *   --wake       V-P3-2   hal_yield_timeout(200) returns 1 when woken at 20 ms
 *   --early      V-P3-3   a wake issued before the matching wait is not lost
 *   --tick       V-P3-4   hal_tick_ms is monotonic (5 s by default,
 *                         WMB_TICK_SECONDS overrides)
 *   --prodcon    V-P3-5   3 fibers, 10000-iteration producer/consumer,
 *                         no lost wakeups
 *   --release    V-P3-6   W5  the release wake is NOT a data signal
 *   --coalesce   V-P3-7   W2  wakes coalesce (two wakes, first wait
 *                             consumes, second blocks)
 *   --flag       V-P3-8   W3  one flag serves both hal_task_wait and
 *                             hal_yield_timeout
 *   --recycle    V-P3-9   W7  a recycled slot starts clean
 *   --created    V-P3-10  a created-but-not-woken task does not run
 *
 * Each subcommand calls hal_task_runtime_init from scratch, so the
 * shell can run them independently under Valgrind or ASan.
 */
#include "fiber.h"
#include "hal_task.h"
#include "hal_time.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------- */

static int poll_flag(volatile int *flag, uint32_t deadline_ms)
{
    uint32_t deadline = hal_tick_ms() + deadline_ms;
    while (!*flag) {
        hal_task_yield();
        if (hal_tick_ms() > deadline) return -1;
    }
    return 0;
}

/* --------------------------------------------------------------------
 * V-P3-1: hal_yield_timeout(50) returns 0 at 50 +- 5 ms
 * -------------------------------------------------------------------- */

#define T1_ITERS   100
#define T1_TARGET  50u
#define T1_SLOP    5u

typedef struct {
    uint32_t min_ms;
    uint32_t max_ms;
    uint32_t sum_ms;
    int      wrong_rc;
    int      done;
} t1_state_t;

static void t1_entry(void *arg)
{
    t1_state_t *s = (t1_state_t *)arg;
    for (int i = 0; i < T1_ITERS; i++) {
        uint32_t start   = hal_tick_ms();
        int      rc      = hal_yield_timeout(T1_TARGET);
        uint32_t elapsed = hal_tick_ms() - start;

        if (rc != 0) { s->wrong_rc = 1; break; }
        if (elapsed < s->min_ms) s->min_ms = elapsed;
        if (elapsed > s->max_ms) s->max_ms = elapsed;
        s->sum_ms += elapsed;
    }
    s->done = 1;
}

static int run_timeout(void)
{
    hal_task_runtime_init(0);
    t1_state_t s = { UINT32_MAX, 0u, 0u, 0, 0 };

    hal_task_t *t = hal_task_create("t1", t1_entry, &s, 0);
    if (!t) { fputs("V-P3-1 FAIL: create\n", stderr); return 1; }
    hal_task_wake(t);

    if (poll_flag(&s.done, T1_ITERS * (T1_TARGET + T1_SLOP) + 500u) != 0) {
        fputs("V-P3-1 FAIL: deadline\n", stderr);
        return 1;
    }
    if (s.wrong_rc) {
        fputs("V-P3-1 FAIL: hal_yield_timeout returned non-zero\n", stderr);
        return 1;
    }
    uint32_t mean = s.sum_ms / T1_ITERS;
    int ok = (s.min_ms + T1_SLOP >= T1_TARGET) && (s.max_ms <= T1_TARGET + T1_SLOP);
    printf("V-P3-1: %d x hal_yield_timeout(%u): min=%u max=%u mean=%u ms %s\n",
           T1_ITERS, T1_TARGET, s.min_ms, s.max_ms, mean, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-2: hal_yield_timeout(200) returns 1 when woken at t=20 ms
 * -------------------------------------------------------------------- */

static volatile int g_t2_done;
static int      g_t2_rc;
static uint32_t g_t2_elapsed_ms;

static void t2_entry(void *arg)
{
    (void)arg;
    uint32_t start = hal_tick_ms();
    g_t2_rc         = hal_yield_timeout(200);
    g_t2_elapsed_ms = hal_tick_ms() - start;
    g_t2_done       = 1;
}

static int run_wake(void)
{
    hal_task_runtime_init(0);
    g_t2_done = 0;
    hal_task_t *t = hal_task_create("t2", t2_entry, NULL, 0);
    if (!t) return 1;
    hal_task_wake(t);   /* release */

    /* Wait 20 ms wall-clock then wake. */
    uint32_t start = hal_tick_ms();
    while (hal_tick_ms() - start < 20u) hal_task_yield();
    hal_task_wake(t);

    if (poll_flag(&g_t2_done, 500u) != 0) {
        fputs("V-P3-2 FAIL: deadline\n", stderr);
        return 1;
    }
    int ok = (g_t2_rc == 1) && (g_t2_elapsed_ms >= 18u) && (g_t2_elapsed_ms <= 30u);
    printf("V-P3-2: hal_yield_timeout(200) rc=%d elapsed=%u ms %s\n",
           g_t2_rc, g_t2_elapsed_ms, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-3: a wake issued before the matching wait is not lost
 * -------------------------------------------------------------------- */

static volatile int g_t3_done;
static uint32_t g_t3_elapsed_ms;

static void t3_entry(void *arg)
{
    (void)arg;
    /* pending_wake was set by main's second wake BEFORE we ran, so this
     * wait must return immediately rather than block. */
    uint32_t start = hal_tick_ms();
    hal_task_wait();
    g_t3_elapsed_ms = hal_tick_ms() - start;
    g_t3_done       = 1;
}

static int run_early(void)
{
    hal_task_runtime_init(0);
    g_t3_done = 0;
    hal_task_t *t = hal_task_create("t3", t3_entry, NULL, 0);
    if (!t) return 1;
    hal_task_wake(t);   /* release */
    hal_task_wake(t);   /* pending_wake, before the task ever runs */

    if (poll_flag(&g_t3_done, 500u) != 0) {
        fputs("V-P3-3 FAIL: deadline\n", stderr);
        return 1;
    }
    int ok = (g_t3_elapsed_ms <= 5u);
    printf("V-P3-3: wait after early wake returned in %u ms %s\n",
           g_t3_elapsed_ms, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-4: hal_tick_ms is monotonic
 * -------------------------------------------------------------------- */

static int run_tick(void)
{
    hal_task_runtime_init(0);

    uint32_t seconds = 5u;
    const char *env = getenv("WMB_TICK_SECONDS");
    if (env) {
        long v = strtol(env, NULL, 10);
        if (v > 0 && v < 3600) seconds = (uint32_t)v;
    }

    uint32_t start   = hal_tick_ms();
    uint32_t prev    = start;
    uint64_t samples = 0u;
    while (hal_tick_ms() - start < seconds * 1000u) {
        uint32_t now = hal_tick_ms();
        if (now < prev) {
            printf("V-P3-4 FAIL: tick went backwards: %u -> %u\n", prev, now);
            return 1;
        }
        prev = now;
        samples++;
    }
    printf("V-P3-4: %llu samples over %u s, monotonic PASS "
           "(doc gate is 60 s; set WMB_TICK_SECONDS=60 for full run)\n",
           (unsigned long long)samples, seconds);
    return 0;
}

/* --------------------------------------------------------------------
 * V-P3-5: 3-fiber producer/consumer, 10000 iterations, no lost wakeups
 * -------------------------------------------------------------------- */

#define PC_ITERS  10000
#define PC_QMASK  15u   /* 16-slot ring */

static int             g_pc_queue[16];
static volatile int    g_pc_head, g_pc_tail;
static volatile int    g_pc_done;
static int             g_pc_error;
static hal_task_t     *g_pc_producer, *g_pc_consumer;

static void pc_producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < PC_ITERS; i++) {
        while (((g_pc_tail + 1) & PC_QMASK) == (uint32_t)g_pc_head) {
            hal_task_yield();
        }
        g_pc_queue[g_pc_tail] = i;
        g_pc_tail = (g_pc_tail + 1) & PC_QMASK;
        hal_task_wake(g_pc_consumer);
    }
}

static void pc_consumer(void *arg)
{
    (void)arg;
    for (int i = 0; i < PC_ITERS; i++) {
        while (g_pc_head == g_pc_tail) {
            hal_task_wait();
        }
        int v = g_pc_queue[g_pc_head];
        g_pc_head = (g_pc_head + 1) & PC_QMASK;
        if (v != i) g_pc_error = 1;
    }
    g_pc_done = 1;
}

static int run_prodcon(void)
{
    hal_task_runtime_init(0);
    g_pc_head = g_pc_tail = 0;
    g_pc_done = 0;
    g_pc_error = 0;

    g_pc_consumer = hal_task_create("consumer", pc_consumer, NULL, 0);
    g_pc_producer = hal_task_create("producer", pc_producer, NULL, 0);
    if (!g_pc_producer || !g_pc_consumer) return 1;
    hal_task_wake(g_pc_consumer);
    hal_task_wake(g_pc_producer);

    if (poll_flag(&g_pc_done, 30u * 1000u) != 0) {
        fputs("V-P3-5 FAIL: deadline\n", stderr);
        return 1;
    }
    int ok = !g_pc_error;
    printf("V-P3-5: %d items produced/consumed, order intact %s\n",
           PC_ITERS, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-6 (W5): the release wake is not a data signal
 *
 * A task whose entry is hal_yield_timeout(P), woken exactly once at
 * create time, must observe its first return at t ~= P.  If the release
 * wake had left a pending_wake set, hal_yield_timeout would return at
 * t ~= 0.
 * -------------------------------------------------------------------- */

static volatile int g_t6_done;
static uint32_t     g_t6_first_toggle_ms;
static uint32_t     g_t6_start_ms;

static void t6_entry(void *arg)
{
    uint32_t p = (uint32_t)(uintptr_t)arg;
    hal_yield_timeout(p);
    g_t6_first_toggle_ms = hal_tick_ms() - g_t6_start_ms;
    g_t6_done = 1;
}

static int run_release(void)
{
    const uint32_t P = 100u;
    hal_task_runtime_init(0);
    g_t6_done = 0;

    hal_task_t *t = hal_task_create("t6", t6_entry,
                                    (void *)(uintptr_t)P, 0);
    if (!t) return 1;

    g_t6_start_ms = hal_tick_ms();
    hal_task_wake(t);   /* one wake; must be release only, not signal */

    if (poll_flag(&g_t6_done, 2u * P + 200u) != 0) {
        fputs("V-P3-6 FAIL: deadline\n", stderr);
        return 1;
    }
    int ok = (g_t6_first_toggle_ms + 10u >= P)
          && (g_t6_first_toggle_ms <= P + 20u);
    printf("V-P3-6 W5: first toggle at %u ms (expected ~%u) %s\n",
           g_t6_first_toggle_ms, P, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-7 (W2): wakes coalesce
 *
 * Two wakes register as one pending_wake.  The first wait consumes it;
 * the second must block until an explicit third wake unblocks it.
 * -------------------------------------------------------------------- */

static volatile int g_t7_stage;   /* 0 init, 1 first wait done, 2 second wait done */
static uint32_t     g_t7_second_elapsed_ms;

static void t7_entry(void *arg)
{
    (void)arg;
    hal_task_wait();          /* consumes the (coalesced) pending_wake */
    g_t7_stage = 1;
    uint32_t start = hal_tick_ms();
    hal_task_wait();          /* must BLOCK */
    g_t7_second_elapsed_ms = hal_tick_ms() - start;
    g_t7_stage = 2;
}

static int run_coalesce(void)
{
    hal_task_runtime_init(0);
    g_t7_stage = 0;

    hal_task_t *t = hal_task_create("t7", t7_entry, NULL, 0);
    if (!t) return 1;
    hal_task_wake(t);   /* release */
    hal_task_wake(t);   /* pending_wake = 1 */
    hal_task_wake(t);   /* still 1 - coalesced */

    /* Wait until task consumes the first wait. */
    uint32_t deadline = hal_tick_ms() + 200u;
    while (g_t7_stage < 1) {
        hal_task_yield();
        if (hal_tick_ms() > deadline) {
            fputs("V-P3-7 FAIL: first wait deadline\n", stderr);
            return 1;
        }
    }

    /* Sleep 100 ms.  If W2 holds, task's second wait is blocked. */
    uint32_t sleep_start = hal_tick_ms();
    while (hal_tick_ms() - sleep_start < 100u) hal_task_yield();
    if (g_t7_stage >= 2) {
        printf("V-P3-7 W2 FAIL: second wait returned prematurely at %u ms\n",
               g_t7_second_elapsed_ms);
        return 1;
    }

    /* Explicit unblock. */
    hal_task_wake(t);
    deadline = hal_tick_ms() + 500u;
    while (g_t7_stage < 2) {
        hal_task_yield();
        if (hal_tick_ms() > deadline) {
            fputs("V-P3-7 FAIL: unblock deadline\n", stderr);
            return 1;
        }
    }

    int ok = (g_t7_second_elapsed_ms >= 95u);
    printf("V-P3-7 W2: second wait blocked for %u ms %s\n",
           g_t7_second_elapsed_ms, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-8 (W3): one flag serves both waits
 *
 * A single hal_task_wake, followed by hal_yield_timeout(1000), must
 * return 1 promptly - the same pending_wake flag consumed by
 * hal_task_wait is consumed here too.
 * V-P3-3 already exercises the hal_task_wait side of the pair.
 * -------------------------------------------------------------------- */

static volatile int g_t8_done;
static int      g_t8_rc;
static uint32_t g_t8_elapsed_ms;

static void t8_entry(void *arg)
{
    (void)arg;
    uint32_t start = hal_tick_ms();
    g_t8_rc         = hal_yield_timeout(1000);
    g_t8_elapsed_ms = hal_tick_ms() - start;
    g_t8_done       = 1;
}

static int run_flag(void)
{
    hal_task_runtime_init(0);
    g_t8_done = 0;

    hal_task_t *t = hal_task_create("t8", t8_entry, NULL, 0);
    if (!t) return 1;
    hal_task_wake(t);   /* release */
    hal_task_wake(t);   /* pending_wake before task runs */

    if (poll_flag(&g_t8_done, 1500u) != 0) {
        fputs("V-P3-8 FAIL: deadline\n", stderr);
        return 1;
    }
    int ok = (g_t8_rc == 1) && (g_t8_elapsed_ms <= 5u);
    printf("V-P3-8 W3: wake+hal_yield_timeout(1000) rc=%d elapsed=%u ms %s\n",
           g_t8_rc, g_t8_elapsed_ms, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-9 (W7): a recycled slot starts clean
 *
 * Create A, wake A twice (release + pending_wake), let A consume its
 * wait and exit.  Destroy A.  Create B - which reuses A's slot.  Wake B
 * ONCE (release only).  B's first wait must block; if the recycled slot
 * had leaked A's pending_wake, B would fall through instantly.
 * -------------------------------------------------------------------- */

static volatile int g_t9a_done;
static volatile int g_t9b_stage;   /* 0 init, 1 in wait, 2 wait returned */
static uint32_t     g_t9b_elapsed_ms;

static void t9a_entry(void *arg)
{
    (void)arg;
    hal_task_wait();    /* consumes pending_wake */
    g_t9a_done = 1;
}

static void t9b_entry(void *arg)
{
    (void)arg;
    g_t9b_stage = 1;
    uint32_t start = hal_tick_ms();
    hal_task_wait();    /* MUST block */
    g_t9b_elapsed_ms = hal_tick_ms() - start;
    g_t9b_stage = 2;
}

static int run_recycle(void)
{
    hal_task_runtime_init(0);
    g_t9a_done = 0;
    g_t9b_stage = 0;

    hal_task_t *a = hal_task_create("t9a", t9a_entry, NULL, 0);
    if (!a) return 1;
    hal_task_wake(a);   /* release */
    hal_task_wake(a);   /* pending_wake */

    if (poll_flag(&g_t9a_done, 200u) != 0) {
        fputs("V-P3-9 FAIL: A didn't finish\n", stderr);
        return 1;
    }
    hal_task_destroy(a);
    hal_task_yield();   /* let scheduler reclaim ZOMBIE + slot */

    hal_task_t *b = hal_task_create("t9b", t9b_entry, NULL, 0);
    if (!b) return 1;
    hal_task_wake(b);   /* release ONLY - no pending_wake */

    uint32_t deadline = hal_tick_ms() + 200u;
    while (g_t9b_stage < 1) {
        hal_task_yield();
        if (hal_tick_ms() > deadline) {
            fputs("V-P3-9 FAIL: B didn't reach wait\n", stderr);
            return 1;
        }
    }

    uint32_t sleep_start = hal_tick_ms();
    while (hal_tick_ms() - sleep_start < 100u) hal_task_yield();
    if (g_t9b_stage >= 2) {
        printf("V-P3-9 W7 FAIL: recycled slot leaked pending_wake\n");
        return 1;
    }

    hal_task_wake(b);
    deadline = hal_tick_ms() + 500u;
    while (g_t9b_stage < 2) {
        hal_task_yield();
        if (hal_tick_ms() > deadline) {
            fputs("V-P3-9 FAIL: unblock deadline\n", stderr);
            return 1;
        }
    }

    int ok = (g_t9b_elapsed_ms >= 95u);
    printf("V-P3-9 W7: recycled slot blocked for %u ms %s\n",
           g_t9b_elapsed_ms, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* --------------------------------------------------------------------
 * V-P3-10: creation state is CREATED
 *
 * A created-but-not-woken task must not execute across many scheduler
 * rounds.  Doc uses "20 rounds" as the threshold.
 * -------------------------------------------------------------------- */

static volatile int g_t10_toggled;

static void t10_entry(void *arg)
{
    (void)arg;
    g_t10_toggled = 1;
}

static int run_created(void)
{
    hal_task_runtime_init(0);
    g_t10_toggled = 0;

    hal_task_t *t = hal_task_create("t10", t10_entry, NULL, 0);
    if (!t) return 1;

    for (int i = 0; i < 20; i++) hal_task_yield();
    if (g_t10_toggled) {
        printf("V-P3-10 FAIL: task ran without hal_task_wake\n");
        return 1;
    }

    hal_task_wake(t);
    if (poll_flag(&g_t10_toggled, 200u) != 0) {
        fputs("V-P3-10 FAIL: task didn't run after wake\n", stderr);
        return 1;
    }
    printf("V-P3-10: task blocked at creation, ran after wake PASS\n");
    return 0;
}

/* --------------------------------------------------------------------
 * Dispatch
 * -------------------------------------------------------------------- */

static const struct {
    const char *flag;
    int (*run)(void);
} g_commands[] = {
    { "--timeout",  run_timeout  },
    { "--wake",     run_wake     },
    { "--early",    run_early    },
    { "--tick",     run_tick     },
    { "--prodcon",  run_prodcon  },
    { "--release",  run_release  },
    { "--coalesce", run_coalesce },
    { "--flag",     run_flag     },
    { "--recycle",  run_recycle  },
    { "--created",  run_created  },
};

static void usage(void)
{
    fputs("usage: wmb_task_smoke <flag>\n"
          "flags:\n"
          "  --timeout    V-P3-1   hal_yield_timeout(50) at 50 +- 5 ms\n"
          "  --wake       V-P3-2   hal_yield_timeout returns 1 when woken\n"
          "  --early      V-P3-3   wake before wait is not lost\n"
          "  --tick       V-P3-4   hal_tick_ms monotonic\n"
          "  --prodcon    V-P3-5   producer/consumer, 10000 items\n"
          "  --release    V-P3-6   W5: release wake is not a data signal\n"
          "  --coalesce   V-P3-7   W2: wakes coalesce\n"
          "  --flag       V-P3-8   W3: one flag serves both waits\n"
          "  --recycle    V-P3-9   W7: recycled slot starts clean\n"
          "  --created    V-P3-10  creation state is BLOCKED\n",
          stderr);
}

int main(int argc, char **argv)
{
    if (argc != 2) { usage(); return 1; }
    for (size_t i = 0; i < sizeof(g_commands) / sizeof(g_commands[0]); i++) {
        if (strcmp(argv[1], g_commands[i].flag) == 0) {
            return g_commands[i].run();
        }
    }
    usage();
    return 1;
}
