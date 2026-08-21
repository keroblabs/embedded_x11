# Integrating JWM into the Single‑Binary WMB

**Last updated**: 2026-07-13
**Scope**: How to compile JWM into the same binary as HAL, Kdrive, fiber
scheduler, TinyX and pseudo‑libX11, without symbol/header conflicts.
Covers isolation, entry‑point rewriting, threading model, and how loaded
apps interact with the window manager through the fiber runtime.

**Related docs**:
- [KDRIVE_CREATION_GUIDE.md](KDRIVE_CREATION_GUIDE.md) — display/input HAL.
- [TINYX_OS_PORTING_GUIDE.md](TINYX_OS_PORTING_GUIDE.md) — libc/syscall layer.
- [CLIENT_SERVER_TRANSPORT_REMOVAL.md](CLIENT_SERVER_TRANSPORT_REMOVAL.md) —
  in‑process libX11 via the `wmb_api_t` vtable.
- [FIBER_PORTABLE_ARCHITECTURE.md](FIBER_PORTABLE_ARCHITECTURE.md) —
  cooperative scheduler and `hal_task_*` surface. **This document depends
  on that scheduler being in place.**

---

## Table of Contents

1. [The Target Binary Layout](#1-the-target-binary-layout)
2. [The Symbol‑Conflict Problem](#2-the-symbolconflict-problem)
3. [Isolation Strategy — Rename, Namespace, Redirect](#3-isolation-strategy--rename-namespace-redirect)
4. [JWM Header Surgery](#4-jwm-header-surgery)
5. [JWM as a Fiber, Not a Process](#5-jwm-as-a-fiber-not-a-process)
6. [Handling `fork` / `execl` in `command.c`](#6-handling-fork--execl-in-commandc)
7. [Handling `select()` in `event.c`](#7-handling-select-in-eventc)
8. [Timing, Signals, Environment](#8-timing-signals-environment)
9. [Optional Dependencies (PNG, XFT, iconv, gettext, …)](#9-optional-dependencies-png-xft-iconv-gettext-)
10. [Application Loading & Wiring](#10-application-loading--wiring)
11. [The Complete Boot Sequence](#11-the-complete-boot-sequence)
12. [Build System — Single CMake Target](#12-build-system--single-cmake-target)
13. [Implementation Roadmap](#13-implementation-roadmap)
14. [Verification Checklist](#14-verification-checklist)

---

## 1. The Target Binary Layout

Everything that runs on the target ships as one ELF, statically linked, and
is laid out so that every component sees the address of every other
component at link time:

```
 +---------------------------------------------------------------------+
 |                  WMB single binary (wmb.elf)                        |
 |                                                                     |
 |   .wmb_api section  -> wmb_api_t vtable @ 0x80100000                |
 |     |  XOpenDisplay, XCreateWindow, XMapWindow, ... (see            |
 |     |  CLIENT_SERVER_TRANSPORT_REMOVAL.md §5)                       |
 |                                                                     |
 |   Fibers scheduled by framework/fiber/ (hal_task_* surface)         |
 |   +---------------------------------------------------------------+ |
 |   |  fiber: HAL pump      | drains lcd/keypad/mouse HAL, ticks    | |
 |   |  fiber: X server      | TinyX Dispatch loop + block/wake      | |
 |   |  fiber: JWM           | JWM's EventLoop() (rewritten)         | |
 |   |  fiber: app 1..N      | user apps, each with its own Display  | |
 |   +---------------------------------------------------------------+ |
 |                                                                     |
 |   Static libs linked in:                                            |
 |     libhal           (SDL or RISC-V driver impls of hal_drivers.h)  |
 |     libkdrive_tima   (tinyx/kdrive/tima/*.c)                        |
 |     libtinyx_core    (tinyx/{dix,mi,fb,Xext,render,randr,...})      |
 |     libtinyx_os_min  (log.c, utils.c, osinit.c, oscolor.c,          |
 |                       strlcpy.c, strlcat.c, wmb_waitfor.c)          |
 |     libX11_wmb       (libX11/src/*.c minus xcb_io.c/xcb_conn.c/     |
 |                       xcb_xlib.c, plus wmb_transport.c,             |
 |                       wmb_open.c, wmb_close.c, wmb_api_table.c)     |
 |     libfiber         (framework/fiber/ + arch/<host|tima>)          |
 |     libjwm           (jwm/src/*.c, patched — see §3–§8)             |
 |     libapps          (in-tree demo apps; optional)                  |
 |     libc             (newlib or picolibc)                           |
 +---------------------------------------------------------------------+
```

No dynamic loader. No `.so` files. No `dlopen`. External apps loaded at
runtime (§10) are position‑dependent ELFs built with a matching
[libX11-app](CLIENT_SERVER_TRANSPORT_REMOVAL.md#4-twoblock-design-app-wrapper--wmb-vtable)
static archive.

---

## 2. The Symbol‑Conflict Problem

JWM was written as a standalone X client executable. Linking it into the
same object as TinyX (a server) and libX11 (a client library) exposes
several classes of clash:

### 2.1 Duplicate `main()`

- `jwm/src/main.c` defines `int main(int, char**)`.
- `tinyx/kdrive/src/kdrive.c` (via DIX) also defines `main()`.
- Every demo application will also define `main()`.

Only one `main` can survive the final link.

### 2.2 Duplicate global variables

`jwm/src/main.c` exports:

| Symbol             | Also defined by                                |
|--------------------|------------------------------------------------|
| `display`          | Every X client (name pattern in libX11 demos)  |
| `rootWindow`, `rootWidth`, `rootHeight`, `rootScreen`, `rootDepth` | any X client that uses the same idiom |
| `initializing`, `shouldExit`, `shouldRestart`, `shouldReload` | any app with lifecycle globals |
| `currentDesktop`, `previousDesktop` | pager/desktop apps                |
| `clientContext`, `frameContext`     | conflicts with client code that uses `XUniqueContext()` |

### 2.3 Duplicate helpers

`Allocate`, `Reallocate`, `Release`, `CopyString`, `Warning`, `FatalError`,
`GetCurrentTime`, `GetTimeDifference`, `Signal`, `EventLoop`, `Initialize`,
`Startup`, `Shutdown` — all lowercase generic names in the JWM namespace
that will clash with equally generic names in apps or TinyX.

### 2.4 Header pollution

`jwm/src/jwm.h` pulls in `<png.h>`, `<X11/Xft/Xft.h>`, `<X11/Xatom.h>`,
`<sys/wait.h>`, `<sys/select.h>`, `<signal.h>` — the exact POSIX/Linux
headers we've been striving to keep out of the target build (see
[TINYX_OS_PORTING_GUIDE.md §13](TINYX_OS_PORTING_GUIDE.md#13-removing-linux-dependencies--filebyfile)).

### 2.5 Duplicate config header

Both JWM (`jwm/config.h` generated by autotools) and TinyX
(`kdrive-config.h`) define `HAVE_*`, `PACKAGE_*`, etc. A blind
`-I` order can pick up the wrong one.

---

## 3. Isolation Strategy — Rename, Namespace, Redirect

Three techniques, applied in this order:

### 3.1 Namespace the JWM binary

Compile `libjwm.a` with a private header prefix and enforce every
identifier to be prefixed. Concretely:

- **Rename `main` → `Jwm_Main`.** Only touch `jwm/src/main.c`. Change one
  line; forward declaration exposed via a new `jwm_entry.h`.
- **Wrap generic globals in a struct.** Introduce `jwm_state_t` holding
  `display`, `rootWindow`, `rootWidth`, `rootHeight`, `rootScreen`,
  `rootColormap`, `rootVisual`, `rootDepth`, `rootGC`, `currentDesktop`,
  `previousDesktop`, `shouldExit`, `shouldRestart`, `shouldReload`,
  `initializing`, `clientContext`, `frameContext`. Access via
  `JwmState.display`, etc.
- **Prefix internal helpers.** `Allocate` → `Jwm_Allocate`; `Warning` →
  `Jwm_Warning`; `EventLoop` → `Jwm_EventLoop`. Use a sed script + one
  compile pass to catch stragglers.
- **Give JWM its own `config.h`** at `jwm/wmb_config.h` (produced by CMake
  as part of the WMB build), keep it out of the global include path, and
  add `-include jwm/wmb_config.h` **only** for `libjwm.a` sources via
  `target_compile_options`.

Where possible use `--wrap` or an internal `jwm/jwm_namespace.h` with
`#define`s so upstream JWM source stays legible.

### 3.2 Redirect X calls through the vtable

JWM already funnels every X call through `jxlib.h`:

```c
#define JXOpenDisplay(a)              JFUNC1(XOpenDisplay, a)
#define JXCreateSimpleWindow(...)     JFUNC9(XCreateSimpleWindow, ...)
/* ... 200 more ... */
```

Under `UNIT_TEST`, `JFUNCn` is redefined to `Mock_<name>()`. **We reuse
this exact hook for WMB**:

```c
/* jxlib.h — new profile block */
#elif defined(WMB_INPROC)
#  define JFUNC1(name, a)              (WMB_API->name(a))
#  define JFUNC2(name, a, b)           (WMB_API->name(a, b))
#  define JFUNC3(name, a, b, c)        (WMB_API->name(a, b, c))
/* ... etc ... */
```

Result: every `JX*` call in JWM becomes a call through the fixed‑address
`wmb_api_t`. No JWM source changes needed for the X binding.

Any raw (non‑`JX*`) X call in JWM (there are a few in `event.c`,
`hint.c`, `client.c`) must be rewritten to use its `JX*` counterpart —
a small mechanical pass.

### 3.3 Move `Xlib.h` inclusion behind a shim

`jwm/src/jwm.h` currently does `#include <X11/Xlib.h>`. That still works
in the WMB build because `X11/Xlib.h` is unmodified — it defines types
(`Display`, `Window`, `GC`, `XEvent`, …). The **function prototypes** it
declares are never actually called by JWM (all calls go through
`jxlib.h`), so no symbol clash arises.

However, the header also transitively drags in `<sys/select.h>` on some
platforms. Fix by adding, before including Xlib:

```c
#define X_NOT_STDC_ENV       /* keep Xlib from pulling POSIX headers */
#define X_NOT_POSIX
```

or, safer, provide a tiny `X11/Xlib_wmb.h` that includes just the type
definitions we need and shadow the real header for the JWM compile unit
only.

---

## 4. JWM Header Surgery

Concrete edits to `jwm/src/jwm.h`:

| Current include                    | WMB action                                   |
|------------------------------------|----------------------------------------------|
| `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<limits.h>` | Keep — newlib provides |
| `<setjmp.h>`                       | Keep                                         |
| `<locale.h>`, `<libintl.h>`        | **Disable** via `-UENABLE_NLS -UENABLE_ICONV` |
| `<stdarg.h>`                       | Keep                                         |
| `<signal.h>`                       | **Remove** — `-UHAVE_SIGNAL_H`               |
| `<unistd.h>`                       | Keep                                         |
| `<time.h>`, `<sys/time.h>`         | Keep — needed for `struct timeval`           |
| `<sys/wait.h>`                     | **Remove** — `-UHAVE_SYS_WAIT_H`             |
| `<sys/select.h>`                   | **Remove** — `-UHAVE_SYS_SELECT_H`           |
| `<X11/Xlib.h>` and Xlib helpers    | Keep (types only)                            |
| `<X11/extensions/shape.h>`         | **Disable** — `-UUSE_SHAPE` for the target   |
| `<X11/Xft/Xft.h>`                  | **Disable** — `-UUSE_XFT`                    |
| `<X11/extensions/Xinerama.h>`      | **Disable** — `-UUSE_XINERAMA`               |
| `<X11/extensions/Xrender.h>`       | Keep only if you ship Render extension       |
| `<png.h>`, `<jpeg.h>`              | **Disable** — `-UUSE_PNG -UUSE_JPEG`         |
| `<fribidi/fribidi.h>`              | **Disable** — `-UUSE_FRIBIDI`                |

All of these are already guarded by `HAVE_*` / `USE_*` macros in
`jwm/src/jwm.h`. We simply do not define them for the WMB build. That
alone removes ~60% of JWM's optional footprint.

---

## 5. JWM as a Fiber, Not a Process

JWM is not exec'd; it is scheduled. After the WMB boot code has brought
up the HAL, Kdrive, and TinyX server fibers, it does:

```c
JwmState.dpy_name = NULL;             /* default display */
JwmState.config_path = "/etc/jwmrc";  /* or NULL — JWM uses built-ins */
hal_task_t *jwm = hal_task_create("jwm",
                                  Jwm_FiberEntry,
                                  &JwmState,
                                  128 * 1024 /* stack */);
```

Where `Jwm_FiberEntry` is a thin adapter:

```c
/* jwm/src/jwm_entry.c */
#include "jwm_entry.h"
#include "main.h"

void Jwm_FiberEntry(void *arg)
{
    (void)arg;
    /* JWM's original main() was renamed to Jwm_Main() during §3.1. */
    static char *fake_argv[] = { "jwm", NULL };
    Jwm_Main(1, fake_argv);
    /* If Jwm_Main returns, tear down the fiber cleanly. */
    hal_task_destroy(hal_task_self());
}
```

### 5.1 Cooperative points

JWM already has one natural blocking point — `WaitForEvent()` in
`event.c` (see the disassembly around line 118 which calls `select()`).
That's where cooperation must land. The rewrite is:

```c
char WaitForEvent(XEvent *event)
{
    long sleep_ms = compute_next_callback_delay();

    while (JXPending(JwmState.display) == 0) {
        /* Yield to other fibers, up to sleep_ms; return early if woken.  */
        int woken = hal_yield_timeout((uint32_t)sleep_ms);
        Jwm_Signal();                         /* run periodic callbacks   */
        if (!woken && sleep_ms > 0) {
            /* Timeout expired — recompute deadline and keep waiting.     */
            sleep_ms = compute_next_callback_delay();
        }
    }
    JXNextEvent(JwmState.display, event);
    return 1;
}
```

This replaces the `select(ConnectionNumber(display), …)` idiom
(`event.c:118‑132`). No fd, no signal — pure fiber yield.

The wakeup path is:

- **New input events** arrive via the Kdrive HAL pump, which calls
  `KdEnqueue*Event` → `WriteEventsToClient` → `_XEnq(dpy, …)` (see
  [CLIENT_SERVER_TRANSPORT_REMOVAL.md §8](CLIENT_SERVER_TRANSPORT_REMOVAL.md#8-event-delivery-path-server--client)).
  After enqueue, the wmb bridge calls `hal_task_wake(dpy->owner_fiber)`.
- JWM's `owner_fiber` is set to itself at `XOpenDisplay` time (stored
  inside `WmbDpy`).

That single line — `hal_task_wake(dpy->owner_fiber)` after any event
enqueue — is what makes the cooperative model responsive.

---

## 6. Handling `fork` / `execl` in `command.c`

`jwm/src/command.c` runs three kinds of external commands: startup
commands, shutdown commands, and pipe‑commands used by menus / include.

On a bare‑metal target there is no `fork`, no shell, and no separate
address space to launch into. The replacement is:

**Register in‑process apps by name.** Startup / restart command strings
map to a **command table** compiled into the WMB:

```c
/* framework/wmb_bridge/wmb_commands.c */
typedef void (*wmb_app_entry_t)(int argc, char **argv);

typedef struct {
    const char *name;              /* e.g. "xterm-lite", "clock", "menu" */
    wmb_app_entry_t entry;
    size_t stack_bytes;
} wmb_app_t;

extern const wmb_app_t wmb_app_table[];
extern const size_t    wmb_app_table_size;

void Jwm_RunCommand(const char *command);   /* replaces RunCommand() */
```

`Jwm_RunCommand` tokenises the JWM config string, looks up the first
token in `wmb_app_table`, and, on hit, calls `hal_task_create` to spawn
that entry point as a new fiber. On miss it logs a warning; no fallback
to `execl`.

Effect on `command.c`:

- `RunCommand()`, `RunCommandNoReplace()` — reduced to `Jwm_RunCommand()`.
- `ReadFromPipe()` (used for XML `<Include>`s and menu piping) — either
  (a) compile the config file into a C string constant and eliminate
  pipe reads, or (b) replace with a synchronous in‑process reader that
  invokes the target app entry and captures its output via a
  callback registered on `stdout`. Option (a) is simpler.

All `fork` / `execl` / `waitpid` / `pipe` / `select` in `command.c` are
removed from the target build.

---

## 7. Handling `select()` in `event.c`

See §5.1 above. `select` and `sleepTime`/`timeout` are replaced by
`hal_yield_timeout`. Because there is no fd, there is no
`ConnectionNumber(display)` any more; JWM must just ask its `Display`
whether events are pending via `JXPending`, which under WMB reads
`dpy->head != dpy->tail` — no syscall.

---

## 8. Timing, Signals, Environment

### 8.1 Timing

`jwm/src/timing.c` uses `gettimeofday`, `time`, `strftime`, `localtime`,
`tzset`, `getenv/putenv("TZ")`. Actions:

- Keep `gettimeofday` — already in Tier 1 of the porting guide.
- Keep `time`, `localtime`, `strftime` — provided by newlib. Newlib's
  `localtime` reads `TZ` from `getenv` at first call; if `getenv`
  returns NULL, it defaults to UTC. That's acceptable for embedded.
- **Delete** the `TZ` push/pop in `GetTimeString()` (lines 66‑84). Have
  it always render in local (== system) time. Removes `getenv`,
  `putenv`, `unsetenv`, `tzset` from the build.

### 8.2 Signals

`jwm/src/main.c` (~line 395) installs `sigaction` handlers for
`SIGHUP`/`SIGINT`/`SIGTERM`/`SIGPIPE`/`SIGCHLD`. The WMB has neither
signals nor children. Wrap the whole block in `#ifndef WMB_INPROC` and
provide alternate shutdown paths:

- **Shutdown request**: `Jwm_RequestExit()` — public function callable
  from the HAL Ctrl+C shortcut or from any other fiber.
- **Restart / reload**: same pattern; expose functions the launcher can
  call.

Delete `HandleChild()` — no children exist.

### 8.3 Environment

Only two `getenv` calls remain outside `timing.c`:
`jwm/src/misc.c:164` (variable expansion in config paths) and
`jwm/src/command.c:137` (writing `DISPLAY=…` before exec). Both
disappear once `-UENABLE_NLS` is set and `fork/execl` is removed. A
`getenv → return NULL` stub is enough for anything that slips through.

---

## 9. Optional Dependencies (PNG, XFT, iconv, gettext, …)

The WMB build turns **all** of these off:

| Feature       | Macro          | Reason                                                     |
|---------------|----------------|------------------------------------------------------------|
| PNG icons     | `USE_PNG`      | pulls libpng + zlib                                        |
| JPEG icons    | `USE_JPEG`     | pulls libjpeg                                              |
| XPM icons     | `USE_XPM`      | pulls libXpm                                               |
| XFT fonts     | `USE_XFT`      | pulls libXft + libfontconfig + freetype                    |
| XRender       | `USE_XRENDER`  | *keep only* if TinyX built with Render extension            |
| Xinerama      | `USE_XINERAMA` | multi‑head irrelevant                                       |
| Shape         | `USE_SHAPE`    | pulls libXext extension; JWM works without it              |
| iconv         | `USE_ICONV`    | already gated in `font.c`                                  |
| gettext (NLS) | `ENABLE_NLS`   | pulls libintl; ship English strings only                    |
| FriBiDi       | `USE_FRIBIDI`  | RTL text; not needed                                        |
| RSVG          | `USE_RSVG`     | pulls librsvg + cairo + glib                                |

JWM boots and manages windows fine with all of the above disabled — the
`configure.ac` explicitly makes each optional. In CMake:

```cmake
target_compile_definitions(jwm PRIVATE
    JWM_MINIMAL=1
    # deliberately not defining any USE_* / ENABLE_NLS
)
```

Font support degrades to X11 core fonts (BDF/PCF) provided by TinyX's
built‑in `fixed` font — good enough for a bring‑up UI.

---

## 10. Application Loading & Wiring

An "application" in the WMB context is any fiber that opens a `Display`
via `WMB_API->XOpenDisplay(NULL)` and drives an X event loop.

Two flavours:

### 10.1 In‑tree apps (compiled into the WMB)

Simplest. Add a `wmb_app_t` entry to `wmb_app_table[]` pointing to the
app's `argc/argv` entry point. Launch by name from JWM's config
(`<StartupCommand>xterm-lite</StartupCommand>`) or programmatically.

Each app fiber gets its own stack, its own `Display*` (allocated by
`XOpenDisplay`), and its own `WmbClientRec` on the TinyX side. Events
flow via the mechanism described in
[CLIENT_SERVER_TRANSPORT_REMOVAL.md §8](CLIENT_SERVER_TRANSPORT_REMOVAL.md#8-event-delivery-path-server--client);
requests via the `SyncHandle` bridge.

### 10.2 Out‑of‑tree apps (loaded at runtime)

If Tima OS supports loading additional ELFs into the same address space
(the "loaded binary" wording in the request suggests so), an app is
built against `libX11-app.a` and linked with a base address chosen at
load time.

At load time:

1. The loader maps the app text/rodata/data/bss.
2. The loader resolves the app's imports of `WMB_API` — since apps
   read the vtable at a fixed absolute address (`WMB_API_ADDRESS`),
   there is nothing to relocate.
3. The loader calls the app's entry function on a new fiber:
   `hal_task_create(app_name, app_entry, argv, stack_bytes)`.

That's the entire "linked via the fiber infra" story: apps discover
services through one immutable address, and their execution unit is a
fiber owned by the WMB scheduler.

---

## 11. The Complete Boot Sequence

Putting HAL + Kdrive + fiber + TinyX + libX11 + JWM together:

```
1. Reset vector -> _start (crt0) -> zero .bss, set up SP -> call wmb_boot()

2. wmb_boot():
   a. os_early_init()                — UART for ErrorF, heap for malloc
   b. fiber_runtime_init(10)         — main becomes the immortal fiber
   c. hal_task_create("hal",   Hal_PumpFiber,    NULL,        16*1024)
   d. hal_task_create("xsrv",  TinyX_ServerFiber,NULL,       256*1024)
      -> that fiber calls Xserver main dispatch:
         OsInit(); InitOutput(); InitInput(); ... Dispatch();
   e. hal_task_create("jwm",   Jwm_FiberEntry,   &JwmState,  128*1024)
      -> Jwm_FiberEntry() -> Jwm_Main() -> XOpenDisplay() -> EventLoop()
   f. (optional) hal_task_create("shell", ShellApp_Entry, NULL, 64*1024)
   g. fiber_yield();  — never returns; main fiber is a no-op park
```

The first time the JWM fiber runs, it calls `WMB_API->XOpenDisplay(NULL)`.
That call:

1. Executes inside the JWM fiber's stack, in the WMB binary's text.
2. Allocates a `Display` and a `WmbClientRec`, registers the client
   with TinyX's `clients[]` array, sets `dpy->synchandler =
   wmb_sync_handler` and `WmbDpy::owner_fiber = hal_task_self()`.
3. Returns the `Display*` to JWM.

From that point on, each `JX*` call in JWM is a call through the
vtable → libX11 API function → fills `dpy->buffer` → `SyncHandle()` →
`wmb_sync_handler` → `ProcVector[opcode](client)` — all in the same
stack frame, no fiber switch unless a reply/event handler intentionally
blocks with `hal_task_wait()`.

---

## 12. Build System — Single CMake Target

One top‑level `CMakeLists.txt` orchestrates everything:

```cmake
cmake_minimum_required(VERSION 3.16)
project(wmb C)

# ---------------------------------------------------------------
# 1. Configuration knobs
# ---------------------------------------------------------------
set(TINYX_TARGET_OS   "tima"   CACHE STRING "linux | tima")
set(WMB_INPROC         ON       CACHE BOOL   "Enable in-process transport")
set(WMB_INCLUDE_JWM    ON       CACHE BOOL   "Compile JWM into the WMB")

# ---------------------------------------------------------------
# 2. Sub-libraries — each pinned to its own compile flags
# ---------------------------------------------------------------
add_subdirectory(framework/fiber)                 # libfiber
add_subdirectory(framework/sdl_drivers)           # libhal (or riscv_drivers)
add_subdirectory(tinyx)                           # libtinyx_core + libkdrive_tima
add_subdirectory(libX11)                          # libX11_wmb (WMB profile)
add_subdirectory(framework/wmb_bridge)            # transport shim + WaitFor

if(WMB_INCLUDE_JWM)
    add_subdirectory(jwm)                         # libjwm — private flags
endif()

# ---------------------------------------------------------------
# 3. Final binary
# ---------------------------------------------------------------
add_executable(wmb.elf framework/wmb_bridge/wmb_boot.c)
target_link_libraries(wmb.elf PRIVATE
    libjwm                # optional (WMB_INCLUDE_JWM)
    libX11_wmb            # order matters — before libtinyx_core
    libtinyx_core
    libkdrive_tima
    libtinyx_os_min
    libhal
    libfiber
    libc                  # newlib/picolibc
)
set_target_properties(wmb.elf PROPERTIES
    LINK_FLAGS "-T ${CMAKE_SOURCE_DIR}/framework/wmb_bridge/wmb.ld"
)
```

Per‑library isolation happens inside each `CMakeLists.txt`:

```cmake
# jwm/CMakeLists.txt (relevant excerpt)
add_library(libjwm STATIC ${JWM_SRCS})
target_include_directories(libjwm
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated/jwm       # jwm's private config.h
        ${CMAKE_SOURCE_DIR}/X11                 # for Xlib types
        ${CMAKE_SOURCE_DIR}/framework/library   # for hal_task.h
)
target_compile_definitions(libjwm PRIVATE
    WMB_INPROC=1
    JWM_MINIMAL=1
    # deliberately no USE_XFT/USE_PNG/USE_SHAPE/ENABLE_NLS/etc.
)
target_compile_options(libjwm PRIVATE
    -include jwm_namespace.h                    # applies rename table
    -fno-common                                 # catch tentative-def clashes
)
```

`jwm_namespace.h` (private header, injected via `-include`) is where the
rename table from §3.1 lives:

```c
/* jwm/src/jwm_namespace.h — injected into every JWM TU. */
#ifndef JWM_NAMESPACE_H
#define JWM_NAMESPACE_H

/* Entry point */
#define main                Jwm_Main

/* Globals moved into JwmState */
/*   (real globals declared in main.c/main.h wrapped in a struct) */

/* Generic helpers */
#define Allocate            Jwm_Allocate
#define Reallocate          Jwm_Reallocate
#define Release             Jwm_Release
#define CopyString          Jwm_CopyString
#define Warning             Jwm_Warning
#define FatalError          Jwm_FatalError
#define Signal              Jwm_Signal
#define GetCurrentTime      Jwm_GetCurrentTime
#define GetTimeDifference   Jwm_GetTimeDifference
#define GetTimeString       Jwm_GetTimeString
#define EventLoop           Jwm_EventLoop
#define Initialize          Jwm_Initialize
#define Startup             Jwm_Startup
#define Shutdown            Jwm_Shutdown
#define RunCommand          Jwm_RunCommand

#endif
```

Because it's injected via `-include`, upstream JWM source stays
unmodified — apart from `main.c` where we also `#undef main` right
before the definition to sidestep the macro:

```c
#undef main
int Jwm_Main(int argc, char *argv[])
{
    /* ...original body unchanged... */
}
```

---

## 13. Implementation Roadmap

Sequenced to keep each step independently testable.

**Phase 0 — Prep**

- Freeze the hosted build (SDL) as reference. Snapshot logs.

**Phase 1 — Rename & compile JWM as a static library**

- Add `jwm_namespace.h`, `-include` it, produce `libjwm.a`.
- Confirm that `libjwm.a` + a trivial `wmb_boot.c` linking calls to
  `Jwm_Main` builds against **stock libX11** on the host with no
  duplicate‑symbol errors.

**Phase 2 — Route JWM through the vtable**

- Extend `jxlib.h` with the `WMB_INPROC` `JFUNCn` definitions (§3.2).
- Build a small host demo: `wmb_boot` starts TinyX via the standard
  socket path but JWM through the vtable. Verify JWM can drive the
  server.

**Phase 3 — Cooperative event loop**

- Replace `WaitForEvent`'s `select()` with `hal_yield_timeout()`.
- Wire `wmb_WriteToClient` to `hal_task_wake(dpy->owner_fiber)` after
  event enqueue.
- Run JWM as a fiber alongside the TinyX server fiber. Prove input
  events cause JWM redraws.

**Phase 4 — Strip external‑process paths**

- Rewrite `command.c` to use the `wmb_app_table` (§6).
- Delete `HandleChild`, `sigaction`, `fork`, `execl`, `pipe`, `select`,
  `waitpid` under a `#ifdef WMB_INPROC` block.
- Recompile; verify no undefined POSIX symbols remain in `libjwm.a`
  (`nm --undefined-only`).

**Phase 5 — Cross‑compile for target**

- Switch to the RISC‑V toolchain + newlib.
- Confirm `libjwm.a` links into `wmb.elf` with only the symbols listed
  in [TINYX_OS_PORTING_GUIDE.md §5](TINYX_OS_PORTING_GUIDE.md#5-the-posix-syscall-surface)
  Tier 1.

**Phase 6 — Multi‑app**

- Add one demo app (in‑tree). Wire it via `wmb_app_table`.
- Launch from JWM's startup command. Verify both fibers coexist,
  both receive their own events, neither starves.

**Phase 7 — Runtime app loading (target only, optional)**

- Implement the Tima OS ELF loader hook that resolves against
  `WMB_API_ADDRESS` and calls `hal_task_create` on the app's entry
  point.
- Load and run a pre‑built out‑of‑tree app.

---

## 14. Verification Checklist

**Link stage**

- [ ] `nm --defined-only libjwm.a | grep ' T main$'` — empty (`main` is
      only in `wmb_boot.o`).
- [ ] `nm --undefined-only libjwm.a | grep -E 'fork|execv?[el]?|waitpid|sigaction|select|pipe'`
      — empty.
- [ ] `nm --defined-only wmb.elf | grep -c ' T '` — no duplicate symbol
      names (compare against a baseline list).

**Boot stage**

- [ ] `ErrorF` output shows the ordered banner: `fiber_runtime_init` →
      `hal init` → `TinyX InitOutput` → `TinyX InitInput` →
      `Jwm_FiberEntry` → `Jwm_Main` → `JXOpenDisplay`.
- [ ] Framebuffer clears to black on `tima_Enable`, JWM root window is
      created within the first 200 ms.

**Runtime**

- [ ] JWM handles a `MotionNotify` originating from HAL mouse events
      (verify by moving the pointer and observing a pager repaint).
- [ ] JWM handles a `KeyPress` and toggles between two desktops via a
      keybinding.
- [ ] A second in‑tree app launched via `Jwm_RunCommand("hello")`
      opens its own window, receives its own events, and does not
      steal focus arbitrarily.

**Stability**

- [ ] 1‑hour run with periodic keyboard/mouse activity: `_sbrk`
      high‑water mark stable; no fiber stack canary trips
      (`FIBER_ERR_OVERFLOW` count = 0).
- [ ] Restart path (`Jwm_RequestRestart()`) tears down JWM's fiber
      cleanly and re‑creates it without leaking `WmbClientRec` slots.

---

## Appendix — File Cross‑Reference

JWM sources needing attention:

- [jwm/src/main.c](../../jwm/src/main.c) — rename `main`; delete signal
  installation; wrap globals.
- [jwm/src/event.c](../../jwm/src/event.c) — rewrite `WaitForEvent` to
  use `hal_yield_timeout`.
- [jwm/src/command.c](../../jwm/src/command.c) — replace `fork/execl`
  with `wmb_app_table` dispatch.
- [jwm/src/timing.c](../../jwm/src/timing.c) — drop `TZ` push/pop.
- [jwm/src/misc.c](../../jwm/src/misc.c) — stub the last `getenv`.
- [jwm/src/jwm.h](../../jwm/src/jwm.h) — gate POSIX includes on
  `WMB_INPROC`.
- [jwm/src/jxlib.h](../../jwm/src/jxlib.h) — add `WMB_INPROC` `JFUNCn`
  definitions routing to `WMB_API`.

Framework support code to create:

- `framework/wmb_bridge/wmb_boot.c` — the top‑level `main()`, spawns
  the initial fibers.
- `framework/wmb_bridge/wmb_commands.c` — `wmb_app_table` and
  `Jwm_RunCommand`.
- `framework/wmb_bridge/wmb_waitfor.c` — the reduced `WaitForSomething`.
- `framework/library/hal_task.h` — already planned in
  [FIBER_PORTABLE_ARCHITECTURE.md](FIBER_PORTABLE_ARCHITECTURE.md).
- `framework/wmb_bridge/wmb.ld` — linker script pinning the `.wmb_api`
  section.

Prior notes:

- [../archive_docs2/CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md)
- [../archive_docs2/WMB_API_DESIGN.md](../archive_docs2/WMB_API_DESIGN.md)
- [../archive_docs2/MULTI_CORE_GUI_ANALYSIS.md](../archive_docs2/MULTI_CORE_GUI_ANALYSIS.md)
