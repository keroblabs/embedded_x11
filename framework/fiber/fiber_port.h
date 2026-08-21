/*
 * fiber_port.h - Internal contract between fiber.c and each arch port.
 *
 * Application code MUST NOT include this header.  Only fiber.c and
 * the per-arch fiber_port.c / fiber_switch.S use these symbols.
 */
#ifndef _fiber_port_h_
#define _fiber_port_h_

#include "types.h"
#include "fiber.h"

/* Port-required stack alignment (bytes).  Defined per arch in its
 * fiber_port.c via the same FIBER_PORT_STACK_ALIGN macro picked up
 * by the generic alignment check in fiber.c.                         */
#ifndef FIBER_PORT_STACK_ALIGN
#  if defined(RISCV)
#    define FIBER_PORT_STACK_ALIGN  16u
#  elif defined(ARM)
#    define FIBER_PORT_STACK_ALIGN  8u
#  else
#    define FIBER_PORT_STACK_ALIGN  8u
#  endif
#endif

/* One-shot: install the deferred-switch handler.  No-op on this
 * platform if the periodic tick is already wired (the firmware's
 * existing 1 ms timer_irq calls fiber_tick_isr() via a weak hook).
 * period_ms = number of 1 ms ticks between preemption switches.     */
void     fiber_port_init(uint32_t period_ms);

/* Forge an initial saved-context frame inside the stack region
 * `[stack_base, stack_base + stack_size)` such that the very first
 * dispatch to this fiber lands in `entry(arg)`.  The port computes
 * the top and rounds it to FIBER_PORT_STACK_ALIGN internally.
 * Returns the SP value to store in fiber_t::sp.                     */
void    *fiber_port_init_stack(void *stack_base, size_t stack_size,
                               fiber_func_t entry, void *arg);

/* Pend a low-priority software interrupt that will drive the
 * scheduler (PendSV on ARMv6-M, MSIP on RV32).                       */
void     fiber_port_request_switch(void);

/* Atomic critical-section helpers.  Nestable via the saved value.   */
uint32_t fiber_port_irq_lock(void);
void     fiber_port_irq_unlock(uint32_t prev);

/* --------------------------------------------------------------------
 * Scheduler entry point - called by the per-arch SWI/PendSV handler
 * after it has saved the outgoing fiber's full register frame to its
 * own stack.  `cur_sp` is the outgoing fiber's SP (= top of the saved
 * frame).  Returns the incoming fiber's SP, which the handler then
 * installs before restoring the corresponding frame.
 * -------------------------------------------------------------------- */
void *fiber_sched_swap(void *cur_sp);

/* Trampoline that every freshly-created fiber starts in.  The forged
 * frame puts this function's address into the resume PC slot.        */
void  fiber_trampoline(void);

/* Allocate a stack of at least `size` bytes, aligned to
 * FIBER_PORT_STACK_ALIGN.  The port may add a guard page, canary or
 * any other bracketing - the generic layer only sees the usable
 * region [return, return + size).  Returns NULL on failure.         */
void *fiber_port_alloc_stack(size_t size);

/* Free a stack previously returned by fiber_port_alloc_stack.
 * `size` is the same value that was passed to alloc.                */
void  fiber_port_free_stack(void *stack, size_t size);

#endif /* _fiber_port_h_ */
