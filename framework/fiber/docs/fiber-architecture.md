# Fiber Runtime Architecture

**Status:** v2.0 — synchronized with implementation
**Scope:** cooperative fiber runtime for hosted Linux and for bare-metal
ARMv6-M (Cortex-M0/M0+) and RV32IMAC targets. Used by WMB as its entire
concurrency model ([`90-decisions.md`](../../../docs/90-decisions.md) D‑10, D‑31).
**Source of truth:** [`fiber.c`](../fiber.c), [`fiber_sync.c`](../fiber_sync.c),
[`hal_task.c`](../hal_task.c), [`fiber.h`](../fiber.h),
[`fiber_sync.h`](../fiber_sync.h), [`fiber_internal.h`](../fiber_internal.h),
[`fiber_port.h`](../fiber_port.h), [`fiber_config.h`](../fiber_config.h).
**Companion docs:** [fiber-user-reference-guide.md](fiber-user-reference-guide.md) ·
[fiber-implementation-plan.md](fiber-implementation-plan.md) ·
[fiber-synchronization-implementation-plan.md](fiber-synchronization-implementation-plan.md) ·
[hosted-interrupt-simulation.md](hosted-interrupt-simulation.md)

### What changed in v2.0

`fiber_wait_for()` / `fiber_notify()` became the runtime's **one** blocking
primitive. Mutex, condition variable, join and the whole `hal_task_*` layer
are now thin expressions of it rather than independent mechanisms with their
own wait queues.

| | v1.1 | v2.0 |
|---|---|---|
| Blocking mechanisms | 3 wait queues + a joiner list | 1 predicate per FCB |
| `struct fiber` | `wait_next`, `joiners_head`, `joiners_tail` | `wait_cb`, `wait_data`, `wait_deadline`, `generation` |
| Timeouts on blocking calls | non-goal | supported everywhere |
| Blocking from `main` | `FIBER_ERR_INVALID` | polls; works |
| Hosted port | not shipped | shipped — `linux_hosted/fiber_linux/` |
| Preemptive tick | armed | **disabled**, see §6.5 |

The reason for the change is stated once, because it governs everything
below: **a predicate is level-triggered, so there is no edge to lose.**
Every lost-wakeup bug comes from a signal arriving before its waiter parks.
`fiber_wait_for` evaluates its predicate before parking, so that window
does not exist.

---

## 1. Goals

1. Single portable C API usable by application code with no knowledge of the
   underlying CPU.
2. Cooperative `fiber_yield()` always available; an optional preemptive tick
   as a **safety net** against a runaway fiber, never as the scheduling
   mechanism.
3. Identical behaviour on three back-ends:
   - `linux_hosted/fiber_linux` — hosted Linux, libucontext / glibc `<ucontext.h>`
   - `arch/emulator_riscv` — RV32IMAC, bare-metal
   - `arch/emulator_arm` — Cortex-M0/M0+ / ARMv6-M, bare-metal
4. Deterministic round-robin ordering: `main → f1 → f2 → … → fn → main → f1 …`.
5. Any fiber (including `main`) may create or destroy any other fiber.
6. A fiber may self-terminate by `fiber_destroy(fiber_self())` or by returning
   from its entry function.
7. `main` is immortal — `fiber_destroy(main)` returns `FIBER_ERR_IMMORTAL`.
8. **No allocation on any steady-state path.** All pools are static.

### Non-goals (v2.0)

- Priorities and sleep queues.
- Message queues. Layer them on `fiber_wait_for` — see §10.6.
- Floating-point context save (no FPU on either supported bare-metal core).
- SMP. Single-CPU only.
- Fairness guarantees on mutex acquisition — see §10.2.

---

## 2. Directory Layout

The generic layer is platform-free; every port is a sibling tree
([`03-target-architecture.md`](../../../docs/03-target-architecture.md) rule L9).

```
framework/fiber/                    # Generic, portable layer
├── fiber.h                         # Lifecycle, scheduling, critical section,
│                                   #   fiber_wait_for / fiber_notify
├── fiber_sync.h                    # Mutex, cond, join
├── fiber_internal.h                # struct fiber; shared with fiber_sync.c
├── fiber_port.h                    # Port contract (arch authors only)
├── fiber_config.h                  # Compile-time tunables
├── fiber.c                         # Scheduler, FCB pool, lifecycle, wait_for
├── fiber_sync.c                    # Mutex, cond, join - all predicates
├── hal_task.c                      # hal_task.h contract (WMB; see 12- section 9)
├── fiber.cmake                     # Exports SOURCE_FILES_FIBER, FIBER_DEFINES
└── docs/                           # This document and companions

linux_hosted/fiber_linux/           # Hosted port
├── fiber_port.c                    # libucontext / <ucontext.h> switch
├── intr_handler.c                  # Global IRQ-enable flag, crash hook
└── fiber_linux.cmake
```

`fiber.cmake` exports **`FIBER_DEFINES = USE_FIBER`**. Any target linking the
fiber core must apply it: it is what routes `framework/library/buffer.c`'s
locks to `fiber_critical_enter/exit` ([`90-decisions.md`](../../../docs/90-decisions.md) D‑47.4).

---

## 3. Public API

Two headers are public. Everything else under `framework/fiber/` is internal.

### 3.1 `fiber.h` — lifecycle, scheduling, critical section, waiting

```c
typedef struct fiber fiber_t;
typedef void   (*fiber_func_t)(void *arg);
typedef bool_t (*check_cb_t)(void *data);

#define FIBER_WAIT_FOREVER  0xFFFFFFFFu

/* Lifecycle */
void      fiber_runtime_init(uint32_t preemption_ms);
fiber_t  *fiber_create(fiber_func_t func, void *arg, size_t stack_size);
fiber_t  *fiber_create_static(fiber_func_t func, void *arg,
                              void *stack_buf, size_t stack_size);
void      fiber_yield(void);
int       fiber_destroy(fiber_t *target);

/* The one blocking primitive */
bool_t    fiber_wait_for(check_cb_t check_cb, void *data, uint32_t timeout_ms);
void      fiber_notify(void);

/* Critical section - suppresses preemption AND masks interrupts.
 * Nestable; only the outermost exit restores state.            */
void      fiber_critical_enter(void);
void      fiber_critical_exit(void);

/* Accessors */
fiber_t  *fiber_self(void);
fiber_t  *fiber_main(void);
bool_t    fiber_is_main(const fiber_t *f);
```

### 3.2 `fiber_sync.h` — synchronization

Unchanged signatures; the implementations are now predicates (§10).

```c
fiber_mutex_t *fiber_mutex_create(void);
int            fiber_mutex_destroy(fiber_mutex_t *m);
int            fiber_mutex_lock(fiber_mutex_t *m);
int            fiber_mutex_trylock(fiber_mutex_t *m);
int            fiber_mutex_unlock(fiber_mutex_t *m);

fiber_cond_t  *fiber_cond_create(void);
int            fiber_cond_destroy(fiber_cond_t *c);
int            fiber_cond_wait(fiber_cond_t *c);
int            fiber_cond_signal(fiber_cond_t *c);

int            fiber_join(fiber_t *target);
```

### 3.3 Error codes

```c
FIBER_OK            =  0
FIBER_ERR_NOMEM     = -1
FIBER_ERR_IMMORTAL  = -2   /* attempt to destroy / join main */
FIBER_ERR_INVALID   = -3   /* bad arg, illegal context, mis-aligned stack, … */
FIBER_ERR_OVERFLOW  = -4   /* stack canary tripped on dispatch */
```

`fiber_create*` signals failure with `NULL`. `fiber_wait_for` returns
`bool_t`, not an error code: `TRUE` = predicate satisfied, `FALSE` = timed
out, rejected, or called with a NULL callback.

---

## 4. Internal Port Contract (`fiber_port.h`)

Five symbols. A port implements these and gets the scheduler, the
synchronization layer and `hal_task_*` for free
([`12-platform-contract.md`](../../../docs/12-platform-contract.md) §9).

| Symbol | Responsibility |
|---|---|
| `fiber_port_init(period_ms)` | Arm the preemption tick. **Currently ignored on every port** — see §6.5 |
| `fiber_port_init_stack(top, entry, arg)` | Forge an initial frame; return its `sp` |
| `fiber_port_request_switch()` | Pend a deferred switch (PendSV / MSIP / direct swap) |
| `fiber_port_irq_lock()` | Mask interrupts, return previous state |
| `fiber_port_irq_unlock(prev)` | Restore |

`fiber_sched_swap(cur_sp)` and `fiber_trampoline()` are provided *by* the
generic layer and called *from* the port.

---

## 5. Fiber Control Block (`struct fiber`)

Lives in [`fiber_internal.h`](../fiber_internal.h) so `fiber.c`, `fiber_sync.c`
and `hal_task.c` can see it. Application code never includes that header.
The **first word must be `sp`** — per-arch assembly hard-codes offset 0.

```c
typedef enum {
    FIBER_STATE_FREE = 0,
    FIBER_STATE_READY,
    FIBER_STATE_RUNNING,
    FIBER_STATE_BLOCKED,   /* parked, possibly on a predicate */
    FIBER_STATE_ZOMBIE
} fiber_state_t;

struct fiber {
    void           *sp;             /* HARD ABI: offset 0 */
    void           *stack_base;
    size_t          stack_size;
    uint32_t       *canary;         /* &stack_base[0], == FIBER_CANARY */
    struct fiber   *next;           /* circular ring of all fibers */
    fiber_func_t    entry;
    void           *arg;

    uint32_t        generation;     /* bumped on every pool-slot reuse */

    check_cb_t      wait_cb;        /* non-NULL => parked on a predicate */
    void           *wait_data;
    uint32_t        wait_deadline;  /* hal_tick_ms value */

    uint8_t         state;
    uint8_t         is_main;
    uint8_t         owns_stack;
    uint8_t         wait_expired;   /* released by timeout, not predicate */
    uint8_t         wait_timed;     /* wait_deadline is valid */
};
```

`wait_*` fields are mutated only under `fiber_port_irq_lock`. The FCB pool is
a fixed array of `FIBER_MAX` entries; no FCB is ever heap-allocated.

**`generation`** exists so a joiner parked on a target can tell the difference
between "my target is still running" and "my target died, was reclaimed, and
its slot now belongs to a different fiber". See §10.4 for exactly what it does
and does not protect.

---

## 6. Scheduling Model

### 6.1 Ring topology

A single circular list. `fiber_create*` splices the new node immediately
**after** `fiber_self()`:

```
main → f1 → f2 → … → fn → main
```

### 6.2 What the scheduler does per node

`fiber_sched_swap()` walks the ring from `cur->next` and, per node:

| State | Action |
|---|---|
| `ZOMBIE` | reclaim (free stack, unlink, release FCB), continue |
| `BLOCKED` | if `wait_timed` and the deadline passed → release it and run it; otherwise **skip** |
| `READY` / `RUNNING` | dispatch it |

**The scheduler never calls a predicate.** A parked fiber costs one state
compare, plus one integer compare if it has a deadline. That is the entire
cost of a blocked fiber per ring pass, and it is why `fiber_notify()` exists
rather than having the scheduler poll.

`hal_tick_ms()` is read at most once per pass, lazily, and only if some
blocked fiber actually carries a deadline.

### 6.3 Cooperative path

`fiber_yield()` → `fiber_port_request_switch()` → the port pends a switch →
`fiber_sched_swap()` picks the next runnable node.

### 6.4 Termination

Both paths — natural return from the entry function and `fiber_destroy` —
set `ZOMBIE`, clear any predicate, and call `fiber_notify()` so joiners
observe the transition. The stack is freed later, by the scheduler
(invariant 3).

### 6.5 Preemption period — currently disarmed

`fiber_runtime_init(period_ms)` forwards to `fiber_port_init(period_ms)`,
and **every port ignores it today**. No timer thread, no signal handler, no
tick. Scheduling is 100 % cooperative on every profile.

This is a decision with an exit condition, not an omission:
[`90-decisions.md`](../../../docs/90-decisions.md) **D‑47** holds the tick
disabled until four preconditions are met, because arming it makes every
unsynchronised read-modify-write in the system reachable. Write code as if it
were armed.

---

## 7. Per-Port Specifics

### 7.1 Hosted Linux (`linux_hosted/fiber_linux`)

`fiber_port_init_stack` builds a `ucontext_t` carved from the top of the
fiber's own stack buffer; `fiber_port_request_switch` performs the swap
directly. `fiber_port_irq_lock/unlock` manipulate a process-global
interrupt-enable flag in `intr_handler.c` — there are no real interrupts to
mask, so the flag exists to make the *same* code correct on both platforms.

libucontext is preferred over glibc `<ucontext.h>`
([`90-decisions.md`](../../../docs/90-decisions.md) D‑11); when it is absent
the build warns and falls back.

### 7.2 RISC-V (`arch/emulator_riscv`)

Full-register swap in `fiber_switch.S`; MSIP is the deferred-switch source.

### 7.3 ARMv6-M (`arch/emulator_arm`)

9-register swap; PendSV at the lowest exception priority so the swap can
never re-enter itself.

---

## 8. Memory & Performance Budget

Measured, not estimated. `struct fiber` is **96 B on x86-64**; on a 32-bit
target it is approximately 56 B (seven pointers become 4 B each).

| Item | Size |
|---|---:|
| Static FCB pool | `FIBER_MAX × sizeof(struct fiber)` = 8 × 96 B = 768 B hosted |
| Static fiber heap (`fiber_create`) | `FIBER_HEAP_SIZE` = 16,896 B at defaults |
| Static mutex pool | `FIBER_MUTEX_MAX × sizeof(fiber_mutex_t)` |
| Static cond pool | `FIBER_COND_MAX × sizeof(fiber_cond_t)` |
| Saved-frame size | 128 B (RV32, full GP) · 36 B (ARMv6-M, 9 words) |
| Min stack | 256 B (enforced) |
| **Allocations in steady-state hot paths** | **0** |
| Cost of a parked fiber, per ring pass | 1 state compare (+1 integer compare if timed) |
| Cost of `fiber_notify()` | O(`FIBER_MAX`), one predicate call per parked fiber |

`fiber_create` uses a dedicated `fiber_heap[]` fed to `heap_init()` — never
the system allocator — so the runtime has no `_sbrk()` dependency. The v2.0
FCB is *smaller* than v1.1 despite the new fields: three wait-queue pointers
were removed and one `uint32_t` added.

Defaults are microcontroller-sized and must be overridden for the host; see
[`12-platform-contract.md`](../../../docs/12-platform-contract.md) §6.4.

---

## 9. Concurrency & Safety Invariants

1. The FCB ring and all `wait_*` fields are mutated **only** under
   `fiber_port_irq_lock()`.
2. `fiber_sched_swap()` runs with interrupts disabled. The trampoline
   re-enables them on first entry to a freshly-created fiber.
3. A `ZOMBIE`'s stack is freed **only** by the scheduler tail
   (`zombie_reclaim_locked`), never by the destroyer — this eliminates the
   "free while still on that stack" race.
4. Stack canary (`*f->canary == FIBER_CANARY`, default `0xDEADBEEF`) is
   checked on every dispatch of the outgoing fiber. Mismatch calls
   `intr_handler_call_crash(FIBER_ERR_OVERFLOW)`. Main has no canary and is
   skipped.
5. On ARMv6-M, PendSV is at the lowest exception priority; on RV32 the MSIP
   handler runs after the timer IRQ returns. Both prevent re-entry of the
   swap routine.
6. The tick ISR is a **no-op** while `fiber_internal_in_critical() == TRUE`
   — preemptive switches are suppressed for the entire nested critical
   section. (Moot while §6.5 holds, and still true when it is lifted.)
7. `fiber_internal_block_self_locked()` drops the IRQ lock as it pends the
   switch; the awakening fiber resumes with interrupts unlocked at the
   unblocker's expense.
8. **Predicates never run from the switch path.** They are called only from
   `fiber_notify()`, on the notifier's stack, and from `fiber_wait_for()`'s
   up-front evaluation, on the waiter's own stack. No application callback
   executes in PendSV/MSIP context.
9. **Inside a predicate, `fiber_self()` reports the parked fiber**, not the
   caller of `fiber_notify()`. Without this a test-and-acquire predicate
   claims its resource for the wrong fiber (§10.2).
10. `fiber_notify()` re-entered from within a predicate is a no-op, so a
    predicate that notifies cannot recurse.

---

## 10. Waiting and Synchronization

### 10.1 `fiber_wait_for` — the one blocking primitive

```c
bool_t fiber_wait_for(check_cb_t check_cb, void *data, uint32_t timeout_ms);
```

Order of operations:

1. **Evaluate `check_cb(data)` immediately.** If TRUE, return TRUE without
   parking. This is what closes the entry race — a producer that acted
   before the consumer called `fiber_wait_for` cannot be missed.
2. If `timeout_ms == 0`, return FALSE. This is *poll-once*, and it is legal
   inside a critical section because it never parks.
3. If inside a critical section, return FALSE — parking with interrupts
   masked would deadlock the runtime.
4. If the caller is `main`, poll (`fiber_yield()` + re-evaluate) rather than
   park, because the scheduler requires main to stay runnable.
5. Otherwise record `{cb, data, deadline}` in the FCB, set `BLOCKED`, and
   yield. Return TRUE if released by a predicate, FALSE if by timeout.

**There is no producer-side counterpart to a wait.** Fibers communicate
through the data the predicate inspects:

```c
/* producer */                        /* consumer */
list_push(&q, item);                  fiber_wait_for(list_not_empty, &q, 1000u);
fiber_notify();                       item = list_pop(&q);
```

`fiber_notify()` is a *hint*, not a handshake. Omitting it does not corrupt
anything and does not lose a signal — it delays the waiter until its timeout,
which then re-evaluates. Correctness depends on the predicate; only latency
depends on the notify.

### 10.1.1 Rules for a predicate

| Rule | Why |
|---|---|
| Must not block, yield, or nest `fiber_wait_for` | It runs under the runtime lock |
| Must not allocate | Same, and it may run on a target from a constrained context |
| Must be small | It is called once per parked fiber per `fiber_notify()` |
| **May mutate state** | This is what makes test-and-acquire expressible |
| `fiber_self()` is the *parked* fiber | Invariant 9 |

### 10.2 Mutex

Expressed as a test-and-acquire predicate:

```c
static bool_t mutex_acquire(void *data)
{
    struct fiber_mutex *m = data;
    if (m->owner == NULL) { m->owner = fiber_self(); return TRUE; }
    return FALSE;
}
```

The runtime lock held during evaluation is what makes test-and-acquire
atomic; there is no window between observing the mutex free and owning it.

- Owner-checked unlock: `unlock` by a non-owner returns `FIBER_ERR_INVALID`.
- Non-recursive: re-lock by the current owner returns `FIBER_ERR_INVALID`.
- `unlock` clears the owner and calls `fiber_notify()`.
- `trylock` is `fiber_wait_for(..., 0)` — never parks, so it is legal from
  main and from inside a critical section.

**No fairness guarantee.** v1.1's strict FIFO hand-off is gone: waiters are
served in ring order, not arrival order. This is a deliberate trade — WMB
needs no locking in its own code
([`12-platform-contract.md`](../../../docs/12-platform-contract.md) §6.5), so
the mutex exists for generality. If FIFO is ever required, the mutex can keep
a private queue without changing the core.

### 10.3 Condition variable

A monotonically increasing `seq`. `wait` snapshots it on the waiter's stack
(which stays alive while parked) and waits for it to differ; `signal`
increments it and notifies. Every waiter observes the change, so `signal`
remains a **broadcast**.

A `signal` with no waiters is still not stored — a later `wait` snapshots the
new value and waits for the next one. That is correct condition-variable
semantics, and it is why the predicate form is usually better: if what you
actually want is "wait until X", say so with `fiber_wait_for` and skip the
cond entirely.

### 10.4 Join

```c
static bool_t join_done(void *data)
{
    join_wait_t  *j = data;
    struct fiber *t = j->target;
    if (t->generation != j->generation) return TRUE;   /* slot recycled */
    if (t->state == FIBER_STATE_ZOMBIE) return TRUE;
    if (t->state == FIBER_STATE_FREE)   return TRUE;
    return FALSE;
}
```

- `target == fiber_self()` → `FIBER_ERR_INVALID`.
- `target == fiber_main()` → `FIBER_ERR_IMMORTAL`.
- Already `ZOMBIE`/`FREE` → returns immediately.
- Any number of fibers may join the same target.

**What `generation` protects, precisely.** It covers the case where the
target terminates, is reclaimed, and its FCB slot is handed to a *new* fiber
**while the joiner is parked**. Without it the joiner would compare a
different fiber's state and could block forever.

It does **not** validate a handle that was already stale when `fiber_join`
was called: the generation is captured at call time, so a stale pointer
captures the *new* fiber's generation and the join waits on the wrong fiber.
Joining an already-destroyed handle is undefined. Detecting that would
require `fiber_create` to return a `{pointer, generation}` handle, which is
a larger API change than the problem currently justifies.

### 10.5 `hal_task_*` — WMB's task contract

[`hal_task.c`](../hal_task.c) implements all nine functions of
[`hal_task.h`](../../library/hal_task.h) over this layer. It lives here, not
in a port tree, because `hal_task.h` is the one platform-contract header a
platform does **not** implement
([`90-decisions.md`](../../../docs/90-decisions.md) D‑31).

`hal_task_wait` is `fiber_wait_for` on a per-task wake flag; `hal_task_wake`
sets the flag and notifies. Because the flag is consumed *inside* the
predicate — under the runtime lock — the test-and-clear atomicity that
[`12-platform-contract.md`](../../../docs/12-platform-contract.md) §6.2.2 rule
W4 demands is automatic rather than hand-written.

### 10.6 Building a message queue

Explicitly not provided (§1). The idiom is three lines:

```c
static bool_t q_has_item(void *d) { return ((queue_t *)d)->count != 0; }

/* consumer */
if (fiber_wait_for(q_has_item, &q, 1000u)) { item = q_pop(&q); }

/* producer */
q_push(&q, item);
fiber_notify();
```

Give the wait a real timeout rather than `FIBER_WAIT_FOREVER` unless a
missing `fiber_notify()` should hang the system.

---

## 11. Resolved Design Decisions

1. **Static stack — IN.** `fiber_create_static(func, arg, buf, size)` ships
   alongside `fiber_create`. The FCB always comes from the static pool, so a
   `_static` fiber needs **zero** heap allocations. Mis-aligned buffers cause
   `NULL`. `owns_stack = 0` makes reclaim skip the heap free.

2. **`fiber_create` uses a private heap.** `fiber_heap[FIBER_HEAP_SIZE]` is
   fed to `heap_init()` at runtime init. No `malloc()` dependency.

3. **Critical section is a first-class API.** Nestable, masks interrupts at
   the outermost level, and suppresses the preemptive tick for its full
   duration. It is the sanctioned primitive for any structure shared between
   two fibers or between a fiber and an ISR, and it is what
   `framework/library/buffer.c` and `pipe.c` use under `USE_FIBER`.

4. **`fiber_join` — IN.** The original work-around (a caller-owned
   `volatile bool_t done` plus a polling loop) is now literally what
   `fiber_wait_for` does, with the polling removed.

5. **Hosted port — SHIPPED.** `linux_hosted/fiber_linux/` implements the port
   contract and is what every current build runs on. It is cooperative-only:
   see §6.5.

6. **One blocking primitive — NEW in v2.0.** Mutex, cond, join and
   `hal_task_*` are all `fiber_wait_for` underneath. Adding a fourth
   mechanism should be resisted; adding a fourth *predicate* costs nothing.

---

## 12. Worked Example

```c
#include "fiber.h"

typedef struct { int  items; } queue_t;
static queue_t   q;
static fiber_t  *producer_f;

static bool_t queue_ready(void *data) { return ((queue_t *)data)->items > 0; }

static void producer_fiber(void *arg)
{
    (void)arg;
    for (;;) {
        fiber_wait_for(NULL, NULL, 0u);   /* no-op; illustrative */
        q.items++;
        fiber_notify();                   /* hint: a consumer may proceed */
        fiber_yield();
    }
}

static void consumer_fiber(void *arg)
{
    (void)arg;
    for (;;) {
        if (fiber_wait_for(queue_ready, &q, 1000u) == FALSE) {
            continue;                     /* timed out - re-check, log, etc. */
        }
        q.items--;
        /* consume */
    }
}

int main(void)
{
    static uint8_t prod_stk[4096] __attribute__((aligned(16)));
    static uint8_t cons_stk[4096] __attribute__((aligned(16)));

    fiber_runtime_init(DEFAULT_PREEMPTION_TIME_MS);   /* accepted, ignored - 6.5 */

    producer_f = fiber_create_static(producer_fiber, NULL, prod_stk, sizeof prod_stk);
    (void)fiber_create_static(consumer_fiber, NULL, cons_stk, sizeof cons_stk);

    for (;;) fiber_yield();   /* main is immortal and stays runnable */
}
```

See [fiber-user-reference-guide.md](fiber-user-reference-guide.md) for a
fuller tour of the API and idioms.
