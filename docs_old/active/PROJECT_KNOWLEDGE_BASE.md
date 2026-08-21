# WMB Project — Centralized Knowledge Base

**Status:** Authoritative entry point. Update in place; do not create new "catch-up" docs.
**Last consolidation:** 2026-05-19
**Supersedes:** every catch-up / session-summary / re-analysis doc in [docs/archive_docs2/](../archive_docs2/) and [docs/library/](../library/). Those remain only as historical reference.

> Read this single document before any work on the project. If something here is wrong, **fix this file** — don't write a new analysis doc.

---

## 1. What WMB Is (One Paragraph)

**WMB (Window Manager Binary)** is a single standalone X11 binary for **Tima OS** (a future RISC-V target, no spec yet). It bundles **TinyX (KDrive X server)** + **JWM (window manager)** + a customized **libX11** (called "libX11-wmb") into one linked image. There is **no X11 socket transport** inside WMB: client and server live in the same address space and talk through direct in-process function calls. The binary is loaded into a fixed memory region and exposes a **fixed-address API entry point** (non-PIC). Hardware access goes through a small **HAL** layer driven by KDrive. A Linux **SDL-hosted** build is mandatory for development.

---

## 2. Non-Negotiable Requirements (source: [BASE_REQUIREMENTS.md](../archive_docs2/BASE_REQUIREMENTS.md), read-only)

1. **Single standalone binary**: TinyX + JWM + libX11-wmb linked into one image.
2. **No client/server socket path** inside WMB.
   - Internal clients (JWM) call TinyX APIs directly.
   - External applications use thin "one-liner" wrappers that enter WMB through a fixed entry-point table.
3. **Two-part X11 library**:
   - **App side**: minimal wrapper stubs (small, redistributable).
   - **WMB side**: full request building / event management.
4. **Fixed-address entry point**, **non-PIC** loading.
5. **HAL-driven graphics + input**, framebuffer-first, fixed width/height/bpp, **no hardware acceleration**.
6. **Xlib-compatible API** (zero learning curve for X11 developers).
7. **Single build invocation** producing the single binary, using **CMake + VS Code**.
8. **Hosted (Linux + SDL) build is mandatory** for development and debugging, with a context-switch library (not pthread) to mimic Tima OS infrastructure.
9. **WMB must be agnostic to the OS** except via HAL / OS-specific files and folders.

These are fixed. Any conflicting design idea elsewhere loses.

---

## 3. Canonical Architecture Decisions

These decisions are settled. Do not re-litigate them.

| Decision | Choice | Why | Source |
|---|---|---|---|
| X server base | **TinyX / KDrive** (not GEM-XServer) | Already framebuffer-oriented, clean HAL boundary | [X_SERVER_COMPARISON.md](../archive_docs2/X_SERVER_COMPARISON.md), [TINYX_MIGRATION.md](../archive_docs2/TINYX_MIGRATION.md) |
| libX11 strategy | **XCB-stub / SyncHandle-based transport substitution** in libX11 — *not* a custom rewrite | Touches almost no libX11 API files (172 files unchanged); intercept request execution via `dpy->synchandler` | [XCB_STUB_ARCHITECTURE.md](../archive_docs2/XCB_STUB_ARCHITECTURE.md) (recommended), [CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md) (superseded broader plan) |
| TinyX integration shape | **Whole-stack module integration**, not file-by-file cherry-picking | KDrive symbols cascade (KdInitOutput, KdInitInput, shadow*, etc.); piecemeal linking always fails | [library/TINYX_WHOLE_STACK_STUB_STANDALONE_ANALYSIS_2026-03-10.md](../library/TINYX_WHOLE_STACK_STUB_STANDALONE_ANALYSIS_2026-03-10.md) |
| TinyX patch policy | **Upstream-safe**: adapter-first, patch-last. Hard cap ≤ 2-3 modified upstream files across M2+M3, all behind feature macros, exportable as ordered `.patch` files | Allows future upstream TinyX updates with low conflict cost | [library/TINYX_STANDALONE_STUB_MILESTONES_UPSTREAM_SAFE_2026-03-10.md](../library/TINYX_STANDALONE_STUB_MILESTONES_UPSTREAM_SAFE_2026-03-10.md) |
| Event wait model | **Cooperative yield**, never true block. `tima_WaitForEvents()` → `hal_yield_timeout(ms)` provided by Tima OS | Single-threaded target; OS must keep running services during waits | [SESSION_SUMMARY_2026-01-25.md](../archive_docs2/SESSION_SUMMARY_2026-01-25.md), [MULTI_CORE_GUI_ANALYSIS.md](../archive_docs2/MULTI_CORE_GUI_ANALYSIS.md) |
| HAL boundary | KDrive `KdCardFuncs` / `KdKeyboardFuncs` / `KdMouseFuncs` / `KdOsFuncs` + small Tima HAL (`lcd_driver_*`, `keypad_driver_*`, `mouse_driver_*`, `hal_yield_timeout`) | KDrive already abstracts hardware; only ~10 functions to fill | [HAL_SPECIFICATION.md](../archive_docs2/HAL_SPECIFICATION.md), [KDRIVE_HAL_GUIDE.md](../archive_docs2/KDRIVE_HAL_GUIDE.md) |
| Cooperative scheduling (hosted) | **libucontext** wrapped behind `hal_task_*`; one OS thread, many ucontext tasks (HAL pump + TinyX server + one per internal client). Target uses Tima OS task primitives behind the same HAL. | Required by §2.8 (no pthread); matches single-thread cooperative model; concretizes "who runs server code on SyncHandle" | §5.1 |
| JWM | Stripped: no Cairo, Pango, Xrender, PNG/JPEG; XBM icons only; hard-coded config | Size + dependency budget | [README.md](../archive_docs2/README.md), [ROADMAP.md](../archive_docs2/ROADMAP.md) |
| Build system | CMake (top-level orchestration). TinyX autotools is **not** the production path. | Required by spec | [BASE_REQUIREMENTS.md](../archive_docs2/BASE_REQUIREMENTS.md) |
| Multi-core | Single-threaded baseline; dual-core (dedicated GUI core with WFI) is a *future* deployment option, not a current milestone | Keep scope tight | [MULTI_CORE_GUI_ANALYSIS.md](../archive_docs2/MULTI_CORE_GUI_ANALYSIS.md) |

---

## 4. Repository Layout

```
x11_window/
├── docs/
│   ├── active/                ← THIS doc (current source of truth)
│   ├── archive_docs/          ← original HAL/porting references (kept)
│   ├── archive_docs2/         ← prior planning + session summaries (historical)
│   └── library/               ← prior catch-ups + integration analyses (historical)
├── tinyx/                     ← TinyX/KDrive upstream-ish tree (small local deltas)
│   └── kdrive/tima/           ← OUR DDX/HAL adapter (5 files, untracked module)
├── libX11/                    ← libX11 upstream tree (currently unmodified)
├── X11/                       ← X11 protocol/header tree
├── jwm/                       ← JWM upstream (currently unmodified locally)
├── framework/                 ← Hosted (SDL) build target — the dev sandbox
│   ├── CMakeLists.txt         ← single hosted target `wmb_tima_build`
│   ├── config_tima.h, types.h
│   ├── sdl_drivers/           ← HAL backend over SDL (lcd, keyraw, mouse)
│   ├── sdl_library/           ← SDL window/input/mouse glue
│   ├── library/               ← heap/buffer helpers
│   └── wmb_x11/               ← wmb_main.c (currently empty), wmb_io.c, Xlibint_wmb.h
├── fonts/freetype/            ← future font path
├── build/, build_local/       ← out-of-tree CMake builds
```

### 4.1 Tima DDX/HAL adapter — `tinyx/kdrive/tima/`

| File | Role | Notes |
|---|---|---|
| `tima_kdrive.h` | Public types + `KdCardFuncs` / `KdKeyboardFuncs` / `KdMouseFuncs` declarations | Stable |
| `tima_kdrive_fb.c` | Framebuffer DDX (card/screen init, draw helpers, shadow setup) | 435 LOC |
| `tima_kdrive_kbd.c` | Keyboard driver (uses HAL `keypad_driver_*`) | 321 LOC |
| `tima_kdrive_mouse.c` | Mouse driver (uses HAL `mouse_driver_*`) | Known bugs — see §7 |
| `tima_kdrive_system.c` | `KdOsFuncs` + TinyX DDX entry points (`InitCard`, `InitOutput`, `InitInput`, `OsVendorInit`, `ddx*`, `AbortDDX`) | `tima_WaitForEvents()` **not yet implemented** |

This directory is **not** wired into `tinyx/kdrive/Makefile.am`. It is referenced only by the hosted CMake build.

### 4.2 Hosted framework — `framework/`

- Single target: `wmb_tima_build` (executable).
- Compiles SDL HAL backend + the 5 Tima DDX files only.
- Includes paths for TinyX KDrive/MI/FB/Xext/shadow/damage/render/randr/xfixes/os/dix headers.
- **Does not link TinyX core libraries yet** → currently fails at link with dozens of unresolved KDrive/DIX/shadow symbols.
- SDL is sourced from `/home/wifi/projects/SDL` (local checkout). A build-time `SDL2/SDL.h` shim is generated so source `#include "SDL2/SDL.h"` works.
- Compile defines: `_USE_SCREEN_WIDTH=800`, `_USE_SCREEN_HEIGHT=480`, `TIMA_USE_CONFIG`, `_USE_LINUX`, `PIXEL_TYPE_T=uint16_t`. `HAVE_CONFIG_H` is intentionally **off** (no `kdrive-config.h` generated yet).
- `framework/wmb_x11/wmb_main.c` is a stub `main()` that does nothing.

### 4.3 Local TinyX deltas (`git diff --stat` on `tinyx/`)

```
dix/devices.c       |  6 ++++++
kdrive/src/kdrive.c | 11 +++++++++--
kdrive/src/kmode.c  |  2 ++
os/log.c            |  2 ++
?? kdrive/tima/      (untracked, additive)
```
4 modified core files. They need to be **classified** (keep-with-rationale vs revert) before M1 closes, per the upstream-safe budget. Nothing in `dix/main.c`, `os/connection.c`, `os/io.c` yet.

### 4.4 JWM / libX11 local state

- `jwm/` and `libX11/` working trees are **clean** (no local modifications). Earlier session reports of "52 deleted JWM headers" were transient and have been resolved.

---

## 5. Architecture Diagram (Target)

```
┌────────────────────────────────────────────────────────────────────┐
│  External app                                                      │
│   one-line wrappers  ──► fixed-address entry table                 │
└──────────────────────────────────┬─────────────────────────────────┘
                                   │ (function calls; no socket)
┌──────────────────────────────────▼─────────────────────────────────┐
│  WMB binary (single image, non-PIC, fixed load address)            │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │ libX11-wmb (full Xlib API; transport replaced)               │  │
│  │   Xlib API files: UNCHANGED                                  │  │
│  │   xcb_io.c stubs: _XGetRequest / _XReply / _XSend / _XRead*  │  │
│  │   _XConnectXCB / _XFlush                                     │  │
│  │   Hook: dpy->synchandler = wmb_sync_handler                  │  │
│  │   Data() macro: resize dpy->buffer instead of _XSend         │  │
│  └─────────────────────────────┬────────────────────────────────┘  │
│                                │ direct call into ProcVector[op]   │
│  ┌─────────────────────────────▼────────────────────────────────┐  │
│  │ JWM (stripped) ── also an internal client of TinyX           │  │
│  └─────────────────────────────┬────────────────────────────────┘  │
│  ┌─────────────────────────────▼────────────────────────────────┐  │
│  │ TinyX (DIX + KDrive + MI + FB + Xext + shadow + render +     │  │
│  │        randr + xfixes + damage + os)                         │  │
│  │   Internal-client path: NextAvailableClient() + OsCommRec    │  │
│  │   Listener path: present in M1, disabled in M3               │  │
│  └─────────────────────────────┬────────────────────────────────┘  │
│  ┌─────────────────────────────▼────────────────────────────────┐  │
│  │ Tima DDX (tinyx/kdrive/tima/*)                               │  │
│  │   KdCardFuncs / KdKeyboardFuncs / KdMouseFuncs / KdOsFuncs   │  │
│  └─────────────────────────────┬────────────────────────────────┘  │
│  ┌─────────────────────────────▼────────────────────────────────┐  │
│  │ HAL (lcd_driver_*, keypad_driver_*, mouse_driver_*,          │  │
│  │      hal_yield_timeout, os_get_tick_ms, monotonic timer)     │  │
│  └─────────────────────────────┬────────────────────────────────┘  │
└────────────────────────────────┼───────────────────────────────────┘
                                 │
                  Hosted: SDL ◄──┴──► Target: Tima OS + framebuffer + input
```

---

## 5.1 Cooperative Multi-Application via libucontext (Hosted)

The hosted build needs to mimic Tima OS infrastructure **without using pthreads** (see §2.8). The chosen primitive is **[libucontext](https://github.com/kaniini/libucontext)** — a portable, dependency-free userland implementation of POSIX `ucontext.h` (`getcontext` / `makecontext` / `swapcontext` / `setcontext`). It gives us **cooperative stack switching** between independent execution flows in a single OS thread.

### 5.1.1 Why libucontext (not pthread, not setjmp/longjmp)

| Property | libucontext | pthread | setjmp/longjmp |
|---|---|---|---|
| Single OS thread | ✓ matches Tima target | ✗ | ✓ |
| Independent stacks per task | ✓ | ✓ | ✗ (single stack) |
| Deterministic switch points | ✓ explicit `swapcontext` | ✗ preemptive | ✓ |
| No locking/atomics needed | ✓ | ✗ | ✓ |
| Tima-OS portable shape | ✓ HAL maps to Tima context API | ✗ | partial |
| Builds anywhere we host | ✓ (portable, RISC-V/x86/ARM/MIPS) | depends on libc | ✓ |

libucontext on Linux replaces glibc's deprecated/optional `ucontext` implementation and is available for the same architectures we develop on. On the Tima target, the same HAL surface (`hal_task_*`, `hal_yield_timeout`) is implemented by Tima's own cooperative scheduler — **WMB code does not change** between hosted and target.

### 5.1.2 Execution Model — One Process, Many Cooperative "Apps"

WMB inside a single OS process / single hardware core looks like this:

```
   OS thread (single)
   ┌──────────────────────────────────────────────────────────────┐
   │                                                              │
   │  ┌──────────┐    swapcontext     ┌────────────┐              │
   │  │ HAL host │  ◄──────────────►  │ WMB main   │ (TinyX +     │
   │  │ pump     │  hal_yield_timeout │ event loop │  KDrive +    │
   │  │ (SDL)    │                    │            │  dispatcher) │
   │  └────┬─────┘                    └──────┬─────┘              │
   │       │                                 │ ServerClient       │
   │       │                                 │ dispatch ► Proc*   │
   │       │                                 ▼                    │
   │       │                          ┌────────────────────────┐  │
   │       │       swapcontext        │ Internal client tasks  │  │
   │       └──────────────────────────│  • JWM                 │  │
   │                                  │  • app A (hosted dev)  │  │
   │                                  │  • app B (hosted dev)  │  │
   │                                  └────────────────────────┘  │
   └──────────────────────────────────────────────────────────────┘
```

- **Exactly one OS thread.** No pthread. No preemption.
- **One ucontext per cooperative task.** Each task has its own stack (typ. 64-256 KB).
- **Task kinds:**
  1. `WMB_TASK_HAL` — pumps SDL events, advances the monotonic clock, polls HAL input (hosted only).
  2. `WMB_TASK_SERVER` — TinyX DIX dispatch loop; consumes requests from internal clients, emits replies/events.
  3. `WMB_TASK_CLIENT[i]` — one per internal client (JWM + any hosted test apps + any external one-liner-wrapped app exercised in hosted mode).
- **All switches are explicit** via the HAL: `hal_yield_timeout()`, `hal_task_yield()`, `hal_task_wait_for_event()`.

### 5.1.3 HAL Surface (extends §3 row "HAL boundary")

New cooperative-scheduling additions to the existing HAL (`lcd_driver_*`, `keypad_driver_*`, `mouse_driver_*`, `os_get_tick_ms`):

```c
/* hal_task.h — same surface, two implementations (libucontext, Tima) */

typedef struct hal_task hal_task_t;
typedef void (*hal_task_entry_t)(void *arg);

hal_task_t *hal_task_create(const char *name,
                            hal_task_entry_t entry,
                            void *arg,
                            size_t stack_bytes);

void  hal_task_destroy(hal_task_t *t);

/* Cooperative yield: returns once any of:
 *   - max_ms elapses, or
 *   - HAL input becomes available, or
 *   - the scheduler is asked to resume this task explicitly.
 * Already declared in §6/M2; libucontext is what makes it real on host. */
int   hal_yield_timeout(uint32_t max_ms);

/* Direct hand-off, used by request/reply path: */
void  hal_task_yield(void);                /* round-robin yield */
void  hal_task_wake(hal_task_t *t);        /* mark t runnable */
void  hal_task_wait(void);                 /* block this task until woken */
```

Implementation map:

| Symbol | Hosted impl | Target impl |
|---|---|---|
| `hal_task_create` | `mmap` + `getcontext`/`makecontext` (libucontext) | Tima OS task primitive |
| `hal_task_yield` | `swapcontext` to scheduler ctx, scheduler picks next | Tima yield |
| `hal_task_wait` / `hal_task_wake` | runqueue manipulation + `swapcontext` | Tima primitives |
| `hal_yield_timeout` | scheduler runs HAL pump + sleeps `min(remaining,1ms)` between rounds | Tima `WFI`-aware yield |
| `os_get_tick_ms` | `clock_gettime(CLOCK_MONOTONIC)` | Tima monotonic counter |

The scheduler itself is **~150-250 LOC** and lives in `framework/library/wmb_sched.c` (new in M2). It is **not** a general-purpose scheduler: round-robin over a ready list, one I/O pump task at highest priority, no priorities, no preemption.

### 5.1.4 Request / Reply Flow with ucontext (clarifies §3 "libX11 strategy")

Building on `XCB_STUB_ARCHITECTURE` (`dpy->synchandler` = `wmb_sync_handler`):

```
[Client task: JWM]                          [Server task: TinyX]
  XMapWindow(dpy, w)
   ├─ GetReq(...)                fills dpy->buffer
   ├─ UnlockDisplay              (no-op; single thread)
   └─ SyncHandle()
        └─ wmb_sync_handler(dpy)
             ├─ pkg = { client=this_client, buf=dpy->buffer, len }
             ├─ queue.push(pkg)
             ├─ hal_task_wake(WMB_TASK_SERVER)
             └─ hal_task_yield()                  ◄──── swapcontext
                                                  ────►  scheduler picks SERVER
                                                  pkg = queue.pop()
                                                  client->requestBuffer = pkg.buf
                                                  ProcVector[opcode](client)
                                                    ├─ may push events to other
                                                    │   clients (hal_task_wake them)
                                                    └─ writes reply to client's
                                                       OsCommRec via M2 hook
                                                  hal_task_yield()
                                                  ◄──── back to JWM
   return
```

- **Fire-and-forget** (`XMapWindow`): client yields, server runs, server yields back when done. One round-trip = two `swapcontext` calls.
- **Reply-bearing** (`XGetWindowAttributes`): same path, but the client task does `hal_task_wait()` after pushing the request; server, on completing the reply, calls `hal_task_wake(client_task)` and yields.
- **Async events** (input from HAL pump): HAL pump task wakes SERVER; SERVER walks event mask, deposits into each interested client's event queue and `hal_task_wake`s them.

This makes the "no socket, internal client" architecture concrete and removes the only fuzzy part of [XCB_STUB_ARCHITECTURE.md](../archive_docs2/XCB_STUB_ARCHITECTURE.md): *who runs the server code when a client makes a request?* Answer: **the server task does, after a `swapcontext` triggered by `SyncHandle()`.**

### 5.1.5 Multi-Application Topology

| Slot | Hosted dev role | Target role |
|---|---|---|
| `WMB_TASK_HAL` | SDL event pump + keyboard/mouse polling | HW IRQ → ring buffer |
| `WMB_TASK_SERVER` | TinyX DIX dispatch | same |
| `WMB_TASK_CLIENT[JWM]` | always present | always present |
| `WMB_TASK_CLIENT[app_1..N]` | hosted dev: spawned at init from a hard-coded test list, or via a thin "load app" entry on the fixed-address API | target: spawned by Tima OS when it loads an external app and calls into the WMB entry table |

External applications in production never share a stack with WMB internal code — they call **one-liner wrappers** (§2.3) that enter WMB through the fixed entry table. Inside WMB, that entry call is dispatched onto the calling task's existing context. The "task" that an external app runs on is **owned by Tima OS**, not by WMB. In hosted dev mode, libucontext stands in for Tima OS and creates one task per simulated app.

> **Concrete consequence:** external apps do not need to be ucontext-aware. ucontext is an *internal-to-WMB* mechanism on the host. On the target, Tima OS provides equivalent task semantics through the same HAL surface.

### 5.1.6 Stacks, Sizes, Limits

| Task | Hosted stack | Target stack |
|---|---|---|
| HAL pump | 64 KB | small (~16 KB) |
| Server | 256 KB | 128 KB |
| JWM client | 128 KB | 64 KB |
| Generic internal client | 64 KB default, override on create | 32-64 KB |

Stack allocation: `mmap(... PROT_READ|PROT_WRITE ...)` with a guard page below (host) — `mprotect(guard, PROT_NONE)` to catch overflow. On target, Tima OS owns guard pages.

### 5.1.7 Build Integration

- Vendor libucontext as a git submodule under `framework/third_party/libucontext/` (or system package if available; prefer vendored for reproducibility).
- CMake: add as `STATIC` library, link only into hosted target. Guard with `WMB_HOSTED` define.
- Target build path: define `WMB_TARGET_TIMA`; `hal_task_*` implementations come from Tima OS HAL, libucontext is **not** linked.
- The same `hal_task.h` header is included by WMB code in both profiles.

### 5.1.8 Why this satisfies the §2 requirements

- §2.2 "no socket": all client→server traffic is in-process `swapcontext` hand-offs.
- §2.4 "fixed-address entry point": orthogonal — the entry table is a vtable, ucontext is the runtime substrate that the entry-point implementations run on.
- §2.8 "context-switch library (not pthread)": libucontext is exactly that.
- §2.9 "WMB OS-agnostic except via HAL": ucontext usage is wrapped behind `hal_task_*`, never called directly from TinyX/JWM/libX11-wmb.

### 5.1.9 Risks specific to ucontext

| Risk | Mitigation |
|---|---|
| Stack overflow inside JWM/TinyX with too-small default stacks | Guard pages; size table above; revisit after first crash-free smoke run. |
| `swapcontext` is slow on some libc implementations | Vendored libucontext avoids glibc's variant; switch cost ~100-300 ns is fine for our rates. |
| Signal handling inside ucontext tasks | Only the HAL pump task installs signal handlers; client tasks must not. |
| Debugger confusion (gdb stack unwinding across contexts) | Document; `gdb` since 8.0 handles ucontext switches; add a `.gdbinit` helper later. |
| Drift between hosted ucontext semantics and Tima task semantics | `hal_task_*` is the only contract; write a conformance smoke test runnable on both. |

---

## 6. Milestones (Canonical)

This is the agreed sequence. Do not invent parallel milestone schemes.

### M0 — Baseline triage (must close before anything else)

- Classify the 4 modified TinyX core files (`dix/devices.c`, `kdrive/src/kdrive.c`, `kdrive/src/kmode.c`, `os/log.c`) as keep-with-rationale or revert.
- Confirm SDL is available at `/home/wifi/projects/SDL` (or update CMake to find a system SDL2).
- Verify `jwm/` and `libX11/` working trees stay clean.
- Update §7 of this doc with any newly discovered blockers.

### M1 — Whole TinyX hosted baseline

- CMake assembles TinyX as module groups (mirror `KDRIVE_LIBS`).
- Generate `dix-config.h` / `kdrive-config.h` for the hosted profile; re-enable `HAVE_CONFIG_H`.
- All unresolved KDrive/DIX/shadow symbols gone.
- Listener/socket path **left enabled** (no standalone enforcement yet).
- Fix Tima mouse adapter bugs (§7.3).
- Decide entry point: TinyX DIX `main` vs wrapper `main`. Avoid duplicate `main` ownership. Replace empty `framework/wmb_x11/wmb_main.c`.
- **DoD**: hosted target links; server loop reached; SDL window + keyboard + mouse path works end-to-end.
- **Patch budget**: 0 modified TinyX core files preferred (additive only).

### M2 — Stubbed internal client path (socketless WMB clients)

- Implement libX11 transport stubs per [XCB_STUB_ARCHITECTURE.md](../archive_docs2/XCB_STUB_ARCHITECTURE.md):
  `_XConnectXCB`, `_XGetRequest`, `_XReply`, `_XRead*`, `_XReadEvents`, `_XSend`, `_XFlush`.
- Install `wmb_sync_handler` during `XOpenDisplay`.
- Modify the `Data()` macro in `X11/Xlibint.h` (single targeted change) to resize `dpy->buffer` instead of calling `_XSend`.
- TinyX side: additive internal-client registration in `tinyx/os/*`; at most one guarded hook in `tinyx/os/io.c` (`WriteToClient`) for internal-client output.
- Implement `tima_WaitForEvents()` cooperatively using `hal_yield_timeout()`.
- **DoD**: `XOpenDisplay` (stub path) returns valid `Display*`; create/map/draw/event-loop smoke runs entirely in-process.
- **Patch budget**: ≤ 1 modified upstream TinyX file (hard cap 2). Everything behind `WMB_INTERNAL_CLIENT_IO` guard.

### M3 — Strict standalone (zero listeners)

- Introduce `WMB_STANDALONE_NO_LISTEN` build/runtime guard.
- Single guarded edit in `tinyx/os/connection.c` to relax the `CreateWellKnownSockets()` fatal path when in standalone mode.
- Disable XDMCP / remote-listener-dependent paths in that profile.
- **DoD**: server boots with zero listener sockets; internal clients still work; no listener-related fatal errors.
- **Patch budget total (M2+M3)**: ≤ 2 preferred, ≤ 3 maximum modified upstream TinyX files.
- Deliver TinyX patch manifest + ordered `.patch` files + replay verification on a fresh upstream checkout.

### M4 — JWM integration

- Strip JWM dependencies (no Cairo/Pango/Xrender/PNG/JPEG).
- Hard-code JWM config.
- JWM runs as an internal client over the M2 stub transport.
- **DoD**: multiple decorated windows; move/resize; root menu works.

### M5 — Fixed-address entry-point API + external one-liner wrappers

- Define the external API entry table layout and ABI.
- Produce the redistributable one-liner wrapper library.
- **DoD**: an external test app links *only* against the wrapper lib and runs against a loaded WMB image.

### M6 — Target (Tima OS) port

- Replace SDL HAL with Tima HAL drivers.
- Provide real `hal_yield_timeout`, `os_get_tick_ms`, monotonic timer, allocator policy (per [OS_LIBC_REQUIREMENTS.md](../archive_docs2/OS_LIBC_REQUIREMENTS.md)).
- Single-thread baseline. Dual-core/WFI deployment is post-M6 if/when needed.

> Multi-core dedicated-GUI-core deployment is intentionally **not** an M0-M6 milestone. It is a future deployment variant only.

---

## 7. Known Issues / Gaps (current truth)

### 7.1 Hosted build (blockers)
- **Link fails**: TinyX core not linked. Missing symbols include `shadowSetup`, `shadowUpdatePacked`, `KdShadowSet`, `kdScreenPrivateIndex`, `kdKeymap`, `KdEnqueueKeyboardEvent`, `kdMouseInfo`, `KdEnqueueMouseEvent`, `KdCardInfoAdd`, `KdInitOutput`, `KdInitInput`, `KdOsInit`, `FatalError`.
  Fix: M1 — bring in TinyX module groups.
- `framework/sdl_library/sdl_window.c` (historic report) had a duplicate `sdl_send_mouse_wheel_event` definition. Re-verify before M1 closes.
- `framework/wmb_x11/wmb_main.c` is a 6-line empty `main()`. Needs an explicit entry decision in M1.
- `HAVE_CONFIG_H` is intentionally off. Will be re-enabled in M1 phase B once `dix-config.h` / `kdrive-config.h` are generated.

### 7.2 Architecture gaps (documented, not implemented)
- libX11 transport stubs: nothing in workspace yet.
- `_XBufferOverflow` (called by the patched `Data()` macro) not yet written.
- Internal-client registration helper in `tinyx/os/*`: not yet written.
- `tima_WaitForEvents()` cooperative wait: not implemented in `tima_kdrive_system.c`.
- Fixed-address API entry table: design exists at a high level only.

### 7.3 Tima DDX correctness bugs (must fix in M1)
- `tima_MouseInit` does not call `mouse_driver_init(0)`.
- Mouse poll uses `dx`, `dy`, `flags` without safe initialization.
- Mouse packet handling can partially process events due to `continue` / `break` flow.
- Wheel input is read from HAL but not delivered to KDrive event level.

### 7.4 Patch/upstream hygiene
- 4 modified TinyX core files are unclassified. Until classified, every milestone gate fails on the upstream-safe rule.

---

## 8. Sizing & Memory Budget (planning targets, not measured)

| Component | Code | Notes |
|---|---|---|
| HAL | ~20 KB | trivial |
| TinyX core (DIX + KDrive + MI + FB + ext set) | ~150 KB | bulk of binary |
| libX11-wmb | ~50 KB | full API but no XCB |
| JWM (stripped) | ~60 KB | XBM only |
| Fonts (bitmap or freetype subset) | ~40 KB | TBD |
| libc (newlib-nano profile) | ~100 KB | per OS_LIBC_REQUIREMENTS |
| **Total** | **~420 KB** | tunable |

Runtime: ~32-64 MB RAM target (heap 16-32 MB, FB ~3 MB at 1024x768x32, stack 1 MB).

---

## 9. Reference Map (where to look when you actually need detail)

Use this instead of re-reading the whole archive.

| Need | Read |
|---|---|
| Exact original requirements (verbatim) | [BASE_REQUIREMENTS.md](../archive_docs2/BASE_REQUIREMENTS.md) |
| Why TinyX (not GEM-XServer) | [X_SERVER_COMPARISON.md](../archive_docs2/X_SERVER_COMPARISON.md), [TINYX_MIGRATION.md](../archive_docs2/TINYX_MIGRATION.md) |
| libX11 stub design (THE implementation plan) | [XCB_STUB_ARCHITECTURE.md](../archive_docs2/XCB_STUB_ARCHITECTURE.md) |
| `XOpenDisplay` internals + XCB dependency points | [XOPENDISPLAY_ANALYSIS.md](../archive_docs2/XOPENDISPLAY_ANALYSIS.md), [XCB_DEPENDENCY_RISK_ASSESSMENT.md](../archive_docs2/XCB_DEPENDENCY_RISK_ASSESSMENT.md) |
| Broader (now superseded) custom-xlib plan | [CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md) |
| HAL function signatures | [HAL_SPECIFICATION.md](../archive_docs2/HAL_SPECIFICATION.md), [KDRIVE_HAL_GUIDE.md](../archive_docs2/KDRIVE_HAL_GUIDE.md) |
| KDrive reference implementation walk-through | [KDRIVE_IMPLEMENTATION_REFERENCE.md](../archive_docs2/KDRIVE_IMPLEMENTATION_REFERENCE.md) |
| Cooperative event-loop rationale | [SESSION_SUMMARY_2026-01-25.md](../archive_docs2/SESSION_SUMMARY_2026-01-25.md), [MULTI_CORE_GUI_ANALYSIS.md](../archive_docs2/MULTI_CORE_GUI_ANALYSIS.md) |
| OS/libc expectations | [OS_LIBC_REQUIREMENTS.md](../archive_docs2/OS_LIBC_REQUIREMENTS.md) |
| Build / dependency strategy (legacy long form) | [BUILD_PLAN.md](../archive_docs2/BUILD_PLAN.md) |
| TinyX preprocessor define landscape | [TINYX_PREPROCESSOR_DEFINITIONS.md](../archive_docs2/TINYX_PREPROCESSOR_DEFINITIONS.md) |
| Whole-stack + stub feasibility analysis | [library/TINYX_WHOLE_STACK_STUB_STANDALONE_ANALYSIS_2026-03-10.md](../library/TINYX_WHOLE_STACK_STUB_STANDALONE_ANALYSIS_2026-03-10.md) |
| Upstream-safe patch policy + M1/M2/M3 detail | [library/TINYX_STANDALONE_STUB_MILESTONES_UPSTREAM_SAFE_2026-03-10.md](../library/TINYX_STANDALONE_STUB_MILESTONES_UPSTREAM_SAFE_2026-03-10.md) |
| Tima DDX integration gap analysis | [library/TINYX_TIMA_INTEGRATION_NEXT_STEPS_2026-03-10.md](../library/TINYX_TIMA_INTEGRATION_NEXT_STEPS_2026-03-10.md) |
| Code review of current Tima DDX | [TIMA_KDRIVE_CODE_REVIEW.md](../archive_docs2/TIMA_KDRIVE_CODE_REVIEW.md) |
| Original system-level architecture write-up | [ARCHITECTURE_ANALYSIS.md](../archive_docs2/ARCHITECTURE_ANALYSIS.md) |

Documents marked **OUTDATED** in [DOCUMENTATION_INDEX.md](../archive_docs2/DOCUMENTATION_INDEX.md) (notably [VDI_HAL_MAPPING.md](../archive_docs2/VDI_HAL_MAPPING.md), GEM-era material): historical only.

---

## 10. Working Rules (enforce on every change)

1. **No new catch-up docs.** Update this file in place. Add a dated entry to §11.
2. **No new "analysis" docs** unless they answer a question §9 does not cover. If you write one, link it from §9 and summarise its conclusion here.
3. **Adapter-first** on TinyX: every change starts as additive in `tinyx/kdrive/tima/*` or `framework/*` or `libX11/src/*` *new* files. Only escalate to an in-place TinyX/libX11 core edit if no adapter path exists, and only behind a feature macro.
4. **Patch budget gates** are real: M1 = 0 preferred TinyX core edits, M2 ≤ 1, M2+M3 ≤ 2 (hard cap 3). Exceeding it blocks the milestone.
5. **Single hosted target first.** Don't start M2 work until M1 links + runs.
6. **One entry point.** Decide between TinyX-DIX `main` and wrapper `main` once, in M1.
7. **Hosted-only assumptions stay behind `_USE_LINUX` / SDL conditional compilation.** Target build must not pull SDL.
8. **Re-validate this file** at the start of each work session: §3, §6, §7. If anything has drifted, fix here before coding.

---

## 11. Change Log (update each work session)

- **2026-05-19** — Added §5.1 "Cooperative Multi-Application via libucontext (Hosted)". Selected libucontext as the §2.8 context-switch primitive; defined `hal_task_*` HAL surface used by both hosted and Tima target; mapped the SyncHandle-based request/reply flow onto explicit `swapcontext` hand-offs; documented task topology, stack sizing, build integration, and risks. Added matching row in §3 decision table.

- **2026-05-19** — Initial consolidation. Folded `PROJECT_CATCHUP_2026-03-09`, `TINYX_WHOLE_STACK_STUB_STANDALONE_ANALYSIS_2026-03-10`, `TINYX_STANDALONE_STUB_MILESTONES_UPSTREAM_SAFE_2026-03-10`, `TINYX_TIMA_INTEGRATION_NEXT_STEPS_2026-03-10`, session summaries (01-22 / 01-24 / 01-25), `BASE_REQUIREMENTS`, `DOCUMENTATION_INDEX`, `XCB_STUB_ARCHITECTURE`, `README`, `ROADMAP` into this single entry point. Verified current code state: TinyX has 4 local core diffs + untracked `kdrive/tima/`, JWM/libX11 clean, framework hosted build fails at link (TinyX core not assembled).
