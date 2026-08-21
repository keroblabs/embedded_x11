/*
 * hal_task.c - WMB's implementation of hal_task.h.
 *
 * Not a platform backend: these nine functions are the same on every
 * profile (90-decisions.md D-31, 12-platform-contract.md section 9).
 * Mapping is docs/12-platform-contract.md section 6.3.
 *
 * struct hal_task is deliberately not struct fiber: the contract needs
 * a name and a pending-wake flag that the FCB has no room for.
 */
#include "hal_task.h"
#include "hal_time.h"

#include "fiber.h"
#include "fiber_internal.h"
#include "fiber_port.h"

#include <string.h>

///////////////////////////////////////////////////////////////////////////

struct hal_task {
    fiber_t          *fiber;
    const char       *name;
    volatile uint8_t  pending_wake;
    uint8_t           started;      /* cleared until the release wake */
    uint8_t           in_use;
};

/* Slot 0 is the root task, bound to the main fiber by runtime_init. */
static struct hal_task task_pool[FIBER_MAX];

///////////////////////////////////////////////////////////////////////////

static struct hal_task *slot_of(const fiber_t *f)
{
    int i;

    for (i = 0; i < FIBER_MAX; i++) {
        if (task_pool[i].in_use && (task_pool[i].fiber == f)) {
            return &task_pool[i];
        }
    }
    return NULL;
}

static struct hal_task *slot_alloc(void)
{
    int i;

    for (i = 0; i < FIBER_MAX; i++) {
        if (!task_pool[i].in_use) {
            memset(&task_pool[i], 0, sizeof(task_pool[i]));
            task_pool[i].in_use = 1u;
            return &task_pool[i];
        }
    }
    return NULL;
}

/* Consumes the pending wake if one is set.  Runs under the runtime lock
 * inside fiber_notify(), so test-and-clear is atomic for free. */
static bool_t task_woken(void *data)
{
    struct hal_task *t = (struct hal_task *)data;

    if (t->pending_wake) {
        t->pending_wake = 0u;
        return TRUE;
    }
    return FALSE;
}

///////////////////////////////////////////////////////////////////////////

int hal_task_runtime_init(uint32_t safety_tick_ms)
{
    memset(task_pool, 0, sizeof(task_pool));

    fiber_runtime_init(safety_tick_ms);

    task_pool[0].fiber   = fiber_main();
    task_pool[0].name    = "main";
    task_pool[0].started = 1u;
    task_pool[0].in_use  = 1u;

    return 0;
}

hal_task_t *hal_task_create(const char *name, hal_task_entry_t entry,
                            void *arg, size_t stack_bytes)
{
    struct hal_task *t;
    fiber_t *f;
    uint32_t prev;

    if (entry == NULL) {
        return NULL;
    }

    t = slot_alloc();
    if (t == NULL) {
        return NULL;
    }

    f = fiber_create(entry, arg, stack_bytes);
    if (f == NULL) {
        t->in_use = 0u;
        return NULL;
    }

    /* Contract: creation state is BLOCKED.  The fiber is already in the
     * ring; the scheduler skips BLOCKED entries until hal_task_wake. */
    prev = fiber_port_irq_lock();
    f->state = FIBER_STATE_BLOCKED;
    fiber_port_irq_unlock(prev);

    t->fiber = f;
    t->name  = name;

    return t;
}

void hal_task_destroy(hal_task_t *t)
{
    fiber_t *f;

    if ((t == NULL) || (t->fiber == NULL)) {
        return;
    }

    f = t->fiber;

    t->fiber  = NULL;
    t->in_use = 0u;

    fiber_destroy(f);       /* never returns when f is the caller */
}

hal_task_t *hal_task_self(void)
{
    return slot_of(fiber_self());
}

const char *hal_task_name(const hal_task_t *t)
{
    return (t != NULL) ? t->name : NULL;
}

void hal_task_yield(void)
{
    fiber_yield();
}

void hal_task_wait(void)
{
    struct hal_task *self = hal_task_self();

    if (self == NULL) {
        return;
    }

    (void)fiber_wait_for(task_woken, self, FIBER_WAIT_FOREVER);
}

void hal_task_wake(hal_task_t *t)
{
    uint32_t prev;

    if ((t == NULL) || (t->fiber == NULL)) {
        return;
    }

    prev = fiber_port_irq_lock();
    if (t->started) {
        t->pending_wake = 1u;
    } else {
        /* The release of a created-blocked task is a start signal, not a
         * data signal: recording it would consume the task's first wait. */
        t->started = 1u;
    }

    /* A task parked at creation has no predicate for fiber_notify() to
     * evaluate, so release it directly. */
    if (t->fiber->wait_cb == NULL) {
        fiber_internal_wake_locked(t->fiber);
    }
    fiber_port_irq_unlock(prev);

    fiber_notify();     /* never switches */
}

int hal_yield_timeout(uint32_t max_ms)
{
    struct hal_task *self = hal_task_self();

    if (max_ms == 0u) {
        fiber_yield();
        return 0;
    }
    if (self == NULL) {
        /* Not a task: nothing can wake us, so just burn the interval. */
        uint32_t start_ms = hal_tick_ms();
        while ((hal_tick_ms() - start_ms) < max_ms) {
            fiber_yield();
        }
        return 0;
    }

    return (fiber_wait_for(task_woken, self, max_ms) == TRUE) ? 1 : 0;
}
