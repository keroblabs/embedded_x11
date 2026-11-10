# WMB

X11 stack (TinyX server + libX11 + JWM window manager) running inside a cooperative fiber runtime, with an in-process transport replacing the socket. Two targets: Linux hosted (SDL backend) and a freestanding embedded platform.

Status: the framework, hosted HAL, TinyX, retained Xlib and **P7 JWM desktop**
are runnable. Portability/patch gates remain active; external ABI and hardware
certification are later milestones. See the
[JWM desktop and reuse guide](jwm/wmb/README.md),
[P6 portability guide](docs/48-p6-portability.md),
[client transport guide](transport/x11/README.md),
[TinyX server guide](tinyx/wmb/README.md) and
[`docs/40-implementation-plan.md`](docs/40-implementation-plan.md).

## Build

Requires CMake ≥ 3.16, GCC or Clang, and (optionally) `libsdl2-dev` for the SDL demos.

```bash
cmake -B build
cmake --build build
```

Every binary lands in `build/bin/`. Individual targets:

```bash
cmake --build build --target wmb_fiber_smoke
cmake --build build --target wmb_task_smoke
cmake --build build --target sdl_fiber_example_v2   # requires SDL2
```

When SDL2 isn't found the SDL demos are skipped and configure prints:
```
-- wmb: SDL2 not found - SDL demos will be skipped
```

## Run

Every smoke test dispatches on a subcommand and exits `0` on pass.

```bash
./build/bin/wmb_fiber_smoke --pingpong    # V-P2-1
./build/bin/wmb_fiber_smoke --sync        # V-P2-3
./build/bin/wmb_fiber_smoke --overflow    # V-P2-4

./build/bin/wmb_task_smoke --timeout      # V-P3-1
./build/bin/wmb_task_smoke --wake         # V-P3-2
./build/bin/wmb_task_smoke --early        # V-P3-3
./build/bin/wmb_task_smoke --tick         # V-P3-4  (WMB_TICK_SECONDS=60 for the full doc gate)
./build/bin/wmb_task_smoke --prodcon      # V-P3-5
./build/bin/wmb_task_smoke --release      # V-P3-6  W5
./build/bin/wmb_task_smoke --coalesce     # V-P3-7  W2
./build/bin/wmb_task_smoke --flag         # V-P3-8  W3
./build/bin/wmb_task_smoke --recycle      # V-P3-9  W7
./build/bin/wmb_task_smoke --created      # V-P3-10
```

SDL demo (opens a window):

```bash
./build/bin/sdl_fiber_example_v2
```

See the [v2 example guide](linux_hosted/sdl_fiber_example_v2/README.md) for its
request/render flow, included files, and starting-point modifications.

## Reusing the SDL backend

The [portable HAL integration guide](linux_hosted/sdl_drivers/README.md) documents
the display/input contracts, private backend configuration, lifecycle,
regression tests, and integration into another CMake project or an embedded port. The
[SDL wrapper guide](linux_hosted/sdl_library/README.md) covers direct window/callback use.

Applications link `wmb_display_input` and include only the HAL headers; SDL types,
headers, and pixel/configuration macros stay private. Use `wmb_hosted_display`
when hosted WMB tasks/fibers are also needed. Software drawing belongs above the HAL.
To build only the
backend and its headless tests:

```bash
cmake -S linux_hosted/sdl_drivers -B build-sdl -DWMB_SDL_BUILD_TESTS=ON
cmake --build build-sdl
ctest --test-dir build-sdl --output-on-failure
```

## KDrive HAL module

The generic [KDrive HAL port and harness](tinyx/kdrive/hal/README.md) builds real
KDrive code against the platform-neutral display/input contracts. Its standalone
tests do not require SDL or a running TinyX server:

```sh
cmake -S tinyx/kdrive/hal -B build-kdrive
cmake --build build-kdrive
ctest --test-dir build-kdrive --output-on-failure
```

The standalone harness remains independent of the full server. See the
[KDrive module status](docs/43-kdrive-module-plan.md).

## TinyX P4 server

```sh
cmake -S . -B build-p4 -DWMB_TINYX_BUILD_SERVER=ON \
    -DWMB_KDRIVE_BUILD_TESTS=ON -DWMB_SDL_BUILD_TESTS=ON
cmake --build build-p4
./build-p4/bin/wmb_host
ctest --test-dir build-p4 --output-on-failure
```

This opens the real X root window. Close it to stop. The optional
`--self-test` runs for ten seconds and verifies rendering, Damage, idle CPU,
polling rate and cleanup. The first configure builds a pinned bitmap-font
dependency locally. See the [build/dependency/verification guide](tinyx/wmb/README.md).
Native X socket transport is not selected. To run the P5 Xlib application:

```sh
./build-p4/bin/wmb_host --client-test
```

It opens an unmanaged white window, draws a rectangle/text, verifies backend
input, and closes after ten seconds. Additional flow, timeout and independent
stock-Xlib wire gates are in the [P5 guide](transport/x11/README.md).

The P6 milestone had 30 checks. The [P6 guide](docs/48-p6-portability.md)
documents the whole-archive no-backend link, active include/symbol audit,
reference-preserving patches and remaining freestanding requirements.

## P7 managed desktop

The same build now includes real JWM:

```sh
./build-p4/bin/wmb_host --desktop
./build-p4/bin/wmb_host --wm-test
```

The desktop opens two managed applications. Drag title bars/corners, click to
focus/raise, or right-click the background to open another application.
The taskbar clock shows uptime, not calendar time. Closing the display or
selecting **Exit desktop** unwinds JWM, applications and the server cooperatively.

There are now **36 standard tests**, or 37 with the optional stock-Xlib/Xvfb
reference harness. The [JWM guide](jwm/wmb/README.md) documents the lifecycle/
registry APIs, profile limits, exact interaction/cancellation gates and capture
procedure. [The actual desktop capture](docs/reference/tinyx-jwm-rgb565.png)
shows native title bars, task list and clock.

Project-specific TinyX changes use the shared `WMB_DEVEL=1` selector, including
Release and embedded builds. Driver policy stays in the HAL port; necessary
core changes retain guarded reference alternatives. See the
[TinyX change methodology](docs/47-tinyx-project-methodology.md).

## AddressSanitizer

The fiber port brackets each `swapcontext` with `__sanitizer_start_switch_fiber` / `_finish_switch_fiber`, so ASan follows the fiber correctly.

```bash
rm -rf build-asan
CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" \
LDFLAGS="-fsanitize=address" \
cmake -B build-asan
cmake --build build-asan
./build-asan/bin/wmb_fiber_smoke --pingpong
./build-asan/bin/wmb_task_smoke  --prodcon
```

ASan prints a boilerplate `WARNING: ASan doesn't fully support makecontext/swapcontext functions ...`; that's informational — the annotations handle the switch.

## Layout

| Directory | Contents |
|---|---|
| `framework/` | Fiber runtime + library primitives (heap, ring buffer, list, pipe). Platform-free, X11-free. |
| `hal/` | Contract headers: `hal_task.h`, `hal_display.h`, `hal_input.h`, `hal_time.h`, `hal_alloc.h`. |
| `linux_hosted/` | Hosted fiber port, `hal_time`/`hal_alloc` implementations, SDL-backed `hal_display`/`hal_input`, test apps. |
| `transport/` | X11 protocol replacement (client half). Held for P5 in [`docs/44`](docs/44-framework-module-plan.md) §§4–6. |
| `tinyx/`, `libX11/`, `jwm/`, `X11/` | Vendored upstream trees. |
| `docs/` | Module and implementation plans (00–91, numbered). |

## CMake targets (libraries the app CMakeLists link against)

| Target | Kind | What it exports |
|---|---|---|
| `wmb_hal` | INTERFACE | The five contract headers on the include path |
| `wmb_framework` | STATIC | fiber runtime + library primitives |
| `wmb_fiber_linux` | STATIC | hosted fiber port (mmap + guard page + ucontext) |
| `wmb_system_linux` | STATIC | hosted `hal_time` + `hal_alloc` |
| `wmb_hosted` | INTERFACE | shortcut for framework + hosted port + hosted system |
| `wmb_sdl_library` | STATIC | SDL2 window/input wrappers, no WMB runtime dependency (only if SDL2 was found) |
| `wmb_sdl_drivers` | STATIC | Hosted implementation of the two portable HALs; private SDL dependency |
| `wmb_display_input` | ALIAS | Backend-neutral display/input target; only HAL headers are public |
| `wmb_hosted_display` | INTERFACE | Hosted framework/runtime plus the selected display/input backend |
| `wmb_kdrive` | STATIC | Actual KDrive core and generic HAL port; no SDL dependency |
| `wmb_tinyx_project` | INTERFACE | Shared `WMB_DEVEL=1` selection for TinyX and conditional-header consumers |
| `wmb_tinyx_config` | INTERFACE | Generated configuration prefix and consistent server protocol types (P4 option) |
| `wmb_tinyx` | STATIC | Actual TinyX DIX/MI/FB/extensions and owned cooperative OS integration |
| `wmb_transport_core` | STATIC | Standard-C request leases, framing, copied packet/event queues and control mailbox |
| `wmb_xlib` | STATIC | Retained client API and in-process transport; matching client headers, no server ABI prefix |
