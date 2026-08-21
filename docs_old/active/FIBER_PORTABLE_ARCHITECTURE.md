# Portable Fiber Library — Architecture for WMB

**Status:** Architectural proposal
**Date:** 2026-05-29
**Scope:** Adapt the existing `framework/fiber/` runtime (from `firmware_bringup`) into a portable fiber library for the WMB project, replacing the tima/SDL/host reliance on Linux threads and unifying the cooperative scheduling model.

---

## 1. Problem Statement

The WMB project (§2.8 of PROJECT_KNOWLEDGE_BASE) requires:

1. **No pthreads** — cooperative context switching to mimic Tima OS.
2. **Single OS thread** — one process, many cooperative "apps" (HAL pump, TinyX server, JWM, internal clients).
3. **Abstraction via HAL** — WMB code calls `hal_task_*` only; never ucontext/platform primitives directly.
4. **Two build profiles** — `WMB_HOSTED` (Linux + SDL + libucontext) and `WMB_TARGET_TIMA` (Tima OS task primitives).

The existing `framework/fiber/` code was designed for bare-metal (RV32IMAC / ARMv6-M) with preemptive tick-driven scheduling. It needs to be adapted for the **hosted** profile where:
- Preemption is simulated via POSIX signals (not a hardware timer ISR).
- Stacks are large (64–256 KB, `mmap`'d with guard pages).
- The external contract is `hal_task_*`, not `fiber_*` directly (though fiber is the substrate).

---

## 2. Existing Fiber Runtime — What We Have

From `framework/fiber/`:

| Component | Role | Portable? |
|---|---|---|
| `fiber.h` | Public API: create, yield, destroy, critical section | Yes — fully generic |
| `fiber_sync.h` | Mutex, condvar, join | Yes — fully generic |
| `fiber_config.h` | Tunables: FIBER_MAX, stack sizes, canary | Yes — build-flag overridable |
| `fiber.c` | Scheduler, FCB pool, ring, trampoline, canary | Yes — zero arch dependency |
| `fiber_sync.c` | Mutex/cond/join on top of fiber.c internals | Yes — zero arch dependency |
| `fiber_internal.h` | Shared struct fiber layout | Yes — sp at offset 0 is the only arch contract |
| `fiber_port.h` | Port contract: init_stack, request_switch, irq_lock/unlock | Interface only — each arch implements |

**Key insight:** The generic layer (`fiber.c`, `fiber_sync.c`) is already portable and proven. What's missing is a **hosted port** (`arch/hosted/`) that implements `fiber_port.h` using libucontext + POSIX signals.

---

## 3. Architecture: Three Layers

```
┌────────────────────────────────────────────────────────────────────────┐
│  WMB Application Code (TinyX, JWM, internal clients)                   │
│    calls: hal_task_create / hal_task_yield / hal_yield_timeout         │
│           hal_task_wake / hal_task_wait                                │
└────────────────────────┬───────────────────────────────────────────────┘
                         │ (thin mapping layer)
┌────────────────────────▼───────────────────────────────────────────────┐
│  HAL Task Layer  (framework/library/hal_task.h + hal_task_hosted.c)    │
│    - Maps hal_task_* → fiber_* calls                                   │
│    - Adds: named tasks, timeout-based yield, event-driven wake         │
│    - Owns the "run queue" policy (round-robin, I/O priority for HAL)   │
│    - On target: replaced by hal_task_tima.c (Tima OS primitives)       │
└────────────────────────┬───────────────────────────────────────────────┘
                         │
┌────────────────────────▼───────────────────────────────────────────────┐
│  Fiber Core  (framework/fiber/)                                        │
│    fiber.c + fiber_sync.c + fiber_internal.h                           │
│    Pure C, zero platform dependency, static pools                      │
└────────────────────────┬───────────────────────────────────────────────┘
                         │ (fiber_port.h contract)
┌────────────────────────▼───────────────────────────────────────────────┐
│  Port Backend                                                          │
│    ┌─────────────────────────────┐  ┌────────────────────────────────┐ │
│    │ arch/hosted/                │  │ arch/emulator_riscv/           │ │
│    │  fiber_port_hosted.c        │  │  fiber_port.c + fiber_switch.S │ │
│    │  (libucontext + signals)    │  │  (MSIP + register swap asm)    │ │
│    └─────────────────────────────┘  └────────────────────────────────┘ │
│    ┌─────────────────────────────┐                                     │
│    │ arch/tima/                  │                                     │
│    │  fiber_port_tima.c          │                                     │
│    │  (Tima OS task primitives)  │                                     │
│    └─────────────────────────────┘                                     │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Hosted Port Design (`arch/hosted/`)

### 4.1 Files

```
framework/
├── fiber/
│   ├── fiber.h, fiber_sync.h          (public, unchanged)
│   ├── fiber_config.h                 (tunables — override for hosted profile)
│   ├── fiber_port.h                   (contract — unchanged)
│   ├── fiber_internal.h               (shared struct — unchanged)
│   ├── fiber.c, fiber_sync.c          (generic core — unchanged)
│   └── arch/
│       └── hosted/
│           ├── fiber_port_hosted.c    (implements fiber_port.h via libucontext)
│           ├── hosted_intr.c          (POSIX signal-based interrupt simulation)
│           └── hosted_timer.c         (periodic tick thread)
├── library/
│   ├── hal_task.h                     (public HAL surface for WMB code)
│   └── hal_task_hosted.c             (maps hal_task_* → fiber_*)
└── third_party/
    └── libucontext/                   (vendored, built as STATIC lib)
```

### 4.2 Port Contract Implementation

| `fiber_port.h` symbol | Hosted implementation |
|---|---|
| `fiber_port_init(period_ms)` | Install SIGUSR1/SIGRTMIN handlers; spawn `timer_thread` (periodic `clock_nanosleep` + `hosted_irq_raise`) |
| `fiber_port_init_stack(stack_top, entry, arg)` | `libucontext_getcontext` + `libucontext_makecontext` targeting `fiber_trampoline`; store `entry`/`arg` in FCB; return context pointer as "sp" |
| `fiber_port_request_switch()` | `hosted_irq_raise(INTR_VECTOR_SWI)` → `pthread_kill(app_tid, signal)` → handler calls `fiber_sched_swap` |
| `fiber_port_irq_lock()` | `sigprocmask(SIG_BLOCK, &hosted_sigset, &prev)` — returns prev mask |
| `fiber_port_irq_unlock(prev)` | `sigprocmask(SIG_SETMASK, &prev, NULL)` |

### 4.3 The "sp" Reinterpretation

On bare-metal, `fiber_t::sp` is the actual CPU stack pointer. On the hosted port, **sp stores a pointer to the `libucontext_ucontext_t` context structure**. The scheduler's `fiber_sched_swap(cur_sp)` / return-new-sp contract still works:

```c
void *fiber_sched_swap(void *cur_sp);  // generic, in fiber.c
```

The hosted SWI handler wraps this:
```c
// In hosted signal handler (conceptual):
static void hosted_pendsv_handler(void) {
    // cur_sp = pointer to current fiber's ucontext
    void *cur_sp = &g_current_fiber_ucontext;
    void *new_sp = fiber_sched_swap(cur_sp);
    // swapcontext from current to the fiber whose ucontext is new_sp
    libucontext_swapcontext((libucontext_ucontext_t *)cur_sp,
                            (libucontext_ucontext_t *)new_sp);
}
```

### 4.4 Stack Allocation (Hosted Profile)

For the hosted build, fiber stacks are **large** (64–256 KB) and managed via `mmap`:

```c
void *hosted_stack_alloc(size_t size) {
    size_t total = size + PAGE_SIZE;  // extra page for guard
    void *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mprotect(base, PAGE_SIZE, PROT_NONE);  // guard page at bottom
    return base;  // usable region starts at base + PAGE_SIZE
}
```

This replaces the bare-metal `fiber_heap[]` static buffer. The override is controlled by:
- `FIBER_HOSTED_MMAP_STACKS` — when defined, `fiber_create()` uses `mmap` instead of the internal heap.
- `fiber_create_static()` remains unchanged (caller provides buffer).

### 4.5 Config Overrides for Hosted

```c
// fiber_config.h overrides for WMB hosted build:
#define FIBER_MAX                  16      // HAL + server + JWM + test clients
#define FIBER_DEFAULT_STACK        (64*1024)
#define FIBER_DEFAULT_PREEMPTION_MS  10    // cooperative primary; 10ms safety net
#define FIBER_MUTEX_MAX            16
#define FIBER_COND_MAX             16
```

---

## 5. HAL Task Layer — The WMB-Visible API

WMB code (TinyX, JWM, libX11-wmb) never calls `fiber_*` directly. It calls the HAL:

### 5.1 Public Interface (`hal_task.h`)

```c
#ifndef _hal_task_h_
#define _hal_task_h_

#include "types.h"
#include <stddef.h>

typedef struct hal_task hal_task_t;
typedef void (*hal_task_entry_t)(void *arg);

/* Create a named cooperative task. Returns NULL on failure. */
hal_task_t *hal_task_create(const char *name,
                            hal_task_entry_t entry,
                            void *arg,
                            size_t stack_bytes);

/* Destroy a task. If self, does not return. */
void  hal_task_destroy(hal_task_t *t);

/* Cooperative yield — round-robin to next ready task. */
void  hal_task_yield(void);

/* Yield with timeout: returns when either:
 *   - max_ms elapses (return 0)
 *   - this task is explicitly woken (return 1)
 * Used by tima_WaitForEvents() and the SDL HAL pump. */
int   hal_yield_timeout(uint32_t max_ms);

/* Wake a specific task (mark it runnable). */
void  hal_task_wake(hal_task_t *t);

/* Block until explicitly woken. */
void  hal_task_wait(void);

/* Get current task handle. */
hal_task_t *hal_task_self(void);

#endif /* _hal_task_h_ */
```

### 5.2 Hosted Implementation (`hal_task_hosted.c`)

Thin mapping to fiber primitives:

| `hal_task_*` | Implementation |
|---|---|
| `hal_task_create` | `fiber_create(entry, arg, stack_bytes)` + store name in wrapper struct |
| `hal_task_destroy` | `fiber_destroy(fiber)` |
| `hal_task_yield` | `fiber_yield()` |
| `hal_yield_timeout` | `fiber_cond_wait` with a timeout counter checked each time the task resumes (tick-based) |
| `hal_task_wake` | Set "woken" flag + `fiber_internal_wake_locked` if BLOCKED |
| `hal_task_wait` | Clear "woken" flag + `fiber_internal_block_self_locked` |

### 5.3 Timeout Implementation

`hal_yield_timeout(max_ms)` is the critical function — it must yield CPU to other tasks while tracking elapsed time:

```c
int hal_yield_timeout(uint32_t max_ms) {
    hal_task_t *self = hal_task_self();
    self->woken = 0;
    uint32_t start = os_get_tick_ms();

    while (!self->woken) {
        uint32_t elapsed = os_get_tick_ms() - start;
        if (elapsed >= max_ms) return 0;  // timeout
        fiber_yield();
    }
    return 1;  // woken explicitly
}
```

This is cooperative — on each `fiber_yield()`, other tasks (including the HAL pump and timer tick) run and advance the clock.

---

## 6. Integration with WMB Request/Reply Model

The `swapcontext`-based request/reply flow from §5.1.4 of the knowledge base maps directly:

```
[Client task: JWM]                    [Server task: TinyX]
  XMapWindow(dpy, w)
   └─ wmb_sync_handler(dpy)
        ├─ queue.push(request)
        ├─ hal_task_wake(server_task)
        └─ hal_task_wait()            ◄── fiber blocks client
                                      ──► scheduler picks server
                                      server processes request
                                      hal_task_wake(client_task)
                                      hal_task_yield()
                                      ◄── client resumes
   return
```

Each `hal_task_wait` / `hal_task_wake` pair = one `swapcontext` under the hood on the hosted build.

---

## 7. What Changes from the Existing fiber/ Code

| Area | Change | Rationale |
|---|---|---|
| `fiber_config.h` | Add `#ifdef WMB_HOSTED` section with larger defaults | 64–256KB stacks, 16 tasks |
| `fiber.c` `fiber_create()` | Conditional: use `mmap` instead of `heap_malloc` when `FIBER_HOSTED_MMAP_STACKS` | Large stacks don't fit in a static heap |
| `fiber_port.h` | No change — already arch-agnostic | — |
| New: `arch/hosted/fiber_port_hosted.c` | Implements port contract via libucontext | The missing hosted back-end |
| New: `arch/hosted/hosted_intr.c` | POSIX signal dispatch for interrupt simulation | Preemption fidelity |
| New: `arch/hosted/hosted_timer.c` | `clock_nanosleep` tick thread | Drives `fiber_tick_isr` |
| New: `hal_task.h` / `hal_task_hosted.c` | HAL-level task API wrapping fiber | WMB's only visible scheduling API |
| `fiber.cmake` | Conditional `include` of `arch/hosted/` sources | Build integration |
| Remove: SDL thread usage | Delete any `SDL_CreateThread` / `pthread_create` from WMB paths | Requirement §2.8 |

### 7.1 What Does NOT Change

- `fiber.c` — the scheduler, ring, canary, trampoline logic.
- `fiber_sync.c` — mutex/cond/join.
- `fiber_internal.h` — struct layout (sp at offset 0).
- The bare-metal ports (RV32, ARM) — untouched.
- TinyX / JWM / libX11-wmb source — they only see `hal_task_*`.

---

## 8. Dependency Map

```
WMB hosted binary
  ├── framework/library/hal_task_hosted.c   (hal_task_* → fiber_*)
  ├── framework/fiber/fiber.c               (scheduler core)
  ├── framework/fiber/fiber_sync.c          (sync primitives)
  ├── framework/fiber/arch/hosted/
  │     ├── fiber_port_hosted.c             (libucontext port)
  │     ├── hosted_intr.c                   (signal dispatch)
  │     └── hosted_timer.c                  (tick thread)
  ├── framework/third_party/libucontext/    (vendored static lib)
  └── libpthread (link only for timer_thread + irq_thread)

WMB target binary (Tima OS)
  ├── framework/library/hal_task_tima.c     (hal_task_* → Tima OS)
  ├── framework/fiber/fiber.c               (same core)
  ├── framework/fiber/fiber_sync.c          (same)
  └── framework/fiber/arch/tima/
        └── fiber_port_tima.c               (Tima OS context switch)
```

Note: `libpthread` is linked **only** for the two background threads (`timer_thread`, `irq_thread`) that simulate hardware interrupts. Application code and all fibers run in the **single `app_thread`** — satisfying the "no pthread in WMB logic" requirement.

---

## 9. Implementation Phases

### Phase A — Vendor libucontext + hosted port skeleton

1. Add `framework/third_party/libucontext/` (git submodule or copy).
2. Create `framework/fiber/arch/hosted/fiber_port_hosted.c` — implement `fiber_port_init_stack` and `fiber_port_request_switch` using `libucontext_makecontext` / `libucontext_swapcontext`.
3. Implement `fiber_port_irq_lock/unlock` via `sigprocmask`.
4. Stub `hosted_intr.c` and `hosted_timer.c`.
5. **Test:** two fibers ping-ponging via `fiber_yield()` on Linux.

### Phase B — Preemption simulation

1. Implement `hosted_timer.c` — `clock_nanosleep` loop raising `INTR_VECTOR_TIMER`.
2. Implement `hosted_intr.c` — `pthread_kill`-based dispatch.
3. Wire `fiber_tick_isr()` to the timer vector.
4. **Test:** busy-loop fiber gets preempted within 2× tick period.

### Phase C — HAL task layer

1. Implement `hal_task.h` + `hal_task_hosted.c`.
2. Implement `hal_yield_timeout()` with tick-based cooperative timeout.
3. Implement `hal_task_wait()` / `hal_task_wake()` using fiber sync primitives.
4. **Test:** producer/consumer pattern with timeout.

### Phase D — WMB integration

1. Replace SDL thread usage in `framework/sdl_library/` with `hal_task_create`.
2. Wire `tima_WaitForEvents()` in `tima_kdrive_system.c` to `hal_yield_timeout()`.
3. Wire `wmb_sync_handler` request/reply path through `hal_task_wait/wake`.
4. **Test:** SDL window opens, keyboard/mouse events flow, TinyX processes requests.

---

## 10. Risk Assessment

### 10.1 What Is NOT a Risk

The fiber core (`fiber.c`, `fiber_sync.c`, `fiber_internal.h`) is **proven working** on two embedded targets (RV32IMAC and ARMv6-M). Each target has a board-specific `fiber_port.c` + `fiber_switch.S` that implements the 5-function port contract. The scheduler, FCB pool, ring topology, canary checks, sync primitives and all lifecycle semantics are validated — they are not experimental.

The hosted port is **the same pattern**: implement `fiber_port_init_stack`, `fiber_port_request_switch`, `fiber_port_irq_lock/unlock`, and `fiber_port_init` — this time using libucontext instead of register-swap assembly. The port contract is narrow, well-defined, and exercised by two independent backends already. Adding a third (libucontext) is a bounded integration task, not a design risk.

### 10.2 Actual Risks (Hosted Port Specific)

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| `libucontext` package unavailable on specific host distro | Low | Low | Fall back to glibc's built-in `ucontext.h` (identical 4-function API); CMake `check_include_file(ucontext.h)` with vendored fallback |
| Signal delivery while inside `swapcontext` (kernel races) | Low | Medium | Only `app_thread` is signalled; `fiber_port_irq_lock` blocks the signal set before any context manipulation; same pattern as the bare-metal IRQ disable around `fiber_sched_swap` |
| Stack overflow with large JWM/TinyX call chains | Medium | Medium | Guard pages (`mprotect PROT_NONE`); start conservative (256KB server, 128KB JWM); canary check still fires on scheduler walk |
| `libucontext_swapcontext` slower than bare-metal register swap | Low | Low | ~100-300ns per switch is acceptable for cooperative model; vendored build avoids glibc's `sigprocmask`-on-every-swap overhead |
| GDB stack unwinding confused across ucontext frames | Low | Low | GDB 8.0+ handles ucontext switches natively; add `.gdbinit` helper for fiber ring inspection |
| Semantic drift between hosted ucontext and Tima OS task behaviour | Medium | Medium | `hal_task_*` is the single contract; write a conformance smoke test exercising create/yield/wait/wake/timeout runnable on both profiles |

---

## 11. Key Design Principles

1. **The fiber core is untouched.** It's proven on two architectures. Adding a hosted port is additive.
2. **WMB code only sees `hal_task_*`.** This insulates from both fiber internals and platform differences.
3. **The hosted port uses exactly two pthreads** for interrupt simulation — never for application logic.
4. **libucontext is the context-switch engine** on the host; on target, Tima OS is. The fiber core doesn't know or care which.
5. **Preemption is a safety net, not the primary model.** WMB is cooperative-first; the tick exists only to prevent starvation from buggy code during development.
