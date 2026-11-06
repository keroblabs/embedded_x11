# KDrive on the display/input HAL

This is the generic KDrive device-dependent port. It uses
[hal_display.h](../../../hal/hal_display.h) and
[hal_input.h](../../../hal/hal_input.h), not a proprietary platform driver layer.

```text
TinyX server core
    -> KDrive core + generic HAL port
        -> display/input HAL
            -> hosted SDL implementation or embedded implementation
```

The port has no SDL headers, window IDs, native input device paths, or
platform-specific framebuffer addresses. Device configuration belongs below the
HAL. KDrive obtains native geometry, pixel format, pixels, and byte stride after
display initialization.

## Build the standalone harness

From the repository root:

```sh
cmake -S tinyx/kdrive/hal -B build-kdrive -DWMB_KDRIVE_BUILD_TESTS=ON
cmake --build build-kdrive
ctest --test-dir build-kdrive --output-on-failure
```

Or use the full repository build:

```sh
cmake -S . -B build -DWMB_KDRIVE_BUILD_TESTS=ON
cmake --build build --target wmb_kdrive wmb_kdrive_harness
ctest --test-dir build -R '^kdrive_hal_' --output-on-failure
```

The standalone build needs CMake 3.16+, a GNU-compatible C11 compiler/linker,
the repository's X-server headers, and the C/math library. **SDL and the full
TinyX server are not needed.** The harness exits zero on success and reports
the failing assertion otherwise.

Individual scenarios can also be run directly:

```sh
./build-kdrive/tests/wmb_kdrive_harness keyboard
./build-kdrive/tests/wmb_kdrive_harness pointer
./build-kdrive/tests/wmb_kdrive_harness damage
```

## What is built

The `wmb_kdrive` static library contains actual
[KDrive core sources](../src/): card/screen/pointer factories, input/event
processing, screen/argument code, colormaps, modes, and shadow helpers.
The native device mapper is not part of this HAL build.

| Port file | Responsibility |
|---|---|
| [kdrive_hal.h](kdrive_hal.h) | KDrive port tables and input lifecycle/poll declarations. This is an X-server integration header, not an application HAL header. |
| [hal_display.c](hal_display.c) | Card/screen setup from HAL metadata, correct X depth/stride, damage publication, optional power, and resource cleanup. |
| [hal_input.c](hal_input.c) | HAL command/data decoding, ASCII keymap/modifiers, relative motion, pointer pressure/buttons, and wheel translation. |
| [hal_os.c](hal_os.c) | Standard DDX entry points and the KDrive OS polling table; HAL ownership and stop/error handling. |
| [CMakeLists.txt](CMakeLists.txt) | Real core/port library and optional harness build. |

The low-layer implementations still live outside KDrive. A real server links
`wmb_kdrive` with its chosen HAL implementation (for example `wmb_display_input`)
and the actual TinyX server services.

`wmb_kdrive` publicly consumes `wmb_tinyx_project` from
[wmb_devel.cmake](../../wmb_devel.cmake), exporting `WMB_DEVEL=1` to all
consumers of its conditional KDrive headers. This selects the project variant
in Debug **and Release**, for hosted and embedded builds; it does not configure
the HAL backend. Future TinyX targets must consume the same shared target.

## What the harness verifies

The [harness](tests/kdrive_harness.c) links the **real KDrive library**.
It does not replace KDrive event generation with test stubs.

Building it also compiles the reference core with `WMB_DEVEL` undefined and the
project core with one, zero, and empty definitions.
The [header probes](tests/kdrive_header_probe.c) check the project/reference
button capacity and state layout, including propagation of the project flag to
a consumer. These are compile checks, not native-device runtime tests.

Only the lower [HAL](tests/mock_hal.c) and the required
[X-server services](tests/mock_xserver.c) are simulated. The latter use the
repository's real screen, device, event, region, and damage structures, capture
events passed to MI, and supply device initialization and damage callbacks.

| CTest scenario | Coverage |
|---|---|
| `kdrive_hal_framebuffer` | Real card/screen factories, RGB565 and XRGB8888, actual padded row pitch/pixel stride, colour masks, fixed geometry, and single-screen constraints. |
| `kdrive_hal_damage` | Damage callback registration, multiple/empty regions, forwarding rectangles to HAL flush, preserved pixels on enable, power results, and callback destruction. |
| `kdrive_hal_keyboard` | Every printable ASCII byte, control keys, modifier combinations, lock transitions, fragmented pairs, and invalid packets through real `KdEnqueueKeyboardEvent`. |
| `kdrive_hal_pointer` | Five physical button IDs, releases, relative motion, pressure, all wheel directions/ticks, side-button preservation, and fragmented pairs through real `KdEnqueueMouseEvent`. |
| `kdrive_hal_lifecycle` | Input ownership, actual block-handler polling, 20 ms poll deadline, earlier timeout preservation, disable/finalize, and display stop/error signaling. |
| `kdrive_hal_faults` | HAL initialization/control failures, card allocation failure, invalid framebuffer data, screen/damage failures, and real input-device startup error propagation. |
| `kdrive_hal_core` | Highest supported keycode/bitset bounds, key release, mouse transformation, and rejection of native input FDs in the HAL profile. |
| `kdrive_hal_arguments` | Real KDrive argument dispatch and rejection of native input/VT/shell options. |

For sanitizers:

```sh
cmake -S tinyx/kdrive/hal -B build-kdrive-sanitize \
    -DCMAKE_C_FLAGS="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-kdrive-sanitize
ctest --test-dir build-kdrive-sanitize --output-on-failure
```

## Integration rules

- The HAL port supports one fixed display and one pointer. It rejects incompatible
  screen geometry, pixel format, rotation, or an additional screen.
- RGB565 maps to X depth 16/bpp 16; XRGB8888 maps to X depth 24/bpp 32.
  Pixel stride is derived from the **actual byte pitch**, not from visible width.
- X-server rendering writes the HAL-owned framebuffer. A raw-region Damage
  callback queues the changed rectangles through the HAL. Device transfer,
  cache coherency, and presentation remain below the HAL.
- Pointer packets are relative deltas. KDrive performs coordinate transformation
  and acceleration; the port does not maintain a competing absolute position.
- Physical buttons map to X buttons 1/2/3/8/9. Vertical wheel ticks generate
  press/release pairs on 4/5; horizontal ticks use 6/7. Wheel pulses preserve the
  currently held physical button.
- All key scancodes are private: printable bytes use 128-222, control keys
  223-229, and modifiers 230-235. The keymap produces X keycodes 8-115 and avoids
  the legacy PC/numpad remapping without an ASCII flag in the core.
  Lock-state changes generate a press/release pulse, not a held lock key.
- The selected library uses the shared `WMB_DEVEL=1` project selector. Native
  SIGIO/FD, virtual-terminal, and shell-switch handling are excluded/rejected;
  only undefined reference builds retain their original paths and layout.
  Any definition, including zero or an empty value, selects the project path.
- All project improvements in existing core files are guarded by that selector.
  Device policy stays in the port; the [methodology](../../../docs/47-tinyx-project-methodology.md)
  applies to future TinyX Linux/threading and runtime changes too.
- KDrive's standard block handler polls the port and supplies a maximum 20 ms
  wait deadline, without overriding an earlier pending deadline. Implementing
  the actual cooperative wait belongs to the TinyX/runtime integration.
- Callbacks and queue consumers must be serialized. Hardware interrupt producers
  synchronize their HAL queues; they do not call KDrive from an ISR.

Keyboard LEDs/bell and hardware cursor/acceleration hooks are not provided by
these HAL contracts. The port selects software cursor rendering and TrueColor,
so no hardware palette interface is exposed.

## Status and boundaries

This is **standalone KDrive/HAL module verification**, beyond syntax checks or
HAL-only tests. The generic port and tested real-core paths are built and exercised.

The harness uses function/data sections and linker garbage collection to isolate
the exercised paths from unused full-server entry points. It does **not** execute
the complete `KdInitOutput`/DIX/MI server startup, real framebuffer rendering
operations, a real server event queue, client protocol dispatch, JWM, or transport.
The simulated Damage service verifies the port's registration/report/lifetime
contract, not the server's whole damage implementation.

The next integration milestone is a real TinyX root window and client-driven
rendering/input, including the cooperative server wait path. Passing this harness
is a strong module-level gate, not a claim that the entire X11 stack is complete.

See [the module plan](../../../docs/43-kdrive-module-plan.md) and
[the low-layer HAL contract](../../../docs/12-platform-contract.md).
Retain the applicable repository/upstream license notices when redistributing.
