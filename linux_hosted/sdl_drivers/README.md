# Portable display/input HAL: hosted SDL2 backend

Applications use only [hal_display.h](../../hal/hal_display.h) and
[hal_input.h](../../hal/hal_input.h). These interfaces contain no SDL types,
headers, pixel macros, window IDs, or backend-specific command buffers.
The same application-facing API applies to an embedded implementation.

SDL2 is an implementation detail of this hosted backend, using the adjacent
[window/event wrapper](../sdl_library/README.md). The backend has no WMB
framework, fiber, allocator, or time implementation dependency.

The legacy LCD/keypad/mouse public interface has been removed. Pixel filling,
bitmaps, text, transparency, and software scrolling belong in the application
or its graphics library, not in a device driver. The
[example drawing helper](../apps/common/framebuffer_draw.h) demonstrates
format-aware software rendering above the HAL; it is not a driver dependency.

## Public API

### Display

| Function in [hal_display.h](../../hal/hal_display.h) | Contract |
|---|---|
| `hal_display_init()` | Initialize the backend's fixed native configuration. Returns zero on success, negative on failure. An already-open display rejects another initialization. |
| `hal_display_get_fb()` | Return the owned framebuffer descriptor after successful initialization, otherwise `NULL`. It supplies pixels, width, height, byte stride, bpp, and pixel format. |
| `hal_display_flush(x, y, w, h)` | Clip/merge damage and queue device publication without waiting. This is not a drawing operation. |
| `hal_display_pump()` | Advance nonblocking presentation/device work. Returns `HAL_DISPLAY_ACTIVE`, `HAL_DISPLAY_STOP_REQUESTED`, or `HAL_DISPLAY_ERROR`. |
| `hal_display_shutdown()` | Release device resources. Idempotent. |
| `hal_display_set_power(on)` | Optional panel power control; unsupported backends return a negative result. |

There are no application-requested dimensions or format arguments. Configure the
panel/backend once, then obtain geometry and format from the framebuffer.
No separate width/height getters are required.

A stop request does **not** destroy the framebuffer. The caller still shuts down
explicitly, after its graphics work has stopped. A presentation/device failure
remains `HAL_DISPLAY_ERROR` until shutdown/reinitialization. A hardware backend
normally stays active; the hosted implementation maps a close/quit request to
`HAL_DISPLAY_STOP_REQUESTED`.

The backend owns the pixels. Never free them. Respect the descriptor's **byte**
stride instead of assuming packed rows. For row `y`:

```c
uint8_t *row = (uint8_t *)fb->pixels + (size_t)y * fb->stride;
/* Cast the row according to fb->format, not a platform pixel macro. */
```

RGB565 uses native `uint16_t` values; XRGB8888 uses native `uint32_t` values with
an unused high byte. A graphics library performs colour packing. A device with
no physical framebuffer may own a shadow buffer and transfer damage on flush/pump.
DMA, cache coherency, scanout, and presentation remain backend responsibilities.

### Keyboard and pointer

| Function in [hal_input.h](../../hal/hal_input.h) | Contract |
|---|---|
| `hal_input_init()` | Initialize the keyboard/pointer streams. Returns zero on success, negative on failure; duplicate initialization fails. |
| `hal_input_has_data(dev)` | Nonblocking queue query. |
| `hal_input_read_byte(dev, out)` | Read one byte if available; false leaves the output unchanged. Data byte zero is valid and must not be confused with an empty read. |
| `hal_input_control(dev, control, value)` | Apply a typed optional control synchronously, without waiting. |
| `hal_input_pump()` | Collect device events without waiting. |
| `hal_input_shutdown()` | Release/reset input state. Idempotent. |

Each command in `HAL_CMD_*` has exactly one data byte. Consumers reassemble
command/data pairs; decode motion and wheel bytes as `(int8_t)data`.
Keyboard bytes are ASCII `0x20` through `0x7E`, the seven `HAL_KEY_*` control
keys, and aggregate `HAL_MOD_*` masks. Button IDs are `0` released and
`HAL_MOUSE_BUTTON_LEFT/MIDDLE/RIGHT/X1/X2`. These constants belong to the
portable contract, not the hosted implementation.

The contract represents one keyboard and one pointer. Application-specific
keymap/scancode assignments belong above the HAL, including the TinyX modifier
scancodes; they are not hardware contract constants.

#### Typed controls

| Control | Value |
|---|---|
| `HAL_INPUT_CTRL_SCALE_Q12` | Signed multiplier: `HAL_INPUT_SCALE_ONE` (4096) is 1x, 8192 is 2x, zero stops motion, negative values invert it. |
| `HAL_INPUT_CTRL_CURSOR_VISIBLE` | `0` hides; `1` shows. |
| `HAL_INPUT_CTRL_POINTER_MODE` | `HAL_POINTER_BUTTONS` or `HAL_POINTER_PRESSURE`. |

Results are `HAL_INPUT_CONTROL_OK`, `HAL_INPUT_CONTROL_UNSUPPORTED`,
`HAL_INPUT_CONTROL_INVALID`, or `HAL_INPUT_CONTROL_FAILED`. Embedded backends
need not implement every optional control; unsupported controls must leave
device state unchanged. There is no opaque/native-endian byte payload.

```c
int rc = hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12,
                           HAL_INPUT_SCALE_ONE * 2);
if (rc != HAL_INPUT_CONTROL_OK && rc != HAL_INPUT_CONTROL_UNSUPPORTED)
    fprintf(stderr, "Pointer control failed\n");
```

## Portable application lifecycle

Drawing is application code. This minimal example clears the framebuffer without
any LCD helper or platform pixel definition:

```c
#include <stdio.h>
#include <string.h>
#include "hal_display.h"
#include "hal_input.h"

static int drain_input(hal_input_dev_t dev)
{
    uint8_t command, value;
    while (hal_input_read_byte(dev, &command))
    {
        if (!hal_input_read_byte(dev, &value))
        {
            fprintf(stderr, "Incomplete input packet\n");
            return -1;
        }
        /* Interpret command/value here; zero is a valid value. */
    }
    return 0;
}

int main(void)
{
    int result = 0;
    if (hal_display_init() != 0) return 1;
    if (hal_input_init() != 0)
    {
        hal_display_shutdown();
        return 1;
    }

    const hal_framebuffer_t *fb = hal_display_get_fb();
    memset(fb->pixels, 0, (size_t)fb->stride * fb->height);
    hal_display_flush(0, 0, fb->width, fb->height);

    for (;;)
    {
        hal_input_pump();
        hal_display_status_t status = hal_display_pump();
        if (status == HAL_DISPLAY_STOP_REQUESTED) break;
        if (status == HAL_DISPLAY_ERROR ||
            drain_input(HAL_INPUT_KEYBOARD) < 0 ||
            drain_input(HAL_INPUT_MOUSE) < 0)
        {
            result = 1;
            break;
        }
        /* Yield/sleep using the application's task runtime if needed. */
    }
    hal_input_shutdown();
    hal_display_shutdown();
    return result;
}
```

Both pumps must be called regularly when both devices are in use. Display-only
applications need only the display lifecycle/pump. Input and display can be
initialized/shut down independently. Keep calls serialized on the device-owning
task/main-loop context; do not invoke public APIs from an ISR or recursively pump.
An embedded backend may use an ISR internally with an appropriate synchronized
producer queue. Its consumer-facing API is unchanged.

## Build and integration

Hosted requirements: CMake 3.16+, C11 compiler, and SDL2 2.0.5+ development files.
The optional tests also use a C++11 compiler and a Linux linker supporting `--wrap`.
For Debian/Ubuntu, install `build-essential cmake libsdl2-dev pkg-config`.

From the repository root:

```sh
cmake -S linux_hosted/sdl_drivers -B build-hal -DWMB_SDL_BUILD_TESTS=ON
cmake --build build-hal
ctest --test-dir build-hal --output-on-failure
```

For another project, vendor this repository and select the backend at build time:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_application C)
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/embedded_x11/linux_hosted/sdl_drivers"
    "${CMAKE_CURRENT_BINARY_DIR}/platform-backend"
)
add_executable(my_application main.c)
target_link_libraries(my_application PRIVATE wmb_display_input)
```

`wmb_display_input` is the backend-neutral target name. This hosted directory
implements it using a private SDL dependency. An embedded backend should export
the same target name, implement the same twelve functions, and publish only the
HAL include directory. Changing the selected backend should not change application
source, public headers, or graphics code.

The target does **not** propagate SDL include paths, SDL headers, framework type
headers, `PIXEL_TYPE_T`, `SDL_MAIN_HANDLED`, or screen/pixel configuration macros.
Static-link SDL dependencies are supplied by CMake without becoming compile-time
application dependencies. `wmb_sdl_library` remains separately available for
explicit SDL-only clients, which are intentionally not portable HAL applications.

In the full repository, `wmb_hosted_display` combines the hosted WMB task runtime
with the selected display/input backend. The existing hosted examples link this
target, draw above the HAL, and use portable display status rather than SDL state.

### Hosted backend configuration

| Cache variable | Default | Allowed values |
|---|---|---|
| `WMB_SDL_WIDTH` | 800 | Integer 1 through 65535 |
| `WMB_SDL_HEIGHT` | 480 | Integer 1 through 65535 |
| `WMB_SDL_PIXEL_BITS` | 16 | 16 (RGB565), 32 (XRGB8888) |
| `WMB_SDL_BUILD_TESTS` | OFF | Optional regression tests |

These are backend-private build choices, not application macros. For example:

```sh
cmake -S linux_hosted/sdl_drivers -B build-hal32 \
    -DWMB_SDL_PIXEL_BITS=32 -DWMB_SDL_WIDTH=1024 -DWMB_SDL_HEIGHT=600
cmake --build build-hal32
```

The hosted backend owns one fixed display window, queues clipped damage, and
presents at 50 Hz. It requests no renderer vsync and does not sleep in the pumps.
Input and display share the private SDL event subsystem, but input pumping does
not perform presentation. No global `SDL_Quit()` is called; shutdown releases
only this implementation's video reference and own display window.

Hosted input maps unshifted key symbols plus modifier snapshots, ignores text/IME
and repeat events, and releases tracked input on focus loss. Pressure mode is
mouse-to-pressure emulation (0/100), not native multitouch. The byte protocol has
one active button ID, not simultaneous button chords. Scaled motion/wheels clamp
to int8 and pressure to uint8 with warnings. Each output queue holds 512 bytes;
overflow logs and drops a whole new event, never an unread byte or partial pair.
Applications must drain regularly; lost releases are not automatically recovered.

## Verification and migration

The [backend tests](tests/sdl_driver_test.c) cover metadata/pitch, actual rendered
damage, clipping/overflow, pump errors and stop requests, byte framing, modifiers,
pointer controls/buttons/wheels, queue bounds, independent lifecycle ownership,
and injected creation failures.

The **same** [C++ client](tests/integration/main.cpp) builds/runs against this
backend and a [hardware-style reference backend](tests/integration/reference_backend.c)
with different fixed geometry and padded rows. A
[header probe](tests/integration/public_headers.c) compiles under
`-ffreestanding -nostdinc` with only minimal C standard headers, verifies function
signatures, and rejects leaked backend macros. Consumer usage requirements are
also checked for backend include paths/definitions. The reference is a test
fixture, not an implemented embedded hardware port.

```sh
cmake -S linux_hosted/sdl_drivers -B build-hal-sanitize \
    -DWMB_SDL_BUILD_TESTS=ON -DWMB_SDL_WIDTH=7 -DWMB_SDL_HEIGHT=5 \
    -DWMB_SDL_PIXEL_BITS=16 \
    -DCMAKE_C_FLAGS="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-hal-sanitize
ctest --test-dir build-hal-sanitize --output-on-failure
```

Repeat with 32 bits. The tests use dummy/software rendering and do not require a
display server.

This is an intentional source/API break:

- Replace the removed legacy header/functions with the two HAL headers.
- Call parameterless display initialization; read geometry/format/stride from
  the framebuffer instead of getters or compile definitions.
- Move software drawing to application graphics code.
- Replace platform event-loop/running queries with the two pumps and display status.
- Replace byte-packed input controls with typed controls. Cursor visibility now
  has ordinary `0 = hidden`, `1 = visible` semantics.
- Application-specific keymap scancodes are no longer HAL constants.

The [generic KDrive/HAL port](../../tinyx/kdrive/hal/README.md) now has a separate
real-core harness, correct private modifier slots, wheel/button translation,
and damage publication. These are not covered by the HAL-only tests above.
Full TinyX/JWM/transport integration remains a separate workstream; the hosted
examples do not exercise the complete X11 stack.

No standalone license is declared in these module directories. Confirm
redistribution permission/license terms and retain applicable repository/SDL2
notices before publishing the backend in another project.
