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
| `transport/x11/Xlibint_wmb.h` | **ADAPTED** — declaration-only contract for `_XBufferGrow`; no global heap dependency and no unchecked `Data()` override. The `WMB_INPROC` `Data()` change belongs in `X11/Xlibint.h` at P5, and requires a real growth implementation and failure handling |
| `transport/x11/wmb_main.c` | **RESERVED** — obsolete `void main(void)` removed. Portable `wmb_main()` and `wmb_shutdown()` will be implemented here per D-43; hosted `main()` belongs separately in `linux_hosted/wmb_boot/` |
| `linux_hosted/sdl_fiber_example_v2/` | **DONE** — proves the dispatch handshake (V-P5-0), not X11 transport correctness |

**Build impact:** this cleanup changes no current CMake target. Neither TinyX nor the X11 transport is built yet. The declaration-only
`_XBufferGrow` is **not implemented** by this commit; do not enable the `WMB_INPROC` Xlib path until P5 supplies and tests it.
## 3. Phase Participation

| Phase | This module's involvement | Weight |
|---|---|---|
| P1 | — | — |
| P2 | — | — |
| P3 | — | — |
| P4 | `wmb_boot.c` creates the server fiber and calls `TinyX_ServerFiber` | Light |
| **P5** | **Primary owner (client half)** — the 13 replacements, `Data()`, the queue, `wmb_test_client` | **Heavy** |
| **P6** | The two `libX11/` guards | Medium |
| P7 | The server-readiness gate inside `wmb_main()`; `wmb_app_table` plumbing | Light |
| **P8** | **Primary owner** — the external ABI, the wrapper, `wmb_hello` | **Heavy** |
| P9 | `wmb_boot.c`'s freestanding half, `.wmb_api` placement, the link floor | Medium |

## 4. Phase P5 — The Client Half of the Transport

**Goal:** `XCreateWindow(dpy, …)` reaches `ProcCreateWindow(client)` by function call, with the request still sitting in `dpy->buffer` where Xlib
built it. [`40-`](40-implementation-plan.md) §6, [`21-`](21-inprocess-transport.md) §3, §4.

### 4.1 Work items

| # | Item | Est. LOC | Spec |
|--:|---|---:|---|
| F5‑1 | `wmb_queue.c` — slots, ready mask, client slots | — | [`21-`](21-inprocess-transport.md) §5.1, §5.3 |
| F5‑2 | `wmb_open.c` — in-process `XOpenDisplay`: build `Display`, register the `ClientRec`, install `dpy->synchandler`, plus the locale/i18n no-op stubs | ~140 | [`21-`](21-inprocess-transport.md) §3.2 |
| F5‑3 | `wmb_setup.c` — synthesise the `xConnSetup` blob `XOpenDisplay` normally reads off the wire | ~120 | [`21-`](21-inprocess-transport.md) §3.3 |
| F5‑4 | `wmb_close.c` | ~60 | [`21-`](21-inprocess-transport.md) §3.2 |
| F5‑5 | `wmb_transport.c` — **the thirteen `xcb_io.c` replacements**, implemented fresh according to D-46 | ~280 | [`21-`](21-inprocess-transport.md) §3.4 |
| F5‑6 | `X11/Xlibint.h` `Data()` edit — on overflow grow `dpy->buffer` instead of calling `_XSend` | 1 macro | [`21-`](21-inprocess-transport.md) §3.5 |
| F5‑7 | `wmb_dispatch` / `wmb_dispatch_done` — post, `fiber_notify()`, park on a predicate with a deadline | — | [`21-`](21-inprocess-transport.md) §4.1 |
| F5‑8 | Event path — `wmb_event.c`, the `lists.c`-backed queue with motion compression | — | [`21-`](21-inprocess-transport.md) §6 |
| F5‑9 | `apps/wmb_test_client/` | ~110 | [`01-`](01-component-inventory.md) §11.2 |
| F5‑10 | Debug aids: `WMB_DEBUG_TRACE_REQUESTS`, `WMB_DEBUG_REPLY_TIMEOUT_MS`, `WMB_DEBUG_BUFFER_POISON`, `WMB_DEBUG_FIBER_NAMES`, the `.gdbinit` helper | — | [`30-`](30-host-build.md) §8 |

### 4.2 What is already settled, and what is not

The handshake does **not** have to be discovered here. [`21-`](21-inprocess-transport.md) §11 records a running proof of concept — 40 requests posted,
40 served, 40 completions, **0 timeouts**, every draw on the server fiber — which rules out the three failure modes a design review cannot: a lost
notify, an early release, and a buffer released under a running server. That code lives in
[`linux_hosted/sdl_fiber_example_v2`](../linux_hosted/sdl_fiber_example_v2/) and it depends only on framework primitives — it belongs to the framework's
demonstration surface, not to this module.

**P5 supplies the X11 half**: replies with payload, the event path, errors and sequence numbers, `InsertFakeRequest`, grabs, and the
`BlockHandler`/`WakeupHandler` pair. Those stay gates.

### 4.3 The `Data()` edit is the highest-leverage line in the project

One macro in `X11/Xlibint.h`, guarded by `WMB_INPROC`. **It is what lets 245 unmodified Xlib API files work.** Two consequences to hold in mind while
writing it:

- Upstream code does `GetReq(...); req->field = x; Data(...); req->other = y;` — **a reallocation invalidates a live `xReq*`** (risk R2). The grow
  policy allocates generously up front (16 KB initial) so growth is rare, and `WMB_DEBUG_BUFFER_POISON` traps the rest.
- Verify every translation unit actually sees the modified header, with a compile-time `#error` canary. A stale copy of `Xlibint.h` in one TU is a
  silent, intermittent corruption (risk R5 in [`11-`](11-portability-plan.md) §11).

### 4.4 Definition of Done — transport slice

- [ ] `XOpenDisplay(NULL)` returns a valid `Display*` with correct `Screen` and `Visual` (step 5 of [`21-`](21-inprocess-transport.md) §10).
- [ ] `XCreateSimpleWindow` + `XMapWindow` produce a window (step 6).
- [ ] `wmb_test_client` prints **six PASS lines**: open display, create window, map, receive Expose, fill rectangle + draw string, close (V-P5-5).
- [ ] The resulting window — white, with a black rectangle and the text "WMB TEST OK" — is **visible in the SDL window** (V-P5-6).
- [ ] 1,000 `MotionNotify` posted into a 128-entry queue leave 128 events, **the newest ones**, and nothing allocated after `list_clear` (step 8).
- [ ] A deliberately dropped reply returns at `WMB_REPLY_TIMEOUT_MS` with a diagnosable error rather than hanging (step 11, risk R5).
- [ ] **Differential byte test** (V-P5-11): for the ten calls of [`41-`](41-verification-plan.md) §5.2, the bytes in `dpy->buffer` before dispatch are
  byte-identical to a checked-in golden capture of stock Xlib. **Calls 4 and 6 — a 4 KB `XChangeProperty` and the matching `XGetWindowProperty` — are
  the reason the test exists.**
- [ ] Valgrind clean over a 60 s session (V-P5-14).

## 5. Phase P6 — The Two `libX11/` Guards

| # | File | Guard | Edit |
|--:|---|---|---|
| 9 | `X11/Xlibint.h` | `WMB_INPROC` | The `Data()` macro — written at P5 (§4.1 F5‑6) |
| 10 | `libX11/src/XlibInt.c` | `WMB_INPROC` | Guard the `<sys/socket.h>` / `<sys/ioctl.h>` / `<sys/utsname.h>` block and the fd-based `_XWaitForReadable` / `_XWaitForWritable` helpers; **keep error handling and `_XEatData`** |

- [ ] Both patches replay on a pristine `libX11` checkout (S7).
- [ ] `libX11/` patch count **≤ 3** — planned 2, one file of slack (S6).
- [ ] Gate N6: no `xcb` symbol anywhere in the link.
- [ ] `grep -rl "SDL" libX11/` is empty (S1).

The `libX11/` exclusions — `xcb_io.c`, `x11_xcb.c`, `locking.c`, `CrGlCur.c`, `os2Stubs.c`, `SetLocale.c`, `Xrm.c`, `lc*`, `xlibi18n/` — are a
source-list matter, not a patch: **~8,800 LOC removed with no upstream diff at all**.

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
| `wmb_queue.c` | Slots, ready mask, `wmb_client_queue_pending()`, `wmb_collect_ready_clients()` | `io_wmb.c`, `waitfor_wmb.c` → [`42-`](42-tinyx-module-plan.md) |
| `wmb_register_internal_client()` | *Consumed* by `wmb_open.c` | Defined by [`42-`](42-tinyx-module-plan.md) |
| `wmb_main()` | Creates every fiber blocked, releases clients when the server is ready | [`45-`](45-jwm-module-plan.md), D‑33 / D‑43 |
| `wmb_app_table` | The in-process launch table | `jwm/src/command.c` → [`45-`](45-jwm-module-plan.md) |
| `wmb_api_t` | The fixed-address vtable | `wrapper/libX11-app/`, external binaries |
| `hal_task.h` | *Consumed* — request path parks on `hal_yield_timeout` | Defined by [`44-`](44-framework-module-plan.md) |
| Framework library primitives | *Consumed* — `lists.c` backs the event queue, `buffer.c` backs the request buffer | Defined by [`44-`](44-framework-module-plan.md) |

## 8. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| TR‑1 | `dpy->buffer` reallocation invalidates a live `xReq*` | **High** | **High** | §4.3; 16 KB initial allocation; `WMB_DEBUG_BUFFER_POISON`; audit the 17 `Data()` call sites |
| TR‑2 | Deadlock: a client waits for a reply that will never come | Medium | **High** | Closed by construction — `wmb_dispatch` waits with a deadline in **every** build, not just debug |
| TR‑3 | The `Data()` edit does not reach every TU | Low | **High** | A compile-time `#error` canary in the modified header |
| TR‑4 | `lists.c`'s node contract broken — a node freed while linked, or a queue outliving its `Display` | Low | **High** | One entry type, one owner; `list_unlink` clears `next`; teardown is `list_clear` in `wmb_slot_release()` |
| TR‑5 | An A5 macro is missed and segfaults in someone else's application | Medium | **High** | §6.2 — generated and CI-diffed, never typed |
| TR‑6 | Multi-part reply misclassification (R1 in [`21-`](21-inprocess-transport.md)) — header and data released as separate slots | Medium | **High** | Closed by predicate-wait design; `WMB_REPLY_TIMEOUT_MS` traps regression |

## 9. Budget

| | Value |
|---|---|
| New LOC | ~1,100 (P5) + ~700 (P8) |
| Upstream files guarded | **2 of the `libX11/` cap of 3** — one file of slack |
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
