# 43 - KDrive and the low-layer HAL

## 1. Architecture and ownership

KDrive uses the new low-layer display/input HAL. Its device-dependent integration
is the generic [HAL port](../tinyx/kdrive/hal/README.md), not a named target OS or
a second proprietary driver API.

```text
TinyX DIX/MI/FB server services
    -> KDrive core and generic HAL port
        -> hal_display.h / hal_input.h
            -> hosted SDL implementation or embedded implementation
```

| Component | Owner |
|---|---|
| [Display/input contracts](../hal/) | Platform-neutral native framebuffer metadata, nonblocking publication/status, byte streams, and typed controls |
| [KDrive core](../tinyx/kdrive/src/) | Card/screen/input framework, X keycodes/events, modes, colormaps, and optional shadow helpers |
| [Generic HAL port](../tinyx/kdrive/hal/) | KDrive-to-HAL screen/input/lifecycle integration |
| [Hosted backend](../linux_hosted/sdl_drivers/README.md) | SDL resources/events/presentation below the HAL |
| Embedded backend | Board-specific framebuffer/transfer/input implementation of the same contracts |
| TinyX server core/runtime/transport | [Server module](42-tinyx-module-plan.md), [framework](44-framework-module-plan.md), and [transport](21-inprocess-transport.md) |

No KDrive/HAL port source includes SDL or a hosted backend header. The HAL owns
native geometry, format, memory, and hardware synchronization; application
rendering and X-server behavior remain above it.

## 2. Current implementation status

The `wmb_kdrive` target now builds actual KDrive core sources and the generic HAL
port. The root CMake build includes it; it can also be built independently.
The [standalone harness](../tinyx/kdrive/hal/tests/kdrive_harness.c) links real
KDrive code and mocks only the HAL and required X-server services.

| Work | Status |
|---|---|
| Native display initialization and descriptor | Implemented; no application-requested resolution/pixel macro |
| X depth/bpp/masks and padded byte/pixel stride | Implemented and tested for RGB565 and XRGB8888 |
| Damage-to-HAL publication | Implemented through raw-region Damage registration/reporting |
| Keyboard byte-pair decoding and keymap | Implemented, including every printable ASCII byte and control keys |
| Modifier/private scancode separation | Implemented; modifier codes no longer overlap printable ASCII |
| Lock transitions | Press/release pulses; core lock state clears/reset correctly |
| Pointer deltas/buttons/releases/pressure | Implemented through real KDrive event generation |
| Wheels and extended buttons | Wheels use X buttons 4-7; physical side buttons use 8/9 |
| Core device initialization/error propagation | Implemented and tested |
| Poll/deadline/stop/error handling | Standard KDrive block-handler path tested |
| Fixed-mode RandR | One native mode, rotation zero; incompatible changes refused |
| Full TinyX startup/root window | Verified by the separate [P4 server gate](../tinyx/wmb/README.md), not by the standalone harness |
| Client dispatch | Still P5; no client is admitted by P4 |
| Real embedded device | Still a hardware port milestone |

The old adapter directory and identifiers have been removed. This is a generic
HAL integration, with no compatibility wrapper for the old named layer.

## 3. Source/build inventory

| Group | Files |
|---|---|
| Core library | `kdrive.c`, `kinfo.c`, `kinput.c`, `kcmap.c`, `kmode.c`, `kshadow.c` |
| Port | [hal_display.c](../tinyx/kdrive/hal/hal_display.c), [hal_input.c](../tinyx/kdrive/hal/hal_input.c), [hal_os.c](../tinyx/kdrive/hal/hal_os.c), [kdrive_hal.h](../tinyx/kdrive/hal/kdrive_hal.h) |
| Build | [port CMakeLists.txt](../tinyx/kdrive/hal/CMakeLists.txt), [test CMakeLists.txt](../tinyx/kdrive/hal/tests/CMakeLists.txt) |
| Project selection | [wmb_devel.cmake](../tinyx/wmb_devel.cmake) exports the shared `wmb_tinyx_project` target |
| Harness | [test cases](../tinyx/kdrive/hal/tests/kdrive_harness.c), [mock HAL](../tinyx/kdrive/hal/tests/mock_hal.c), [mock server services](../tinyx/kdrive/hal/tests/mock_xserver.c) |

The native memory mapper and native Linux/FBDEV/VESA drivers are not selected by
the HAL target. Server extension initialization and DIX/MI/FB implementations
remain server-core responsibilities.

`WMB_DEVEL=1` selects the project's TinyX changes, not just a KDrive backend.
The shared `wmb_tinyx_project` INTERFACE target propagates it PUBLIC through
`wmb_kdrive`, keeping conditional headers/structure layouts consistent in all
consumers. It applies to Debug/Release and hosted/embedded builds, not to the
HAL-only libraries or application capability profiles.

The selector guards every project improvement in the existing core, including
native FD/SIGIO, VT, and shell exclusions. An undefined macro preserves reference
paths; any definition, including zero or an empty value, selects WMB.
Follow [the TinyX methodology](47-tinyx-project-methodology.md) for future
project changes, including Linux/threading removal.

## 4. Standalone verification gate

```sh
cmake -S tinyx/kdrive/hal -B build-kdrive -DWMB_KDRIVE_BUILD_TESTS=ON
cmake --build build-kdrive
ctest --test-dir build-kdrive --output-on-failure
```

Or, from the root build:

```sh
cmake -S . -B build -DWMB_KDRIVE_BUILD_TESTS=ON
cmake --build build --target wmb_kdrive wmb_kdrive_harness
ctest --test-dir build -R '^kdrive_hal_' --output-on-failure
```

SDL is not a harness dependency. The HAL mock supplies native framebuffer memory,
controlled failures, and input bytes. The server mock uses real repository types,
supplies device/damage services, and observes events emitted by real KDrive.

| Scenario | Gate |
|---|---|
| Framebuffer | Factories, native geometry, both formats, exact pitch/pixel stride, masks, incompatible requests |
| Damage | Registration, multi/empty regions, flush rectangles, enabled pixels, optional power, destruction |
| Keyboard | Full printable range, control keys, modifiers/locks, fragments, malformed packets |
| Pointer | All five physical buttons, releases, signed deltas, pressure, four wheel directions, held-button preservation |
| Lifecycle | Initialization ownership, actual polling/deadline path, earlier deadline, stop/error/finalization |
| Faults | Display/input/control/allocation/damage failures and actual device initialization propagation |
| Core | Keycode/bitset endpoints, release, pointer matrix, native FD rejection |
| Arguments | Real argument dispatch and unsupported native option rejection |

Run the same scenarios with ASan/UBSan; instructions are in the
[module README](../tinyx/kdrive/hal/README.md).

The harness build also compiles the reference core with `WMB_DEVEL` undefined
and the project core with one, zero, and empty definitions. Header probes check
nine/wide versus seven/byte button layouts and
PUBLIC project-definition propagation. These checks do not exercise native
drivers.

### What this gate does not claim

Unused full-server paths are isolated using function/data sections and linker
garbage collection. Complete `KdInitOutput`/DIX/MI startup, actual graphics
operations, real Damage internals and server event processing, transport/client
dispatch, and JWM are not executed by this harness.

Call the result **standalone-verified KDrive/HAL integration**, not a completed
TinyX image. It is stronger than syntax-only checks and HAL-only tests.

## 5. Project-guarded core corrections

The real-core harness exposed assumptions that could not be tested below KDrive:

- Key-state storage must include the highest accepted absolute X keycode.
- Lock state must clear on a toggle/reset, not only accumulate set bits.
- Pointer initialization uses a one-based map with room for the last entry.
- Pointer masks/event loops must cover nine buttons, not truncate or stop at five.
- Device initialization must propagate callback/service failure.
- The polling deadline must not replace an earlier pending deadline.
- Native FD/signal/shell handling is not a HAL device implementation.

These engine changes stay under `WMB_DEVEL` with unchanged reference alternatives.
ASCII/control-to-private-scancode adaptation lives entirely in the HAL port;
the core has no ASCII-specific field or remapping bypass. Event initialization,
safe acceleration magnitude, and lifecycle/reset hardening are also guarded.
They are tested alongside the port, and upstream copyright/license notices
remain intact.

## 6. Next TinyX integration milestones

1. **Done at P4:** link the actual server core with `wmb_kdrive` and the selected HAL.
2. **Done at P4:** execute real initialization and display an X root window.
3. **Done at P4:** real framebuffer drawing produces Damage and presentation.
4. **Done at P4:** connect the cooperative wait/dispatch loop to the standard
   `KdBlockHandler` polling/deadline path. A 20 ms poll bound is a maximum;
   earlier timeouts are retained.
5. Verify actual clients receive keyboard/pointer/wheel events and clean shutdown.
6. Repeat on a board implementation of the same low-layer HAL.

The driver does not implement its own blocking scheduler or a proprietary wait
API. The server/runtime consumes KDrive's deadlines using the cooperative task
primitive defined by [the task contract](12-platform-contract.md).

## 7. Invariants

- Device/framebuffer lifetimes are explicit; no freeing HAL-owned pixels in KDrive.
- A fixed native display is not silently substituted for an incompatible X screen.
- Software rendering and X keymap policy are not hardware APIs.
- Input consumers reassemble pairs and preserve partial state between polls.
- Wheel pulses do not release an independently held physical button.
- Serialized controller/server contexts are required; an ISR calls only its
  synchronized backend producer, never the DDX.
- Full-server completion is measured by real rendering/input/dispatch tests,
  not inferred from isolated harness passes.

See [the overall implementation plan](40-implementation-plan.md) for phase
ordering and [the verification plan](41-verification-plan.md) for full-stack gates.
