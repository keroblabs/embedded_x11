# 42 — TinyX Module Plan

## 1. Purpose

This is the **TinyX team's working document**: the slice of [`40-implementation-plan.md`](40-implementation-plan.md) that lands inside `tinyx/` above
the DDX — the protocol core, the renderer, the extensions, and the `tinyx/os/` layer where the transport used to live.

[`40-`](40-implementation-plan.md) remains the authority on **phase order and gate colour**. This document is the authority on **what this module
builds inside each phase**, and it is written so that the TinyX work can be planned, staffed and reviewed without reading the other three module plans
end to end.

### 1.1 Ownership boundary

| | In this module | Elsewhere |
|---|---|---|
| Server core | `tinyx/dix/`, `tinyx/mi/`, `tinyx/fb/`, `tinyx/miext/` | — |
| Extensions | `tinyx/render/`, `randr/`, `xfixes/`, `damageext/`, selected `tinyx/Xext/` | — |
| OS layer | all of `tinyx/os/`, including the three `*_wmb.c` replacements | — |
| Generated config | `cmake/dix-config.h.in`, `cmake/kdrive-config.h.in` | `kdrive-config.h`'s KDrive‑facing macros are reviewed by [`43-`](43-kdrive-module-plan.md) |
| DDX and KDrive framework | — | `tinyx/kdrive/` → [`43-`](43-kdrive-module-plan.md) |
| Client half of the transport | — | `framework/wmb_x11/`, `libX11/` → [`44-`](44-framework-module-plan.md) |
| The request queue | — | `wmb_queue.c` is a framework artefact ([`01-`](01-component-inventory.md) §12, group `WMB_TRANSPORT`); this module *consumes* it |

**Rule of thumb:** if the file lives under `tinyx/` and is not under `tinyx/kdrive/`, it is this module's.

### 1.2 What this document is not

- **Not the file inventory.** [`01-component-inventory.md`](01-component-inventory.md) §7, §8.
- **Not the transport design.** [`21-inprocess-transport.md`](21-inprocess-transport.md) §5 specifies every function named here.
- **Not the gate definitions.** [`41-verification-plan.md`](41-verification-plan.md) owns the V‑ and S‑/N‑ identifiers.

## 2. The File Set

From [`01-`](01-component-inventory.md) §12, the module groups this team owns:

| Group | Contents | Files | ~LOC |
|---|---|---:|---:|
| `TINYX_DIX` | `tinyx/dix/*.c` | 25 | 26,685 |
| `TINYX_OS` | `log.c`, `oscolor.c`, `strlcat.c`, `strlcpy.c`, `osinit.c`, `utils.c` + the three `*_wmb.c` replacements | 9 | 4,100 |
| `TINYX_MI` | `tinyx/mi/*.c` | 35 | 23,533 |
| `TINYX_FB` | `tinyx/fb/*.c` | 32 | 14,437 |
| `TINYX_MIEXT` | `miext/shadow/*.c` + `miext/damage/damage.c` | 21 | 3,514 |
| `TINYX_EXT` | `render/` + `randr/` + `xfixes/` + `damageext/` + selected `Xext/` | 39 | 24,000 |

**~96 kLOC across 161 translation units**, of which this project writes roughly **570 lines** (the three `*_wmb.c` replacements plus
`excluded_stubs_wmb.c`) and guards **seven files**. Everything else is upstream, carried unmodified. That ratio is the point: the module's difficulty
is *assembly*, not authorship.

### 2.1 The exclusion ledger this module must hold

[`01-`](01-component-inventory.md) §13 and [`11-`](11-portability-plan.md) §4.1. Never compiled, never deleted:

`os/access.c` · `os/{auth,mitauth,xdmauth,xauembed}.c` · `os/xdmcp.c` · `os/xstrans.c` · `os/{WaitFor,connection,io}.c` · `dbe/` · `Xext/shm.c` ·
`Xext/xf86bigfont.c`

The last three require one guarded line each in `mi/miinitext.c`. The rest require stub symbols in `os/excluded_stubs_wmb.c`.

## 3. Phase Participation

| Phase | This module's involvement | Weight |
|---|---|---|
| P1 | None | — |
| P2 | None | — |
| P3 | None — but P3's `hal_yield_timeout` semantics are what `waitfor_wmb.c` is written against | — |
| **P4** | **Primary owner.** The whole‑stack assembly is this module's phase | **Heavy** |
| **P5** | **Server half of the transport** — `waitfor_wmb.c`, `connection_wmb.c`, `io_wmb.c` | **Heavy** |
| P6 | Seven of the thirteen guards, and the whole `tinyx/` patch budget except `kinput.c` | Medium |
| P7 | Support only: `XGrabServer` flow control lands in `io_wmb.c` / `connection_wmb.c` | Light |
| P8 | None directly; `dix` re‑entrancy is asserted by the E4 test, not changed | — |
| P9 | The freestanding compile of ~96 kLOC — the largest surface for libc‑floor surprises | Medium |

**This module is idle until P4 and then becomes the critical path for two consecutive phases.** Plan staffing accordingly: the C1 audit (§4.2) and the
reference `configure` run (§4.3) are the two items that can start early and remove the most uncertainty.

## 4. Phase P4 — Whole‑Stack Assembly

**Goal:** link and run the whole X server, and prove it renders. [`40-`](40-implementation-plan.md) §5.

### 4.1 Work items

| # | Item | Deliverable | Spec |
|--:|---|---|---|
| T4‑1 | `cmake/WmbModules.cmake` — the six groups of §2, globs only where a whole directory is included | New | [`30-`](30-host-build.md) §4.3 |
| T4‑2 | Reference `configure` run on a pristine checkout → `docs/reference/*.reference.h` | New | [`90-`](90-decisions.md) D‑37 |
| T4‑3 | `cmake/dix-config.h.in` + `cmake/kdrive-config.h.in`, curated against T4‑2 | New | [`30-`](30-host-build.md) §4.6, [`91-`](91-open-questions.md) Q2 |
| T4‑4 | **C1 audit** → `docs/c1-fd-bitset-audit.md` | New | §4.2, [`91-`](91-open-questions.md) Q14 |
| T4‑5 | Exclusion freeze — the ledger of §2.1 frozen and script‑checked | — | [`11-`](11-portability-plan.md) §4 |
| T4‑6 | `tinyx/os/excluded_stubs_wmb.c` (~180 LOC, three stub shapes only) | New | [`11-`](11-portability-plan.md) §4.2 |
| T4‑7 | `dix/main.c` guard: `main()` → `TinyX_ServerFiber(void *)`; drop `envp`; guard `getenv("XAUTHORITY")` and the XDMCP arguments | Guard #1 | [`11-`](11-portability-plan.md) §8.1 |
| T4‑8 | `mi/miinitext.c` guard: do not register `dbe`, `shm`, `xf86bigfont` | Guard #2 | [`90-`](90-decisions.md) D‑41 |

Guards #1 and #2 are *written* here and *manifested as patches* at P6 (§6). Writing them at P6 would mean P4 cannot link.

### 4.2 The C1 audit — do first, before anything else in this phase

[`91-`](91-open-questions.md) Q14 is formally due in P5, but the code it governs is *written* in P5. An audit that runs concurrently with the code it
is meant to inform is not an audit, so it runs here.

It is a day of grepping `dix/` and `tinyx/include/` for `fd_set`, `FD_SET`, `XFD_`, `ClientsWithInput`, `AllSockets` and `MaxClients`, and classifying
every hit as one of three things:

| Class | Meaning | Consequence |
|---|---|---|
| Transport‑only | The hit is in a file we do not compile | Safe; record and move on |
| Typedef‑only | `dix/` sees the type but never the semantics | Guardable in `osdep.h` |
| **Genuine `dix/` dependency** | `dix/` uses an fd bitset as a bitset | **`OsCommRec`'s `fd` is not reducible to a synthetic index — P5's stub plan is revised before P5 starts** |

The third class is the finding that would change the plan, and it is cheap to look for now and expensive to discover in the middle of `io_wmb.c`.

### 4.3 Config header curation — the item most likely to overrun

`dix-config.h` is curated **by hand against T4‑2's reference run** — never guessed, never copied in wholesale ([`90-`](90-decisions.md) D‑37). A
missing `RENDER` / `RANDR` / `XFIXES` produces "undefined reference to `PictureInit`" and an afternoon of bisection; a *wrong* macro produces a server
that links and then behaves subtly incorrectly.

Every difference between the curated template and the reference carries a one‑line justification in the template itself. `XKB` is a literal `0` in
every profile ([`90-`](90-decisions.md) D‑39) so that turning it on is a decision rather than a configure flag.

### 4.4 Definition of Done — TinyX slice

Profile `host-pure`. Items marked **(shared)** also need [`43-`](43-kdrive-module-plan.md) green.

- [ ] `wmb_host` links with **zero** unresolved symbols.
- [ ] No file from the exclusion ledger appears in any CMake source list (verified by script — gate S4).
- [ ] CMake module group membership matches [`01-`](01-component-inventory.md) §12 exactly (gate S5).
- [ ] The server fiber reaches `Dispatch()` and idles at **< 5 % CPU** — not spinning **(shared)**.
- [ ] The startup log reports screen geometry, depth, visual list and registered extensions, and they match a checked‑in reference.
- [ ] That extension list contains no `DOUBLE-BUFFER`, `MIT-SHM`, `XFree86-Bigfont` or `XKEYBOARD`.
- [ ] An SDL window opens showing the X root window **(shared — the pixels come from the DDX)**.
- [ ] `docs/c1-fd-bitset-audit.md` exists, every hit is classified, and no hit is a genuine `dix/` dependency — or P5's scope is revised **before** P5
  starts.
- [ ] The curated config templates differ from `docs/reference/*.reference.h` only in ways carrying a one‑line justification.

Gates: V‑P4‑1, V‑P4‑2, V‑P4‑5, V‑P4‑6, S4, S5 ([`41-`](41-verification-plan.md) §4.4).

### 4.5 Anticipated problems, in order of likelihood

1. **`dix-config.h` macro set is wrong.** §4.3.
2. **Extension registration order.** `miinitext.c` has ordering constraints — RENDER before DAMAGE, and so on.
3. **Missing stub symbols from excluded files.** `excluded_stubs_wmb.c` absorbs them; the list will grow during this phase and that is expected, not a
   defect.
4. **`main()` ownership.** `dix/main.c` and any example `main` collide ([`90-`](90-decisions.md) D‑07).
5. **`HAVE_CONFIG_H` enabling code paths that were previously dark.** The current build deliberately leaves it off; whole‑stack assembly cannot.

## 5. Phase P5 — The Server Half of the Transport

**Goal:** `dix/` receives requests and emits replies, events and errors with no socket underneath it. [`40-`](40-implementation-plan.md) §6,
[`21-`](21-inprocess-transport.md) §5.

**Precondition:** §4.2's audit is complete. `waitfor_wmb.c` and `io_wmb.c` must not be written against an assumption about where the fd bitsets reach.

### 5.1 Work items

| # | Item | Replaces | Est. LOC | Spec |
|--:|---|---|---:|---|
| T5‑1 | `tinyx/os/connection_wmb.c` — `CreateWellKnownSockets` no‑op, `wmb_register_internal_client()`, `CloseDownConnection`, `IgnoreClient`/`AttendClient` | `connection.c` (1,027) | ~120 | [`21-`](21-inprocess-transport.md) §5.2, §5.7 |
| T5‑2 | `tinyx/os/waitfor_wmb.c` — `WaitForSomething()` **plus the timer machinery** (`TimerSet`/`TimerForce`/`TimerFree`/`SetScreenSaverTimer` and the DPMS helpers) | `WaitFor.c` (670) | ~290 | [`21-`](21-inprocess-transport.md) §5.6, [`91-`](91-open-questions.md) Q22 |
| T5‑3 | `tinyx/os/io_wmb.c` ingress — `ReadRequestFromClient` reads `dpy->buffer` in place | `io.c` (1,026) | ~160 | [`21-`](21-inprocess-transport.md) §5.4 |
| T5‑4 | `io_wmb.c` egress — `WriteToClient` append semantics, `FlushClient` no‑op | ″ | ″ | [`21-`](21-inprocess-transport.md) §5.5 |
| T5‑5 | `OsCommRec` extension — the internal‑client fields, behind the `osdep.h` guard | — | — | [`21-`](21-inprocess-transport.md) §5.3 |
| T5‑6 | `InsertFakeRequest` / `ResetCurrentRequest` | — | — | [`21-`](21-inprocess-transport.md) §5.4 |
| T5‑7 | Grab flow control — a grabbed server does not service other clients | — | — | [`21-`](21-inprocess-transport.md) §5.7 |

The `osdep.h` edit (guard #6) is needed **here**, not at P6: `io_wmb.c` and `connection_wmb.c` do not compile without the internal‑client fields. P6
formalises it into the patch series; P5 writes it.

### 5.2 Order within the phase

Follow [`21-`](21-inprocess-transport.md) §10, whose first four steps are this module's:

| Step | Deliverable | Proven by |
|--:|---|---|
| 1 | `wmb_queue.c` — consumed here, built by [`44-`](44-framework-module-plan.md) | Register 4 clients, post, collect |
| 2 | T5‑1 + `excluded_stubs_wmb.c` | Whole‑stack link succeeds in `host-pure` |
| 3 | T5‑2 | Server idles; `pollEvents` observed at ≥ 30 Hz |
| **4** | **T5‑3** | **A hand‑built `X_NoOperation` reaches `ProcNoOperation`** |

**Step 4 is the milestone to reach first.** It needs no Xlib at all — a 4‑byte buffer and a `ClientRec` — and it is the cheapest possible end‑to‑end
proof that the substitution works. Everything after it is incremental.

### 5.3 The one detail that must not be lost

`WaitFor.c:206` calls `BlockHandler(&wt, &LastSelectMask)` immediately before `select()` and `WakeupHandler(i, &LastSelectMask)` immediately after.
KDrive registers `KdBlockHandler` there, and `KdBlockHandler` is what calls `KdOsFuncs.pollEvents`. `mieq` drains in `WakeupHandler`.

`waitfor_wmb.c` **must** preserve both calls, in the same order, with the same arguments minus the fd mask:

```c
int WaitForSomething(int *pClientsReady)
{
    unsigned long timeout_ms = 0;

    BlockHandler(&timeout_ms, NULL);          /* → KdBlockHandler → pollEvents */

    if (!wmb_client_queue_pending())
        hal_yield_timeout((uint32_t)timeout_ms);

    WakeupHandler(0, NULL);                   /* → mieqProcessInputEvents      */

    return wmb_collect_ready_clients(pClientsReady);
}
```

A stub that skips these two calls **compiles, links, runs and draws — and has no keyboard or mouse.** It is the highest‑likelihood, highest‑impact bug
in the project ([`21-`](21-inprocess-transport.md) R3, [`11-`](11-portability-plan.md) R1). Gate V‑P5‑3 is a one‑line instrumented counter and it
exists solely to catch it.

### 5.4 Definition of Done — TinyX slice

- [ ] `pollEvents` is called at **≥ 30 Hz** (instrumented counter) — the `BlockHandler` preservation check (V‑P5‑3).
- [ ] A hand‑built `X_NoOperation` reaches `ProcNoOperation` (step 4).
- [ ] A synthetic key event injected at the backend reaches a client's event queue (V‑P5‑4).
- [ ] `XGetWindowAttributes` returns correct data — the fixed reply path (V‑P5‑7).
- [ ] `XGetWindowProperty` on a 4 KB property round‑trips exactly — the **multi‑part** reply path (V‑P5‑8).
- [ ] A deliberate `BadWindow` reaches an installed `XSetErrorHandler` (V‑P5‑9); an async error surfaces at the next `XNextEvent` (V‑P5‑10).
- [ ] `InsertFakeRequest` / `ResetCurrentRequest` work: `sleepuntil`'s `AwaitSync` completes (V‑P5‑12).
- [ ] XID allocation past the initial range succeeds — XC‑MISC top‑up (V‑P5‑13).
- [ ] `strace -f -e trace=network,select,poll` produces no output; `lsof` shows no socket (V‑P5‑1, V‑P5‑2).
- [ ] The server idles at < 5 % CPU; Valgrind clean over 60 s.

**Debugging order for any P5 failure is fixed:** re‑run P4's root‑window gate (V‑P4‑3) first. If the root window still paints, `dix`/`mi`/`fb` are
intact and the fault is in the transport ([`40-`](40-implementation-plan.md) §5.3).

## 6. Phase P6 — Guard Application

**Goal:** the seven `tinyx/` guards this module owns, exported as a replayable patch series.

| # | File | Guard | Edit |
|--:|---|---|---|
| 1 | `dix/main.c` | `WMB_INPROC` | Written at P4 (§4.1 T4‑7) |
| 2 | `mi/miinitext.c` | `WMB_EXT_MINIMAL` | Written at P4 (T4‑8) |
| 3 | `os/utils.c` | `WMB_HAS_OS_TRANSPORT` | `<signal.h>`, `sigaction` installs, `<sys/wait.h>` child reaping, `setrlimit`, `Fopen()`, `<netdb.h>` hostname resolution — **the largest guard surface in the tree** |
| 4 | `os/osinit.c` | `WMB_HAS_OS_TRANSPORT` | `setrlimit(RLIMIT_NOFILE)`, the `fopen` stderr redirect |
| 5 | `os/log.c` | `WMB_FREESTANDING` | `<sys/stat.h>` and log rotation; `ErrorF`/`LogVWrite` kept |
| 6 | `os/osdep.h` | `WMB_INPROC` | Written at P5 (§5.1 T5‑5) |
| 7 | `os/strlcpy.c` | — | `<sys/types.h>` → `<stddef.h>`, one line (`strlcat.c` is #8b and does not count separately) |

Guard #8, `kdrive/src/kinput.c`, belongs to [`43-`](43-kdrive-module-plan.md) and draws on the same `tinyx/` cap.

### 6.1 Definition of Done — TinyX slice

- [ ] Patches `0001`–`0007` exist under `patches/tinyx/` with a `MANIFEST.md` row each (file, guard, rationale, LOC delta).
- [ ] Every patch applies cleanly to a pristine upstream checkout (`git apply --check`) — gate S7.
- [ ] Only the five sanctioned guard macros appear in any `tinyx/` file — gate S3.
- [ ] `tinyx/` patch count ≤ 8 including `kinput.c` — gate S6.
- [ ] Every P5 gate still passes after the guards are applied — V‑P6‑1.
- [ ] `grep -rl 'include *<sys/\|include *<linux/\|include *<netinet/\|include *<pthread\.h>\|include *<dlfcn\.h>'` over the compiled `tinyx/` set is
  empty — gate S2.
- [ ] `grep -rl "SDL" tinyx/` is empty — gate V6/S1.

**P6 is deliberately after P5.** A guard applied to code that has never been compiled is a guess; once `host-pure` runs, each guard is applied and
immediately re‑verified against a working system.

## 7. Phase P9 — Freestanding Compile

This module contributes ~96 kLOC to the `-ffreestanding -fno-builtin -fno-pic` compile, which makes it the largest surface for
[`11-`](11-portability-plan.md) R6: a libc dependency nobody censused, e.g. `qsort` in `dix/resource.c`. **This is expected, is rated low‑impact, and
is why Q6/P9a is its own phase.**

- [ ] Every `tinyx/` translation unit compiles freestanding.
- [ ] Nothing this module needs falls outside `docs/floor-symbols.txt`, or the floor is amended with a one‑line justification per addition.

**P9a is worth starting as soon as P6 is green**, out of order, against a stub backend. It costs little and surfaces the floor surprises while there
is still time to absorb them.

## 8. Interfaces With Other Modules

The seams, stated as contracts so that a change on either side is a conversation rather than a surprise.

| Seam | This module | Counterpart | Owner of the definition |
|---|---|---|---|
| Cooperative wait | `waitfor_wmb.c` calls `hal_yield_timeout(ms)` | `framework/fiber/` | [`44-`](44-framework-module-plan.md), [`12-`](12-platform-contract.md) §6 |
| Request queue | `io_wmb.c` / `waitfor_wmb.c` call `wmb_client_queue_pending()`, `wmb_collect_ready_clients()` | `wmb_queue.c` | [`44-`](44-framework-module-plan.md), [`21-`](21-inprocess-transport.md) §5.1 |
| Client registration | `connection_wmb.c` exports `wmb_register_internal_client()` | `wmb_open.c` calls it | **This module** |
| Reply / event egress | `WriteToClient` deposits into the client's slot | `_XReply`, the event queue | [`21-`](21-inprocess-transport.md) §5.5, §6 |
| Block / wakeup handlers | `waitfor_wmb.c` calls them | `KdBlockHandler` → `pollEvents` | [`43-`](43-kdrive-module-plan.md) |
| Screen and input init | `dix/main.c` → `InitOutput`, `InitInput` | `tima_kdrive_system.c` | [`43-`](43-kdrive-module-plan.md) |
| Server fiber entry | Exports `TinyX_ServerFiber(void *)` | `wmb_main()` creates the fiber | [`44-`](44-framework-module-plan.md) |
| Build config | Publishes `dix-config.h`, `kdrive-config.h` | Every `tinyx/` TU | **This module** |

## 9. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| TX‑1 | `waitfor_wmb.c` omits `BlockHandler`/`WakeupHandler` → no input, no obvious symptom | **High** | **High** | §5.3; gate V‑P5‑3 is a counter, not a judgement call |
| TX‑2 | `dix-config.h` macro set wrong | **High** | Medium | §4.3 — curate against a reference run; every diff justified |
| TX‑3 | `WriteToClient` misclassifies a multi‑part reply | **High** | **High** | [`21-`](21-inprocess-transport.md) §5.5 append semantics; gate V‑P5‑8 and the differential byte test |
| TX‑4 | `osdep.h` fd bitsets leak into `dix/` and cannot be guarded cleanly | Medium | High | §4.2's audit resolves it **before** P5; fallback is a dummy `fd` field |
| TX‑5 | An EXCLUDE file turns out to be referenced at link time | Medium | Medium | §2.1 pre‑enumerates the symbol obligations; `excluded_stubs_wmb.c` absorbs surprises |
| TX‑6 | Extension registration order wrong in `miinitext.c` | Medium | Medium | V‑P4‑6 compares the logged extension list against a checked‑in reference |
| TX‑7 | Freestanding compile surfaces an uncensused libc call | **High** | Low | Expected; P9a run early against a stub backend |
| TX‑8 | A stub that *lies* — reports success for something that did not happen | Low | High | [`11-`](11-portability-plan.md) §4.2: three stub shapes only, each with a one‑line comment naming its shape |

## 10. Budget

| | Value |
|---|---|
| New files | `excluded_stubs_wmb.c`, `waitfor_wmb.c`, `connection_wmb.c`, `io_wmb.c` + two config templates |
| New LOC | ~750 |
| Upstream files guarded | **7 of the `tinyx/` cap of 8** (the eighth is `kinput.c`, owned by [`43-`](43-kdrive-module-plan.md)) |
| Slack | **0.** An eighth guard in this module requires a [`90-decisions.md`](90-decisions.md) entry with the rejected alternative |

## 11. What to Do First

1. **Run the reference `configure`** on a pristine TinyX checkout and check in `docs/reference/*.reference.h` (T4‑2). It unblocks the config templates
   and it can be done today, in parallel with P1–P3.
2. **Do the C1 audit** (§4.2). It is a day of grepping, it is the only thing that can invalidate P5's plan, and its answer is worth having early.
3. **Freeze the exclusion ledger** and write the script that checks it (T4‑5, gate S4) — before the source lists exist, so the check is never
   retrofitted.

Only then start T4‑1. Items 1–3 are the phase's uncertainty; the rest is work.
