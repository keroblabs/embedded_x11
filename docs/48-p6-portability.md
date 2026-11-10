# P6 - Hosted portability closure

P6 removes the remaining native OS dependencies from the **selected windowing
source cohorts**, without changing the working P5 transport or `Dispatch()`.
This is not yet a freestanding image or a JWM integration.

This guide records the **P6 milestone**. [P7](../jwm/wmb/README.md) now adds JWM
and lifecycle, extending the same gates to 506 units, 44 guarded files and nine
whole archives. Its normal suite has 36 tests (37 with the optional native
reference). The P6 evidence/counts below are retained as milestone history,
not the current full-stack inventory.

## Reproduce the gates

From a repository clone containing the original import commit, with CMake
3.16+, Make or Ninja, a GNU-compatible C11 toolchain/linker, Git, `patch`,
Python 3.10+, SDL2 development files and zlib:

```sh
cmake -S . -B build-p6 -DWMB_TINYX_BUILD_SERVER=ON \
    -DWMB_KDRIVE_BUILD_TESTS=ON -DWMB_SDL_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build-p6
ctest --test-dir build-p6 --output-on-failure
./build-p6/bin/wmb_host --client-test
```

The existing `build-p4` directory also contains the current runnable host.
Generated files stay in the build tree. `compile_commands.json` is exported by
the root build; it is an input to verification, not a file to commit.

To run only the new checks:

```sh
ctest --test-dir build-p6 --output-on-failure -R '^tinyx_p6_'
python3 tools/check_portability.py --build-dir build-p6
```

| Test | What it proves |
|---|---|
| `tinyx_p6_detachability` | All six real windowing/runtime archives resolve with `--whole-archive` and `--no-gc-sections`, contract stubs, libc/math/zlib, and **no SDL, hosted fiber/system backend, or host launcher** in the link |
| `tinyx_p6_portability` | Active include/symbol boundary, client/server/HAL flag isolation, exact patch coverage/budgets, original notice bytes, patch replay and reference variants |
| `tinyx_p6_gate_checks` | Negative cases for active-header ownership, system X11 leakage, versioned/native symbol parsing and license-byte corruption |

The full suite has **30 tests**. Debug RGB565, Release XRGB8888 at 319x177,
and strict ASan/UBSan with both padded RGB565 and XRGB8888 at 319x177 pass.
The P4 root, actual P5 clients/flow/errors/timeout, and independent stock-Xlib wire
comparisons remain gates. The client also checks actual glyph-cursor creation
and WM hints/identity, including ignoring a supplied `RESOURCE_NAME` environment
variable. A native SYNC `SERVERTIME` relative wait verifies the new deadline seam,
not just the older XTEST sleep path.

Final P6 Valgrind 3.22 checks also pass: the sixty-second `--client-soak` and
multi-client `--flow-test` each report **zero errors and zero lost bytes**, with
about 42 KiB of reachable server/font caches at exit. Use
`--max-stackframe=65536` to distinguish nearby fiber stacks; no memory-error
suppressions are used. The final normal Debug client measured 1.68% CPU and
49.7 Hz polling.

With Valgrind available, repeat the memory gate:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    valgrind --max-stackframe=65536 --error-exitcode=42 --leak-check=full \
    --errors-for-leak-kinds=definite,possible ./build-p6/bin/wmb_host --client-soak
```

Repeat with `--flow-test` for grabs, native SYNC waits and large-request replay.
The host CPU-performance gate is measured without memory instrumentation.

## What changed

- Server wait/block handlers exchange a typed `uint32_t` millisecond budget, not
  a POSIX `struct timeval`. All users still call `AdjustWaitForDelay`; earlier
  deadlines are preserved, including zero and values above the 32-bit range.
  SYNC's callback type and the shared `OSTimePtr` definition agree.
- Server native locks, process/privilege helpers, signal handling and signal
  timers are excluded in the `WMB_DEVEL` branch. Audit prefixes use HAL monotonic
  time rather than calendar time/process identity.
- Shared X11 headers have narrow standard-C branches. There are no fake POSIX
  headers or stand-in system typedefs.
- Xlib uses the retained overflow-checked `xreallocarray`, a fixed `WMB` identity,
  built-in/Unicode key names, caller-provided WM hints and the C locale.
  Native XCB/locking headers no longer sneak in through the public macro helpers.
- Unsupported file/default/locale backends are excluded. Bitmap-file and default
  resource APIs that remain exported diagnose unsupported use and return their
  documented failure result. In-memory bitmap/drawing APIs still work.
- The native `XCreateGlyphCursor` path is retained with dynamic Xcursor loading
  disabled. The whole-archive link exposed and removed duplicate owned
  `XLockDisplay`/`XUnlockDisplay` definitions; the retained threadless source
  already implements them.
- The font cohort is built-in-only, including its memory reader/decompressor.
  Native descriptor/directory/writer/catalogue/fontenc implementations are not
  selected. Directory access is explicitly refused. `libfontenc` is no longer a
  build/link dependency; zlib remains necessary for embedded compressed PCF data.

The frozen inventories are
[173 server units](reference/tinyx-sources.reference),
[248 client units](reference/xlib-sources.reference) and
[30 font units](reference/xfont-sources.reference). With the one standard-C bridge
unit, the active-header audit checks **452 actual translation units**, including
their transitive project/protocol/font headers.

## Guard and patch discipline

[The generated manifest](../patches/MANIFEST.md) and
[machine-readable ledger](../patches/manifest.json) contain **27 guarded files**:
15 TinyX, 10 libX11 and two shared X11 headers. The user-approved expansion from
the P5 budget is recorded in [D-54](90-decisions.md). JWM is not patched at P6.

`WMB_DEVEL`, `WMB_INPROC` and the font dependency's private
`WMB_FONT_BUILTINS_ONLY` use presence semantics. Zero and empty definitions are
not the reference variant. Existing TinyX changes still use only
`defined(WMB_DEVEL)` / `!defined(WMB_DEVEL)`.

The source baseline is the exact unmodified repository import
`73ab9f28bccf8e2ff56638896a4d82f079a2e5a7`. It is **not** labeled as a verified
official upstream release: that provenance was not recorded by the import.
Every file has before/after SHA-256 values. The test reconstructs original files,
runs `git apply --check` and applies each patch, then requires byte-for-byte
agreement with the current source, including original license notices.

The native-reference gate preprocesses and compiles 20 affected translation
units, including the framebuffer template consumer. Undefined-selector output
must match the pristine imported source. Source-location macros are normalized;
native client probes enable host I/O features and two legacy protocol-annotation
definitions only in the probe. Project one/zero/empty output must be identical.
The standalone KDrive build also compiles its original and one/zero/empty variants.

After an intentional upstream edit, update the ledger if needed, then regenerate:

```sh
python3 tools/check_portability.py --regenerate-patches
ctest --test-dir build-p6 --output-on-failure -R '^tinyx_p6_'
```

Do not regenerate merely to hide a hash mismatch. Read the actual source delta
and preserve its reference branch. The separately pinned libXfont release and
its three checked patches remain managed by
[WmbXfont.cmake](../cmake/WmbXfont.cmake).

## Scope of the negative claims

The gate scans **active compiler includes**, not a raw grep that would also
match intentionally preserved reference branches. It rejects native OS/XCB/
thread/backend headers originating above HAL, but does not mistake libc's own
implementation headers for application dependencies.

Symbol gates inspect every complete selected archive and the no-backend probe,
not only the normal executable after linker garbage collection. They reject
native transport, process, thread, dynamic-loading, environment, clock and
file-opening dependencies, including dormant code.

SDL and the real hosted fiber/time ports legitimately use their host OS below
HAL. The desktop executable is not a zero-socket process: SDL may initialize
D-Bus/udev even in dummy mode. These platform services are not WMB X transport.

## What remains

- **P7:** build/integrate JWM and verify decorated windows, menus and focus.
- **P8/lifecycle:** external ABI, foreign-thread admission, long stability and
  abandoned-Display cleanup/portable launcher work.
- **P9:** a real target toolchain and HAL, freestanding compile/link, allocator,
  console and termination floor, and hardware validation.

P6 deliberately retains hosted libc diagnostics/termination and the C/math/zlib
substrate. Direct TinyX/Xlib allocations are not all routed through HAL allocation.
Functions such as `strdup`, `strnlen`, `strcasecmp`, `strncasecmp`, `hypot`, `cbrt`
and the full observed math set still need an explicit target-floor disposition.
`stdio` diagnostics and `exit` need target adaptation. Passing P6 is not a claim
that these P9 obligations or the larger shipping-font registry are finished.
