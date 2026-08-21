# 43 — KDrive DDX and Platform Backend Module Plan

## 1. Purpose

This is the **DDX and backend team's working document**: everything between the X server and the hardware — the platform contract itself, the KDrive
device‑dependent layer that sits on it, and the two implementations underneath it (SDL on the host, MMIO on the target).

It is the slice of [`40-implementation-plan.md`](40-implementation-plan.md) that owns **P1 outright** and then supplies pixels, keys and pointer
events to every phase after it. [`40-`](40-implementation-plan.md) remains the authority on phase order; this document is the authority on what this
module builds.

### 1.1 Ownership boundary

| | In this module | Elsewhere |
|---|---|---|
| Contract headers | `hal_display.h`, `hal_input.h`, `hal_time.h`, `wmb_os.h` | `hal_task.h` is WMB's own scheduler surface → [`44-`](44-framework-module-plan.md) |
| KDrive framework | `tinyx/kdrive/src/` minus `kmap.c` | — |
| The DDX | `tinyx/kdrive/tima/` — all five files | — |
| Hosted backend | `linux_hosted/sdl_drivers/`, `linux_hosted/sdl_library/`, `linux_hosted/system_linux/` | `linux_hosted/fiber_linux/` → [`44-`](44-framework-module-plan.md) |
| Target backend | `<target>_native/{display,input,system}_<target>/` | `<target>_native/fiber_<target>/` → [`44-`](44-framework-module-plan.md) |
| SDL discovery | `cmake/FindLocalSDL2.cmake` | The rest of the build skeleton → [`44-`](44-framework-module-plan.md) |
| Server core | — | `tinyx/dix`, `mi`, `fb`, `os` → [`42-`](42-tinyx-module-plan.md) |

**Rule of thumb:** this module owns everything *below* `hal_*.h` and the one thing directly *above* it that is device‑dependent — the DDX. It owns no
protocol code and no transport code.

### 1.2 The layering rule this module exists to enforce

> **No file in `tinyx/`, `libX11/` or `jwm/` may reference SDL, a backend header, or a platform type.**

Rule L1, gate S1, success criterion 4. This module is the only place a backend name may appear, and `linux_hosted/` and `<target>_native/` are
**peers** — neither is a default and neither is an exception ([`12-`](12-platform-contract.md) §10, [`31-`](31-target-build.md) §4.6).

### 1.3 What this document is not

- **Not the contract specification.** [`12-platform-contract.md`](12-platform-contract.md) §4–§8 gives every prototype, constant and semantic rule.
- **Not the port guide.** [`31-target-build.md`](31-target-build.md) §4.6 is the checklist a new port follows.
- **Not the file inventory.** [`01-component-inventory.md`](01-component-inventory.md) §5, §6.

## 2. The File Set

| Group | Contents | Files | ~LOC |
|---|---|---:|---:|
| `WMB_HAL` | `hal_*.h` + the contract adapters | 9 | 900 |
| `WMB_BACKEND_SDL` | `linux_hosted/sdl_drivers/` + `linux_hosted/sdl_library/` | 9 | 1,420 |
| `TINYX_KDRIVE` | `tinyx/kdrive/src/` minus `kmap.c` | 6 | 3,480 |
| `TINYX_DDX` | `tinyx/kdrive/tima/` | 4 (+1 header) | 1,088 |

### 2.1 The DDX as it stands

Five files, 1,088 LOC, **written and never compiled**. They reach the hardware through `hal_drivers.h` — an ad‑hoc backend header — and must be
re‑pointed at the contract.

| File | Provides | Gap |
|---|---|---|
| `tima_kdrive_fb.c` | `KdCardFuncs`: cardinit, scrinit, initScreen, enable, dpms, disable, restore, getColors, putColors | Re‑point to `hal_display_*` |
| `tima_kdrive_kbd.c` | `KdKeyboardFuncs` + `tima_KeyboardPoll()` | Re‑point to `hal_input_poll_key`; LEDs and bell ignored |
| `tima_kdrive_mouse.c` | `KdMouseFuncs` + `tima_MousePoll()` | Re‑point, **plus defects D1–D4** |
| `tima_kdrive_system.c` | `KdOsFuncs` + `InitCard`, `InitOutput`, `InitInput`, `OsVendorInit`, `ddx*` | **`tima_WaitForEvents()` does not exist** — defect D5 — plus D6 |
| `tima_kdrive.h` | Vtable externs, private types | — |

These four `.c` files are **ours** (an additive local module), so editing them costs no upstream patch budget. `tinyx/kdrive/src/` is upstream and
does cost budget — one file, `kinput.c`, guard #8.

### 2.2 The known defect list

[`01-`](01-component-inventory.md) §6.3. **D1–D6 close in P1; D7 closes in P2.**

| # | File | Defect |
|---|---|---|
| D1 | `tima_kdrive_mouse.c` | `dx`, `dy`, `flags` used before initialisation |
| D2 | `tima_kdrive_mouse.c` | No button‑release tracking |
| D3 | `tima_kdrive_mouse.c` | Wheel events read and discarded instead of becoming X buttons 4/5/6/7 |
| D4 | `tima_kdrive_mouse.c` | `mouse_driver_init(0)` never called from `tima_MouseInit()` |
| D5 | `tima_kdrive_system.c` | `tima_WaitForEvents()` missing — **the cooperative wait hook the whole design rests on** |
| D6 | `tima_kdrive_system.c` | `ddxProcessArgument` returns 0 unconditionally while `ddxUseMsg` advertises `-fb` and `-fbsize` |
| **D7** | `keyraw_driver.c`, `mouse_driver.c` | The HAL input rings use `buffer.c` outside its scope: a byte FIFO that drops a **single byte** on overflow, carrying command/data **pairs**. A full ring desynchronises the decoder *permanently* |

## 3. Phase Participation

| Phase | This module's involvement | Weight |
|---|---|---|
| **P1** | **Primary owner.** Contract, backends, DDX re‑pointing, D1–D6 | **Heavy** |
| P2 | D7 / precondition **R2**, and **R3** (the damage union) | Medium |
| P3 | `hal_time_hosted.c` and `wmb_os_hosted.c` — the platform half of the task layer | Light |
| P4 | `tima_WaitForEvents()` wired to `hal_yield_timeout`; `InitOutput`/`InitInput` bring the screen up; **the root window pixels are this module's** | Medium |
| P5 | `pollEvents` at ≥ 30 Hz through `KdBlockHandler`; synthetic key injection reaches a client | Light |
| P6 | Guard #8 (`kinput.c`); the no‑SDL‑leakage gate | Light |
| P7 | Real input driving a real WM — the first time the input path is exercised adversarially | Light |
| **P9** | **The target port**: display, input, time, console, heap on real hardware | **Heavy** |

**This module is the critical path at the very start and again at the very end.** Between P2 and P8 it is a supplier, not a bottleneck.

## 4. Phase P1 — Platform Contract and Backends

**Goal:** establish the boundary everything else sits on, and make the DDX platform‑agnostic. [`40-`](40-implementation-plan.md) §2,
[`11-`](11-portability-plan.md) Q2+Q3.

### 4.1 Work items

| # | Item | Deliverable | Spec |
|--:|---|---|---|
| K1‑1 | `hal/hal_display.h` | New | [`12-`](12-platform-contract.md) §4 |
| K1‑2 | `hal/hal_input.h`, **carrying the existing `CMD_*` / `KEY_DATA_*` / `MOUSE_BUTTON_*` constants verbatim** | New | [`12-`](12-platform-contract.md) §5 |
| K1‑3 | `hal/hal_time.h`, `hal/wmb_os.h` | New | [`12-`](12-platform-contract.md) §7, §8 |
| K1‑4 | `linux_hosted/sdl_drivers/hal_display_sdl.c` — adapts `lcd_driver_*` | New | [`12-`](12-platform-contract.md) §10 |
| K1‑5 | `linux_hosted/sdl_drivers/hal_input_sdl.c` — adapts `keypad_driver_*` + `mouse_driver_*` | New | ″ |
| K1‑6 | `linux_hosted/system_linux/hal_time_hosted.c` (+ `wmb_os_hosted.c`) | New | ″ |
| K1‑7 | `linux_hosted/sdl_drivers/hal_mock.c` — a null backend for link isolation | New | [`11-`](11-portability-plan.md) §6.1 |
| K1‑8 | `cmake/FindLocalSDL2.cmake` — system SDL2 via `find_package`, `WMB_SDL_ROOT` override, **no hard‑coded path, no `SDL2/SDL.h` shim** | New | [`30-`](30-host-build.md) §5 |
| K1‑9 | Re‑point the four DDX files at `hal_*.h`; retain `hal_drivers.h` as a *backend‑internal* header that re‑includes the constants | Edit (ours) | [`11-`](11-portability-plan.md) §6.1 |
| K1‑10 | Close D1–D6 | Edit (ours) | §2.2 |
| K1‑11 | Delete `-D_USE_LINUX` from the build | — | [`10-`](10-os-dependency-census.md) §8 |
| K1‑12 | `wmb_hal_smoke` + `wmb_input_smoke` + `wmb_ddx_linktest` | New | [`30-`](30-host-build.md) §7 B2, B4 |

### 4.2 Contract design rules that are not negotiable

| # | Rule | Consequence if broken |
|---|---|---|
| PC1 | **No platform type crosses the contract** — no `fd`, no `SDL_Window*`, no `struct timespec` | The target port cannot implement the header |
| PC2 | **WMB polls; the backend never calls up into WMB** | An ISR that calls `dix` is a data race on target |
| PC3 | **Nothing below the contract blocks**, except inside `hal_yield_timeout` | The cooperative model stops being cooperative |
| S8 | **Each header compiles standalone** under `-ffreestanding -nostdinc` with only `<stdint.h>`, `<stddef.h>`, `<stdbool.h>` | A contract that needs `<sys/types.h>` is not a contract |

The contract is **closed**: seventeen functions a platform supplies, plus five `fiber_port.h` symbols owned by [`44-`](44-framework-module-plan.md).
Adding a function to a `hal_*.h` header is a [`90-decisions.md`](90-decisions.md) entry, not a commit.

### 4.3 The link‑isolation test is the phase's real deliverable

`wmb_ddx_linktest` links the DDX object files against `hal_mock.o` **with no SDL on the link line**. It is the only check that proves the DDX is
genuinely decoupled rather than merely appearing to be, and it costs one CMake target.

Everything else in P1 can be satisfied by a DDX that includes `hal_display.h` and still, somewhere, drags in a backend symbol. This gate cannot.

### 4.4 Definition of Done

- [ ] `cmake -B build -DWMB_PROFILE=host-pure` configures on a machine with system `libsdl2-dev` and **no** `WMB_SDL_ROOT` set (V‑P1‑2).
- [ ] All four profiles configure (V‑P1‑1).
- [ ] Each of the five contract headers compiles standalone under `-ffreestanding -nostdinc` (gate S8; `hal_task.h` co‑owned with
  [`44-`](44-framework-module-plan.md)).
- [ ] `wmb_hal_smoke` draws a colour gradient through `hal_display_get_fb()->pixels` and `hal_display_flush()`; **it is visible in an SDL window**
  (V‑P1‑3).
- [ ] Pressing keys and moving the mouse prints the correct `HAL_CMD_*` byte pairs (V‑P1‑5).
- [ ] Mouse: the wheel produces buttons 4/5/6/7 (V‑P1‑6); button release is tracked (V‑P1‑7); `dx`/`dy` are initialised; `hal_input_init` is called.
- [ ] `grep -rl 'SDL\|sdl_\|hal_drivers\.h' tinyx/` is empty (gate S1).
- [ ] The DDX object files link against `hal_mock.o` with **no SDL on the link line** (V‑P1‑4).
- [ ] `grep -rl 'SDL\|<linux/\|clock_gettime\|ucontext\|termios' framework/` is empty — `framework/` is platform‑free (gate S12).

### 4.5 Why this phase is first

Every later phase depends on this boundary, and a boundary that is wrong is cheapest to fix before 165 kLOC sits on top of it. **P1 is also the phase
with the shortest path to a visible result** — a gradient in a window — which makes it the right place to shake out the build, the profiles and the
SDL discovery at the same time.

## 5. Phase P2 — Input Ring Correctness

This module does not own the fiber runtime, but it owns **two of the four preconditions** that D‑47 sets before the preemption tick may ever be
re‑armed ([`90-`](90-decisions.md) §D‑47.3).

| Precondition | Where | This module's work |
|---|---|---|
| R1 — SPSC input rings | `buffer.c` | **DONE** by [`44-`](44-framework-module-plan.md); nothing to do |
| **R2** | `keyraw_driver.c`, `mouse_driver.c` | **Overflow must drop a whole command/data pair, never half of one** (defect D7) |
| **R3** | `hal_display_flush` | The damage union is done under `fiber_critical_enter`/`exit`, **or** made a single word |
| R4 — preemption soak | — | Deferred with the tick itself |

R2 is **a live bug today, independent of preemption**: [`12-`](12-platform-contract.md) §5.3 requires overflow never to truncate a pair, and a full
keyboard (20 B) or mouse (128 B) ring desynchronises the decoder permanently. `buffer.c` is correct as scoped by [`90-`](90-decisions.md) D‑48 — the
fix belongs in the two drivers.

### 5.1 Definition of Done

- [ ] **V‑P2‑9**: overfill a ring by 1, 2 and 3 bytes; the decoder still sees command/data alignment.
- [ ] **R3** reviewed: the damage union is atomic or critical‑sectioned; V‑P1‑3 still passes unchanged.

## 6. Phase P3 — The Platform Half of the Task Layer

`hal_task_*` is WMB's own ([`90-`](90-decisions.md) D‑31) and belongs to [`44-`](44-framework-module-plan.md). What this module supplies underneath
it:

| Item | Contract |
|---|---|
| `hal_time_hosted.c` | `hal_tick_ms`, `hal_tick_us` — [`12-`](12-platform-contract.md) §7 |
| `wmb_os_hosted.c` | `wmb_os_console_write`, `wmb_os_abort`, `wmb_os_exit`, `wmb_os_sbrk` — [`12-`](12-platform-contract.md) §8 |

- [ ] **V‑P3‑4**: `hal_tick_ms()` is monotonic over a 60‑second run, sampled at 1 kHz, with no backward step.
- [ ] `wmb_os_console_write` works **before** `hal_display_init` — it is the only diagnostic channel during early boot.

## 7. Phase P4 — Bringing the Screen Up

The server assembles in [`42-`](42-tinyx-module-plan.md); the **pixels** are this module's.

| Item | Deliverable |
|---|---|
| `tima_WaitForEvents()` wired to `hal_yield_timeout` (defect D5) | The cooperative wait hook `KdOsFuncs` needs |
| `InitCard` / `InitOutput` / `InitInput` / `OsVendorInit` | Screen, card and input device registration |
| `kdrive-config.h` review | Authored by [`42-`](42-tinyx-module-plan.md); the `KDRIVESERVER` / `KDRIVEFBDEV` values are this module's call |

### 7.1 Definition of Done — DDX slice

- [ ] An SDL window opens showing the X root window (V‑P4‑3).
- [ ] The startup log's screen geometry, depth and visual list match the configured `WMB_SCREEN_WIDTH`/`HEIGHT`/`BPP` (V‑P4‑5).
- [ ] The server idles at < 5 % CPU — `tima_WaitForEvents` yields, it does not spin (V‑P4‑2).

**V‑P4‑3 doubles as the bisection gate for every P5 failure** ([`40-`](40-implementation-plan.md) §5.3). Keep it runnable and keep it fast.

## 8. Phase P5 — Input Reaches a Client

| Gate | What this module supplies |
|---|---|
| V‑P5‑3 | `KdOsFuncs.pollEvents` observed at **≥ 30 Hz** through `KdBlockHandler` |
| V‑P5‑4 | A synthetic key event injected at the backend reaches a client's event queue |

If V‑P5‑3 fails, the fault is almost never here: it is `waitfor_wmb.c` having dropped the `BlockHandler` call ([`42-`](42-tinyx-module-plan.md) §5.3).
Check that first — this module's `pollEvents` is reached only *through* it.

## 9. Phase P6 — Guard #8 and the Leakage Gate

| # | File | Guard | Edit |
|--:|---|---|---|
| 8 | `tinyx/kdrive/src/kinput.c` | `WMB_HAS_OS_TRANSPORT` | Guard the `<signal.h>` / SIGIO async‑input path. **The polled path is unchanged** |

- [ ] `patches/tinyx/0008-kinput-guard-sigio.patch` exists and replays on a pristine checkout (S7).
- [ ] `grep -rl "SDL" tinyx/` is empty (S1, V6).
- [ ] Every P5 gate still passes after the guard (V‑P6‑1).

This is the module's only draw on the `tinyx/` patch budget, which has **zero slack** ([`42-`](42-tinyx-module-plan.md) §10). A second guarded file
here requires a [`90-decisions.md`](90-decisions.md) entry.

## 10. Phase P9 — The Target Port

**Goal:** prove that `linux_hosted/` was a *port*, not a *dependency*.

### 10.1 Tree shape

A port is a directory tree, not a set of edits scattered through the codebase ([`31-`](31-target-build.md) §4.6.1):

```
<target>_native/
├── system_<target>/    hal_time_<target>.c   wmb_os_<target>.c   system_<target>.cmake
├── display_<target>/   hal_display_<target>.c                    display_<target>.cmake
├── input_<target>/     hal_input_<target>.c                      input_<target>.cmake
└── fiber_<target>/     ← owned by the framework module, 44-
```

Each `*.cmake` exports `SOURCE_FILES_<MODULE>`, `<MODULE>_INCLUDE_PATH` and any `<MODULE>_DEFINES`. **Selecting a platform means including one set of
fragments instead of another; no source file changes.**

### 10.2 Symbols to define

Twelve of the seventeen contract functions ([`31-`](31-target-build.md) §4.6.2) — the other five are `fiber_port.h`'s, owned by
[`44-`](44-framework-module-plan.md):

| Header | Symbols |
|---|---|
| `hal_display.h` | `hal_display_init` `hal_display_shutdown` `hal_display_get_fb` `hal_display_flush` `hal_display_set_power` |
| `hal_input.h` | `hal_input_init` `hal_input_shutdown` `hal_input_has_data` `hal_input_read_byte` `hal_input_control` `hal_input_pump` |
| `hal_time.h` | `hal_tick_ms` `hal_tick_us` |
| `wmb_os.h` | `wmb_os_console_write` `wmb_os_abort` `wmb_os_exit` `wmb_os_sbrk` |

**If a port needs to define anything outside this list, either the list is wrong or the port is doing work that belongs above the contract.**

### 10.3 Bring‑up order

Each step is testable with only the steps before it, so a failure is attributable ([`31-`](31-target-build.md) §4.6.4):

| # | Step | Testable by |
|--:|---|---|
| 1 | `wmb_os_console_write`, `wmb_os_abort` | "hello" over UART before anything else exists |
| 2 | `wmb_os_sbrk` + `heap.c` | 1,000 alloc/free; check the high‑water mark |
| 3 | `hal_time_<target>.c` | V‑P3‑4 |
| 4 | `fiber_<target>/` | V‑P2‑1 — [`44-`](44-framework-module-plan.md) |
| 5 | `hal_display_<target>.c` | V‑P1‑3 — a gradient |
| 6 | `hal_input_<target>.c` | V‑P1‑5 — a key press produces the expected byte pair |
| 7 | Whole image | JWM root window; `wmb_test_client` |

Steps 1–5 are exactly what `linux_hosted/` already does, **which is why the hosted port is worth keeping honest: it is the reference implementation a
target port is checked against.**

### 10.4 Definition of Done

- [ ] Static fiber stacks sized from **P8 measurements**, not estimates ([`31-`](31-target-build.md) §5.2).
- [ ] V‑P9‑3/4/5 on hardware: boot to a JWM root window; `wmb_test_client` passes all six steps; keyboard and mouse work.
- [ ] V‑P9‑6: a one‑hour run on hardware with a flat heap and no canary trip.

**P9a — the freestanding compile — needs no hardware** and can run against a stub backend as soon as P6 is green. Only §10.4's last three lines need a
board.

## 11. Interfaces With Other Modules

| Seam | This module provides | Consumer |
|---|---|---|
| `hal_display.h` | Framebuffer pointer, flush, power | `tima_kdrive_fb.c`, `wmb_hal_smoke` |
| `hal_input.h` | The `HAL_CMD_*` byte protocol — **the constants are carried verbatim from the existing drivers** | `tima_kdrive_kbd.c`, `tima_kdrive_mouse.c` |
| `hal_time.h`, `wmb_os.h` | Ticks, console, heap primitive, termination | `framework/`, `tinyx/os/log.c`, `utils.c` |
| `KdOsFuncs.pollEvents` | Drains the input rings into `mieq` | `KdBlockHandler`, called from `waitfor_wmb.c` → [`42-`](42-tinyx-module-plan.md) |
| `InitOutput` / `InitInput` | Screen and device registration | `dix/main.c` → [`42-`](42-tinyx-module-plan.md) |
| `hal_yield_timeout` | *Consumed* by `tima_WaitForEvents()` | Provided by [`44-`](44-framework-module-plan.md) |
| `fiber_critical_enter/exit` | *Consumed* by the damage union (R3) | Provided by [`44-`](44-framework-module-plan.md) |

## 12. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| KD‑1 | A backend symbol survives in the DDX and nobody notices until the target build | Medium | High | §4.3's mock link test, run every build (V‑P1‑4) |
| KD‑2 | D7 left open: the input decoder desynchronises permanently under load | **High** | **High** | V‑P2‑9; it is a live bug today, not a preemption‑only hazard |
| KD‑3 | `tima_WaitForEvents()` implemented as a spin rather than a yield | Medium | Medium | V‑P4‑2's < 5 % CPU gate |
| KD‑4 | The contract grows a function to solve a hosted‑only problem | Medium | High | The contract is closed; extensions require a decision entry. [`30-`](30-host-build.md) §9's anti‑pattern list |
| KD‑5 | Wheel/button defects (D2, D3) surface only at P7, as "JWM feels wrong" | Medium | Low | V‑P1‑6 and V‑P1‑7 close them at P1, with scripted injection |
| KD‑6 | A target ISR delivers input by calling into WMB | Low | **High** | PC2. `hal_input_pump` exists so that the ISR only ever fills a ring |
| KD‑7 | The hosted build acquires a shortcut above the contract | Medium | High | Gate S12: `framework/` must stay platform‑free |

## 13. Budget

| | Value |
|---|---|
| New files (P1) | 4 contract headers + 3 SDL adapters + `hal_mock.c` + `FindLocalSDL2.cmake` + 3 smoke targets |
| New LOC (P1) | ~900 |
| New LOC (P9) | ~600 for the target backend |
| Upstream files guarded | **1** — `kinput.c`, drawing on the `tinyx/` cap of 8 |
| DDX files edited | 4, all ours; **no patch‑budget cost** |

## 14. What to Do First

1. **Write the four contract headers.** They are ~300 lines total, fully specified in [`12-platform-contract.md`](12-platform-contract.md) §4–§8, and
   every subsequent file in this module includes one of them.
2. **Build `wmb_hal_smoke` and get a gradient on screen.** It is the project's first observable result, it validates the whole P1 boundary, and it
   costs a fraction of anything that follows.
3. **Stand up `wmb_ddx_linktest` immediately after** — before the DDX re‑pointing is finished, so that it fails first and then goes green, rather than
   being written to pass.

Then D1–D6, in the order listed in §2.2 — D5 last, because it is the only one that needs `hal_task.h` from [`44-`](44-framework-module-plan.md).
