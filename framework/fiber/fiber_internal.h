/*
 * fiber_internal.h - State shared between fiber.c and fiber_sync.c.
 *
 * NOT a public header.  Application code must keep including only
 * fiber.h (lifecycle) and fiber_sync.h (synchronization).
 *
 * The full struct fiber definition lives here so the synchronization
 * primitives can touch state and the wait_* fields without having to
 * duplicate the FCB layout.  See fiber/docs/fiber-architecture.md
 * sections 5, 9 and 10 for the invariants that govern access.
 */
#ifndef _fiber_internal_h_
#define _fiber_internal_h_

#include "fiber.h"
#include "fiber_port.h"
#include "fiber_config.h"

#include <stddef.h>

typedef enum {
    FIBER_STATE_FREE = 0,
    FIBER_STATE_READY,
    FIBER_STATE_RUNNING,
    FIBER_STATE_BLOCKED,   /* parked on a mutex/cond/join wait queue */
    FIBER_STATE_ZOMBIE
} fiber_state_t;

struct fiber {
    /* ---- HARD ABI: assembly hard-codes sp at offset 0 ---- */
    void           *sp;
    /* ---- end ABI ---- */

    void           *stack_base;
    size_t          stack_size;
    uint32_t       *canary;
    struct fiber   *next;          /* circular ring of all fibers     */
    fiber_func_t    entry;
    void           *arg;

    /* Bumped every time this pool slot is reused.  A handle held across
     * a destroy is detected by comparing it (see fiber_join).         */
    uint32_t        generation;

    /* Predicate wait (fiber_wait_for).  wait_cb != NULL means this
     * fiber is parked on a predicate that fiber_notify() evaluates. */
    check_cb_t      wait_cb;
    void           *wait_data;
    uint32_t        wait_deadline; /* hal_tick_ms value                */

    uint8_t         state;         /* fiber_state_t                   */
    uint8_t         is_main;
    uint8_t         owns_stack;
    uint8_t         wait_expired;  /* set when released by timeout    */
    uint8_t         wait_timed;    /* wait_deadline is valid          */
};

/* --------------------------------------------------------------------
 * Helpers exported by fiber.c for fiber_sync.c.
 *
 * All "_locked" suffixed helpers expect the caller to be holding
 * fiber_port_irq_lock().  block_self_locked() drops the lock as part
 * of yielding the CPU; it returns when the fiber is re-scheduled with
 * interrupts unlocked.
 * -------------------------------------------------------------------- */
void   fiber_internal_block_self_locked(uint32_t saved_irq);
void   fiber_internal_wake_locked(struct fiber *f);

/* TRUE while the current fiber holds at least one fiber_critical_enter
 * nesting level - used by sync primitives to refuse blocking calls
 * that would otherwise deadlock the runtime.                         */
bool_t fiber_internal_in_critical(void);

#endif /* _fiber_internal_h_ */
