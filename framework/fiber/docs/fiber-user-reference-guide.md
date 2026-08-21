# Fiber User Reference Guide

**Audience:** Developers writing application or firmware code on top of the
fiber runtime.
**Headers to include:** [`fiber.h`](../fiber.h) for lifecycle, waiting and the
critical section; [`fiber_sync.h`](../fiber_sync.h) for mutex / condition
variable / join.
**Design background:** [fiber-architecture.md](fiber-architecture.md).
**Applies to:** v2.0.

---

## 1. Mental Model in 30 Seconds

- The runtime gives you **cooperative fibers** on a single CPU. Every fiber
  has a private stack; they all share global state.
- One fiber is the **main fiber** — the original `main()` thread of execution.
  It is created by `fiber_runtime_init()` and is immortal.
- Fibers are scheduled **round-robin** in creation order:
  `main → f1 → f2 → … → main`.
- The scheduler runs on every `fiber_yield()`. A preemptive tick exists in the
  design as a *safety net*, but it is **disabled today** — see §4.
- **There is one way to block: `fiber_wait_for()`.** Mutex, condition variable
  and join are all built from it. So is `hal_task_wait()`.

The single idea worth internalising:

> You wait on a **predicate**, not on a signal. A predicate is level-triggered,
> so there is no edge to miss and no lost-wakeup bug to write.

---

## 2. Bootstrapping the Runtime

Call exactly once, from the platform `main()`, before creating any fiber:

```c
#include "fiber.h"

int main(void)
{
    /* board init: clocks, UART, heap, timer … */

    fiber_runtime_init(DEFAULT_PREEMPTION_TIME_MS);

    /* create fibers, then yield forever */
    for (;;) fiber_yield();
}
```

- The caller becomes the main fiber.
- `preemption_ms` is **accepted and ignored** on every port today (§4). It is
  in the signature so that arming the tick later changes no call sites.

---

## 3. Creating Fibers

### 3.1 Heap-backed

```c
static void worker(void *arg) { /* … */ }

fiber_t *w = fiber_create(worker, my_arg, /*stack_size=*/1024);
if (!w) { /* pool or heap exhausted */ }
```

- Stack comes from the runtime's private `fiber_heap[]`, never the system
  allocator.
- `stack_size == 0` ⇒ `FIBER_DEFAULT_STACK`.
- `NULL` on FCB-pool exhaustion, heap exhaustion, or `stack_size < 256`.

### 3.2 Static buffer (recommended for production)

```c
static uint8_t worker_stk[1024] __attribute__((aligned(16)));

fiber_t *w = fiber_create_static(worker, my_arg, worker_stk, sizeof worker_stk);
```

- The caller owns the buffer for the lifetime of the fiber.
- Must be aligned to the port boundary: **16 B on RV32 and hosted**,
  **8 B on ARMv6-M**. `aligned(16)` is portable.

### 3.3 Where the new fiber slots into the ring

Spliced **immediately after the calling fiber**. If main creates `A` then `B`:
`main → A → B → main`. If `A` then creates `C`: `main → A → C → B → main`.

---

## 4. Yielding and Preemption

**Cooperative** — `fiber_yield()` from anywhere.

**Preemptive** — not today. `fiber_port_init()` ignores its period on every
port: no timer thread, no signal, no tick.

That is a decision with an exit condition, not a gap:
[`90-decisions.md`](../../../docs/90-decisions.md) **D‑47** holds the tick
disabled until four preconditions are met, because arming it makes every
unsynchronised read-modify-write reachable.

**Write your code as if it were armed.** Do not rely on a read-modify-write
being atomic merely because nothing can currently interrupt it. Use
`fiber_critical_enter/exit` (§6).

Put a `fiber_yield()` inside any long-running loop. With no tick, a compute
loop without a yield freezes the entire system.

---

## 5. Terminating Fibers

1. **Return** from the entry function — the trampoline marks `ZOMBIE`,
   notifies joiners, and triggers reclaim.
2. **`fiber_destroy(fiber_self())`** — same, never returns.
3. **`fiber_destroy(other)`** — flags `other` `ZOMBIE`; the scheduler frees
   its stack next time it walks past, so the destroyer does not wait.

```c
int rc = fiber_destroy(worker);
/* FIBER_OK | FIBER_ERR_IMMORTAL (was main) | FIBER_ERR_INVALID (NULL/FREE) */
```

Main is immortal: `fiber_destroy(fiber_main())` returns `FIBER_ERR_IMMORTAL`.

> **Do not keep using a `fiber_t *` after destroying it.** The FCB slot is
> recycled by the next `fiber_create`, so a stale pointer silently refers to a
> different fiber. See §10.

---

## 6. Critical Section

Wrap any read-modify-write of state shared with another fiber or an ISR:

```c
fiber_critical_enter();
shared.counter++;
shared.last_seen = now_ms();
fiber_critical_exit();
```

- Nestable; only the outermost `_exit` restores state.
- Masks **both** the preemption switch and interrupts.
- Callable from an ISR (it saves and restores the previous mask).
- **Do not** call `fiber_wait_for`, `fiber_mutex_lock`, `fiber_cond_wait`,
  `fiber_join` or `fiber_yield()` inside one. The blocking calls refuse
  (returning `FALSE` / `FIBER_ERR_INVALID`); yielding with IRQs masked would
  deadlock. `fiber_mutex_trylock` is safe — it never parks.

Keep them short: every cycle inside is latency added to every IRQ.

This is the sanctioned primitive for shared structures, and it is what
`framework/library/buffer.c` and `pipe.c` use under `USE_FIBER`.

---

## 7. Waiting — `fiber_wait_for`

```c
bool_t fiber_wait_for(check_cb_t check_cb, void *data, uint32_t timeout_ms);
void   fiber_notify(void);
```

Halts the caller until `check_cb(data)` returns `TRUE`, or `timeout_ms`
elapses. Returns `TRUE` if the predicate was satisfied, `FALSE` on timeout.

```c
typedef struct { int count; } queue_t;
static queue_t q;

static bool_t q_has_item(void *d) { return ((queue_t *)d)->count != 0; }

/* consumer */
if (fiber_wait_for(q_has_item, &q, 1000u) == TRUE) {
    q.count--;
    /* consume */
}

/* producer - anywhere, any fiber */
q.count++;
fiber_notify();
```

### 7.1 There is no producer-side wait API

Fibers communicate **through the data the predicate inspects**. The producer
mutates state and calls `fiber_notify()`; it never names the consumer, never
holds a handle to it, and cannot wake the wrong one.

`fiber_notify()` is a **hint, not a handshake**. Forgetting it does not corrupt
anything and does not lose a signal — the waiter simply stays parked until its
timeout, then re-evaluates. Correctness depends on the predicate; only latency
depends on the notify.

That is why you should pass a real timeout rather than `FIBER_WAIT_FOREVER`
unless a missing notify genuinely should hang the system.

### 7.2 Rules for a predicate

| Rule | Why |
|---|---|
| No blocking, no yielding, no nested `fiber_wait_for` | It runs under the runtime lock |
| No allocation | Same |
| Keep it small | Called once per parked fiber per `fiber_notify()` |
| **It may mutate state** | This is what makes test-and-acquire possible |
| `fiber_self()` is the **parked** fiber, not the notifier | Predicates run on the notifier's stack, on the waiter's behalf |

### 7.3 Behaviour worth knowing

| Situation | Result |
|---|---|
| Predicate already `TRUE` at the call | Returns `TRUE` immediately; never parks; no notify needed |
| `timeout_ms == 0` | *Poll-once*: evaluate, return, never park. Legal inside a critical section |
| Called from `main` | Polls instead of parking, so main stays runnable. Same semantics |
| Called inside a critical section with a non-zero timeout | Returns `FALSE` without waiting |
| `check_cb == NULL` | Returns `FALSE` |

---

## 8. Mutex

Non-recursive, owner-checked.

```c
static fiber_mutex_t *uart_mtx;

void board_init(void) { uart_mtx = fiber_mutex_create(); }

void log_line(const char *s) {
    fiber_mutex_lock(uart_mtx);
    uart_write(s);
    fiber_mutex_unlock(uart_mtx);
}
```

| Call | From main? | From critical section? | Notes |
|---|---|---|---|
| `fiber_mutex_create` / `_destroy` | yes | yes | non-blocking |
| `fiber_mutex_lock` | **yes** (polls) | no | blocking |
| `fiber_mutex_trylock` | yes | **yes** | never parks |
| `fiber_mutex_unlock` | only if owner | only if owner | clears owner, notifies |

- Re-locking by the current owner returns `FIBER_ERR_INVALID` (non-recursive).
- `unlock` by a non-owner returns `FIBER_ERR_INVALID`.
- Pool size `FIBER_MUTEX_MAX`; `_create` returns `NULL` when full.

> **No fairness guarantee.** v1.1's strict FIFO hand-off is gone. Waiters are
> served in **ring order**, not arrival order, so a mutex under permanent
> contention can starve a waiter. If you need FIFO, do not use this mutex.

---

## 9. Condition Variable (Broadcast)

```c
static fiber_cond_t   *evt_cv;
static volatile bool_t evt_pending;

void produce_event(void) {
    fiber_critical_enter();
    evt_pending = TRUE;
    fiber_critical_exit();
    fiber_cond_signal(evt_cv);          /* broadcast */
}

void consumer(void *arg) {
    for (;;) {
        fiber_cond_wait(evt_cv);
        if (!evt_pending) continue;     /* always re-check */
        fiber_critical_enter();
        evt_pending = FALSE;
        fiber_critical_exit();
        handle_event();
    }
}
```

- `fiber_cond_signal` is a **broadcast**. There is no `signal_one`.
- A signal with no waiters is **not latched**. Pair it with a predicate flag
  whenever the wake matters.
- No cond–mutex coupling (unlike POSIX). Protect your predicate yourself.

> **Consider `fiber_wait_for` instead.** If the pattern is "wait until X",
> say that directly — it is one call, it cannot drop the edge, and it takes a
> timeout. The condition variable earns its place only when you genuinely want
> broadcast wake semantics without a shared predicate.

---

## 10. Join — Wait for Termination

```c
fiber_t *child = fiber_create_static(work_fn, NULL, stk, sizeof stk);
int rc = fiber_join(child);      /* FIBER_OK once child reached ZOMBIE */
```

- Illegal on `fiber_self()` (`FIBER_ERR_INVALID`) or `fiber_main()`
  (`FIBER_ERR_IMMORTAL`), and inside a critical section.
- Idempotent: joining an already-terminated target returns immediately.
- Any number of fibers may join the same target.

> **Joining an already-destroyed handle is undefined.** The runtime detects a
> target that dies *while you are parked on it* — including the case where its
> slot is recycled to a new fiber — via a generation counter. It cannot detect
> a handle that was stale **before** `fiber_join` was called, because the
> generation is captured at call time. Do not call `fiber_join(f)` after
> `fiber_destroy(f)` has been reaped.

---

## 11. What Each Call Returns

| Call | Success | Common failure |
|---|---|---|
| `fiber_runtime_init` | `void` | — |
| `fiber_create` / `_static` | non-NULL `fiber_t *` | `NULL` (pool/heap, alignment, size) |
| `fiber_yield` | `void` | — |
| `fiber_destroy` | `FIBER_OK` | `FIBER_ERR_IMMORTAL`, `FIBER_ERR_INVALID` |
| **`fiber_wait_for`** | `TRUE` (predicate satisfied) | `FALSE` (timeout, NULL cb, or in a critical section) |
| **`fiber_notify`** | `void` | — (no-op if re-entered from a predicate) |
| `fiber_critical_enter` / `_exit` | `void` | — (unmatched `_exit` is a no-op) |
| `fiber_self` / `_main` | non-NULL `fiber_t *` | — |
| `fiber_mutex_create` / `fiber_cond_create` | non-NULL | `NULL` (pool exhausted) |
| `fiber_mutex_lock` | `FIBER_OK` | `FIBER_ERR_INVALID` (in critical, not in use, double-lock) |
| `fiber_mutex_trylock` | `FIBER_OK` | `FIBER_ERR_INVALID` (held by another) |
| `fiber_mutex_unlock` | `FIBER_OK` | `FIBER_ERR_INVALID` (not owner) |
| `fiber_cond_wait` | `FIBER_OK` | `FIBER_ERR_INVALID` (in critical, not in use) |
| `fiber_cond_signal` | `FIBER_OK` | `FIBER_ERR_INVALID` (not in use) |
| `fiber_join` | `FIBER_OK` | `FIBER_ERR_INVALID` / `FIBER_ERR_IMMORTAL` |

---

## 12. Configuration Knobs (`fiber_config.h`)

Override from the build (e.g. `-DFIBER_MAX=16`):

| Macro | Default | Meaning |
|---|---:|---|
| `FIBER_MAX` | `8` | FCB pool size (including main) |
| `FIBER_DEFAULT_STACK` | `2048` | Stack bytes when `fiber_create` gets `0` |
| `FIBER_DEFAULT_PREEMPTION_MS` | `1` | Tick count per slice (unused while §4 holds) |
| `FIBER_CANARY` | `0xDEADBEEF` | Sentinel at stack base; checked every dispatch |
| `FIBER_HEAP_SIZE` | `FIBER_MAX × (FIBER_DEFAULT_STACK + 64)` | Bytes for `fiber_create` stacks |
| `FIBER_MUTEX_MAX` | `8` | Static `fiber_mutex_t` pool |
| `FIBER_COND_MAX` | `8` | Static `fiber_cond_t` pool |

Defaults are microcontroller-sized. Hosted builds must override them — see
[`12-platform-contract.md`](../../../docs/12-platform-contract.md) §6.4.

Builds linking the fiber core must also define **`USE_FIBER`**, which
`fiber.cmake` exports as `FIBER_DEFINES`.

---

## 13. Common Patterns

### 13.1 Producer–consumer

The v2.0 form. No mutex, no condition variable, no lost wakeup:

```c
static bool_t not_empty(void *d) { return ((queue_t *)d)->count > 0; }
static bool_t not_full (void *d) { return ((queue_t *)d)->count < Q_SIZE; }

void enqueue(const msg_t *m) {
    if (fiber_wait_for(not_full, &q, 1000u) == FALSE) return;   /* dropped */
    q.buf[q.count++] = *m;
    fiber_notify();
}

bool_t dequeue(msg_t *out) {
    if (fiber_wait_for(not_empty, &q, 1000u) == FALSE) return FALSE;
    *out = q.buf[--q.count];
    fiber_notify();
    return TRUE;
}
```

With more than one consumer, do the decrement **inside** the predicate so the
claim is atomic with the test — the same trick the mutex uses:

```c
static bool_t take_one(void *d) {
    queue_t *qq = d;
    if (qq->count > 0) { qq->count--; return TRUE; }
    return FALSE;
}
```

### 13.2 Supervisor waits for a one-shot child

```c
static void child(void *arg)      { do_long_init(); }   /* return ⇒ ZOMBIE */
static void supervisor(void *arg) { fiber_join((fiber_t *)arg);
                                    log_line("child finished"); }
```

### 13.3 Periodic work

```c
static bool_t never(void *d) { (void)d; return FALSE; }

static void ticker(void *arg) {
    for (;;) {
        (void)fiber_wait_for(never, NULL, 100u);   /* sleep ~100 ms */
        do_periodic_work();
    }
}
```

A predicate that is never true plus a timeout is the idiomatic sleep.

---

## 14. Pitfalls and How to Avoid Them

| Symptom | Likely cause | Fix |
|---|---|---|
| Waiter never wakes although the state changed | Producer did not call `fiber_notify()` | Add it — or rely on the timeout, which is why you should pass one |
| `fiber_wait_for` returns `FALSE` immediately | Called inside a critical section with a non-zero timeout | Close the critical section first; or use `timeout_ms == 0` to poll |
| Two fibers both think they hold a resource | Predicate tested but did not **claim**. Test-and-acquire must happen inside the predicate | Do the mutation in the callback, as in §13.1 |
| Predicate sees the wrong `fiber_self()` | You assumed it runs on the waiter's stack | It runs on the notifier's; `fiber_self()` is scoped to the parked fiber, everything else is the notifier's context |
| Whole system freezes | A compute loop with no `fiber_yield()` — there is no preemptive tick to rescue you (§4) | Add a yield |
| `fiber_create_static` returns `NULL` | Mis-aligned stack, size < 256, or `FIBER_MAX` reached | `aligned(16)`; bump `FIBER_MAX` |
| `FIBER_ERR_OVERFLOW` crash | Stack canary tripped | Increase stack; review recursion and large locals |
| `fiber_join` blocks forever | Target handle was already stale — you joined a recycled slot | Do not use a `fiber_t *` after its fiber is reaped (§10) |
| Mutex waiter starves | Ring order, not FIFO (§8) | Restructure, or do not use the mutex for contended hot paths |

---

## 15. Quick API Cheat-Sheet

```c
/* lifecycle */
fiber_runtime_init(ms);                   /* ms currently ignored */
fiber_t *f = fiber_create(fn, arg, stack_size);
fiber_t *f = fiber_create_static(fn, arg, buf16aligned, size);
fiber_yield();
fiber_destroy(f);                         /* or fiber_destroy(fiber_self()) */

/* introspection */
fiber_self();  fiber_main();  fiber_is_main(f);

/* THE blocking primitive */
bool_t ok = fiber_wait_for(predicate, data, timeout_ms);   /* FIBER_WAIT_FOREVER */
fiber_notify();                           /* hint after changing shared state */

/* critical section (nestable, masks IRQs + suppresses preemption) */
fiber_critical_enter(); /* … */ fiber_critical_exit();

/* mutex (non-recursive, owner-checked, NOT fair) */
fiber_mutex_t *m = fiber_mutex_create();
fiber_mutex_lock(m); /* … */ fiber_mutex_unlock(m);
fiber_mutex_trylock(m);                   /* safe inside a critical section */

/* condition variable (signal == broadcast, no edge latched) */
fiber_cond_t *c = fiber_cond_create();
fiber_cond_wait(c);  fiber_cond_signal(c);

/* join */
fiber_join(other);                        /* not self, not main, not in critical */
```

For scheduler internals, the port contract and the FCB layout, see
[fiber-architecture.md](fiber-architecture.md).
