# 47 - TinyX project-change methodology

## 1. Project selector

Defining `WMB_DEVEL` selects this project's TinyX variant. It covers the generic HAL
integration, necessary engine improvements, and future removal of native
Linux/process/thread dependencies.

Despite its name, it is **not a Debug-build flag**. Keep it enabled in Debug and
Release, on hosted and embedded platforms. It is not an SDL selector or another
application capability/profile macro. The reusable
[display](../hal/hal_display.h) and [input](../hal/hal_input.h) contracts do not
depend on it.

An undefined `WMB_DEVEL` selects the reference behavior in modified upstream
files. Any definition, including `WMB_DEVEL=0` or an empty value, selects WMB.
Reference builds do not select the WMB-only HAL port or other project replacement
files.

## 2. Adapter first, guarded engine changes second

| Change | Location |
|---|---|
| Framebuffer geometry/format/lifetime, damage publication, input decoding, private scancodes, button/wheel translation, device controls | [KDrive HAL port](../tinyx/kdrive/hal/) |
| Hardware/SDL resources, cache synchronization, transfer, interrupt-side input queues | Implementation below the platform-neutral HAL |
| KDrive data-layout limits, event bookkeeping, device startup/error propagation, polling deadlines | Small guarded changes in the existing [KDrive core](../tinyx/kdrive/src/) |
| Server lifecycle, transport, cooperative waiting, or dependencies with no DDX seam | The responsible TinyX subsystem or project-owned replacement file, not the display/input driver |

Reuse existing KDrive/DDX seams before editing an engine file. Do not copy
upstream engine functions into the HAL directory just to conceal a core diff.
Do not add driver-policy flags to core structures when the port can adapt its
own data.

Linux and threading removal follows the same rule: exclude an unnecessary
native file first; use a project-owned replacement with the same external
symbols where appropriate; otherwise guard the necessary upstream call site.
This selector does not itself make the entire server freestanding or remove
every threading dependency.

## 3. Guard style and reference preservation

For a project change in an existing upstream TinyX file:

```c
#if defined(WMB_DEVEL)
    /* Project implementation. */
#else
    /* Unchanged reference implementation. */
#endif
```

For a project-only addition to an existing file, use the same positive guard
without an `#else`. Guard related includes, declarations, and structure members
as well as executable statements.

Use presence-based checks: `#if defined(WMB_DEVEL)` for the project path and
`#if !defined(WMB_DEVEL)` for reference-only regions. The macro's value is not
tested; undefine it to disable WMB. Capability/profile checks can refine a
project branch, but cannot replace the project selector.

Keep changes local, retain the original alternative, and preserve upstream
copyright/license notices, including their original encoding. A generally
useful bug fix still stays under this project selector unless an independently
approved upstream change is being made.

New project-owned files have no upstream alternative to duplicate. Select them
only in the project build; require the selector in their shared integration
header where layout/behavior depends on it. The HAL port does this in
[kdrive_hal.h](../tinyx/kdrive/hal/kdrive_hal.h).

## 4. One shared build definition

[wmb_devel.cmake](../tinyx/wmb_devel.cmake) defines the `wmb_tinyx_project`
INTERFACE target and exports `WMB_DEVEL=1`. The current
[KDrive build](../tinyx/kdrive/hal/CMakeLists.txt) links it **PUBLIC**, so the
library and consumers of its headers use the same variant.

Future TinyX targets must consume the same target, for example:

```cmake
include("${WMB_SOURCE_ROOT}/tinyx/wmb_devel.cmake")
add_library(wmb_tinyx_server STATIC ${TINYX_SERVER_SOURCES})
target_link_libraries(wmb_tinyx_server PUBLIC wmb_tinyx_project wmb_kdrive)
```

Header layout is part of the ABI: the project pointer has nine buttons and wide
button state; the reference pointer retains seven buttons and byte-sized state.
Every translation unit using these shared structures must agree, including
server services and tests. Do not mix objects compiled with different
defined/undefined states.

Do not use directory-global definitions or attach this selector to `wmb_hal`,
the SDL backend, or framework targets. HAL-only applications must not inherit
TinyX project policy. The WMB HAL target deliberately requires the project
variant; passing `-DWMB_DEVEL=0` is not a way to turn it into a native driver.

## 5. Current separation

- [hal_input.c](../tinyx/kdrive/hal/hal_input.c) maps the 95 printable HAL bytes
  to private scancodes 128-222, seven control keys to 223-229, and six modifiers
  to 230-235. The resulting X keycodes are 8-115.
- Those private codes avoid the legacy PC/numpad remapping slots. The core
  remapper and keyboard-function structure need no ASCII-specific flag.
- [kdrive.h](../tinyx/kdrive/src/kdrive.h) guards the extended button capacity,
  constants, and state layout.
- [kdrive.c](../tinyx/kdrive/src/kdrive.c) guards native VT/shell exclusions and
  project lifecycle/OS-initialization handling.
- [kinput.c](../tinyx/kdrive/src/kinput.c) guards native FD/SIGIO exclusion,
  key-state bounds/reset/lock corrections, pointer map/callback/startup
  corrections, event initialization, wide button processing, overflow-safe
  acceleration magnitude, and earlier-deadline preservation.
- P4 adds guarded server entry/lifecycle, OS initialization, native transport
  exclusion, extension observation and unsigned visual-mask generation. The
  generated configuration is force-included through `wmb_tinyx_config` before
  X protocol headers, preventing mixed server ABI layouts.
- P5 adds one guarded framebuffer arithmetic correction in
  [fbbits.h](../tinyx/fb/fbbits.h), exposed by real line rendering under UBSan.
  Transport policy stays in the owned OS/client adapters; `Dispatch()` is not
  patched. Client code uses its own matching headers and never inherits the
  server prefix or `_XSERVER64`.

The low-layer HAL still transports its original bytes and descriptors.
Scancode allocation and X-server policy are not new hardware APIs.

## 6. Verification and future changes

The [standalone harness](../tinyx/kdrive/hal/README.md) exercises actual KDrive
code. Building it also compiles the reference core with the selector undefined
and the project core with one, zero, and empty definitions. It checks the
project/reference structure layouts through
[compile probes](../tinyx/kdrive/hal/tests/CMakeLists.txt).

For each further TinyX change:

1. Identify the owning seam and first try the port/source-selection solution.
2. If a core edit is necessary, preserve the reference path under the guard.
3. Propagate the shared build target wherever conditional headers are consumed.
4. Run the smallest real-code tests for the changed behavior and compile the
   reference variants. For reference preservation, compare preprocessed code
   against a recorded pre-change baseline with matching compiler/configuration.
5. Run directly affected HAL consumers to detect policy leakage; update the
   module status/decision and preserve the upstream notices.

Keep Debug, Release, and ASan/UBSan gates. Compile probes and standalone tests
do not prove full TinyX startup, real rendering/Damage/event processing, client
dispatch, or embedded readiness; those remain integration milestones.

The separate [P4 server gate](../tinyx/wmb/README.md) now verifies actual startup,
root rendering, real Damage, cooperative dispatch/wait and cleanup. Separate
[P5 gates](../transport/x11/README.md) now verify client dispatch, replies,
events and backend input. Embedded readiness remains later; standalone passes
still must not be used as a substitute for either integration gate.

P6 now automates reference preservation across the
[complete guarded manifest](../patches/MANIFEST.md). Run
`python3 tools/check_portability.py --build-dir BUILD` after intentional changes.
It checks exact patch coverage, original notice bytes, pristine imported
reference compilation, and presence-based one/zero/empty equivalence, including
the framebuffer template consumer. Native client probes enable host feature/
annotation compatibility only inside the probe, not the portable build.
It also audits actual active includes and complete archives; a raw grep of
reference branches or a dead-stripped executable is not a portability proof.
See [the P6 guide](48-p6-portability.md) and D-54 before adding guard sites.

P7 extends this exact discipline to JWM and lifecycle (D-55). The only new
existing-TinyX engine site is [empty property storage](../tinyx/dix/property.c):
explicit free/clear replaces implementation-defined `realloc(ptr, 0)` in the
project branch, with the original reference alternative preserved. Display-stop
deferral stays in the generic HAL port, not core dispatch or public HAL contracts.
The current ledger has 44 sites; native compilation/preprocessing and
one/zero/empty proof now cover 35 affected units. `Dispatch()` remains unchanged.

This methodology is recorded in [D-51](90-decisions.md) and in the
[TinyX-scoped workspace instruction](../.github/instructions/tinyx-wmb.instructions.md).
