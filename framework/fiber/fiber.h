/*
 * fiber.h - Portable cooperative + preemptive fiber API.
 *
 * Application code only ever includes this header.  See
 * fiber/docs/fiber-architecture.md and
 * fiber/docs/fiber-implementation-plan.md.
 */
#ifndef _fiber_h_
#define _fiber_h_

#include "types.h"
#include "fiber_config.h"

/* Default preemption time passed to fiber_runtime_init().  Applications
 * may override by defining before including this header or via build
 * flag.  Value is in milliseconds (number of 1 ms ticks).            */
#ifndef DEFAULT_PREEMPTION_TIME_MS
#define DEFAULT_PREEMPTION_TIME_MS  FIBER_DEFAULT_PREEMPTION_MS
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fiber fiber_t;
typedef void (*fiber_func_t)(void *arg);

/* Error / status codes (negative = error). */
#define FIBER_OK             0
#define FIBER_ERR_NOMEM     -1
#define FIBER_ERR_IMMORTAL  -2   /* attempt to destroy main */
#define FIBER_ERR_INVALID   -3
#define FIBER_ERR_OVERFLOW  -4   /* stack canary tripped */

/* --------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------- */

/* Converts the calling thread of execution into the immortal 'main'
 * fiber and arms preemption at the given period (milliseconds).
 * preemption_ms = number of 1 ms ticks between forced switches.     */
void      fiber_runtime_init(uint32_t preemption_ms);

/* Dynamically allocate FCB-from-pool + heap stack.  Returns NULL on
 * exhaustion.                                                        */
fiber_t  *fiber_create(fiber_func_t func, void *arg, size_t stack_size);

/* Use a caller-supplied stack buffer (no heap involvement).
 * stack_buf must be aligned to the port's required boundary
 * (16 B on RV32, 8 B on ARMv6-M).                                    */
fiber_t  *fiber_create_static(fiber_func_t func, void *arg,
                              void *stack_buf, size_t stack_size);

/* Cooperative yield.  Round-robin: main -> f1 -> f2 -> ... -> main.  */
void      fiber_yield(void);

/* --------------------------------------------------------------------
 * Predicate wait - the runtime's one blocking primitive
 * -------------------------------------------------------------------- */

typedef bool_t (*check_cb_t)(void *data);

#define FIBER_WAIT_FOREVER      0xFFFFFFFFu

/* Halt the calling fiber until check_cb(data) returns TRUE, or until
 * timeout_ms has elapsed.  Returns TRUE if the predicate was satisfied,
 * FALSE on timeout.
 *
 * check_cb is evaluated once up front - if it is already TRUE the fiber
 * never blocks - and thereafter only from fiber_notify().  A producer
 * that changes state a waiter cares about must call fiber_notify();
 * omitting it leaves the waiter parked until its timeout.
 *
 * check_cb runs under the runtime lock, on the stack of whoever calls
 * fiber_notify().  It must be small: no blocking, no yielding, no
 * allocation, no nested fiber_wait_for.  It may mutate state, which is
 * what lets a mutex be expressed as test-and-acquire.
 *
 * Called from main it polls instead of parking, because the scheduler
 * requires main to stay runnable.                                    */
bool_t    fiber_wait_for(check_cb_t check_cb, void *data, uint32_t timeout_ms);

/* Re-evaluate every parked predicate and release the fibers whose
 * predicate is now TRUE.  Never switches: it returns to the caller.
 * Call it after changing state another fiber may be waiting on.      */
void      fiber_notify(void);

/* Terminate any fiber.  If target == fiber_self(), never returns.
 * Returns FIBER_ERR_IMMORTAL if target is main.                       */
int       fiber_destroy(fiber_t *target);

/* --------------------------------------------------------------------
 * Critical section - suppress preemptive scheduling
 * -------------------------------------------------------------------- */

/* Enter a fiber critical section.  Disables preemptive context
 * switching AND masks interrupts, providing a fully protected
 * execution block.  Nestable: only the outermost exit re-enables.   */
void      fiber_critical_enter(void);

/* Exit a fiber critical section.  When the outermost nesting level
 * is released, preemption and interrupts are restored.              */
void      fiber_critical_exit(void);

/* --------------------------------------------------------------------
 * Accessors
 * -------------------------------------------------------------------- */
fiber_t  *fiber_self(void);
fiber_t  *fiber_main(void);
bool_t    fiber_is_main(const fiber_t *f);

#ifdef __cplusplus
}
#endif

#endif /* _fiber_h_ */
