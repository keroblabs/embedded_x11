# 01 — Component Inventory

## 1. Purpose

This document is the **authoritative list of every component WMB needs**, at file granularity, with a disposition for each. It is the first deliverable of
workstream 1 ([`00-introduction.md`](00-introduction.md) §5.1) and it is the scoping input for every other workstream: the OS dependency census
([`10-`](10-os-dependency-census.md)) only looks at files marked CARRY or GUARD here, and the build assembly ([`30-`](30-host-build.md)) mirrors the module
groups defined here one‑for‑one.

It answers three questions:

1. **What must be linked into WMB?** Which upstream modules are load‑bearing, which are optional, and which are dead weight that must be actively excluded.
2. **What must be written?** The components that do not exist yet — a hosted fiber port, a portable display/input HAL, a transport shim, a boot sequence, a
   target HAL skeleton.
3. **What does each cost?** LOC, so effort and image size can be reasoned about before the first line is written.

### 1.1 What this document is not

- **Not the architecture.** How the components interact is [`02-baseline-architecture.md`](02-baseline-architecture.md) (today) and
  [`03-target-architecture.md`](03-target-architecture.md) (target).
- **Not the OS dependency analysis.** Which files are Linux‑coupled and how badly is [`10-os-dependency-census.md`](10-os-dependency-census.md).
- **Not the build script.** The concrete CMake is [`30-host-build.md`](30-host-build.md) §4. This document defines the *groups* that CMake mirrors.
- **Not a size budget.** LOC counts here are inputs to a future size analysis, not a commitment.

## 2. Disposition Vocabulary

Every file or module group gets exactly one disposition.

| Tag | Meaning | Consequence |
|---|---|---|
| **CARRY** | Compiled into WMB unmodified, in both profiles. | No entry in the patch manifest. Still subject to the OS census. |
| **GUARD** | Compiled into WMB, but with `#ifdef` guards added around OS‑coupled regions. | Line item in the patch manifest ([`11-`](11-portability-plan.md) §9). Hard budget applies. |
| **STUB** | Excluded from the target profile; a WMB‑owned replacement file with the same external symbols is compiled instead. | New file in `tinyx/os/` or `framework/`. Not a patch. |
| **EXCLUDE** | Never compiled into WMB, in any profile. | Must be *proven* unreachable — an EXCLUDE whose symbols are referenced is a defect. |
| **NEW** | Does not exist yet; this project writes it. | Owned by `framework/` or by a WMB‑prefixed file in an upstream tree. |
| **DORMANT** | Present in the repository, not used by WMB now, retained for a possible future phase. | Not compiled, not deleted, explicitly noted. |

## 3. Component Map

WMB is eight components. Everything below is one of these.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  C8  Applications        JWM  +  in-tree test apps  +  external apps         │
├──────────────────────────────────────────────────────────────────────────────┤
│  C7  Entry-point ABI     wmb_api_t vtable · thin app-side wrapper library    │
├──────────────────────────────────────────────────────────────────────────────┤
│  C6  Xlib                libX11 API files (unchanged) + WMB transport shim   │
├──────────────────────────────────────────────────────────────────────────────┤
│  C5  X server core       DIX · MI · FB · shadow · damage · extensions        │
├──────────────────────────────────────────────────────────────────────────────┤
│  C4  Server OS layer     tinyx/os/ — reduced to logging, time, colours, utils│
├──────────────────────────────────────────────────────────────────────────────┤
│  C3  DDX (KDrive)        kdrive/src/ generic  +  kdrive/tima/ our backend    │
├──────────────────────────────────────────────────────────────────────────────┤
│  C2  Platform contract   hal_display · hal_input · hal_task · hal_time · os  │
├──────────────────────────────────────────────────────────────────────────────┤
│  C1  Runtime substrate   fiber core + arch port  ·  heap  ·  boot            │
└──────────────────────────────────────────────────────────────────────────────┘
        Backends (exactly one selected at build time):
        · hosted:  linux_hosted/sdl_drivers/ + linux_hosted/sdl_library/
                   + linux_hosted/fiber_linux/ + linux_hosted/system_linux/
        · target:  <target>_native/display_<target>/ + input_<target>/
                   + fiber_<target>/ + system_<target>/
```

C1–C2 are ours. C3 is half ours. C4–C6 are upstream under a patch budget. C7 is ours. C8 is upstream (JWM) plus ours (apps).

## 4. C1 — Runtime Substrate

The bottom of the stack: what provides execution contexts, memory, and the entry point.

### 4.1 Fiber core — `framework/fiber/`

Already written, portable, never compiled. This is the single most valuable pre‑existing asset in the tree.

| File | LOC | Disposition | Notes |
|---|---:|---|---|
| `fiber.c` | 520 | CARRY | Core scheduler: create/destroy/yield/critical sections, canary check, **`fiber_wait_for` / `fiber_notify`** |
| `fiber_sync.c` | 250 | CARRY | `fiber_mutex_t`, `fiber_cond_t`, `fiber_join` — all expressed as predicates over `fiber_wait_for` (v2.0) |
| `fiber.h` | 115 | CARRY | Public API: `fiber_runtime_init`, `fiber_create`, `fiber_create_static`, `fiber_yield`, `fiber_destroy`, `fiber_wait_for`, `fiber_notify`, `fiber_critical_enter/exit`, `fiber_self`, `fiber_main` |
| `fiber_sync.h` | 57 | CARRY | Sync primitives |
| `fiber_internal.h` | 70 | CARRY | Scheduler internals |
| `fiber_config.h` | 45 | GUARD | Tunables. Needs a hosted profile block — see §4.2 |
| `fiber_port.h` | 60 | CARRY | **The arch contract**: `fiber_port_init`, `fiber_port_init_stack`, `fiber_port_request_switch`, `fiber_port_irq_lock/unlock`, `fiber_sched_swap`, `fiber_trampoline` |
| `hal_task.c` | 210 | **DONE** | WMB's implementation of all nine `hal_task_*` functions. Lives here, not in a port tree, because `hal_task.h` is the one contract header a platform does **not** implement ([`90-`](90-decisions.md) D‑31, [`12-`](12-platform-contract.md) §9) |
| `fiber.cmake` | 8 | GUARD | Needs conditional arch source selection |
| `docs/*.md` | — | DORMANT | Six pre‑existing design documents; useful reference, not part of this doc set |

**Current defaults are sized for a microcontroller and must be overridden for the host:** `FIBER_MAX` 8, `FIBER_DEFAULT_STACK` 2048 bytes,
`FIBER_DEFAULT_PREEMPTION_MS` 1, `FIBER_MUTEX_MAX` 8, `FIBER_COND_MAX` 8. A 2 KB stack will not survive one call into `fb`. Required hosted overrides are
in [`12-platform-contract.md`](12-platform-contract.md) §6.4.

### 4.2 Fiber architecture ports — **NEW**

`fiber_port.h` is a contract in `framework/fiber/`; its implementations live in the port trees (rule L9). Two are needed.

| File | Est. LOC | Disposition | Implements |
|---|---:|---|---|
| `linux_hosted/fiber_linux/fiber_port.c` | 120 | **DONE** (cooperative only) | `fiber_port_init_stack` via `makecontext`, `fiber_port_request_switch`, `fiber_port_irq_lock/unlock`. The preemption tick is still absent — P2 |
| `linux_hosted/fiber_linux/intr_handler.c` | 30 | **DONE** | Global IRQ-enable flag and the crash hook |
| `linux_hosted/fiber_linux/hosted_timer.c` | ~90 | NEW (P2) | One `pthread` running a `clock_nanosleep` loop that drives `fiber_tick_isr()` — the *only* thread in the hosted build |
| `linux_hosted/fiber_linux/hosted_intr.c` | ~70 | NEW (P2) | POSIX signal → deferred‑switch dispatch, simulating a software interrupt |
| `linux_hosted/third_party/libucontext/` | vendored | NEW (P2) | Portable `getcontext`/`makecontext`/`swapcontext`; avoids glibc's deprecated implementation ([`90-`](90-decisions.md) D‑11). Until it lands the port falls back to glibc `<ucontext.h>` |
| `<target>_native/fiber_<target>/` | ~150 + asm | NEW (P9) | Target port. Deferred to [`31-target-build.md`](31-target-build.md) §6 |

### 4.3 Memory — `framework/library/`

| File | LOC | Disposition | Notes |
|---|---:|---|---|
| `heap.c` / `heap.h` | 474 / 50 | CARRY | Region allocator, no libc dependency |
| `heap_mngr.c` / `heap_mngr.h` | 122 / 22 | CARRY | Heap registry / policy |
| `buffer.c` / `buffer.h` | 257 / 60 | CARRY | Ring buffer used by the HAL input path. Byte streams only ([`90-`](90-decisions.md) D‑48) |
| `pipe.c` / `pipe.h` | 239 / 65 | CARRY | Bounded FIFO of owned byte buffers; preserves record boundaries, discards whole entries |
| `lists.c` / `lists.h` | 358 / 115 | CARRY | Singly linked, counted list of owned nodes. The one container that can drop a record from the *middle*, which is what the transport's event queue needs ([`21-`](21-inprocess-transport.md) §2.1, §6.1) |

These plus `framework/fiber/` are the **shareable framework** — `libwmb_runtime.a` ([`30-`](30-host-build.md) §4.4). They must never acquire a
dependency on `tinyx/`, `libX11/` or `jwm/` (rule L8), because other system components will eventually link them without WMB present
([`40-`](40-implementation-plan.md) §13).

`heap.c` is the allocator in **every** profile — `malloc`/`calloc`/`realloc`/`free` redirect to it on host and target alike ([`90-`](90-decisions.md)
D‑29). Using libc `malloc` on the host and `heap.c` on the target was rejected: a heap bug that appears only where there is no Valgrind is the worst
possible outcome, and running the same allocator in both places makes the hosted high‑water measurement meaningful for target sizing.

### 4.4 Boot — **NEW**

| File | Est. LOC | Disposition | Notes |
|---|---:|---|---|
| `linux_hosted/wmb_boot/wmb_boot.c` | ~40 | NEW | This configuration's **host**, and the `main()`. Owns: early platform init → `heap_init` → `fiber_runtime_init()` → `wmb_main()` → park. Outside the windowing set ([`90-`](90-decisions.md) D‑42) |
| `transport/x11/wmb_main.c` | ~80 | REWRITE | `wmb_main()` / `wmb_shutdown()` — display and input bring‑up, WMB's fiber creation, the readiness gate ([`90-`](90-decisions.md) D‑43). Replaces the 6‑line stub of the same name |

**Only one `main()` may exist in the image.** `tinyx/dix/main.c:227` currently defines one. Its resolution is a settled decision — see
[`90-decisions.md`](90-decisions.md) D‑07.

## 5. C2 — Platform Contract and Backends

### 5.1 The contract — **NEW**

Five headers. This is *the entire surface* between WMB and the machine. Full signatures in [`12-platform-contract.md`](12-platform-contract.md).

| File | Est. LOC | Disposition | Provides |
|---|---:|---|---|
| `hal/hal_display.h` | 55 | **DONE** | `hal_framebuffer_t`, `hal_pixfmt_t`, `hal_display_init/shutdown/get_fb/flush/set_power` |
| `hal/hal_input.h` | 90 | **DONE** | `hal_input_dev_t`, `hal_input_init/shutdown/has_data/read_byte/control/pump`, and the `HAL_CMD_*` / `HAL_KEY_*` / `HAL_MOUSE_BUTTON_*` / `HAL_SC_*` wire constants |
| `hal/hal_task.h` | 60 | **DONE** | `hal_task_runtime_init/create/destroy/self/name/yield/wait/wake`, `hal_yield_timeout` |
| `hal/hal_time.h` | 35 | **DONE** | `hal_tick_ms`, `hal_tick_us`, and `hal_wallclock_s` behind `WMB_HAS_WALLCLOCK` ([`90-`](90-decisions.md) D‑30) |
| `framework/library/wmb_os.h` | ~80 | NEW | Console write, abort/exit, allocator hooks, optional file I/O — the libc floor |

All four completed headers compile standalone under `-ffreestanding -nostdinc` (gate PC‑V1 / S8).

### 5.2 Hosted backend — `linux_hosted/`

Exists; needs refactoring to implement the §5.1 contract instead of the current ad‑hoc `hal_drivers.h`. Per rule L9 the whole SDL backend lives in the
hosted port tree, not in `framework/`.

| File | LOC | Disposition | Notes |
|---|---:|---|---|
| `sdl_drivers/hal_drivers.h` | 76 | GUARD → superseded | Current interface: `lcd_driver_*` (10 fns), `keypad_driver_*` (3), `mouse_driver_*` (5), plus the `CMD_*` / `KEY_DATA_*` / `MOUSE_BUTTON_*` protocol constants. The **constants are kept** (they are the target wire format); the function set is re‑expressed as §5.1 |
| `sdl_drivers/lcd_driver.c` | 213 | CARRY | Becomes the `hal_display_*` implementation |
| `sdl_drivers/keyraw_driver.c` | 109 | CARRY | Becomes part of `hal_input_*` |
| `sdl_drivers/mouse_driver.c` | 193 | CARRY | Becomes part of `hal_input_*` |
| `sdl_library/sdl_window.c` | 336 | CARRY | SDL window + texture + present |
| `sdl_library/sdl_input.c` | 261 | CARRY | SDL keyboard event → `CMD_*` byte stream |
| `sdl_library/sdl_mouse.c` | 108 | CARRY | SDL mouse event → `CMD_*` byte stream |
| `sdl_library/sdl_main.c` | 78 | GUARD | SDL init/teardown; must **not** own `main()` |
| `linux_hosted/sdl_drivers/hal_display_sdl.c` | ~120 | **DONE** | Thin adapter binding `lcd_driver_*` to `hal_display_*` |
| `linux_hosted/sdl_drivers/hal_input_sdl.c` | ~110 | **DONE** | Thin adapter binding the driver trio to `hal_input_*` |
| `linux_hosted/system_linux/hal_time_hosted.c` | ~75 | **DONE** | `clock_gettime(CLOCK_MONOTONIC)` |
| `linux_hosted/fiber_linux/fiber_port.c` | 120 | **DONE** | `fiber_port.h` for the hosted port; cooperative‑only until P2 adds the tick |

All paths in this table are relative to the repository root; the leading `linux_hosted/` is the port tree required by rule L9
([`03-`](03-target-architecture.md) §3.1).

### 5.3 Target backend — **NEW**, deferred to P9

A sibling of `linux_hosted/`, with the same module shape. The full per‑file port checklist is [`31-target-build.md`](31-target-build.md) §4.6.

| File | Disposition | Notes |
|---|---|---|
| `<target>_native/display_<target>/hal_display_<target>.c` | NEW | MMIO framebuffer |
| `<target>_native/input_<target>/hal_input_<target>.c` | NEW | Input ring buffer fed by ISR |
| `<target>_native/system_<target>/hal_time_<target>.c` | NEW | Free‑running hardware counter |
| `<target>_native/system_<target>/wmb_os_<target>.c` | NEW | `wmb_os_sbrk` / `console_write` / `abort` / `exit` |
| `<target>_native/fiber_<target>/fiber_port.c` + `<arch>.S` | NEW | `fiber_port.h`'s five symbols |

### 5.4 Shared type header — `framework/types.h`, `framework/config_tima.h`

| File | LOC | Disposition | Notes |
|---|---:|---|---|
| `types.h` | 171 | GUARD | `bool_t`, sized ints, `PIXEL_TYPE_T`. Must become freestanding‑clean (`<stdint.h>` / `<stddef.h>` only) |
| `config_tima.h` | 58 | GUARD | Screen geometry and feature switches. Rename target: `wmb_config.h` |

## 6. C3 — DDX (KDrive)

### 6.1 Generic KDrive — `tinyx/kdrive/src/` — 7 files, 3,631 LOC

| File | LOC | Disposition | Notes |
|---|---:|---|---|
| `kdrive.c` | 1,241 | CARRY | `KdInitOutput`, `KdCardInfoAdd`, screen/card lifecycle, `KdOsInit` |
| `kinput.c` | 1,634 | GUARD | `KdEnqueueKeyboardEvent`, `KdEnqueueMouseEvent`, keymap handling. **Includes `<signal.h>`** — one guard |
| `kmode.c` | 272 | CARRY | Mode list handling |
| `kcmap.c` | 241 | CARRY | Colormap |
| `kmap.c` | 151 | **EXCLUDE** | `mmap`/`ioctl` based physical memory mapping — a hosted‑Linux hardware facility we do not use |
| `kinfo.c` | 137 | CARRY | Card/screen info |
| `kshadow.c` | 77 | CARRY | Shadow buffer binding |
| `kdrive.h` | 419 | CARRY | The four vtable typedefs, `KdCardInfo`, `KdScreenInfo` |
| `kkeymap.h` | 50 | CARRY | `kdKeymap`, `kdMinScanCode`, `kdMaxScanCode` |

### 6.2 Our DDX backend — `tinyx/kdrive/tima/` — 5 files, 1,088 LOC

Exists, never compiled. Currently reaches the hardware through `hal_drivers.h`; must be re‑pointed at the §5.1 contract.

| File | LOC | Disposition | Provides | Gaps |
|---|---:|---|---|---|
| `tima_kdrive.h` | 88 | GUARD | Vtable externs, private types | — |
| `tima_kdrive_fb.c` | 435 | GUARD | `KdCardFuncs`: cardinit, scrinit, initScreen, finishInitScreen, createRes, preserve, enable, dpms, disable, restore, scrfini, cardfini, getColors, putColors | Re‑point to `hal_display_*` |
| `tima_kdrive_kbd.c` | 321 | GUARD | `KdKeyboardFuncs` + `tima_KeyboardPoll()` | Re‑point to `hal_input_poll_key`; LEDs and bell ignored |
| `tima_kdrive_mouse.c` | 116 | GUARD | `KdMouseFuncs` + `tima_MousePoll()` | **Four known defects** — see §6.3 |
| `tima_kdrive_system.c` | 128 | GUARD | `KdOsFuncs` + `InitCard`, `InitOutput`, `InitInput`, `OsVendorInit`, `ddxUseMsg`, `ddxProcessArgument`, `ddxGiveUp`, `AbortDDX` | **`tima_WaitForEvents()` does not exist** — the cooperative wait hook the whole design rests on |

### 6.3 Known DDX defects — must close in P1

Carried forward from the prior document generation and confirmed against the current sources:

| # | File | Defect |
|---|---|---|
| D1 | `tima_kdrive_mouse.c` | `dx`, `dy`, `flags` used before initialisation |
| D2 | `tima_kdrive_mouse.c` | No button‑release tracking — press/release deltas are never computed |
| D3 | `tima_kdrive_mouse.c` | Wheel events (`CMD_MOUSE_WHEEL_X/Y`) are read and discarded instead of becoming X buttons 4/5/6/7 |
| D4 | `tima_kdrive_mouse.c` | `mouse_driver_init(0)` is never called from `tima_MouseInit()` |
| D5 | `tima_kdrive_system.c` | `tima_WaitForEvents()` missing; `KdOsFuncs` has no cooperative wait wired |
| D6 | `tima_kdrive_system.c` | `ddxProcessArgument` returns 0 unconditionally while `ddxUseMsg` advertises `-fb` and `-fbsize` |
| **D7** | `keyraw_driver.c`, `mouse_driver.c` | **The HAL input rings use `buffer.c` outside its scope.** `buffer.c` is a byte FIFO and drops a single byte on overflow by design ([`90-`](90-decisions.md) D‑48); these two rings carry `HAL_CMD_*` command/data **pairs**, and [`12-`](12-platform-contract.md) §5.3 requires overflow never to truncate one. A full keyboard (20 B) or mouse (128 B) ring desynchronises the decoder *permanently*. Not a `buffer.c` bug — a misuse of it. D‑47 precondition **R2** |

### 6.4 Other KDrive backends — EXCLUDE

| Directory | Files / LOC | Disposition | Reason |
|---|---|---|---|
| `tinyx/kdrive/linux/` | `linux.c` 348, `keyboard.c` 469, `mouse.c` 943 | **EXCLUDE** | The Linux console/evdev/VT backend. Includes `<linux/*>`, `<termios.h>`, `<sys/ioctl.h>`, `<signal.h>`, and the only `select()` outside `tinyx/os/`. Wholly replaced by `kdrive/tima/` |
| `tinyx/kdrive/fbdev/` | `fbdev.c` 784, `fbinit.c` 100 | **EXCLUDE** | Linux `/dev/fb0` backend |
| `tinyx/kdrive/vesa/` | `vesa.c` 1,696, `vbe.c` 692, `vm86.c` 754, `vga.c` 224, `vesainit.c` 106 | **EXCLUDE** | x86 VESA/VM86 backend |

Excluding these three directories removes **6,116 LOC and the majority of the tree's Linux coupling in one stroke.** See
[`10-os-dependency-census.md`](10-os-dependency-census.md) §4.

## 7. C4 — Server OS Layer (`tinyx/os/`)

16 `.c` files, 11,082 LOC. This is where the transport lives and therefore where the surgery concentrates. Full analysis in
[`10-os-dependency-census.md`](10-os-dependency-census.md) §3; dispositions summarised here.

| File | LOC | Disposition | Reason |
|---|---:|---|---|
| `log.c` | 196 | CARRY | `ErrorF`, `LogVWrite`. Needs only console write |
| `oscolor.c` | 1,598 | CARRY | Built‑in RGB name table. Large but self‑contained and file‑I/O‑free in the built‑in path |
| `strlcat.c` | 56 | CARRY | Pure string helper |
| `strlcpy.c` | 51 | GUARD | Includes `<sys/types.h>` purely for `size_t`; one‑line change to `<stddef.h>` |
| `osinit.c` | 199 | GUARD | `OsInit`. Drop `setrlimit` (`<sys/resource.h>`), drop the `fopen` log redirect |
| `utils.c` | 1,640 | GUARD | Mixed bag: `GetTimeInMillis`, `FatalError`, argument parsing — plus signals (`<signal.h>`), `<sys/wait.h>`, `<netdb.h>`, privilege‑dropping `Fopen()`. Largest guard surface in the tree |
| `WaitFor.c` | 670 | **STUB** | The `select()` scheduler loop. `Select()` at L215/L219. Replaced by a `hal_yield_timeout` loop |
| `connection.c` | 1,027 | **STUB** | Socket listener, `accept`, `getpeername`, client lifecycle. Replaced by internal‑client registration |
| `io.c` | 1,026 | **STUB** | `ReadRequestFromClient`, `WriteToClient`, `FlushClient` over `xtrans`. Replaced by direct buffer access |
| `xstrans.c` | 30 | **EXCLUDE** | Pulls in the whole `X11/xtrans` layer |
| `access.c` | 1,952 | **EXCLUDE** | Host access control. `<sys/socket.h>`, `<netinet/*>`, `<netdb.h>`, `<pwd.h>`, `<grp.h>`, `<sys/utsname.h>`, `<sys/ioctl.h>`, `fopen` |
| `auth.c` | 236 | **EXCLUDE** | Authorisation dispatch |
| `mitauth.c` | 162 | **EXCLUDE** | MIT‑MAGIC‑COOKIE |
| `xdmauth.c` | 501 | **EXCLUDE** | XDM‑AUTHORIZATION‑1 |
| `xauembed.c` | 120 | **EXCLUDE** | Embedded Xauthority |
| `xdmcp.c` | 1,618 | **EXCLUDE** | XDMCP over UDP |
| `osdep.h` | 274 | GUARD | Internal `tinyx/os/` header; becomes a thin shim over `wmb_os.h` |

**Result: 4,589 LOC excluded outright, 2,723 LOC stubbed, 3,740 LOC carried or guarded.** The three STUB files are the transport and are the subject of
[`20-transport-inventory.md`](20-transport-inventory.md).

### 7.1 Replacement stubs — **NEW**

| File | Est. LOC | Replaces | Notes |
|---|---:|---|---|

| `tinyx/os/waitfor_wmb.c` | ~290 | `WaitFor.c` | `WaitForSomething()` → `BlockHandler` → `hal_yield_timeout` → `WakeupHandler`; returns the ready‑client count. **Also carries the timer machinery** — `TimerSet`/`TimerForce`/`TimerFree`/`SetScreenSaverTimer` and the DPMS helpers — since `WaitFor.c` is never compiled ([`91-`](91-open-questions.md) Q22) |
| `tinyx/os/connection_wmb.c` | ~120 | `connection.c` | `CreateWellKnownSockets` no‑op; `wmb_register_internal_client()`; `CloseDownConnection`; `IgnoreClient`/`AttendClient` no‑ops |
| `tinyx/os/io_wmb.c` | ~160 | `io.c` | `ReadRequestFromClient` reads `dpy->buffer` in place; `WriteToClient` deposits into the client's fiber‑local reply/event queue; `FlushClient` no‑op |

Naming convention: a WMB replacement for an upstream file is `<original>_wmb.c` and lives beside it. It is a **new file**, not a patch, and therefore does
not consume patch budget.

## 8. C5 — X Server Core

The parts of TinyX that implement the protocol and draw pixels. Almost all CARRY — this is upstream code that is already OS‑agnostic.

| Module | Dir | Files | LOC | Disposition | Notes |
|---|---|---:|---:|---|---|
| **DIX** | `tinyx/dix/` | 25 | 26,685 | GUARD (1 file) | Protocol core. `dispatch.c` holds the main loop; `tables.c` holds `ProcVector`. Only `main.c` needs a guard (`main()` + `getenv("XAUTHORITY")` at L250) |
| **MI** | `tinyx/mi/` | 35 | 23,533 | CARRY | Machine‑independent rendering, `mieq` event queue, `mipointer` cursor, `miinitext` extension registration |
| **FB** | `tinyx/fb/` | 32 | 14,437 | CARRY | The framebuffer engine. Zero OS coupling |
| **shadow** | `tinyx/miext/shadow/` | 20 | ~3,000 | CARRY | Shadow buffer + rotation blitters. Only `shadow.c`, `shalloc.c` and the 16‑bit packed blitters are load‑bearing at our bpp; the rest are dead weight but harmless and are kept for simplicity |
| **damage** | `tinyx/miext/damage/` | 1 | ~500 | CARRY | `miext/damage/damage.c`, required by `damageext` and by shadow |
| **render** | `tinyx/render/` | 12 | 7,962 | CARRY | RENDER extension. Required by JWM's compositing‑adjacent paths and by `fbpict.c` |
| **randr** | `tinyx/randr/` | 12 | 5,217 | CARRY | Referenced unconditionally by `kdrive.c`. Not used functionally (fixed mode) but must link |
| **xfixes** | `tinyx/xfixes/` | 5 | 2,475 | CARRY | Referenced by `render` and `damageext` |
| **damageext** | `tinyx/damageext/` | 1 | 515 | CARRY | DAMAGE protocol extension |
| **dbe** | `tinyx/dbe/` | 2 | 2,620 | **EXCLUDE** | Double Buffer Extension. Not required by JWM; not required by KDrive. Candidate for later re‑inclusion |
| **Xext** | `tinyx/Xext/` | 11 | 8,742 | **SPLIT** | See §8.1 |

### 8.1 `tinyx/Xext/` — per‑file split

| File | Disposition | Reason |
|---|---|---|
| `bigreq.c` | CARRY | BIG‑REQUESTS; trivial, referenced by `miinitext` |
| `shape.c` | CARRY | SHAPE; JWM uses it for rounded/shaped decorations if enabled |
| `sync.c` | CARRY | SYNC; referenced by `miinitext` and by the smart scheduler |
| `xcmisc.c` | CARRY | XC‑MISC; XID range allocation — needed once clients live a long time |
| `xtest.c` | CARRY | XTEST; cheap, useful for automated input injection in verification |
| `sleepuntil.c` | CARRY | Helper for SYNC's `AwaitSync`; touches `WaitForSomething` semantics — verify against the stub |
| `saver.c` | GUARD | MIT‑SCREEN‑SAVER. Keep; disable the DPMS path |
| `dpms.c` | GUARD | DPMS. Keep the protocol, stub the card hooks |
| `xres.c` | CARRY | X‑Resource; small |
| `shm.c` | **EXCLUDE** | MIT‑SHM. Requires `<sys/shm.h>`, `<unistd.h>`. Meaningless when there is one address space |
| `xf86bigfont.c` | **EXCLUDE** | Requires `<unistd.h>` and shared memory. Obsolete |

Excluding `shm.c` and `xf86bigfont.c` also requires a guarded edit to `tinyx/mi/miinitext.c`, which registers them. That is one GUARD line item.

### 8.2 Generated configuration headers — **NEW**

TinyX is an autotools project; its sources expect `dix-config.h` and `kdrive-config.h` under `HAVE_CONFIG_H`. The current framework build sidesteps this by
leaving `HAVE_CONFIG_H` undefined, which will not survive whole‑stack assembly.

| File | Disposition | Notes |
|---|---|---|
| `framework/dix-config.h.in` | NEW | CMake `configure_file()` template; hosted and target variants |
| `framework/kdrive-config.h.in` | NEW | Same |

The full macro set to define is [`91-open-questions.md`](91-open-questions.md) Q2. It is curated by hand against a one‑off reference `configure` run on a
pristine checkout — never guessed, and never copied in wholesale ([`90-`](90-decisions.md) D‑37).

## 9. C6 — Xlib

`libX11/src/` is 262 `.c` files, 37,265 LOC. The overwhelming majority is **API surface**: `XCreateWindow.c`‑style files that build a request into
`dpy->buffer` and call `SyncHandle()`. Those files are untouched.

| Group | Files | Disposition | Notes |
|---|---:|---|---|
| Request‑building API files | ~245 | CARRY | The entire point of the XCB‑stub strategy: replace the transport, not the API |
| `xcb_io.c` | 1 | **EXCLUDE** | The transport. 13 non‑static functions — see [`20-`](20-transport-inventory.md) §3 |
| `xcb_disp.c` | 1 | **STUB** | `xcb_get_setup()` etc.; the setup blob comes from the server over the normal reply path ([`21-`](21-inprocess-transport.md) §3.3) |
| `x11_xcb.c` | 1 | **EXCLUDE** | XCB interop |
| `OpenDis.c` | 1 | **STUB** | `XOpenDisplay` — replaced by an in‑process variant that registers a `ClientRec` |
| `ClDisplay.c` | 1 | **STUB** | `XCloseDisplay` |
| `SetLocale.c`, `lc*` , `Xrm.c`, `im/`, `xlibi18n/` | many | **EXCLUDE** | Locale, input methods, resource manager. Out of scope per [`00-`](00-introduction.md) §4.2 |
| `X11/Xlibint.h` | 1 header | GUARD | The `Data()` macro must resize `dpy->buffer` instead of calling `_XSend`. **One targeted edit; the highest‑leverage patch in the project** |

### 9.1 Transport shim — **NEW**

| File | Est. LOC | Disposition | Notes |
|---|---:|---|---|
| `transport/x11/wmb_transport.c` | ~280 | NEW | Replaces `xcb_io.c`. Grows from the existing 277‑line `wmb_io.c` sketch |
| `transport/x11/wmb_open.c` | ~140 | NEW | In‑process `XOpenDisplay`: build `Display`, register `ClientRec`, install `dpy->synchandler` |
| `transport/x11/wmb_close.c` | ~60 | NEW | Teardown |
| `transport/x11/wmb_setup.c` | ~120 | NEW | Synthesise the `xConnSetup` blob that `XOpenDisplay` normally reads off the wire |
| `transport/x11/Xlibint_wmb.h` | 43 (exists) | CARRY | Local Xlib internal overrides |
| `transport/x11/wmb_io.h` | 0 (empty) | NEW | Currently a zero‑byte file. Must be written |

### 9.2 Existing sketch — `transport/x11/wmb_io.c`

277 lines, never compiled. Contains a working‑looking `wmb_sync_handler()` that sets `client->requestBuffer = dpy->buffer` and calls
`client->requestVector[opcode](client)`, plus `_XGetRequest` and a `WmbState` struct. **It assumes a single global client** (`static WmbState wmb_state`),
which is wrong the moment JWM and an application coexist. It is a starting point, not a design. See
[`21-inprocess-transport.md`](21-inprocess-transport.md) §4.

## 10. C7 — Entry‑Point ABI

For applications that are *separate binaries* rather than in‑tree fibers. Fully specified in [`22-entry-point-abi.md`](22-entry-point-abi.md).

| File | Est. LOC | Disposition | Notes |
|---|---:|---|---|
| `transport/api/wmb_api.h` | ~180 | NEW | The `wmb_api_t` vtable definition, shipped to applications |
| `transport/api/wmb_api_table.c` | ~80 | NEW | The vtable instance, placed in a dedicated section |
| `transport/api/wmb.ld` | ~30 | NEW | Linker fragment pinning that section to a fixed address |
| `wrapper/libX11-app/wrappers.c` | ~220 | NEW | The redistributable thin wrapper library: one line per entry point |

## 11. C8 — Applications

### 11.1 JWM — `jwm/src/` — 50 `.c`, 26,325 LOC (+ 4,517 LOC headers)

| Concern | Files | Disposition | Action |
|---|---|---|---|
| Core WM logic | `client.c`, `border.c`, `place.c`, `resize.c`, `move.c`, `desktop.c`, `group.c`, `taskbar.c`, `tray.c`, `menu.c`, `root.c`, `winmenu.c`, … | CARRY | No changes |
| Entry point | `main.c` | GUARD | `main` → `Jwm_Main`; drop `sigaction`; hoist globals into a state struct |
| Event loop | `event.c` | GUARD | `select()` → `hal_yield_timeout()` |
| Command execution | `command.c` | GUARD | `fork`/`execl` → in‑tree application table lookup |
| Timing | `timing.c` | GUARD | Drop `TZ` push/pop |
| Misc | `misc.c` | GUARD | Last `getenv` |
| Fonts | `font.c` | GUARD | Core fonts only; no Xft path |
| Xlib call macros | `jxlib.h` | GUARD | Add the profile block that routes `JX*` through the vtable |
| Build glue | `jwm_namespace.h`, `jwm_entry.c`, `jwm_entry.h`, `jwm/CMakeLists.txt` | NEW | Symbol renaming (injected with `-include`) and the fiber entry adapter |

**Optional dependencies to disable** (all present in `jwm/configure.ac` as `--enable-*`): PNG, JPEG, Xpm, cairo, librsvg, Xft, Xrender, pango, Xmu,
Xinerama. Retained: XBM icons (via `XReadBitmapFileData`), SHAPE (optional).

Six files hold JWM's entire POSIX footprint: `command.c`, `event.c`, `font.c`, `main.c`, `misc.c`, `timing.c`. Everything else is pure Xlib.

### 11.2 Test applications — **NEW**

| File | Est. LOC | Disposition | Purpose |
|---|---:|---|---|
| `linux_hosted/sdl_fiber_example_v2/` | 330 | **DONE** | The transport proof of concept: three client fibers dispatch draw requests to one render fiber over a `lists.c` queue, using the post/notify/predicate protocol of [`21-`](21-inprocess-transport.md) §4.1 and §11. Gate V‑P5‑0 |
| `apps/wmb_test_client/main.c` | ~110 | NEW | The **Level‑0 test**: open display, create window, map, wait Expose, fill rect, draw string, close — printing PASS/FAIL per step. Runs as a fiber, zero sockets. This is the gate for P5 |
| `apps/wmb_hello/main.c` | ~70 | NEW | An *external* binary linking only the wrapper library, to validate C7 |

### 11.3 Dormant

| Tree | Disposition | Notes |
|---|---|---|
| `fonts/freetype/` | DORMANT | Full FreeType checkout. Not compiled. Built‑in bitmap fonts ship first; FreeType is the named later upgrade — [`90-`](90-decisions.md) D‑38 |
| `X11/fonts/` | DORMANT | `libXfont` sources. Font handling in WMB is built‑in bitmap only for now |
| `X11/Xaw`, `X11/Xmu`, `X11/Xt` (`Intrinsic.c` etc.) | DORMANT | Present in the include root. Nothing in WMB links against them |
| `X11/xtrans` | **EXCLUDE** | The transport abstraction being removed |

## 12. Module Groups (Build Assembly Contract)

The build in [`30-host-build.md`](30-host-build.md) §4 defines exactly these groups. Group membership is derived from §6–§11 and nowhere else.

| Group | Contents | Files | ~LOC |
|---|---|---:|---:|
| `WMB_RUNTIME` | `framework/fiber/` + selected port + `framework/library/heap*` + `buffer` + `pipe` + `lists` | 16 | 2,500 |
| `WMB_HAL` | `hal/*.h` + selected backend adapters | 9 | 900 |
| `WMB_BACKEND_SDL` | `linux_hosted/sdl_drivers/` + `linux_hosted/sdl_library/` | 9 | 1,420 |
| `TINYX_DIX` | `tinyx/dix/*.c` | 25 | 26,685 |
| `TINYX_OS` | `log.c`, `oscolor.c`, `strlcat.c`, `strlcpy.c`, `osinit.c`, `utils.c` + the three `*_wmb.c` stubs | 9 | 4,100 |
| `TINYX_MI` | `tinyx/mi/*.c` | 35 | 23,533 |
| `TINYX_FB` | `tinyx/fb/*.c` | 32 | 14,437 |
| `TINYX_MIEXT` | `miext/shadow/*.c` + `miext/damage/damage.c` | 21 | 3,514 |
| `TINYX_EXT` | `render/` + `randr/` + `xfixes/` + `damageext/` + selected `Xext/` | 39 | 24,000 |
| `TINYX_KDRIVE` | `kdrive/src/` minus `kmap.c` | 6 | 3,480 |
| `TINYX_DDX` | `kdrive/tima/` | 4 | 1,088 |
| `LIBX11_API` | `libX11/src/` minus the excluded set | ~245 | ~34,000 |
| `WMB_TRANSPORT` | `transport/x11/` | 6 | 900 |
| `WMB_API` | `transport/api/` | 2 | 260 |
| `JWM` | `jwm/src/` + the new glue | 52 | 26,500 |
| `WMB_APPS` | `apps/` | 2 | 180 |

**Approximate total compiled into WMB: ~165 kLOC across ~500 translation units.** Of that, ~5.5 kLOC is code this project writes; the rest is upstream
carried with guards.

## 13. Exclusion Ledger

Everything actively kept out, with the proof obligation for each. An EXCLUDE that turns out to be referenced is a P4 blocker, not a footnote.

| Excluded | LOC | Referenced by | Proof obligation |
|---|---:|---|---|
| `tinyx/kdrive/linux/` | 1,760 | `configure.ac` only | None — never in the CMake source list |
| `tinyx/kdrive/fbdev/` | 884 | `configure.ac` only | None |
| `tinyx/kdrive/vesa/` | 3,472 | `configure.ac` only | None |
| `tinyx/kdrive/src/kmap.c` | 151 | `kdrive.h` declares `KdMapDevice`/`KdUnmapDevice` | Confirm no caller in the retained set; if there is, provide a no‑op |
| `tinyx/os/access.c` | 1,952 | `connection.c` (stubbed), `dix/dispatch.c` (`ProcChangeHosts`, `ProcListHosts`) | Provide `AccessUsingXdmcp`‑free no‑op stubs in `connection_wmb.c` |
| `tinyx/os/{auth,mitauth,xdmauth,xauembed}.c` | 1,019 | `connection.c`, `dix/main.c` | `CheckAuthorization` stub returning "allowed" |
| `tinyx/os/xdmcp.c` | 1,618 | `dix/main.c` argument parsing | Guard the `-from`/`-query` argument block in `main.c` |
| `tinyx/os/xstrans.c` | 30 | `connection.c`, `io.c` | Both stubbed |
| `tinyx/dbe/` | 2,620 | `mi/miinitext.c` | One guarded line in `miinitext.c` |
| `tinyx/Xext/shm.c` | ~1,000 | `mi/miinitext.c` | Same guard |
| `tinyx/Xext/xf86bigfont.c` | ~700 | `mi/miinitext.c` | Same guard |
| `libX11/src/xcb_io.c` | ~800 | 30+ Xlib files via `Xlibint.h` prototypes | Replaced symbol‑for‑symbol by `wmb_transport.c` — see [`20-`](20-transport-inventory.md) §3 |
| `libX11/src/x11_xcb.c` | small | `Xlib-xcb.h` consumers | None in our set |
| `libX11` locale / i18n / Xrm | ~8,000 | `XOpenDisplay` (locale init), `XSetLocaleModifiers` | Stubbed in `wmb_open.c` |
| `X11/xtrans` | header lib | `os/xstrans.c` only | Excluded together |

## 14. Verification Obligations

These become gates in [`41-verification-plan.md`](41-verification-plan.md).

| # | Obligation | Checked at |
|---|---|---|
| V1 | Every file compiled into WMB has a row in §6–§11 | P4 |
| V2 | No EXCLUDE file appears in any CMake source list | P4 |
| V3 | Every symbol referenced from an EXCLUDE file is satisfied by a STUB or a guard | P4 (link) |
| V4 | The six DDX defects D1–D6 are closed | P1 |
| V5 | Module group membership in CMake matches §12 exactly | P4 |
| V6 | `grep -rl "SDL" tinyx/ libX11/ jwm/` is empty | P6 |
