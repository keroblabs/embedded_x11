/*
 * fiber_sync.c - Mutex, condition variable and join for fibers.
 *
 * All three are expressed as predicates over fiber_wait_for(); this
 * file owns no wait queues and no blocking logic of its own.  See
 * fiber/docs/fiber-architecture.md.
 *
 * Rules inherited from fiber_wait_for():
 *   - Parking inside a critical section is refused (would deadlock).
 *     trylock is exempt because it never parks.
 *   - main polls instead of parking, so it may use every primitive
 *     here without stalling the scheduler.
 *   - Predicates run under the runtime lock, on the notifier's stack.
 */
#include "fiber.h"
#include "fiber_sync.h"
#include "fiber_port.h"
#include "fiber_config.h"
#include "fiber_internal.h"

#include <stddef.h>
#include <string.h>

/* --------------------------------------------------------------------
 * Object layouts (private to this TU - only the typedefs leak out).
 * -------------------------------------------------------------------- */
struct fiber_mutex {
    struct fiber *owner;        /* NULL when free */
    uint8_t       in_use;       /* pool slot occupancy */
};

struct fiber_cond {
    uint32_t seq;               /* bumped by signal; waiters snapshot it */
    uint8_t  in_use;
};

static struct fiber_mutex g_mtx_pool[FIBER_MUTEX_MAX];
static struct fiber_cond  g_cond_pool[FIBER_COND_MAX];

/* --------------------------------------------------------------------
 * Mutex
 * -------------------------------------------------------------------- */

/* Test-and-acquire in one predicate: the runtime lock held during
 * evaluation is what makes it atomic, so there is no window between
 * observing the mutex free and owning it. */
static bool_t mutex_acquire(void *data)
{
    struct fiber_mutex *m = (struct fiber_mutex *)data;

    if (m->owner == NULL) {
        m->owner = fiber_self();
        return TRUE;
    }
    return FALSE;
}

fiber_mutex_t *fiber_mutex_create(void)
{
    fiber_mutex_t *ret = NULL;
    uint32_t prev = fiber_port_irq_lock();
    int i;

    for (i = 0; i < FIBER_MUTEX_MAX; i++) {
        if (!g_mtx_pool[i].in_use) {
            memset(&g_mtx_pool[i], 0, sizeof(g_mtx_pool[i]));
            g_mtx_pool[i].in_use = 1u;
            ret = &g_mtx_pool[i];
            break;
        }
    }

    fiber_port_irq_unlock(prev);
    return ret;
}

int fiber_mutex_destroy(fiber_mutex_t *m)
{
    uint32_t prev;

    if ((m == NULL) || !m->in_use) {
        return FIBER_ERR_INVALID;
    }

    prev = fiber_port_irq_lock();
    m->owner  = NULL;
    m->in_use = 0u;
    fiber_port_irq_unlock(prev);

    fiber_notify();     /* anyone parked on it will never be served */
    return FIBER_OK;
}

int fiber_mutex_lock(fiber_mutex_t *m)
{
    if ((m == NULL) || !m->in_use) {
        return FIBER_ERR_INVALID;
    }
    if (m->owner == fiber_self()) {
        return FIBER_ERR_INVALID;       /* non-recursive */
    }

    return (fiber_wait_for(mutex_acquire, m, FIBER_WAIT_FOREVER) == TRUE)
           ? FIBER_OK : FIBER_ERR_INVALID;
}

int fiber_mutex_trylock(fiber_mutex_t *m)
{
    if ((m == NULL) || !m->in_use) {
        return FIBER_ERR_INVALID;
    }
    if (m->owner == fiber_self()) {
        return FIBER_ERR_INVALID;
    }

    /* timeout 0 never parks, so this is legal from a critical section */
    return (fiber_wait_for(mutex_acquire, m, 0u) == TRUE)
           ? FIBER_OK : FIBER_ERR_INVALID;
}

int fiber_mutex_unlock(fiber_mutex_t *m)
{
    uint32_t prev;

    if ((m == NULL) || !m->in_use) {
        return FIBER_ERR_INVALID;
    }
    if (m->owner != fiber_self()) {
        return FIBER_ERR_INVALID;       /* owner-checked */
    }

    prev = fiber_port_irq_lock();
    m->owner = NULL;
    fiber_port_irq_unlock(prev);

    fiber_notify();
    return FIBER_OK;
}

/* --------------------------------------------------------------------
 * Condition variable
 * -------------------------------------------------------------------- */

typedef struct {
    struct fiber_cond *cond;
    uint32_t           seen;
} cond_wait_t;

static bool_t cond_advanced(void *data)
{
    cond_wait_t *w = (cond_wait_t *)data;
    return (w->cond->seq != w->seen) ? TRUE : FALSE;
}

fiber_cond_t *fiber_cond_create(void)
{
    fiber_cond_t *ret = NULL;
    uint32_t prev = fiber_port_irq_lock();
    int i;

    for (i = 0; i < FIBER_COND_MAX; i++) {
        if (!g_cond_pool[i].in_use) {
            memset(&g_cond_pool[i], 0, sizeof(g_cond_pool[i]));
            g_cond_pool[i].in_use = 1u;
            ret = &g_cond_pool[i];
            break;
        }
    }

    fiber_port_irq_unlock(prev);
    return ret;
}

int fiber_cond_destroy(fiber_cond_t *c)
{
    uint32_t prev;

    if ((c == NULL) || !c->in_use) {
        return FIBER_ERR_INVALID;
    }

    prev = fiber_port_irq_lock();
    c->in_use = 0u;
    c->seq++;                   /* release current waiters */
    fiber_port_irq_unlock(prev);

    fiber_notify();
    return FIBER_OK;
}

int fiber_cond_wait(fiber_cond_t *c)
{
    cond_wait_t w;

    if ((c == NULL) || !c->in_use) {
        return FIBER_ERR_INVALID;
    }

    /* Snapshot on the waiter's stack, which stays alive while parked. */
    w.cond = c;
    w.seen = c->seq;

    return (fiber_wait_for(cond_advanced, &w, FIBER_WAIT_FOREVER) == TRUE)
           ? FIBER_OK : FIBER_ERR_INVALID;
}

int fiber_cond_signal(fiber_cond_t *c)
{
    uint32_t prev;

    if ((c == NULL) || !c->in_use) {
        return FIBER_ERR_INVALID;
    }

    prev = fiber_port_irq_lock();
    c->seq++;                   /* broadcast: every waiter sees the change */
    fiber_port_irq_unlock(prev);

    fiber_notify();
    return FIBER_OK;
}

/* --------------------------------------------------------------------
 * Join
 * -------------------------------------------------------------------- */

typedef struct {
    struct fiber *target;
    uint32_t      generation;
} join_wait_t;

/* The generation check is load-bearing: once the target is reclaimed its
 * FCB is handed to the next fiber_create, so state alone would report on
 * a different fiber entirely. */
static bool_t join_done(void *data)
{
    join_wait_t  *j = (join_wait_t *)data;
    struct fiber *t = j->target;

    if (t->generation != j->generation) return TRUE;
    if (t->state == FIBER_STATE_ZOMBIE) return TRUE;
    if (t->state == FIBER_STATE_FREE)   return TRUE;

    return FALSE;
}

int fiber_join(fiber_t *target)
{
    join_wait_t j;

    if (target == NULL) {
        return FIBER_ERR_INVALID;
    }
    if (target == fiber_self()) {
        return FIBER_ERR_INVALID;
    }
    if (fiber_is_main(target) == TRUE) {
        return FIBER_ERR_IMMORTAL;
    }

    j.target     = target;
    j.generation = target->generation;

    (void)fiber_wait_for(join_done, &j, FIBER_WAIT_FOREVER);

    return FIBER_OK;
}
