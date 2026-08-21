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
#include <sys/mman.h>
#include <unistd.h>

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

/* --------------------------------------------------------------------
 * Stack allocation with a guard page
 * --------------------------------------------------------------------
 * Layout returned to the generic layer:
 *
 *   low addr  [ guard page (PROT_NONE)           ]   <- overflow SIGSEGVs here
 *             [ usable stack, rounded up to page ]
 *   high addr [ (return + size)                  ]
 *
 * Stack grows down on every architecture the hosted port supports, so
 * an overflow sees the guard page before it corrupts anything else.
 * -------------------------------------------------------------------- */

static size_t port_page_size(void)
{
    static size_t cached = 0;
    if (cached == 0u) {
        long ps = sysconf(_SC_PAGESIZE);
        cached  = (ps > 0) ? (size_t)ps : 4096u;
    }
    return cached;
}

static size_t port_stack_map_size(size_t size)
{
    size_t page    = port_page_size();
    size_t rounded = (size + (page - 1u)) & ~(page - 1u);
    return rounded + page;      /* usable + one guard page */
}

void *fiber_port_alloc_stack(size_t size)
{
    if (size == 0u) return NULL;

    size_t page  = port_page_size();
    size_t total = port_stack_map_size(size);

    void *base = mmap(NULL, total,
                      PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (base == MAP_FAILED) {
        return NULL;
    }
    if (mprotect(base, page, PROT_NONE) != 0) {
        (void)munmap(base, total);
        return NULL;
    }
    return (void *)((unsigned char *)base + page);
}

void fiber_port_free_stack(void *stack, size_t size)
{
    if (stack == NULL) return;

    size_t page  = port_page_size();
    size_t total = port_stack_map_size(size);
    void  *base  = (void *)((unsigned char *)stack - page);
    (void)munmap(base, total);
}
