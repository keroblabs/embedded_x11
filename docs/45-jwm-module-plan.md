# 45 - JWM module and P7 status

## 1. Purpose and status

**P7 is implemented on the actual in-process stack.** JWM runs as an internal
client fiber, reparents windows, draws native decorations, handles moves/resizes,
focus/stacking, task-list/root-menu actions and an uptime clock.
It is not a native process and does not call `fork`, `select` or signal APIs.

[The integration guide](../jwm/wmb/README.md) is the runnable build/reuse guide.
[41-](41-verification-plan.md) defines the gates.
This document supersedes the earlier speculative three-file/400-line estimate.
The actual source/guard audit and user-approved scope are recorded in D-55.

### 1.1 Ownership

| Area | Owner |
|---|---|
| Window management, config and interaction | JWM and its [owned adapters](../jwm/wmb/) |
| Request/reply/event transport | [46-](46-transport-module-plan.md) |
| Server DIX execution | Existing `xsrv` fiber and unchanged upstream `Dispatch()` |
| Startup/shutdown and application registry | [transport lifecycle](../transport/x11/wmb_main.c), [application adapter](../transport/x11/wmb_apps.c) |
| OS/SDL entry, CPU/capture and input test scripts | [host application](../linux_hosted/apps/wmb_host/) |
| Display/input behavior | Platform-neutral HAL via KDrive; no JWM backend API |

## 2. Source/build profile

`wmb_jwm` has **52 translation units**: 49 retained sources and three owned
entry/configuration/timing adapters. Native `timing.c` is excluded unchanged.
The [frozen manifest](reference/jwm-sources.reference) is configure-checked.
Generated configuration stays in the build tree.

Core fonts, drawn title buttons and in-memory client icons are retained.
Optional graphics/font/image libraries, SHAPE/Xinerama and image/config files
are off. File includes, process output and environment expansion fail visibly;
commands resolve to registered applications, not a shell. The clock uses HAL
monotonic uptime; an enabled wall clock is not assumed.

### 2.1 Upstream preservation and symbols

Sixteen files carry `WMB_INPROC` branches. The
[generated manifest](../patches/MANIFEST.md) is authoritative; undefined selects
the native imported reference, while defined one/zero/empty selects WMB.
Native compilation/preprocessing and notice bytes are checked.

`jwm_namespace.h` uses compiler external-name pragmas to isolate **400 symbols**.
A plain `#define display` would also rename unrelated XEvent fields and was
rejected. Four native definitions lacking prior declarations receive proper
prototype declarations in the generated namespace header. No binary post-link
rewriting or copied core is involved.

The private JWM/Xlib archives are hidden and ordered before SDL's native
dependencies. JWM calls retained Xlib directly; `jxlib.h` is unchanged. P8's
external application vtable is a separate boundary, not required for this
in-process WM.

### 2.2 Necessary behavior corrections

- Event waits use predicates and actual callback deadlines. A fixed `select`
  timeout or `XNextEvent`-forever would prevent periodic callbacks/cancellation.
- Under a pointer grab, callers use the grabbed-root context and queued pre-grab
  motion is normalized to root coordinates. This retains the newest motion
  without reading an incoming event that keyboard entry has not initialized.
- An absent optional NumLock has no modifier mask. It must not be interpreted
  as the zero entries in the modifier map.
- The clock registers one-second callbacks, records completed draw time and
  observes deadline-aware event progress.
- Every modal event-loop consumer honors termination, using native controller/
  grab/status cleanup for moves, resize, pager drag, menus and choose-window mode.
- Core WM_CLIENT_MACHINE conversion owns/frees its string property correctly
  without loading locale/fontset backends.

These are guarded project improvements, not unconditional changes to native JWM.

## 3. Contracts and lifecycle

### 3.1 Application registry (I5)

[wmb_app_table.h](../transport/api/wmb_app_table.h) supplies the frozen
`int entry(int, char **)` registry contract. The boot module defines the static
table and size; lookup returns NULL for unknown names and JWM logs the miss.
The real launch adapter copies argv, creates a blocked task and retains its
control block until safe owner-side reaping.

Commands are whitespace-separated, at most 32 arguments and 4096 bytes per
argument. Shell operators/quoting are rejected. The returned task handle is
borrowed: wake it, but do not destroy it or retain it after completion.
The P7 test proves copying, blocked start, OOM, 100 reuse cycles and cancellation.
The original declaration-only smoke remains a separate contract baseline.

### 3.2 Component entry and readiness

[wmb.h](../transport/api/wmb.h) defines:

- `wmb_main(cfg)`: once-only synchronous startup after runtime initialization.
- `wmb_poll()`: 0 while running, 1 after clean completion, negative on failure.
- `wmb_shutdown()`: bounded cooperative stop.
- `wmb_ready()` / `wmb_stop_requested()`: client admission and application stop.

Server and JWM tasks start blocked. Only `xsrv` is released until native server
readiness; JWM is then released. Applications are admitted only when WM readiness
has been reported. The lifecycle returns rather than taking ownership of the
host's main loop. HAL polling stays on the server's existing KDrive block seam;
a duplicate `hal` fiber would give the device two owners.

The caller must continue yielding/polling and keep custom XML/registry entries
alive through shutdown. Foreign-thread use, transparent reinitialization and
abandoned-display cleanup are not supported P7 lifecycle semantics.

### 3.3 Stop order

1. JWM unwinds any active interaction, unmanages live windows and closes.
2. Applications observe stop, close their own `Display` and return.
3. The component reaps tasks, stops `xsrv`, then releases HAL resources.

A generic KDrive-port stop callback defers SDL close/error while this happens.
It is not added to the public HAL; root-only/standalone users keep their existing
termination path. A noncooperative application produces an explicit timeout.
Fatal Xlib/server failures are not advertised as recoverable external ABI errors.

### 3.4 Error-handler isolation

Native `XSetErrorHandler` remains the API fallback. JWM installs an internal
per-Display handler so another in-process application's global handler cannot
replace its startup/access-error behavior. The test deliberately installs a
client handler and generates BadWindow while JWM stays error-free.

## 4. Verification

### 4.1 In-process integration

`--wm-test` verifies:

| Gate | Actual evidence |
|---|---|
| V-P7-1 | Complete archive export/import and no-backend linkage checks |
| V-P7-2 | Server + JWM ready in under 200 ms in the measured normal workload |
| V-P7-3 | Two reparented clients, frame extents, title-band pixels and actual desktop capture |
| V-P7-4 | Held-button title-bar drag and native keyboard move produce exact geometry deltas |
| V-P7-5 | Corner drag and native keyboard resize produce exact client/frame dimensions |
| V-P7-6 | A real mapped menu is selected and launches a third registered client |
| V-P7-7 | Clicking each client transfers focus and raises its frame |
| V-P7-8 | A grab on JWM's own connection blocks a real peer until native ungrab |
| V-P7-9 | Clock framebuffer-draw completions within 990-1010 ms; bounded callbacks and normal idle CPU <5% |
| V-P7-10 | Actual server/JWM/demo fiber dispatch gaps <=50 ms |

The test waits on server state/geometry/grab cleanup, not guessed sleeps.
Small-display tests exposed why a resize must wait for the real status window
before injecting its final motion. Native window-crossing semantics are retained.
Timing measurements run without concurrent builds/instrumentation; they are not
hard real-time guarantees under arbitrary host load.

`--wm-cadence` repeats the idle/precision window for 60 seconds.
`--wm-soak` keeps behavior/memory gates while not treating Valgrind overhead as a
performance certificate. Three additional modes stop with a live menu/move/resize.
The full normal suite has 36 tests; the optional reference makes 37.

### 4.2 Independent reference

The same portable archive is also linked to genuine system Xlib, not the subject
Xlib/TinyX. An isolated Xvfb test verifies two reparented stock clients, registry
launch and cleanup. The runner waits for its unique display and always terminates
that specific server. This is optional host scaffolding, not another transport
profile in the subject.

The native JWM configuration was generated from the unmodified imported source
with optional libraries off; [the captured header](reference/jwm-config.reference.h)
feeds native-reference probes. It is not a production config.

### 4.3 Extended P6 boundary

The audit now covers **506 actual units** and **44 patched files**. Thirty-five
affected reference translation units compile/preprocess identically to the exact
import baseline with the selector undefined; one/zero/empty project definitions
agree. Original notices and patch reproduction are checked.

Nine complete windowing/runtime archives resolve under `--whole-archive` and
`--no-gc-sections` with contract stubs, libc/math/zlib, no host launcher and no
backend. JWM's 400-symbol namespace is checked independently. The HAL/framework
remain free of windowing ABI flags.

## 5. Phase participation

| Phase | Current participation |
|---|---|
| P1-P3 | Consume the already working HAL/runtime |
| P4-P6 | Preserve independent root/client/portability regression gates |
| **P7** | **Implemented: real WM, registry, lifecycle, interaction and cancellation** |
| P8 | Decorate external-ABI clients; extend attribution, stress and threading gates |
| P9 | Hardware and freestanding console/allocator/libc/math/zlib closure |

## 6. Next boundaries

P7 does not implement the external wrapper/vtable, foreign-thread submission,
10,000-window stability, one-hour endurance, restartable multi-instance
lifecycle or a freestanding board port. P8/P9 retain those obligations.
See [the run/reuse guide](../jwm/wmb/README.md) and
[the verification plan](41-verification-plan.md) rather than treating an archive
build alone as completion.
