# 90 — Decision Log

## 1. Purpose

Settled decisions, with the alternatives that lost and the reason. It is a deliverable of workstream 6 ([`00-introduction.md`](00-introduction.md) §5.6).

Its job is to stop the same argument being had twice. Several of these were argued at length across four generations of documents in `docs_old/`; they are
recorded here so that the argument does not have to be reconstructed from a session summary.

**Reopening a decision** requires: a new entry superseding the old one, a statement of what changed, and an update to every document that cites it.

### 1.1 Status vocabulary

| Status | Meaning |
|---|---|
| **SETTLED** | Decided. Do not re‑litigate without a superseding entry |
| **PROVISIONAL** | Decided for now; a named event may reopen it |
| **SUPERSEDED** | Replaced by a later entry, kept for the record |

## 2. Requirements‑Level Decisions

### D‑01 — Single binary, no socket transport · SETTLED

TinyX + libX11 + JWM + applications link into one image; client and server communicate by direct function call.

| Alternative | Why rejected |
|---|---|
| Keep the socket, run everything in one process | The socket is the OS dependency being removed. It imports `select`, non‑blocking I/O, `SIGPIPE`, credentials, `xtrans` |
| Named pipes / shared memory ring | Still a transport, still needs the OS, still serialises |

**Consequences:** [`20-`](20-transport-inventory.md), [`21-`](21-inprocess-transport.md). No X11 network transparency; no remote clients on target.

### D‑02 — TinyX/KDrive as the X server · SETTLED

| Alternative | Why rejected |
|---|---|
| Full X.Org `xserver` | Vastly larger; assumes DRM/KMS, udev, a module loader |
| A from‑scratch X server | Reimplements 26 kLOC of `dix` protocol handling with no upstream to fall back on |
| A non‑X windowing system | Loses the "zero learning curve for X11 developers" requirement |

KDrive was designed for embedded framebuffer use. Its four‑vtable HAL boundary (`KdCardFuncs`/`KdKeyboardFuncs`/`KdMouseFuncs`/`KdOsFuncs`) is already the
seam we need — [`02-`](02-baseline-architecture.md) §6.1.

### D‑03 — Transport substitution in libX11, not a custom Xlib · SETTLED

Replace `xcb_io.c`'s thirteen symbols and hook `dpy->synchandler`. Leave the ~245 request‑building API files untouched.

| Alternative | Why rejected |
|---|---|
| Write a custom Xlib‑compatible library | ~37 kLOC to reimplement, with a permanent compatibility burden |
| Bypass the protocol and call `dix` functions directly | Loses error codes, sequence numbers, event ordering, extension compatibility, and requires rewriting every entry point. See D‑09 |

**Consequences:** exactly one upstream header edit (the `Data()` macro), 13 new functions. [`21-`](21-inprocess-transport.md) §3.

## 3. Architecture Decisions

### D‑04 — All server code runs on one dedicated fiber · SETTLED · reinstated by D‑46

A client fiber never calls `ProcVector[op]` on its own stack. It posts the request into its slot, wakes the server fiber, and yields. The server fiber runs
upstream's `Dispatch()` loop, unmodified.

| Alternative | Why rejected |
|---|---|
| Direct call from the client fiber — what `transport/x11/wmb_io.c` sketches today | Fourteen sites in the code we compile suspend a client *inside* a request handler and rely on the dispatcher to resume it later — `ClientSleep`, `IgnoreClient`, `ResetCurrentRequest`. A call on the client's own stack has nowhere to put that wait. Census in D‑46 |
| Run the server on the main thread with clients as callbacks | Inverts control; JWM's event loop cannot be expressed that way |

**Briefly superseded by D‑45, reinstated by D‑46.** D‑45's audit of this entry's *original* wording was correct on its own terms — `InsertFakeRequest`
really does have one call site, and cooperative fibers really do prevent interleaving. It audited a symbol rather than the hazard class, and the hazard
class is alive in fourteen places. D‑46 records the census and restores this decision on firmer ground than it was first written.

**Now demonstrated, not only argued.** `linux_hosted/sdl_fiber_example_v2` runs the post/notify/predicate handshake against the real fiber runtime: three
client fibers, one server fiber owning the device, 40 requests, 0 timeouts ([`21-`](21-inprocess-transport.md) §11, gate V‑P5‑0).

### D‑45 — A client request executes on the calling fiber's stack · SUPERSEDED by D‑46

An application fiber built its request into `dpy->buffer` and then dispatched it **on the same stack** — no server fiber for request execution, no queue —
resting on a single invariant: **no code path inside `dix` may yield.** Grab waits moved into the transport shim ahead of `dix`; what remained of
`Dispatch()` moved to a housekeeping fiber; external callers kept D‑04's post‑and‑wake model.

**Superseded.** The invariant is not one WMB can hold, because upstream breaks it deliberately in fourteen places, and bypassing `Dispatch()` discards a
per‑request contract larger than the call it replaces. D‑46 has the census, the contract inventory and the two consequences neither entry originally
weighed — request‑burst liveness and `host-dev` oracle fidelity.

**What survives, as fact rather than assertion:** `InsertFakeRequest` has exactly one call site, `dix/dispatch.c:3639` inside `NextAvailableClient()`, and
is never reached from a request handler — so D‑04's original objection was wrong as written. And `UpdateCurrentTime()` → `ProcessInputEvents()`, reached
from 19 sites across `dix/events.c`, `dix/property.c`, `Xext/shape.c`, `Xext/sync.c`, `Xext/saver.c` and `Xext/xtest.c`, is a **nested call on one stack**,
exactly as in the socket build — not concurrency, and safe under either model. Two further findings are carried into D‑46 as amendments: a full event
queue must never block, and backtrace continuity is worth buying.

### D‑46 — The server fiber executes every request; `Dispatch()` is kept unmodified · SETTLED · supersedes D‑45, reinstates D‑04

Every request — internal client, external caller, no exception — is posted to a per‑client slot and executed by the `xsrv` fiber running TinyX's own
`Dispatch()` loop. One execution model in the image, one stack inside `dix`, and the busiest file in the tree is not edited.

#### What changed

D‑45 was argued from an audit of two symbols. Re‑auditing the *hazard class* reverses the conclusion.

**1. Fourteen sites suspend a client from inside a request handler.** D‑45 established that `InsertFakeRequest` cannot re‑enter dispatch. True, and beside
the point: the mechanism handlers actually use is `ClientSleep` / `IgnoreClient` / `ResetCurrentRequest`, which returns to the dispatcher **without having
completed the request** and relies on it to resume the client later.

| Site | Mechanism |
|---|---|
| `dix/dispatch.c:1129–1134` — `ProcGrabServer`, contended | `ResetCurrentRequest` + `client->sequence--` + `BITSET(grabWaiters)` + `IgnoreClient` |
| `dix/dixfonts.c:313, 623, 649, 668, 929, 946, 1347, 1523` — `doOpenFont`, `doListFontsAndAliases`, `doListFontsWithInfo`, `doPolyText`, `doImageText` | `ClientSleep` on `Suspended` |
| `Xext/sync.c:1700` — `ProcAwaitSync` | `IgnoreClient` |
| `Xext/sleepuntil.c:135` | `IgnoreClient` |
| `Xext/xtest.c:216, 224` | `ClientSleepUntil` + `ResetCurrentRequest` |

`ClientSleep` (`dix/dixutils.c:546`) calls `IgnoreClient` and parks the client on `sleepQueue`; `ClientWakeup` calls `AttendClient`. `sync.c`,
`sleepuntil.c` and `xtest.c` are all CARRY ([`01-`](01-component-inventory.md)). D‑45's shim handled exactly one of these — the grab — with a pre‑`dix`
gate. The other thirteen have no home in that model, because the decision to suspend is taken *inside* the handler after inspecting the request, which is
precisely what "no yield inside `dix`" forbids.

**2. `Dispatch()` is a per‑request contract, not a loop around `ProcVector`.** Its inner loop (`dix/dispatch.c:407–450`) does nine things; D‑45's
`wmb_execute` reimplemented one and dropped the rest:

| `Dispatch()` does | In D‑45's shim |
|---|---|
| `ReadRequestFromClient`; `CloseDownClient` on `< 0` | Missing |
| `client->sequence++` | Missing — while [`21-`](21-inprocess-transport.md) §8 asserts the counters stay in step |
| `result > (maxBigRequestSize << 2)` → `BadLength` | Missing |
| `result != Success` → `SendErrorToClient(...)` or `CloseDownClient` | Missing — the entire error path |
| `isItTimeToYield`, set by `IgnoreClient`, `ProcKillClient`, `Xext/sync.c` | No meaning |
| `grabState == GrabKickout` → `GrabActive` | Re‑implemented in the shim |
| `FlushAllOutput()` | Missing |
| Smart‑schedule slice and priority penalty | Removed |
| `requestingClient` set and restore | Present |

**3. Liveness.** Under D‑04 a client yields once per request, so input, timers and block handlers are pumped between requests. Under D‑45 a client issuing
a burst never yields, the housekeeping fiber never runs and input stops — and with `isItTimeToYield` and the smart‑schedule slice gone, nothing breaks the
burst. On an embedded UI that is a visible freeze.

**4. The `host-dev` oracle.** D‑15 calls dual‑sourcing the highest‑value de‑risking device in the plan, because it separates "did we assemble TinyX
correctly?" from "did we replace the transport correctly?". That holds only if both profiles run the same server control flow. Under D‑04 they do — only
`WaitForSomething`, `ReadRequestFromClient` and `WriteToClient` differ. Under D‑45 the socket path runs `Dispatch()` and the in‑process path bypasses it,
so a stock `xterm` stops being independent evidence about the path we ship.

> **Point 4 no longer applies.** D‑49 retired `host-dev` and the stock‑client oracle entirely. It is left in place because it was part of the argument at
> the time; D‑46 rests on points 1–3, each of which stands on its own.

**The generalisation**, and the reason this is settled rather than finely balanced:

> D‑45's safety was a global property of 78 kLOC we do not own. D‑04's is a local property of ~150 LOC we wrote.

Under D‑04, "one stack inside `dix`" holds by construction whatever a future extension, DDX change or upstream merge does. Under D‑45 each of those must be
re‑audited against an invariant upstream has never heard of, and the failure is silent.

#### The four amendments

D‑04 returns with four changes, so this is not a plain revert.

| # | Amendment | Why |
|---|---|---|
| 1 | **`Dispatch()` is kept unmodified.** The seam stays where upstream already put it — `os/io.c`, `os/WaitFor.c`, `os/connection.c` | D‑04's actual prize, and D‑14's preference order applied to the busiest file in the tree. It is also what makes D‑15 work. The dispatcher's own "this client has no more requests" edge — `ReadRequestFromClient` returning 0 — becomes the completion hook, so no patch is needed to signal the client ([`21-`](21-inprocess-transport.md) §4.1) |
| 2 | **No grab check in the shim.** `ProcGrabServer`'s native contention path handles it | Gate T4 then verifies *upstream* behaviour rather than ours. D‑45 itself flagged the shim version as easy to leave as a no‑op and hard to notice |
| 3 | **A full event queue must never block.** `io_wmb.c` grows the buffer or kills the client — X's existing semantics for a client that will not read | D‑45's finding, correct under either model. Here it is load‑bearing: the obvious implementation, waiting for the reader, deadlocks the server fiber against the client it is trying to feed |
| 4 | **Backtrace continuity is bought as a debug aid, not as an architecture.** The slot records the requesting client's fiber handle; `wmb_dump_client_stack()` prints the client's stack beside the server's | This was D‑45's largest genuine win. ~30 LOC instead of the execution model |

#### Correction carried over

D‑45's cost table mixed profiles: "448 KB → 768 KB … on a 4 MB target" used the **hosted** stack column. Derived from the target column of
[`03-`](03-target-architecture.md) §4.1, JWM plus two applications is ~240 KB under D‑04 against ~464 KB under D‑45. The multiplier was right; the absolute
figure was roughly double. One deep stack is also one stack to size — P8 measures it once rather than once per client.

#### What it costs

| Cost | Detail |
|---|---|
| Two context switches per request | ~400 ns hosted. Against a socket path three orders of magnitude slower, and performance is not why we are doing this ([`03-`](03-target-architecture.md) §4.2) |
| The slot table and ready mask exist | One slot per client plus one word, statically sized, no allocation ([`21-`](21-inprocess-transport.md) §5.1) — ~150 LOC, and it maps 1:1 onto the `fd_set` semantics it replaces (census SD1) |
| A client can wait on a wedged server | A deadlock class D‑45 did not have. The safety tick is the backstop, and "client blocked on a reply" is a watchdog‑visible state |
| The stack is severed at the queue in a debugger | Amendment 4 |

**Consequences:** [`21-`](21-inprocess-transport.md) §4 reverts to post‑and‑wake and §2's decision table stands as originally written;
[`03-`](03-target-architecture.md) §4.1 and §4.3 are restored with the arithmetic corrected. [`20-`](20-transport-inventory.md) SA4,
[`22-`](22-entry-point-abi.md) §7 E4, [`31-`](31-target-build.md) §5.2 and [`91-`](91-open-questions.md) Q21 were never updated for D‑45 and become correct
again without edits. D‑06's original clause is restored. D‑44's asymmetry between internal and external callers disappears: one model, with one atomic
remaining on the foreign‑task post path.

### D‑05 — Per‑`Display` transport state, never global · SETTLED

`WmbDisplay` hangs off `dpy->xcb`; there is no global transport state.

| Alternative | Why rejected |
|---|---|
| `static WmbState wmb_state` — what `wmb_io.c` does today | Breaks the moment JWM and one application coexist, which is the minimum useful configuration |

**Consequences:** [`21-`](21-inprocess-transport.md) §3.1. Reusing the `dpy->xcb` slot avoids any change to the `Display` layout and therefore costs no
patch budget.

### D‑06 — Zero‑copy request · SETTLED

The server reads the request directly from `dpy->buffer`.

| Alternative | Why rejected |
|---|---|
| Copy the request into a server‑owned queue | Trades a `memcpy` per request for a queue, and breaks X11's in‑order execution guarantee unless real ordering is built |

**Consequences:** the buffer is stable because the client fiber is suspended for the whole time the server holds the slot — it posts, wakes `xsrv` and
yields, and is woken only once the dispatcher is finished with the request. The slot holds a pointer, never a copy. (D‑45 briefly made this trivially true
by a different route; D‑46 restores the original clause.)

### D‑07 — `dix/main.c`'s `main()` becomes a fiber entry point · SETTLED

`main(int, char**, char**)` at `tinyx/dix/main.c:227` is guarded and renamed to `TinyX_ServerFiber(void *)`. The single `main()` in the image is
`linux_hosted/wmb_boot/wmb_boot.c`.

| Alternative | Why rejected |
|---|---|
| Keep `dix/main.c`'s `main()` and have it spawn the other fibers | The X server should not own boot. It also makes JWM's and the HAL's lifecycle subordinate to the server's argument parsing |
| A wrapper `main()` that calls `dix_main(argc, argv, envp)` | Still requires editing `dix/main.c` to remove `envp` and the `getenv` call; no saving |
| Rename with `-Dmain=TinyX_ServerMain` | Fragile: it also renames any other `main` in the same TU and confuses debuggers |

**Consequences:** one guarded upstream file (patch `tinyx/0001`). [`11-`](11-portability-plan.md) §8.1, [`31-`](31-target-build.md) §9.

### D‑08 — Replies and events are copied into per‑client slots · SETTLED

`WriteToClient` copies into the target client's reply slot or event queue.

| Alternative | Why rejected |
|---|---|
| Zero‑copy on the return path too | The reply frequently lives on the server fiber's stack (`Proc*` handlers build replies as locals). It must be copied before the server fiber continues |

**Consequences:** [`21-`](21-inprocess-transport.md) §5.5. The **append** semantics for multi‑part replies are the subtle part and are gate V‑P5‑8.

### D‑09 — The X11 wire format is preserved end to end · SETTLED

Requests are built as protocol bytes and dispatched through `ProcVector`. Only the *wire* is removed, not the *format*.

| Alternative | Why rejected |
|---|---|
| Bypass encoding: `XCreateWindow` calls `CreateWindow()` in `dix/window.c` directly | Saves one `memcpy` per request. Costs: rewriting all 245 Xlib entry points, every extension, all error‑code semantics, sequence numbers, and event ordering. It is a different project |

**Consequences:** `dix/swapreq.c` and `dix/swaprep.c` are retained even though `client->swapped` is always FALSE — they cost nothing at runtime, and
deleting ~2,500 LOC from upstream `dix/` would be a large edit for no gain.

### D‑27 — `XNextEvent` may block; it yields internally · SETTLED

An external application's `XNextEvent` loops on `wmbYield` until an event is available. It does not return early.

| Alternative | Why rejected |
|---|---|
| Return immediately when no event is queued; the application polls | Not Xlib. Every existing X application's event loop becomes a busy spin, and the failure is *silent* — the program works, it just burns the CPU |
| A platform "block this task until woken" primitive | Cleanest semantics, but it adds a fifth function to the platform contract to serve the least common case, and makes every future port harder |

`wmbYield` stays in the vtable: an application that wants a non‑blocking loop can still write one with `XPending`. What changes is that it is no longer
*obliged* to.

**Consequences:** [`22-`](22-entry-point-abi.md) §3.3, §7 E1. `wmbYield(ms)` must be callable from a platform‑owned task — it is the one contract
function a platform cannot implement as a no‑op. That is now a stated requirement rather than an open option. Closes [`91-`](91-open-questions.md) Q7.

### D‑28 — The vtable is a struct of typed function pointers · SETTLED

| | Typed struct | Flat `void *slots[]` |
|---|---|---|
| Call safety | Compiler checks every call | A cast at every call site |
| Marshalling for the bridge | Per‑slot stubs either way | Same |
| Versioning | `count` field | Same |
| Generated code | Same | Same |

Both are generated from `api.def`, so the flat array's only advantage — uniformity — is one the generator already provides. The typed struct's type
checking is worth more than that.

**Consequences:** [`22-`](22-entry-point-abi.md) §3.2 stands as written. Closes [`91-`](91-open-questions.md) Q8.

### D‑42 — WMB is a detachable component with three boundaries · SETTLED · configuration PROVISIONAL

The reusable payload is **the windowing set**: `jwm/`, `tinyx/`, `libX11/`, `transport/x11/`, `transport/api/`. It must be portable into another
project without dragging this project's boot, backend or build assumptions with it. Three boundaries define it, and nothing in the windowing set may
reference anything outside them.

| Boundary | Direction | Interface | Who is on the other side |
|---|---|---|---|
| **Platform contract** | Downward | `hal_display.h`, `hal_input.h`, `hal_task.h`, `hal_time.h`, `wmb_os.h` | A port implements it |
| **Application ABI** | Upward | `wmb_api.h` / the vtable ([`22-`](22-entry-point-abi.md)) | Other components call in |
| **Lifecycle** | Inward | `wmb_main()` (+ optional `wmb_shutdown()`) — D‑43 | Whoever hosts WMB calls it |

The lifecycle boundary is the new one, and it is what makes the other two mean anything. **WMB does not own `main()`.** `linux_hosted/wmb_boot/wmb_boot.c` is the
*whole‑system configuration's host* — one file, outside the windowing set, which every current profile happens to link. A different system supplies its own
host and calls `wmb_main()` (D‑43).

Layering rule L5 ("exactly one `main()`, in `wmb_boot.c`") is therefore a property of **the configuration**, not of WMB —
[`03-`](03-target-architecture.md) §3 says so.

| Alternative | Why rejected |
|---|---|
| WMB owns `main()` and the CPU | True today and cheap today, and it silently welds the windowing set to this project's boot. The cost of unwelding it later is every call site that assumed it |
| Draw the boundaries when a second component actually appears | They are nearly free now and expensive later. D‑07 already renames `dix/main.c`'s `main()`; making the replacement a library entry point rather than a new `main()` costs one function |

**Verification**, because the rule is worthless without it: the windowing set links against contract stubs plus the runtime substrate and **nothing else**
— no backend, no `wmb_boot.o`, no SDL. That is P1's mock‑backend link test extended to the whole set, and it is what *proves* detachability rather than
asserting it.

**PROVISIONAL on the configuration, not on the boundaries.** For now WMB *is* the whole contained system: one image, one `main()`, WMB's fibers are the
only fibers, and that is the only configuration built or tested. What is settled is that the windowing set must not **depend** on it being so.

**The detachment is specified in advance**, not left to be invented under pressure: [`40-`](40-implementation-plan.md) §13 gives the trigger, the seven
steps and their exit conditions. Two of those steps — separate heap accounting and an explicit fiber budget — are invisible until a second tenant exists,
which is precisely why they are written down while nobody needs them.

Terminology, because these documents have been using one word for three things:

| Term | Means |
|---|---|
| **The windowing set** | `jwm` + `tinyx` + `libX11-wmb` + transport + DDX — the detachable payload |
| **The runtime substrate** | Fiber core, heap, contract headers — shareable with other components (D‑31) |
| **The system image** | Substrate + windowing set + backend + host. Currently everything, and currently the only configuration |

**Consequences:** [`03-`](03-target-architecture.md) §3 L5, L7, L8; [`22-`](22-entry-point-abi.md) §7; [`31-`](31-target-build.md) §2, §9;
[`40-`](40-implementation-plan.md) §13. The lifecycle function is D‑43; the scheduling arrangement around it is D‑44.

### D‑43 — `wmb_main()` is WMB's single entry point · SETTLED

```c
/* transport/x11/wmb_main.c */

/* Bring WMB up.  Returns 0 once the server is dispatching, the window
 * manager is running and clients may connect; negative on failure.
 * Precondition: the heap and the fiber runtime are already up.
 * cfg == NULL selects the compiled-in defaults.  Call once. */
int wmb_main(const wmb_config_t *cfg);

/* Optional; never called on a target that simply runs forever. */
int wmb_shutdown(void);
```

This replaces the `wmb_init`/`wmb_start`/`wmb_stop` triple sketched in D‑42. One call is easier to hold correctly than three, and there is no useful state
between "initialised" and "started" that a caller could act on.

**What it does, in order:**

| # | Step |
|---|---|
| 1 | Validate `cfg`; fail before touching anything if the runtime is not up |
| 2 | `hal_display_init`, `hal_input_init` |
| 3 | Create **every** WMB fiber — `hal`, `xsrv`, `jwm`, the app slots — all in the blocked state |
| 4 | Release `hal` and `xsrv` only, and pump the scheduler until the server reports `InitOutput` and `InitInput` complete |
| 5 | Release `jwm` and the app fibers |
| 6 | Return 0 |

**Step 4 is the "block until ready" behaviour**, and it is why the function is synchronous. While `wmb_main()` is executing, no client fiber — and no
other component's fiber — is scheduled. WMB reaches a known state before anything can observe it, so `XOpenDisplay` cannot race `InitOutput`: nothing that
could call it is runnable yet (D‑33).

**It returns rather than parking.** Hosted, `wmb_boot.c`'s `main()` calls it and then parks. On target, the system's own `main()` calls it and carries on
with whatever else it owns. A `wmb_main()` that never returned would force WMB to be the whole system — exactly what D‑42 forbids.

| Alternative | Why rejected |
|---|---|
| `wmb_init()` + `wmb_start()` as separate calls | Three lifecycle functions where one will do. The split invites a caller to do work between them, and there is nothing safe to do there |
| `wmb_main()` never returns; it parks | Makes WMB the owner of the system. Fine hosted, wrong on target |
| Return immediately, signal readiness asynchronously | Puts the readiness protocol back into every caller — the option D‑33 already rejected |
| Take `argc`/`argv` like a real `main` | There is no command line on target, and D‑07 removed argument parsing from the server's entry. A typed `wmb_config_t` says what is actually configurable |

**Consequences.** One addition to the task contract: **`hal_task_create` creates a task in the blocked state**, runnable only after `hal_task_wake`. That
is what makes step 3 expressible without a second creation API, and it costs nothing — a created fiber could not run before its creator yielded anyway.
[`12-`](12-platform-contract.md) §6.1–§6.2, [`31-`](31-target-build.md) §9, [`40-`](40-implementation-plan.md) §13.3.

**One further precondition**, because step 4 yields: `wmb_main()` must be called from a task that belongs to the fiber runtime. The calling fiber needs
little stack — WMB's work happens on WMB's own fibers — but it must be a fiber, not an arbitrary host context.

### D‑44 — The caller owns scheduling; WMB is preemptible as a unit · SETTLED

Whoever calls `wmb_main()` owns scheduling. Hosted, that is `wmb_boot.c`'s `main()`, which parks in `hal_task_yield()`. On target it is the system's
dispatcher, which is already running a scheduler; WMB's fibers simply join its ready list. **WMB neither owns the CPU nor needs to know who does.**

WMB's only requirement of its host is the one D‑31 already states: its own fibers are scheduled cooperatively by `framework/fiber/`. Everything above that
— how many components exist, what share each gets, whether the host preempts between them — is the host's business.

**WMB's four obligations in return:**

| # | Obligation | Why it matters under a share‑limited host |
|---|---|---|
| 1 | Yield as soon as there is no work for this moment | A component that spins burns its share and gets throttled, which makes the UI *worse*. Already gated: "server idles at < 5 % CPU" |
| 2 | Tolerate arbitrary gaps in scheduling | Between any two instructions WMB may have been suspended for an unbounded time. All timing is unsigned monotonic deltas; no code may assume `hal_tick_ms()` advanced by a small amount (D‑12) |
| 3 | Keep all mutable state private to WMB | Component‑level preemption is safe *only* because nothing outside WMB observes WMB state mid‑update. Rules L7 and L8 are what keep this true |
| 4 | Serialise entry from outside | The one place obligation 3 does not hold — see below |

#### The boundary condition

> **A host may preempt WMB. A host may not preempt *within* WMB.**

Suspending the task that hosts the fiber runtime is safe at any instant: WMB resumes exactly where it left off, and no other component can observe its
intermediate state. Scheduling two of WMB's fibers concurrently — on another core, or by any mechanism that bypasses `framework/fiber/` — is **not**
supported. It reintroduces every problem D‑04 and D‑10 exclude, across 78 kLOC of upstream code that has never been audited for it.

This distinction is the whole of the decision. "Preemptive scheduling is allowed" read without it is an invitation to the second thing.

#### Where it bites, precisely

Worth being exact, because the obvious statement — "two callers could be inside `dix` at once" — is **wrong**, and the real hazard is both narrower and
in a different place.

**`dix` is not reachable concurrently.** Internal client and external caller alike post a request and wait; the `xsrv` fiber executes it (D‑04, reinstated
by D‑46). Exactly one stack is ever inside `dix`, and it is the same stack in both cases — there is no second execution model to reason about.

**Nor could `dix` tolerate concurrency if it were reachable.** TinyX contains no threading at all — no `pthread_`, no input thread — and `Dispatch()`
(`dix/dispatch.c`) sets per‑server globals for the request in flight: `requestingClient`, `grabState`, `grabWaiters[]`, `isItTimeToYield`, and in
`dix/events.c` `spriteTrace`, `filters[128]`, `DontPropagateRefCnts[]`. A stock X server is not memory‑safe under concurrent dispatch either, and does not
need to be: client concurrency in X is **multiplexing** — `select()` plus one request at a time — never parallelism. Making `dix` genuinely concurrent is
not a hardening exercise; it is a different project.

**What an external caller executes on its own task is the transport**, and that is where the hazard lives:

| Step, on the caller's task | Shared? |
|---|---|
| Xlib builds the request into `dpy->buffer` | No — per‑`Display` (D‑05) |
| `wmb_dispatch` posts into the request queue and sets the ready bit | **Yes** — concurrent read‑modify‑write on the queue and the ready mask |
| Wake the server fiber | **Yes** — a non‑fiber, preemptible context touching the runtime's ready list, which D‑10 assumes is only ever manipulated cooperatively |
| Read the reply from this client's own slot | No — per‑client (D‑08) |

So the guard belongs on the **post‑and‑wake path**, not on vtable entry. Two mechanisms, both cheap:

1. The request queue is multi‑producer / single‑consumer, with the producer side safe against preemption — one atomic on the ready mask.
2. The wake is a mailbox flag the server fiber polls, rather than a foreign task calling into the scheduler. That is D‑13's idiom — the outside never calls
   up into WMB, WMB asks — applied one layer in, and it keeps D‑10's "no locking inside WMB" intact.

**The external caller is the only actor in the image that is not a fiber.** Everything else is serialised by cooperative scheduling for free. That single
asymmetry is the whole of the problem, and it is worth one atomic and one polled flag.

| Alternative | Why rejected |
|---|---|
| Serialise every vtable entry with one lock | Broader than the hazard, and actively wrong: the guard would be held across `wmb_dispatch`'s wait for the reply, which yields. A lock held across a yield either deadlocks against the server fiber or serialises every client behind the slowest request |
| Serialise only when the host is preemptive | Makes a memory‑safety property configuration‑dependent, and the misconfiguration is silent until two callers happen to overlap |
| Make `dix` thread‑safe | See the globals above. It is a rewrite of the server, and it buys nothing — D‑04 already gives the property the serialisation was for |
| Require the host not to preempt across vtable calls | Pushes a WMB invariant into every host's scheduler configuration, where nothing checks it |

**Consequences:** [`21-`](21-inprocess-transport.md) §5.1 (the queue becomes MPSC on the producer side), [`22-`](22-entry-point-abi.md) §7,
[`40-`](40-implementation-plan.md) §9.1–§9.2. WMB's own safety tick becomes optional where the host already bounds component runtime —
`hal_task_runtime_init(0)` disables it ([`12-`](12-platform-contract.md) §6.1). Closes [`91-`](91-open-questions.md) Q21.

## 4. Runtime Decisions

### D‑10 — Cooperative fibers, not threads · SETTLED

One OS thread. Concurrency via `framework/fiber/`.

| Alternative | Why rejected |
|---|---|
| `pthread` per component | Requires auditing ~78 kLOC of upstream X server code for thread safety. Not available on target. Non‑deterministic switch points make the transport's suspend/resume protocol unimplementable |
| A single event loop with no independent stacks | JWM's event loop and the server's dispatch loop cannot both be "the" loop without inverting one of them |
| `setjmp`/`longjmp` coroutines | Single shared stack; cannot suspend mid‑call |

**Consequences:** [`12-`](12-platform-contract.md) §6.5. Exactly one `pthread` exists in the hosted build — the 1 ms tick — and it touches two variables.
Exactly one atomic exists anywhere in WMB — the transport's ready‑mask update on the external‑caller path (D‑44) — and it sits on the boundary, not
inside.

### D‑47 — The preemption safety tick is disabled until its preconditions hold · SETTLED · revisit at P2

`hal_task_runtime_init()` is called with a safety tick, but the tick **is not armed**. `fiber_port_init()` ignores `period_ms`, no timer thread is
created, and no signal is installed. Scheduling is 100 % cooperative on every profile. Re‑enabling it is gated on §D‑47.3 below, not on someone deciding
it looks ready.

This is a decision, not an omission. The tick is a *safety net* for a runaway fiber ([`12-`](12-platform-contract.md) §6.1) — it is not the scheduling
mechanism and nothing depends on it for correctness. Arming it converts every unsynchronised read‑modify‑write in WMB from unreachable to reachable, and
that set has not been audited.

#### D‑47.1 What the DDX argument does and does not cover

The argument for enabling it is: *the DDX cannot be re‑entered, because the server serves one request to completion before the next.* That is **true**, and
D‑44 already states it — `Dispatch()` runs on the `xsrv` fiber, exactly one stack is ever inside `dix`, and the DDX's own state
(`mouse_state` in `tima_kdrive_mouse.c`, `current_modifiers` in `tima_kdrive_kbd.c`) is touched only from `tima_OsPollEvents` on that same fiber.

It is also **not sufficient**, because the hazard is not between two clients. It is between the **`hal` fiber and the `xsrv` fiber**:

```
hal fiber   hal_input_pump() -> sdl_event_loop() -> *_driver_event_*() -> buffer_write()  ─┐
                                                                                          ├─ same ring
xsrv fiber  BlockHandler -> KdOsFuncs.pollEvents -> tima_*Poll() -> *_driver_read_data()  ─┘
                                                                     -> buffer_read()
```

Serialising clients says nothing about this pair. The producer is not a client.

#### D‑47.2 The three preemption‑exposed structures

| # | Structure | Producer / consumer | What a mid‑update switch does |
|---|---|---|---|
| **P‑1** | Input rings — `buffer_data_t` in `keyraw_driver.c` (20 B) and `mouse_driver.c` (128 B) | `hal` writes, `xsrv` reads | `buffer_write_internal` is a four‑step RMW: store byte, `pointer_in++`, wrap, and on overflow `pointer_out++` + wrap. A switch between the store and `pointer_in++` publishes a stale byte at a valid index; a switch inside the overflow branch moves `pointer_out` under a reader |
| **P‑2** | Damage rect in `hal_display_sdl.c` | drawing fiber writes, `hal` reads | `hal_display_flush` unions four words. A torn union presents the wrong region. Cosmetic, not corrupting |
| **P‑3** | `pending_wake` in `hal_task.c` | any fiber writes, owner clears | Already handled: test‑and‑clear is under `fiber_port_irq_lock`, including on the resume path ([`12-`](12-platform-contract.md) §6.2.2 W4) |

**`buffer.c` has no synchronisation of any kind.** `buffer_mutex_lock`/`buffer_mutex_unlock` compile to nothing unless `USE_MULTITHREAD` is defined, and it
is defined nowhere in the tree — `config_tima.h` sets `DONT_USE_MULTITHREAD`. Today that is correct and free, because a cooperative switch can only happen
at a `fiber_yield()` and neither routine yields. Under a tick it is a data race.

#### D‑47.3 Re‑enable checklist

Every item is a precondition, not a nice‑to‑have. The tick stays disabled until all four are green.

| # | Precondition | Verified by |
|---|---|---|
| **R1** | ~~The input rings are single‑producer/single‑consumer safe~~ **DONE.** `buffer.c`'s `buffer_mutex_lock`/`unlock` are wired to `fiber_critical_enter`/`fiber_critical_exit` under `USE_FIBER`. Every `buffer_*` entry point already bracketed its critical region; only the two hooks were empty | Gate V‑P2‑8 |
| **R2** | The HAL input rings never truncate a command/data pair (defect D7). `buffer.c` itself is correct as scoped by D‑48; the fix belongs in the two drivers. Required by [`12-`](12-platform-contract.md) §5.3 **today**, independent of preemption | New gate V‑P2‑9 |
| **R3** | `hal_display_flush`'s damage union is done under `fiber_critical_enter`/`exit`, or made a single word | Code review + V‑P1‑3 unchanged |
| **R4** | A preemption soak: the tick armed at 1 ms, input driven continuously for one hour, with a decoder that asserts every command byte is followed by its data byte | New gate V‑P2‑10 |

#### D‑47.4 How R1 was closed

`fiber_critical_enter()` is not a mutex. It takes `fiber_port_irq_lock()` **first**, then increments `g_critical_nesting`, which `fiber_tick_isr()` checks
and honours by returning without requesting a switch. So one call both masks interrupts and suppresses preemption, it nests, and — because the IRQ mask
precedes the counter update — it is safe to call from an ISR as well as from a fiber.

That makes it the correct primitive for this problem where a mutex is not:

| Property needed | `fiber_critical_enter` | mutex |
|---|---|---|
| Callable from an ISR (the target's producer) | ✓ never blocks | ✗ |
| Suppresses the preemption tick, not just other tasks | ✓ | ✗ |
| Costs nothing when the tick is disabled | ✓ flag + IRQ mask | ✗ allocation per buffer |
| Available before `fiber_runtime_init()` | ✓ the port's IRQ flag is static | ✗ |

Selection is by build macro, so the choice is visible at the call site's build and not baked in:

| Macro | `buffer_mutex_lock` becomes |
|---|---|
| `USE_MULTITHREAD` | `tthread_mutex_lock` — one mutex per buffer (Tima OS builds) |
| **`USE_FIBER`** | **`fiber_critical_enter`** — exported by `framework/fiber/fiber.cmake` as `FIBER_DEFINES`; any target linking the fiber core sets it |
| neither | nothing — correct only while the scheduler is cooperative |

**On target the ISR side needs no lock of its own.** An ISR cannot be preempted by a fiber, so the producer is already atomic with respect to the consumer;
it is the *fiber* side that must mask. Calling `fiber_critical_enter` from the ISR anyway is harmless (it saves and restores an already‑masked state) and
keeps one code path for both platforms.

| Alternative | Why rejected |
|---|---|
| Arm the tick now and fix races as they appear | The failure mode is a desynchronised input decoder — the keyboard starts producing wrong keysyms minutes later, in a build where the obvious suspect is the DDX. The cost of finding it exceeds the cost of the four preconditions |
| Never have a tick | A fiber that computes for 200 ms without yielding freezes the UI, and upstream code we do not own is entitled to do that. The backstop has to exist eventually (D‑31) |
| A per‑buffer mutex instead of the critical section | Wrong primitive: the target's producer is an ISR, which cannot block on a mutex, and a mutex does not suppress the preemption tick. See §D‑47.4 |
| A lock‑free SPSC ring instead of the critical section | Viable, and what [`31-`](31-target-build.md) §4.2 sketches for the target ISR ring. Rejected **for now** because it changes `buffer.c`'s overflow semantics as well as its synchronisation, and R2 has to change those semantics anyway. Revisit if the critical section shows up in a profile |

**Consequences:** [`40-`](40-implementation-plan.md) §3 P2 loses its preemption deliverable and gains R1–R4;
[`41-`](41-verification-plan.md) §4.2 V‑P2‑2 is deferred and V‑P2‑8..10 are added; [`01-`](01-component-inventory.md) §6.3 gains defect D7.
`fiber_port_init` continues to accept and ignore `period_ms`, so no call site changes when the tick is armed.

### D‑48 — `buffer.c` is a byte FIFO, not a message queue · SETTLED

`framework/library/buffer.c` carries **unstructured byte streams** — UART RX/TX and anything else where the consumer re‑synchronises on its own. It is
explicitly *not* the transport for framed records, and it is not the structure the in‑process transport uses.

That scope decision is what makes defect **D7** acceptable rather than a bug to fix. `buffer_write_internal` drops one byte when the ring is full, not a
whole record, because at this layer there is no record to preserve — a byte stream has no framing for the buffer to protect.

| Alternative | Why rejected |
|---|---|
| Make `buffer.c` record‑aware (length‑prefixed entries, pair‑atomic overflow) | Puts framing knowledge in the layer whose entire job is not to have any. Every UART user then pays for a framing model it does not use |
| One structure for both streams and messages | The two have incompatible overflow policies. A byte stream drops bytes and resynchronises; a message queue must drop whole messages or corrupt the reader permanently |

**Residual obligation, and it is not closed by this decision.** The HAL input rings in `keyraw_driver.c` and `mouse_driver.c` are `buffer_data_t`
instances carrying `HAL_CMD_*` command/data **pairs**, and [`12-`](12-platform-contract.md) §5.3 requires that overflow never truncate a pair. Those two
rings are therefore using `buffer.c` outside the scope this decision draws. Resolving that is D‑47 precondition **R2**, and the options are:

| Option | Cost |
|---|---|
| Size the rings so overflow cannot occur, and drain every `hal_input_pump` | Free, but it is a bound that has to be argued, not measured once and forgotten |
| Give the two drivers a pair‑aware wrapper over `buffer.c` (write both bytes or neither) | ~20 LOC, keeps `buffer.c` scope‑pure |
| Move the input rings to the message module | Wrong on target: the producer is an ISR and cannot allocate |

**Consequences:** [`01-`](01-component-inventory.md) §6.3 D7 is re‑scoped from "`buffer.c` defect" to "HAL input rings use `buffer.c` out of scope";
[`12-`](12-platform-contract.md) §5.3's requirement is unchanged and still owed.

### D‑11 — libucontext, vendored, for the hosted fiber port · SETTLED

| Alternative | Why rejected |
|---|---|
| glibc `<ucontext.h>` | Deprecated; absent in musl; performs a `sigprocmask` syscall per switch on some platforms |
| Hand‑written assembly for x86‑64 | We would have to write and maintain it; libucontext already covers x86‑64, ARM, AArch64, RISC‑V, MIPS |
| System libucontext package | Vendoring is reproducible and removes a build dependency |

**PROVISIONAL**: if the hosted port turns out to need only x86‑64, hand‑written assembly becomes viable and removes a vendored dependency. Reopen only if
libucontext causes trouble.

### D‑12 — Monotonic time only; no wall clock in the contract · SETTLED · amended by D‑30

`GetTimeInMillis()` becomes `hal_tick_ms()`. `tinyx/os/utils.c:388`'s `X_GETTIMEOFDAY` is removed.

| Alternative | Why rejected |
|---|---|
| Provide a wall clock too | Every consumer in the tree (screen saver, XKB repeat, smart scheduler, JWM timing) wants elapsed time. A wall clock would be provided speculatively and then depended on |

**Consequences:** [`12-`](12-platform-contract.md) §7.1. **D‑30 amends this**: a wall clock exists in the contract but is compiled out by default and may
not be called from anything below the UI layer. The substance of D‑12 — `GetTimeInMillis()` is monotonic, and no timing logic uses calendar time — is
unchanged.

### D‑13 — Polling, not callbacks, across the platform contract · SETTLED

The platform never calls up into WMB. WMB asks the platform.

| Alternative | Why rejected |
|---|---|
| Backend delivers input via a callback | Implies an OS thread or an interrupt context calling into WMB, which imposes re‑entrancy requirements a cooperative single thread cannot honour |

**Consequences:** [`12-`](12-platform-contract.md) §2 PC2, §5.3. On target, the ISR fills a lock‑free ring; the fiber polls it. The ISR never touches WMB
data structures.

### D‑29 — `heap.c` is the allocator in every profile · SETTLED

`framework/library/heap.c` backs `malloc`/`calloc`/`realloc`/`free` on host and target alike, through a redirect (`#define malloc wmb_malloc`, or
`-Wl,--wrap`).

| Alternative | Why rejected |
|---|---|
| libc `malloc` everywhere | Does not exist on target |
| libc `malloc` on host, `heap.c` on target | Divergent behaviour between profiles. A heap bug that appears only on target — where there is no Valgrind, no ASan and no guard page — is the worst possible outcome. This is the tempting option and it is the wrong one |

**Consequences:** [`01-`](01-component-inventory.md) §4.3, [`31-`](31-target-build.md) §3.1. The hosted heap high‑water measurement (gate V‑P8‑4) becomes
directly meaningful for target sizing, because it measures the same allocator — provided it is attributed to **WMB** rather than to the whole heap, which
is why P8 tags it that way from the start ([`40-`](40-implementation-plan.md) §13.3 step 4). Whether `heap.c` fragments under an X server's allocation
pattern — many small long‑lived objects mixed with occasional multi‑hundred‑KB pixmaps — becomes a P4 measurement rather than an assumption; if it proves
inadequate the fix is inside `heap.c`, not a change of allocator. Closes [`91-`](91-open-questions.md) Q1.

### D‑30 — Wall clock is optional and compiled out by default · SETTLED · amends D‑12

`hal_time.h` gains one function behind a capability macro:

```c
#ifdef WMB_HAS_WALLCLOCK
/* Seconds since 1970-01-01 UTC.  Returns 0 if the platform has no RTC
 * or it has not been set.  Never used for elapsed-time arithmetic. */
uint32_t hal_wallclock_s(void);
#endif
```

`WMB_HAS_WALLCLOCK` is **off in every current profile**.

| Alternative | Why rejected |
|---|---|
| No wall clock at all (D‑12 as originally written) | The target is expected to have an RTC. Refusing to specify the function does not remove the requirement, it guarantees that whoever needs it invents an ad‑hoc one outside the contract |
| Wall clock unconditionally present | D‑12's reasoning stands: a wall clock that is always there will be used by code that should have called `hal_tick_ms()` |

The rule that keeps both true: **nothing below the UI layer may call it.** `dix`, `mi`, `fb`, the transport, the scheduler and the screen saver use
`hal_tick_ms()`. The only sanctioned consumer is a widget that displays calendar time.

**Consequences:** [`12-`](12-platform-contract.md) §7. Enabling the macro is additive, so a target with an RTC costs one function and no restructuring.
Closes [`91-`](91-open-questions.md) Q5.

### D‑31 — WMB's internal concurrency is fibers · SETTLED

`framework/fiber/` schedules every fiber WMB owns — `hal`, `xsrv`, `jwm`, `app_N`. WMB never creates an OS thread or an RTOS task for its own components,
on either profile. Platform‑specific code is confined to the port tree — `linux_hosted/fiber_<platform>/` — and `fiber_config.h`.

**Scope, precisely.** This is a claim about WMB's *internal* concurrency, not about the system WMB runs in:

| | |
|---|---|
| WMB guarantees | Its own components run as cooperative fibers, and never block a caller's task except through `hal_yield_timeout` |
| WMB requires | To be entered from one task, and for its own fibers to be scheduled cooperatively |
| WMB does **not** require | To be the only thing running, to own `main()`, or to own the CPU |

The fiber core is **runtime substrate, not WMB‑private**. Other components in the image may create fibers on it. The obligation runs the other way: any
component sharing the runtime must honour the cooperative discipline, because a fiber that computes for 200 ms without yielding freezes the UI for 200 ms.
The safety tick is a backstop against a runaway fiber, not a substitute for yielding.

| Alternative | Why rejected |
|---|---|
| Map `hal_task_*` onto a scheduler the platform supplies | Reintroduces preemption *for WMB's own components*, and with it the requirement to audit 78 kLOC of upstream code for thread safety — D‑10's argument, one layer down |
| Decide per target | Two scheduling models means two sets of concurrency bugs, two sets of tests, and a suspend/resume protocol that has to be correct under both |

**`hal_task_*` is therefore not a platform‑implemented part of the contract.** WMB implements all nine functions once, in `framework/fiber/`. What a
platform supplies for task management is `fiber_port.h`'s five symbols ([`31-`](31-target-build.md) §5.1) — a periodic timer, a pended software interrupt,
an IRQ lock, a stack‑frame forge and a context‑switch stub — roughly 150 LOC of C and 60 lines of assembly.

**Consequences:** [`31-`](31-target-build.md) §5.3, [`12-`](12-platform-contract.md) §6.3 and §9. Near‑term work is the hosted port only; the target port
is a directory with a known shape. Closes [`91-`](91-open-questions.md) Q11. How WMB's fibers coexist with *other components'* scheduling is D‑44.

### D‑32 — `setjmp`/`longjmp` stay in the libc floor · SETTLED

They are mandatory, not conditional. Hosted and stub‑backed freestanding builds use them to give `FatalError` a defined unwind to a recovery point in
`wmb_boot.c`. A real target may instead route `FatalError` into its own fault infrastructure, in which case `wmb_os_abort()` is that platform's halt or
reset path.

| Alternative | Why rejected |
|---|---|
| Always `wmb_os_abort()`, no unwind | Fine on hardware, poor on the host: a fatal error during development should produce a diagnosable stop, not a process kill |
| Make the floor conditional on the target | A floor that varies per target is not a floor. Two functions is a cheap price for one list |

**Consequences:** [`31-`](31-target-build.md) §3.1's `<setjmp.h>` row is unconditional. Census item C5 closes. Closes
[`91-`](91-open-questions.md) Q17.

### D‑33 — Fibers are created before the scheduler starts · SETTLED

`wmb_main()` creates **every** WMB fiber up front, in one place, and no client fiber is scheduled until the server fiber reports readiness — `InitOutput`
and `InitInput` complete, `Dispatch()` about to be entered. `XOpenDisplay` therefore cannot race server initialisation, and the gate exists exactly once.

The host (`wmb_boot.c` in the current configuration) brings up the heap and the fiber runtime, then calls `wmb_main()`. It does not create WMB's fibers
itself — that would put WMB's internal structure on the far side of the lifecycle boundary (D‑42). The mechanism is D‑43 step 4.

| Alternative | Why rejected |
|---|---|
| The server sets a flag and `wmb_open.c` yields until it is set | Puts the readiness protocol in the transport, where every future client reimplements it, and spins forever if the server dies during init |
| Create the JWM fiber only after the server signals readiness (the earlier leaning) | Splits fiber creation across two points and needs the server fiber to call back into init. Gating *scheduling* instead of *creation* achieves the same thing in one place |
| Assume creation order is execution order | Wrong. Creation order is not execution order |

**Consequences:** [`31-`](31-target-build.md) §9 constraint 5, [`40-`](40-implementation-plan.md) §8.1. Boot has exactly one ordering rule, expressed
once, and the target build gets a single static fiber table. Closes [`91-`](91-open-questions.md) Q19.

## 5. Portability Decisions

### D‑14 — Adapter first, patch last, fork never · SETTLED

Preference order: don't compile the file → compile a replacement → guard a region → (forbidden) rewrite.

**Consequences:** [`11-`](11-portability-plan.md) §2. Hard patch caps: `tinyx` 8, `libX11` 3, `jwm` 4.

### D‑15 — Dual‑source transport files, selected by profile · SUPERSEDED by D‑49

Both `tinyx/os/WaitFor.c` and `tinyx/os/waitfor_wmb.c` were to stay in the tree, with CMake picking one per profile, so that a `host-dev` build serving
stock X clients could answer "is TinyX assembled correctly?" independently of "is the transport replaced correctly?".

**Superseded.** D‑49 retires `host-dev`: no build accepts a stock X client, so the second source set has no profile to be selected in. The upstream
transport files are simply never compiled. What survives is the *naming* convention — a replacement is `<original>_wmb.c` beside the original — and the
reason it works: the replacements keep the upstream symbol names, so substitution is a source‑list change rather than a patch.

### D‑49 — One X11 transport, ever; no listener, no oracle profile · SETTLED · supersedes D‑15

WMB has exactly one X11 transport in every profile: the in‑process path. TinyX's socket transport is never compiled, `WMB_ENABLE_X11_LISTENER` does not
exist, and no build accepts a connection from an unmodified X binary. Every client is either **in‑tree** (a fiber, [`03-`](03-target-architecture.md) §6.1)
or **external** (built against the wrapper library, §6.2).

The seam is the linker. `dix/dispatch.c` calls `WaitForSomething()`, `ReadRequestFromClient()` and `WriteToClient()` by name; `waitfor_wmb.c`, `io_wmb.c`
and `connection_wmb.c` define those names. That is why the in‑process port needs **zero** edits to `dix/` — not one line in `Dispatch()`, `ProcVector` or
any handler.

| Alternative | Why rejected |
|---|---|
| Keep `host-dev` as a stock‑client oracle (D‑15) | Out of scope: it requires building and maintaining a second configuration, plus a standalone JWM on the development machine, for a path we never ship |
| A `TinyXTransportOps` table bound at startup | Only pays for itself if two transports coexist, and they never do. It buys a choice CMake already makes correctly at link time, and adds an unbound‑table failure mode |
| Two X11 transports in one image, routed per client | Would restore "stock `xterm` decorated by JWM", but requires `ReadRequestFromClient` / `WriteToClient` / the grab quartet to know which kind of client they serve. Rejected as scope |

**Consequence — assembly and transport are no longer separable.** D‑15's real value was that a failure at first light had one candidate cause instead of
two. That is now gone, and the mitigation is staging rather than profiles: P4 proves link, server loop and root window with no client at all; P5 adds the
first client. See [`40-`](40-implementation-plan.md) §5.2–§5.3. Obligation T5 loses its live comparison and falls back to a golden capture
([`20-`](20-transport-inventory.md) §8).

### D‑16 — Exclude the three Linux KDrive backends wholesale · SETTLED

`tinyx/kdrive/{linux,fbdev,vesa}/` are never compiled. 6,116 LOC and most of the tree's Linux coupling removed by not listing three directories.

### D‑17 — No filesystem on target · SUPERSEDED by D‑34

Colours from `oscolor.c`'s built‑in table; a built‑in bitmap font; generic error strings. `WMB_HAS_FILESYSTEM` is off in the target profile.

| Alternative | Why rejected |
|---|---|
| Require a small filesystem for fonts and `rgb.txt` | Adds a whole subsystem to the platform contract for two data files that fit in `.rodata` |

**Consequences:** [`31-`](31-target-build.md) §7. Scalable fonts on target become a separate project; `fonts/freetype/` stays dormant.

### D‑18 — Profile macros key off capability, never OS name · SETTLED

`WMB_HOSTED` / `WMB_FREESTANDING` / `WMB_HAS_OS_TRANSPORT`, not `_USE_LINUX`.

The existing `-D_USE_LINUX` in `framework/CMakeLists.txt:30` is deleted. An OS‑name macro invites `#ifdef _USE_LINUX` blocks that encode assumptions no one
can later audit.

### D‑34 — The filesystem is an optional capability, not an exclusion · SETTLED · supersedes D‑17

The five `wmb_os_*` file functions are a **supported** part of the contract, compiled under `WMB_HAS_FILESYSTEM`. The macro defaults **off**, and WMB must
remain fully functional with it off.

| Alternative | Why rejected |
|---|---|
| D‑17's "no filesystem, ever" | Overcommitted. It turned a defaulted‑off capability into an architectural prohibition, which would have to be reversed the first time a target wants user configuration, additional fonts, or application resources |
| Filesystem mandatory | Removes the ability to run on a target with no storage at all, which is the configuration the whole project is shaped around |

What does **not** change: nothing above the contract may *require* a filesystem. Colours come from `oscolor.c`'s built‑in table, fonts are built in
(D‑38), error strings are `XErrDes.c`'s generic fallback. A filesystem is an enhancement, never a dependency — and the test that keeps that honest is that
`host-pure` builds and passes every gate with `WMB_HAS_FILESYSTEM` undefined.

**Consequences:** [`12-`](12-platform-contract.md) §8, [`31-`](31-target-build.md) §3.3, [`03-`](03-target-architecture.md) §9. Closes
[`91-`](91-open-questions.md) Q3.

### D‑35 — Double buffering on target · SETTLED

`fb.pixels` is a back buffer; `hal_display_flush` presents it. Where the display controller supports a page flip, presenting is a pointer swap at the next
vertical blank; where it does not, it is a DMA blit of the damaged rows.

| Alternative | Why rejected |
|---|---|
| Direct scanout, single buffer | Saves $W \times H \times \text{bpp}/8$ and guarantees tearing. Every intermediate state of an X repaint would be visible |
| Defer to a per‑target decision | The flush path's shape differs between the two, so deferring means the DDX flush code cannot be written |

Cost: 750 KB at 800×480×16. That is a real number on a small target, and it is now a line in the memory budget rather than a contingency. The host is
unaffected — SDL already presents from a texture. This is separate from TinyX's own `miext/shadow`, which sits above the contract.

**Consequences:** [`31-`](31-target-build.md) §4.1, §8. Closes [`91-`](91-open-questions.md) Q12 and unblocks it from Q6.

### D‑36 — newlib‑nano for the first target build · PROVISIONAL

| Alternative | Why not first |
|---|---|
| picolibc | Smaller, with better `printf` size options, but less universally present in vendor toolchains |
| Hand‑written (~600 LOC) | The right end state for a size‑constrained product, but writing it before the floor is known ([`91-`](91-open-questions.md) Q18) means writing the wrong thing |

**Reopened by:** Q6's answer, in one respect only — if the target's vendor toolchain ships picolibc and not newlib, take picolibc. The decision that
matters is "a small third‑party libc first, hand‑written later as a size optimisation", not the brand. Closes [`91-`](91-open-questions.md) Q13.

## 6. Build Decisions

### D‑19 — CMake, top‑level, not autotools · SETTLED

TinyX's `configure`/`Makefile.am` is not the production path. `dix-config.h` / `kdrive-config.h` are generated by `configure_file()`.

**Consequences:** the macro set is derived from a one‑off reference `configure` run and then curated by hand — never inherited wholesale from it. The
method is **D‑37**; the resulting macro list is [`91-`](91-open-questions.md) Q2.

### D‑20 — Static libraries per layer, not one flat source list · SETTLED

`libwmb_runtime.a`, `libwmb_hal.a`, `libtinyx.a`, `libx11_wmb.a`, `libjwm.a`.

A layering violation (e.g. `libtinyx.a` needing an SDL symbol) becomes a link error instead of a silent success.

### D‑21 — Host build first, target build last · SETTLED

Everything is developed and gated on the host. The target build is a port of a working system.

| Alternative | Why rejected |
|---|---|
| Cross‑compile from the start | Every ordinary bug becomes a cross‑compilation mystery. No debugger, no Valgrind, no ASan, no guard pages |

**PROVISIONAL refinement**: the *freestanding compile* (P9a) is worth running early, out of order, as soon as P6 is green — it surfaces libc floor
surprises cheaply. Only the *hardware bring‑up* (P9b) is genuinely last. [`40-`](40-implementation-plan.md) §10.3.

### D‑22 — SDL discovered, not hard‑coded · SETTLED

`find_package(SDL2)` with `WMB_SDL_ROOT` as an override. Replaces the absolute `/home/wifi/projects/SDL` in the current build file.

### D‑37 — Config macros: reference `configure` run, then curation · SETTLED · refines D‑19

Run `autogen.sh && configure` **once** on a pristine TinyX checkout in a scratch tree. Check the generated headers in as
`docs/reference/dix-config.reference.h` and `kdrive-config.reference.h`. Hand‑curate `framework/dix-config.h.in` and `kdrive-config.h.in` against them.
Derive the target template as a **delta** from the host template.

| Alternative | Why rejected |
|---|---|
| Read `configure.ac` alone and write the templates from it | Correct in principle; slow and error‑prone in practice. `configure.ac` computes macros through `m4` expansion and conditional chains, and a macro missed by eye is a link error in the longest phase of the project |
| Use the generated header directly as a build input | Forbidden — and this is what D‑19 was actually prohibiting. It encodes the generating machine: `HAVE_SYS_SOCKET_H`, sizes, endianness, and facilities the target does not have |
| Generate the host and target templates independently | They will drift. A silent divergence between profiles is the hardest class of bug this project can produce |

The distinction that reconciles this with D‑19: the generated header is a **reference artefact for review**, never a build input. It answers "what does
upstream expect to exist"; the curated template answers "what do we provide". The diff between the two *is* the review, and it is checked in.

Feature switches are forced to decisions already taken: RENDER/RANDR/XFIXES/DAMAGE on (D‑26), XKB off (D‑39), DBE off (D‑41), MIT‑SHM and XF86‑BIGFONT off
(D‑25).

**Consequences:** [`30-`](30-host-build.md) §4.6, [`40-`](40-implementation-plan.md) §5.1. [`91-`](91-open-questions.md) Q2 remains open for its *answer*;
its *method* is settled here.

## 7. Scope Decisions

### D‑23 — JWM as the window manager, stripped · SETTLED

All ten optional libraries off: PNG, JPEG, Xpm, cairo, librsvg, Xft, Xrender, pango, Xmu, Xinerama. XBM icons retained.

43 of JWM's 50 source files need no changes at all; its POSIX footprint is seven files.

### D‑24 — No authentication, no access control · SETTLED

`access.c`, `auth.c`, `mitauth.c`, `xdmauth.c`, `xauembed.c`, `xdmcp.c` — 5,589 LOC excluded. There is no untrusted peer in a single address space.

### D‑25 — Excluded extensions: SHM, XF86‑BIGFONT, DBE · SETTLED

| Extension | Reason |
|---|---|
| MIT‑SHM | Shared memory is meaningless with one address space |
| XF86‑BIGFONT | Obsolete; needs shared memory |
| DBE | Not required by JWM or KDrive. **PROVISIONAL** — re‑includable if an application needs it. Confirmed by audit in D‑41 |

### D‑26 — RANDR and RENDER compiled in despite being unused functionally · SETTLED

`kdrive.c` references RANDR unconditionally; `fbpict.c` and JWM reference RENDER. Compiling them is cheaper than stubbing them, and `--gc-sections`
recovers most of the size on target.

**PROVISIONAL**: if the target image is over budget, dropping RANDR (5,217 LOC) and/or RENDER (7,962 LOC) is lever 3–4 in [`31-`](31-target-build.md) §8.2
and each needs its own decision entry.

### D‑38 — Several built‑in bitmap fonts; FreeType is a later addition · SETTLED

A small set of built‑in bitmap fonts at different sizes (~150 KB `.rodata`), converted BDF→C at build time.

| Alternative | Why not |
|---|---|
| One 6×13 `fixed` font (~30 KB) | Every UI element is then the same size. Enough to prove the pipeline; not enough to ship |
| FreeType now | Needs embedded font data or a filesystem, RENDER glyph paths, and a large size budget. It is a separate project, not a phase |

FreeType remains the intended end state, and this decision places one requirement on the implementation to keep it cheap: the font path must be a
**registry** — fonts registered from a table at init — so that adding a scalable backend later is an addition to that table rather than a rewrite of the
glyph path. `fonts/freetype/` stays in the tree, dormant.

**Consequences:** [`31-`](31-target-build.md) §7, [`00-`](00-introduction.md) §4.2. Closes [`91-`](91-open-questions.md) Q4.

### D‑39 — XKB is not compiled in · SETTLED

Keyboard handling uses the core protocol's keymap model. KDrive's `kinput.c` XKB paths compile out.

| Alternative | Why rejected |
|---|---|
| Compile XKB in | ~15 kLOC larger, needs keymap data and a compiler for it, and buys internationalisation — which is explicitly out of scope ([`00-`](00-introduction.md) §4.2). A fixed embedded keyboard does not need groups, LED state or keymap compilation |

From the build's point of view this is part of D‑37's macro set and must be reflected there. Reopened only if internationalisation enters scope; nothing
else will reopen it.

**Consequences:** closes [`91-`](91-open-questions.md) Q10.

### D‑40 — `kmap.c` stays excluded; no stub is required · SETTLED

Audit result. `KdMapDevice`, `KdUnmapDevice`, `KdSetMappedMode` and `KdResetMappedMode` are declared in `kdrive/src/kdrive.h:392–398` and defined in
`kdrive/src/kmap.c`. Their **only** callers anywhere in the tree are `kdrive/vesa/vbe.c:379, 386, 415, 416` — and `kdrive/vesa/` is excluded wholesale by
D‑16. (`KdMapRegisters`, named in the original question, does not exist; the mapped‑mode pair is what `kdrive.h` actually declares.)

No entry in `excluded_stubs_wmb.c` is needed. The declarations left in `kdrive.h` are harmless — declaring a function that is never called costs nothing.

**Consequences:** census item C2 closes. The P4 link remains the final proof, but it is now expected to be silent on this rather than informative. Closes
[`91-`](91-open-questions.md) Q15.

### D‑41 — DBE stays excluded · SETTLED · confirms D‑25

Audit result. `jwm/src/` contains **zero** references to `Xdbe` or DBE. The only mention in retained TinyX code is `mi/miinitext.c` — five lines, all
inside `#ifdef DBE` — which is already covered by the `miinitext.c` guard budgeted in P4 for MIT‑SHM and XF86‑BIGFONT. No additional patch budget is
consumed.

`tinyx/dbe/` (2,620 LOC) is never compiled and `DBE` is undefined in the config macro set (D‑37). D‑25's "re‑includable if an application needs it" stands
for future applications; it is no longer provisional for the current configuration.

**Consequences:** census item C3 closes. Closes [`91-`](91-open-questions.md) Q16.

## 8. Superseded Decisions

Recorded so that reading `docs_old/` does not resurrect them.

| Old decision | Where it came from | Superseded by |
|---|---|---|
| Direct `ProcVector` call from the client fiber; global `WmbState` | `transport/x11/wmb_io.c`, and the prior `XCB_STUB_ARCHITECTURE.md` | **D‑05** for the global state, **D‑04** for the direct call. D‑45 briefly reinstated the direct call; D‑46 retired it again, this time with the census that says why |
| Dispatch on the calling client's stack; no request queue | **D‑45** | **D‑46** — fourteen sites suspend a client from inside a request handler, and `Dispatch()`'s per‑request contract is larger than the call it replaces |
| A named target OS ("Tima OS") as a design input | `docs_old/active/PROJECT_KNOWLEDGE_BASE.md` | [`00-`](00-introduction.md) §4.3 — the target is a *contract*, not a product. The name survives only as a directory label |
| `hal_drivers.h` in `linux_hosted/sdl_drivers/` as the DDX's interface | Current tree | **D‑13** + [`12-`](12-platform-contract.md); the contract moves to `hal/hal_*.h` |
| A "dual bridge" (X11 socket + WMB bridge) as an architectural feature | `docs_old/active/HOST_BUILD_DUAL_BRIDGE.md` | [`03-`](03-target-architecture.md) §7 — both are **scaffolding**, compiled out on target, and never a proof |
| Milestone scheme M0–M6 | `docs_old/active/PROJECT_KNOWLEDGE_BASE.md` §6 | [`40-`](40-implementation-plan.md) phases P1–P9 |
| Phase scheme "Phase 1–7" | `docs_old/active/IMPLEMENTATION_PLAN.md` | Same |
| `-D_USE_LINUX` | `framework/CMakeLists.txt` | **D‑18** |
| Custom Xlib rewrite | `docs_old/archive_docs2/CUSTOM_XLIB.md` | **D‑03** |
| Writing `libX11/src/wmb_transport.c` inside the libX11 tree | `docs_old/active/CLIENT_SERVER_TRANSPORT_REMOVAL.md` §7 | The transport lives in `transport/x11/`, keeping `libX11/` free of WMB‑owned files and costing zero patch budget |
| No filesystem on target, as an architectural prohibition | **D‑17** | **D‑34** — it is an optional capability, defaulted off, not an exclusion |

## 9. Decision Index

| # | Decision | Status | Primary document |
|---|---|---|---|
| D‑01 | Single binary, no socket transport | SETTLED | [`00-`](00-introduction.md) |
| D‑02 | TinyX/KDrive as the X server | SETTLED | [`01-`](01-component-inventory.md) |
| D‑03 | Transport substitution, not a custom Xlib | SETTLED | [`21-`](21-inprocess-transport.md) |
| D‑04 | One dedicated server fiber | SETTLED (reinstated by D‑46) | [`21-`](21-inprocess-transport.md) §4 |
| D‑05 | Per‑`Display` state | SETTLED | [`21-`](21-inprocess-transport.md) §3.1 |
| D‑06 | Zero‑copy request | SETTLED | [`21-`](21-inprocess-transport.md) §4.2 |
| D‑07 | `dix/main.c` → fiber entry | SETTLED | [`11-`](11-portability-plan.md) §8.1 |
| D‑08 | Copy replies into per‑client slots | SETTLED | [`21-`](21-inprocess-transport.md) §5.5 |
| D‑09 | Preserve the wire format | SETTLED | [`21-`](21-inprocess-transport.md) §2 |
| D‑10 | Cooperative fibers, not threads | SETTLED | [`12-`](12-platform-contract.md) §6.5 |
| D‑11 | Vendored libucontext | PROVISIONAL | [`30-`](30-host-build.md) §6.1 |
| D‑12 | Monotonic time only | SETTLED (amended by D‑30) | [`12-`](12-platform-contract.md) §7.1 |
| D‑13 | Polling, not callbacks | SETTLED | [`12-`](12-platform-contract.md) §2 |
| D‑14 | Adapter first, patch last | SETTLED | [`11-`](11-portability-plan.md) §2 |
| D‑15 | Dual‑source transport files | SUPERSEDED by D‑49 | [`11-`](11-portability-plan.md) §7 |
| D‑16 | Exclude the Linux KDrive backends | SETTLED | [`01-`](01-component-inventory.md) §6.4 |
| D‑17 | No filesystem on target | SUPERSEDED by D‑34 | [`31-`](31-target-build.md) §7 |
| D‑18 | Capability macros, not OS names | SETTLED | [`12-`](12-platform-contract.md) §3 |
| D‑19 | CMake, not autotools | SETTLED | [`30-`](30-host-build.md) |
| D‑20 | Static libraries per layer | SETTLED | [`30-`](30-host-build.md) §4.4 |
| D‑21 | Host first, target last | PROVISIONAL | [`40-`](40-implementation-plan.md) |
| D‑22 | SDL discovered, not hard‑coded | SETTLED | [`30-`](30-host-build.md) §5 |
| D‑23 | JWM, stripped | SETTLED | [`01-`](01-component-inventory.md) §11.1 |
| D‑24 | No auth, no access control | SETTLED | [`01-`](01-component-inventory.md) §7 |
| D‑25 | Exclude SHM, BIGFONT, DBE | SETTLED (DBE confirmed by D‑41) | [`01-`](01-component-inventory.md) §8.1 |
| D‑26 | Keep RANDR and RENDER | PROVISIONAL | [`31-`](31-target-build.md) §8.2 |
| D‑27 | `XNextEvent` blocks by yielding internally | SETTLED | [`22-`](22-entry-point-abi.md) §3.3 |
| D‑28 | Typed‑struct vtable | SETTLED | [`22-`](22-entry-point-abi.md) §3.2 |
| D‑29 | `heap.c` is the allocator in every profile | SETTLED | [`01-`](01-component-inventory.md) §4.3 |
| D‑30 | Optional wall clock, off by default | SETTLED | [`12-`](12-platform-contract.md) §7 |
| D‑31 | WMB's internal concurrency is fibers | SETTLED | [`31-`](31-target-build.md) §5.3 |
| D‑32 | `setjmp`/`longjmp` stay in the floor | SETTLED | [`31-`](31-target-build.md) §3.1 |
| D‑33 | Create fibers first, start the scheduler on readiness | SETTLED | [`31-`](31-target-build.md) §9 |
| D‑34 | Filesystem is an optional capability | SETTLED | [`12-`](12-platform-contract.md) §8 |
| D‑35 | Double buffering on target | SETTLED | [`31-`](31-target-build.md) §4.1 |
| D‑36 | newlib‑nano first | PROVISIONAL | [`31-`](31-target-build.md) §3.1 |
| D‑37 | Reference `configure` run, then curation | SETTLED | [`30-`](30-host-build.md) §4.6 |
| D‑38 | Several built‑in bitmap fonts; FreeType later | SETTLED | [`31-`](31-target-build.md) §7 |
| D‑39 | XKB off | SETTLED | [`30-`](30-host-build.md) §4.6 |
| D‑40 | `kmap.c` excluded, no stub needed | SETTLED | [`01-`](01-component-inventory.md) §6.4 |
| D‑41 | DBE excluded, confirmed by audit | SETTLED | [`01-`](01-component-inventory.md) §8.1 |
| D‑42 | WMB is detachable: contract, ABI, lifecycle | SETTLED (configuration PROVISIONAL) | [`03-`](03-target-architecture.md) §3 |
| D‑43 | `wmb_main()` is the single entry point | SETTLED | [`31-`](31-target-build.md) §9 |
| D‑44 | Caller owns scheduling; WMB preemptible as a unit | SETTLED | [`22-`](22-entry-point-abi.md) §7 |
| D‑45 | Requests execute on the calling fiber's stack | SUPERSEDED by D‑46 | [`21-`](21-inprocess-transport.md) §4 |
| D‑46 | The server fiber executes every request; `Dispatch()` unmodified | SETTLED | [`21-`](21-inprocess-transport.md) §4 |
| D‑47 | Preemption safety tick disabled until its preconditions hold | SETTLED (revisit at P2) | [`12-`](12-platform-contract.md) §6 |
| D‑48 | `buffer.c` is a byte FIFO, not a message queue | SETTLED | [`21-`](21-inprocess-transport.md) §2.1 |
| D‑49 | One X11 transport, ever; no listener, no oracle profile | SETTLED | [`11-`](11-portability-plan.md) §7 |

D‑46 is load‑bearing and easy to under‑read. Every request, from every kind of client, is executed by the `xsrv` fiber running upstream's `Dispatch()`; no
client ever calls `ProcVector` on its own stack. The safety property is **structural** — only one fiber can enter `dix` — rather than a rule imposed on
upstream code, and the four amendments in D‑46 are what distinguish it from a plain revert to D‑04.

D‑49 is its counterpart on the build side, and just as easy to under‑read in the other direction. Because only the in‑process transport is ever compiled and
the replacement files keep the upstream symbol names, the entire port is expressible as *a source list* plus new files. `dix/dispatch.c` is not edited, not
wrapped and not indirected — the `WaitForSomething()` call it already contains is the seam. The price is that no unmodified X client can ever connect, and
that server assembly can no longer be proven independently of the transport.
