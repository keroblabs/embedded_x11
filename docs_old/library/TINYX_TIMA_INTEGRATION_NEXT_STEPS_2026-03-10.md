# TinyX + Tima Integration: Missing Pieces and Build-System Roadmap

**Date**: 2026-03-10
**Scope**: Hosted SDL build first, then target-ready Tima OS integration

---

## 1) Verified Current State (as of this document)

### Build status
- `framework/CMakeLists.txt` now resolves local SDL from:
  - `/home/wifi/projects/SDL/include`
  - `/home/wifi/projects/SDL/build/build/.libs`
- Build reaches **link stage**.
- Current failure is now primarily **missing TinyX linkage**, not missing SDL headers.

### Linker evidence (current)
Unresolved symbols from `tinyx/kdrive/tima/*.o` include:
- `shadowSetup`, `shadowUpdatePacked`, `KdShadowSet`, `kdScreenPrivateIndex`
- `kdKeymap`, `KdEnqueueKeyboardEvent`, `kdMouseInfo`, `KdEnqueueMouseEvent`
- `KdCardInfoAdd`, `KdInitOutput`, `KdInitInput`, `KdOsInit`, `FatalError`

These symbols are expected to come from TinyX core libraries/modules, which are not yet linked in the current standalone target.

---

## 2) HAL Completion Audit (Display / Keyboard / Mouse)

## 2.1 Display HAL (`lcd_driver_*`)
**Status**: Mostly complete for hosted path.

Implemented in `framework/sdl_drivers/lcd_driver.c`:
- `lcd_driver_init`, `lcd_driver_deinit`
- framebuffer accessors (`get_framebuffer`, width/height, bpp)
- draw helpers (`set_pixel`, `fill_colour`, bitmap blit, scroll)
- update loop (`lcd_driver_update_window`)

Tima usage in `tima_kdrive_fb.c` is consistent with this API.

## 2.2 Keyboard HAL (`keypad_driver_*`)
**Status**: Complete for current KDrive adapter.

Implemented in `framework/sdl_drivers/keyraw_driver.c`:
- `keypad_driver_init`
- `keypad_driver_has_data`
- `keypad_driver_read_data`
- protocol generation for modifier + key up/down events

Tima usage in `tima_kdrive_kbd.c` is aligned with `bool_t keypad_driver_has_data(void)`.

## 2.3 Mouse HAL (`mouse_driver_*`)
**Status**: HAL implemented, Tima adapter still needs fixes.

Implemented in `framework/sdl_drivers/mouse_driver.c`:
- `mouse_driver_init`
- `mouse_driver_has_data`
- `mouse_driver_read_data`
- command processing + wheel events

Remaining integration issues in `tinyx/kdrive/tima/tima_kdrive_mouse.c`:
1. `tima_MouseInit` does not call `mouse_driver_init(0)`.
2. `dx`, `dy`, `flags` are used without safe initialization in poll flow.
3. Current packet handling (`continue`/`break` sequence) can produce partial-event processing.
4. Wheel input is consumed but ignored at KDrive event level.

## 2.4 KDrive OS function table (`KdOsFuncs`)
**Status**: Minimal implementation present.

Implemented in `tinyx/kdrive/tima/tima_kdrive_system.c`:
- lifecycle: `Init/Enable/Disable/Fini`
- `pollEvents`: calls keyboard + mouse polling
- TinyX required entry points: `InitCard`, `InitOutput`, `InitInput`, `OsVendorInit`

Current limitation: this is a **minimal poll-only OS adapter**; it does not yet provide a complete Tima-specific wait/timer strategy for a fully isolated target runtime.

---

## 3) Missing Tima OS Infrastructure / Stubs (What still has to be provided)

This section separates what is needed **now** for hosted TinyX integration vs what is needed **later** for target-quality Tima OS deployment.

## 3.1 Needed now (hosted integration)
1. **No large new OS stub layer is required immediately** if TinyX `os`/`dix` modules are linked as designed.
2. Fix Tima mouse adapter issues listed above.
3. Select final server entry strategy:
   - current `framework/wmb_x11/wmb_main.c` is a placeholder `main`
   - full TinyX server startup normally expects DIX main path and DDX hooks (`InitCard`, etc.)

## 3.2 Needed later (target/bare-metal quality)
1. **Event wait + yield abstraction**
   - Provide cooperative wait function(s) for idle periods (e.g., timeout-based yield).
   - Keep single-thread semantics unless explicitly changed.

2. **Timer/timebase integration**
   - Monotonic ms time source.
   - Sleep/delay service for event/timer scheduling.

3. **Memory integration policy**
   - Confirm allocator strategy (`malloc/free` via libc/newlib, heap sizing, failure behavior).
   - Keep consistent with `OS_LIBC_REQUIREMENTS.md`.

4. **Thread control policy**
   - Current configuration is effectively single-thread (`DONT_USE_MULTITHREAD`).
   - Define explicit yield/no-yield behavior for Tima scheduler hooks.

5. **I/O + filesystem expectations**
   - Font/config file access strategy (real filesystem vs embedded assets).
   - Logging channel policy.

6. **Transport/auth policy**
   - Decide whether socket transports are kept for hosted only.
   - For target local-only runtime, document disabled transport/auth paths.

7. **Signal/error handling**
   - Fatal/error/reporting pathway equivalent to TinyX expectations (`FatalError`, logging).

---

## 4) TinyX Build-System Pieces to Import Next

## 4.1 Immediate modules required by current unresolved symbols
At minimum, current unresolved symbols map to:
- `tinyx/kdrive/src`:
  - providers for `KdInitOutput`, `KdOsInit`, `KdShadowSet`, `kdScreenPrivateIndex`
  - input internals (`KdInitInput`, `KdEnqueue*`, `kdKeymap`)
  - card/mouse info (`KdCardInfoAdd`, `kdMouseInfo`)
- `tinyx/miext/shadow`:
  - `shadowSetup`, `shadowUpdatePacked`
- `tinyx/os/log.c`:
  - `FatalError`

## 4.2 Recommended module set (align with TinyX autotools model)
Based on `tinyx/configure.ac` (`KDRIVE_LIBS` / `KDRIVE_PURE_LIBS`), expected integration ultimately includes:
- `dix`
- `kdrive/src`
- `fb`, `mi`
- `xfixes`, `Xext`, `render`, `randr`, `damageext`
- `miext/damage`, `miext/shadow`
- `os`
- `kdrive/src/libkdrivestubs` equivalent (`fbcmap` source role)

Notes:
- `kdrive/linux` is backend-specific and not automatically required for Tima HAL path.
- `kdrive/fbdev` and `kdrive/vesa` are reference DDX targets; not part of Tima target unless explicitly needed.

---

## 5) Expected `framework/CMakeLists.txt` Changes (Phased)

## Phase A — Linkage unblocking (short-term)
1. Keep existing SDL local path + shim logic.
2. Split build into logical targets:
   - `framework_hal` (SDL drivers + support libs)
   - `tima_ddx` (the 4 `tima_kdrive_*.c` files)
   - TinyX component static libs (or grouped object libraries)
3. Add TinyX source groups for at least:
   - `kdrive/src`, `miext/shadow`, `os/log`
4. Link in dependency order matching TinyX layering (DDX -> KDrive -> MI/FB/Xext/OS).

## Phase B — TinyX-consistent core integration
1. Add broad TinyX modules to match `KDRIVE_LIBS` composition.
2. Introduce generated config headers for TinyX C sources:
   - `dix-config.h`
   - `kdrive-config.h`
3. Re-enable `HAVE_CONFIG_H` once generated config headers are available in include path.
4. Remove placeholder assumptions and align compile defines with chosen feature subset.

## Phase C — Runtime entrypoint cleanup
1. Decide whether executable entry is:
   - TinyX DIX `main` path, or
   - project wrapper `main` that initializes DIX explicitly.
2. Avoid duplicate `main` ownership.
3. Keep Tima DDX hooks (`InitCard`, `InitOutput`, `InitInput`, `OsVendorInit`) as the DDX boundary.

---

## 6) Recommended Implementation Sequence

1. **Fix Tima mouse adapter correctness** (init + packet state + variable initialization).
2. **Add minimal TinyX providers** for current unresolved symbols.
3. **Promote to `KDRIVE_LIBS`-aligned module set** in CMake.
4. **Finalize entrypoint strategy** (TinyX main vs wrapper).
5. **Only then** expand target-side OS abstraction work (timers/yield/filesystem policy).

---

## 7) Definition of Done for the Next Milestone

A milestone is complete when all of the following are true:
- `wmb_tima_build` links successfully with TinyX core modules (no unresolved KDrive/DIX symbols).
- Tima display + keyboard + mouse event path runs in hosted SDL mode.
- Mouse adapter fixes are merged and verified.
- CMake target structure clearly separates:
  - HAL backend
  - Tima DDX layer
  - TinyX core libraries
- A documented decision exists for runtime entrypoint and transport policy.

---

## 8) Practical Risk Notes

1. Importing only a few TinyX files tends to trigger cascading unresolved symbols; prefer module-level integration over ad-hoc file patching.
2. Leaving config headers unmanaged (`dix-config.h`, `kdrive-config.h`) will block clean TinyX source integration.
3. Placeholder `main` can hide runtime integration decisions; entrypoint ownership must be explicit before JWM/libX11 integration.
4. Keep hosted and target concerns separated: first achieve TinyX linkage and event flow in hosted build, then tighten target OS contracts.
