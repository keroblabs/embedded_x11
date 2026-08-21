# 91 — Open Questions

## 1. Purpose

Everything that is not decided, with the phase by which it must close and what closing it requires. It is a deliverable of workstream 6
([`00-introduction.md`](00-introduction.md) §5.6).

A question here is a **blocker with a deadline**, not a note. When one closes, it moves to [`90-decisions.md`](90-decisions.md) with a decision number, and
every document that cites it is updated.

### 1.1 Status vocabulary

| Status | Meaning |
|---|---|
| **OPEN** | Undecided; the answer is not yet known |
| **INVESTIGATING** | Work is in progress to determine the answer |
| **BLOCKED** | Cannot be answered until an external input arrives |
| **CLOSED** | Answered; see the referenced decision |

### 1.2 Priority

| Priority | Meaning |
|---|---|
| **P0** | Blocks the phase it is due in. Nothing proceeds |
| **P1** | Must close in its phase but work can proceed around it |
| **P2** | Should close eventually; a reasonable default is in place |

## 2. Question Register

| # | Question | Priority | Due | Status |
|---|---|---|---|---|
| Q1 | Which allocator do TinyX and JWM use? | P1 | P4 | **CLOSED** · D‑29 |
| Q2 | What is the exact `dix-config.h` / `kdrive-config.h` macro set? | **P0** | P4 | INVESTIGATING · method settled (D‑37) |
| Q3 | Does the target need a filesystem at all? | P2 | P9 | **CLOSED** · D‑34 |
| Q4 | What is the font strategy? | P1 | P7 | **CLOSED** · D‑38 |
| Q5 | Does anything need wall‑clock time? | P2 | P7 | **CLOSED** · D‑30 |
| Q6 | What is the target, and therefore the real memory numbers? | **P0** | P9 | BLOCKED |
| Q7 | May `XNextEvent` block for an external caller? | P1 | P8 | **CLOSED** · D‑27 |
| Q8 | Vtable as a typed struct or a flat slot array? | P2 | P8 | **CLOSED** · D‑28 |
| Q9 | Do applications need `XImage` on target? | P2 | P8 | OPEN |
| Q10 | Is XKB compiled in? | P1 | P4 | **CLOSED** · D‑39 |
| Q11 | Fiber core or the platform's own scheduler on target? | P1 | P9 | **CLOSED** · D‑31 |
| Q12 | Shadow framebuffer or direct scanout on target? | P1 | P9 | **CLOSED** · D‑35 |
| Q13 | Which libc on target? | P1 | P9 | **CLOSED** · D‑36 |
| Q14 | Do the `osdep.h` fd bitsets leak into `dix/`? | **P0** | P5 | INVESTIGATING · scheduled ([`40-`](40-implementation-plan.md) §5.1) |
| Q15 | Is `kdrive/src/kmap.c` referenced by any retained file? | P1 | P4 | **CLOSED** · D‑40 |
| Q16 | Does anything require DBE? | P2 | P4 | **CLOSED** · D‑41 |
| Q17 | Does `FatalError` need `setjmp`/`longjmp` on target? | P2 | P9 | **CLOSED** · D‑32 |
| Q18 | What is the complete libc floor? | P1 | P9 | OPEN |
| Q19 | How does JWM's `XOpenDisplay` know the server is ready? | P1 | P7 | **CLOSED** · D‑33 |
| Q20 | Which `Display`‑dereferencing macros must the wrapper redefine? | P1 | P8 | OPEN · scheduled ([`40-`](40-implementation-plan.md) §9.1) |
| Q21 | Who schedules WMB once `wmb_main()` returns? | P1 | P9 | **CLOSED** · D‑44 |
| Q22 | Who owns the timer list when `WaitFor.c` is not compiled? | P1 | P5 | **CLOSED** · D‑49 |

Fourteen of the original twenty closed in the first review round; Q21 was opened by D‑42 and closed by D‑44, and Q22 was opened and closed by the D‑49
review. The six below are what remains: one is a mechanical exercise (Q2), two are audits that must run before the code they govern is written (Q14, Q20),
one is a measurement only the freestanding link can produce (Q18), one needs an application requirement we do not have yet (Q9), and one needs hardware (Q6).

## 3. Details — Still Open

Closed questions are listed in §6; their reasoning lives in [`90-decisions.md`](90-decisions.md).

### Q2 — The `dix-config.h` / `kdrive-config.h` macro set · **P0** · due P4 · INVESTIGATING

TinyX is autotools; `tinyx/configure.ac` computes a large macro set. The current framework build sidesteps this by leaving `HAVE_CONFIG_H` undefined, which
cannot survive whole‑stack assembly.

**This is the highest‑likelihood cause of P4 overrun.** A missing `RENDER` produces `undefined reference to PictureInit`; a missing `SMART_SCHEDULE`
produces subtler misbehaviour.

**The method is settled** ([`90-`](90-decisions.md) D‑37): run `autogen.sh && configure` once on a pristine TinyX checkout in a scratch tree, keep the
generated headers as a checked‑in *reference artefact*, and hand‑curate `framework/dix-config.h.in` against it. The reference is ground truth for "what
does upstream expect to exist"; the curated template is the build input. Copying the generated header into the build is still forbidden — it encodes the
generating machine.

**Closing requires**, in order:

| # | Step | Output |
|---|---|---|
| 1 | Reference `configure` run on vanilla TinyX | `docs/reference/dix-config.reference.h`, `kdrive-config.reference.h` |
| 2 | Classify every macro: host‑detection (`HAVE_*`), feature switch, or sizing constant | An annotated list |
| 3 | Drop host‑detection macros that name facilities we do not have (`HAVE_SYS_SOCKET_H`, `HAVE_POLL`, …) | — |
| 4 | Force the feature switches to the decisions already taken: RENDER/RANDR/XFIXES/DAMAGE on (D‑26), XKB off (D‑39), DBE off (D‑41), SHM/BIGFONT off (D‑25) | — |
| 5 | Diff the curated `host` template against the reference and justify every difference in one line | The review artefact |
| 6 | Derive the `target` template as a **delta** from the `host` template, not a second generation | A short, reviewable list |

Step 6 is the one that matters for the long run: if the two templates are generated independently they will drift, and a divergence between profiles is
exactly the class of bug that is hardest to find. A day's work, and it must happen **before** the first whole‑stack link attempt.

### Q6 — The target platform · **P0** · due P9 · BLOCKED

Everything in [`31-target-build.md`](31-target-build.md) that is a number is currently a placeholder: `WMB_LOAD_BASE` (`0x80100000`),
`WMB_FRAMEBUFFER_BASE`, heap size (4 MB), screen geometry (800×480×16), architecture and toolchain.

**Blocked on:** an external decision about target hardware.

**Mitigation:** P9 splits into P9a (freestanding compile against a stub backend — needs only *an* architecture, not *the* architecture) and P9b (hardware
bring‑up). P9a is worth running as soon as P6 is green ([`40-`](40-implementation-plan.md) §10.3), which de‑risks most of the target work without knowing
the target.

**Current stance:** the host build is the focus, and nothing waits on this question until P9. Every target‑side decision that *could* be taken without
knowing the hardware has been (D‑31, D‑35, D‑36, D‑44), which reduces Q6 to the numbers alone — no design question is blocked behind it.

### Q9 — `XImage` on target · P2 · due P8

`XCreateImage`/`XPutImage`/`XDestroyImage` are in the vtable draft. Keeping them is desirable — raster upload is how an application draws anything it
rendered itself — but it is the most awkward marshalling case in the whole ABI, because an `XImage` carries both a `data` pointer and a table of function
pointers (`f.get_pixel`, `f.put_pixel`, …) that are meaningless across an address‑space or link boundary.

**Provisional stance:** keep `XCreateImage`, `XPutImage` and `XDestroyImage`; leave `XGetImage` out until something asks for it.

**Closing requires** answering three concrete questions about the applications:

| # | Question | If yes |
|---|---|---|
| 1 | Does an external application produce pixels itself (decoded image, camera frame, custom widget) and push them to a window? | `XPutImage` stays; the wrapper must construct the `XImage` on the WMB side so the function‑pointer table is never marshalled |
| 2 | Does anything need to *read* pixels back (screenshot, drag feedback)? | `XGetImage` is added, with a defined ownership rule for the returned buffer |
| 3 | Is the caller in the same address space (bridge) or a separate one? | Separate makes `data` a copy, not a pointer, and fixes the format conversion cost |

Informally blocked on the same input as Q6.

### Q14 — Do `osdep.h`'s fd bitsets leak into `dix/`? · **P0** · due P5 · INVESTIGATING

`tinyx/os/osdep.h` includes `<X11/Xpoll.h>` and defines fd‑set‑based structures. If those types appear in `dixstruct.h` or in `dix/` sources, guarding them
requires more edits than the plan budgets, and `OsCommRec`'s `fd` field may not be reducible to a synthetic index.

**Closing requires:** grepping `dix/` and `tinyx/include/` for `fd_set`, `FD_SET`, `XFD_`, `ClientsWithInput`, `AllSockets`, `MaxClients`, and tracing each
hit. A day's work, and it must happen **before** `waitfor_wmb.c` and `io_wmb.c` are written, not after.

**Scheduled.** It is now an explicit P4 deliverable — the *audit* runs in P4 so that P5 starts with the answer in hand, even though the question is not due
until P5. See [`40-`](40-implementation-plan.md) §5.1 ("C1 audit") and §5.2. The audit produces `docs/c1-fd-bitset-audit.md`: every hit, its file, and its
classification as transport‑only (safe), typedef‑only (guardable), or a genuine `dix/` dependency (which forces a rethink of the `OsCommRec.fd` reduction).

Recorded as census item C1 in [`10-`](10-os-dependency-census.md) §11.

### Q18 — The complete libc floor · P1 · due P9

[`11-`](11-portability-plan.md) §9.1 and [`31-`](31-target-build.md) §3.1 state a hypothesis. The real list is whatever the freestanding link demands, and
it cannot be known before the link is attempted — which is the reason this question cannot be closed early, only prepared for.

**Closing requires:** running P9a and generating `docs/floor-symbols.txt` from the actual undefined‑symbol set:

```
nm --undefined-only libwmb_runtime.a libwmb_hal.a libtinyx.a libx11_wmb.a libjwm.a \
  | sort -u | comm -23 - contract-symbols.txt > docs/floor-symbols.txt
```

**What §3.1 is most likely to be missing**, and therefore what to expect:

| Category | Why it is easy to forget |
|---|---|
| Compiler runtime (`libgcc` / `compiler-rt`) | `__aeabi_*`, `__udivdi3`, `__moddi3` and the soft‑float helpers are emitted by the compiler, not written by upstream, so they appear in no source grep |
| `snprintf` conversions actually used | newlib‑nano's default `printf` omits `%f`/`%ll`; RENDER and the log paths may want them, and the fix is a link flag, not a source change |
| `qsort` / `bsearch` comparators | Present in §3.1, but their presence pulls in more of `stdlib` than expected on some libcs |
| `errno` and its TLS accessor | newlib routes `errno` through `__errno()`, which needs a reentrancy struct the freestanding build must provide |
| `assert` / `__assert_func` | Reachable from upstream `assert()` calls that are not compiled out unless `NDEBUG` is set |

Expected to surface surprises ([`11-`](11-portability-plan.md) R6) — which is exactly why P9a is worth running early, out of order, as soon as P6 is green.
Census item C6.

### Q20 — `Display`‑dereferencing macros in `Xlib.h` · P1 · due P8

`DefaultScreen`, `RootWindow`, `BlackPixel`, `WhitePixel`, `DisplayWidth`, `DisplayHeight`, `DefaultDepth`, `DefaultVisual`, `DefaultColormap`,
`DefaultGC`, `ScreenCount`, `DisplayString`, `ProtocolVersion`, `VendorRelease`, `QLength`, `NextRequest`, `LastKnownRequestProcessed`, `ConnectionNumber`,
`AllPlanes`, … and their `*OfScreen` variants.

Each must become a vtable entry plus a `#undef`/`#define` in the wrapper's compatibility header, or an external application segfaults at first use.

**Closing requires:** an exhaustive enumeration from `X11/Xlib.h`, not a guess. Mechanical.

**Scheduled.** It is now an explicit P8 deliverable with its own gate ([`40-`](40-implementation-plan.md) §9.1, §9.2; [`22-`](22-entry-point-abi.md) §5.2,
A5). The enumeration is generated, not typed: every object‑like and function‑like macro in `X11/Xlib.h` whose expansion mentions the macro's `Display`
argument is extracted mechanically, and the generator's output is checked in so that a future upstream header change shows up as a diff. The gate is that
compiling a test translation unit which *uses* every enumerated macro against the wrapper's compatibility header produces no reference to a `Display`
field — because in the wrapper, `Display` is opaque.

### Q22 — Timer ownership when `WaitFor.c` is excluded · **CLOSED** · D‑49

`WaitFor.c` is two files in one: the `select()` wait loop **and** the server's timer machinery — `TimerSet`, `TimerForce`, `TimerCancel`, `TimerFree`,
`TimerInit`, `DoTimer`, `CheckAllTimers`, plus `SetScreenSaverTimer` / `FreeScreenSaverTimer` and the DPMS helpers. `dix/dispatch.c`'s `ProcSetScreenSaver`
and several `Xext/` files call them, so excluding the file leaves them undefined.

**Closed by D‑49**, which removed the last profile that compiled `WaitFor.c`. With no second copy in the tree there is no divergence risk, so the timer
machinery moves verbatim into `waitfor_wmb.c`, raising its estimate from ~90 to ~290 LOC. No upstream file is edited and the patch cap is untouched.

## 4. Questions by Phase

| Phase | Must close |
|---|---|
| **P4** | Q2 (**P0**) |
| **P5** | Q14 (**P0**) — audited in P4 |
| **P8** | Q9, Q20 |
| **P9** | Q6 (**P0**), Q18 |

P7 no longer has an open question against it: Q4, Q5 and Q19 all closed. P9 is down to the target itself and the libc floor it produces.

## 5. The Three That Matter Most

If only three questions are worked before implementation starts, these:

1. **Q2 — the config macro set.** It is P0 for P4, it is a day's mechanical work, and getting it wrong turns the longest phase in the project into a
   whack‑a‑mole against link errors. The *method* is now settled (D‑37); only the execution remains.

2. **Q14 — the fd bitset leak.** It is P0 for P5, it is a day's grep, and the answer determines whether the transport stub plan is sound or needs
   rethinking. It must be answered *before* the stubs are written, which is why the audit is scheduled in P4.

3. **Q6 — the target.** It is the only remaining external dependency, and it now blocks the *numbers* in [`31-target-build.md`](31-target-build.md) rather
   than the design. It cannot be answered internally, which is why the P9a/P9b split exists — but the sooner it is asked externally, the better. No other
   question is blocked behind it.

## 6. Closed Questions

One‑line answers; the reasoning and the rejected alternatives are in [`90-decisions.md`](90-decisions.md).

| # | Question | Answer | Decision |
|---|---|---|---|
| Q1 | Which allocator? | Option B — `heap.c` in every profile. `malloc`/`free`/`realloc`/`calloc` redirect to it on host and target alike | [D‑29](90-decisions.md) |
| Q3 | Filesystem on target? | Yes, potentially — it becomes a supported, capability‑gated subsystem rather than an excluded one. `WMB_HAS_FILESYSTEM` still defaults off | [D‑34](90-decisions.md) |
| Q4 | Font strategy? | Option B now — several built‑in bitmap fonts. Option C (FreeType) is a named later upgrade, and the font path is built so it is an addition, not a rewrite | [D‑38](90-decisions.md) |
| Q5 | Wall clock? | Configurable. `hal_wallclock_s()` is specified and compiled out by default; the target is expected to have an RTC | [D‑30](90-decisions.md) |
| Q7 | May `XNextEvent` block? | Option B — it loops on `wmbYield` internally, so it is Xlib‑compatible | [D‑27](90-decisions.md) |
| Q8 | Vtable shape? | A struct of typed function pointers | [D‑28](90-decisions.md) |
| Q10 | XKB? | Off | [D‑39](90-decisions.md) |
| Q11 | Fiber core or platform scheduler? | The fiber core, and only the fiber core — there is no RTOS on target. Platform code is isolated in `arch/`; the hosted port comes first | [D‑31](90-decisions.md) |
| Q12 | Shadow or direct scanout? | Double buffered, with a hardware page flip where the target provides one. The host is unaffected | [D‑35](90-decisions.md) |
| Q13 | Which libc? | newlib‑nano for the first target build; hand‑written as a later size optimisation | [D‑36](90-decisions.md) |
| Q15 | Is `kmap.c` referenced? | **No.** Its only callers are `kdrive/vesa/vbe.c`, which is excluded by D‑16. No stub is needed | [D‑40](90-decisions.md) |
| Q16 | Does anything require DBE? | **No.** JWM has zero DBE references; the only mention in retained code is `mi/miinitext.c`, already covered by the planned guard | [D‑41](90-decisions.md) |
| Q17 | `setjmp`/`longjmp` for `FatalError`? | Stays in the libc floor. Hosted and stubbed builds use it; a real target substitutes its own fault infrastructure | [D‑32](90-decisions.md) |
| Q19 | Server readiness? | Create every fiber first, start the scheduler only once the server reports ready | [D‑33](90-decisions.md) |
| Q21 | Who schedules WMB? | Whoever called `wmb_main()`. WMB never owns the CPU and never needs to know who does. It may be preempted **as a component**; it may not be preempted **within**. `dix` stays serialised by the server fiber (D‑04); only the transport's post‑and‑wake path needs a guard | [D‑44](90-decisions.md) |
