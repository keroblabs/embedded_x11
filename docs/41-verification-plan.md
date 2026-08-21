# 41 — Verification Plan

## 1. Purpose

This document defines **what "green" means** at every gate in [`40-implementation-plan.md`](40-implementation-plan.md). It is the second deliverable of
workstream 5 ([`00-introduction.md`](00-introduction.md) §5.5).

Gate **definitions** live here and nowhere else. Gate **ownership** — which team runs which gate, and which module a failure is attributable to — is in the
four module plans, [`42-`](42-tinyx-module-plan.md) through [`45-`](45-jwm-module-plan.md). A gate listed in a module plan that does not appear below is a
defect in the module plan.

It exists because the project's central claims — "no OS dependency", "no network transport", "portable" — are all *negative* claims, and negative claims
are only credible when they are mechanically checked. A claim that rests on someone having read the code carefully is not a claim.

### 1.1 Principles

| # | Principle |
|---|---|
| **1** | **Every gate is one command.** If checking it requires judgement, it is not a gate |
| **2** | **Negative claims get symbol gates.** "No sockets" means `nm` says so, not that we believe so |
| **3** | **The gated profile is `host-pure`.** It is the same source configuration as `target` |
| **4** | **Visible output beats a return code.** For anything that draws, the gate is a human seeing the right pixels — automated where possible, but never *only* a exit status |
| **5** | **Measurements replace estimates.** Every number in [`31-target-build.md`](31-target-build.md) that is currently an estimate becomes a P8 measurement |

## 2. The Test Pyramid

```
                 ┌────────────────────────────────────┐
   Level 4       │  Stability: 1-hour runs, leak tests │   P8, P9
                 ├────────────────────────────────────┤
   Level 3       │  System: JWM + 2 clients, grabs     │   P7
                 ├────────────────────────────────────┤
   Level 2       │  Integration: wmb_test_client       │   P5
                 │  in-process, zero sockets           │
                 ├────────────────────────────────────┤
   Level 1       │  Assembly: server initialises,      │   P4
                 │  dispatches, paints a root window   │
                 ├────────────────────────────────────┤
   Level 0       │  Unit: HAL, fibers, hal_task, queue │   P1–P3
                 └────────────────────────────────────┘
```

Level 1 is deliberately *below* Level 2 in credibility. It proves the server is assembled correctly; it proves nothing about the architecture, because no
client has yet reached it. An earlier plan put stock X clients here as an independent oracle; that profile was retired ([`90-`](90-decisions.md) D‑49).

## 3. Static Gates (Symbol and Source)

These run on every build and take seconds. They are the backbone of the verification story.

### 3.1 Source gates

| ID | Gate | Command | From |
|---|---|---|---|
| S1 | No backend leakage above the contract | `grep -rl 'SDL\|sdl_\|hal_drivers\.h' tinyx/ libX11/src/ jwm/src/ transport/` → empty | [`03-`](03-target-architecture.md) L1 |
| S2 | No POSIX headers in the compiled set | `grep -rl 'include *<sys/\|include *<linux/\|include *<netinet/\|include *<pthread\.h>\|include *<dlfcn\.h>\|include *<netdb\.h>' $(cmake_source_list)` → empty | [`10-`](10-os-dependency-census.md) |
| S3 | Only sanctioned guard macros in upstream files | `grep -rhoE '#if(n?def)? +WMB_[A-Z_]+' tinyx libX11 jwm X11/Xlibint.h \| sort -u` → exactly the five | [`11-`](11-portability-plan.md) §8.6 |
| S4 | No excluded file in any source list | script diffs CMake lists against the ledger | [`01-`](01-component-inventory.md) §13 |
| S5 | CMake module groups match the inventory | script diffs `WmbModules.cmake` against [`01-`](01-component-inventory.md) §12 | [`01-`](01-component-inventory.md) V5 |
| S6 | Patch budget respected | `ls patches/tinyx/*.patch \| wc -l` ≤ 8, `libX11` ≤ 3, `jwm` ≤ 4 | [`11-`](11-portability-plan.md) §8.4 |
| S7 | Patches replay on pristine upstream | `git -C /tmp/pristine/$t apply --check patches/$t/*.patch` | [`11-`](11-portability-plan.md) §8.6 |
| S8 | Contract headers are freestanding | compile each alone with `-ffreestanding -nostdinc` | [`12-`](12-platform-contract.md) PC‑V1 |
| S9 | Vtable artefacts are generated, not hand‑edited | regenerate from `api.def`, `git diff` → empty | [`22-`](22-entry-point-abi.md) E‑V1 |
| S10 | **The windowing set is detachable** | link `libtinyx.a libx11_wmb.a libjwm.a libwmb_api.a` against contract stubs + runtime substrate, with no backend and no `wmb_boot.o` → resolves | [`03-`](03-target-architecture.md) L7, [`90-`](90-decisions.md) D‑42 |
| S11 | **The framework is shareable** | `grep -rn '#include' framework/ \| grep -E 'tinyx\|libX11\|jwm\|transport\|Xlib\|dix'` → empty | [`03-`](03-target-architecture.md) L8 |
| S12 | **`framework/` is platform‑free** | `grep -rl 'SDL\|<linux/\|clock_gettime\|ucontext\|termios' framework/ --include=*.c --include=*.h` → empty | [`03-`](03-target-architecture.md) L9 |

### 3.2 Symbol gates

Run against the linked image. **These are the gates that make the project's claims true.**

| ID | Gate | Command |
|---|---|---|
| N1 | No transport symbols | `nm --undefined-only wmb \| grep -E '\b(socket\|bind\|listen\|accept\|connect\|select\|poll\|readv\|writev\|fcntl\|getpeername)\b'` → empty |
| N2 | No process symbols | `... \| grep -E '\b(fork\|execve\|execl\|waitpid\|sigaction\|signal\|kill\|setrlimit)\b'` → empty |
| N3 | No environment/filesystem symbols (target only) | `... \| grep -E '\b(getenv\|fopen\|opendir\|readdir\|stat\|access)\b'` → empty |
| N4 | No dynamic loading | `... \| grep -E '\b(dlopen\|dlsym\|dlclose)\b'` → empty |
| N5 | No threads outside the tick shim | `nm framework/**/*.o \| grep pthread_` appears only in `hosted_timer.o`. The P8 re‑entrancy harness uses threads deliberately; it is a test binary and is not linked into `wmb_host` |
| N6 | No XCB | `... \| grep xcb` → empty |
| N7 | Exactly one `main` | `nm --defined-only wmb \| grep ' T main$' \| wc -l` = 1 |
| N8 | Link floor respected (target) | `comm -23 <(nm --undefined-only wmb.elf \| sort -u) docs/floor-symbols.txt` → empty |
| N9 | No dynamic relocations (target) | `readelf -r wmb.elf` → none |
| N10 | `.wmb_api` placement (target) | `readelf -S wmb.elf` shows `.wmb_api` at `WMB_LOAD_BASE` |

N3 is target‑only because the hosted build legitimately uses `fopen` in the SDL backend and in debug tooling.

## 4. Runtime Gates by Phase

### 4.1 P1 — Contract and backends

| ID | Gate | Method |
|---|---|---|
| V‑P1‑1 | All four profiles configure | `for p in host-pure host-bridge host-unit target; do cmake -B /tmp/b-$p -DWMB_PROFILE=$p; done` |
| V‑P1‑2 | Configures with system SDL2 and no `WMB_SDL_ROOT` | Clean container with `libsdl2-dev` |
| V‑P1‑3 | Gradient visible through the contract | `wmb_hal_smoke`; screenshot compared against a reference PNG (tolerance for SDL scaling) |
| V‑P1‑4 | DDX links against `hal_mock.o` with no SDL | `wmb_ddx_linktest` target |
| V‑P1‑5 | Key bytes correct | Scripted `xdotool`‑style injection → expected `HAL_CMD_KEY_DOWN`/`KEY_UP` pairs on stdout |
| V‑P1‑6 | Mouse wheel produces buttons 4/5/6/7 | Same, checking the DDX's `KdEnqueueMouseEvent` calls |
| V‑P1‑7 | Mouse button release tracked | Press+release produces two events, not one |
| S1, S8 | | |

### 4.2 P2 — Fiber runtime

| ID | Gate | Method |
|---|---|---|
| V‑P2‑1 | 100,000 ping‑pong switches < 100 ms | `wmb_fiber_smoke --pingpong` |
| V‑P2‑2 | Busy fiber preempted within 2 ticks | `wmb_fiber_smoke --preempt` — **DEFERRED** by [`90-`](90-decisions.md) D‑47; blocked on V‑P2‑8..10 |
| V‑P2‑3 | Mutex/cond work between fibers | `wmb_fiber_smoke --sync` |
| V‑P2‑4 | Stack overflow → SIGSEGV on the guard page | `wmb_fiber_smoke --overflow`; expected signal and fault address in the guard range |
| V‑P2‑5 | Exactly 2 OS threads | `ps -o nlwp= -p $(pidof wmb_fiber_smoke)` = 2 — **1 while D‑47 holds**, since the tick thread is the second one |
| V‑P2‑6 | Valgrind clean | `valgrind --suppressions=tools/libucontext.supp` |
| V‑P2‑7 | ASan clean, no false positives on switches | `-fsanitize=address` build |
| V‑P2‑8 | **R1** — the preemption tick is suppressed inside a critical section | Drive `fiber_tick_isr()` 100 times from inside `fiber_critical_enter`/`exit`; assert no switch occurred, then assert one occurs after the exit. Also assert `buffer.c.o` references `fiber_critical_enter` (i.e. `USE_FIBER` reached it) |
| V‑P2‑9 | **R2** — overflow drops a whole pair | Overfill a ring by 1, 2 and 3 bytes; assert the decoder still sees command/data alignment (defect D7) |
| V‑P2‑10 | **R4** — preemption soak | Tick armed at 1 ms, continuous input for one hour, decoder asserts every command byte is followed by its data byte |
| N5 | | |

V‑P2‑8 through V‑P2‑10 are the re‑enable preconditions for the preemption tick ([`90-`](90-decisions.md) D‑47 §D‑47.3). Until all three are green, V‑P2‑2
is not attempted — it is the gate that *arms* the hazard the other three exist to remove.

### 4.3 P3 — `hal_task_*`

| ID | Gate | Method |
|---|---|---|
| V‑P3‑1 | `hal_yield_timeout(50)` → 0 at 50 ± 5 ms | 100 iterations, report min/max/mean |
| V‑P3‑2 | `hal_yield_timeout(200)` → 1 when woken | Woken at t=20 ms; assert return 1 and elapsed ≈ 20 ms |
| V‑P3‑3 | Wake before wait is not lost | `hal_task_wake(t)` then `t` calls `hal_task_wait()`; must return immediately |
| V‑P3‑4 | `hal_tick_ms` monotonic over 60 s | Sample at 1 kHz; assert no backward step |
| V‑P3‑5 | 3‑fiber producer/consumer, 10,000 iterations, no lost wakeups | `wmb_task_smoke` |
| V‑P3‑6 | **W5** — the release wake is not a signal | A task created blocked, then woken once, whose entry is `hal_yield_timeout(P)`: its first toggle lands at t ≈ P, not t ≈ 0 |
| V‑P3‑7 | **W2** — wakes coalesce | Two `hal_task_wake` then two `hal_task_wait`: the first returns immediately, the second blocks |
| V‑P3‑8 | **W3** — one flag serves both waits | `hal_task_wake` then `hal_yield_timeout(1000)` returns 1 promptly; and `hal_task_wake` then `hal_task_wait` returns immediately |
| V‑P3‑9 | **W7** — a recycled slot starts clean | Wake a destroyed task, create a new one, assert its first wait blocks |
| V‑P3‑10 | Creation state is `CREATED` | A created-but-not-woken task does not execute across 20 scheduler rounds |

V‑P3‑6 through V‑P3‑9 gate the wake rules in [`12-`](12-platform-contract.md) §6.2.2. They exist because every one of them is invisible to a test that
only checks "the task ran": each produces a timing bug rather than a functional one, and W5 in particular corrupts exactly the bring‑up order
[`90-`](90-decisions.md) D‑43 prescribes.

### 4.4 P4 — TinyX assembly

| ID | Gate | Method |
|---|---|---|
| V‑P4‑1 | Link with zero unresolved symbols | Build log |
| V‑P4‑2 | Server idles < 5 % CPU | `top -b -n 10` over 10 s idle |
| V‑P4‑3 | The SDL window shows the X root window | Screenshot |
| V‑P4‑4 | `KdOsFuncs.pollEvents` is reached at ≥ 30 Hz | Instrumented counter |
| V‑P4‑5 | Startup log reports expected geometry, depth, visual list | Compare against a checked‑in reference |
| V‑P4‑6 | Startup log's extension list matches, and excludes the four of D‑25/D‑39/D‑41 | Compare against a checked‑in reference |
| S4, S5 | | |

These gates deliberately involve **no client**. The stock‑client oracle (`xeyes`, `xclock`, `xterm`) was retired with the socket profile
([`90-`](90-decisions.md) D‑49), so P4 proves assembly only as far as "initialises, dispatches, and paints"; the first client‑driven proof is V‑P5‑5.
V‑P4‑3 doubles as the bisection gate for P5 failures — [`40-`](40-implementation-plan.md) §5.3.

### 4.5 P5 — In‑process transport (**the gated profile**)

| ID | Gate | Method |
|---|---|---|
| V‑P5‑0 | **Transport shape proven out of tree** — post → `fiber_notify()` → predicate wait, one server fiber owning the device, `lists.c` as the queue | `linux_hosted/sdl_fiber_example_v2`, 10 s: 3 client fibers post 40 requests, 40 served, 40 completions, **0 timeouts**, every draw on the render fiber — **DONE** ([`21-`](21-inprocess-transport.md) §11) |
| V‑P5‑1 | Zero sockets | `lsof -p $(pidof wmb_host) \| grep -c sock` = 0 |
| V‑P5‑2 | Zero network/wait syscalls | `strace -f -e trace=network,select,poll` → empty |
| V‑P5‑3 | **`pollEvents` at ≥ 30 Hz** | Instrumented counter; **this is the `BlockHandler` preservation check** |
| V‑P5‑4 | Injected key event reaches a client's event queue | Backend injection → client receives `KeyPress` |
| V‑P5‑5 | `wmb_test_client` prints 6 PASS lines | Exit status + stdout match |
| V‑P5‑6 | The test window is visible with correct content | Screenshot diff against reference |
| V‑P5‑7 | Reply path: `XGetWindowAttributes` returns correct data | Compare against the values used at creation |
| V‑P5‑8 | Variable‑length reply: `XGetWindowProperty` on a 4 KB property round‑trips exactly | Byte compare |
| V‑P5‑9 | Error path: deliberate `BadWindow` reaches `XSetErrorHandler` | Handler sets a flag |
| V‑P5‑10 | Async error (fire‑and‑forget request) surfaces at the next `XNextEvent` | Handler flag with no intervening round trip |
| V‑P5‑11 | **Differential byte test** — §5 | See §5 |
| V‑P5‑12 | `InsertFakeRequest` / `ResetCurrentRequest` work | XKB auto‑repeat produces repeated `KeyPress`; `Xext/sleepuntil` `AwaitSync` completes |
| V‑P5‑13 | XID allocation past the initial range (XC‑MISC top‑up) | Allocate `resource_mask + 100` resources |
| V‑P5‑14 | Valgrind clean over a 60 s session | |
| N1, N2, N6 | | |

V‑P5‑3 deserves emphasis. It is a one‑line instrumented counter and it catches the single highest‑likelihood/highest‑impact bug in the project
([`21-`](21-inprocess-transport.md) R3, [`11-`](11-portability-plan.md) R1): a `WaitForSomething` stub that compiles, links, runs, draws — and has no
input, because `BlockHandler`/`WakeupHandler` were dropped.

V‑P5‑0 is green already and is **not** a substitute for any gate below it. It proves the dispatch protocol and the queue in isolation, before TinyX is in
the picture, so a failure in V‑P5‑5..14 can be attributed to the X11 half rather than to the handshake.

### 4.6 P6 — Dependency severing

| ID | Gate |
|---|---|
| V‑P6‑1 | Every P5 gate still passes after the guards are applied |
| S2, S3, S6, S7, N1, N2, N3, N4 | |

### 4.7 P7 — JWM

| ID | Gate | Method |
|---|---|---|
| V‑P7‑1 | `libjwm.a` has no duplicate `T` symbols and no undefined `fork`/`execv`/`waitpid`/`sigaction`/`select` | `nm` |
| V‑P7‑2 | Root window within 200 ms of boot | Instrumented timestamp |
| V‑P7‑3 | Client window appears **with decorations** | Screenshot |
| V‑P7‑4 | Title‑bar drag moves the window | Scripted pointer input + screenshot |
| V‑P7‑5 | Corner drag resizes the window | Same |
| V‑P7‑6 | Root menu opens and items are selectable | Same |
| V‑P7‑7 | Two clients coexist; focus follows click; stacking is correct | Same |
| V‑P7‑8 | **`XGrabServer` blocks the other client** | Client A grabs; client B's request does not complete until the ungrab |
| V‑P7‑9 | JWM periodic callbacks fire without busy‑spin | CPU < 5 % with a clock widget running |
| V‑P7‑10 | No fiber starves: each of three fibers scheduled within 50 ms | Instrumented per‑fiber last‑run timestamp |

### 4.8 P8 — External ABI and stability

| ID | Gate | Method |
|---|---|---|
| V‑P8‑1 | `libX11-app.a` < 16 KB, exports only wrappers + `wmb_bind` | `size`, `nm` |
| V‑P8‑2 | `wmb_hello` produces a visible decorated window over the bridge | Screenshot |
| V‑P8‑3 | v1 app runs against v2 WMB | Build both, run |
| V‑P8‑4 | **Leak**: 10,000 window create/destroy cycles, flat heap high‑water | Instrumented `wmb_os_sbrk` high‑water mark |
| V‑P8‑5 | **Stability**: 1‑hour run with continuous scripted input; flat heap; no canary trip; no starvation | Long‑run harness |
| V‑P8‑6 | **Measurements recorded**: per‑fiber stack high‑water, heap high‑water, image size | Written into [`31-`](31-target-build.md) §5.2, §8.1, replacing the estimates |
| S9 | | |

### 4.9 P9 — Freestanding target

| ID | Gate |
|---|---|
| V‑P9‑1 | Every TU compiles `-ffreestanding -fno-builtin -fno-pic` |
| V‑P9‑2 | Image fits the memory budget (`size wmb.elf`) |
| V‑P9‑3 | On hardware: boot to a JWM root window |
| V‑P9‑4 | On hardware: `wmb_test_client` passes all six steps |
| V‑P9‑5 | On hardware: keyboard and mouse work |
| V‑P9‑6 | On hardware: 1‑hour run, flat heap, no canary trip |
| N3, N7, N8, N9, N10 | |

## 5. The Differential Byte Test

Gate V‑P5‑11 / T5. The single highest‑value test in the project, because it catches the two failure modes that
[`21-inprocess-transport.md`](21-inprocess-transport.md) identifies as high‑likelihood **and** high‑impact.

### 5.1 Method

```
                reference                                 subject
   ┌────────────────────────────────┐      ┌──────────────────────────────────┐
   │ golden capture (offline, once) │      │ host-pure profile                │
   │   ref_client ──socket──► any   │      │   ref_client (as a fiber) ──► WMB│
   │   system X server              │      │   WMB_DEBUG_TRACE_REQUESTS dumps │
   │   xtrace captures the bytes    │      │   dpy->buffer before dispatch    │
   └───────────────┬────────────────┘      └────────────────┬─────────────────┘
                   └───────────── diff ─────────────────────┘
```

`ref_client` is a single program exercising a fixed script of API calls, compiled twice — once against stock libX11 with sockets, once as an internal WMB
fiber. The reference side runs against the development machine's own X server and its output is checked in, so no WMB profile needs a socket.

### 5.2 The script

Ten calls chosen to cover every request shape:

| # | Call | Shape it covers |
|--:|---|---|
| 1 | `XCreateSimpleWindow` | Fixed‑size request, XID allocation |
| 2 | `XMapWindow` | Minimal request |
| 3 | `XStoreName` | Variable payload via `Data()` — **the `Data()` path** |
| 4 | `XChangeProperty` with a 4 KB value | **`Data()` overflow → buffer growth** — the R2 case |
| 5 | `XGetWindowAttributes` | Fixed reply |
| 6 | `XGetWindowProperty` on that 4 KB property | **Multi‑part variable reply** — the R1 case |
| 7 | `XQueryTree` | Variable reply with a list |
| 8 | `XInternAtom` | Reply plus string payload |
| 9 | `XDrawLines` with 500 points | Large fixed‑shape payload |
| 10 | `XCreateGC` with many values | Value‑list encoding |

Calls 4 and 6 are the reason the test exists.

### 5.3 Pass criterion

For each of the ten calls, the request bytes are **byte‑identical** between reference and subject, and the reply bytes returned to the client are
byte‑identical.

Permitted differences: the sequence number field (both sides must be internally consistent, not identical to each other) and the XID base.

## 6. Continuous Checks

Wired into every build from P4 onward; total runtime seconds.

| Check | Frequency |
|---|---|
| S1–S6 source gates | Every build |
| N1, N2, N4, N5, N6, N7 symbol gates | Every link (`host-pure`) |
| N3, N8, N9, N10 | Every link (`target`) |
| Level‑0 unit smokes (P1–P3 gates) | Every build |
| `wmb_test_client` six‑step | Every build from P5 |
| Differential byte test | Nightly |
| One‑hour stability | Weekly and before any phase sign‑off |

## 7. Traceability

Every requirement in [`00-introduction.md`](00-introduction.md) §9, mapped to the gate that proves it.

| Success criterion | Proven by |
|---|---|
| 1. Single binary | N7, plus the absence of any `.so` in the link |
| 2. Zero transport | N1, V‑P5‑1, V‑P5‑2 |
| 3. Zero OS coupling above the contract | S2, N2, N3, N4 |
| 4. Zero backend leakage | S1, V‑P1‑4 |
| 5. The hosted build runs | V‑P7‑2 … V‑P7‑7 |
| 6. The target path is what was tested | The gated profile is `host-pure` throughout §4.5–§4.8 |
| 7. Patch budget respected | S6, S7, S3 |
| 8. Freestanding compile green | V‑P9‑1, N8 |
| 9. Stability | V‑P8‑4, V‑P8‑5, V‑P9‑6 |

And the four things the project was asked to deliver:

| Request | Proven by |
|---|---|
| List all necessary components | S4, S5 — the inventory is mechanically checked against the build |
| Detach Linux dependency, portable, raw compilation | S2, S8, N2, N3, N8, V‑P9‑1 |
| Remove network transport | N1, N6, V‑P5‑1, V‑P5‑2, V‑P5‑11 |
| Host build with SDL first, then embedded | Phase ordering; V‑P1‑3 → V‑P7‑* on host, V‑P9‑* on target |

## 8. What Is Not Tested

Stated explicitly so that nobody mistakes silence for coverage.

| Not tested | Why |
|---|---|
| X11 protocol conformance beyond what our clients use | We are not shipping a general X server. JWM, `wmb_test_client` and `wmb_hello` define the required surface |
| Rendering pixel‑exactness against a reference X server | `tinyx/fb` is unmodified upstream; if it renders differently, that is upstream's behaviour |
| Extensions we do not compile (SHM, DBE, BIGFONT, XDMCP) | Excluded by design |
| Multi‑screen, RandR mode setting, DPMS transitions | Out of scope |
| Locale, XIM, complex text | Out of scope |
| Security / access control | No untrusted peer exists |
| Performance | Not a project goal. Latency is incidentally ~1000× better; nobody has to prove it |
