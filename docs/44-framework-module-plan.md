# 44 — Framework Module Plan

## 1. Purpose

This is the **framework team's working document**: the substrate everything else runs on — the cooperative scheduler, the heap and ring buffers, the
client half of the in‑process transport, the external ABI, and the host that boots it all.

It is the slice of [`40-implementation-plan.md`](40-implementation-plan.md) that covers P2, P3, the client side of P5, and P8, plus the detachment
procedure that keeps `framework/` shareable. [`40-`](40-implementation-plan.md) remains the authority on phase order; this document is the authority
on what this module builds.

### 1.1 Ownership boundary

| | In this module | Elsewhere |
|---|---|---|
| Scheduler | `framework/fiber/` + `hal_task.h` / `hal_task.c` | — |
| Fiber ports | `linux_hosted/fiber_linux/`, `<target>_native/fiber_<target>/` | The display/input/time/os ports → [`43-`](43-kdrive-module-plan.md) |
| Portable library | `framework/library/` — `heap.c`, `buffer.c`, `lists.c`, `pipe.c` | The four `hal_*` device headers → [`43-`](43-kdrive-module-plan.md) |
| Transport, client half | `framework/wmb_x11/` — including `wmb_queue.c` | `tinyx/os/*_wmb.c` → [`42-`](42-tinyx-module-plan.md) |
| Xlib | All `libX11/` work: exclusions, stubs, guards #9 and #10 | — |
| External ABI | `framework/wmb_api/`, `wrapper/libX11-app/`, `apps/wmb_hello/` | — |
| Host and entry | `framework/wmb_boot.c`, `wmb_main()`, `apps/wmb_test_client/` | — |
| Build skeleton | Top‑level `CMakeLists.txt`, `cmake/WmbProfiles.cmake` | `FindLocalSDL2.cmake` → [`43-`](43-kdrive-module-plan.md); per‑group source lists → the owning module |

**Rule of thumb:** this module owns everything under `framework/`, everything under `libX11/`, and the two things that wrap the whole image — the boot
host and the external ABI.

### 1.2 The two rules this module exists to protect

| Rule | Statement | Gate |
|---|---|---|
| **L8** | The framework references nothing above itself — no `tinyx/`, no `libX11/`, no `jwm/`, no `Xlib`, no `dix` | S11 |
| **L9** | `framework/` is platform‑free — no SDL, no `<linux/>`, no `clock_gettime`, no `ucontext`, no `termios` | S12 |

**L8 is the rule most likely to be violated silently**, because it is violated by convenience: a WMB‑specific field added to `buffer.h`, a `#include`
of a WMB header in `heap.c`, an enum in `fiber_config.h` that names WMB's fibers. Each is a five‑minute change that costs a day to unpick — and each
is what makes §9's detachment a quarter instead of a week.

### 1.3 What this document is not

- **Not the transport design.** [`21-inprocess-transport.md`](21-inprocess-transport.md) specifies every function named in §6.
- **Not the ABI specification.** [`22-entry-point-abi.md`](22-entry-point-abi.md) gives the vtable layout, placement and versioning rules.
- **Not the task contract.** [`12-platform-contract.md`](12-platform-contract.md) §6 defines `hal_task_*` semantics down to the wake protocol.

## 2. The File Set

| Group | Contents | Files | ~LOC |
|---|---|---:|---:|
| `WMB_RUNTIME` | `framework/fiber/` + selected port + `heap*` + `buffer` + `pipe` + `lists` | 16 | 2,500 |
| `WMB_TRANSPORT` | `framework/wmb_x11/` | 6 | 900 |
| `WMB_API` | `framework/wmb_api/` | 2 | 260 |
| `LIBX11_API` | `libX11/src/` minus the excluded set — **carried unmodified** | ~245 | ~34,000 |
| `WMB_APPS` | `apps/` | 2 | 180 |

The 34 kLOC of `libX11/src/` is listed because this module is accountable for it linking, not because it is edited. **The entire XCB‑stub strategy is:
replace the transport, not the API.**

### 2.1 What already exists

| Artefact | State |
|---|---|
| `framework/fiber/` — `fiber.c`, `fiber_sync.c`, `hal_task.c` | Complete and portable |
| `linux_hosted/fiber_linux/` — `fiber_port.c` + `fiber_linux.cmake` | **DONE.** Builds against system libucontext when present, falls back to glibc `<ucontext.h>` when not. No vendored tree ([`90-`](90-decisions.md) D‑11 reopens only if the fallback bites) |
| `linux_hosted/sdl_fiber_example_v2/` | **DONE.** A running proof of concept of the transport handshake — gate V‑P5‑0 ([`21-`](21-inprocess-transport.md) §11) |
| `buffer.c` critical sections | **DONE.** Wired to `fiber_critical_enter/exit` under `USE_FIBER` — precondition R1 |
| `framework/wmb_x11/wmb_io.c` | 277 lines, never compiled. **Assumes a single global client**, which is wrong the moment JWM and an application coexist. A starting point, not a design |
| `framework/wmb_x11/wmb_io.h` | Zero bytes. Must be written |

## 3. Phase Participation

| Phase | This module's involvement | Weight |
|---|---|---|
| P1 | `hal_task.h` authored alongside the device headers; `-D_USE_LINUX` deleted; build skeleton | Light |
| **P2** | **Primary owner** — the fiber runtime made real | Medium |
| **P3** | **Primary owner** — `hal_task_*`, the primitive that replaces `select()` in three places | Medium |
| P4 | `wmb_boot.c` creates the server fiber and calls `TinyX_ServerFiber` | Light |
| **P5** | **Client half of the transport** — the 13 replacements, `Data()`, the queue, `wmb_test_client` | **Heavy** |
| P6 | The two `libX11/` guards | Light |
| P7 | The server‑readiness gate inside `wmb_main()`; `wmb_app_table` plumbing | Light |
| **P8** | **Primary owner** — the external ABI, the wrapper, and every measurement the target build depends on | **Heavy** |
| P9 | The target fiber port, `wmb_boot.c`'s freestanding half, `.wmb_api` placement, the link floor | Medium |

## 4. Phase P2 — Fiber Runtime

**Goal:** make `framework/fiber/` actually run. [`40-`](40-implementation-plan.md) §3.

### 4.1 Work items

| # | Item | State | Spec |
|--:|---|---|---|
| F2‑1 | Vendor libucontext | ~~Planned~~ **superseded** by `fiber_linux/` | [`90-`](90-decisions.md) D‑11 |
| F2‑2 | `fiber_port_hosted.c` | **DONE** — `linux_hosted/fiber_linux/fiber_port.c` | — |
| F2‑3 | `hosted_timer.c` — the one `pthread` | **DEFERRED** | [`90-`](90-decisions.md) D‑47 |
| F2‑4 | `hosted_intr.c` — signal‑based deferred switch | **DEFERRED** | D‑47 |
| F2‑5 | **R1** — SPSC input rings | **DONE** | D‑47 §D‑47.4; gate V‑P2‑8 |
| F2‑6 | `hosted_stack.c` — `mmap` + guard page | To do | [`30-`](30-host-build.md) §6.4 |
| F2‑7 | Hosted overrides in `fiber_config.h` | To do | [`12-`](12-platform-contract.md) §6.4 |
| F2‑8 | `fiber.cmake` conditional arch sources | To do | — |
| F2‑9 | ASan fiber annotations (`__sanitizer_start_switch_fiber`) | To do | [`30-`](30-host-build.md) §8 |
| F2‑10 | `wmb_fiber_smoke` | To do | — |

Preconditions **R2** and **R3** belong to [`43-`](43-kdrive-module-plan.md) and must be green before this phase is signed off.

### 4.2 Two things that are easy to get wrong

**Stack sizes.** The existing `fiber_config.h` defaults — `FIBER_MAX` 8, `FIBER_DEFAULT_STACK` 2048 — are microcontroller‑sized. The hosted overrides
are **not optional**: a 2 KB stack will not survive a single call into `tinyx/fb/`.

**Preemption is deliberately out of scope.** D‑47 disarmed the tick. Doing R1–R3 now rather than at the moment the tick is armed is the whole point:
they are cheap while the data races are unreachable and a debugging exercise afterwards.

### 4.3 Definition of Done

- [ ] Two fibers ping‑pong via `fiber_yield()` 100,000 times in < 100 ms, no crash (V‑P2‑1).
- [ ] `fiber_mutex_t` and `fiber_cond_t` work between fibers (V‑P2‑3).
- [ ] A deliberate stack overflow produces an immediate SIGSEGV **on the guard page**, not silent corruption (V‑P2‑4).
- [ ] Valgrind clean, with a suppression file matching whichever `ucontext` implementation the build selected (V‑P2‑6).
- [ ] ASan runs with no false positives on context switches (V‑P2‑7).
- [ ] `ps -o nlwp` shows **1** thread while D‑47 holds (V‑P2‑5).
- [ ] **R1 green** (V‑P2‑8): the tick is suppressed inside a critical section, and `buffer.c.o` references `fiber_critical_enter`.
- [ ] R2 and R3 green in [`43-`](43-kdrive-module-plan.md).
- [ ] Gate N5: no `pthread_*` symbol outside `hosted_timer.o`.

~~A fiber in a tight loop is preempted within 2× the tick period.~~ **Deferred** with the tick (V‑P2‑2). It is the gate that *arms* the hazard the
other three exist to remove, and it is not attempted until V‑P2‑8, ‑9 and ‑10 are all green.

## 5. Phase P3 — the `hal_task_*` Layer

**Goal:** give the rest of WMB a way to wait that is not an OS call. [`40-`](40-implementation-plan.md) §4.

| Item | Deliverable |
|---|---|
| `hal_task_hosted.c` | [`12-`](12-platform-contract.md) §6.3 |
| `wmb_boot.c` skeleton — heap, runtime, `wmb_main()` | [`31-`](31-target-build.md) §9, [`90-`](90-decisions.md) D‑42 |
| `wmb_task_smoke` | New |

### 5.1 Why this is a separate phase

`hal_yield_timeout` is the primitive that replaces `select()` in **three different places** — TinyX, JWM and the client transport — and every one of
them will be debugged against its semantics. **Getting the lost‑wakeup case wrong here produces intermittent hangs at P5, P7 and P9 that look like
three different bugs.**

### 5.2 Definition of Done

- [ ] `hal_task_create` spawns named fibers with the requested stack size.
- [ ] `hal_yield_timeout(50)` returns **0** after 50 ± 5 ms, over 100 iterations (V‑P3‑1).
- [ ] `hal_yield_timeout(200)` returns **1** immediately when another fiber wakes it (V‑P3‑2).
- [ ] A wake issued **before** the matching wait is not lost (V‑P3‑3).
- [ ] `hal_tick_ms()` monotonic over 60 s (V‑P3‑4 — the platform half is [`43-`](43-kdrive-module-plan.md)'s).
- [ ] Three‑fiber producer/consumer, 10,000 iterations, no lost wakeups (V‑P3‑5).
- [ ] **W5** (V‑P3‑6): the release wake is not a signal — a task created blocked, woken once, whose entry is `hal_yield_timeout(P)`, first toggles at
  t ≈ P, not t ≈ 0.
- [ ] **W2** (V‑P3‑7): wakes coalesce.
- [ ] **W3** (V‑P3‑8): one flag serves both waits.
- [ ] **W7** (V‑P3‑9): a recycled slot starts clean.
- [ ] Creation state is `CREATED` — a created‑but‑not‑woken task does not execute across 20 scheduler rounds (V‑P3‑10).

V‑P3‑6 through ‑9 exist because each is invisible to a test that only checks "the task ran": every one produces a timing bug rather than a functional
one, and **W5 in particular corrupts exactly the bring‑up order D‑43 prescribes** for P7.

## 6. Phase P5 — The Client Half of the Transport

**Goal:** `XCreateWindow(dpy, …)` reaches `ProcCreateWindow(client)` by function call, with the request still sitting in `dpy->buffer` where Xlib
built it. [`40-`](40-implementation-plan.md) §6, [`21-`](21-inprocess-transport.md) §3, §4.

### 6.1 Work items

| # | Item | Est. LOC | Spec |
|--:|---|---:|---|
| F5‑1 | `wmb_queue.c` — slots, ready mask, client slots | — | [`21-`](21-inprocess-transport.md) §5.1, §5.3 |
| F5‑2 | `wmb_open.c` — in‑process `XOpenDisplay`: build `Display`, register the `ClientRec`, install `dpy->synchandler`, plus the locale/i18n no‑op stubs | ~140 | [`21-`](21-inprocess-transport.md) §3.2 |
| F5‑3 | `wmb_setup.c` — synthesise the `xConnSetup` blob `XOpenDisplay` normally reads off the wire | ~120 | [`21-`](21-inprocess-transport.md) §3.3 |
| F5‑4 | `wmb_close.c` | ~60 | [`21-`](21-inprocess-transport.md) §3.2 |
| F5‑5 | `wmb_transport.c` — **the thirteen `xcb_io.c` replacements**, grown from the `wmb_io.c` sketch | ~280 | [`21-`](21-inprocess-transport.md) §3.4 |
| F5‑6 | `X11/Xlibint.h` `Data()` edit — on overflow grow `dpy->buffer` instead of calling `_XSend` | 1 macro | [`21-`](21-inprocess-transport.md) §3.5 |
| F5‑7 | `wmb_dispatch` / `wmb_dispatch_done` — post, `fiber_notify()`, park on a predicate with a deadline | — | [`21-`](21-inprocess-transport.md) §4.1 |
| F5‑8 | Event path — `wmb_event.c`, the `lists.c`‑backed queue with motion compression | — | [`21-`](21-inprocess-transport.md) §6 |
| F5‑9 | `apps/wmb_test_client/` | ~110 | [`01-`](01-component-inventory.md) §11.2 |
| F5‑10 | Debug aids: `WMB_DEBUG_TRACE_REQUESTS`, `WMB_DEBUG_REPLY_TIMEOUT_MS`, `WMB_DEBUG_BUFFER_POISON`, `WMB_DEBUG_FIBER_NAMES`, the `.gdbinit` helper | — | [`30-`](30-host-build.md) §8 |

### 6.2 What is already settled, and what is not

The handshake does **not** have to be discovered here. [`21-`](21-inprocess-transport.md) §11 records a running proof of concept — 40 requests posted,
40 served, 40 completions, **0 timeouts**, every draw on the server fiber — which rules out the three failure modes a design review cannot: a lost
notify, an early release, and a buffer released under a running server.

**P5 supplies the X11 half**: replies with payload, the event path, errors and sequence numbers, `InsertFakeRequest`, grabs, and the
`BlockHandler`/`WakeupHandler` pair. Those stay gates.

### 6.3 The `Data()` edit is the highest‑leverage line in the project

One macro in `X11/Xlibint.h`, guarded by `WMB_INPROC`. **It is what lets 245 unmodified Xlib API files work.** Two consequences to hold in mind while
writing it:

- Upstream code does `GetReq(...); req->field = x; Data(...); req->other = y;` — **a reallocation invalidates a live `xReq*`** (risk R2). The grow
  policy allocates generously up front (16 KB initial) so growth is rare, and `WMB_DEBUG_BUFFER_POISON` traps the rest.
- Verify every translation unit actually sees the modified header, with a compile‑time `#error` canary. A stale copy of `Xlibint.h` in one TU is a
  silent, intermittent corruption (risk R5 in [`11-`](11-portability-plan.md) §11).

### 6.4 Definition of Done — framework slice

- [ ] `XOpenDisplay(NULL)` returns a valid `Display*` with correct `Screen` and `Visual` (step 5 of [`21-`](21-inprocess-transport.md) §10).
- [ ] `XCreateSimpleWindow` + `XMapWindow` produce a window (step 6).
- [ ] `wmb_test_client` prints **six PASS lines**: open display, create window, map, receive Expose, fill rectangle + draw string, close (V‑P5‑5).
- [ ] The resulting window — white, with a black rectangle and the text "WMB TEST OK" — is **visible in the SDL window** (V‑P5‑6).
- [ ] 1,000 `MotionNotify` posted into a 128‑entry queue leave 128 events, **the newest ones**, and nothing allocated after `list_clear` (step 8).
- [ ] A deliberately dropped reply returns at `WMB_REPLY_TIMEOUT_MS` with a diagnosable error rather than hanging (step 11, risk R5).
- [ ] **Differential byte test** (V‑P5‑11): for the ten calls of [`41-`](41-verification-plan.md) §5.2, the bytes in `dpy->buffer` before dispatch are
  byte‑identical to a checked‑in golden capture of stock Xlib. **Calls 4 and 6 — a 4 KB `XChangeProperty` and the matching `XGetWindowProperty` — are
  the reason the test exists.**
- [ ] Valgrind clean over a 60 s session (V‑P5‑14).

## 7. Phase P6 — The Two `libX11/` Guards

| # | File | Guard | Edit |
|--:|---|---|---|
| 9 | `X11/Xlibint.h` | `WMB_INPROC` | The `Data()` macro — written at P5 (§6.1 F5‑6) |
| 10 | `libX11/src/XlibInt.c` | `WMB_INPROC` | Guard the `<sys/socket.h>` / `<sys/ioctl.h>` / `<sys/utsname.h>` block and the fd‑based `_XWaitForReadable` / `_XWaitForWritable` helpers; **keep error handling and `_XEatData`** |

- [ ] Both patches replay on a pristine `libX11` checkout (S7).
- [ ] `libX11/` patch count **≤ 3** — planned 2, one file of slack (S6).
- [ ] Gate N6: no `xcb` symbol anywhere in the link.
- [ ] `grep -rl "SDL" libX11/` is empty (S1).

The `libX11/` exclusions — `xcb_io.c`, `x11_xcb.c`, `locking.c`, `CrGlCur.c`, `os2Stubs.c`, `SetLocale.c`, `Xrm.c`, `lc*`, `xlibi18n/` — are a
source‑list matter, not a patch: **~8,800 LOC removed with no upstream diff at all**.

## 8. Phase P8 — External ABI and Stability

**Goal:** prove the external application story, and prove the system survives. [`40-`](40-implementation-plan.md) §9, [`22-`](22-entry-point-abi.md).

### 8.1 Work items

| # | Item | Spec |
|--:|---|---|
| F8‑1 | `framework/wmb_api/api.def` + generator | [`22-`](22-entry-point-abi.md) §5.1 |
| F8‑2 | `wmb_api.h`, `wmb_api_table.c` | [`22-`](22-entry-point-abi.md) §3, §4.1 |
| F8‑3 | **A5 enumeration** → `docs/xlib-display-macros.txt`, generated from `X11/Xlib.h` | §8.2, [`91-`](91-open-questions.md) Q20 |
| F8‑4 | `wrapper/libX11-app/` + the macro compatibility header, driven by F8‑3 | [`22-`](22-entry-point-abi.md) §5 |
| F8‑5 | `wmb_bridge.c` — the host validation scaffold | [`22-`](22-entry-point-abi.md) §6 |
| F8‑6 | MPSC producer side on the request queue + a **polled** server‑wake mailbox | [`22-`](22-entry-point-abi.md) §7 E4, [`90-`](90-decisions.md) D‑44 |
| F8‑7 | `apps/wmb_hello/` | [`01-`](01-component-inventory.md) §11.2 |
| F8‑8 | Heap and stack instrumentation, **tagged as WMB's** rather than whole‑heap | §9.3 step 4 |

### 8.2 The A5 enumeration is generated, not typed

`DefaultScreen`, `RootWindow`, `BlackPixel`, `DisplayWidth`, `ScreenCount`, `NextRequest`, `ConnectionNumber` and their `*OfScreen` variants all
expand to a dereference of a `Display` or `Screen` struct **that the wrapper does not have**. Missing one does not fail to compile — it segfaults at
first use, in an application we did not write.

So the list is extracted mechanically from `X11/Xlib.h` (every macro whose expansion mentions its `Display` or `Screen` parameter), checked in, and
regenerated in CI so that an upstream header change appears as a diff rather than as a field report.

### 8.3 Definition of Done

- [ ] `wmb_api.h`, `wrappers.c` and the slot count are all generated from `api.def`; regeneration produces no diff (S9).
- [ ] `docs/xlib-display-macros.txt` is generated, checked in, and regeneration produces no diff.
- [ ] A test TU **using every macro in that list** compiles against the wrapper's compatibility header and produces no reference to any `Display` or
  `Screen` field — in the wrapper, both types are opaque.
- [ ] `libX11-app.a` is **< 16 KB** and exports only the wrapper entry points plus `wmb_bind` (V‑P8‑1).
- [ ] `wmb_hello`, a separate process linking only the wrapper, produces a **visible, JWM‑decorated window** over the bridge (V‑P8‑2).
- [ ] An application built against ABI v1 runs against a v2 WMB (V‑P8‑3).
- [ ] **Re‑entrancy (E4)**: two bridge clients posting from two OS threads, deliberately preempting each other, produce correct results and lose no
  requests. The contention is on the queue and the wake, **not on `dix`**, which the server fiber still serialises. This is the only test in the
  project that deliberately uses threads, and it tests the boundary, not WMB.
- [ ] **Leak (V‑P8‑4)**: 10,000 window create/destroy cycles leave the heap high‑water mark flat, **attributed to WMB rather than to the whole heap**.
- [ ] **Stability (V‑P8‑5)**: a one‑hour run with scripted continuous input — flat heap, no fiber canary trip, no fiber starvation.
- [ ] **Measurements recorded (V‑P8‑6)**: per‑fiber stack high‑water, heap high‑water and image size written into [`31-`](31-target-build.md) §5.2 and
  §8.1, replacing the estimates.

**V‑P8‑6 is the phase's obligation to the target build.** Sizing a target's static stacks and heap from an estimate is how embedded projects run out
of memory in the field.

## 9. Detachment — Specified Now, Executed Later

[`40-`](40-implementation-plan.md) §13 in full is this module's charter. Everything above builds **one executable** — `wmb_host` = `wmb_boot.c` +
framework + windowing set + backend — and that is the only configuration built or tested ([`90-`](90-decisions.md) D‑42). It is not the end shape.

### 9.1 Trigger

Any one of: a second component needs to run in the same image; the windowing set needs lifting into another project; something outside WMB needs
fibers, the heap or a ring buffer.

### 9.2 What must hold continuously

| Rule | What it prevents | Gate |
|---|---|---|
| **L7** — the windowing set references only the contract, the ABI and the framework | `tinyx/` quietly calling into boot or the backend | S10 |
| **L8** — the framework references nothing above itself | The framework becoming WMB‑specific and unshareable | S11 |
| **D‑42's lifecycle boundary** — WMB is entered through `wmb_main()`, never by owning `main()` | Step 2 below becoming a refactor instead of a file move | `nm libwmb.a \| grep ' T main$'` — **runnable today** |

### 9.3 The procedure

| # | Step | Done when |
|---|---|---|
| 1 | **Fix the public surface.** `fiber.h`, `hal_task.h`, `heap.h`, `buffer.h` are the API; everything else under `framework/fiber/` and `framework/library/` is private | The windowing set compiles with only the public headers on the include path |
| 2 | **Extract the host.** `wmb_boot.c` moves to `host/` and belongs to no `libwmb*.a` | `nm libwmb.a \| grep ' T main$'` is empty |
| 3 | **Stop assuming WMB initialises the runtime.** `wmb_main()` must not call `heap_init` or `fiber_runtime_init` | `wmb_main()` succeeds against a runtime someone else brought up |
| 4 | **Account WMB's heap usage separately.** | The leak gate reports a flat **WMB** high‑water mark while another component allocates concurrently |
| 5 | **Declare a fiber budget.** `FIBER_MAX` is a compile‑time global; WMB reserves slots rather than assuming the table is its own | `wmb_main()` fails cleanly and diagnosably when the host under‑provisioned |
| 6 | **Package.** `libwmb_framework.a` + `libwmb.a` + public headers + a one‑page manifest of what a host must supply | A build outside this tree links against them |
| 7 | **Prove it.** A trivial second component — a fiber that advances a counter and yields — running alongside WMB | The UI still works, the counter advances, neither starves |

**Step 7 is the only one that proves anything.** Steps 4 and 5 are the two that are invisible until a second tenant exists, which is exactly why they
are written down now.

## 10. Phase P9 — The Target Half

| Item | Spec |
|---|---|
| `<target>_native/fiber_<target>/` — `fiber_port.c` + `<arch>.S`, the five `fiber_port.h` symbols | [`31-`](31-target-build.md) §5.1 |
| Static fiber stacks, **sized from P8 measurements** | [`31-`](31-target-build.md) §5.2 |
| `wmb_boot.c` freestanding half — heap region, runtime init, `wmb_main()` | [`31-`](31-target-build.md) §9 |
| Linker script + `.wmb_api` placement at `WMB_LOAD_BASE` | [`22-`](22-entry-point-abi.md) §4.1 |
| `docs/floor-symbols.txt`, generated from [`11-`](11-portability-plan.md) §9.1 + §9.2 and checked in | [`11-`](11-portability-plan.md) §9.3 |

- [ ] Exactly one `main`, from `wmb_boot.o` (N7).
- [ ] `readelf -r` shows no dynamic relocations (N9); `.wmb_api` is at offset 0 and `WMB_API_ADDRESS == WMB_LOAD_BASE` (N10).
- [ ] The link references nothing outside `docs/floor-symbols.txt` (N8).
- [ ] No forbidden symbol: `socket`, `select`, `poll`, `fork`, `exec*`, `sigaction`, `getenv`, `fopen`, `dlopen`, `pthread_*` (N1–N4).

## 11. Interfaces With Other Modules

| Seam | This module provides | Consumer |
|---|---|---|
| `hal_task.h` — 9 functions | `hal_task_create`, `hal_task_wait`, `hal_task_wake`, `hal_yield_timeout`, … | `tinyx/os/waitfor_wmb.c`, `jwm/src/event.c`, `tima_kdrive_system.c` |
| `fiber_critical_enter/exit` | Tick‑suppressing critical sections | `buffer.c`, `hal_display_flush` (R3) |
| `fiber_port.h` — 5 symbols | *Consumed* from each port tree | [`43-`](43-kdrive-module-plan.md) supplies the peer device ports |
| `wmb_queue.c` | Slots, ready mask, `wmb_client_queue_pending()`, `wmb_collect_ready_clients()` | `io_wmb.c`, `waitfor_wmb.c` → [`42-`](42-tinyx-module-plan.md) |
| `wmb_register_internal_client()` | *Consumed* by `wmb_open.c` | Defined by [`42-`](42-tinyx-module-plan.md) |
| `wmb_main()` | Creates every fiber blocked, releases clients when the server is ready | [`45-`](45-jwm-module-plan.md), D‑33 / D‑43 |
| `wmb_app_table` | The in‑process launch table | `jwm/src/command.c` → [`45-`](45-jwm-module-plan.md) |
| `wmb_api_t` | The fixed‑address vtable | `wrapper/libX11-app/`, external binaries |

## 12. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| FW‑1 | `dpy->buffer` reallocation invalidates a live `xReq*` | **High** | **High** | §6.3; 16 KB initial allocation; `WMB_DEBUG_BUFFER_POISON`; audit the 17 `Data()` call sites |
| FW‑2 | Lost‑wakeup semantics wrong in `hal_task_*` | Medium | **High** | V‑P3‑3, V‑P3‑6..‑9 — four gates for four rules, because each failure looks like a different bug three phases later |
| FW‑3 | Deadlock: a client waits for a reply that will never come | Medium | **High** | Closed by construction — `wmb_dispatch` waits with a deadline in **every** build, not just debug |
| FW‑4 | The `Data()` edit does not reach every TU | Low | **High** | A compile‑time `#error` canary in the modified header |
| FW‑5 | `lists.c`'s node contract broken — a node freed while linked, or a queue outliving its `Display` | Low | **High** | One entry type, one owner; `list_unlink` clears `next`; teardown is `list_clear` in `wmb_slot_release()` |
| FW‑6 | An A5 macro is missed and segfaults in someone else's application | Medium | **High** | §8.2 — generated and CI‑diffed, never typed |
| FW‑7 | **L8 violated silently** by a convenience edit to `buffer.h` or `fiber_config.h` | **High** | Medium | Gate S11 on every build; §9.2 |
| FW‑8 | The glibc `<ucontext.h>` fallback misbehaves on a developer machine | Low | Medium | [`90-`](90-decisions.md) D‑11 reopens vendoring if it bites; the Valgrind suppression file already tracks which implementation was selected |

## 13. Budget

| | Value |
|---|---|
| New LOC | ~300 (P2) + ~350 (P3) + ~1,100 (P5) + ~700 (P8) + ~400 (P9 share) |
| Upstream files guarded | **2 of the `libX11/` cap of 3** — one file of slack |
| Files this module *removes* from the build with no diff | ~10 kLOC of `libX11/src/` |

## 14. What to Do First

1. **Write the top‑level `CMakeLists.txt` and `cmake/WmbProfiles.cmake`.** Nothing can be built until the build exists, and today's
   `framework/CMakeLists.txt` hard‑codes an absolute SDL path that will not survive contact with a second machine.
2. **Author `hal_task.h`** alongside [`43-`](43-kdrive-module-plan.md)'s four device headers, so that all five clear gate S8 together.
3. **Finish P2's three open items** — `hosted_stack.c`'s guard page, the `fiber_config.h` overrides, the ASan annotations — then `wmb_fiber_smoke`.
   The guard page and the ASan annotations both pay for themselves within days: without them, stack overflows are silent and ASan is unusable.

Then P3 in full, before anything touches the transport.
