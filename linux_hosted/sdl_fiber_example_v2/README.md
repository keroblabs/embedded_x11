# Framebuffer request/render fiber example (v2)

This example demonstrates a small in-process request queue using WMB's cooperative
fiber runtime. Three client fibers periodically submit rectangle-drawing requests
to one render fiber. **Only the render fiber writes the framebuffer.**

The main/root context runs the scheduler and pumps display presentation. Display
access uses the portable HAL; SDL is confined to the hosted backend.

This is a request/render proof of concept, not the complete TinyX/libX11/JWM
stack or an implementation of the X11 protocol. Its request flow follows the
[in-process transport design](../../docs/21-inprocess-transport.md).

## Build and run

### Requirements

- Linux, a C11 compiler, and CMake 3.16 or newer.
- SDL2 2.0.5 or newer, including development headers/libraries.
- A desktop display session for the visible window, or the headless settings below.
- `libucontext` is optional: the hosted fiber port can fall back to glibc
  `ucontext`. See the [hosted fiber port](../fiber_linux/README.md).

For Debian/Ubuntu, the basic packages are:

```sh
sudo apt install build-essential cmake libsdl2-dev pkg-config
```

**Configure the repository root**, not this directory. The local
[CMakeLists.txt](CMakeLists.txt) relies on targets created by the root project.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sdl_fiber_example_v2
./build/bin/sdl_fiber_example_v2
```

Alternatively, from this example directory:

```sh
cmake -S ../.. -B ../../build -DCMAKE_BUILD_TYPE=Debug
cmake --build ../../build --target sdl_fiber_example_v2
../../build/bin/sdl_fiber_example_v2
```

The example runs for approximately ten seconds. Closing the window ends the run
early through the portable display stop-request status.

There are currently **no command-line options**. Duration, clients, and drawing
parameters are source constants described below.

### Headless run

Use SDL's dummy video backend and software renderer when there is no desktop:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build/bin/sdl_fiber_example_v2
```

No window is visible, but the scheduler, request queue, framebuffer drawing,
presentation calls, and summary output still run.

### Expected behavior

The default display is 800 x 480, RGB565. Three 50 x 50 boxes alternate between
their colour and black:

| Client | Position | Colour (RGB) | Period |
|---|---|---|---|
| `white-1000ms` | `(10, 10)` | `(255, 255, 255)` | 1000 ms |
| `red-800ms` | `(70, 10)` | `(255, 128, 64)` | 800 ms |
| `green-500ms` | `(130, 10)` | `(64, 255, 64)` | 500 ms |

The terminal prints each served request and a final summary. A typical full run
ends with roughly:

```text
render      40 requests served, peak queue depth 1
white-1000ms  9 sent, 9 completed, 0 timed out
red-800ms    12 sent, 12 completed, 0 timed out
green-500ms  19 sent, 19 completed, 0 timed out
```

Counts, timestamps, and peak depth can vary with host load and early shutdown.
Inspect the timeout counts; they should normally be zero. The demo reports
timeouts in its summary but does not turn them alone into a failing exit code.

### Changing the emulated display

Geometry and pixel format belong to the backend, not to application initialization:

```sh
cmake -S . -B build-display32 \
    -DWMB_SDL_WIDTH=1024 -DWMB_SDL_HEIGHT=600 -DWMB_SDL_PIXEL_BITS=32
cmake --build build-display32 --target sdl_fiber_example_v2
./build-display32/bin/sdl_fiber_example_v2
```

The application obtains dimensions, format, and byte stride from the framebuffer
descriptor. Its drawing helper supports RGB565 and XRGB8888. The box layout does
not automatically rearrange for a smaller display; rectangles are clipped.

See the [HAL/backend integration guide](../sdl_drivers/README.md) for configuration
limits and portable embedded-backend integration.

## Files used by this example

### Application and direct project includes

| File | Purpose |
|---|---|
| [sdl_fiber_example_v2.c](sdl_fiber_example_v2.c) | Client definitions, request structure, shared queue, render/client fibers, startup, display pumping, and shutdown. |
| [CMakeLists.txt](CMakeLists.txt) | Creates the executable, links `wmb_hosted_display`, and enables compiler warnings. |
| [framebuffer_draw.h](../apps/common/framebuffer_draw.h) | Application-level RGB colour packing and clipped rectangle filling using the framebuffer's format/stride. This is software graphics code, not a driver API. |
| [hal_display.h](../../hal/hal_display.h) | Fixed-native display initialization, framebuffer metadata, damage publication, display progress/status, and shutdown. |
| [hal_task.h](../../hal/hal_task.h) | Cooperative task creation, waking, yielding, timed waiting, and destruction. |
| [hal_time.h](../../hal/hal_time.h) | Monotonic millisecond timestamps used for duration and logging. |
| [lists.h](../../framework/library/lists.h) | Intrusive queue operations and the `LIST_HEADER` member. |
| [fiber.h](../../framework/fiber/fiber.h) | Predicate waiting and notifications used to coordinate requests/completion. |

The source also includes standard C headers for logging, allocation failure
handling, and the POSIX `nanosleep` used by `host_idle_1ms`.

### Runtime and platform implementation

The [root CMake project](../../CMakeLists.txt) supplies `wmb_hosted_display`, which
combines the hosted WMB runtime with the selected display/input backend:

| Location | Role |
|---|---|
| [framework/fiber/](../../framework/fiber/) | Fiber/task runtime and waiting/notification primitives. |
| [framework/library/](../../framework/library/) | List implementation and common library/allocator support. |
| [fiber_linux/](../fiber_linux/) | Hosted context switching and stack implementation. |
| [system_linux/](../system_linux/) | Hosted monotonic time and allocation. |
| [sdl_drivers/](../sdl_drivers/) | Hosted implementation of the portable display/input HALs. |
| [sdl_library/](../sdl_library/) | Private SDL window/event/presentation implementation. |

The example itself does not include SDL headers or initialize keyboard/mouse
input. Display-only applications can use the display pump independently.

## How the request path works

```text
client timer
  -> allocate/fill draw_req_t
  -> append to g_queue
  -> notify and wait for request completion
render fiber
  -> wake when queue has work
  -> unlink requests
  -> draw and queue display damage
  -> mark requests done and notify waiters
client
  -> observe completion and free its request
main/root context
  -> schedule fibers and pump display presentation
```

Key implementation points in [the source](sdl_fiber_example_v2.c):

- `main` initializes the display, creates tasks **blocked**, wakes the renderer
  before the clients, then drives scheduling and presentation.
- `client_entry` waits for its client's period and toggles the requested colour.
- `client_dispatch` owns each allocation, submits it, and waits on that request's
  completion state before freeing it.
- `render_entry` is the single queue consumer and framebuffer writer. It drains
  available requests and notifies once per drain pass.
- `fill_rect` draws through the application graphics helper and queues damage.
- Completion means **drawing finished and damage was queued**, not that a display
  refresh/vsync has completed. The display pump performs physical presentation.

`RENDER_IDLE_TICK_MS` is an idle-wait backstop, not a two-second redraw period.
Notifications wake the renderer when work arrives. Likewise, client periods are
cooperative timing intervals, not hard real-time guarantees.

## Using this as a starting point

### Common changes

All names below are in [sdl_fiber_example_v2.c](sdl_fiber_example_v2.c) unless a
different file is linked.

| What to change | Where |
|---|---|
| Run duration | `DEMO_RUN_MS`; replace the timed main loop with your application's lifetime policy for a long-running program. |
| Number of clients | `DEMO_CLIENT_COUNT` and `g_clients`; keep the count consistent with the initializer. |
| Client timing, position, colour, or names | Entries in `g_clients`. |
| Rectangle size | `BOX_WIDTH` and `BOX_HEIGHT`. |
| Work generated by clients | `client_entry` and `client_dispatch`. |
| Request payload or new operation kinds | `draw_req_t`; add an operation selector/payload and dispatch it in `render_entry`. |
| Drawing primitives | `fill_rect` and [framebuffer_draw.h](../apps/common/framebuffer_draw.h), or replace them with your graphics library. |
| Completion/deadline behavior | `request_done`, `REQUEST_TIMEOUT_MS`, and the timeout branch in `client_dispatch`. |
| Renderer idle backstop | `RENDER_IDLE_TICK_MS`. |
| Stack sizes | The last argument of each task-creation call in `main`; zero selects the runtime default. |
| Display size/format | Backend configuration through CMake, not hard-coded application pixel types. |
| Executable name and libraries | [CMakeLists.txt](CMakeLists.txt). |
| Main-loop host pacing | `host_idle_1ms`; review this POSIX helper when moving to an embedded runtime. |

### Create another application in this repository

1. Copy this folder to a new application directory, for example under the hosted
   application tree.
2. Rename the source/executable and update its local CMake file.
3. Add the new subdirectory in the [root CMake project](../../CMakeLists.txt), in
   the display-enabled branch if it requires `wmb_hosted_display`.
4. Preserve or update the relative include for the shared graphics helper.

A minimal application CMake fragment, once the root targets exist, is:

```cmake
add_executable(my_application ${CMAKE_CURRENT_LIST_DIR}/my_application.c)
target_link_libraries(my_application PRIVATE wmb_hosted_display)
target_compile_options(my_application PRIVATE -Wall -Wextra)
```

For a separate repository, this example needs the fiber/task, list, allocation,
and time runtime **as well as** the display backend. Linking only
`wmb_display_input` is insufficient for its task APIs. Use the runtime targets
from the [root project](../../CMakeLists.txt) or integrate their modules explicitly;
do not copy this leaf CMake fragment into an otherwise empty project.

### Add keyboard or pointer input

Include [hal_input.h](../../hal/hal_input.h), initialize input after display setup,
pump it in the serialized main/HAL context, and consume complete command/data
pairs. Shut input down before the display. Keep input interpretation in application
code and retain the render fiber as the only framebuffer writer. The
[HAL guide](../sdl_drivers/README.md) describes the protocol and typed controls.

### Invariants to preserve

- Keep `LIST_HEADER` first in a request: list operations rely on that layout.
- Do not free a queued or in-flight request while the renderer can access it.
  On timeout, a successful unlink proves it was not taken; otherwise the current
  code waits for completion before freeing it.
- Initialize requests completely before queue insertion, and mark completion
  only after serving them.
- Preserve notification plus predicate waiting; a delay alone is not an
  acknowledgement mechanism.
- This design assumes serialized, cooperative execution on one OS thread.
  Moving producers/consumers to preemptive threads or ISRs requires appropriate
  synchronization and an explicit ownership model.
- Retain display pumping and orderly shutdown after a stop request or error.

For production use, add explicit handling for every initialization/allocation
failure, define queue/backpressure limits, and strengthen shutdown handling for
outstanding work. This executable is a small demonstration, not a complete
application lifecycle or transport implementation.

## Troubleshooting

- **Target not found:** check the configure output. If SDL2 was not found, the
  root project skips this example. Install its development package and configure
  again. Also ensure you configured the repository root.
- **No desktop/display available:** use the dummy/software headless command.
- **Blank or missing boxes after changing geometry:** the configured display may
  be smaller than the fixed box positions; change `g_clients` or the layout.
- **Timeouts or sluggish updates:** look for work that blocks or does not yield;
  keep render operations bounded and review notification/completion handling.
- **Unexpected pixel layout:** always use framebuffer format and byte stride.
  Odd RGB565 widths may include row padding.
