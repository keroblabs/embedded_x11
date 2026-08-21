# 31 — Target Build (Freestanding)

## 1. Purpose

This document specifies **the embedded target build**: what "no OS dependency" means concretely, what the platform must supply, how the image is laid out,
and how the claim is proven. It is the second deliverable of workstream 4 ([`00-introduction.md`](00-introduction.md) §5.4).

**It is deliberately the last thing attempted.** Every capability is developed and gated on the host first ([`30-host-build.md`](30-host-build.md)); the
target build is a *port of a working system*, not a bring‑up of an unknown one. Attempting it earlier converts every ordinary bug into a cross‑compilation
mystery.

### 1.1 What this document is not

- **Not a hardware specification.** The target platform is characterised by its contract, not its identity ([`00-`](00-introduction.md) §4.3). No chip is
  named.
- **Not the host build.** [`30-`](30-host-build.md).
- **Not the portability plan.** How dependencies are severed is [`11-portability-plan.md`](11-portability-plan.md); this document states what remains.

## 2. What "No OS Dependency" Means

Precisely three things:

1. **The C library is freestanding plus a documented floor.** Compiled with `-ffreestanding`. Only the functions in §3.1 may be referenced.
2. **Exactly four platform primitives are required.** §3.2. Everything else WMB needs, WMB brings.
3. **No syscall, no kernel interface, no driver model, no filesystem, no process, no thread, no signal.**

The target has no RTOS that WMB uses. WMB is a **library with an entry point**, not an application on an operating system: its own concurrency is
`framework/fiber/` and nothing else (§5.3). It does not assume it is the only component in the image — see [`90-`](90-decisions.md) D‑42 for the
boundaries that keep that true and D‑44 for how it coexists with whatever else the system runs.

## 3. The Floor

### 3.1 libc floor

Referenced by upstream code and therefore mandatory. Satisfiable by newlib‑nano, picolibc, or ~600 lines of hand‑written code.

| Header | Functions |
|---|---|
| `<string.h>` | `memcpy` `memmove` `memset` `memcmp` `strlen` `strcpy` `strncpy` `strcmp` `strncmp` `strcat` `strncat` `strchr` `strrchr` `strstr` `strdup` |
| `<stdlib.h>` | `malloc` `calloc` `realloc` `free` `abort` `atoi` `strtol` `strtoul` `qsort` `bsearch` `abs` |
| `<stdio.h>` | `snprintf` `vsnprintf` `sprintf` |
| `<ctype.h>` | `isalpha` `isdigit` `isspace` `isprint` `isxdigit` `toupper` `tolower` |
| `<setjmp.h>` | `setjmp` `longjmp` — mandatory; `FatalError` unwinds to a recovery point ([`90-`](90-decisions.md) D‑32) |
| `<math.h>` | `sin` `cos` `sqrt` `floor` `ceil` — RENDER only |
| freestanding | `<stdint.h>` `<stddef.h>` `<stdbool.h>` `<stdarg.h>` `<limits.h>` `<float.h>` |

The exact list is a **Q6 deliverable** ([`11-`](11-portability-plan.md) §9.3), generated from the actual link and checked in as `docs/floor-symbols.txt`.
§3.1 is the starting hypothesis; the freestanding compile refines it. Expecting it to be exactly right on the first attempt is the mistake this phase is
structured to avoid — [`91-`](91-open-questions.md) Q18 lists the five categories it is most likely to be missing, of which the compiler runtime
(`libgcc` / `compiler-rt`) is the one no source grep can find.

The libc itself is **newlib‑nano** for the first target build, with a hand‑written floor as a later size optimisation ([`90-`](90-decisions.md) D‑36).

`malloc`/`free`/`realloc`/`calloc` are listed as libc but are **implemented by WMB** (`framework/library/heap.c`) on top of `wmb_os_sbrk`. They appear here
because upstream code calls them by those names.

### 3.2 Platform primitives — four functions

| Function | Contract | Typical implementation |
|---|---|---|
| `wmb_os_sbrk(intptr_t)` | Grow the heap; return the previous break or `(void*)-1` | Bump pointer over a linker‑reserved region |
| `wmb_os_console_write(const char*, int)` | Emit diagnostics; must work before display init | UART TX, or semihosting |
| `hal_tick_ms(void)` | Monotonic milliseconds | A free‑running hardware counter |
| `wmb_os_abort(void)` | Terminate; never returns | Halt, or reset |

That is the entire mandatory syscall surface. Everything else in the platform contract ([`12-platform-contract.md`](12-platform-contract.md)) is display,
input and task scheduling, which are hardware, not OS.

### 3.3 Explicitly not required

| Not required | Consequence for WMB |
|---|---|
| Filesystem | Not required, but **supported when present** ([`90-`](90-decisions.md) D‑34). With `WMB_HAS_FILESYSTEM` off, colours come from `tinyx/os/oscolor.c`'s built‑in table and fonts are built in (§7) |
| Environment variables | One call site removed |
| Processes, `fork`, `exec` | JWM's launch table replaces them |
| Signals | Nothing in WMB is asynchronous except input, which is polled |
| Threads | Fibers |
| Sockets, network stack | The whole point |
| MMU / virtual memory | `wmb_os_sbrk` over a physical region; fiber stacks are statically reserved |
| Dynamic linking | Static, non‑PIC |
| Wall‑clock time | Not required. `hal_wallclock_s()` is available behind `WMB_HAS_WALLCLOCK` for a target with an RTC, and no timing logic may use it ([`12-`](12-platform-contract.md) §7.2) |

## 4. Platform Backend

What must be written for a specific target. Roughly **600–900 LOC total**, in a `<target>_native/` tree that is a peer of `linux_hosted/`
([`03-`](03-target-architecture.md) §3.1, rule L9). Nothing in `framework/` changes.

### 4.1 Display

```c
/* <target>_native/display_<target>/hal_display_<target>.c */

static hal_framebuffer_t fb;

int hal_display_init(uint32_t w, uint32_t h, hal_pixfmt_t fmt)
{
    if (w != WMB_SCREEN_WIDTH || h != WMB_SCREEN_HEIGHT) return -1;
    display_controller_configure(w, h, fmt);
    fb.pixels = (void *)WMB_FRAMEBUFFER_BASE;
    fb.width  = w;  fb.height = h;
    fb.stride = w * (fmt == HAL_PIXFMT_RGB565 ? 2 : 4);
    fb.bpp    = (fmt == HAL_PIXFMT_RGB565 ? 16 : 32);
    fb.format = fmt;
    return 0;
}

void hal_display_flush(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    /* If `pixels` IS the scanout buffer this is a no-op or a cache
     * writeback. If it is a shadow, queue a DMA descriptor. */
    cache_clean_range(row_addr(y), h * fb.stride);
}
```

Two arrangements, both supported:

| Arrangement | `hal_display_flush` |
|---|---|
| `fb.pixels` is the scanout buffer | Cache writeback only |
| `fb.pixels` is a shadow, scanout is elsewhere | DMA blit of the damaged rows |

**The target is double buffered** ([`90-`](90-decisions.md) D‑35): `fb.pixels` is a back buffer and `hal_display_flush` presents it — a page flip at the
next vertical blank where the controller supports one, a DMA blit of the damaged rows where it does not. The single‑buffer arrangement remains in the code
because the API supports it, but it is not the configuration WMB ships: it guarantees that every intermediate state of an X repaint is visible.

Cost: $W \times H \times \text{bpp}/8$ a second time — 750 KB at 800×480×16 — which is a line in §8's budget, not a contingency.

### 4.2 Input

Interrupt‑driven producer, polled consumer:

```c
/* ISR context — the only asynchronous code in WMB */
void keyboard_irq_handler(void)
{
    uint8_t raw = keyboard_read_reg();
    ring_push(&kbd_ring, HAL_CMD_KEY_DOWN);      /* command byte */
    ring_push(&kbd_ring, translate(raw));        /* data byte    */
}

/* fiber context */
bool hal_input_has_data(hal_input_dev_t dev) { return !ring_empty(ring_for(dev)); }
bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out) { return ring_pop(ring_for(dev), out); }
void hal_input_pump(void) { /* nothing to do — the ISR fills the rings */ }
```

**The ring buffer is the only structure touched from two contexts.** It must be a single‑producer/single‑consumer lock‑free ring with power‑of‑two
capacity, and it must push the command and data bytes atomically with respect to the consumer — a reader that sees a command without its data
desynchronises permanently ([`12-`](12-platform-contract.md) §5.3). The simplest correct approach is to write both bytes, then publish the head index with
a release store.

### 4.3 Time

```c
uint32_t hal_tick_ms(void) { return (uint32_t)(hw_counter_read() / TICKS_PER_MS); }
uint64_t hal_tick_us(void) { return hw_counter_read() / TICKS_PER_US; }
```

Must be monotonic across the counter's hardware wrap. If the hardware counter is narrower than 32 bits, an ISR must extend it.

### 4.4 Console and termination

```c
int  wmb_os_console_write(const char *buf, int len) { return uart_write(buf, len); }
void wmb_os_abort(void) { uart_write("WMB ABORT\n", 10); for (;;) __asm__ volatile("wfi"); }
void wmb_os_exit(int c) { (void)c; wmb_os_abort(); }
```

### 4.5 Heap

```c
extern char __heap_start[], __heap_end[];      /* from the linker script */
static char *brk = __heap_start;

void *wmb_os_sbrk(intptr_t inc)
{
    char *prev = brk;
    if (inc < 0) { if (brk + inc < __heap_start) return (void *)-1; }
    else         { if (brk + inc > __heap_end)   return (void *)-1; }
    brk += inc;
    return prev;
}
```

## 4.6 Port Checklist

A port is a directory tree, not a set of edits scattered through the codebase. `linux_hosted/` is the worked example; `<target>_native/` is its peer. Both
implement the same headers from `framework/library/`, and neither is privileged.

### 4.6.1 Tree shape

```
<target>_native/
├── system_<target>/            hal_time_<target>.c      → hal_time.h
│                               wmb_os_<target>.c        → wmb_os.h
│                               system_<target>.cmake
├── display_<target>/           hal_display_<target>.c   → hal_display.h
│                               display_<target>.cmake
├── input_<target>/             hal_input_<target>.c     → hal_input.h
│                               input_<target>.cmake
└── fiber_<target>/             fiber_port.c + <arch>.S  → framework/fiber/fiber_port.h
                                fiber_<target>.cmake
```

Each `*.cmake` follows the convention already in use (`linux_hosted/system_linux/system_linux.cmake`): export a `SOURCE_FILES_<MODULE>` list, a
`<MODULE>_INCLUDE_PATH`, and any `<MODULE>_DEFINES` the module needs. A build selects a platform by including one set of these fragments instead of the
other; no source file changes.

### 4.6.2 Symbols a port must define

Seventeen contract functions plus five port symbols. Nothing else — if a port needs to define anything outside this list, either the list is wrong or the
port is doing work that belongs above the contract.

| Header | Symbols | Notes |
|---|---|---|
| `hal_display.h` | `hal_display_init` `hal_display_shutdown` `hal_display_get_fb` `hal_display_flush` `hal_display_set_power` | §4.1. `set_power` may return −1 |
| `hal_input.h` | `hal_input_init` `hal_input_shutdown` `hal_input_has_data` `hal_input_read_byte` `hal_input_control` `hal_input_pump` | §4.2 |
| `hal_time.h` | `hal_tick_ms` `hal_tick_us` (+ `hal_wallclock_s` if `WMB_HAS_WALLCLOCK`) | §4.3 |
| `wmb_os.h` | `wmb_os_console_write` `wmb_os_abort` `wmb_os_exit` `wmb_os_sbrk` (+5 filesystem if `WMB_HAS_FILESYSTEM`) | §4.4, §4.5 |
| `fiber_port.h` | `fiber_port_init` `fiber_port_init_stack` `fiber_port_request_switch` `fiber_port_irq_lock` `fiber_port_irq_unlock` | §5.1 |

**`hal_task_*` is not on the list.** WMB implements all nine itself, in `framework/fiber/`, on every profile ([`90-`](90-decisions.md) D‑31). A port
supplies `fiber_port.h`'s five symbols and gets the scheduler for free.

### 4.6.3 What a port must not do

| Prohibition | Why |
|---|---|
| Add a file under `framework/` | Rule L9. `framework/` compiles identically on every profile |
| Add a function to a `hal_*.h` header | The contract is closed; extending it is a decision ([`90-`](90-decisions.md)) |
| Expose a platform type across the contract | PC1 — no `fd`, no `SDL_Window*`, no `struct timespec` |
| Call up into `tinyx/`, `libX11/`, `jwm/` | Rule L1; a port sits strictly below the contract |
| Block anywhere except inside `hal_yield_timeout` | Rule L2 / PC3 |
| Deliver input by calling into WMB | PC2 — WMB polls; the ISR only fills a ring |

### 4.6.4 Bring-up order

The order matters: each step is testable with only the steps before it, so a failure is attributable.

| # | Step | Testable by |
|---|---|---|
| 1 | `wmb_os_console_write`, `wmb_os_abort` | A "hello" over UART before anything else exists |
| 2 | `wmb_os_sbrk` + `framework/library/heap.c` | Allocate and free 1,000 blocks; check the high-water mark |
| 3 | `hal_time_<target>.c` | `hal_tick_ms` monotonic over 60 s (gate V‑P3‑4) |
| 4 | `fiber_<target>/` | Two fibers ping-pong 100,000 times (gate V‑P2‑1) |
| 5 | `hal_display_<target>.c` | A gradient written to `hal_display_get_fb()->pixels`, then `hal_display_flush` (gate V‑P1‑3) |
| 6 | `hal_input_<target>.c` | Key press produces the expected `HAL_CMD_*` byte pair (gate V‑P1‑5) |
| 7 | Whole image | `wmb_main()`; JWM root window; `wmb_test_client` |

Steps 1–5 are exactly what `linux_hosted/` already does, which is why the hosted port is worth keeping honest: it is the reference implementation a target
port is checked against.

## 5. Fiber Port

### 5.1 The contract

`framework/fiber/fiber_port.h`, already defined and already implemented once (hosted). The target port is ~150 LOC of C plus ~60 lines of assembly.

| Symbol | Implementation |
|---|---|
| `fiber_port_init(period_ms)` | Configure the periodic timer; enable its interrupt at the lowest priority |
| `fiber_port_init_stack(top, entry, arg)` | Forge a register frame so the first dispatch lands in `fiber_trampoline` → `entry(arg)` |
| `fiber_port_request_switch()` | Pend a low‑priority software interrupt (`PendSV` on ARMv6‑M/v7‑M, `MSIP` on RISC‑V) |
| `fiber_port_irq_lock()` / `unlock(prev)` | `cpsid i` / `csrrc mstatus, MIE`, returning the previous state for nesting |
| `fiber_switch.S` | Save the callee‑saved frame, call `fiber_sched_swap(sp)`, restore |

`FIBER_PORT_STACK_ALIGN` is already parameterised in `fiber_port.h` (16 on RISC‑V, 8 on ARM).

### 5.2 Static stacks

No `mmap` on target. `hal_task_create` uses `fiber_create_static()` with stacks from a statically reserved pool:

```c
static uint8_t stack_hal [WMB_STACK_HAL ] __attribute__((aligned(16)));
static uint8_t stack_xsrv[WMB_STACK_XSRV] __attribute__((aligned(16)));
static uint8_t stack_jwm [WMB_STACK_JWM ] __attribute__((aligned(16)));
static uint8_t stack_app [WMB_APP_MAX][WMB_STACK_APP] __attribute__((aligned(16)));
```

Overflow detection is `FIBER_CANARY` checked on every switch — cheaper than a guard page and available without an MMU. It detects overflow *after* the fact
rather than at the moment of the offending store, which is why stack sizing is validated on the host (where guard pages trap precisely) before the target
build.

### 5.3 WMB brings its own scheduling

`framework/fiber/` schedules every fiber WMB owns ([`90-`](90-decisions.md) D‑31). WMB does not use an RTOS and does not ask the platform for a scheduler,
so task management is **not part of what a port supplies** — §5.1's five symbols are the whole of it.

In the current whole‑system configuration `wmb_boot.c`'s `main()` is the entry point, `fiber_runtime_init()` takes the CPU, and the root fiber is the idle
task, free to `WFI` when the ready list is empty. That is a property of the configuration, not of WMB (D‑42): a different host may own the CPU and give
WMB time instead.

What the fiber core guarantees, and what the rest of WMB is consequently allowed to assume:

| # | Invariant | Depended on by |
|---|---|---|
| 1 | No implicit preemption between yields; the safety tick only breaks a fiber that never yields | D‑04's "one request in flight", and every unguarded upstream data structure |
| 2 | Block/wake with no lost wakeups — a wake issued before its wait is held pending | The transport's suspend/resume protocol ([`21-`](21-inprocess-transport.md) §4) |
| 3 | Per‑task stacks of the sizes in [`03-`](03-target-architecture.md) §4.1, statically reserved (§5.2) | `fb` and `render` call depth |
| 4 | No WMB code need be re‑entrant | 78 kLOC of upstream X server code |

Invariant 1 is the load‑bearing one: it is why D‑10 rejected threads, and it is the first property that would be lost if WMB's own task management were
delegated outward. The four are stated as invariants rather than as requirements on a platform primitive because there is no platform primitive — they are
properties `framework/fiber/` must preserve in every arch port.

Other components may share the runtime, and inherit invariant 1 as an **obligation**: a component that computes for 200 ms without yielding freezes the UI
for 200 ms. WMB owes the same obligation in return — it yields as soon as there is no work for that moment ([`90-`](90-decisions.md) D‑44).

## 6. Cross‑Compilation

### 6.1 Toolchain file

```cmake
# cmake/toolchain-<target>.cmake
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  <arch>)
set(CMAKE_C_COMPILER        <triple>-gcc)
set(CMAKE_C_COMPILER_WORKS  1)

set(WMB_TARGET_FLAGS "-ffreestanding -fno-builtin -fno-pic -fno-pie \
                      -fno-common -fno-stack-protector -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_INIT      "${WMB_TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -no-pie -Wl,--gc-sections")
```

```
cmake -B build-target -DWMB_PROFILE=target \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-<target>.cmake \
      -DWMB_LOAD_BASE=0x80100000
```

### 6.2 Compiler flags of consequence

| Flag | Why |
|---|---|
| `-ffreestanding` | No hosted libc assumptions; `main` is not special |
| `-fno-builtin` | Prevents GCC synthesising calls to `memcpy`/`memset` we have not provided — a classic freestanding trap |
| `-fno-pic -fno-pie -no-pie` | Fixed load address ([`22-`](22-entry-point-abi.md) §4.3) |
| `-fno-common` | Tentative definitions become errors rather than silently merging |
| `-ffunction-sections -fdata-sections -Wl,--gc-sections` | Discard the unreachable half of `miext/shadow` and the unused `fb` variants |
| `-nostdlib` | We supply the entry point and the runtime |

`--gc-sections` matters more than usual here: `tinyx/miext/shadow/` contains 20 rotation blitters of which two are reachable at any given bpp, and
`tinyx/fb/` contains many macro‑generated variants. Section GC removes them without any source change.

## 7. Built‑In Resources

Because the target profile runs with `WMB_HAS_FILESYSTEM` off ([`90-`](90-decisions.md) D‑34), three things must be compiled in.

| Resource | Normally | On target |
|---|---|---|
| Colour names (`rgb.txt`) | `/etc/X11/rgb.txt` | `tinyx/os/oscolor.c`'s built‑in table — **already there**, 1,598 LOC, no work required |
| Error strings (`XErrorDB`) | `/etc/X11/XErrorDB` | Generic strings from `libX11/src/XErrDes.c`'s fallback path |
| Fonts | `/usr/share/fonts/**` | Built‑in bitmap fonts, converted to C arrays at build time |

The fonts are the only real work. The shipped set is **several** bitmap fonts at different sizes (~150 KB `.rodata`), converted from BDF to C arrays by a
build‑time script and registered from a table at init — enough for a real UI rather than merely legible ([`90-`](90-decisions.md) D‑38). The classic 6×13
`fixed` is one of them, so that `XLoadQueryFont("fixed")` and JWM's default font both resolve.

The registry shape matters more than the font count: because fonts are registered from a table, adding a scalable backend later is an addition to that
table, not a rewrite of the glyph path. `fonts/freetype/` stays dormant in the tree — scalable fonts on target are a later project, not an abandoned one.

## 8. Memory Layout

```
   ┌────────────────────────────────┐  ← WMB_LOAD_BASE
   │ .wmb_api      (vtable)         │     external entry, offset 0
   ├────────────────────────────────┤
   │ .text                          │     ~1.5–2.5 MB estimated
   ├────────────────────────────────┤
   │ .rodata  (colour table, font,  │     ~200 KB
   │           protocol tables)     │
   ├────────────────────────────────┤
   │ .data                          │
   ├────────────────────────────────┤
   │ .bss                           │
   ├────────────────────────────────┤
   │ fiber stacks (§5.2)            │     ~300 KB
   ├────────────────────────────────┤  ← __heap_start
   │ heap                           │     ≥ 4 MB (§8.1)
   ├────────────────────────────────┤  ← __heap_end
   │ shadow framebuffer (optional)  │     W×H×bpp/8
   └────────────────────────────────┘
```

### 8.1 Heap sizing

The X server allocates per‑resource: windows, pixmaps, GCs, colormaps, fonts, client structures. Indicative:

| Consumer | Estimate |
|---|---|
| Server base structures, atoms, extension state | ~500 KB |
| Per client (`ClientRec`, resource table) | ~16 KB × 4 |
| Per window (`WindowRec`, `WindowOptRec`, borders) | ~1 KB × 50 |
| Backing pixmaps for JWM decorations | $W \times H \times \text{bpp}/8$ per off‑screen surface |
| libX11 per‑`Display` (`Display`, buffer, event queue, XID table) | ~64 KB × 4 |
| **Total working set** | **~2 MB, headroom to 4 MB** |

These are estimates and must be replaced by measurements from the host build's one‑hour stability run (gate H‑V9). The measurement, not the estimate, sizes
the target heap.

### 8.2 Image size

`.text` is dominated by `tinyx/fb/` (14.4 kLOC of macro‑expanded rendering, which expands badly) and `tinyx/mi/` (23.5 kLOC). A realistic first build will
be 2–3 MB before `--gc-sections` and `-Os`. If that is too large, the reduction levers in order of value are:

1. `-Os` instead of `-O2` (typically 25–30 % on this kind of code)
2. `--gc-sections` (removes the unreachable shadow blitters and `fb` variants)
3. Drop RENDER if nothing uses it (7,962 LOC — but `fbpict.c` and JWM may)
4. Drop RANDR if `kdrive.c`'s references can be stubbed (5,217 LOC)
5. Drop the `mi` cursor/sprite path if the hardware has a cursor overlay

Levers 3–5 are behavioural changes and each needs a decision entry.

## 9. Boot Sequence

```c
/* linux_hosted/wmb_boot/wmb_boot.c — this configuration's host; see D-42 */
int main(void)
{
    platform_early_init();                 /* clocks, UART, memory controller */
    wmb_os_console_write("WMB boot\n", 9);

    heap_init(__heap_start, __heap_end);
    hal_task_runtime_init(WMB_SAFETY_TICK_MS);

    if (wmb_main(NULL) != 0)               /* returns once WMB is up: D-43 */
        wmb_os_abort();

    for (;;) hal_task_yield();             /* the root fiber parks here */
}
```

On a target that owns its own `main()`, the same call appears wherever the system's dispatcher brings components up. WMB is one component among several
and the runtime already exists — this is the arrangement the target build expects ([`90-`](90-decisions.md) D‑44):

```c
/* the system's dispatcher, which WMB does not own and knows nothing about */
void system_start(void)
{
    board_init();
    heap_init(__heap_start, __heap_end);
    hal_task_runtime_init(0);              /* host bounds component runtime */

    component_start(&comm_stack);
    component_start(&telemetry);

    if (wmb_main(&wmb_cfg) != 0)           /* returns once WMB is up */
        fault("WMB failed to start");

    dispatcher_run();                      /* never returns */
}
```

WMB's fibers are now in the dispatcher's ready list alongside everything else's. WMB yields whenever there is no work for that moment, tolerates being
suspended for an unbounded interval, and never assumes it owns the CPU.

and inside the windowing set:

```c
/* transport/x11/wmb_main.c */
int wmb_main(const wmb_config_t *cfg)
{
    if (!cfg) cfg = &wmb_default_config;
    if (hal_display_init(cfg->width, cfg->height, cfg->pixfmt) != 0) return -1;
    if (hal_input_init() != 0) return -2;

    /* Created blocked; none of these runs yet. */
    wmb.hal  = hal_task_create("hal",  Hal_PumpFiber,     NULL, cfg->stack_hal);
    wmb.xsrv = hal_task_create("xsrv", TinyX_ServerFiber, NULL, cfg->stack_xsrv);
    wmb.jwm  = hal_task_create("jwm",  Jwm_FiberEntry,    NULL, cfg->stack_jwm);
    if (!wmb.hal || !wmb.xsrv || !wmb.jwm) return -3;

    hal_task_wake(wmb.hal);
    hal_task_wake(wmb.xsrv);
    while (!wmb_server_ready)              /* the "block until ready" step */
        hal_task_yield();

    hal_task_wake(wmb.jwm);                /* clients only now */
    return 0;
}
```

Ordering constraints:

| # | Constraint | Owned by |
|---|---|---|
| 1 | Console before anything that can fail — it is the only diagnostic channel | Host |
| 2 | Heap before the runtime (fiber control blocks may be heap‑allocated) | Host |
| 3 | Runtime before `wmb_main()` — it calls `hal_task_create` | Host |
| 4 | Display and input before the server fiber — `InitOutput` calls `hal_display_get_fb()` | WMB |
| 5 | **All WMB fibers created before any of them runs; clients released only once the server reports ready** | WMB |
| 6 | The root fiber must never return | Host |

That split *is* the lifecycle boundary ([`90-`](90-decisions.md) D‑42): a different host performs 1–3 and 6 its own way and calls the same function.

Constraint 5 is the whole of the readiness story ([`90-`](90-decisions.md) D‑33, mechanism in D‑43). Creation order is *not* execution order, so ordering
the `hal_task_create` calls proves nothing on its own; what makes JWM's `XOpenDisplay` safe is that no client fiber is runnable until the server fiber has
finished `InitOutput` and `InitInput`. Keeping the gate inside `wmb_main()` rather than in `wmb_open.c` means the protocol exists once, instead of every
future client reimplementing it.

## 10. Verification

| # | Gate | How |
|---|---|---|
| T‑V1 | Every TU compiles with `-ffreestanding -fno-builtin` | `cmake --build build-target` |
| T‑V2 | The link references nothing outside `docs/floor-symbols.txt` | `comm -23 <(nm --undefined-only …) docs/floor-symbols.txt` empty |
| T‑V3 | No forbidden symbol | `nm --undefined-only wmb.elf \| grep -E 'socket\|select\|poll\|fork\|exec\|sigaction\|getenv\|fopen\|dlopen\|pthread_'` empty |
| T‑V4 | Exactly one `main` | `nm --defined-only wmb.elf \| grep ' T main$' \| wc -l` = 1 |
| T‑V5 | No dynamic relocations | `readelf -r wmb.elf` shows none |
| T‑V6 | `.wmb_api` at offset 0 and equal to `WMB_LOAD_BASE` | `readelf -S wmb.elf` |
| T‑V7 | Image fits the memory budget | `size wmb.elf` |
| T‑V8 | Boot to a visible root window on hardware | Manual |
| T‑V9 | Keyboard and mouse reach a client on hardware | `wmb_test_client` on target |
| T‑V10 | One‑hour run: flat heap high‑water mark, no canary trip | Instrumented |

T‑V2 and T‑V3 are the gates that make the project's central claim true. They are cheap, mechanical, and should be wired into CI from the first successful
target link.

## 11. Deferred Decisions

| # | Item | Tracked as |
|---|---|---|
| D1 | Which target, and therefore the real values of `WMB_LOAD_BASE`, `WMB_FRAMEBUFFER_BASE`, heap size, screen geometry | [`91-`](91-open-questions.md) Q6 |
| D2 | Fiber port vs platform scheduler (§5.3) | **Closed** — WMB brings its own scheduling; no RTOS is used ([`90-`](90-decisions.md) D‑31). Coexistence with other components is D‑44 |
| D3 | Shadow framebuffer vs direct scanout (§4.1) | **Closed** — double buffered ([`90-`](90-decisions.md) D‑35) |
| D4 | Which libc | **Closed** — newlib‑nano first, hand‑written later ([`90-`](90-decisions.md) D‑36), pending toolchain availability under Q6 |
| D5 | Font strategy | **Closed** — several built‑in bitmap fonts, FreeType later ([`90-`](90-decisions.md) D‑38) |
| D6 | Whether XKB is compiled in | **Closed** — no ([`90-`](90-decisions.md) D‑39) |

Only D1 remains, and it is an external input. Every target‑side decision that could be taken without knowing the hardware has been.
