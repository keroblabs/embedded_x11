# 45 — JWM Module Plan

## 1. Purpose

This is the **window manager team's working document**: the slice of [`40-implementation-plan.md`](40-implementation-plan.md) that turns Joe's Window
Manager from a POSIX process into an internal WMB client — a fiber that opens a `Display`, reparents client windows, and never calls `fork`, `select`
or `sigaction`.

It is a **single‑phase module with a very concrete result**: at the end of P7 a window has a title bar, it moves when dragged, and a right‑click opens
a menu. [`40-`](40-implementation-plan.md) remains the authority on phase order; this document is the authority on what this module builds.

### 1.1 Ownership boundary

| | In this module | Elsewhere |
|---|---|---|
| The WM | all of `jwm/src/` | — |
| Build glue | `jwm/CMakeLists.txt`, `jwm_namespace.h`, `jwm_entry.c`, `jwm_entry.h` | — |
| Harness | `linux_hosted/jwm_standalone/` | — |
| Configuration | The hard‑coded JWM configuration that replaces `.jwmrc` | — |
| Xlib | — | `libX11/` and the transport → [`46-`](46-transport-module-plan.md) |
| The launch table | `command.c`'s call into `wmb_app_table` | `wmb_app_table` itself → [`46-`](46-transport-module-plan.md) |
| Readiness gate | — | `wmb_main()` releases the client fibers → [`46-`](46-transport-module-plan.md) |

**Six files hold JWM's entire POSIX footprint**: `command.c`, `event.c`, `font.c`, `main.c`, `misc.c`, `timing.c`. Everything else — 44 files, the
whole window‑management core — **is pure Xlib and is not touched.**

### 1.2 What this document is not

- **Not the JWM file inventory.** [`01-component-inventory.md`](01-component-inventory.md) §11.1.
- **Not the guard specification.** [`11-portability-plan.md`](11-portability-plan.md) §8.3 gives the three guards verbatim.
- **Not the transport.** JWM is a *client*; it sees `XOpenDisplay` and nothing below it.

## 2. The File Set

| Group | Contents | Files | ~LOC |
|---|---|---:|---:|
| `JWM` | `jwm/src/` + the new glue | 52 | 26,500 |

26,325 LOC of C plus 4,517 LOC of headers, of which this project writes roughly **400 lines of glue** and guards **three files**.

### 2.1 Disposition by concern

| Concern | Files | Disposition | Action |
|---|---|---|---|
| Core WM logic | `client.c`, `border.c`, `place.c`, `resize.c`, `move.c`, `desktop.c`, `group.c`, `taskbar.c`, `tray.c`, `menu.c`, `root.c`, `winmenu.c`, … | CARRY | **No changes** |
| Entry point | `main.c` | GUARD | `main` → `Jwm_Main`; drop `sigaction`; hoist globals into a state struct |
| Event loop | `event.c` | GUARD | `select()` → `hal_yield_timeout()` |
| Command execution | `command.c` | GUARD | `fork`/`execl`/`waitpid` → `wmb_app_table` lookup + `hal_task_create()` |
| Timing | `timing.c` | GUARD | Drop `TZ` push/pop |
| Misc | `misc.c` | GUARD | The last `getenv` |
| Fonts | `font.c` | GUARD | Core fonts only; no Xft path |
| Xlib call macros | `jxlib.h` | GUARD | Add the profile block routing `JX*` through the vtable |
| Build glue | `jwm_namespace.h`, `jwm_entry.c`, `jwm_entry.h`, `jwm/CMakeLists.txt` | NEW | Symbol renaming and the fiber entry adapter |

**Only three of these consume patch budget.** `main.c`, `misc.c` and `timing.c` are handled by **`-include jwm_namespace.h`** instead of by editing —
the header `#define`s `main` → `Jwm_Main`, `getenv` → `Jwm_GetEnvStub`, and neutralises the `TZ` helpers. That mechanism is what keeps the JWM diff at
three files.

### 2.2 Optional dependencies — all off

Present in `jwm/configure.ac` as `--enable-*`, all disabled in `jwm/CMakeLists.txt`:

PNG · JPEG · Xpm · cairo · librsvg · Xft · Xrender · pango · Xmu · Xinerama

**Retained:** XBM icons (via `XReadBitmapFileData`) and SHAPE (optional). Every one of the ten disabled libraries would otherwise drag in a
filesystem, a font config path, or a second rendering stack.

## 3. Phase Participation

| Phase | This module's involvement | Weight |
|---|---|---|
| P1–P5 | None. JWM cannot be integrated before a client can open a display | — |
| P6 | The `jwm/` guards are counted in the thirteen; see §4.1 | — |
| **P7** | **Primary owner — the whole module** | **Heavy** |
| P8 | JWM decorates `wmb_hello`'s window; the E4 threading test runs against a live WM | Light |
| P9 | JWM's root window is the on‑hardware boot gate (V‑P9‑3) | Light |

### 3.1 Preconditions

The two acts of §4.7 have very different preconditions. **Act A (the standalone harness, §4.8)** needs the framework, one contract header from the
transport module, and stock system Xlib — nothing more. **Act B (the full‑P7 crossover)** needs the transport module's `libx11_wmb.a` and the tinyx
server. The window manager team's day‑one blocker is a single header from [`46-`](46-transport-module-plan.md); everything else it needs for J7‑0 is
either shipped or in‑tree.

#### 3.1.1 Code inputs the window manager team consumes

| # | Input | Owner | State | Needed for |
|---|---|---|---|---|
| I1 | `libwmb_framework.a` + `libwmb_fiber_linux.a` + `libwmb_system_linux.a` — the runtime substrate | [`44-`](44-framework-module-plan.md) | **DONE** — top‑level `CMakeLists.txt`, commits 1–8 | A + B |
| I2 | `hal/hal_task.h` — the nine cooperative‑wait functions (`hal_task_create`, `hal_yield_timeout`, `hal_task_wake`, `hal_task_wait`, …) | [`44-`](44-framework-module-plan.md) | **DONE** — frozen contract, PC‑V5..PC‑V7 | A + B |
| I3 | `libX11/include/X11/` — client‑side Xlib headers (`Xlib.h`, `Xlibint.h`, `Xutil.h`, `Xresource.h`, …) | upstream, in tree | **In tree**, carried unmodified | A + B |
| I4 | `X11/` (workspace root) — protocol and support headers (`X.h`, `Xproto.h`, `Xatom.h`, `keysym.h`, `cursorfont.h`, …) | upstream, in tree | **In tree** | A + B |
| I5 | `wmb_app_table` **contract header** — the `wmb_app_t` struct and the `wmb_app_table[]` / `wmb_app_table_size` externs. **Header only**; §4.8.4's stub is the first implementation | [`46-`](46-transport-module-plan.md) | **MISSING — the one blocker for J7‑0** | A + B |
| I6 | Stock system Xlib (`libX11-dev` / `libX11.so.6`) + `xserver-xephyr` (developer) / `xvfb` (CI) | System | Assumed present | A only |
| I7 | `libx11_wmb.a` — `libX11/src/` rebuilt under `WMB_INPROC` with the two `libX11/` guards | [`46-`](46-transport-module-plan.md), P5 + P6 | Not yet built | B only |
| I8 | `libtinyx.a` — the server, including grab flow control in `io_wmb.c` / `connection_wmb.c` | [`42-`](42-tinyx-module-plan.md), P4 + P5 + P6 | Not yet built | B only |
| I9 | Real `wmb_main()` with the readiness gate; real pointer + keyboard backends (D2, D3) | [`46-`](46-transport-module-plan.md), [`43-`](43-kdrive-module-plan.md) | Not yet built | B only |

**`libjwm.a` compiles against headers only** — I1–I5. No `.a` from another module is needed to produce the archive: every guard in `event.c`,
`command.c`, `main.c`, `misc.c`, `timing.c` and `jwm.h` resolves against a declaration, not a symbol.

#### 3.1.2 Do we need `libx11_wmb.a` before starting?

**No.** `libx11_wmb.a` is the in‑tree libX11 that the transport module builds by compiling `libX11/src/` (~245 files, ~34 kLOC) under `WMB_INPROC` with
the two guards of [`46-`](46-transport-module-plan.md) §5. This module does not build it, does not depend on it for the archive `libjwm.a`, and does not
depend on it for the harness. The harness links `libjwm.a` against **stock system `libX11.so.6`** (§4.8.1). That is the whole point of Act A: guards
#11–#13 and the event‑loop rewrite land behaviourally, on a well‑understood client library, **before** the transport becomes a variable.

`libx11_wmb.a` is required at §4.7 step 8 — Act B, the crossover — and it is delivered there by [`46-`](46-transport-module-plan.md), not authored
here. The window manager team is a consumer of it, not an author.

#### 3.1.3 What actually blocks J7‑0 today

Only I5. The framework libraries ship, the HAL contract is frozen, the X11 headers are in tree, and the system provides Xephyr / Xvfb. Before this
module writes its first line of glue, [`46-`](46-transport-module-plan.md) must publish a header — `transport/api/wmb_app_table.h` or the equivalent —
declaring:

```c
#include <hal_task.h>          /* for hal_task_t */

typedef int (*wmb_app_main_t)(int argc, char **argv);   /* the C main() shape */

typedef struct {
    const char     *name;
    wmb_app_main_t  entry;
    size_t          stack_bytes;
} wmb_app_t;

extern const wmb_app_t wmb_app_table[];
extern const size_t    wmb_app_table_size;

const wmb_app_t *wmb_app_lookup(const char *name);                          /* NULL on miss */
hal_task_t      *wmb_app_launch(const wmb_app_t *app,
                                int argc, char **argv);                     /* creates the fiber */
```

so J7‑6's `command.c` guard and §4.8.4's stub compile against the same declarations, and so an incompatible change to the real launch table breaks
the harness at build time rather than at deployment. **One header, no implementation** — the implementation lands with the rest of the transport in P7.
This is the concrete artefact §4.8.4's "executable specification" claim rests on.

**A JWM integration that starts Act B before I7–I9 are true will spend its time debugging other modules.** The original warning applies to the
crossover, not to J7‑0.

#### 3.1.4 Closing I5 — the concrete steps

Finishing I5 is small in code and short on decisions. The design work happens first; once it is settled the header is ~20 LOC and a five‑line CMake
fragment.

**Decisions (transport ↔ this module, before typing any code).** Each row states the problem the decision solves, then this module's recommended
answer.

| # | Decision | Problem | This module's ask |
|---|---|---|---|
| D‑I5‑1 | Entry‑point signature | The `wmb_app_t.entry` field is a function pointer. Its signature determines what "an in‑tree app" looks like at the source level, and whether the caller can drop into `hal_task_create` directly or needs an adapter | Use `main()`'s shape: `int (*)(int argc, char **argv)`. JWM's own `Jwm_Main` (`main` renamed, §4.4) already has this signature, and every in‑tree app looks like a standalone C `main()`. WMB does not have to pass arguments today — an empty argv is acceptable — but the interface is kept for later. Cost: one transport‑side trampoline that unpacks a heap argv slab and calls the entry, so `hal_task_create` (which wants `void(*)(void *)`) can drive it. Written once inside `wmb_app_launch` (artefact 1), not per app. **`Jwm_Main` itself spawns whatever internal fibers JWM needs** — that is JWM's concern, not the launch table's |
| D‑I5‑2 | Who splits the command string | JWM's config strings are shell‑style: `<StartupCommand>xterm -e vi</StartupCommand>`. With no shell in WMB, someone has to split that string into a program name and an `argv` before the entry point can be called. Two candidates: JWM's `command.c` before it calls into the transport, or the transport inside an overloaded `wmb_app_launch(const char *command)` | JWM tokenises. `command.c` splits on whitespace and calls `wmb_app_launch(app, argc, argv)` with the split already done. Keeps the transport contract free of a string parser and keeps §4.8.4's stub trivial — each stub app receives an already‑parsed argv, like any C `main()` |
| D‑I5‑3 | How JWM finds an entry by name | JWM's `command.c` starts with a name (`"xterm"`) and needs the matching `wmb_app_t *`. Two APIs are possible: expose `wmb_app_table[]` and let JWM walk it, or provide a `wmb_app_lookup(name)` that does the walk internally | Provide `const wmb_app_t *wmb_app_lookup(const char *name)`. Encapsulates the table; gives one place to add case‑folding, aliasing (`"term"` → `"xterm"`), or later plumbing (metrics, permission checks) without touching every caller |
| D‑I5‑4 | What happens when a name isn't in the table | e.g. JWM's config says `<StartupCommand>doesnt-exist</StartupCommand>` and no `wmb_app_t` matches. Behaviour options: return `NULL` and let the caller decide, or hard‑fail (`abort` / trap) inside the transport, or do nothing silently | `wmb_app_lookup` returns `NULL`. JWM logs the miss at the call site, where it has context — which config line, which menu item — that the transport does not. §4.3's "fails visibly" is that log line, not a transport abort. A silent no‑op is explicitly not allowed |
| D‑I5‑5 | Where the header lives | JWM must `#include` the contract, so it needs a stable public path. `transport/` currently contains only `transport/x11/` (client‑side implementation) and a `.gitkeep`. The question is which directory owns the transport module's **public** headers, distinct from the transport's own source | `transport/api/wmb_app_table.h`. Create `transport/api/` as a peer of `transport/x11/`. `transport/x11/` stays implementation‑only; `transport/api/` is what external consumers `#include` — mirroring how `hal/` (contract) sits outside `framework/` (implementation) |

**Artefacts:**

1. `transport/api/wmb_app_table.h` — the `wmb_app_main_t` typedef, the `wmb_app_t` struct, the two externs, `wmb_app_lookup`, and `wmb_app_launch(const wmb_app_t *, int argc, char **argv)` which does the D‑I5‑1 trampoline and calls `hal_task_create`. ~20 LOC.
2. Top‑level `CMakeLists.txt` addition: an interface library `wmb_transport_api` exporting `transport/api/` as its public include path. Consumers pick it up with `target_link_libraries(... wmb_transport_api)`.
3. A one‑TU compile‑only smoke test (~20 LOC) that `#include`s the header, instantiates a two‑entry `wmb_app_table[]` with a `log_hello` entry (`int log_hello(int, char **)`), and calls `wmb_app_lookup("log_hello")`. Proves the header is includable and the struct usable, and doubles as the seed for §4.8.4's stub.

**Sign‑off:**

- [`46-`](46-transport-module-plan.md) confirms the API matches what `linux_hosted/wmb_boot/` will populate in P7 (this module's stub is retired entry‑for‑entry then).
- [`44-`](44-framework-module-plan.md) confirms `wmb_app_launch`'s trampoline correctly bridges `wmb_app_main_t` (`int(*)(argc, argv)`) to `hal_task_create`'s `hal_task_entry_t` (`void(*)(void *)`) — argv slab lifetime, stack sizing, and what happens when the entry returns.

**After I5 closes, this module executes §4.1's J7‑1..J7‑4 to produce `libjwm.a`** — the CMakeLists, the namespace header, the fiber entry adapter,
and the `jwm.h` include guard. I5 removes the interface blocker; the archive still has to be built. §4.7 Act A step 1 gives the order.

## 4. Phase P7 — Integration

**Goal:** a real window manager, running as an internal client, managing real windows. [`40-`](40-implementation-plan.md) §8.

### 4.1 Work items

| # | Item | Deliverable | Spec |
|--:|---|---|---|
| J7‑0 | Standalone harness: minimal `wmb_main` variant + stub `wmb_app_table`, linked against stock system Xlib, driven under Xephyr/Xvfb | New | §4.8 |
| J7‑1 | `jwm/CMakeLists.txt` with all ten optional libraries off | New | [`01-`](01-component-inventory.md) §11.1 |
| J7‑2 | `jwm_namespace.h`, injected with `-include` | New | §2.1 |
| J7‑3 | `jwm_entry.c` — `Jwm_FiberEntry` | New | — |
| J7‑4 | `jwm/src/jwm.h` — gate the `<sys/select.h>` / `<unistd.h>` / `<signal.h>` / `<sys/wait.h>` include block | Guard #11 | [`11-`](11-portability-plan.md) §8.3 |
| J7‑5 | `jwm/src/event.c` — `select()` on `ConnectionNumber` → a `hal_yield_timeout()` loop, **preserving the `Signal()` callback cadence** | Guard #12 | ″ |
| J7‑6 | `jwm/src/command.c` — `fork`/`execl`/`waitpid` → `wmb_app_table` lookup + `hal_task_create()` | Guard #13 | ″ |
| J7‑7 | Hard‑coded JWM configuration replacing `.jwmrc` | New | — |
| J7‑8 | `jxlib.h` profile block routing `JX*` through the vtable | Guard (slack) | [`01-`](01-component-inventory.md) §11.1 |

**Sequencing note.** [`11-`](11-portability-plan.md) §8.3 assigns guards #11–#13 to phase Q5, which maps to **P6** — but the code that carries them is
written **here**, in P7, because JWM is not in the build before P7. In practice the `patches/jwm/` series is authored and exported during this phase,
and P6's "all 13 guards applied" gate is satisfied for `tinyx/` and `libX11/` at P6 and completed for `jwm/` at P7. If the P6 gate must be literally
green on all thirteen, **J7‑0 is the mechanism**: it authors and behaviourally validates J7‑4…J7‑6 ahead of the rest of this phase, against Xephyr,
without any dependency on the transport (§4.8). The residual decision — whether to count JWM's guards in the P6 tally or defer them to P7 sign‑off —
still has to be recorded before P6 closes, but it no longer blocks anything.

### 4.2 The event loop is the whole integration

`event.c` is where JWM stops being a process. The rewrite is small and its two failure modes are both silent:

| Failure | Symptom | Guard against it |
|---|---|---|
| The loop busy‑spins instead of yielding | CPU pinned; every other fiber starves | V‑P7‑9: CPU < 5 % with a clock widget running |
| `Signal()`'s cadence is lost | Clock widgets stop, autohide never fires, tooltips never appear — **and nothing errors** | V‑P7‑9 measures cadence, not just CPU |

`hal_yield_timeout(ms)` returns **0** on timeout and **1** on wake. JWM's loop must treat both as "there may be work", and must not assume a return of
1 means an X event is pending.

### 4.3 The launch table replaces `fork`

`command.c`'s `RunCommand()` currently forks. In WMB there is no second process: an in‑tree application is a **fiber**, started by `hal_task_create()`
after a lookup in `wmb_app_table` ([`46-`](46-transport-module-plan.md)). A command that is not in the table fails visibly — it does not silently do
nothing.

### 4.4 Symbol namespacing

JWM defines `main`, and so does `dix/main.c`, and so does `wmb_boot.c`. `jwm_namespace.h` resolves this by renaming rather than editing, with **every
rename prefixed `Jwm_`** so that a collision with a legitimate symbol is visible ([`11-`](11-portability-plan.md) R7).

The check is mechanical and belongs in the build: `nm libjwm.a` must show **no duplicate `T` symbols**.

### 4.5 Definition of Done

- [ ] `libjwm.a` under the WMB profile links and its archive shows no undefined `fork`/`execv`/`waitpid`/`sigaction`/`select` (V‑P7‑0, harness).
- [ ] `libjwm.a` links with **no duplicate `T` symbols** and no undefined `fork`/`execv`/`waitpid`/`sigaction`/`select` under full P7 (V‑P7‑1).
- [ ] JWM's root window appears **within 200 ms of boot** (V‑P7‑2, harness + full P7).
- [ ] `wmb_test_client`'s window appears **with JWM decorations** (V‑P7‑3, harness with `xterm` in Xephyr + full P7 with `wmb_test_client`).
- [ ] The window can be **moved** by dragging its title bar (V‑P7‑4, harness + full P7).
- [ ] The window can be **resized** by dragging a corner (V‑P7‑5, harness + full P7).
- [ ] The root menu opens on a right‑click and its items are selectable (V‑P7‑6, harness + full P7).
- [ ] Two client windows coexist; focus follows the click; stacking order is correct (V‑P7‑7, harness + full P7).
- [ ] **`XGrabServer` works**: while JWM holds a grab, the other client is not serviced (V‑P7‑8, gate T4 — **full P7 only**; the harness's Xephyr grab is wire‑protocol, not the `io_wmb.c` client‑pause path).
- [ ] JWM's periodic callbacks (`Jwm_Signal`) fire at the expected cadence **without busy‑spinning** (V‑P7‑9, harness + full P7).
- [ ] No fiber starves: with three fibers running, each is scheduled within 50 ms (V‑P7‑10, **full P7 only**; the harness has no third fiber).
- [ ] `grep -rl "SDL" jwm/` is empty (S1); `jwm/` patch count ≤ 4 (S6); the series replays on a pristine checkout (S7).

**Harness vs full P7.** Of the eleven gates above, **eight fire in the standalone harness (§4.8)** — V‑P7‑0, V‑P7‑2..V‑P7‑7, V‑P7‑9 — and are
re‑run under full P7 to confirm the WMB transport doesn't regress them. Two gates — V‑P7‑8 and V‑P7‑10 — are full‑P7 only by design: one tests
tinyx‑specific flow control, the other needs a third fiber the harness doesn't spawn.

### 4.6 Note on the grab test

`XGrabServer` is **easy to leave as a no‑op and hard to notice is broken** — it manifests as intermittent decoration corruption weeks later. The
explicit two‑client gate exists because [`21-`](21-inprocess-transport.md) §5.7 is otherwise likely to be skipped, and because JWM is the only client
in the system that actually uses it.

### 4.7 The bring‑up order inside the phase

The order runs top‑to‑bottom in **two acts**. Act A stands JWM up in the harness (§4.8), which needs nothing below Xlib. Act B repeats the
behavioural steps against the WMB in‑process transport once P4/P5/P6 have delivered it.

| # | Act | Step | Runnable result |
|--:|:--:|---|---|
| 1 | A | J7‑1 + J7‑2: `libjwm.a` builds and links, entry point renamed | `nm` on the archive is clean; nothing runs yet |
| 2 | A | J7‑4: `jwm.h` guard | The tree compiles with no POSIX includes |
| 3 | A | J7‑0 + J7‑3 + J7‑7: harness `wmb_main` variant + stub `wmb_app_table` + `Jwm_FiberEntry` + the hard‑coded configuration | JWM reaches `XOpenDisplay(NULL)`, opens a connection to Xephyr, exits cleanly (V‑P7‑0) |
| 4 | A | J7‑5: the cooperative event loop | **The root window appears in Xephyr** (V‑P7‑2, harness) |
| 5 | A | Decoration and interaction with `xterm`s launched into the Xephyr `$DISPLAY` | V‑P7‑3..V‑P7‑7 (harness) |
| 6 | A | J7‑6: `command.c` → stub `wmb_app_table` | Root menu launches a stub fiber with visible side effect (V‑P7‑6, harness) |
| 7 | A | Cadence and CPU under the harness | V‑P7‑9 (harness) — clock cadence stable, CPU < 5 % |
| 8 | B | Cross over to the full P7 build — same `libjwm.a`, WMB transport instead of stock libX11 + Xephyr | V‑P7‑1, then V‑P7‑2..V‑P7‑7 and V‑P7‑9 re‑fire against tinyx in‑process |
| 9 | B | Grabs and the three‑fiber starvation check | V‑P7‑8 and V‑P7‑10 — **full P7 only** |

**Step 4 is first light** — the root window appears, guards work, the cooperative loop yields correctly. **Step 8 is the transport crossover** —
the same behaviour reproduces against the real in‑process transport. Everything between steps 4 and 7 is behaviour verified before the transport is
wired; everything from step 8 onward proves the transport doesn't take that behaviour back.

### 4.8 The standalone harness

The J7‑0 harness is a first‑class artifact: a JWM binary that runs behavioural tests against real X11 without depending on tinyx or the WMB
in‑process transport. It exists so guards #11–#13 are validated **before** P7 pulls the transport onto the critical path, and so the framework API
surface JWM consumes is nailed down by an executable client rather than by inspection.

#### 4.8.1 Composition

| Layer | Component | Status |
|---|---|---|
| JWM sources | `jwm/src/*.c` under the same J7‑2/J7‑4/J7‑5/J7‑6 guards used by full P7 | Written in P7 (shared) |
| Fiber runtime | [`framework/fiber/`](../framework/fiber/) + [`linux_hosted/fiber_linux/`](../linux_hosted/fiber_linux/) | **Working** |
| HAL contract | [`hal/hal_task.h`](../hal/hal_task.h) (nine functions, PC‑V5..PC‑V7) | **Frozen** |
| Harness `wmb_main` variant | ~80 LOC — inits the runtime, spawns one JWM fiber, loops on `hal_task_yield` | New |
| Stub `wmb_app_table` | ~30 LOC — two or three entries with observable side effects (e.g. `log_hello`, `toggle_flag_and_exit`) | New |
| Xlib | Stock system `libX11.so.6` — encodes to the wire, sends over a Unix socket | System |
| X server | `Xephyr :N -screen 800x480` on developer machines; `Xvfb :N -screen 0 800x480x24` on CI | System |
| Launcher | `linux_hosted/jwm_standalone/run.sh` — starts the X server, runs the binary with `DISPLAY=:N`, tears down | New |

The harness lives in `linux_hosted/jwm_standalone/` alongside the other `fiber_example` targets. **It is not part of the P6/P7 boot chain** — it is
a build target and a script.

#### 4.8.2 What it verifies

| Gate | How the harness fires it |
|---|---|
| V‑P7‑0 | `nm libjwm.a` on the WMB‑profile archive: no undefined `fork`/`execv`/`waitpid`/`sigaction`/`select`, no duplicate `T` |
| V‑P7‑2 | Root window appears in Xephyr within 200 ms of `Jwm_FiberEntry` |
| V‑P7‑3 | `xterm` or `xclock` launched into Xephyr's `$DISPLAY` appears with JWM decorations |
| V‑P7‑4 | Drag the title bar of an `xterm` — window moves |
| V‑P7‑5 | Drag a corner — window resizes |
| V‑P7‑6 | Right‑click on root → menu opens; menu items launch the stub app‑table entries with visible side effects |
| V‑P7‑7 | Launch two `xterm`s — focus follows the click, stacking order is correct |
| V‑P7‑9 | `Jwm_Signal()` cadence: clock widget advances every second ±10 ms over 60 s; CPU < 5 % |

#### 4.8.3 What it does not verify

| Gap | Why | Where it's covered |
|---|---|---|
| WMB in‑process transport semantics | Uses stock libX11 → socket, not the vtable → tinyx path | [`42-`](42-tinyx-module-plan.md), [`46-`](46-transport-module-plan.md) module gates |
| `XGrabServer` flow control | Real Xorg's grab is a wire‑protocol grab; tinyx's grab is the `io_wmb.c` client‑pause flag | V‑P7‑8, full P7 only |
| Three‑fiber starvation | The harness has only JWM + main unless the stub app‑table populates more | V‑P7‑10, full P7 |
| Startup ordering across `wmb_main` readiness gate | The harness has no server fiber to release from | Full P7, D‑33 / D‑43 |

#### 4.8.4 The stub `wmb_app_table` is a reference implementation, not a mock

The stub uses the same struct layout, same lookup function, same lifetime semantics as the real `wmb_app_table` specified by
[`46-`](46-transport-module-plan.md). It differs only in payload: its entries are visible test hooks (log lines, flag toggles) rather than real
clients. When the real table lands in P7, the stub is retired entry‑for‑entry. This is why the harness doubles as an executable specification of
the launch‑table contract — a change to the contract breaks the stub, which breaks the harness, which is louder than a review comment.

#### 4.8.5 Cost

| | Value |
|---|---|
| New source in `linux_hosted/jwm_standalone/` | ~130 LOC (harness `wmb_main` + stub `wmb_app_table`) |
| New CMake | ~50 LOC |
| Launcher script | ~20 LOC |
| Runtime dependency added | `xserver-xephyr` on developer machines, `xvfb` on CI |
| CI wall time | ~15–30 s per run, dominated by X server startup |

## 5. Interfaces With Other Modules

| Seam | JWM's side | Counterpart |
|---|---|---|
| Display connection | `XOpenDisplay(NULL)` from inside `Jwm_FiberEntry` | Act A: stock system `libX11.so.6`. Act B: `wmb_open.c` → [`46-`](46-transport-module-plan.md) |
| Waiting | `hal_yield_timeout(ms)` in the event loop | `framework/fiber/` → [`44-`](44-framework-module-plan.md) |
| Launching | `wmb_app_table` lookup + `hal_task_create()` | Table → [`46-`](46-transport-module-plan.md); `hal_task_create` → [`44-`](44-framework-module-plan.md) |
| Startup ordering | Created **blocked**; released when the server signals ready | `wmb_main()`, D‑33 / D‑43 → [`46-`](46-transport-module-plan.md) |
| Server grabs | `XGrabServer` / `XUngrabServer` | Act A: stock server's wire‑protocol grab. Act B: `io_wmb.c` flow control → [`42-`](42-tinyx-module-plan.md) |
| Input semantics | Button press/release pairs, wheel as buttons 4/5/6/7 | [`43-`](43-kdrive-module-plan.md), defects D2 and D3 |
| Fonts | Core X fonts only | Built‑in bitmap fonts; FreeType stays dormant ([`90-`](90-decisions.md) D‑38) |

## 6. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| JW‑1 | `event.c`'s rewrite loses the `Signal()` cadence | Medium | Medium | §4.2; V‑P7‑9 measures cadence, not only CPU |
| JW‑2 | The cooperative loop busy‑spins and starves the server fiber | Medium | High | V‑P7‑9 and V‑P7‑10 together — one catches the spin, the other catches the consequence |
| JW‑3 | `jwm_namespace.h` renames collide with a legitimate symbol | Low | Medium | Every rename prefixed `Jwm_`; `nm` duplicate‑`T` check in the build |
| JW‑4 | `XGrabServer` silently a no‑op | Medium | Medium | §4.6; the explicit two‑client gate V‑P7‑8 |
| JW‑5 | An optional library sneaks back in through a distro `pkg-config` hit | Low | Medium | All ten are `OFF` explicitly in `jwm/CMakeLists.txt`, not left to detection |
| JW‑6 | The hard‑coded configuration drifts from what the gates assume | Medium | Low | It is a source file under review, not a data file; V‑P7‑6's menu items come from it |
| JW‑7 | The guard‑phase ambiguity of §4.1 is discovered at P6 sign‑off | Low | Low | J7‑0 authors and validates the JWM guards ahead of P6; the residual is a recording decision, not a code decision |
| JW‑8 | JWM turns out to need a POSIX facility not in the census | Low | Medium | The census found the footprint in six files; a seventh is a [`90-decisions.md`](90-decisions.md) entry, not a quiet edit |
| JW‑9 | The standalone harness passes but full P7 fails against the WMB transport | Medium | Low | §4.8.3's "does not verify" table is explicit; V‑P7‑1, V‑P7‑8 and V‑P7‑10 remain full‑P7 gates; §4.7 Act B re‑runs the harness's behavioural gates against the real transport |

## 7. Budget

| | Value |
|---|---|
| New files (JWM proper) | `jwm/CMakeLists.txt`, `jwm_namespace.h`, `jwm_entry.c`, `jwm_entry.h` |
| New files (harness) | `linux_hosted/jwm_standalone/{harness_main.c, stub_app_table.c, CMakeLists.txt, run.sh}` |
| New LOC | ~530 (~400 JWM glue + ~130 harness) |
| Upstream files guarded | **3 of the `jwm/` cap of 4** — one file of slack, provisionally reserved for `jxlib.h` (J7‑8) |
| Files handled by namespace header instead of a patch | 3 — `main.c`, `misc.c`, `timing.c` |
| Runtime CI dependency | `xserver-xephyr` (developer machines), `xvfb` (CI) |

## 8. What to Do First

The original guidance was "nothing until P5 and P6 are green". J7‑0 changes that: because the harness depends only on the fiber runtime (working) and
stock libX11 (system), Act A of §4.7 can be authored and validated in parallel with the P4/P5/P6 work on the transport, without competing for it.

0. **Get I5 landed** (§3.1.1) — the `wmb_app_table` contract header from [`46-`](46-transport-module-plan.md). This is the one prerequisite for J7‑0
   that this module cannot author itself. A one‑file header, no implementation. Everything else in the input list (I1–I4, I6) is already true.
1. **Build the standalone harness end‑to‑end** (J7‑0, subsumes J7‑1 and J7‑2). The stock‑Xlib build the earlier draft called out is folded into this:
   the harness *is* a JWM binary against stock system Xlib, with everything the guards and the app‑table interaction need, plus a runnable X server
   (Xephyr on the desk, Xvfb on CI). See §4.8. This exercises the fiber runtime and the `hal_task` contract with JWM's real event loop, and it is
   the mechanism §4.1's sequencing note refers to.
2. **Draft the hard‑coded configuration** (J7‑7) and decide what the root menu contains — V‑P7‑6 tests exactly those items, and the stub
   `wmb_app_table`'s contents follow from it.
3. **Enumerate what `Signal()` drives** — clock widget, autohide, tooltips, popup delays — with each one's cadence, so that §4.2's rewrite has an
   acceptance list rather than a hope, and V‑P7‑9 has numbers to compare against.

Then wait for P6 — specifically for I7–I9 (§3.1.1) — and run Act B of §4.7 — the transport crossover — in order.

**The failure mode this section used to warn against still applies**: starting the *transport* integration early means debugging other modules. Act A
avoids that trap by staying above Xlib.
