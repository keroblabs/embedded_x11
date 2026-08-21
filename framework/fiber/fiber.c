/*
 * fiber.c - Generic, portable fiber runtime.
 *
 * Owns: FCB pool, the circular ring, the scheduler, create/destroy
 * semantics, ZOMBIE reclamation, stack-canary checks.  Knows nothing
 * about CPU registers or interrupt controllers - all that lives
 * behind fiber_port.h.
 */
#include "fiber.h"
#include "fiber_port.h"
#include "fiber_config.h"
#include "fiber_internal.h"
#include "intr_handler.h"
#include "hal_time.h"

#include <stddef.h>
#include <string.h>

/* --------------------------------------------------------------------
 * Internal types
 * --------------------------------------------------------------------
 * struct fiber and fiber_state_t live in fiber_internal.h so the
 * synchronization layer (fiber_sync.c) can see them.
 * -------------------------------------------------------------------- */

/* --------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------- */
static struct fiber  fcb_pool[FIBER_MAX];
static struct fiber *g_current = NULL;
static struct fiber *g_main    = NULL;

/* Preemption tick state: switch is requested every g_preemption_ticks
 * invocations of fiber_tick_isr() (each tick = 1 ms).               */
static uint32_t      g_preemption_ticks = 1;
static uint32_t      g_tick_counter     = 0;

/* Critical section nesting depth.  When > 0, preemptive switches are
 * suppressed and interrupts remain masked (the hardware critical
 * section from fiber_port_irq_lock).                                 */
static volatile uint32_t g_critical_nesting = 0;
static uint32_t          g_critical_saved   = 0;  /* IRQ state from first lock */

/* Guards a predicate that calls fiber_notify() back. */
static uint8_t       g_in_notify       = 0u;

/* Non-NULL while fiber_notify() evaluates a predicate: a predicate runs
 * on behalf of the parked fiber, so fiber_self() must report that fiber
 * and not the notifier.  Without this a test-and-acquire predicate
 * claims the resource for the wrong fiber. */
static struct fiber *g_predicate_fiber = NULL;

/* --------------------------------------------------------------------
 * FCB pool helpers (called under irq_lock)
 * -------------------------------------------------------------------- */
static struct fiber *fcb_alloc_locked(void)
{
    for (int i = 0; i < FIBER_MAX; i++) {
        if (fcb_pool[i].state == FIBER_STATE_FREE) {
            uint32_t gen = fcb_pool[i].generation + 1u;
            memset(&fcb_pool[i], 0, sizeof(fcb_pool[i]));
            fcb_pool[i].generation = gen;
            fcb_pool[i].state = FIBER_STATE_READY;
            return &fcb_pool[i];
        }
    }
    return NULL;
}

static void fcb_release_locked(struct fiber *f)
{
    f->state = FIBER_STATE_FREE;
    f->next  = NULL;
}
/* Splice `f` immediately after `prev` in the circular ring. */
static void ring_insert_after_locked(struct fiber *prev, struct fiber *f)
{
    f->next    = prev->next;
    prev->next = f;
}

/* Remove `f` from the ring.  Assumes ring has >= 2 nodes (caller
 * guarantees: main is never destroyed). */
static void ring_unlink_locked(struct fiber *f)
{
    struct fiber *p = f->next;
    while (p->next != f) p = p->next;
    p->next = f->next;
}

/* --------------------------------------------------------------------
 * Reclaim - called by the scheduler tail while walking the ring.
 * Only the scheduler frees a ZOMBIE's stack, never the destroyer
 * (architecture-doc invariant 9.3).
 * -------------------------------------------------------------------- */
static void zombie_reclaim_locked(struct fiber *z)
{
    if (z->owns_stack && z->stack_base) {
        fiber_port_free_stack(z->stack_base, z->stack_size);
    }
    ring_unlink_locked(z);
    fcb_release_locked(z);
}

/* --------------------------------------------------------------------
 * Scheduler tail.  Called by the arch SWI/PendSV handler with the
 * outgoing fiber's stacked-frame SP.  Returns the incoming fiber's SP.
 *
 * Runs with interrupts disabled (the IRQ entry guarantees that).
 * -------------------------------------------------------------------- */
void *fiber_sched_swap(void *cur_sp)
{
    struct fiber *cur = g_current;

    cur->sp = cur_sp;
    if (cur->state == FIBER_STATE_RUNNING) {
        cur->state = FIBER_STATE_READY;
    }

    /* Canary check on the outgoing fiber (skip main - no canary). */
    if (cur->canary && *(cur->canary) != FIBER_CANARY) {
        intr_handler_call_crash((uint32_t)FIBER_ERR_OVERFLOW);
    }

    /* Walk the ring picking the next runnable (READY or RUNNING).
     * Reclaim ZOMBIEs we pass; skip BLOCKED fibers in place since
     * a synchronization wake will flip them back to READY.        */
    struct fiber *next = cur->next;
    uint32_t now_ms   = 0u;
    int      have_now = 0;

    for (;;) {
        if (next->state == FIBER_STATE_ZOMBIE) {
            struct fiber *dead = next;
            next = next->next;
            zombie_reclaim_locked(dead);
            continue;
        }
        if (next->state == FIBER_STATE_BLOCKED) {
            /* The only work the scheduler does for a parked predicate:
             * an integer compare.  fiber_notify() runs the callbacks. */
            if (next->wait_timed) {
                if (!have_now) { now_ms = hal_tick_ms(); have_now = 1; }
                if ((int32_t)(now_ms - next->wait_deadline) >= 0) {
                    next->wait_cb      = NULL;
                    next->wait_timed   = 0u;
                    next->wait_expired = 1u;
                    next->state        = FIBER_STATE_READY;
                    break;
                }
            }
            next = next->next;
            continue;
        }
        break;   /* READY or RUNNING (only the outgoing fiber, if
                  * everyone else is blocked - main is guaranteed
                  * runnable by sync-API invariant 12.A).          */
    }

    next->state = FIBER_STATE_RUNNING;
    g_current   = next;
    return next->sp;
}

/* --------------------------------------------------------------------
 * Trampoline - every newly-created fiber starts here.
 * -------------------------------------------------------------------- */
void fiber_trampoline(void)
{
    /* The forged frame leaves interrupts in an arch-dependent state
     * (RV32 mret may restore stale MPIE; ARM PendSV exits with
     * PRIMASK preserved).  Force them on so the fiber can be
     * preempted by the next timer tick.                          */
    intr_global_set_enabled(TRUE);

    struct fiber *self = g_current;
    if (self->entry) {
        self->entry(self->arg);
    }

    /* Natural return == self-terminate.  Wake every joiner before
     * pending the final switch so they observe ZOMBIE the moment
     * they next run.                                              */
    {
        uint32_t prev = fiber_port_irq_lock();
        self->state = FIBER_STATE_ZOMBIE;
        fiber_port_irq_unlock(prev);
    }
    fiber_notify();             /* release anyone joined on us */
    fiber_port_request_switch();
    /* The next dispatch will skip us, reclaim our stack, and
     * never return here.  Spin defensively if it does.          */
    for (;;) { }
}

/* --------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------- */

fiber_t *fiber_self(void) { return (g_predicate_fiber != NULL) ? g_predicate_fiber : g_current; }
fiber_t *fiber_main(void) { return g_main;    }
bool_t   fiber_is_main(const fiber_t *f) { return (f == g_main) ? TRUE : FALSE; }

void fiber_runtime_init(uint32_t preemption_ms)
{
    /* Bootstrap the FCB pool. */
    memset(fcb_pool, 0, sizeof(fcb_pool));

    struct fiber *m = &fcb_pool[0];
    m->sp         = NULL;          /* filled by first switch-out */
    m->stack_base = NULL;
    m->stack_size = 0;
    m->canary     = NULL;          /* main runs on the boot stack */
    m->entry      = NULL;
    m->arg        = NULL;
    m->state      = FIBER_STATE_RUNNING;
    m->is_main    = 1;
    m->owns_stack = 0;
    m->next       = m;             /* ring of one */

    g_main    = m;
    g_current = m;

    /* Clamp to at least 1 ms to avoid zero-divide edge case. */
    g_preemption_ticks = (preemption_ms > 0u) ? preemption_ms : 1u;
    g_tick_counter     = 0;

    fiber_port_init(preemption_ms);
}

/* Common allocation path used by both create variants. */
static fiber_t *fiber_create_common(fiber_func_t func, void *arg,
                                    void *stack_buf, size_t stack_size,
                                    uint8_t owns_stack)
{
    if (!func || !stack_buf || stack_size < 256u) {
        return NULL;
    }
    if (((uintptr_t)stack_buf & (FIBER_PORT_STACK_ALIGN - 1u)) != 0u) {
        return NULL;
    }

    uint32_t prev = fiber_port_irq_lock();
    struct fiber *f = fcb_alloc_locked();
    if (!f) {
        fiber_port_irq_unlock(prev);
        return NULL;
    }

    f->stack_base = stack_buf;
    f->stack_size = stack_size;
    f->canary     = (uint32_t *)stack_buf;
    *f->canary    = FIBER_CANARY;
    f->entry      = func;
    f->arg        = arg;
    f->state      = FIBER_STATE_READY;
    f->owns_stack = owns_stack;

    /* Top of stack = highest address, rounded DOWN to port alignment. */
    uintptr_t top = (uintptr_t)stack_buf + stack_size;
    top &= ~(uintptr_t)(FIBER_PORT_STACK_ALIGN - 1u);
    f->sp = fiber_port_init_stack((void *)top, func, arg);

    ring_insert_after_locked(g_current, f);
    fiber_port_irq_unlock(prev);
    return f;
}

fiber_t *fiber_create(fiber_func_t func, void *arg, size_t stack_size)
{
    if (stack_size == 0u) stack_size = FIBER_DEFAULT_STACK;

    void *stack = fiber_port_alloc_stack(stack_size);
    if (!stack) return NULL;

    fiber_t *f = fiber_create_common(func, arg, stack, stack_size, 1u);
    if (!f) {
        fiber_port_free_stack(stack, stack_size);
        return NULL;
    }
    return f;
}

fiber_t *fiber_create_static(fiber_func_t func, void *arg,
                             void *stack_buf, size_t stack_size)
{
    return fiber_create_common(func, arg, stack_buf, stack_size, 0u);
}

void fiber_yield(void)
{
    /* Pend the deferred switch.  On RV32 the SWI fires as soon as
     * MIE is on; on ARMv6-M PendSV runs at the lowest priority
     * after the BL/return.  Either way, control returns here only
     * after we've been scheduled in again.                       */
    fiber_port_request_switch();
}

int fiber_destroy(fiber_t *target)
{
    if (!target)              return FIBER_ERR_INVALID;
    if (target->is_main)      return FIBER_ERR_IMMORTAL;

    uint32_t prev = fiber_port_irq_lock();
    if (target->state == FIBER_STATE_FREE) {
        fiber_port_irq_unlock(prev);
        return FIBER_ERR_INVALID;
    }
    target->state = FIBER_STATE_ZOMBIE;
    target->wait_cb    = NULL;
    target->wait_timed = 0u;
    fiber_port_irq_unlock(prev);

    fiber_notify();             /* release anyone joined on target */

    if (target == g_current) {
        /* Self-terminate: yield, never to return. */
        fiber_port_request_switch();
        for (;;) { }
    }
    return FIBER_OK;
}

/* --------------------------------------------------------------------
 * Predicate wait
 *
 * A parked fiber costs the scheduler one integer compare per pass.
 * Predicates are evaluated only by fiber_notify(), on the notifier's
 * stack, so no application callback ever runs from the switch path.
 * -------------------------------------------------------------------- */

static bool_t predicate_holds(check_cb_t cb, void *data)
{
    bool_t   held;
    uint32_t prev = fiber_port_irq_lock();

    held = cb(data);
    fiber_port_irq_unlock(prev);

    return held;
}

bool_t fiber_wait_for(check_cb_t check_cb, void *data, uint32_t timeout_ms)
{
    struct fiber *self;
    uint32_t      prev;
    bool_t        satisfied;

    if (check_cb == NULL) {
        return FALSE;
    }

    /* Evaluate up front: if the producer already acted there is nothing
     * to wait for, and therefore no notify that could have been missed. */
    if (predicate_holds(check_cb, data) == TRUE) {
        return TRUE;
    }
    if (timeout_ms == 0u) {
        return FALSE;      /* poll-once: legal inside a critical section */
    }
    /* Parking with interrupts masked would deadlock the runtime. */
    if (fiber_internal_in_critical() == TRUE) {
        return FALSE;
    }

    self = g_current;

    /* Main must stay runnable or the scheduler has nothing to run, so
     * it polls rather than parking. */
    if (self->is_main) {
        uint32_t start_ms = hal_tick_ms();

        for (;;) {
            fiber_yield();

            if (predicate_holds(check_cb, data) == TRUE) {
                return TRUE;
            }
            if ((timeout_ms != FIBER_WAIT_FOREVER) &&
                ((hal_tick_ms() - start_ms) >= timeout_ms)) {
                return FALSE;
            }
        }
    }

    prev = fiber_port_irq_lock();
    self->wait_cb      = check_cb;
    self->wait_data    = data;
    self->wait_expired = 0u;
    if (timeout_ms == FIBER_WAIT_FOREVER) {
        self->wait_timed = 0u;
    } else {
        self->wait_timed    = 1u;
        self->wait_deadline = hal_tick_ms() + timeout_ms;
    }
    fiber_internal_block_self_locked(prev);      /* drops the lock */

    prev = fiber_port_irq_lock();
    self->wait_cb    = NULL;
    self->wait_timed = 0u;
    satisfied        = (self->wait_expired == 0u) ? TRUE : FALSE;
    fiber_port_irq_unlock(prev);

    return satisfied;
}

void fiber_notify(void)
{
    uint32_t prev;
    int i;

    if (g_main == NULL) {
        return;
    }

    prev = fiber_port_irq_lock();

    if (g_in_notify) {              /* re-entered from a predicate */
        fiber_port_irq_unlock(prev);
        return;
    }
    g_in_notify = 1u;

    for (i = 0; i < FIBER_MAX; i++) {
        struct fiber *f = &fcb_pool[i];

        if ((f->state != FIBER_STATE_BLOCKED) || (f->wait_cb == NULL)) {
            continue;
        }

        g_predicate_fiber = f;
        if (f->wait_cb(f->wait_data) == TRUE) {
            f->wait_cb      = NULL;
            f->wait_timed   = 0u;
            f->wait_expired = 0u;
            f->state        = FIBER_STATE_READY;
        }
        g_predicate_fiber = NULL;
    }

    g_in_notify = 0u;
    fiber_port_irq_unlock(prev);
}

/* --------------------------------------------------------------------
 * Critical section - nestable preemption + IRQ suppression
 * -------------------------------------------------------------------- */
void fiber_critical_enter(void)
{
    uint32_t prev = fiber_port_irq_lock();
    if (g_critical_nesting == 0u) {
        g_critical_saved = prev;   /* remember state at outermost lock */
    }
    ++g_critical_nesting;
    /* Interrupts stay masked until the outermost exit. */
}

void fiber_critical_exit(void)
{
    /* Defensive: exit without matching enter is a no-op. */
    if (g_critical_nesting == 0u) {
        return;
    }
    --g_critical_nesting;
    if (g_critical_nesting == 0u) {
        fiber_port_irq_unlock(g_critical_saved);
    }
}

/* --------------------------------------------------------------------
 * Helpers exported to fiber_sync.c (declared in fiber_internal.h).
 *
 * All "_locked" helpers assume the caller already holds
 * fiber_port_irq_lock.  block_self_locked drops that lock as it
 * yields the CPU; it returns once this fiber is re-scheduled, with
 * interrupts unlocked at the unblocker's expense.
 * -------------------------------------------------------------------- */
void fiber_internal_block_self_locked(uint32_t saved_irq)
{
    g_current->state = FIBER_STATE_BLOCKED;
    fiber_port_irq_unlock(saved_irq);
    fiber_port_request_switch();
}

void fiber_internal_wake_locked(struct fiber *f)
{
    if (f == NULL || f->state != FIBER_STATE_BLOCKED) {
        return;
    }
    f->state = FIBER_STATE_READY;
}

bool_t fiber_internal_in_critical(void)
{
    return (g_critical_nesting > 0u) ? TRUE : FALSE;
}

/* --------------------------------------------------------------------
 * Tick hook - overrides the weak symbol in arch/drivers/timer_driver.c
 * so the existing 1 ms timer_irq drives preemption without us having
 * to allocate a second timer channel.
 * -------------------------------------------------------------------- */
void fiber_tick_isr(void)
{
    if (g_main == NULL) {
        return;   /* runtime not yet initialised */
    }
    /* Suppress preemptive switch while inside a critical section. */
    if (g_critical_nesting > 0u) {
        return;
    }
    if (++g_tick_counter >= g_preemption_ticks) {
        g_tick_counter = 0;
        fiber_port_request_switch();
    }
}
