# 03 — Target Architecture

## 1. Purpose

This document describes **what WMB becomes**: the single‑image architecture, the layering rules that keep it portable, the execution model, and the two
build profiles it must satisfy simultaneously. It is the third deliverable of workstream 1 ([`00-introduction.md`](00-introduction.md) §5.1) and the design
premise that [`11-`](11-portability-plan.md), [`21-`](21-inprocess-transport.md), [`30-`](30-host-build.md) and [`31-`](31-target-build.md) each implement
a slice of.

### 1.1 What this document is not

- **Not the API specification.** Signatures live in [`12-platform-contract.md`](12-platform-contract.md) (platform),
  [`21-inprocess-transport.md`](21-inprocess-transport.md) (transport) and [`22-entry-point-abi.md`](22-entry-point-abi.md) (external ABI).
- **Not the build.** [`30-`](30-host-build.md) / [`31-`](31-target-build.md).
- **Not the plan.** [`40-implementation-plan.md`](40-implementation-plan.md).

## 2. The Single Image

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│ EXTERNAL APPLICATION (separate binary, target only)                             │
│   #include <X11/Xlib.h>   XCreateWindow(dpy, ...)                               │
│   links: libX11-app.a  ── one-line wrappers, ~20 KB                             │
└───────────────────────────────────┬─────────────────────────────────────────────┘
                                    │ indirect call through wmb_api_t
                                    │ at a fixed load address (non-PIC)
╔═══════════════════════════════════▼═════════════════════════════════════════════╗
║ WMB — ONE LINKED IMAGE                                                          ║
║                                                                                 ║
║  ┌────────────────────────┐   ┌──────────────────────┐   ┌───────────────────┐  ║
║  │ JWM (window manager)   │   │ in-tree application  │   │ wmb_test_client   │  ║
║  │  fiber: "jwm"          │   │  fiber: "app_N"      │   │  fiber: "test"    │  ║
║  └───────────┬────────────┘   └──────────┬───────────┘   └─────────┬─────────┘  ║
║              └───────────────────┬───────┴─────────────────────────┘            ║
║  ┌───────────────────────────────▼──────────────────────────────────────────┐   ║
║  │ libX11-wmb                                                               │   ║
║  │   • 245 upstream API files  — UNCHANGED                                  │   ║
║  │   • wmb_transport.c  — replaces xcb_io.c (13 symbols)                    │   ║
║  │   • wmb_open.c / wmb_close.c / wmb_setup.c                               │   ║
║  │   • dpy->synchandler = wmb_sync_handler       ← the bridge               │   ║
║  └───────────────────────────────┬──────────────────────────────────────────┘   ║
║                                  │  in-process hand-off (no bytes on a wire)    ║
║  ┌───────────────────────────────▼──────────────────────────────────────────┐   ║
║  │ TinyX server                              fiber: "xsrv"                  │   ║
║  │   dix (dispatch, tables, window, events, gc, resource, ...)              │   ║
║  │   mi · fb · miext/shadow · miext/damage                                  │   ║
║  │   render · randr · xfixes · damageext · Xext(subset)                     │   ║
║  │   os: log · oscolor · utils · osinit  +  io_wmb · connection_wmb ·       │   ║
║  │       waitfor_wmb                                                        │   ║
║  └───────────────────────────────┬──────────────────────────────────────────┘   ║
║  ┌───────────────────────────────▼──────────────────────────────────────────┐   ║
║  │ KDrive DDX                                                               │   ║
║  │   kdrive/src (generic)  +  kdrive/tima (ours)                            │   ║
║  │   KdCardFuncs · KdKeyboardFuncs · KdMouseFuncs · KdOsFuncs               │   ║
║  └───────────────────────────────┬──────────────────────────────────────────┘   ║
║  ┌───────────────────────────────▼──────────────────────────────────────────┐   ║
║  │ PLATFORM CONTRACT  ── the only OS-facing surface in the image            │   ║
║  │   hal_display.h · hal_input.h · hal_task.h · hal_time.h · wmb_os.h       │   ║
║  └───────────────────────────────┬──────────────────────────────────────────┘   ║
║  ┌───────────────────────────────▼──────────────────────────────────────────┐   ║
║  │ RUNTIME SUBSTRATE      fiber core · heap · buffer     (shareable: D-31)   │   ║
║  │ HOST                   wmb_boot.c — this configuration's main(): D-42    │   ║
║  └───────────────────────────────┬──────────────────────────────────────────┘   ║
╚══════════════════════════════════▼══════════════════════════════════════════════╝
   ┌──────────────────────────────┐   ┌────────────────────────────────────────┐
   │ HOSTED BACKEND               │   │ TARGET BACKEND                         │
   │  SDL window/texture/events   │OR │  MMIO framebuffer · input ISR ring     │
   │  libucontext fiber port      │   │  target fiber port · 4 syscalls        │
   │  clock_gettime · libc malloc │   │  monotonic counter · region allocator  │
   └──────────────────────────────┘   └────────────────────────────────────────┘
```

## 3. Layering Rules

Nine rules. Each is mechanically checkable and each has a gate in [`41-verification-plan.md`](41-verification-plan.md).

| # | Rule | Gate |
|---|---|---|
| L1 | **Nothing above the platform contract names a platform.** No `SDL`, no `<linux/*>`, no target register names in `tinyx/`, `libX11/`, `jwm/`, or `transport/`. | `grep -rl "SDL\|<linux/" tinyx libX11 jwm transport` empty |
| L2 | **Nothing above the contract blocks.** All waiting is `hal_yield_timeout()`. No `select`, `poll`, `sleep`, `usleep`, `nanosleep`, `pthread_cond_wait`. | Symbol gate on the link |
| L3 | **The contract is the only downward interface.** A DDX file may include `hal_*.h`; it may not include a backend header. | `grep -rl "sdl_\|hal_drivers.h" tinyx/` empty |
| L4 | **Exactly one backend is linked.** Selected by the build profile, never by runtime dispatch. | CMake source list |
| L5 | **Exactly one `main()`.** In `linux_hosted/wmb_boot/wmb_boot.c` on the hosted build, `<target>_native/wmb_boot/wmb_boot.c` on the target build. This is a property of the *whole‑system configuration*, not of WMB: `wmb_boot.c` sits outside the windowing set and WMB itself is entered through `wmb_main()` ([`90-`](90-decisions.md) D‑42, D‑43). | `nm --defined-only \| grep ' T main$'` returns exactly one, from `wmb_boot.o` |
| L6 | **Upstream trees are patched only behind macros.** Every edit is exportable and replayable. | Patch manifest, [`11-`](11-portability-plan.md) §9 |
| L7 | **The windowing set is detachable.** `jwm`, `tinyx`, `libX11`, `transport/x11`, `transport/api` reference only the platform contract, the application ABI and the runtime substrate — never the backend, never the host. | The set links against contract stubs + substrate with no backend and no `wmb_boot.o` |
| L8 | **The framework depends on nothing above it.** No file in `framework/` may include or name anything from `tinyx/`, `libX11/`, `jwm/`, `transport/` or `Xlib`. It must stay shareable by components that are not WMB. | `grep -rn '#include' framework/ \| grep -E 'tinyx\|libX11\|jwm\|transport\|Xlib\|dix'` empty |
| L9 | **`framework/` is platform‑free; every port lives in its own tree.** `framework/` holds only what compiles on *every* profile. A platform's implementation of the contract lives in a sibling port tree — `linux_hosted/` for the hosted SDL port, `<target>_native/` for an embedded target — and a port tree may depend on `framework/`, never the reverse. | `grep -rl 'SDL\|<linux/\|clock_gettime\|ucontext' framework/` empty (excluding `framework/CMakeLists.txt`) |

### 3.1 Where a file goes

L9 makes the placement question mechanical, and it is the rule that decides how much work a second platform is:

| Kind of file | Tree | Example |
|---|---|---|
| Contract header | `hal/` | `hal_display.h`, `hal_input.h`, `hal_task.h`, `hal_time.h`, `hal_alloc.h` |
| Portable runtime | `framework/fiber/`, `framework/library/` | `fiber.c`, `heap.c`, `buffer.c`, `heap_mngr.c` |
| Windowing set | `tinyx/`, `libX11/`, `jwm/`, `transport/` | — |
| **Hosted port** | `linux_hosted/<module>/` | `system_linux/hal_time_hosted.c`, `sdl_drivers/hal_display_sdl.c`, `fiber_linux/fiber_port.c` |
| **Target port** | `<target>_native/<module>/` | `system_<target>/hal_time_<target>.c`, `display_<target>/hal_display_<target>.c` |

The two port trees are **peers**, not a general case and a special case. Neither is privileged, and the contract headers in `hal/` are
identical for both. Porting to a new platform is therefore "write a sibling of `linux_hosted/`", which is a directory with a known shape and a known file
list ([`31-`](31-target-build.md) §4.6) rather than an open‑ended search through the tree for things to change.

Rule L1 is the one that decides whether this project produces something portable or something that merely *runs on SDL*. It is worth being pedantic about:
the current `tinyx/kdrive/tima/*.c` files include `hal_drivers.h`, which lives in `linux_hosted/sdl_drivers/`. That is an L3 violation today, purely by
directory placement, and fixing it is Phase 1 work.

Rule L7 is the one that decides whether the window manager can be lifted into another project later. It costs almost nothing to hold now and is expensive
to recover once something in `tinyx/` has quietly called into boot.

Rule L8 is its mirror, and the one most likely to be broken by convenience rather than by design — a WMB‑specific field in `buffer.h`, a WMB header included
from `heap.c`. Both L7 and L8 exist to make [`40-`](40-implementation-plan.md) §13's detachment procedure a file move rather than a refactor.

Rule L9 is L8 pointed downward. L8 stops the framework reaching *up* into the windowing set; L9 stops it reaching *sideways* into a platform. Both failures
look identical from inside `framework/` — a convenient `#include` that compiles — and both make the tree unportable in the same way.

## 4. Execution Model

### 4.1 One thread, many fibers

There is exactly **one** thread of execution in WMB. Concurrency is cooperative: fibers, each with its own stack, switching only at explicit yield points.

| Fiber | Stack (hosted) | Stack (target) | Role |
|---|---:|---:|---|
| `main` | (host stack) | (boot stack) | Created by `fiber_runtime_init()`; parks immediately after spawning the others |
| `hal` | 32 KB | 16 KB | Pumps the backend: drains SDL events / hardware ring into HAL buffers, advances the clock |
| `xsrv` | 256 KB | 128 KB | Runs `Dispatch()`. **Every** client request executes here, so this is the one stack that carries the deep `dix`/`mi`/`fb`/`render` chains |
| `jwm` | 64 KB | 32 KB | JWM as an internal client — its own code depth only; it posts a request and waits |
| `app_N` | 64 KB | 32 KB | One per in‑tree application — same reason |

**Exactly one stack carries the deep `fb`/`render` call chains**, because [`90-`](90-decisions.md) D‑04 executes every request on the server fiber. With
JWM plus two applications the target total is ~240 KB, against ~464 KB if each client carried that depth itself — which is what D‑45 proposed and D‑46
retired. One deep stack is also one stack to *size*: P8 measures it once instead of once per client. These numbers are starting points, validated by canary
checks (`FIBER_CANARY`) and replaced by P8 measurements.

A **safety‑net preemption tick** (default 10 ms hosted) exists so that a runaway fiber cannot lock the image. It is a backstop, not the scheduling
mechanism — correct WMB code never relies on being preempted.

### 4.2 The request round trip

```
[fiber: jwm]                                          [fiber: xsrv]
  XMapWindow(dpy, w)
    GetReq → _XGetRequest(dpy, X_MapWindow, sz)
      → bytes land in dpy->buffer
    UnlockDisplay()                (no-op)
    SyncHandle()
      → wmb_sync_handler(dpy)
          push {client, dpy->buffer, len} to server queue
          fiber_notify()
          fiber_wait_for(dispatch_done, …) ─────────►  pop request
                                                        client->requestBuffer = buf
                                                        ProcVector[X_MapWindow](client)
                                                          → MapWindow() → damage → shadow
                                                        (no reply for MapWindow)
                             ◄──────────────────────── fiber_notify()  /* in_flight drops */
  return 1
```

A **reply‑bearing** request uses the same call: the predicate simply also requires the reply slot to be populated. The server never names the client fiber
and never wakes it — release happens when the state is complete ([`21-`](21-inprocess-transport.md) §4.1).

For an **event**, the direction reverses: `hal` wakes `xsrv`, `xsrv` runs `mieqProcessInputEvents` → `DeliverEvents` → `WriteToClient`, which enqueues into
the target client's event queue; the client is parked on a predicate over that queue.

Latency: two context switches per fire‑and‑forget request. A `swapcontext` on the host costs 100–300 ns, so a request round trip is sub‑microsecond —
roughly three orders of magnitude better than the socket path, though **performance is not why we are doing this**; removing the OS dependency is.

### 4.3 Why not just call `ProcVector` directly from the client fiber?

It was tried on paper — [`90-`](90-decisions.md) D‑45 — and retired by D‑46. The reason is not the re‑entrancy usually cited. It is that **fourteen sites
in the code we compile suspend a client from inside a request handler** and rely on the dispatcher to resume it later: contended `ProcGrabServer`
(`dix/dispatch.c:1129–1134`), eight `ClientSleep` sites in `dix/dixfonts.c`, and `Xext/sync.c`, `Xext/sleepuntil.c`, `Xext/xtest.c`. A call made on the
client's own stack has nowhere to put that wait.

Second, `Dispatch()` is a per‑request *contract*, not a loop around `ProcVector`: `ReadRequestFromClient`, `client->sequence++`, the `BadLength` check,
`SendErrorToClient` when a handler fails, `isItTimeToYield`, the round robin and the smart‑schedule slice, `FlushAllOutput`. Executing on the server fiber
keeps that file **unmodified** — D‑14's preference order applied to the busiest file in the tree.

Running all server code on **one dedicated fiber** gives the dispatcher the invariant it assumes — exactly one request in flight — *structurally*, rather
than as a rule imposed on 78 kLOC of upstream code. The existing sketch in `transport/x11/wmb_io.c` does the opposite (direct call on the client stack)
and is superseded.

## 5. Build Profiles

Profiles are composed from orthogonal macros, not selected from a menu. Full definitions in [`12-platform-contract.md`](12-platform-contract.md) §3.

| Macro | Meaning |
|---|---|
| `WMB_HOSTED` | Host development build: libc, SDL, libucontext, POSIX available |
| `WMB_FREESTANDING` | Target build: no hosted libc beyond the documented floor |
| `WMB_HAS_OS_TRANSPORT` | Platform capability only: the OS provides sockets/`select`, used by the P8 `wmb_api_t` bridge. Never X11 protocol |
| `WMB_INPROC` | Always on. The in‑process path is TinyX's only X11 transport ([`90-`](90-decisions.md) D‑49) |

Four sanctioned combinations:

| Profile | `WMB_HOSTED` | `WMB_HAS_OS_TRANSPORT` | Purpose |
|---|---|---|---|
| `host-pure` | ✓ | ✗ | **The important one.** Hosted build with zero sockets — proves the target code path on a machine with a debugger |
| `host-bridge` | ✓ | ✓ | P8 only. Adds a host socket carrying framed `wmb_api_t` calls for `wmb_hello` ([`22-`](22-entry-point-abi.md) §6) |
| `target` | ✗ (`WMB_FREESTANDING`) | ✗ | Production embedded image |
| `host-unit` | ✓ | ✗ | Unit tests of the runtime and contract, no X server |

**`host-pure` is the profile that matters.** It is the same source configuration as `target`, running on a machine where GDB and Valgrind work. Every
capability gate in [`41-verification-plan.md`](41-verification-plan.md) is defined against it.

**There is no stock‑client oracle profile.** `host-dev` and TinyX's socket listener were retired with D‑49: no build accepts connections from unmodified X
binaries, so `xeyes`/`xclock`/`xterm` play no part in verification. The cost is that server assembly and transport replacement are no longer provable
independently — see [`40-`](40-implementation-plan.md) §5.3 for how P4 and P5 are separated instead.

## 6. The Two Client Categories

WMB serves clients in exactly two ways. Anything else is a development scaffold.

### 6.1 Internal clients — in‑tree, same image

JWM, `wmb_test_client`, and any application compiled into WMB. They:

- Run on their own fiber, created by `wmb_boot.c` or by JWM's launch table.
- Call `XOpenDisplay(NULL)`, which reaches `wmb_open.c`, which registers a `ClientRec` with TinyX and installs `wmb_sync_handler`.
- Never see a file descriptor. `ConnectionNumber(dpy)` is `-1`.

This is the **primary path** and the one all testing targets.

### 6.2 External clients — separate binary, fixed‑address vtable

On the target, an application loaded by the platform is a separate image. It cannot link libX11 (that would duplicate ~34 kLOC per app and produce two
independent `Display` universes). Instead it links a **thin wrapper library** whose every function is one line:

```c
int XMapWindow(Display *dpy, Window w) { return WMB_API->XMapWindow(dpy, w); }
```

`WMB_API` is a pointer to a `wmb_api_t` structure at a build‑time‑fixed address inside the WMB image. The wrapper library is a few tens of kilobytes and
contains no protocol logic at all. Full specification: [`22-entry-point-abi.md`](22-entry-point-abi.md).

On the host, external clients are simulated by a socket bridge (§7) purely so the ABI can be exercised before target hardware exists.

## 7. Development Scaffolding (compiled out on target)

One scaffold remains:

| Scaffold | Profile | What it does | Why it exists | Removed by |
|---|---|---|---|---|
| **WMB bridge** | `host-bridge` | A socket carrying a trivial length‑prefixed framing of `wmb_api_t` calls, so a separate host process can exercise the external ABI | Validates C7 before there is a target loader | `WMB_HAS_OS_TRANSPORT` off |

TinyX's X11 socket listener was the second scaffold and is **retired** ([`90-`](90-decisions.md) D‑49). No profile accepts a connection from an unmodified X
binary, so `xeyes`, `xclock` and `xterm` are not part of this project at any phase. Every client is either in‑tree or built against the wrapper library.

The bridge is **explicitly not** the architecture. It is a ladder that gets kicked away. The rule from [`00-`](00-introduction.md) §3.4 applies: a capability
that has only been demonstrated over a bridge has not been demonstrated.

## 8. Data Flow Summary

### 8.1 Client → server (request)

```
 Xlib API fn → dpy->buffer → SyncHandle → wmb_sync_handler
   → enqueue on server request queue → wake xsrv → yield
     → xsrv: ReadRequestFromClient (io_wmb.c) points requestBuffer at the buffer
       → Dispatch → ProcVector[op](client)
```

### 8.2 Server → client (reply)

```
 Proc*Handler → WriteToClient (io_wmb.c)
   → append into the client's reply slot (no wake — see 21- §5.5)
     → release happens once, when ReadRequestFromClient sees in_flight drop
       → the client's _XReply predicate is satisfied and it returns the bytes
```

### 8.3 Server → client (event)

```
 hal fiber: drain backend → HAL input buffers → wake xsrv
   → xsrv BlockHandler chain → tima_OsPollEvents → tima_{Keyboard,Mouse}Poll
     → KdEnqueue{Keyboard,Mouse}Event → mieqEnqueue
       → WakeupHandler → mieqProcessInputEvents → DeliverEvents
         → WriteEventsToClient → WriteToClient (io_wmb.c)
           → enqueue in target client's event queue
             → that client's XNextEvent predicate is satisfied and it returns
```

### 8.4 Client → display (pixels)

```
 ProcPolyFillRectangle → GC ops → fb/ rendering into the shadow buffer
   → miext/damage records the region
     → shadowUpdate (via BlockHandler) → tima_kdrive_fb shadow callback
       → hal_display_flush(fb, x, y, w, h)
         → hosted: SDL_UpdateTexture + SDL_RenderPresent
         → target: DMA / MMIO blit
```

## 9. What Is Deliberately Absent

| Absent | Because |
|---|---|
| Sockets, `select`, `poll` | The entire point |
| Threads, mutexes, atomics in application logic | Single cooperative thread; the only `pthread` is the hosted timer tick |
| `fork`, `exec`, process management | One image. JWM's "run command" becomes a table lookup |
| Signals | No asynchronous OS notification exists on target |
| Authentication, access control, XDMCP | No untrusted peer exists |
| Dynamic loading (`dlopen`) | Static link, non‑PIC, fixed address |
| Locale, XIM, Xrm | Out of scope |
| `getenv` | One call site (`dix/main.c:250`); replaced by a compile‑time constant |
| Filesystem | Not required. Fonts and colours are built in. Supported as an optional capability when a target has storage — see [`90-`](90-decisions.md) D‑34 |
| Hardware acceleration | Framebuffer only |

## 10. Memory Layout (target)

Indicative, to be finalised in [`31-target-build.md`](31-target-build.md) §7.

```
   ┌──────────────────────────┐  ← WMB load base (fixed, non-PIC)
   │ .wmb_api  (vtable)       │     ← the external entry point; first section by design
   ├──────────────────────────┤
   │ .text     WMB code       │
   ├──────────────────────────┤
   │ .rodata   tables, fonts  │
   ├──────────────────────────┤
   │ .data / .bss             │
   ├──────────────────────────┤
   │ fiber stacks (static)    │
   ├──────────────────────────┤
   │ heap region              │     ← X server resources, pixmaps, GCs
   ├──────────────────────────┤
   │ shadow framebuffer       │     ← W × H × bpp/8
   └──────────────────────────┘
   ┌──────────────────────────┐
   │ hardware framebuffer     │     ← may be outside the WMB region
   └──────────────────────────┘
```

Placing `.wmb_api` at offset 0 of the image means an external application needs to know exactly one number — the load base — rather than a symbol table.

## 11. Traceability

How this architecture satisfies the four things the project was asked to deliver.

| Requirement | Satisfied by |
|---|---|
| **List all necessary components** | [`01-component-inventory.md`](01-component-inventory.md); mapped onto layers C1–C8 in §2 above |
| **Detach Linux dependency, portable, raw compilation with no OS** | Layering rules L1–L4 (§3), the platform contract (§2, [`12-`](12-platform-contract.md)), the `target` profile (§5), and the census+plan ([`10-`](10-os-dependency-census.md), [`11-`](11-portability-plan.md)) |
| **Remove network transport between X11 client and server** | §4.2, §8.1–§8.3, and [`20-`](20-transport-inventory.md) / [`21-`](21-inprocess-transport.md) |
| **Host build with SDL first, embedded after** | §5 profiles (`host-pure` before `target`), [`30-`](30-host-build.md), [`31-`](31-target-build.md), and the phase ordering in [`40-`](40-implementation-plan.md) |
