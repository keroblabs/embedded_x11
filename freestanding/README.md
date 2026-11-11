# P9a: freestanding cross-build proof

**P9a is separate from P9b.** This builds and verifies the actual windowing
cohorts for bare-metal ARM, without selecting or claiming a working board.
The approved reference is **Cortex-M3, Thumb, little-endian ILP32, soft float**,
using Arm GNU 14.2 and full newlib. The user approved full newlib after the
installed nano formatter proved insufficient for WMB's C99 diagnostics.

The supplied platform stubs are deliberately **non-runnable**. They record the
missing port operation and stop; they do not fake a working display, clock or
context switch. Do not flash these images as board firmware. There is no reset
vector, board initialization, real scheduler port or hardware acceptance test.

## Build and verify

Put `arm-none-eabi-gcc` and its matching binutils on `PATH`. The toolchain must
provide newlib, libm and libgcc. A native C compiler builds the vendored
`makekeys` utility; Python 3.10+ and GNU patch run verification/build steps.
No global package installation is performed by CMake.

From the checkout root, in a fresh build directory:

```sh
cmake -S . -B build-p9a-arm \
    -DWMB_PROFILE=p9a \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-p9a.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-p9a-arm -j4
ctest --test-dir build-p9a-arm --output-on-failure
```

Repeat with `-DCMAKE_BUILD_TYPE=Debug` in a different directory for assertion-
enabled closure. The default profile is still `hosted`; cross-compiling it is
an error rather than accidentally finding Linux/SDL libraries.

`wmb_p9a_verify` is an **ALL build target**. A build does not succeed merely
because a minimal executable links. Verification must also pass.

## Outputs

| Output in `build-p9a-arm/` | Purpose |
|---|---|
| `bin/wmb_p9a.elf` | Fixed-base component link with section GC |
| `bin/wmb_p9a.bin` | Raw load image, beginning with the actual immutable API table |
| `bin/wmb_p9a_whole.elf` | Fourteen complete archives, **without section GC or LTO** |
| `bin/wmb_p9a_app.elf` | Separate SDK-only ARM hello application, not another Xlib/server |
| `transport/api/sdk/libX11-app.a` | Actual 15,600-byte target SDK archive, below 16 KiB |
| `wmb_p9a*.map` | Complete link maps/cross references |
| `p9a-verification.json` | Compiler, timestamp, floor, image hashes and actual memory spans |

These are component/application load artifacts, **not standalone boot images**.
The 508-byte table is at the component load base; it is not an MCU vector table.
P9b must implement the startup/loader and runtime ownership configuration.

## What is actually verified

[check_freestanding.py](../tools/check_freestanding.py) checks:

- **544 actual ARM translation units**, including the retained server, Xlib,
  JWM, fonts, framework, dependencies and independent SDK/application units.
  The windowing source inventories match the hosted manifests.
- Freestanding/no-builtin/non-PIC flags, actual ELF32 ARM object headers,
  real dependency files and object freshness. No host header may escape the
  cross sysroot; client/SDK units cannot receive server ABI prefixes.
- The native generator is not compiled as an ARM executable or linked into
  the target. SDL/Linux/thread/socket scaffolding and hosted executables are
  not part of this build.
- Real library-floor providers, no unreviewed imports, no libnosys/semihosting
  runtime, no forbidden OS symbols or atomic-library helpers, and no unresolved
  symbols or dynamic relocations.
- All fourteen complete archives' definitions survive the no-GC proof.
  Comment-section removal is not confused with discarded executable code.
- Exactly one boot `main`, immutable ABI header/count/layout, and all 123
  non-NULL Thumb function pointers pointing into actual code.
- The application binding uses the same load base, has disjoint ROM/RAM,
  and contains no retained Xlib/DIX implementation.
- Every file-backed section of the raw image matches the ELF payload, not just
  a valid leading magic value. Both images fit the explicit reference budgets.
- The raw SDK archive is strictly below 16,384 bytes and has exactly its
  expected typed thunk exports plus `wmb_bind`.

The second CTest runs 13 negative verification tests: corrupted ELF/ABI/raw
payload, missing slots, wrong binding/architecture, unresolved/forbidden
symbols and unreviewed floor requirements. It also reruns the **actual linker**
with insufficient ROM/RAM and without the regex provider, requiring failures.

The result proves compile/link closure, **not execution on ARM**. Native tests
below exercise the new floor code without disguising stubs as hardware.

## Library floor and allocator scope

[floor-symbols.txt](../docs/floor-symbols.txt) is an audited allow-list derived
from the complete archives before libc/compiler-runtime resolution. Release
requires 81 entries; Debug requires 79, including `__assert_func`. Their reviewed
union contains 82 entries. Checking an already fully linked ELF's empty
undefined list would not be a dependency census.

The selected floor is:

| Component | Selection |
|---|---|
| C library/math/compiler support | Actual toolchain full newlib, libm and libgcc; C99/size/long-long and floating formatting retained |
| Font decompression | Hash-pinned zlib **1.4**, six inflate/checksum utility units, `Z_SOLO`; no file/gzip I/O backend |
| JWM regular expressions | Hash-pinned BSD-licensed TRE **0.9.0**, ten byte-oriented POSIX matcher units; no locale, wide or approximate matching |
| Console/arena/termination glue | [runtime.c](p9a/runtime.c), bounded linker arena and explicit console-only newlib hooks |
| Hardware/context/time services | [platform_stubs.c](p9a/platform_stubs.c), intentionally fail-fast until P9b |

The source archives and hashes are declared in
[WmbZlib.cmake](../cmake/WmbZlib.cmake) and
[WmbRegex.cmake](../cmake/WmbRegex.cmake); notices/licenses remain in the fetched
sources. Neither upstream CMake/autoconf build is run with host feature guesses.
TRE's release includes a host-generated configuration header: CMake replaces
that generated file with the explicit reference configuration.

All selected dependency allocations retain WMB owner attribution. TRE's real
compatibility header supplies its types; it is not a fake replacement POSIX API.
Its `sys/types.h` inclusion is for libc's `ssize_t` only, including in the real
newlib build. The host include audit permits exactly that data-header edge in
the copied TRE header, never socket/process/service headers or arbitrary
`sys/types.h` includes in JWM.

Fault injection found a real TRE tag-construction OOM assertion. The checked,
zero-fuzz [dependency patch](../cmake/patches/tre-0.9.0-oom-tags.patch) returns
the error through cleanup before completing partial tags; assertions are not
disabled to hide it. Real JWM matching is compared with stock POSIX matching,
and tracked allocation/metadata failures must release every block.

The probe uses the **existing `hal_alloc` seam**, backed by newlib malloc/free
and its bounded arena, as explicitly approved for P9a. Historical D-29's claim
that the verified hosted stack used legacy `heap.c` was not the actual
implementation; the hosted backend uses libc too. This is not a silent legacy
allocator rewrite or a production board heap/locking decision.

The installed nano library disables `%zu`/`%llu` support and uses a different
reentrancy layout from full newlib. P9a therefore does not combine incompatible
formatter objects or rewrite diagnostics to hide missing format support.
The reference boot compilation requires the matching full-newlib feature
configuration. A small rebuilt libc remains a later size optimization.

The arena and console adapter tests are single-owner tests. P9b must establish
the real allocator/newlib-state/diagnostic locking policy for preempting platform
callers, interrupt rules and actual memory ownership.

## Synthetic memory map

Defaults are reference limits, not a selected chip specification:

| Region | Base | Capacity/reservation |
|---|---|---|
| Component ROM/API | `0x08000000` | 4 MiB |
| Component RAM | `0x20000000` | 8 MiB total |
| Component heap | Linker-aligned within RAM | 4 MiB reserved |
| Component/main stack area | Following heap | `0x210000` reserved; **no stack-pool implementation** |
| Separate application ROM | `0x08400000` | 4 MiB |
| Separate application RAM | `0x20800000` | 1 MiB |
| Application heap/stack | Within application RAM | 512 KiB / 64 KiB reserved |

CMake cache variables `WMB_LOAD_BASE`, `WMB_ROM_BYTES`, `WMB_RAM_BASE`,
`WMB_RAM_BYTES`, `WMB_HEAP_BYTES`, `WMB_STACK_BYTES`, `WMB_APP_BASE` and
`WMB_APP_*` set these explicitly. The linker and verifier enforce capacities
and disjoint component/application regions. Do not reinterpret a successful
synthetic budget check as fitting a particular board or framebuffer.

Debug ELF file size includes debug metadata and is not the load-image size.
RAM figures include fixed heap/stack reservations, not measured target heap
usage. There is no framebuffer reservation because no real display is selected.
See [the recorded P9a measurements](../docs/31-target-build.md).

## Native validation of the same floor

Keep the ordinary hosted build as the regression baseline. To exercise the
exact TRE/zlib source configurations in the real desktop:

```sh
cmake -S . -B build-p9a-floor -DWMB_TINYX_BUILD_SERVER=ON \
    -DWMB_USE_BUNDLED_FLOOR=ON \
    -DWMB_KDRIVE_BUILD_TESTS=ON -DWMB_SDL_BUILD_TESTS=ON
cmake --build build-p9a-floor -j4
ctest --test-dir build-p9a-floor -LE endurance --output-on-failure
```

The normal hosted baseline passes 52 tests. The bundled-floor build passes
54, including 210 stock-POSIX matching comparisons, OOM and console/arena/fatal
tests; the same 54 pass strict ASan/UBSan with padded 319x177 RGB565.
No runtime behavior or timing threshold is relaxed for the sanitizer suite.

## What remains P9b

- Select the actual CPU/board, toolchain/memory map, display/input interfaces,
  clocks and integration host.
- Supply real reset/CRT initialization, load placement and SDK entry/ownership.
- Implement device/time/interrupt/fiber context switching and bounded stack
  allocation. The portable framework itself is not a board driver.
- Finalize allocator and newlib/console concurrency policy, real framebuffer
  and stack/heap budgets.
- Execute the client/desktop/input gates and a real hour on hardware.

The actual compiler/libc floor and budget must be rechecked if the board ABI,
toolchain or features differ. P9a does not close Q6 (the unknown hardware).
