# WMB Project Catch-Up Snapshot

**Date:** 2026-03-09
**Purpose:** Consolidate what is known, what is actually implemented, key gaps, and the shortest path to a hosted test executable.

---

## 1) Scope and Baseline Requirements (Source of Truth)

From `docs/BASE_REQUIREMENTS.md`, the non-negotiable baseline is:

1. **Single standalone WMB binary** (TinyX + JWM + libX11-wmb) for Tima OS.
2. **No traditional client/server socket path** inside WMB.
3. **Two-part X11 library model**:
   - External apps: thin one-line wrappers.
   - WMB side: full implementation and event management.
4. **Fixed-address entry point API** (non-PIC loading model).
5. **HAL-driven graphics/input** with framebuffer-first design.
6. **Hosted development build required** on Linux with SDL.

---

## 2) Documents and History Reviewed

Primary references used for this catch-up:

- `docs/BASE_REQUIREMENTS.md`
- `docs/SESSION_SUMMARY_2026-01-22.md`
- `docs/SESSION_SUMMARY_2026-01-24.md`
- `docs/SESSION_SUMMARY_2026-01-25.md`
- `docs/DOCUMENTATION_INDEX.md`
- `docs/ROADMAP.md`
- `docs/BUILD_PLAN.md`
- `docs/CUSTOM_XLIB.md`
- `docs/XCB_STUB_ARCHITECTURE.md`
- `docs/TIMA_KDRIVE_CODE_REVIEW.md`
- `docs/KDRIVE_HAL_GUIDE.md`

---

## 3) Verified Current Workspace State (2026-03-09)

### 3.1 TinyX repository

- Local status shows **one untracked module**:
  - `?? kdrive/tima/`
- The module contains **5 files**:
  - `tima_kdrive.h`
  - `tima_kdrive_fb.c`
  - `tima_kdrive_kbd.c`
  - `tima_kdrive_mouse.c`
  - `tima_kdrive_system.c`

Important reality check:

- `tinyx/kdrive/Makefile.am` currently does **not** include a `tima` subdirectory.
- Therefore, the TinyX autotools build is **not yet wired** to build this driver.

### 3.2 libX11 repository

- No local changed files detected.
- No visible implementation of the planned WMB transport/stub layer yet.

### 3.3 JWM repository

- Local status shows **52 deleted files**, all shown as `D` entries (headers under `jwm/src`).
- This is a major risk item until classified as intentional or reverted.

### 3.4 Framework/CMake hosted path

- `framework/CMakeLists.txt` exists and directly includes `tinyx/kdrive/tima/*.c` sources.
- Fresh local configure succeeds:
  - `cmake -S framework -B build_local`
- Build currently fails immediately on dependency:
  - `SDL2/SDL.h: No such file or directory`

Additional structural blockers identified:

- The current `framework` target does **not** compile/link full TinyX + JWM + libX11 core, so it is not yet an end-to-end hosted WMB binary.
- `framework/sdl_library/sdl_window.c` contains duplicate `sdl_send_mouse_wheel_event` definitions (same symbol name, different parameters), which will fail in C builds once SDL headers are available.

---

## 4) What Has Been Achieved So Far

### Achieved (strong)

1. **Architecture clarity is high** across documentation.
2. **TinyX/KDrive chosen and validated** as the server base (migration decision complete).
3. **Tima KDrive module code drafted** (framebuffer/keyboard/mouse/system integration layer).
4. **Hosted-vs-target strategy documented** in detail.
5. **Bypass architecture options documented**, including:
   - Two-library concept (`CUSTOM_XLIB.md`)
   - Lower-risk `SyncHandle`/XCB-stub strategy (`XCB_STUB_ARCHITECTURE.md`)

### Not yet achieved (critical)

1. End-to-end hosted executable (TinyX + JWM + libX11-wmb path) is **not runnable** yet.
2. `tima_WaitForEvents()` + cooperative yield model from Jan 25 is **documented but not implemented** in current `tinyx/kdrive/tima` files.
3. libX11 WMB transport/stub implementation is **not present** in workspace code.
4. JWM local tree has destructive deletions that must be resolved before integration.

---

## 5) Gap Analysis vs Requirements

| Requirement | Current State | Gap |
|---|---|---|
| Standalone integrated WMB binary | Not assembled yet | **High** |
| No socket client/server in WMB path | Design done, code missing | **High** |
| Two-part X11 (app wrappers + WMB full impl) | Documented, not implemented | **High** |
| Fixed-address API entry model | Documented, not implemented | **High** |
| HAL-backed KDrive | Started (`kdrive/tima`) | **Medium** |
| Hosted SDL executable | Partial framework scaffold | **High** |

---

## 6) Hosted Test Executable Readiness

### Definition used for “hosted test executable”

Minimum practical target:

- Builds on Linux host.
- Opens SDL-backed window/framebuffer path.
- Initializes TinyX via `tima` KDrive path.
- Accepts keyboard/mouse events through HAL path.
- Can run a basic smoke scenario (window/event/draw cycle).

### Current readiness

- **Not ready yet**.
- Toolchain/build scaffolding exists, but integration and dependency blockers remain.

### Remaining work estimate (from current state)

If scope is **hosted smoke executable only** (not full production two-block API):

- **~1.5 to 3 weeks**

If scope is **hosted integrated WMB path with JWM + libX11-wmb bypass foundations**:

- **~4 to 7 weeks**

Assumptions behind estimate:

- JWM deletions are either reverted quickly or intentionally resolved.
- SDL2 development packages are installed.
- Team focuses on `XCB_STUB_ARCHITECTURE.md` approach (lower implementation surface than broad custom rewrite).

---

## 7) Recommended Next Steps (Priority Order)

### P0 – Stabilize repository state (Immediate)

1. Resolve JWM working tree (revert or intentionally commit refactor branch).
2. Confirm hosted acceptance criteria (smoke vs full integrated target).
3. Install/verify SDL2 dev environment on host.

### P1 – Make hosted build executable and debuggable

4. Fix compile blockers in `framework` (including duplicate function symbol in `sdl_window.c`).
5. Add a clean top-level CMake orchestration (or consistent staged build script) instead of partial one-off target.
6. Wire TinyX core pieces required by `tima` driver path (not just the 5 `tima` files).

### P2 – Align code with Jan 25 event model

7. Implement `tima_WaitForEvents()` in `tinyx/kdrive/tima/tima_kdrive_system.c`.
8. Add hosted-safe cooperative wait behavior (polling/yield abstraction) compatible with future `hal_yield_timeout()`.

### P3 – Start bypass implementation with lowest-risk path

9. Implement Phase 1/2 of `XCB_STUB_ARCHITECTURE.md` (`_XConnectXCB`, `_XGetRequest`, `_XReply`, buffer handling).
10. Add smoke tests for: XOpenDisplay, create/map window, event pump.

---

## 8) Suggested Milestone Sequence

1. **M1: Hosted Build Green**
   - Build passes locally with SDL.
   - SDL window + HAL loop alive.

2. **M2: TinyX + Tima Driver Functional**
   - Card/input initialization succeeds.
   - Basic event flow works.

3. **M3: Cooperative Wait Integrated**
   - `tima_WaitForEvents()` behavior matches Jan 25 design intent.

4. **M4: Xlib Bypass Foundation**
   - `XOpenDisplay` + first request/reply path through stub layer.

5. **M5: Hosted Test Executable Demonstration**
   - Repeatable run command + smoke test checklist.

---

## 9) Key Risks to Track

1. **JWM local deletions** may block integration until resolved.
2. **Build fragmentation** (autotools in TinyX vs standalone framework CMake) can create divergent integration paths.
3. **Architecture drift** between older two-block docs and newer XCB-stub approach.
4. **Over-scoping early milestone** may delay first hosted executable.

---

## 10) Practical Working Recommendation

For fastest progress, treat the next sprint as a **hosted executable stabilization sprint**:

- Lock scope to M1–M3 first.
- Keep libX11 bypass work behind that (M4).
- Use this document as a rolling truth source and update at each milestone.
