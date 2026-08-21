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

/* Detect an AddressSanitizer build.  clang exposes __has_feature;
 * gcc predefines __SANITIZE_ADDRESS__.  Under either, ASan needs to
 * be told about every fiber switch so its shadow memory follows the
 * running context.  The overhead is two calls per switch and the
 * cost of storing three extra words per fiber; both are zero when
 * ASan is off. */
#if defined(__SANITIZE_ADDRESS__)
# define WMB_ASAN_FIBER 1
#elif defined(__has_feature)
# if __has_feature(address_sanitizer)
#  define WMB_ASAN_FIBER 1
# endif
#endif

#ifdef WMB_ASAN_FIBER
extern void __sanitizer_start_switch_fiber(void **fake_stack_save,
                                            const void *bottom,
                                            size_t size);
extern void __sanitizer_finish_switch_fiber(void *fake_stack_save,
                                             const void **bottom_old,
                                             size_t *size_old);
#endif

/* Per-fiber port state.  uctx MUST be the first member: pointers to
 * port_ctx_t are handed back to fiber.c as opaque `sp` values and are
 * fed to getcontext / makecontext / swapcontext through &uctx. */
typedef struct port_ctx {
    port_uctx_t uctx;
    void       *asan_fake_stack;
    const void *stack_bottom;
    size_t      stack_size;
} port_ctx_t;

/* Context of whichever thread of execution called
 * fiber_runtime_init(); i.e. the main fiber.  Its stack_bottom and
 * stack_size stay zero: the OS-provided stack has no bounds we can
 * cheaply derive, and ASan tolerates the unknown-bounds case for the
 * context it started in. */
static port_ctx_t   g_boot_ctx;
static port_ctx_t  *g_current_ctx = &g_boot_ctx;

/* --------------------------------------------------------------------
 * Port contract
 * -------------------------------------------------------------------- */

void fiber_port_init(uint32_t period_ms)
{
    /* Ignored by decision: docs/90-decisions.md D-47 holds the tick
     * disabled until preconditions R1-R4 are green. */
    (void)period_ms;

    memset(&g_boot_ctx, 0, sizeof(g_boot_ctx));
    g_current_ctx = &g_boot_ctx;
}

void *fiber_port_init_stack(void *stack_base, size_t stack_size,
                            fiber_func_t entry, void *arg)
{
    /* entry/arg are unused: fiber_trampoline() reads them back from the
     * FCB of whichever fiber the scheduler dispatched.               */
    (void)entry;
    (void)arg;

    /* Carve port_ctx_t out of the top of the fiber's own stack, aligned
     * down to the port alignment.  The usable stack for the fiber is
     * everything below it, down to stack_base. */
    uintptr_t top = (uintptr_t)stack_base + stack_size;
    top &= ~(uintptr_t)(FIBER_PORT_STACK_ALIGN - 1u);
    top -= sizeof(port_ctx_t);
    top &= ~(uintptr_t)(FIBER_PORT_STACK_ALIGN - 1u);

    port_ctx_t *ctx = (port_ctx_t *)top;
    memset(ctx, 0, sizeof(*ctx));
    if (PORT_GETCONTEXT(&ctx->uctx) != 0) {
        return NULL;
    }

    ctx->uctx.uc_stack.ss_sp    = stack_base;
    ctx->uctx.uc_stack.ss_size  = (size_t)((uintptr_t)ctx - (uintptr_t)stack_base);
    ctx->uctx.uc_stack.ss_flags = 0;
    ctx->uctx.uc_link           = NULL;   /* fiber_trampoline never returns */

    ctx->stack_bottom = stack_base;
    ctx->stack_size   = ctx->uctx.uc_stack.ss_size;

    PORT_MAKECONTEXT(&ctx->uctx, (void (*)(void))fiber_trampoline, 0);
    return ctx;
}

void fiber_port_request_switch(void)
{
    port_ctx_t *cur  = g_current_ctx;
    port_ctx_t *next = (port_ctx_t *)fiber_sched_swap(cur);

    if (next == NULL || next == cur) {
        return;                 /* scheduler re-elected the caller */
    }

    g_current_ctx = next;

#ifdef WMB_ASAN_FIBER
    __sanitizer_start_switch_fiber(&cur->asan_fake_stack,
                                    next->stack_bottom, next->stack_size);
#endif
    (void)PORT_SWAPCONTEXT(&cur->uctx, &next->uctx);
#ifdef WMB_ASAN_FIBER
    /* Control returned to `cur`; restore its ASan bookkeeping. */
    __sanitizer_finish_switch_fiber(cur->asan_fake_stack, NULL, NULL);
#endif
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
