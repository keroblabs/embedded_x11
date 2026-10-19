# KDrive / HAL implementation checkpoint

**Branch:** `wmb_kdrive_linktest`  
**Scope:** KDrive DDX + SDL input/display adapters + isolated tests (no TinyX server yet).

## Design boundary

The existing additive `tinyx/kdrive/tima/` DDX is the only KDrive code modified.
Upstream `tinyx/kdrive/src/`, `tinyx/dix/`, `tinyx/fb/`, `tinyx/mi/`,
`libX11/` and `jwm/` remain unchanged.

- WMB boot will initialise `hal_display_init` and `hal_input_init` **once**.
  The DDX merely obtains `hal_display_get_fb()` and drains `hal_input_*`.
- The HAL owns framebuffer allocation and deallocation. DDX owns only
  `KdCardInfo->driver` and `KdScreenInfo->driver` metadata.
- Draw through upstream `fb/`; SDL/presentation are never called by the
  DDX. The host currently repaints the full texture periodically; the
  damage union is recorded but not consumed by the SDL presenter.
- `hal_input_pump()` runs in the HAL/host fiber; `KdOsFuncs.pollEvents`
  drains packets and enqueues X events on the dedicated X-server fiber.
  Direct calls to `ProcVector` or SDL from the DDX are prohibited.
- The first framebuffer format is RGB565, with correct stride and depth.
  XRGB8888 uses **depth 24**, storage **32 bpp**. No 4bpp/palette target
  conversion is implemented in this checkpoint.
- This DDX uses direct FB scanout with a software cursor; it does not
  allocate a KDrive shadow framebuffer. For banked 4bpp VGA, WMB will
  need a separate render-vs-physical-scanout policy above the backend.

## Input contract and current limitations

`hal_input.h` owns the command/value byte protocol. Each command and
its payload are exactly two bytes. SDL backend writers now check capacity
and append the entire pair, dropping a new packet instead of overwriting
half of an older one. This is safe under the **current cooperative-only**
scheduler. If preemption is enabled, the check+append must be placed in
one critical section before enabling the tick (D-47/R2).

**Important packet semantic clarification:** `HAL_CMD_MOUSE_BUTTON` now
carries the **full held-button bitmask** (`HAL_MOUSE_MASK_*`), rather than
a single button id. This enables simultaneous button presses and correct
release behaviour. MC68010 input backends must generate this same value.
`HAL_MOUSE_BUTTON_*` remain physical button ids, not the packet encoding.

The DDX submits signed relative mouse deltas to
`KdEnqueueMouseEvent(..., KD_MOUSE_DELTA, dx, dy)`.
Vertical wheel events become press/release pulses of X11 buttons 4/5.
The retained upstream `kinput.c` processes only five buttons: **horizontal
wheel buttons 6/7 and side-button-to-X11 mapping are deferred**.
Core buttons 4/5 are reserved for wheel up/down, not X1/X2.
Key translation covers the fixed ASCII/core-keyboard subset. Text input
composition, international keyboards and advanced input methods are out
of scope. Large per-event relative deltas are currently clamped to
`[-127,+127]` by the SDL adapter and must be revisited if needed.

## Build and execute

From a checkout of `wmb_kdrive_linktest`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target wmb_kdrive_linktest
./build/bin/wmb_kdrive_linktest
ctest --test-dir build -R wmb_kdrive_linktest --output-on-failure
```

`wmb_kdrive_linktest` compiles the **real four DDX .c files**, including
the real upstream `kdrive.h`, but links them to an intentionally tiny
mock HAL and mock TinyX entry points. It contains **no SDL link dependency**.
It asserts framebuffer geometry/depth/stride, the registration hooks,
keyboard down/up, signed relative mouse motion, simultaneous primary buttons,
button release, vertical-wheel pulse/release and OS polling.

The visual test is present only when SDL2 development libraries are found:

```sh
cmake --build build --target wmb_hal_smoke
./build/bin/wmb_hal_smoke --seconds 15
```

Expect an 800x480 RGB565 window, a gradient and a moving white square;
keyboard, pointer and wheel actions print HAL command/value packets.
The visible test is **not** run in CTest by default because it requires a
working display/SDL video driver. Run it interactively and report results.

## Remaining before an X11 root window

This checkpoint **does not** link `tinyx/dix`, `tinyx/fb` or `libX11`.
It does not initialise or run the TinyX server. The next stage must
complete `dix-config.h`/`kdrive-config.h`, build TinyX, implement the
cooperative `WaitForSomething` replacement and OS compatibility layer,
and drive the normal TinyX `Dispatch()`. `wmb_main()` readiness semantics
remain D-43.

Known follow-ups: SDL damage-presentation optimisation; more robust input
packet boundaries under preemption; possible `KdBlockHandler` polling
frequency; optional richer mouse/wheel semantics; DDX full server test
including cursor and framebuffer exposure; colour conversion for indexed
MC68010/TVGA9000i VGA modes.

**Test status:** Prepared for local compile/runtime validation; not executed
or verified in this commit. If a toolchain exposes missing include paths or
other CMake/TinyX header issues, treat those as validation findings, not as
tests having passed.
