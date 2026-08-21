/*
 * fiber_sync.h - Fiber synchronization primitives.
 *
 * Layered on top of the v1 fiber scheduler (fiber.h).  See
 * fiber/docs/fiber-architecture.md sections 12-13 for semantics
 * and fiber/docs/fiber-synchronization-implementation-plan.md for
 * the rollout plan.
 *
 * Invariants:
 *   - The main fiber never blocks.  Calling fiber_mutex_lock,
 *     fiber_cond_wait or fiber_join from main returns
 *     FIBER_ERR_INVALID instead of suspending.
 *   - A fiber inside a fiber_critical_enter / fiber_critical_exit
 *     region must not call any blocking primitive (same error).
 *   - fiber_cond_signal is BROADCAST: every fiber currently waiting
 *     on the condition is released.
 */
#ifndef _fiber_sync_h_
#define _fiber_sync_h_

#include "fiber.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fiber_mutex fiber_mutex_t;
typedef struct fiber_cond  fiber_cond_t;

/* --------------------------------------------------------------------
 * Mutex - FIFO, non-recursive, owner-checked unlock.
 * -------------------------------------------------------------------- */
fiber_mutex_t *fiber_mutex_create(void);
int            fiber_mutex_destroy(fiber_mutex_t *m);
int            fiber_mutex_lock(fiber_mutex_t *m);
int            fiber_mutex_trylock(fiber_mutex_t *m);
int            fiber_mutex_unlock(fiber_mutex_t *m);

/* --------------------------------------------------------------------
 * Condition variable - signal wakes ALL current waiters (broadcast).
 * -------------------------------------------------------------------- */
fiber_cond_t  *fiber_cond_create(void);
int            fiber_cond_destroy(fiber_cond_t *c);
int            fiber_cond_wait(fiber_cond_t *c);
int            fiber_cond_signal(fiber_cond_t *c);

/* --------------------------------------------------------------------
 * Wait-for-termination - blocks until `target` reaches ZOMBIE.
 * Multiple fibers may join the same target; all wake on termination.
 * -------------------------------------------------------------------- */
int            fiber_join(fiber_t *target);

#ifdef __cplusplus
}
#endif

#endif /* _fiber_sync_h_ */
