# TinyX Standalone Stub Milestones (Upstream-Safe Variant)

**Date**: 2026-03-10
**Purpose**: Define M1/M2/M3 for standalone TinyX + stubbed client path, with an added hard requirement: **minimum TinyX codebase changes to preserve easy upstream updates**.

---

## 1) Inputs Used for This Plan

### Documents reviewed
- `docs/library/TINYX_WHOLE_STACK_STUB_STANDALONE_ANALYSIS_2026-03-10.md`
- `docs/library/TINYX_TIMA_INTEGRATION_NEXT_STEPS_2026-03-10.md`
- `docs/library/PROJECT_CATCHUP_2026-03-09.md`
- `docs/BASE_REQUIREMENTS.md`
- `docs/XCB_STUB_ARCHITECTURE.md`
- `docs/XCB_DEPENDENCY_RISK_ASSESSMENT.md`
- `docs/BUILD_PLAN.md`
- `docs/DOCUMENTATION_INDEX.md`

### Code paths validated
- TinyX listener startup: `tinyx/dix/main.c`, `tinyx/os/connection.c`
- TinyX output/event path: `tinyx/dix/events.c`, `tinyx/os/io.c`, `tinyx/os/osdep.h`
- TinyX internal client creation capability: `tinyx/dix/dispatch.c` (`NextAvailableClient`)
- libX11 transport choke points: `libX11/src/OpenDis.c`, `libX11/src/xcb_io.c`, `libX11/include/X11/Xlibint.h`
- Current hosted integration scaffold: `framework/CMakeLists.txt`, `framework/wmb_x11/wmb_main.c`

---

## 2) Added Requirement (New)

In addition to standalone stubbed operation, the project now requires:

> **Keep TinyX changes minimal and controlled so upstream TinyX updates from mainstream remain low-risk and repeatable.**

This changes milestone acceptance criteria: each milestone must pass both functional goals and **upstream-maintainability gates**.

---

## 3) Codebase Findings That Drive the Milestone Design

1. **Strict no-listener mode is not currently possible without a TinyX OS-layer change.**
   `CreateWellKnownSockets()` in `tinyx/os/connection.c` currently fatal-errors when no listeners exist.

2. **Reply/event delivery currently assumes socket-oriented `osPrivate`/`trans_conn` flow.**
   `WriteEventsToClient()` routes to `WriteToClient()`, and `WriteToClient()/FlushClient()` in `tinyx/os/io.c` assume `OsCommRec` socket semantics.

3. **Internal client allocation already exists in TinyX core API surface.**
   `NextAvailableClient(ospriv)` in `tinyx/dix/dispatch.c` can be used by a WMB internal-client adapter without changing DIX dispatch internals.

4. **libX11 remains the best place for the bulk of stub work.**
   SyncHandle/xcb-transport substitution keeps 172+ API files unchanged and avoids spreading TinyX modifications.

5. **Current local TinyX tree already has non-upstream deltas.**
   Existing edits in core files plus new `kdrive/tima` sources should be treated as a managed patch set, not ad-hoc changes.

---

## 4) Upstream-Safe Engineering Rules (Applies to M1–M3)

1. **Adapter-first, patch-last**
   - Prefer new files/wrappers in WMB/framework/libX11 over TinyX core edits.
   - TinyX edits are allowed only when no external adapter can achieve the same behavior.

2. **Patch budget per milestone**
   - Track number of modified existing TinyX files and net LOC delta.
   - Exceeding budget blocks milestone close until justified.

3. **No broad edits in high-churn TinyX subsystems**
   - Avoid touching `tinyx/dix/*`, `tinyx/mi/*`, `tinyx/kdrive/src/*` unless strictly required.
   - Prefer `tinyx/os/*` hooks for standalone transport behavior.

4. **Compile-time guards for all standalone behavior**
   - Use explicit feature macros (example: `WMB_STANDALONE_NO_LISTEN`, `WMB_INTERNAL_CLIENT_IO`) so upstream behavior remains default.

5. **Patch queue discipline**
   - Maintain TinyX delta as a small ordered patch stack with clear ownership and rationale.
   - Rebase test against latest upstream TinyX is a milestone gate.

6. **Patch-file leverage for unavoidable deep edits**
   - If deep TinyX modifications are unavoidable, store them as ordered, versioned patch files (for example `patches/tinyx/NNN-*.patch`).
   - Generate patch files with `git format-patch` and validate replay on fresh upstream TinyX checkouts via `git apply --check` or `git am`.
   - Keep each patch single-purpose (one concern per patch) to minimize conflict resolution cost on future upstream updates.

---

## 5) Milestone Summary (Revised)

| Milestone | Functional target | TinyX delta target | Upstream gate |
|---|---|---|---|
| **M1** | Whole TinyX hosted baseline (KDrive + Tima HAL) | **0 core TinyX edits preferred** (additive driver integration only) | Build from near-upstream TinyX + documented local patch set |
| **M2** | In-process stub client path (no socket required for WMB client ops) | **<= 1 modified existing TinyX file preferred** (+ additive files) | Rebase dry-run keeps tinyx-core conflict count very low |
| **M3** | Strict standalone server (zero listeners) | **<= 2 modified existing TinyX files total** | Upstream merge/update procedure validated and repeatable, including patch-file replay |

---

## 6) Detailed Milestones

## M1 — Whole TinyX hosted baseline (upstream-clean foundation)

### Objective
Build and run TinyX as a coherent module set with Tima DDX/HAL in hosted SDL mode, while preserving upstream structure.

### Scope
- Align CMake integration to TinyX module groups (`KDRIVE_LIBS`/related layering).
- Resolve current linker blockers by module-level inclusion (not cherry-picked source fixes).
- Fix Tima adapter correctness issues already identified (mouse init/poll correctness).
- Keep listener/socket path enabled in M1 (no strict standalone enforcement yet).

### TinyX change budget
- **Preferred**: 0 modified existing TinyX core files.
- **Allowed**: additive integration files only (for Tima DDX and build glue).

### Definition of Done
- Hosted target links and starts server loop.
- Display + keyboard + mouse path works in hosted SDL mode.
- No unresolved KDrive/DIX/TinyX core symbols.
- TinyX patch inventory documented with rationale.

### Upstream maintainability gate
- TinyX core files unchanged or minimally changed and justified.
- Patch list can be replayed on a fresh upstream TinyX checkout.

---

## M2 — Stubbed internal client path (socketless operation for WMB clients)

### Objective
Enable libX11-wmb request/reply/event operation through in-process stubs (SyncHandle architecture), with TinyX modifications minimized.

### Scope
- Implement transport stubs primarily in libX11 side (`_XConnectXCB`, `_XGetRequest`, `_XReply`, `_XRead*`, `_XReadEvents`, `_XFlush`).
- Use TinyX internal client registration via existing client lifecycle APIs.
- Add the smallest TinyX-side hook needed for non-socket reply/event delivery.

### Preferred TinyX implementation shape
- Additive helper source(s) in `tinyx/os/` for WMB internal-client registration/state.
- Single guarded hook in `tinyx/os/io.c` to divert internal-client output path before socket flushing logic.
- Avoid edits to `tinyx/dix/events.c` if `WriteToClient` hook covers both replies and events.

### TinyX change budget
- **Preferred**: 1 modified existing TinyX file max (`os/io.c`) plus additive new files.
- **Hard cap**: 2 modified existing TinyX files.

### Definition of Done
- `XOpenDisplay()` (stub path) succeeds.
- Basic create/map/draw/event-loop scenario works without network transport.
- Reply and event ordering is correct for smoke scenarios.
- No functional dependency on socket transport for WMB internal clients.

### Upstream maintainability gate
- TinyX diff remains concentrated in `tinyx/os/*` only.
- No `dix/*` or `kdrive/src/*` modifications required for M2 close.
- Any unavoidable deep modification is exported as a dedicated TinyX patch file.

---

## M3 — Strict standalone no-listener mode (final standalone server behavior)

### Objective
Run TinyX in standalone mode with **zero listener sockets** while retaining internal stub client operation.

### Scope
- Introduce explicit standalone mode switch (`WMB_STANDALONE_NO_LISTEN` or equivalent runtime mode).
- Guard `CreateWellKnownSockets()` fatal no-listener behavior for standalone mode.
- Keep XDMCP/remote-listener-dependent paths disabled for standalone profile.

### Preferred TinyX implementation shape
- Single guarded edit in `tinyx/os/connection.c` for listener fatal behavior.
- Avoid touching `tinyx/dix/main.c`; keep call flow unchanged and handle policy in OS layer.

### TinyX change budget
- Total modified existing TinyX files across M2+M3: **<= 2 preferred, <= 3 maximum**.

### Definition of Done
- Server boots with no listener sockets created.
- Internal stub clients still execute requests and receive events/replies.
- No listener-related fatal path triggered.

### Upstream maintainability gate
- TinyX standalone behavior fully behind compile/runtime guards.
- Rebase test against latest upstream TinyX completed with documented conflict status.
- Patch-file bundle replays on target upstream TinyX tag/commit (or has documented minimal rejects and resolutions).

---

## 7) Patch-Management Deliverables Required by M3

1. **TinyX patch manifest**
   - File list, rationale, guard macro, owner, and removal strategy.

2. **Patch stack ordering**
   - P0: additive Tima/WMB helper files.
   - P1: minimal core hook patches (`os/io.c`, `os/connection.c` if needed).

3. **Upstream update procedure**
   - Fresh upstream checkout -> apply patch stack -> build smoke -> record conflicts.

4. **Drift budget report**
   - Modified existing TinyX files count.
   - Net LOC delta in existing TinyX files.
   - Rebase conflict trend across updates.

5. **Patch-file bundle + replay guide**
   - Ordered `.patch` files for all unavoidable deep TinyX modifications.
   - A short apply guide with exact commands for clean replay on a fresh upstream TinyX tree.
   - Replay verification record (`git apply --check` or `git am`) for the chosen upstream reference.

---

## 8) Key Risks and Controls

| Risk | Impact | Control |
|---|---|---|
| TinyX edits spread into DIX/KDrive internals | High merge pain | Enforce OS-layer hook strategy and patch budget |
| No-listener mode breaks startup assumptions | High runtime risk | Isolate policy to `os/connection.c` standalone guard |
| Internal client reply/event path mismatch | Medium-High | Centralize in `WriteToClient` hook and staged protocol tests |
| Existing local TinyX debug edits drift further | Medium | Freeze/triage current edits; keep only justified patches |

---

## 9) Immediate Next Actions

1. **Baseline cleanup**: classify current TinyX local edits as keep/revert before M1 closure.
2. **M1 close**: complete module-consistent build + hosted input/display smoke.
3. **M2 start**: implement libX11 stub transport first, then add minimal TinyX I/O hook only if required.
4. **M3 close**: add standalone no-listen guard and run upstream rebase rehearsal.

---

## 10) Final Decision

Proceed with the original M1→M2→M3 sequence, but milestone acceptance is now dual:

- **Functional success** (standalone stubbed TinyX behavior), and
- **Upstream-safe TinyX delta control** (strict patch budget + rebase validation).

This preserves delivery momentum while directly satisfying the new requirement of maintainable upstream updates.
