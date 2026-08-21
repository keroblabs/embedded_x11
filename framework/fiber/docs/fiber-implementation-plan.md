# Fiber Runtime — Implementation Plan

> ## ℹ Historical record — phases 0–2 delivered
>
> The hosted port shipped, but as `linux_hosted/fiber_linux/` rather than the
> `arch/hosted/` path this plan names ([`03-target-architecture.md`](../../../docs/03-target-architecture.md)
> rule L9 puts ports in sibling trees, not under the framework).
>
> Two items below are **not** delivered and are deliberately deferred:
> `hosted_timer.c` and `hosted_intr.c` — the preemption tick — which
> [`90-decisions.md`](../../../docs/90-decisions.md) D‑47 holds disabled until
> its preconditions are met.
>
> Current design: [fiber-architecture.md](fiber-architecture.md).

**Companion to:** [fiber-architecture.md](fiber-architecture.md) · [hosted-interrupt-simulation.md](hosted-interrupt-simulation.md)
**Status:** Phases 0–2 delivered; preemption tick deferred (D‑47)
**Strategy:** Land the hosted port first (fast iteration, easy debugger), then RV32, then ARMv6-M. Each phase ends with a green test before the next begins.

---

## Phase 0 — Scaffolding (no behaviour change)

| # | Task | Files | Done-when |
|---|---|---|---|
| 0.1 | Create `firmware_bringup/fiber/` with empty `fiber.h`, `fiber_port.h`, `fiber_config.h`, `fiber.c`, `fiber.cmake` | new | Empty TU compiles |
| 0.2 | Include `fiber/fiber.cmake` from top-level `CMakeLists.txt` (always-on) | `CMakeLists.txt` | All three existing builds still link |
| 0.3 | Add `arch/hosted/` with `arch.cmake`, empty `fiber_port.c`, `hosted_intr.c`, `hosted_timer.c` | new | `cmake -S firmware_bringup -B build-hosted -DARCH=hosted` configures (does not need to link yet) |
| 0.4 | Add `fiber/fiber_config.h` tunables: `FIBER_MAX`, `FIBER_DEFAULT_STACK`, `FIBER_TICK_US`, `FIBER_CANARY` | `fiber/fiber_config.h` | Defaults documented inline |

---

## Phase 1 — Generic Core (`fiber.c`)

Implementation order *inside* this phase:

1. **FCB pool** — static `fiber_t pool[FIBER_MAX]` + free-list. Allocator/free routines are O(1) and lock-free under `fiber_port_irq_lock`.
2. **Ring operations** — insert-after, unlink. Pure functions, unit-testable on host without any port.
3. **API surface** — `fiber_runtime_init`, `fiber_create`, `fiber_create_static`, `fiber_yield`, `fiber_destroy`, `fiber_self`, `fiber_main`, `fiber_is_main`. Both `_create` variants funnel through one internal `fiber_create_common(func, arg, stack_buf, stack_size, owns_stack)` so there is exactly one code path to audit. Static variant validates port-required alignment and returns `FIBER_ERR_INVALID` on violation.
4. **Scheduler tail** — `fiber_sched_tail()` picks the next non-ZOMBIE, reclaims any ZOMBIE encountered, calls `fiber_port_switch`. Stack-canary check on every pass.
5. **Trampoline** — `static void fiber_trampoline(void)` reads the captured `entry`/`arg` from a small TLS-equivalent slot in the FCB, calls user function, then `fiber_exit()`.

**Tests (host, no port yet — use a stub `fiber_port_switch` that just swaps the `sp` pointers in a fake "world"):**
- T1.1 Insert order is `main → f1 → f2 → … → fn → main` after N creates.
- T1.2 Destroying a middle fiber removes it from the ring on the *next* dispatch, not immediately.
- T1.3 `fiber_destroy(main)` → `FIBER_ERR_IMMORTAL`, ring unchanged.
- T1.4 Self-terminate inside `worker` never returns control to the worker's caller frame.
- T1.5 NULL / dangling pointer arguments → `FIBER_ERR_INVALID`, no crash.
- T1.6 `fiber_create_static` with a mis-aligned buffer returns `FIBER_ERR_INVALID`; with a correctly-aligned buffer runs to completion and `fiber_destroy` does **not** call `free` on it (verify with a wrapping allocator that asserts on unexpected frees).

---

## Phase 2 — Hosted Port (libucontext + pthreads)

| # | Task | Notes |
|---|---|---|
| 2.1 | `fiber_port_init_stack` via `libucontext_makecontext` with `uc_link = main_ctx` (belt-and-braces against bugs in the trampoline) | Split 64-bit `entry`/`arg` into 32-bit halves per libucontext ABI |
| 2.2 | `fiber_port_switch` → `libucontext_swapcontext` | trivial |
| 2.3 | `fiber_port_irq_lock/unlock` → `sigprocmask(SIG_BLOCK / SIG_SETMASK, {SIGUSR1})` | nesting via saved mask |
| 2.4 | `fiber_port_request_switch` → `pthread_kill(main_tid, SIGUSR1)` | self-signal so the handler runs at a clean point |
| 2.5 | SIGUSR1 handler → `fiber_sched_tail()` | installed with `SA_RESTART | SA_NODEFER` blocked |
| 2.6 | `hosted_timer.c` — dedicated pthread, `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, …)` loop, raises `INTR_VECTOR_TIMER` via `hosted_irq_raise()` (see [hosted-interrupt-simulation.md §6](hosted-interrupt-simulation.md)) | absolute sleep keeps the period stable |
| 2.7 | `hosted_intr.c` — pending/enable masks, `irq_thread` dispatcher, `hosted_isr_dispatch` signal handler, full `intr_handler_*` shim set (see [hosted-interrupt-simulation.md §§3–8](hosted-interrupt-simulation.md)) | satisfies existing `lib_c/critical_section.c` link symbols with no firmware-side change |
| 2.8 | `arch/hosted/arch.cmake` — `find_package(Threads)`, `find_library(UCONTEXT ucontext)`, link both | document fallback to glibc's built-in `ucontext.h` when libucontext is absent |

**Tests (hosted, real port):**
- T2.1 Two-fiber ping-pong using only `fiber_yield()`; 1 M iterations under valgrind → no leaks, no UB.
- T2.2 Preemption test: `worker` does `while(1)` with no yield; ticker pre-empts within `2 × FIBER_TICK_US` (measure with `clock_gettime`).
- T2.3 Cross-terminate: fiber A kills fiber B mid-busy-loop; ring length drops by 1 within one tick; B's stack is freed; canary intact.
- T2.4 `fiber_destroy(fiber_main())` returns `FIBER_ERR_IMMORTAL`.
- T2.5 Stack overflow detection — worker recurses; canary trip routes to `intr_handler_call_crash`.

---

## Phase 3 — RISC-V Port (`arch/emulator_riscv`)

| # | Task | Notes |
|---|---|---|
| 3.1 | `fiber_switch.S` — 13 callee-saved + `sp` slot; RV32 `sw/lw`; align 16 B | `sp` at offset 0 of FCB |
| 3.2 | `fiber_port_init_stack` — forge frame: `ra = trampoline`, `s0–s11 = 0`, plus a 2-word "args" slot (`entry`, `arg`) the trampoline reads | 16-byte align stack top |
| 3.3 | `fiber_port_irq_lock/unlock` — `csrrci`/`csrrsi` on `mstatus.MIE` returning prior bit | nestable via returned value |
| 3.4 | Wire `mc_swi_handler_call` (already present in `trap_entry.S`) to a thin C shim that calls `fiber_sched_tail` then clears `mip.MSIP` | re-use existing SWI plumbing |
| 3.5 | `fiber_port_request_switch` → `intr_trigger_swi` (already exists) | one-liner |
| 3.6 | `fiber_port_init(period_us)` — programme `mtimecmp` for periodic compare; install `mc_timer_handler_call` body that re-arms mtimecmp + `fiber_port_request_switch` | re-use `intr_handler_set_vector` |

**Tests (rv32, run on `wsg_rtl_emulator`):**
- T3.1 Cooperative two-fiber test — same source as T2.1, recompiled.
- T3.2 Preemptive test — busy-loop worker, expect tick-bounded preemption (verify via GPIO/log toggle count).
- T3.3 `intr_handler_call_crash` on canary trip still produces a meaningful trap.

---

## Phase 4 — ARMv6-M Port (`arch/emulator_arm`)

| # | Task | Notes |
|---|---|---|
| 4.1 | `fiber_switch.S` — `.syntax unified`, `.cpu cortex-m0`, `.thumb`; 9 regs via low-reg proxies; `bx r2` epilogue (from `qa.md`) | sp at FCB offset 0 |
| 4.2 | `fiber_port_init_stack` — forge 9-word frame; `lr ← trampoline`; pre-load `r4/r5` proxies with `entry`/`arg` and have trampoline `mov r0, r4; blx r5` | avoids a wrapper function |
| 4.3 | `fiber_port_irq_lock/unlock` — `mrs r0, primask; cpsid i` / `msr primask, r0` | nestable |
| 4.4 | `fiber_port_request_switch` → `ICSR.PENDSVSET = 1` | one-liner |
| 4.5 | `PendSV_Handler` (asm naked) → `bl fiber_sched_tail; bx lr` | priority set to 0xFF in `boot.c` |
| 4.6 | `fiber_port_init(period_us)` — `SysTick_Config(SystemCoreClock/1e6 * period_us)` | SysTick handler just `fiber_port_request_switch` |

**Tests:** T4.1–T4.3 mirror T3.1–T3.3 on the ARM target.

---

## Phase 5 — Demo Application

| # | Task | Notes |
|---|---|---|
| 5.1 | `source_apps/fiber_demo/` — three workers printing tagged ticks, `main` performs watchdog-pet + status print every N ticks | exercises all three back-ends from a single source |
| 5.2 | `fiber_demo.cmake` — wire into top-level `include()` switch | one-line swap with `terminal.cmake` |
| 5.3 | Document run-instructions in `docs/fiber-architecture.md` §10 | already drafted |

---

## Phase 6 — Polish & Hand-off

| # | Task |
|---|---|
| 6.1 | Run typo/spelling pass across `fiber/`, `arch/hosted/`, new docs |
| 6.2 | `cmake --build … --clean-first` on all three configs |
| 6.3 | Update `.claude_tasks/lessons.md` with any new patterns encountered (deferred-switch idiom, libucontext-on-Windows quirks, …) |
| 6.4 | Hand-off note in `docs/` summarising the public API and one paragraph per port |

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| `libucontext` unavailable on Windows host build | M | M | Fall back to MinGW's built-in `ucontext.h`; CMake `find_library` with `OPTIONAL` and `#if HAVE_LIBUCONTEXT` shim |
| Stack-overflow canary too coarse (1 word) | L | H | Add `FIBER_CANARY_WORDS=4` config and check all 4 on every dispatch |
| PendSV priority misconfigured → preempts a higher-priority IRQ | L | H | Assert in `fiber_port_init` that `SHPR3.PRI_14 == 0xFF` |
| RV32 SWI handler runs with stale `mip.MSIP` → infinite loop | L | H | Clear `mip.MSIP` **before** calling `fiber_sched_tail`, unit-test in single-fiber mode |
| Cross-terminate freeing the running fiber's stack | M | C | Architectural invariant §9.3: only scheduler-tail frees ZOMBIE stacks — enforced by code review |

---

## Acceptance Criteria

The work is "done" when **all** of:
1. The demo app from Phase 5 runs on **hosted**, **RV32**, and **ARMv6-M** with identical user-visible behaviour.
2. T1.*–T4.* all pass.
3. No regressions in the existing `terminal` and `discovery_app` builds (selectable via the same top-level `include()` switch).
4. `cmake --build … --clean-first` succeeds on each of the three configurations.
5. `fiber.h` is the **only** header the demo app includes from the fiber subsystem.
