/*
 * fiber_port.c - Linux hosted fiber port built on libucontext.
 *
 * Implements the fiber_port.h contract of framework/fiber on a hosted
 * Linux process.  The port is COOPERATIVE ONLY: no timer and no signal
 * source is installed, so a context switch happens exactly where the
 * application asks for one (fiber_yield(), fiber_destroy(self) or a
 * blocking fiber_sync primitive).  fiber_tick_isr() is therefore never
 * called and preemption never fires.
 *
 * "sp" mapping: the generic layer keeps one opaque word per fiber
 * (fiber_t::sp) that it only ever hands back to the port.  Here that
 * word points at the fiber's machine context, which is carved out of
 * the top of the fiber's own stack buffer so it is reclaimed together
 * with the stack.
 */
#include "fiber.h"
#include "fiber_port.h"
#include "fiber_config.h"
#include "intr_handler.h"

#include <stddef.h>
#include <string.h>

#if defined(FIBER_USE_LIBUCONTEXT)
#include <libucontext/libucontext.h>
typedef libucontext_ucontext_t   port_uctx_t;
#define PORT_GETCONTEXT          libucontext_getcontext
#define PORT_MAKECONTEXT         libucontext_makecontext
#define PORT_SWAPCONTEXT         libucontext_swapcontext
#else
#include <ucontext.h>
typedef ucontext_t               port_uctx_t;
#define PORT_GETCONTEXT          getcontext
#define PORT_MAKECONTEXT         makecontext
#define PORT_SWAPCONTEXT         swapcontext
#endif

/* makecontext() derives the initial stack pointer from
 * ss_sp + ss_size and reads nothing else, so this window is purely
 * descriptive - the authoritative stack bounds stay with the buffer
 * the generic layer allocated.                                       */
#define PORT_UC_STACK_HINT       ((size_t)FIBER_DEFAULT_STACK)

/* Context of whichever thread of execution called
 * fiber_runtime_init(); i.e. the main fiber.                         */
static port_uctx_t   g_boot_uctx;
static port_uctx_t  *g_current_uctx = &g_boot_uctx;

/* --------------------------------------------------------------------
 * Port contract
 * -------------------------------------------------------------------- */

void fiber_port_init(uint32_t period_ms)
{
    /* Ignored by decision: docs/90-decisions.md D-47 holds the tick
     * disabled until preconditions R1-R4 are green. */
    (void)period_ms;

    memset(&g_boot_uctx, 0, sizeof(g_boot_uctx));
    g_current_uctx = &g_boot_uctx;
}

void *fiber_port_init_stack(void *stack_top, fiber_func_t entry, void *arg)
{
    /* entry/arg are unused: fiber_trampoline() reads them back from the
     * FCB of whichever fiber the scheduler dispatched.               */
    (void)entry;
    (void)arg;

    uintptr_t top = (uintptr_t)stack_top & ~(uintptr_t)(FIBER_PORT_STACK_ALIGN - 1u);
    top -= sizeof(port_uctx_t);
    top &= ~(uintptr_t)(FIBER_PORT_STACK_ALIGN - 1u);

    port_uctx_t *uc = (port_uctx_t *)top;
    memset(uc, 0, sizeof(*uc));
    if (PORT_GETCONTEXT(uc) != 0) {
        return NULL;
    }

    uc->uc_stack.ss_sp    = (void *)(top - PORT_UC_STACK_HINT);
    uc->uc_stack.ss_size  = PORT_UC_STACK_HINT;
    uc->uc_stack.ss_flags = 0;
    uc->uc_link           = NULL;   /* fiber_trampoline never returns */

    PORT_MAKECONTEXT(uc, (void (*)(void))fiber_trampoline, 0);
    return uc;
}

void fiber_port_request_switch(void)
{
    port_uctx_t *cur  = g_current_uctx;
    port_uctx_t *next = (port_uctx_t *)fiber_sched_swap(cur);

    if (next == NULL || next == cur) {
        return;                 /* scheduler re-elected the caller */
    }

    g_current_uctx = next;
    (void)PORT_SWAPCONTEXT(cur, next);
    /* Reached again once someone swaps back into this fiber. */
}

uint32_t fiber_port_irq_lock(void)
{
    uint32_t prev = (uint32_t)intr_global_is_enabled();
    intr_global_set_enabled(FALSE);
    return prev;
}

void fiber_port_irq_unlock(uint32_t prev)
{
    intr_global_set_enabled(prev ? TRUE : FALSE);
}
