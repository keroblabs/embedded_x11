# Hosted TinyX P4 server

This builds the actual DIX/MI/FB server, extensions, KDrive, and generic HAL port.
It is not the standalone KDrive harness. The real server creates the root
window, loads embedded `fixed`/`cursor` fonts, processes device input, reports
Damage to the framebuffer HAL, and reaches upstream `Dispatch()`.

## Build and run

From the repository root, with CMake 3.16+, a GNU-compatible C11 compiler/linker,
`patch`, Python 3.10+, SDL2 development files and zlib development files:

```sh
cmake -S . -B build-p4 -DWMB_TINYX_BUILD_SERVER=ON \
    -DWMB_KDRIVE_BUILD_TESTS=ON -DWMB_SDL_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build-p4
./build-p4/bin/wmb_host
```

Close the display window to shut down. The main thread owns the hosted runtime;
the X server runs in the named `xsrv` fiber. No safety/preemption tick is armed.

The first configure downloads the pinned libXfont 1.5.4 source, verifies its
SHA-256, applies two narrowly scoped bitmap-font repairs plus the built-in-only
profile patch, and builds the selected
font sources locally. No packages are installed globally. The library uses its
embedded fonts, not a font directory or FreeType. See
[reference configuration and dependencies](../../docs/reference/README.md).

For offline builds, prepopulate CMake's FetchContent cache or supply
`FETCHCONTENT_SOURCE_DIR_WMB_XFONT_SOURCE` pointing to an extracted matching
release. The same checked patches are applied to that source directory.

## Verification

```sh
ctest --test-dir build-p4 --output-on-failure
./build-p4/bin/wmb_host --self-test
```

The self-test displays the real root for **ten seconds**, verifies mapped root
geometry and rendered pixels, counts actual Damage/HAL flushes, measures process
CPU below **5%**, and requires the actual KDrive OS poll rate to be at least
**30 Hz**. It then requests cooperative termination and executes real server
cleanup. Display failures produce a nonzero process result.

For headless CI:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build-p4/bin/wmb_host --self-test
```

CTest also compares geometry, depth, visual masks/class and all twelve registered
extensions against [the startup reference](../../docs/reference/tinyx-startup.reference.cmake).
The timer test covers deadlines, cancellation, forced callbacks, repeat,
32-bit wrap, screen saver and DPMS. The KDrive close test checks that Damage is
unregistered **before** the framebuffer pixmap is destroyed.

Repeat with `-DWMB_SDL_PIXEL_BITS=32` and an odd width, such as
`-DWMB_SDL_WIDTH=319 -DWMB_SDL_HEIGHT=177`, and with ASan/UBSan:

```sh
cmake -S . -B build-p4-sanitize -DWMB_TINYX_BUILD_SERVER=ON \
    -DWMB_KDRIVE_BUILD_TESTS=ON -DWMB_SDL_BUILD_TESTS=ON \
    -DCMAKE_C_FLAGS="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-p4-sanitize
ctest --test-dir build-p4-sanitize --output-on-failure
```

The normal ASan `makecontext/swapcontext` warning is expected; memory/undefined
behavior reports are not suppressed.

## Files and ownership

| Surface | Role |
|---|---|
| [TinyX CMake](../CMakeLists.txt), [module groups](../../cmake/WmbModules.cmake) | Real server sources and frozen exclusion/membership checks |
| [generated configuration templates](../../cmake/) | Shared configuration prefix; all server-side X protocol types must agree |
| [server.c](server.c), [server.h](server.h) | Fiber entry/lifecycle, root validation and registered-extension observation |
| [host application](../../linux_hosted/apps/wmb_host/wmb_host.c) | Hosted idle sleep, user entry point and measurable integration gate |
| [OS replacement files](../os/) | In-process connection/I/O, typed cooperative waits, timers and explicitly excluded services |
| [HAL port](../kdrive/hal/README.md) | Fixed-mode RandR, framebuffer/Damage/input adaptation; no scheduler or SDL policy |

A consumer links the real server and KDrive archives in a linker rescan group,
plus its runtime and selected HAL implementations; the host
[CMake target](../../linux_hosted/apps/wmb_host/CMakeLists.txt) is the example.
Use `wmb_tinyx_project` and `wmb_tinyx_config` consistently. Mixing configured
and unconfigured X protocol headers is an ABI error, not a supported variant.

## Boundaries

The plain host and `--self-test` preserve the **P4 root-only gate**. Native socket
transport is not compiled. P5 now supplies real client registration, dispatch,
output framing and flow-control hooks; see [the client guide](../../transport/x11/README.md).
`--client-test`, `--flow-test`, `--raw-test` and the wire/timeout gates are
separate from the P4 root-only assertion.

P5 verifies same-address-space client request/reply/event delivery. Neither phase
proves JWM, the shipping multi-font registry, preemptive scheduling, external ABI
callers, or a freestanding/embedded server image.
The [P6 guide](../../docs/48-p6-portability.md) documents the completed
active-header/native-symbol and patch-replay gates. Target libc/console/
allocator/freestanding closure remains P9.

Follow [the project methodology](../../docs/47-tinyx-project-methodology.md).
