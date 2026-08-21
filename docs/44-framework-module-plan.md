# 44 — Framework Module Plan

## 1. Purpose

This is the **framework team's working document**: the substrate everything else runs on — the cooperative scheduler, the heap and ring buffers, the
hosted fiber port with its guard-page stacks and ASan bracketing, and the top-level CMake that produces the whole hosted image.

It is the slice of [`40-implementation-plan.md`](40-implementation-plan.md) that covers **P2 (fiber runtime)** and **P3 (`hal_task_*` layer)**, plus
the measurement half of P8, plus the **P9 target port**. [`40-`](40-implementation-plan.md) remains the authority on phase order; this document is the
authority on what this module builds.

**Status: framework is DONE.** As of the top-level `CMakeLists.txt` landing, every V-P2 and V-P3 gate closes cleanly on the hosted profile. The
detachment procedure of §7 is realized by construction: `hal/`, `framework/`, `transport/` and the hosted platform tree are physically separated. What
remains for this module is (a) the target fiber port (§8) and (b) measurement infrastructure for P8 (§6).

### 1.1 Ownership boundary

| | In this module | Elsewhere |
|---|---|---|
| Scheduler | `framework/fiber/` + `hal_task.c` | `hal_task.h` contract → [`hal/`](../hal), authored here |
| Fiber ports | `linux_hosted/fiber_linux/`, `<target>_native/fiber_<target>/` | The display/input/time/alloc ports → [`43-`](43-kdrive-module-plan.md) |
| Portable library | `framework/library/` — `heap.c`, `heap_mngr.c`, `buffer.c`, `lists.c`, `pipe.c` | — |
| Hosted memory contract | `hal/hal_alloc.h`, `linux_hosted/system_linux/hal_alloc_hosted.c` | Target impl → [`43-`](43-kdrive-module-plan.md)'s target platform tree |
| Build skeleton | Top-level `CMakeLists.txt`, per-module `.cmake` fragments | Backend-specific groups → [`43-`](43-kdrive-module-plan.md) |
| Client-side transport, external ABI, boot host, wrapper | — | → [`46-transport-module-plan.md`](46-transport-module-plan.md) |
| Xlib guards, `libX11/` patches | — | → [`46-`](46-transport-module-plan.md) |

**Rule of thumb:** this module owns everything under `framework/`, everything in `hal/` that framework implements (currently `hal_task.h` and
`hal_alloc.h`), and the hosted fiber port.

### 1.2 The two rules this module exists to protect

| Rule | Statement | Gate |
|---|---|---|
| **L8** | The framework references nothing above itself — no `tinyx/`, no `libX11/`, no `jwm/`, no `transport/`, no `Xlib`, no `dix` | S11 |
| **L9** | `framework/` is platform-free — no SDL, no `<linux/>`, no `clock_gettime`, no `ucontext`, no `termios`, no libc allocator | S12 |

Both rules are now **structural** rather than gated: `framework/` cannot reach `transport/`, `libX11/` or `tinyx/` because the physical layout keeps
them at peer levels; and `framework/` cannot use libc's `malloc/free` because the memory contract lives in `hal/hal_alloc.h` and its hosted impl lives
in `linux_hosted/system_linux/`. The S11 and S12 greps still run — they exist to catch regressions if someone re-organises the tree — but they no
longer serve as the enforcement mechanism.

### 1.3 What this document is not

- **Not the transport plan.** [`46-transport-module-plan.md`](46-transport-module-plan.md) owns the X11 protocol replacement, the external ABI and
  the boot host.
- **Not the platform contract.** [`12-platform-contract.md`](12-platform-contract.md) §6 defines `hal_task_*` semantics down to the wake protocol.
- **Not the fiber design.** [`framework/fiber/docs/fiber-architecture.md`](../framework/fiber/docs/fiber-architecture.md) is where the scheduler shape
  lives.

## 2. The File Set

| Group | Contents | Files | ~LOC |
|---|---|---:|---:|
| `WMB_RUNTIME` | `framework/fiber/`, `framework/library/`, `hal/hal_task.h`, `hal/hal_alloc.h` | 15 | 2,500 |
| `WMB_FIBER_LINUX` | `linux_hosted/fiber_linux/` | 3 | 300 |
| `WMB_SYSTEM_LINUX` | `linux_hosted/system_linux/` | 2 | 100 |
| `WMB_APPS_FRAMEWORK` | `linux_hosted/apps/wmb_fiber_smoke/`, `linux_hosted/apps/wmb_task_smoke/` | 2 | 700 |

### 2.1 What already exists — status as of commit 8

Every runtime substrate item is complete. What follows is a status snapshot rather than a work list.

| Artefact | State | Detail |
|---|---|---|
| `framework/fiber/fiber.c` + `fiber_sync.c` + `hal_task.c` | **DONE** | Portable core; independent of any port |
| `framework/fiber/fiber_port.h`, `framework/fiber/intr_handler.h` | **DONE** | The two port-contract headers; sit next to each other |
| `framework/library/heap.c`, `heap_mngr.c`, `buffer.c`, `lists.c`, `pipe.c` | **DONE** | Shipped by `library.cmake`; `heap_mngr.c` calls `wmb_alloc` |
| `hal/hal_task.h` | **DONE** | Contract implemented by `hal_task.c` |
| `hal/hal_alloc.h` | **DONE** | New in commit 2 — `wmb_alloc` / `wmb_free`. Replaces the `MMALLOC malloc` / `MFREE free` leak from `framework/types.h` |
| `linux_hosted/fiber_linux/fiber_port.c` | **DONE** | Guard-page allocator (commit 4) + ASan-aware fiber switch (commit 5) + fresh-fiber `port_trampoline` (commit 6) |
| `linux_hosted/system_linux/hal_alloc_hosted.c` | **DONE** | `malloc`/`free`-backed impl. The only TU on the hosted side that includes `<stdlib.h>` |
| `linux_hosted/system_linux/hal_time_hosted.c` | **DONE** | Predates this module's work; part of the platform contract [`43-`](43-kdrive-module-plan.md) |
| `linux_hosted/apps/wmb_fiber_smoke/` | **DONE** — commit 6 | Closes V-P2-1, ‑3, ‑4, ‑7 |
| `linux_hosted/apps/wmb_task_smoke/` | **DONE** — commit 7 | Closes V-P3-1..V-P3-10 |
| Top-level `CMakeLists.txt`, `README.md` | **DONE** — commit 8 | Produces `libwmb_framework.a`, `libwmb_fiber_linux.a`, `libwmb_system_linux.a`, the `wmb_hal` interface, and every app under `build/bin/` |
| `linux_hosted/sdl_fiber_example_v2/` | **DONE** — pre-existing | Proves the in-process dispatch pattern (V-P5-0). Belongs to this module's demonstration surface — the pattern lives on framework primitives |

## 3. Phase Participation

| Phase | This module's involvement | Weight | Status |
|---|---|---|---|
| P1 | `hal_task.h` authored alongside device headers; build skeleton | Light | **DONE** — hal/ split, top-level CMake |
| **P2** | **Primary owner** — the fiber runtime made real | Medium | **DONE** — commits 3, 4, 5, 6 |
| **P3** | **Primary owner** — `hal_task_*` layer | Medium | **DONE** — commit 7 |
| P4 | — | — | Not owned by this module |
| P5 | — | — | → [`46-`](46-transport-module-plan.md) |
| P6 | — | — | → [`46-`](46-transport-module-plan.md) |
| P7 | — | — | Not owned by this module |
| P8 | Heap and stack instrumentation (measurement half) | Light | Deferred |
| **P9** | **Primary owner** — the target fiber port, freestanding heap, freestanding boot half | **Heavy** | Deferred |

**This module was the critical path from commits 1 through 8; it becomes a supplier through P4–P8 and returns to the critical path at P9.**

## 4. Phase P2 — Fiber Runtime — DONE

**Goal:** make `framework/fiber/` actually run. [`40-`](40-implementation-plan.md) §3.

### 4.1 Work items

| # | Item | State | Landed in |
|--:|---|---|---|
| F2‑1 | Vendor libucontext | ~~Planned~~ **superseded** by `fiber_linux/` | [`90-`](90-decisions.md) D‑11 |
| F2‑2 | `fiber_port_hosted.c` | **DONE** | Pre-existing — `linux_hosted/fiber_linux/fiber_port.c` |
| F2‑3 | `hosted_timer.c` — the one `pthread` | **DEFERRED** | [`90-`](90-decisions.md) D‑47 |
| F2‑4 | `hosted_intr.c` — signal-based deferred switch | **DEFERRED** | D‑47 |
| F2‑5 | **R1** — SPSC input rings | **DONE** | D‑47 §D‑47.4; gate V-P2-8 |
| F2‑6 | Guard-page stack allocator | **DONE** | Commit 4 — port hooks `fiber_port_alloc_stack` / `fiber_port_free_stack`; hosted impl `mmap` + `mprotect(PROT_NONE)` |
| F2‑7 | Hosted overrides in `fiber_config.h` | **DONE** | Pre-existing — `FIBER_DEFAULT_STACK=65536` set by `fiber_linux.cmake` |
| F2‑8 | `fiber.cmake` conditional arch sources | **N/A** for hosted | Deferred to P9's target port |
| F2‑9 | ASan fiber annotations | **DONE** | Commits 5, 6 — `__sanitizer_start_switch_fiber` / `_finish_switch_fiber` bracket `swapcontext`; `port_trampoline` handles fresh-fiber entry |
| F2‑10 | `wmb_fiber_smoke` | **DONE** | Commit 6 |

Preconditions **R2** and **R3** belong to [`43-`](43-kdrive-module-plan.md).

### 4.2 Definition of Done — all boxes ticked

- [x] Two fibers ping-pong via `fiber_yield()` 100,000 times in < 100 ms, no crash (V-P2-1). Measured: **27 ms**.
- [x] `fiber_mutex_t` and `fiber_cond_t` work between fibers (V-P2-3).
- [x] A deliberate stack overflow produces an immediate SIGSEGV **on the guard page** (V-P2-4). Detected via `SEGV_ACCERR`, not `SEGV_MAPERR`.
- [ ] Valgrind clean with the `ucontext` suppression file (V-P2-6). **Blocked** — `valgrind` isn't installed on the development machine; the harness
  runs `valgrind --error-exitcode=42 ./wmb_fiber_smoke --{pingpong,sync}`.
- [x] ASan runs with no false positives on context switches (V-P2-7). Both `--pingpong` and `--sync` clean under `-fsanitize=address`.
- [x] `ps -o nlwp` shows **1** thread while D‑47 holds (V-P2-5).
- [x] **R1 green** (V-P2-8): the tick is suppressed inside a critical section, and `buffer.c.o` references `fiber_critical_enter`.
- [ ] R2 and R3 green in [`43-`](43-kdrive-module-plan.md).
- [x] Gate N5: no `pthread_*` symbol referenced from the framework runtime.

~~A fiber in a tight loop is preempted within 2× the tick period.~~ **Deferred** with the tick (V-P2-2).

## 5. Phase P3 — the `hal_task_*` Layer — DONE

**Goal:** give the rest of WMB a way to wait that is not an OS call. [`40-`](40-implementation-plan.md) §4.

### 5.1 Why this was a separate phase

`hal_yield_timeout` is the primitive that replaces `select()` in **three different places** — TinyX, JWM and the client transport — and every one of
them will be debugged against its semantics. Getting the lost-wakeup case wrong here would produce intermittent hangs at P5, P7 and P9 that look like
three different bugs. **The four W-rules (W2, W3, W5, W7) are gated by V-P3-6 through V-P3-9 for exactly that reason**, and every one closes.

### 5.2 Definition of Done — all boxes ticked

- [x] `hal_task_create` spawns named fibers with the requested stack size.
- [x] `hal_yield_timeout(50)` returns **0** after 50 ± 5 ms, over 100 iterations (V-P3-1). Measured: min/max/mean = 50/50/50 ms.
- [x] `hal_yield_timeout(200)` returns **1** immediately when another fiber wakes it (V-P3-2). Measured: 20 ms elapsed on a 20 ms wake.
- [x] A wake issued **before** the matching wait is not lost (V-P3-3).
- [x] `hal_tick_ms()` monotonic (V-P3-4). Default 5 s run; `WMB_TICK_SECONDS=60` covers the doc's 60 s gate.
- [x] Three-fiber producer/consumer, 10,000 iterations, no lost wakeups (V-P3-5).
- [x] **W5** (V-P3-6): the release wake is not a signal.
- [x] **W2** (V-P3-7): wakes coalesce.
- [x] **W3** (V-P3-8): one flag serves both waits.
- [x] **W7** (V-P3-9): a recycled slot starts clean.
- [x] Creation state is `CREATED` (V-P3-10).

Every gate is a subcommand of `wmb_task_smoke`; see [`README.md`](../README.md) for the invocation list.

## 6. Phase P8 — Measurement Slice

The framework's obligation to P8 is measurement, not the ABI. [`46-`](46-transport-module-plan.md) §6 owns the ABI itself; this section is what the
framework owes it.

### 6.1 Work items

| # | Item | Spec |
|--:|---|---|
| F8‑8 | Per-fiber stack high-water mark instrumentation, exposed via a diagnostic API | [`31-`](31-target-build.md) §5.2 |
| F8‑9 | Heap high-water tracking that attributes usage to WMB rather than the whole process | §7.3 step 4 |
| F8‑10 | An always-on "canary tripped" fatal path that reports which fiber overflowed | — |

Written together they close **V-P8-6**: the measurements that let [`31-target-build.md`](31-target-build.md) §5.2 and §8.1 replace estimates with facts.

### 6.2 Definition of Done

- [ ] The leak gate (V-P8-4) reports a flat **WMB** high-water mark while another component allocates concurrently.
- [ ] The stability gate (V-P8-5) reports per-fiber stack HWM at end of run.
- [ ] The measurements land in [`31-`](31-target-build.md) §5.2 and §8.1 as facts, not estimates (V-P8-6).

## 7. Detachment — Realized

[`40-`](40-implementation-plan.md) §13 in full was this module's charter for a "later" split. As of commit 8, it's **executed**.

### 7.1 What holds today

| Rule | What it prevents | Status |
|---|---|---|
| **L7** — the windowing set references only the contract, the ABI and the framework | `tinyx/` quietly calling into boot or the backend | Structural — `tinyx/` cannot see `linux_hosted/` from its include tree |
| **L8** — the framework references nothing above itself | The framework becoming WMB-specific and unshareable | Structural — `framework/` doesn't see `transport/`, `libX11/`, `tinyx/`, `jwm/`, `hal/*` beyond `hal_task.h` and `hal_alloc.h` |
| **L9** — `framework/` is platform-free | Framework becoming tied to hosted Linux | Structural — libc allocator use routed through `hal_alloc.h`; no `<linux/>`, no `ucontext`, no SDL |

### 7.2 Procedure — status

| # | Step | Status |
|---|---|---|
| 1 | **Fix the public surface.** `fiber.h`, `hal_task.h`, `heap.h`, `buffer.h` are the API | Done — the four are what the `wmb_framework` and `wmb_hal` targets expose PUBLIC |
| 2 | **Extract the host.** `wmb_boot.c` belongs to no `libwmb*.a` | In progress — `linux_hosted/wmb_boot/` exists as an empty landing site; the code moves in when [`46-`](46-transport-module-plan.md) §4 lands |
| 3 | **Stop assuming WMB initialises the runtime.** | Not yet — the runtime brings itself up today; a hosted app already does `fiber_runtime_init` itself. See `linux_hosted/apps/wmb_fiber_smoke/wmb_fiber_smoke.c` |
| 4 | **Account WMB's heap usage separately.** | Deferred to §6 |
| 5 | **Declare a fiber budget.** `FIBER_MAX` is a compile-time global; WMB reserves slots rather than assuming the table is its own | Not yet — worth revisiting when a second tenant appears |
| 6 | **Package.** `libwmb_framework.a` + public headers + a manifest of what a host must supply | Done — the top-level `CMakeLists.txt` produces these |
| 7 | **Prove it.** A trivial second component running alongside WMB | `linux_hosted/apps/wmb_fiber_smoke` is exactly this — a hosted app that uses `wmb_framework` without any transport, X11 or WMB code |

**Step 7 is the only one that proves anything, and it is proven daily by the smoke tests.**

## 8. Phase P9 — The Target Half

| Item | Spec |
|---|---|
| `<target>_native/fiber_<target>/` — `fiber_port.c` + `<arch>.S`, the five `fiber_port.h` symbols, `intr_handler.c` | [`31-`](31-target-build.md) §5.1 |
| `<target>_native/fiber_<target>/fiber_port_alloc_stack` — target-arena impl (no mmap; heap-backed with a canary) | This document §4.1 F2-6 |
| Static fiber stacks, **sized from P8 measurements** | [`31-`](31-target-build.md) §5.2 |
| `<target>_native/system_<target>/hal_alloc_target.c` — static-arena `wmb_alloc` | [`hal/hal_alloc.h`](../hal/hal_alloc.h) |
| Linker script + `.wmb_api` placement at `WMB_LOAD_BASE` | [`22-`](22-entry-point-abi.md) §4.1 |
| `docs/floor-symbols.txt`, generated from [`11-`](11-portability-plan.md) §9.1 + §9.2 | [`11-`](11-portability-plan.md) §9.3 |

- [ ] Exactly one `main`, from the target's `wmb_boot.o` (N7).
- [ ] `readelf -r` shows no dynamic relocations (N9); `.wmb_api` is at offset 0 and `WMB_API_ADDRESS == WMB_LOAD_BASE` (N10).
- [ ] The link references nothing outside `docs/floor-symbols.txt` (N8).
- [ ] No forbidden symbol: `socket`, `select`, `poll`, `fork`, `exec*`, `sigaction`, `getenv`, `fopen`, `dlopen`, `pthread_*` (N1–N4).

## 9. Interfaces With Other Modules

| Seam | This module provides | Consumer |
|---|---|---|
| `hal_task.h` — 9 functions | `hal_task_create`, `hal_task_wait`, `hal_task_wake`, `hal_yield_timeout`, … | `tinyx/os/waitfor_wmb.c`, `jwm/src/event.c`, `tima_kdrive_system.c`, `transport/x11/` |
| `hal_alloc.h` — 2 functions | `wmb_alloc`, `wmb_free` | Every WMB-side allocator (framework's `heap_mngr.c` today; transport later) |
| `fiber_critical_enter/exit` | Tick-suppressing critical sections | `buffer.c`, `hal_display_flush` (R3) |
| `fiber_port.h`, `intr_handler.h` | 7 symbols the port implements | *Consumed* from each port tree — [`43-`](43-kdrive-module-plan.md) supplies the peer device ports; §8 above supplies the target fiber port |
| `list_*`, `pipe_*`, `buffer_*`, `heap_*` | Library primitives | Transport (event queue, request buffer) → [`46-`](46-transport-module-plan.md); TinyX and JWM downstream |

## 10. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| FW‑1 | Lost-wakeup semantics wrong in `hal_task_*` | ~~Medium~~ **Low** | **High** | Closed — V-P3-3, V-P3-6..‑9 all green |
| FW‑2 | **L8 violated silently** by a convenience edit to `buffer.h` or `fiber_config.h` | ~~High~~ **Low** | Medium | Structural now: `framework/` cannot reach `transport/`, `libX11/`, `tinyx/`, `jwm/`. Gate S11 still runs as a regression check |
| FW‑3 | The glibc `<ucontext.h>` fallback misbehaves on a developer machine | Low | Medium | [`90-`](90-decisions.md) D‑11 reopens vendoring if it bites; Valgrind suppression file already tracks which implementation was selected |
| FW‑4 | Target fiber port picks up hosted assumptions when P9 starts | Medium | Medium | §8's item list is explicit; every hosted-only mechanism (mmap, ASan) is behind `#ifdef` guards or lives outside `framework/` |
| FW‑5 | P8 measurement instrumentation added ad-hoc, later needs re-doing | Medium | Low | Fold instrumentation into `heap_mngr.c` and the fiber canary before P8 starts; keep it always-on |

## 11. Budget

| | Value |
|---|---|
| New LOC landed | ~600 (hal_alloc + guard page + ASan + smoke tests + top-level CMake) |
| Upstream files this module has modified | **0** |
| Files this module removed from the build (via the moves in commits 1–8) | 1 — the old SDL-hardcoded `framework/CMakeLists.txt` |

## 12. What to Do When P9 Starts

Framework's near-term work is DONE. When target-platform work begins:

1. **Author `<target>_native/fiber_<target>/`** — the five symbols in `fiber_port.h` plus `intr_handler.h`'s three, following the pattern in
   `linux_hosted/fiber_linux/`. The generic layer stays untouched.
2. **Implement `fiber_port_alloc_stack` against a static arena** rather than mmap. Guard-page style depends on the target's MPU / MMU; if neither is
   available, use a canary word at the bottom of each stack (the fiber layer already places one).
3. **Wire `hal_alloc_target.c`** against whatever static heap the platform reserves. Keep the contract in `hal/hal_alloc.h` unchanged.
4. **Add the target as a CMake option in the top-level file** — a new `add_library(wmb_fiber_target STATIC …)` alongside `wmb_fiber_linux`, and a
   toggle for which one `wmb_hosted` picks up. Alternatively, split `wmb_hosted` into `wmb_hosted_linux` / `wmb_hosted_target` and let apps pick.

Until then, this module's contribution is done. The transport in [`46-`](46-transport-module-plan.md) is what the project needs next.
