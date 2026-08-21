# WMB

X11 stack (TinyX server + libX11 + JWM window manager) running inside a cooperative fiber runtime, with an in-process transport replacing the socket. Two targets: Linux hosted (SDL backend) and a freestanding embedded platform.

Status: the framework (`framework/`) and hosted platform (`linux_hosted/`) are runnable; the X11 transport (`transport/`) is scheduled. See [`docs/44-framework-module-plan.md`](docs/44-framework-module-plan.md) and [`docs/40-implementation-plan.md`](docs/40-implementation-plan.md) for the plan.

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
| `wmb_sdl_library` | STATIC | SDL2 window/input wrappers (only if SDL2 was found) |
| `wmb_sdl_drivers` | STATIC | SDL-backed `hal_display` / `hal_input` (only if SDL2 was found) |
| `wmb_hosted_sdl` | INTERFACE | shortcut for `wmb_hosted` + the two SDL libraries |
