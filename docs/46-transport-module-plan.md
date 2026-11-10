# 46 — Transport Module Plan

## 1. Purpose

This is the **transport team's working document**: everything that replaces the X11 socket transport when Xlib and TinyX live in the same address
space — the thirteen `xcb_io.c` replacements, the `Data()` edit, `wmb_open`/`wmb_setup`/`wmb_close`, the request/reply queue, the event path, the two
`libX11/` guards, plus the external ABI and the `wmb_hello` bridge that lets a separate process reach the server.

It is the slice of [`40-implementation-plan.md`](40-implementation-plan.md) that covers **P5 (client half of the transport)**, **P6 (`libX11/`
guards)** and **P8 (external ABI + stability)**. [`40-`](40-implementation-plan.md) remains the authority on phase order; this document is the
authority on what this module builds.

This module consumes the framework's fiber runtime and library primitives ([`44-`](44-framework-module-plan.md)); the substrate is done, only the
X11 protocol replacement is left.

### 1.1 Ownership boundary

| | In this module | Elsewhere |
|---|---|---|
| Client-side transport | `transport/x11/` — `wmb_open`, `wmb_setup`, `wmb_close`, `wmb_transport.c`, `wmb_queue.c`, `wmb_event.c`, `wmb_dispatch` | `tinyx/os/*_wmb.c` — server side → [`42-`](42-tinyx-module-plan.md) |
| Xlib | All `libX11/` work: exclusions, stubs, guards #9 and #10, the `Data()` edit | — |
| External ABI | `transport/api/`, `wrapper/libX11-app/`, `apps/wmb_hello/` | — |
| Host and entry | `linux_hosted/wmb_boot/wmb_main.c`, `<target>_native/wmb_boot/` | Runtime bring-up (`heap_init`, `fiber_runtime_init`) → [`44-`](44-framework-module-plan.md) |
| Apps that link the server | `apps/wmb_test_client/`, `apps/wmb_hello/` | — |
| Fiber runtime, library primitives, HAL contract | — | [`44-`](44-framework-module-plan.md), [`hal/`](../hal) |

**Rule of thumb:** if the file lives under `transport/`, `libX11/`, `wrapper/`, `apps/`, or `linux_hosted/wmb_boot/`, it is this module's.

### 1.2 What this document is not

- **Not the transport design.** [`21-inprocess-transport.md`](21-inprocess-transport.md) specifies every function named in §4.
- **Not the ABI specification.** [`22-entry-point-abi.md`](22-entry-point-abi.md) gives the vtable layout, placement and versioning rules.
- **Not the framework plan.** [`44-framework-module-plan.md`](44-framework-module-plan.md) owns fiber, `hal_task`, library primitives.

## 2. The File Set

| Group | Contents | Files | ~LOC |
|---|---|---:|---:|
| `WMB_TRANSPORT` | `transport/x11/` | 6 | 900 |
| `WMB_API` | `transport/api/` | 2 | 260 |
| `LIBX11_API` | `libX11/src/` minus the excluded set — **carried unmodified** | ~245 | ~34,000 |
| `WMB_APPS` | `apps/` | 2 | 180 |
| `WMB_BOOT` | `linux_hosted/wmb_boot/`, `<target>_native/wmb_boot/` | 2 | 200 |

The 34 kLOC of `libX11/src/` is listed because this module is accountable for it linking, not because it is edited. **The entire XCB-stub strategy is:
replace the transport, not the API.**

### 2.1 Transport-cleanup checkpoint (before P4/P5)

| Artefact | State |
|---|---|
| `transport/api/wmb_app_table.h` | **KEEP** — the application launch-table contract and hosted smoke test remain unchanged |
| `transport/x11/wmb_io.c` | **RETIRED** — the uncompiled, single-global-client, direct-`ProcVector` experiment was deleted from the working tree. Git history retains it; do **not** carry its dispatch model into P5 |
| `transport/x11/wmb_io.h` | **RETIRED** — zero-byte header removed; create `wmb_x11_internal.h` with per-`Display` state at P5 (§3.1 of Document 21) |
| `transport/x11/Xlibint_wmb.h` | **ADAPTED** — declaration-only contract for `_XBufferGrow`; the actual complete-builder macros are now in matching `libX11/include/X11/Xlibint.h`, with tested growth/failure handling |
| `transport/x11/wmb_main.c` | **IMPLEMENTED at P7** - once-only standard-C startup/poll/shutdown; hosted entry remains outside the windowing set |
| `linux_hosted/sdl_fiber_example_v2/` | **DONE** — proves the dispatch handshake (V-P5-0), not X11 transport correctness |

That table records the pre-P4 cleanup. **Current status:** P4 and P5 are built.
The real `_XBufferGrow`, complete-batch request transport and tests now exist;
P7 now implements the portable startup/stop and application registry lifecycle;
the external ABI remains P8.
## 3. Phase Participation

| Phase | This module's involvement | Weight |
|---|---|---|
| P1 | — | — |
| P2 | — | — |
| P3 | — | — |
| P4 | `wmb_boot.c` creates the server fiber and calls `TinyX_ServerFiber` | Light |
| **P5** | **Implemented hosted client gate** — retained Xlib, real wire transport, queues and visible client | **Heavy** |
| **P6** | **Implemented and verified:** ten library guards, supported-profile exclusions and replay/native dependency gates | Medium |
| **P7** | **Implemented:** server/WM readiness, application lookup/owned argv/blocked tasks/reaping, scoped errors and cooperative event waits | Medium |
| **P8** | **Primary owner** — the external ABI, the wrapper, `wmb_hello` | **Heavy** |
| P9 | `wmb_boot.c`'s freestanding half, `.wmb_api` placement, the link floor | Medium |

## 4. Phase P5 — The Client Half of the Transport

The [implementation guide](../transport/x11/README.md) supersedes illustrative
buffer/reset examples below. The tested client cohort uses
`libX11/include/X11`, not the older workspace header copy. Unmodified setup parsing
is retained in `OpenDis.c`; the adapter replaces only connection/setup sourcing.
The client archive is hidden and ordered before SDL's native dependencies.

P5 verifies real windows/drawing/input, fixed/variable replies, errors and async
handlers, lease/growth failures, motion retention, native grabs/replay, delayed
input, BIG-REQUESTS, serial wrap, XC-MISC, bounded timeout, stock wire comparison
and a sixty-second Valgrind run. JWM and external callers remain later phases.

**Goal:** `XCreateWindow(dpy, …)` reaches `ProcCreateWindow(client)` by function call, with the request still sitting in `dpy->buffer` where Xlib
built it. [`40-`](40-implementation-plan.md) §6, [`21-`](21-inprocess-transport.md) §3, §4.

### 4.1 Work items

| # | Item | Est. LOC | Spec |
|--:|---|---:|---|
| F5‑1 | `wmb_queue.c` / `wmb_bridge.h` — bounded connections, controls, borrowed batches and owned output | implemented | [`21-`](21-inprocess-transport.md) §5.1, §5.3 |
| F5‑2 | Guarded `OpenDis.c` plus `wmb_transport.c` setup hooks; explicit profile support | implemented | [`21-`](21-inprocess-transport.md) §3.2 |
| F5‑3 | Real initial/establish handshake through native dispatch and output framing | implemented | [`21-`](21-inprocess-transport.md) §3.3 |
| F5‑4 | `wmb_close.c` | ~60 | [`21-`](21-inprocess-transport.md) §3.2 |
| F5‑5 | `wmb_transport.c` — **the thirteen `xcb_io.c` replacements**, implemented fresh according to D-46 | ~280 | [`21-`](21-inprocess-transport.md) §3.4 |
| F5‑6 | Matching `libX11/include/X11/Xlibint.h` and `XlibInt.c` complete-builder preparation | implemented | [`21-`](21-inprocess-transport.md) §3.5 |
| F5‑7 | `wmb_connection_submit` plus read/wait completion boundaries and predicates | implemented | [`21-`](21-inprocess-transport.md) §4.1 |
| F5‑8 | `wmb_queue.c` owned event records and motion retention | implemented | [`21-`](21-inprocess-transport.md) §6 |
| F5‑9 | Real/raw/flow/timeout/wire clients in `transport/x11/` | implemented | [Run guide](../transport/x11/README.md) |
| F5‑10 | Trace callback, bounded timeout, sanitizer guards and unit faults; broader debugger/poison aids remain optional plans | partial tooling, core gates complete | [`21-`](21-inprocess-transport.md) §9 |

### 4.2 What is already settled, and what is not

The handshake does **not** have to be discovered here. [`21-`](21-inprocess-transport.md) §11 records a running proof of concept — 40 requests posted,
40 served, 40 completions, **0 timeouts**, every draw on the server fiber — which rules out the three failure modes a design review cannot: a lost
notify, an early release, and a buffer released under a running server. That code lives in
[`linux_hosted/sdl_fiber_example_v2`](../linux_hosted/sdl_fiber_example_v2/) and it depends only on framework primitives — it belongs to the framework's
demonstration surface, not to this module.

**P5 supplies the X11 half**: replies with payload, the event path, errors and sequence numbers, `InsertFakeRequest`, grabs, and the
`BlockHandler`/`WakeupHandler` pair. Those stay gates.

### 4.3 The `Data()` edit is the highest-leverage line in the project

The matching client header and `XlibInt.c` prepare complete batches under
`WMB_INPROC`; the 245 retained native source files are frozen independently of
the three owned client adapters. Two consequences remain important:

- Upstream code does `GetReq(...); req->field = x; Data(...); req->other = y;` — **a reallocation invalidates a live `xReq*`** (risk R2). The grow
  policy must preserve offsets and forbid leased mutation. Explicit OOM/lease
  tests, actual large requests and ASan verify this; no buffer-poison feature is claimed.
- Verify every translation unit actually sees the modified header, with a compile-time `#error` canary. A stale copy of `Xlibint.h` in one TU is a
  silent, intermittent corruption (risk R5 in [`11-`](11-portability-plan.md) §11).

### 4.4 Definition of Done — transport slice

- [x] In-process open returns actual setup/Screen/Visual data.
- [x] Window creation/map and all six application PASS stages execute.
- [x] The white/black/text client is visible; its framebuffer pixels are checked.
- [x] 1,000 motions retain the newest 128; critical events and storage ownership are tested.
- [x] A missing reply diagnoses failure after five seconds, not an infinite wait.
- [x] **Differential byte test** (V-P5-11): for the ten calls of [`41-`](41-verification-plan.md) §5.2, the bytes in `dpy->buffer` before dispatch are
  identical after documented identity/serial/protocol-padding normalization to
  a genuine stock-Xlib capture. **Calls 4 and 6 — a 4 KB `XChangeProperty` and the matching `XGetWindowProperty` — are
  the reason the test exists.**
- [x] Valgrind: no errors, no definitely/possibly lost bytes over the sixty-second client session.

## 5. Phase P6 — Verified Client Portability

The old three-file estimate missed public/header, resource-name, macro/XCB and
environment dependencies. The user-approved D-54 expands the cap to ten narrow
guarded files; [the manifest](../patches/MANIFEST.md) is authoritative.

| # | File | Guard | Edit |
|--:|---|---|---|
| 9 | `libX11/include/X11/Xlibint.h` | `WMB_INPROC` | Matching client header, complete-builder growth and single-threaded profile |
| 10 | `libX11/src/XlibInt.c` | `WMB_INPROC` | Guard the `<sys/socket.h>` / `<sys/ioctl.h>` / `<sys/utsname.h>` block and the fd-based `_XWaitForReadable` / `_XWaitForWritable` helpers; **keep error handling and `_XEatData`** |
| 19 | `libX11/src/OpenDis.c` | `WMB_INPROC` | Reuse actual setup parsing with internal connection/setup/close hooks |
| 22 | `libX11/include/X11/Xlib.h` | `WMB_INPROC` | Standard public types |
| 23/24 | `libX11/src/reallocarray.c`, `reallocarray.h` | `WMB_INPROC` | Retained checked allocation and standard headers |
| 25 | `libX11/src/Macros.c` | `WMB_INPROC` | Macro functions without native XCB/locking headers |
| 26/27 | `libX11/src/StrKeysym.c`, `KeysymStr.c` | `WMB_INPROC` | Built-in/Unicode names without host resource files |
| 28 | `libX11/src/WMProps.c` | `WMB_INPROC` | Explicit caller hints, WMB identity and C locale |

- [x] Ten patches replay on exact pristine imported files and preserve reference compilation/notices.
- [x] Ten guarded files remain within the expanded cap.
- [x] No active native/XCB/pthread/backend headers or imports in the client archive.
- [x] Whole-archive no-backend linkage, without garbage collection, passes.
- [x] P5 regressions and P6 profile checks pass in Debug/Release and ASan/UBSan.

The supported cohort excludes native transport, locale/fontset/input-method,
resource-file and bitmap-file backends. Exported bitmap/default refusals diagnose
unsupported use; in-memory APIs remain. `CrGlCur.c` is now retained with dynamic
Xcursor disabled, rather than losing actual core glyph-cursor creation.
`LockDis.c` supplies the threadless lock no-ops; duplicate owned definitions were
removed after the whole-archive gate exposed them. See
[the P6 guide](48-p6-portability.md) for all 248 selected units and exact gates.

## 6. Phase P8 — External ABI and Stability

**Goal:** prove the external application story, and prove the system survives. [`40-`](40-implementation-plan.md) §9, [`22-`](22-entry-point-abi.md).

### 6.1 Work items

| # | Item | Spec |
|--:|---|---|
| F8‑1 | `transport/api/api.def` + generator | [`22-`](22-entry-point-abi.md) §5.1 |
| F8‑2 | `wmb_api.h`, `wmb_api_table.c` | [`22-`](22-entry-point-abi.md) §3, §4.1 |
| F8‑3 | **A5 enumeration** → `docs/xlib-display-macros.txt`, generated from `X11/Xlib.h` | §6.2, [`91-`](91-open-questions.md) Q20 |
| F8‑4 | `wrapper/libX11-app/` + the macro compatibility header, driven by F8‑3 | [`22-`](22-entry-point-abi.md) §5 |
| F8‑5 | `wmb_bridge.c` — the host validation scaffold | [`22-`](22-entry-point-abi.md) §6 |
| F8‑6 | MPSC producer side on the request queue + a **polled** server-wake mailbox | [`22-`](22-entry-point-abi.md) §7 E4, [`90-`](90-decisions.md) D‑44 |
| F8‑7 | `apps/wmb_hello/` | [`01-`](01-component-inventory.md) §11.2 |

The **stack and heap instrumentation** (F8-8 in the old 44) is measurement of framework quantities and stays with [`44-`](44-framework-module-plan.md).

### 6.2 The A5 enumeration is generated, not typed

`DefaultScreen`, `RootWindow`, `BlackPixel`, `DisplayWidth`, `ScreenCount`, `NextRequest`, `ConnectionNumber` and their `*OfScreen` variants all
expand to a dereference of a `Display` or `Screen` struct **that the wrapper does not have**. Missing one does not fail to compile — it segfaults at
first use, in an application we did not write.

So the list is extracted mechanically from `X11/Xlib.h` (every macro whose expansion mentions its `Display` or `Screen` parameter), checked in, and
regenerated in CI so that an upstream header change appears as a diff rather than as a field report.

### 6.3 Definition of Done

- [ ] `wmb_api.h`, `wrappers.c` and the slot count are all generated from `api.def`; regeneration produces no diff (S9).
- [ ] `docs/xlib-display-macros.txt` is generated, checked in, and regeneration produces no diff.
- [ ] A test TU **using every macro in that list** compiles against the wrapper's compatibility header and produces no reference to any `Display` or
  `Screen` field — in the wrapper, both types are opaque.
- [ ] `libX11-app.a` is **< 16 KB** and exports only the wrapper entry points plus `wmb_bind` (V-P8-1).
- [ ] `wmb_hello`, a separate process linking only the wrapper, produces a **visible, JWM-decorated window** over the bridge (V-P8-2).
- [ ] An application built against ABI v1 runs against a v2 WMB (V-P8-3).
- [ ] **Re-entrancy (E4)**: two bridge clients posting from two OS threads, deliberately preempting each other, produce correct results and lose no
  requests. The contention is on the queue and the wake, **not on `dix`**, which the server fiber still serialises. This is the only test in the
  project that deliberately uses threads, and it tests the boundary, not WMB.
- [ ] **Leak (V-P8-4)**: 10,000 window create/destroy cycles leave the heap high-water mark flat, **attributed to WMB rather than to the whole heap**.
- [ ] **Stability (V-P8-5)**: a one-hour run with scripted continuous input — flat heap, no fiber canary trip, no fiber starvation.
- [ ] **Measurements recorded (V-P8-6)** — captured against the framework's heap/stack instrumentation; recording is [`44-`](44-framework-module-plan.md)'s
  responsibility. The transport's obligation is to run the workload that produces the measurements.

## 7. Interfaces With Other Modules

| Seam | This module provides | Consumer |
|---|---|---|
| `wmb_queue.c` | Connection controls, leased batches, terminal/completion predicates and packet queues | `io_wmb.c`, `waitfor_wmb.c` -> [42-](42-tinyx-module-plan.md) |
| `wmb_connection_open()` | *Consumed* by the guarded Xlib setup hooks | Native client creation is serviced by `connection_wmb.c` |
| `wmb_main()` / `wmb_poll()` / `wmb_shutdown()` | Once-only library lifecycle, readiness and ordered cooperative cleanup | [P7](45-jwm-module-plan.md), D-33 / D-43 / D-55 |
| `wmb_app_table` | The in-process launch table | `jwm/src/command.c` → [`45-`](45-jwm-module-plan.md) |
| `wmb_apps.c` | Copied argv, initially blocked launch, borrowed handles and owner-side reaping | JWM and the host-populated registry |
| `wmb_xlib.h` | Finite/indefinite client event wait and per-Display internal error hook | JWM and cooperative application clients |
| `wmb_api_t` | The fixed-address vtable | `wrapper/libX11-app/`, external binaries |
| Fiber/task contract | *Consumed* - predicate waits and notifications | Defined by [44-](44-framework-module-plan.md) |
| Framework library primitives | *Consumed* - `lists.c` backs owned packets/events; Xlib owns its request builder | Defined by [44-](44-framework-module-plan.md) |

## 8. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| TR‑1 | `dpy->buffer` reallocation invalidates a live `xReq*` | **High** | **High** | Section 4.3; preserved offsets/leases, real large payloads and ASan; do not rely on rare growth |
| TR‑2 | Deadlock: a client waits for a reply that will never come | Medium | **High** | Closed by construction — `wmb_dispatch` waits with a deadline in **every** build, not just debug |
| TR‑3 | The `Data()` edit does not reach every TU | Low | **High** | A compile-time `#error` canary in the modified header |
| TR‑4 | `lists.c`'s node contract broken — a node freed while linked, or a queue outliving its `Display` | Low | **High** | One owned packet type, explicit take/free accounting and disconnect cleanup |
| TR‑5 | An A5 macro is missed and segfaults in someone else's application | Medium | **High** | §6.2 — generated and CI-diffed, never typed |
| TR‑6 | Multi-part reply misclassification (R1 in [`21-`](21-inprocess-transport.md)) — header and data released as separate slots | Medium | **High** | Closed by predicate-wait design; `WMB_REPLY_TIMEOUT_MS` traps regression |

## 9. Budget

| | Value |
|---|---|
| New LOC | ~1,100 (P5) + ~700 (P8) |
| Upstream files guarded | **10 of the `libX11/` cap of 10** - no remaining client slack |
| Files this module *removes* from the build with no diff | ~10 kLOC of `libX11/src/` |

## 10. What to Do First

**Precondition:** framework's P2 + P3 must be green ([`44-`](44-framework-module-plan.md) §§4, 5). At the time of writing they are — the runtime
substrate ships in `libwmb_framework.a` and every V-P2 / V-P3 gate closes.

1. **Author `hal_task.h`-aware `waitfor_wmb.c` skeleton with [`42-`](42-tinyx-module-plan.md)** so P5's early milestone — dispatching a hand-built
   `X_NoOperation` request into `ProcNoOperation` — has an entry point. That is the cheapest possible end-to-end proof that the substitution works.
2. **Start `wmb_queue.c`** — the slot table and ready mask that both this module and TinyX's `io_wmb.c` consume. It sits at the seam; getting it
   right early makes everything downstream simpler.
3. **Then the `Data()` edit and `wmb_transport.c`** — the highest-leverage lines in the project (§4.3). Once these two land, the 245 unmodified Xlib
   API files start compiling against the in-process transport.

Only after the client half is functional does P8's external ABI make sense to write.
