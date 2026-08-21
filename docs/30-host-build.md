# 30 — Host Build (SDL)

## 1. Purpose

This document specifies **the hosted development build**: the CMake structure, the profiles, the SDL backend, the fiber port, and the bring‑up order. It is
the first deliverable of workstream 4 ([`00-introduction.md`](00-introduction.md) §5.4).

The hosted build carries all development weight. Every capability in WMB is developed, debugged and gated here before the target build is attempted. It is
not a throwaway prototype — the `host-pure` profile compiles the *same sources with the same in‑process transport* as the target, differing only in which
backend is linked underneath the platform contract.

### 1.1 Current state

The build as it stands (`framework/CMakeLists.txt`, 102 lines) is a stub:

- One target, `wmb_tima_build`.
- Compiles 12 files: the SDL library glue, three SDL drivers, two heap helpers, the four Tima DDX files, and a six‑line `main()`.
- Links **nothing** from `tinyx/dix`, `tinyx/os`, `tinyx/mi`, `tinyx/fb`, `tinyx/kdrive/src`, `libX11/` or `jwm/`.
- Hard‑codes `SDL_ROOT` to `/home/wifi/projects/SDL` and fails configuration if it is absent.
- Defines `_USE_LINUX`, which is precisely the kind of dependency the project exists to remove.
- Leaves `HAVE_CONFIG_H` undefined, which cannot survive whole‑stack assembly.

There is no built binary in `build/` or `build_local/`. **This document replaces that file entirely.**

### 1.2 What this document is not

- **Not the target build.** [`31-target-build.md`](31-target-build.md).
- **Not the component list.** [`01-component-inventory.md`](01-component-inventory.md) §12 defines the module groups this build mirrors.
- **Not the contract.** [`12-platform-contract.md`](12-platform-contract.md).

## 2. Build Requirements

| Requirement | Rationale |
|---|---|
| **One command produces one binary.** `cmake --build build` → `wmb_host` | Project requirement |
| **Profiles are CMake options, not source edits.** | [`03-`](03-target-architecture.md) §5 |
| **Module groups mirror [`01-`](01-component-inventory.md) §12 exactly.** | Traceability: a file in the build must be in the inventory |
| **No autotools.** TinyX's `configure`/`Makefile.am` is not the production path | Project requirement |
| **SDL is found, not hard‑coded.** System SDL2 via `find_package`, with an override for a local checkout | The current absolute path is not portable to another developer's machine |
| **Out‑of‑tree builds only.** | `build/`, `build-pure/`, `build-target/` coexist |
| **`compile_commands.json` is produced.** | IDE and static analysis |

## 3. Directory Layout

```
x11_window/
├── CMakeLists.txt                 ← NEW: top-level orchestration (currently absent)
├── cmake/
│   ├── WmbProfiles.cmake          ← NEW: profile → macro expansion
│   ├── WmbModules.cmake           ← NEW: the module group source lists
│   ├── FindLocalSDL2.cmake        ← NEW: system SDL2 or local checkout
│   └── dix-config.h.in            ← NEW: generated TinyX config
│       kdrive-config.h.in
├── framework/                      ← PLATFORM-FREE (rule L9)
│   ├── CMakeLists.txt
│   ├── library/                    ← contract headers + portable heap/buffer
│   ├── fiber/                      ← scheduler + fiber_port.h contract
│   ├── wmb_x11/                    ← transport + wmb_main() entry point
│   ├── wmb_api/                    ← NEW: external ABI
│   └── wmb_boot.c                  ← NEW: this configuration's host; calls wmb_main()
├── linux_hosted/                   ← HOSTED PORT
│   ├── system_linux/               ← hal_time_hosted.c (+ wmb_os_hosted.c)
│   ├── fiber_linux/                ← fiber_port.c, intr_handler.c
│   ├── sdl_drivers/                ← hal_display_sdl.c, lcd/mouse/keyraw drivers
│   ├── sdl_library/                ← SDL window/texture/event glue
│   ├── third_party/libucontext/    ← NEW: vendored
│   └── *_example/                  ← smoke programs
├── <target>_native/                ← TARGET PORT, same shape (31- §4.6)
├── apps/
│   ├── wmb_test_client/            ← NEW
│   └── wmb_hello/                  ← NEW (external, bridge-tested)
└── wrapper/libX11-app/             ← NEW
```

The top‑level `CMakeLists.txt` does not exist today; the build is driven from `framework/`. That inversion is wrong — the framework is one of six trees —
and is corrected here.

**`framework/` contains no platform code.** Selecting a platform means including one port tree's `*.cmake` fragments instead of another's; no source file
changes. This is rule L9 ([`03-`](03-target-architecture.md) §3.1), and it is why the two backend columns in
[`12-`](12-platform-contract.md) §10 are peers rather than a default and an exception.

## 4. CMake Structure

### 4.1 Top level

```cmake
cmake_minimum_required(VERSION 3.16)
project(wmb C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(WMB_PROFILE "host-pure" CACHE STRING "host-pure | host-bridge | host-unit | target")
set_property(CACHE WMB_PROFILE PROPERTY STRINGS host-pure host-bridge host-unit target)

set(WMB_SCREEN_WIDTH  800 CACHE STRING "framebuffer width")
set(WMB_SCREEN_HEIGHT 480 CACHE STRING "framebuffer height")
set(WMB_SCREEN_BPP     16 CACHE STRING "bits per pixel: 16 or 32")
set(WMB_LOAD_BASE  0x80100000 CACHE STRING "fixed load base (target profile)")

include(cmake/WmbProfiles.cmake)     # validates + expands WMB_PROFILE into macros
include(cmake/WmbModules.cmake)      # defines the module group source lists

add_subdirectory(framework)
add_subdirectory(jwm)
add_subdirectory(apps)
if(WMB_INPROC AND WMB_HAS_OS_TRANSPORT)
    add_subdirectory(wrapper/libX11-app)     # host-bridge only: needs both
endif()
```

### 4.2 Profile expansion — `cmake/WmbProfiles.cmake`

```cmake
if(WMB_PROFILE STREQUAL "host-pure")
    set(WMB_DEFS WMB_HOSTED WMB_INPROC)
    set(WMB_TINYX_OS_TRANSPORT_SOURCES wmb)
elseif(WMB_PROFILE STREQUAL "host-bridge")
    # P8: adds a host socket for the wmb_api_t bridge; the X11 transport is unchanged
    set(WMB_DEFS WMB_HOSTED WMB_INPROC WMB_HAS_OS_TRANSPORT)
    set(WMB_TINYX_OS_TRANSPORT_SOURCES wmb)
elseif(WMB_PROFILE STREQUAL "host-unit")
    set(WMB_DEFS WMB_HOSTED)
    set(WMB_TINYX_OS_TRANSPORT_SOURCES none)
elseif(WMB_PROFILE STREQUAL "target")
    set(WMB_DEFS WMB_FREESTANDING WMB_INPROC)
    set(WMB_TINYX_OS_TRANSPORT_SOURCES wmb)
else()
    message(FATAL_ERROR "unknown WMB_PROFILE '${WMB_PROFILE}'")
endif()

list(APPEND WMB_DEFS
     WMB_SCREEN_WIDTH=${WMB_SCREEN_WIDTH}
     WMB_SCREEN_HEIGHT=${WMB_SCREEN_HEIGHT}
     WMB_SCREEN_BPP=${WMB_SCREEN_BPP}
     HAVE_CONFIG_H)
```

**Every profile that runs an X server compiles the same transport.** TinyX's own `connection.c` / `io.c` / `WaitFor.c` / `xstrans.c` are **never** in a
source list ([`90-`](90-decisions.md) D‑49), so `WMB_TINYX_OS_TRANSPORT_SOURCES` now only distinguishes "the X server is present" (`wmb`) from "it is not"
(`none`, for `host-unit`). `host-bridge` differs from `host-pure` by one macro and one extra target, not by a different server.

### 4.3 Module groups — `cmake/WmbModules.cmake`

One list variable per group from [`01-`](01-component-inventory.md) §12. Abbreviated:

```cmake
file(GLOB TINYX_DIX_SOURCES  ${CMAKE_SOURCE_DIR}/tinyx/dix/*.c)
file(GLOB TINYX_MI_SOURCES   ${CMAKE_SOURCE_DIR}/tinyx/mi/*.c)
file(GLOB TINYX_FB_SOURCES   ${CMAKE_SOURCE_DIR}/tinyx/fb/*.c)
file(GLOB TINYX_MIEXT_SOURCES
     ${CMAKE_SOURCE_DIR}/tinyx/miext/shadow/*.c
     ${CMAKE_SOURCE_DIR}/tinyx/miext/damage/damage.c)
file(GLOB TINYX_EXT_SOURCES
     ${CMAKE_SOURCE_DIR}/tinyx/render/*.c
     ${CMAKE_SOURCE_DIR}/tinyx/randr/*.c
     ${CMAKE_SOURCE_DIR}/tinyx/xfixes/*.c
     ${CMAKE_SOURCE_DIR}/tinyx/damageext/*.c)

# Xext is an explicit list, not a glob — two files are excluded (01- §8.1)
set(TINYX_XEXT_SOURCES
    tinyx/Xext/bigreq.c  tinyx/Xext/shape.c   tinyx/Xext/sync.c
    tinyx/Xext/xcmisc.c  tinyx/Xext/xtest.c   tinyx/Xext/sleepuntil.c
    tinyx/Xext/saver.c   tinyx/Xext/dpms.c    tinyx/Xext/xres.c)
#   EXCLUDED: shm.c, xf86bigfont.c

# kdrive/src minus kmap.c (01- §6.1)
set(TINYX_KDRIVE_SOURCES
    tinyx/kdrive/src/kcmap.c  tinyx/kdrive/src/kdrive.c tinyx/kdrive/src/kinfo.c
    tinyx/kdrive/src/kinput.c tinyx/kdrive/src/kmode.c  tinyx/kdrive/src/kshadow.c)

set(TINYX_OS_SOURCES
    tinyx/os/log.c tinyx/os/oscolor.c tinyx/os/strlcat.c tinyx/os/strlcpy.c
    tinyx/os/osinit.c tinyx/os/utils.c)
if(WMB_TINYX_OS_TRANSPORT_SOURCES STREQUAL "wmb")
    # tinyx/os/{connection,io,WaitFor,xstrans,access,auth,mitauth}.c are never compiled
    list(APPEND TINYX_OS_SOURCES
         tinyx/os/connection_wmb.c tinyx/os/io_wmb.c tinyx/os/waitfor_wmb.c
         tinyx/os/excluded_stubs_wmb.c)
endif()
```

Globs are used only where the whole directory is included; every partial directory is an explicit list with the exclusions commented, so that a reader can
verify against [`01-`](01-component-inventory.md) without running CMake.

### 4.4 Targets

```
libwmb_runtime.a     fiber core + arch port + heap + buffer      ← the shareable framework
libwmb_hal.a         contract adapters + selected backend
libtinyx.a           DIX + OS + MI + FB + MIEXT + EXT + KDRIVE + DDX  ┐
libx11_wmb.a         libX11 API files + WMB transport                  ├ the windowing set
libjwm.a             JWM + glue                                        ┘
wmb_host             executable: wmb_boot.c + all of the above
```

Static libraries rather than one flat source list, because it makes the layering visible in the link line and makes a violation (e.g. `libtinyx.a` needing
a symbol from `libwmb_hal.a`'s SDL backend) a link error rather than a silent success.

**This decomposition is also the detachment plan.** `libwmb_runtime.a` is exactly the framework other system components will eventually share — fiber,
heap, buffer, and nothing else — and `wmb_host` is the *integrated* configuration in which everything happens to be linked together today. Keeping the
boundary honest now is what makes the split later cheap: [`40-`](40-implementation-plan.md) §13.

Link order matters and is fixed:

```cmake
target_link_libraries(wmb_host PRIVATE
    libjwm libx11_wmb libtinyx libwmb_hal libwmb_runtime ${SDL2_LIBRARIES} m)
```

### 4.5 Include paths

Derived from `tinyx/configure.ac`'s `KDRIVE_INCS`, with `KDRIVE_OS_INC` pointing at our DDX rather than `kdrive/linux`:

```cmake
set(TINYX_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/X11                     # so <X11/X.h> resolves
    ${CMAKE_SOURCE_DIR}/tinyx/include            # CORE_INCS
    ${CMAKE_SOURCE_DIR}/tinyx/dix
    ${CMAKE_SOURCE_DIR}/tinyx/os
    ${CMAKE_SOURCE_DIR}/tinyx/mi                 # MI_INC
    ${CMAKE_SOURCE_DIR}/tinyx/fb                 # FB_INC
    ${CMAKE_SOURCE_DIR}/tinyx/Xext               # XEXT_INC
    ${CMAKE_SOURCE_DIR}/tinyx/miext/shadow       # MIEXT_SHADOW_INC
    ${CMAKE_SOURCE_DIR}/tinyx/miext/damage       # MIEXT_DAMAGE_INC
    ${CMAKE_SOURCE_DIR}/tinyx/render             # RENDER_INC
    ${CMAKE_SOURCE_DIR}/tinyx/randr              # RANDR_INC
    ${CMAKE_SOURCE_DIR}/tinyx/xfixes             # FIXES_INC
    ${CMAKE_SOURCE_DIR}/tinyx/damageext          # DAMAGE_INC
    ${CMAKE_SOURCE_DIR}/tinyx/kdrive/src         # KDRIVE_INC
    ${CMAKE_SOURCE_DIR}/tinyx/kdrive/tima        # KDRIVE_OS_INC  ← ours, not linux/
    ${CMAKE_BINARY_DIR}/generated)               # dix-config.h, kdrive-config.h
```

### 4.6 Generated configuration headers

`cmake/dix-config.h.in` and `cmake/kdrive-config.h.in`, produced with `configure_file()`. The macro set is **curated by hand against a one‑off reference
`configure` run** on a pristine TinyX checkout — never guessed, and never copied in wholesale ([`90-`](90-decisions.md) D‑37). The resulting list is
[`91-open-questions.md`](91-open-questions.md) Q2 and a P4 deliverable. Indicative content:

```c
#define XVENDORNAME        "WMB"
#define VENDOR_RELEASE     1
#define RENDER             1
#define RANDR              1
#define XFIXES             1
#define DAMAGE             1
#define SHAPE              1
#define XSYNC              1
#define BIGREQS            1
#define XCMISC             1
#define XTEST              1
#define SCREENSAVER        1
#define DPMSExtension      1
#define KDRIVESERVER       1
#define KDRIVEFBDEV        0
#define SMART_SCHEDULE     1
#define MITSHM             0     /* Xext/shm.c excluded              */
#define XF86BIGFONT        0     /* Xext/xf86bigfont.c excluded      */
#define DBE                0     /* dbe/ excluded                    */
#define XDMCP              0
#define SECURITY           0
#define XKB                0     /* D-39: not compiled in            */
#cmakedefine WMB_HAS_OS_TRANSPORT
```

`XKB` is 0 in every profile ([`90-`](90-decisions.md) D‑39): internationalisation is out of scope, and the core protocol's keymap model is adequate for a
fixed embedded keyboard. It is written as a literal rather than a CMake variable so that turning it on is a decision, not a configure flag.

## 5. SDL Discovery

Replaces the hard‑coded `/home/wifi/projects/SDL`:

```cmake
# cmake/FindLocalSDL2.cmake
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    set(WMB_SDL_ROOT "" CACHE PATH "local SDL2 checkout")
    if(NOT WMB_SDL_ROOT)
        message(FATAL_ERROR
            "SDL2 not found. Install libsdl2-dev, or set -DWMB_SDL_ROOT=/path/to/SDL")
    endif()
    # local checkout: headers under include/, libs under build/build/.libs
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    ...
endif()
```

The `SDL2/SDL.h` shim generation in the current file (creating a `sdl2_shim/SDL2/SDL.h` that `#include`s the absolute path) is removed; the SDL sources are
fixed to `#include <SDL.h>` with the include directory supplied by CMake, which is the conventional arrangement.

## 6. The Fiber Port

### 6.1 libucontext

Vendored under `linux_hosted/third_party/libucontext/`, built as a static library, linked only into `libwmb_runtime.a` under `WMB_HOSTED`.

Rationale over glibc's `<ucontext.h>`: glibc's `makecontext`/`swapcontext` are deprecated, are not available on all libcs (musl removed them), and on some
architectures perform a `sigprocmask` syscall per switch. libucontext is a small, portable, dependency‑free implementation with hand‑written switch
routines for x86‑64, ARM, AArch64, RISC‑V and MIPS.

### 6.2 The port

Implements `framework/fiber/fiber_port.h`:

| Contract symbol | Hosted implementation |
|---|---|
| `fiber_port_init(period_ms)` | Start the tick thread; install the deferred‑switch signal handler |
| `fiber_port_init_stack(top, entry, arg)` | `libucontext_getcontext` + `libucontext_makecontext` with `fiber_trampoline` |
| `fiber_port_request_switch()` | Set the pending flag; `pthread_kill(main_thread, SIGRTMIN)` |
| `fiber_port_irq_lock()` | `pthread_sigmask(SIG_BLOCK, …)`, returns the previous mask index |
| `fiber_port_irq_unlock(prev)` | Restore |
| `fiber_sched_swap(cur_sp)` | Provided by `fiber.c`; the port calls it from the signal handler |

### 6.3 The one thread

`linux_hosted/fiber_linux/hosted_timer.c` runs the only `pthread` in WMB:

```c
static void *tick_thread(void *arg)
{
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        next.tv_nsec += 1000000;                 /* 1 ms */
        normalise(&next);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        g_tick_ms++;                             /* atomic store */
        if (--g_preempt_countdown == 0) {
            g_preempt_countdown = g_preempt_period;
            fiber_port_request_switch();
        }
    }
}
```

It touches exactly two variables: a tick counter and a pending‑switch flag. It never touches WMB data structures. That constraint is what keeps "single
threaded" true in every way that matters.

### 6.4 Stack allocation with guard pages

```c
void *hosted_alloc_stack(size_t bytes)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t total = ((bytes + page - 1) & ~(page - 1)) + page;   /* + guard */
    void *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    mprotect(base, page, PROT_NONE);            /* guard page at the low end */
    return (char *)base + page;
}
```

A stack overflow becomes an immediate SIGSEGV at a known address instead of silent corruption of the adjacent fiber's stack. This alone justifies
developing on the host.

## 7. Bring‑Up Order

Each step produces a runnable artefact. **No step starts until the previous one runs.**

| # | Step | Artefact | Gate |
|--:|---|---|---|
| B1 | Top‑level CMake + profiles + SDL discovery | Configures cleanly on a machine with system SDL2 | `cmake -B build -DWMB_PROFILE=host-pure` succeeds |
| B2 | Contract headers + SDL adapters + `hal_display_*` | `wmb_hal_smoke` executable | A colour gradient appears in an SDL window, written through `hal_display_get_fb()` and pushed with `hal_display_flush()` |
| B3 | libucontext + fiber port + `hal_task_*` | `wmb_fiber_smoke` | Two fibers ping‑pong 100,000 times; a busy fiber is preempted within 2 tick periods; `hal_yield_timeout(50)` returns 0 at 50±5 ms |
| B4 | `hal_input_*` adapters | `wmb_input_smoke` | Key and mouse bytes appear on stdout in the `CMD_*` protocol |
| B5 | TinyX whole‑stack assembly | `wmb_host` | Links with zero unresolved symbols; SDL window opens showing the X root window; the server idles |
| B6 | Zero‑socket confirmation | `wmb_host` | `lsof` shows no socket; `pollEvents` observed at ≥ 30 Hz |
| B7 | In‑process client path | `wmb_test_client` runs as a fiber | Six‑step PASS sequence; a window with a rectangle and text is visible |
| B8 | JWM | `wmb_host` | Decorated windows; move, resize, root menu |
| B9 | External ABI over the bridge | `wmb_hello` | A separate process produces a visible window |

**B5 is the pivotal step and the one most likely to consume time.** It is the first time ~165 kLOC of upstream code is linked together in a configuration
nobody has built before. Expect a long tail of missing `dix-config.h` macros, missing stub symbols, and extension registration order problems. With the
stock‑client oracle retired ([`90-`](90-decisions.md) D‑49), the gate here is a painted root window and a server that idles rather than spins — weaker
evidence than `xeyes` would have given, and the reason [`40-`](40-implementation-plan.md) §5.3 fixes a bisection order for P5 failures.

## 8. Development Aids

| Aid | Purpose |
|---|---|
| `WMB_DEBUG_TRACE_REQUESTS` | Log every opcode dispatched, with client index and sequence. The in‑process equivalent of `xtrace` |
| `WMB_DEBUG_REPLY_TIMEOUT_MS` | Turn `hal_task_wait()` in `wmb_dispatch` into a bounded wait that aborts with a backtrace — catches the deadlock class ([`21-`](21-inprocess-transport.md) R5) |
| `WMB_DEBUG_BUFFER_POISON` | Poison `dpy->buffer` after reallocation to trap stale `xReq*` pointers ([`21-`](21-inprocess-transport.md) R2) |
| `WMB_DEBUG_FIBER_NAMES` | Prefix every `ErrorF` with the current fiber name |
| `.gdbinit` helper | `wmb-fibers` command listing fiber names, states and stack usage |
| Valgrind | Works, with `--tool=memcheck` and a suppression file for libucontext's stack switching |
| ASan | Works with `ASAN_OPTIONS=detect_stack_use_after_return=0`; fiber stacks need `__sanitizer_start_switch_fiber` annotations in the port |

The ASan annotation is worth doing early: without it, ASan reports false positives on every context switch and becomes unusable, which removes the single
best tool for finding the buffer bugs that [`21-`](21-inprocess-transport.md) R1/R2 predict.

## 9. What the Host Build Must Not Become

| Anti‑pattern | Why it matters |
|---|---|
| Adding a host‑only shortcut above the contract | It will not exist on target and the bug surfaces at P9 |
| Using `pthread` for anything but the tick | Breaks the single‑threaded invariant that 78 kLOC of upstream code relies on |
| Testing only in `host-bridge` | The bridge is scaffolding, not the target path. `host-pure` is the gated profile |
| Letting SDL types cross the contract | Layering rule L1 |
| Hard‑coding paths (as the current file does) | Makes the build unreproducible |
| Using `getenv` for configuration | It is one of the things being removed |

## 10. Verification

| # | Gate | Step |
|---|---|---|
| H‑V1 | Configure succeeds on a clean machine with `libsdl2-dev` and no `WMB_SDL_ROOT` | B1 |
| H‑V2 | All four profiles configure | B1 |
| H‑V3 | The CMake source lists match [`01-`](01-component-inventory.md) §12 — verified by a script that diffs the two | B5 |
| H‑V4 | `host-pure`: the SDL window shows the X root window and the server idles | B5 |
| H‑V5 | `host-pure`: `strace -e trace=network,select,poll` produces no output | B6 |
| H‑V6 | `host-pure`: idle CPU usage < 5 % (no busy‑spin in `WaitForSomething`) | B6 |
| H‑V7 | `wmb_test_client` prints six PASS lines | B7 |
| H‑V8 | Valgrind clean over a 60‑second interactive session | B8 |
| H‑V9 | One‑hour run with scripted input: flat heap high‑water mark, no canary trip | B8 |
