# Hosted-Build Interrupt Simulation

> ## ⚠ DESIGN NOTE — not implemented
>
> This describes how a preemption tick *would* be simulated on the hosted
> port. None of it is built: the hosted port installs no timer thread and no
> signal handler, and `fiber_port_init()` ignores its period.
>
> That is [`90-decisions.md`](../../../docs/90-decisions.md) **D‑47** — the
> tick is held disabled until its four preconditions are met. This document
> is the plan for when they are.
>
> Current state: [fiber-architecture.md](fiber-architecture.md) §6.5.

**Companion to:** [fiber-architecture.md](fiber-architecture.md) · [fiber-implementation-plan.md](fiber-implementation-plan.md)
**Status:** Draft v1.0
**Scope:** The mechanism by which the **hosted** back-end (`arch/hosted/`) emulates the bare-metal interrupt-controller behaviour that the firmware expects. Everything described here lives entirely below `fiber_port.h` — the generic fiber layer and the application code are oblivious to it.

---

## 1. Requirement

The hosted process runs **exactly two pthreads**:

| Thread | Role |
|---|---|
| **`app_thread`** (the process's original `main()`) | Runs the application, owns the fibers, owns the `main` fiber's stack. |
| **`irq_thread`** (created at `fiber_runtime_init`) | Watches a pending-interrupt set. When something becomes pending, it must (a) **block `app_thread` from making progress**, (b) **run the registered ISR**, and (c) **unblock `app_thread`** before going back to sleep. |

Both threads are scheduled by the Linux kernel using whatever default policy (`SCHED_OTHER`) the process inherits — we do **not** require `SCHED_FIFO`, real-time priorities, or `CAP_SYS_NICE`.

The behaviour must be indistinguishable, from the application's point of view, from the silicon model: an ISR appears to fire asynchronously, runs to completion with the foreground execution paused, and returns control to whatever instruction the foreground was about to execute.

---

## 2. Why POSIX Signals Are the Right Primitive

Linux offers no portable user-space way for one thread to atomically suspend another, run code "in its place", and resume it. The **only** primitive that delivers *exactly* "pause this specific thread, run a handler, then resume it where it left off" is **POSIX signal delivery via `pthread_kill`**.

When `irq_thread` calls `pthread_kill(app_tid, SIGRTMIN+n)`:

1. The kernel marks the signal pending on `app_thread`.
2. At the next user-space instruction boundary in `app_thread` (or immediately, if it is currently in a cancellable syscall), the kernel pushes a signal frame onto `app_thread`'s stack.
3. `app_thread` runs the installed handler **on its own stack, with its own TID**, while its previous execution is frozen mid-flight.
4. `sigreturn` restores `app_thread` to the exact register state it had before the signal arrived.

This is **bit-for-bit isomorphic** to a real CPU servicing an IRQ: the foreground is preempted, the ISR runs on the foreground's stack, and the foreground resumes transparently. The fiber subsystem's existing assumption — that ISR code runs in the same execution context as the preempted code — therefore holds unchanged on the host.

`irq_thread` itself never executes the ISR body; its job is **purely dispatch**. This split lets the kernel do the atomic "block/unblock" for us instead of us trying to invent it with mutexes (see §7 for the rejected alternative and why).

---

## 3. Architecture

```
            ┌──────────────────────────────────────────────────┐
            │                  app_thread                      │
            │  ─ runs main() and all fibers                    │
            │  ─ owns the signal mask for SIGRTMIN+n           │
            │  ─ signal handler == hosted_isr_dispatch()       │
            └──────────────────────────────────────────────────┘
                            ▲                  │
            pthread_kill    │                  │  sigprocmask
            (preempt)       │                  │  (intr_global_set_enabled)
                            │                  ▼
            ┌──────────────────────────────────────────────────┐
            │                  irq_thread                      │
            │  loop:                                           │
            │     sem_wait(&irq_pending_sem)                   │
            │     while ((vec = next_pending()) >= 0):         │
            │         pthread_kill(app_tid, SIGRTMIN+vec)      │
            │         sem_wait(&irq_done_sem) /* see §5 */     │
            └──────────────────────────────────────────────────┘
                            ▲
                            │ sem_post(&irq_pending_sem)
                            │
            ┌──────────────────────────────────────────────────┐
            │   timer_thread  /  hosted peripheral models      │
            │  (e.g. the periodic preemption tick — §6)        │
            └──────────────────────────────────────────────────┘
```

`timer_thread` is a third pthread, but it never touches `app_thread` directly; it raises a "pending interrupt" and lets `irq_thread` dispatch it through the standard path. Other simulated peripherals (UART RX, etc.) follow the same pattern — every "I want to interrupt the CPU" path funnels through `hosted_irq_raise(vector)`.

---

## 4. Data Structures

```c
/* arch/hosted/hosted_intr.c -- private state */

#define HOSTED_IRQ_MAX        16      /* matches INTR_VECTOR_MAX */
#define HOSTED_SIG_BASE       (SIGRTMIN)
#define HOSTED_SIG_OF(vec)    (HOSTED_SIG_BASE + (vec))

static pthread_t        app_tid;                       /* set by fiber_runtime_init */
static pthread_t        irq_tid;
static intr_hdl_t       isr_table[HOSTED_IRQ_MAX];     /* registered via intr_handler_set_vector */
static _Atomic uint32_t pending_mask;                  /* bit n = vector n pending */
static _Atomic uint32_t enable_mask;                   /* bit n = vector n enabled */
static sem_t            irq_pending_sem;               /* posted by hosted_irq_raise */
static sem_t            irq_done_sem;                  /* posted by signal handler -- §5 */
```

The pending and enable masks are **plain atomics**, not mutex-protected — every operation on them is a single load/store/CAS. This keeps `hosted_irq_raise` callable from any thread (including from inside an ISR, mirroring the silicon "an ISR can pend another interrupt" behaviour) without any lock-ordering questions.

---

## 5. The Dispatch Sequence

```
┌─────────────────────────────────────────────────────────────────────────┐
│ peripheral_thread:                                                      │
│   pending_mask |= (1u << vec);                                          │
│   sem_post(&irq_pending_sem);          // wake irq_thread               │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ irq_thread (loop):                                                      │
│   sem_wait(&irq_pending_sem);                                           │
│   while (vec = pick_one(pending_mask & enable_mask)) >= 0):             │
│       pending_mask &= ~(1u << vec);  // edge-clear, ISR may re-pend     │
│       pthread_kill(app_tid, HOSTED_SIG_OF(vec));  // *** preempt ***    │
│       sem_wait(&irq_done_sem);       // wait until ISR returns          │
└─────────────────────────────────────────────────────────────────────────┘
                                  │  signal delivered
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ app_thread signal handler  (hosted_isr_dispatch, installed for every   │
│ HOSTED_SIG_OF(vec) at runtime_init):                                    │
│     intr_hdl_t isr = isr_table[siginfo->si_signo - HOSTED_SIG_BASE];    │
│     if (isr) isr();          // exactly the same call shape as silicon  │
│     sem_post(&irq_done_sem); // tell irq_thread we are done             │
│     // sigreturn -> app_thread resumes its prior instruction            │
└─────────────────────────────────────────────────────────────────────────┘
```

The `irq_done_sem` handshake is the part that satisfies the user's requirement **"block main, run ISR, unblock main"** in the strictest sense:

- During `pthread_kill` → handler entry → handler body → `sem_post(&irq_done_sem)` → `sigreturn`, the kernel guarantees `app_thread` is **not executing user code from before the signal**. That is the "blocked" window.
- `irq_thread` does **not** post the next IRQ until `irq_done_sem` is signalled, which guarantees one ISR completes before another starts — exactly like a single-priority NVIC/PLIC level.
- Once `sigreturn` runs, `app_thread` resumes normally — that is the "unblock".

### Async-signal-safety
`hosted_isr_dispatch` runs in signal context, so the registered ISR body must obey the same restriction it already obeys on silicon: **no malloc, no printf, no general libc**. The firmware's existing ISRs already meet this bar (they only touch peripheral registers and atomic flags), so no porting effort is required.

---

## 6. The Preemption Tick

The periodic preemption tick that drives the fiber scheduler is just one peripheral among the rest. Implemented as a third thread:

```c
static void *timer_thread_fn(void *arg) {
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    const uint64_t period_ns = (uint64_t)tick_period_us * 1000ULL;

    for (;;) {
        next.tv_nsec += period_ns;
        next.tv_sec  += next.tv_nsec / 1000000000;
        next.tv_nsec %= 1000000000;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        hosted_irq_raise(INTR_VECTOR_TIMER);
    }
}
```

`clock_nanosleep(TIMER_ABSTIME)` keeps the cadence stable even if `irq_thread` or `app_thread` briefly steals CPU. The handler registered for `INTR_VECTOR_TIMER` is the same one the firmware already installs on RV32 / ARMv6-M — it ultimately calls `fiber_port_request_switch()`, which on the hosted port pends `INTR_VECTOR_PENDSV` (a synthetic vector) so the actual `swapcontext` runs from **its own** signal-handler frame, after the timer ISR has cleanly returned. This preserves the architecture-doc invariant that the swap never runs nested inside another ISR.

---

## 7. Rejected Alternative — "Pure Mutex" Preemption

Considered: have `app_thread` hold a global `app_mutex` continuously and release it only at "safe points"; `irq_thread` grabs the mutex when an IRQ is pending and runs the ISR on its own stack while `app_thread` blocks in `pthread_mutex_lock`.

**Rejected** because:
1. It is **cooperative**, not preemptive — `app_thread` only ever yields at safe points, so a busy-loop in a fiber would never be preempted. This breaks T2.2 in the implementation plan.
2. ISR running on `irq_thread`'s stack changes the execution context the firmware ISR sees. Any code that examines `pthread_self()`, TLS, or thread-local errno would behave differently on host vs. silicon.
3. Re-pending an interrupt from inside an ISR (a legitimate silicon pattern) would deadlock unless the ISR ran in a recursive mutex *and* `app_thread` carefully avoided holding the lock across long operations — fragile.

The signal-based design has none of these properties.

---

## 8. Public Symbols Exported by `arch/hosted/hosted_intr.c`

These satisfy the link-time contract the firmware already expects from `arch/include/intr_handler.h` and friends, so existing `lib_c/critical_section.c`, `lib_c/exception.c`, and the drivers link unchanged:

| Symbol | Behaviour on host |
|---|---|
| `intr_handler_init()` | Creates `irq_thread` and `timer_thread`, captures `app_tid = pthread_self()`, installs `hosted_isr_dispatch` for every `HOSTED_SIG_OF(vec)` via `sigaction(SA_RESTART | SA_SIGINFO)`. |
| `intr_handler_set_vector(vec, hdl)` | `isr_table[vec] = hdl;` (plain store — vector table writes are serialised by the application anyway). |
| `intr_handler_set_enable(vec, en)` | Atomic set/clear of `enable_mask` bit. |
| `intr_handler_set_ack(vec)` | Atomic clear of `pending_mask` bit. (Most peripherals self-ack; this is for parity.) |
| `intr_global_set_enabled(en)` | `pthread_sigmask(SIG_BLOCK | SIG_UNBLOCK, &hosted_sigset, …)` on `app_thread`. Matches silicon's `mstatus.MIE` / `PRIMASK` exactly: pending IRQs stay pending and fire as soon as the mask is dropped. |
| `intr_trigger_swi()` | `hosted_irq_raise(INTR_VECTOR_SWI)` — direct synthetic-vector pend, used by `fiber_port_request_switch`. |

---

## 9. Invariants

1. `app_tid` is captured **before** `irq_thread` and `timer_thread` are spawned, so the dispatcher never has a window where it could signal an uninitialised TID.
2. The signal set used for hosted IRQs is **blocked on every thread except `app_thread`**. `pthread_create` for `irq_thread` and `timer_thread` is preceded by `pthread_sigmask(SIG_BLOCK, &hosted_sigset, &saved)` and followed by a restore — so a stray timer fire can never preempt the dispatcher itself.
3. `hosted_isr_dispatch` always posts `irq_done_sem` exactly once per invocation, even if the registered ISR is `NULL` (defensive — keeps the dispatcher loop from stalling on a missing vector).
4. `intr_global_set_enabled(FALSE)` is **always** matched by a later `(TRUE)` on the same thread; the firmware's existing `critical_section_enter/exit` already enforces this and we adopt it unchanged.
5. The synthetic `INTR_VECTOR_SWI` priority — i.e., dispatch order — is **last** among any vectors pending at the same instant, so a swap is never scheduled while another ISR's effects are still settling. Implemented in `pick_one()` by masking SWI out until no other bits remain.

---

## 10. Portability Notes

- **Linux:** native pthreads + `SIGRTMIN`. Tested target.
- **macOS:** `SIGRTMIN` is not defined; substitute a fixed range starting at `SIGUSR1` and reduce `HOSTED_IRQ_MAX` to fit the available signal numbers (we only actually need TIMER + SWI + 2–3 peripheral vectors for v1).
- **Windows (MinGW + pthreads-w32):** POSIX signals are not delivered to specific threads. Fall back to the "pure mutex" alternative from §7 — gated by `#ifdef _WIN32` — accepting the cooperative-only limitation on that host. Document this as a known v1 caveat; if true preemption on Windows is required, switch to `SuspendThread`/`GetThreadContext`/`SetThreadContext` in a v1.1 follow-up.

---

## 11. Test Plan (additions to the implementation plan)

| ID | Test | Pass criterion |
|---|---|---|
| H1 | `hosted_irq_raise(VEC_TIMER)` from `irq_thread` → installed ISR fires on `app_thread` (verify with `pthread_self()` log) | TID logged matches `app_tid` |
| H2 | ISR re-pends its own vector → second invocation occurs after first returns, not nested | Counter increments by 2; no nested-handler assert |
| H3 | `intr_global_set_enabled(FALSE)` followed by a raise → ISR does **not** run until re-enabled | Latency between raise and ISR == duration of the disabled window (± tick jitter) |
| H4 | Two vectors pending simultaneously → both ISRs run, SWI runs last | Log order: VEC_TIMER, VEC_OTHER, VEC_SWI |
| H5 | Long burst (1 M raises in 10 s) under valgrind/helgrind | No data race reports, no leaks |
| H6 | Kill `irq_thread` (simulated by cancel) → process exits cleanly, no SIGSEGV | `pthread_join` returns; `atexit` hook clears semaphores |
