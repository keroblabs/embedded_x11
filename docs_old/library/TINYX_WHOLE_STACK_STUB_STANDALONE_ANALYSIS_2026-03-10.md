# TinyX Whole-Stack + Stub Connection Analysis (Standalone Xserver Milestone)

**Date**: 2026-03-10
**Request addressed**: Analyze feasibility of building TinyX as a whole (with KDrive + hosted Tima HAL) while replacing the client/server connection with a stub API path, aiming for a standalone Xserver milestone.

---

## 1) Source Basis Used

This analysis is based on the current roadmap and core design docs:
- `docs/library/TINYX_TIMA_INTEGRATION_NEXT_STEPS_2026-03-10.md`
- `docs/BASE_REQUIREMENTS.md`
- `docs/XCB_STUB_ARCHITECTURE.md`
- `docs/BUILD_PLAN.md`
- `docs/WMB_API_DESIGN.md`
- `docs/CUSTOM_XLIB.md`
- `docs/XOPENDISPLAY_ANALYSIS.md`
- `docs/XCB_DEPENDENCY_RISK_ASSESSMENT.md`

And validated against current source behavior in `tinyx`/`libX11`/`framework`.

---

## 2) Executive Conclusion

## Feasibility verdict: **YES, with phased scope control**

The milestone is feasible and aligned with base requirements **if split into a controlled sequence**:

1. **Integrate TinyX as a whole first** (avoid ad-hoc file cherry-picking and unresolved-symbol cascades).
2. **Introduce stub transport on the libX11 side** (SyncHandle/synchandler approach).
3. **Only then enforce strict no-listener standalone mode** in TinyX OS connection layer.

Trying to do all three at once is high risk because TinyX still has hard assumptions about listener sockets and `osPrivate`-backed I/O paths.

---

## 3) Why “TinyX as a whole” is the correct direction

Current linker failures (already documented) are exactly what happens when only partial TinyX pieces are linked:
- KDrive core providers missing (`KdInitOutput`, `KdInitInput`, `KdOsInit`, `KdCardInfoAdd`, etc.)
- Shadow/miext providers missing (`shadowSetup`, `shadowUpdatePacked`, `KdShadowSet`)
- Input globals/functions missing (`kdKeymap`, `kdMouseInfo`, `KdEnqueue*`)

This matches TinyX’s own architecture model (`KDRIVE_LIBS` / `KDRIVE_PURE_LIBS`) where modules are intended to be integrated as a set, not file-by-file.

**Conclusion**: Your direction (add TinyX as a whole) is the right technical move.

---

## 4) Stub connection approach: strongest candidate

Among documented options, the best fit is the `XCB_STUB_ARCHITECTURE.md` model:
- Keep libX11 API files unchanged.
- Use `SyncHandle()` + `dpy->synchandler` as execution trigger.
- Stub/replace connection and transport internals (`_XConnectXCB`, `_XGetRequest`, `_XReply`, `_XRead*`, `_XReadEvents`, `_XSend`, `_XFlush`).

This is better aligned with current docs than broad custom rewrites:
- `CUSTOM_XLIB.md` describes broader replacement files (`wmb_io.c`, `wmb_disp.c`, etc.).
- `XCB_STUB_ARCHITECTURE.md` is newer and explicitly marked as recommended in `DOCUMENTATION_INDEX.md`.

**Conclusion**: use the SyncHandle-based stub transport plan as the canonical implementation path.

---

## 5) Critical blockers for immediate “no-socket standalone” mode

Two concrete TinyX server assumptions must be handled:

1. **Listener hard requirement at startup**
   `tinyx/dix/main.c` calls `CreateWellKnownSockets()` on server generation 1.
   `tinyx/os/connection.c` currently raises fatal error when no listeners exist:
   - `FatalError("Cannot establish any listening sockets ...")`

2. **Event/reply output path assumes `client->osPrivate` I/O structs**
   `WriteEventsToClient()` -> `WriteToClient()` -> uses `OsCommRec` fields (`fd`, `trans_conn`, output buffers).
   Internal clients must either:
   - provide compatible `OsCommRec` plumbing, or
   - branch event delivery for internal/stub clients.

These are solvable, but should be treated as explicit implementation tasks, not hidden assumptions.

---

## 6) Recommended milestone decomposition

## Milestone M1 — Whole TinyX hosted server baseline
**Goal**: Build and run TinyX + Tima KDrive + hosted HAL as a proper server stack.

Scope:
- Integrate TinyX module set consistent with `KDRIVE_LIBS` model.
- Keep existing listener path operational initially (at least local transport); no hard no-socket requirement yet.
- Complete current Tima mouse adapter fixes.

Definition of done:
- TinyX links as a whole in hosted build.
- DIX/KDrive startup path reaches server loop.
- Input/display via Tima HAL works in hosted mode.

---

## Milestone M2 — Stubbed internal client connection (no socket path for WMB clients)
**Goal**: libX11-wmb requests execute in-process via stubs, not via socket transport.

Scope:
- Implement stub transport functions per `XCB_STUB_ARCHITECTURE.md` phases 1-3.
- Internal client registration path in TinyX.
- Event delivery branch for internal clients.

Definition of done:
- `XOpenDisplay()` (stub path) returns valid `Display*`.
- Basic requests (`XCreateWindow`, `XMapWindow`, simple draw + event loop) execute through stub path.
- No network transport required for internal WMB client operation.

---

## Milestone M3 — Strict standalone server mode (optional hardening milestone)
**Goal**: boot TinyX server with zero listener sockets while retaining internal stub clients.

Scope:
- Add explicit standalone compile/runtime mode (e.g., `WMB_STANDALONE_NO_LISTEN`).
- Relax/replace listener fatal path in `CreateWellKnownSockets()` when internal-client mode is enabled.
- Disable XDMCP/remote auth/listener-dependent features for this mode.

Definition of done:
- Server starts without creating transport listeners.
- Internal stub clients still connect and receive events.
- No listener-related fatal errors.

---

## 7) CMake and build-system implications

To support this safely, CMake should evolve from file list assembly to module-aware targets:

1. Build TinyX components as grouped/static targets mirroring autotools layering.
2. Generate and include TinyX config headers (`dix-config.h`, `kdrive-config.h`) in hosted builds.
3. Keep build profiles explicit:
   - `HOSTED_SERVER` (M1)
   - `HOSTED_SERVER + STUB_CLIENT` (M2)
   - `HOSTED_STANDALONE_NO_LISTEN` (M3)

This reduces churn and makes each milestone testable independently.

---

## 8) Risk profile and mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Partial TinyX linking | High | Integrate module groups, not scattered files |
| Listener assumptions in TinyX OS layer | High | Add explicit standalone mode guard in connection startup |
| Internal client event output path | High | Implement internal-client-specific delivery branch/plumbing |
| Stub transport semantic mismatch | Medium | Follow SyncHandle phases with incremental request/reply tests |
| Config header drift | Medium | Generate config headers per build profile |

---

## 9) Recommended project decision

Adopt the following explicit policy:

1. **Canonical architecture for client/server bypass**: `XCB_STUB_ARCHITECTURE.md` (SyncHandle path).
2. **Canonical server integration strategy**: TinyX whole-stack module integration (not piecemeal).
3. **Milestone sequence**: M1 (whole server) -> M2 (stub client path) -> M3 (strict no-listen standalone).

This gives you a realistic path to the requested standalone Xserver milestone while preserving momentum and controlling integration risk.
