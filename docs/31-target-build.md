# 31 — Target Build (Freestanding)

## 1. Purpose

This document specifies **the embedded target build**: what "no OS dependency" means concretely, what the platform must supply, how the image is laid out,
and how the claim is proven. It is the second deliverable of workstream 4 ([`00-introduction.md`](00-introduction.md) §5.4).

**It is deliberately the last thing attempted.** Every capability is developed and gated on the host first ([`30-host-build.md`](30-host-build.md)); the
target build is a *port of a working system*, not a bring‑up of an unknown one. Attempting it earlier converts every ordinary bug into a cross‑compilation
mystery.

**Status: P9a is verified independently of P9b.** The approved reference uses
Arm GNU 14.2, Cortex-M3/Thumb ILP32, full newlib and a deliberately synthetic
memory map. Both Release and Debug compile/link checks pass. No board,
hardware execution or production allocator/locking policy is certified.
[The P9a guide](../freestanding/README.md) is the implemented command reference.

### 1.1 What this document is not

- **Not a hardware specification.** The target platform is characterised by its contract, not its identity ([`00-`](00-introduction.md) §4.3). No chip is
  named.
- **Not the host build.** [`30-`](30-host-build.md).
- **Not the portability plan.** How dependencies are severed is [`11-portability-plan.md`](11-portability-plan.md); this document states what remains.

## 2. What "No OS Dependency" Means

Precisely three things:

1. **The C library is freestanding plus a documented floor.** Compiled with `-ffreestanding`. Only the functions in §3.1 may be referenced.
2. **Local console/arena/termination glue plus the device/fiber contracts are required.** Section 3.2 describes the primitive seams.
3. **No dependency on a kernel, OS filesystem/process/thread/signal service, native transport or driver model.**

Newlib's local `_write`/`_fstat`/`_isatty` hooks implement console semantics;
read/seek/close hooks diagnose unsupported use. They are not OS syscalls and
are not supplied by `libnosys`. P9a's actual hardware/context/CRT stubs fail
fast instead of returning fake successful values.

The target has no RTOS that WMB uses. WMB is a **library with an entry point**, not an application on an operating system: its own concurrency is
`framework/fiber/` and nothing else (§5.3). It does not assume it is the only component in the image — see [`90-`](90-decisions.md) D‑42 for the
boundaries that keep that true and D‑44 for how it coexists with whatever else the system runs.

## 3. The Floor

### 3.1 libc floor

The table below is the original floor hypothesis, not an exhaustive dependency
census. The implemented ARM reference uses full newlib/libm/libgcc and the
reviewed [actual floor](floor-symbols.txt).

| Header | Functions |
|---|---|
| `<string.h>` | `memcpy` `memmove` `memset` `memcmp` `strlen` `strcpy` `strncpy` `strcmp` `strncmp` `strcat` `strncat` `strchr` `strrchr` `strstr` `strdup` |
| `<stdlib.h>` | `malloc` `calloc` `realloc` `free` `abort` `atoi` `strtol` `strtoul` `qsort` `bsearch` `abs` |
| `<stdio.h>` | `snprintf` `vsnprintf` `sprintf` |
| `<ctype.h>` | `isalpha` `isdigit` `isspace` `isprint` `isxdigit` `toupper` `tolower` |
| `<setjmp.h>` | `setjmp` `longjmp` — mandatory; `FatalError` unwinds to a recovery point ([`90-`](90-decisions.md) D‑32) |
| `<math.h>` | `sin` `cos` `sqrt` `floor` `ceil` — RENDER only |
| freestanding | `<stdint.h>` `<stddef.h>` `<stdbool.h>` `<stdarg.h>` `<limits.h>` `<float.h>` |

P8 additionally uses C11 `<stdatomic.h>` for API admission and generic allocation
statistics, and `<errno.h>` for allocator failure reporting. P9 must census the
actual target compiler's atomic/runtime lowering; hosted lock-free instructions
are not proof that a different MCU requires no compiler support library.

The exact reference list is now checked in as `docs/floor-symbols.txt`, derived
from complete-archive requirements before target-library resolution, not an
already-resolved ELF's empty undefined list.
§3.1 is the starting hypothesis; the freestanding compile refines it. Expecting it to be exactly right on the first attempt is the mistake this phase is
structured to avoid — [`91-`](91-open-questions.md) Q18 lists the five categories it is most likely to be missing, of which the compiler runtime
(`libgcc` / `compiler-rt`) is the one no source grep can find.

The user approved **full newlib for P9a** after actual toolchain inspection:
nano disables the `%zu`/`%llu` formats already used by WMB and has a different
reentrancy layout. Mixing full formatter objects into nano was rejected.
The matching full library/header configuration preserves C99 diagnostics;
a specially built small libc remains later optimization (D-57 amends D-36).

The reference keeps the **actual existing `hal_alloc` seam**, backed by newlib's
allocator over a bounded linker arena. The user explicitly approved this
reference scope. The historical claim that the hosted implementation used
legacy `heap.c` was not true of the verified build; P9a does not silently replace
the hosted allocator or certify a board allocator/locking policy.

Release requires 81 floor entries; Debug requires 79, including assertions.
The reviewed union has 82. The observed additions include compiler soft-float/
integer helpers, newlib errno/state, string extensions and actual RENDER math.
No atomic support library is required for the selected 32-bit reference.
JWM's POSIX matching is supplied by pinned byte-oriented TRE 0.9.0, not a
dummy matcher; pinned inflate-only zlib 1.4 supplies font decompression.

### 3.2 Platform primitives — four functions

| Function | Contract | Typical implementation |
|---|---|---|
| `wmb_os_sbrk(intptr_t)` | Grow the heap; return the previous break or `(void*)-1` | Bump pointer over a linker‑reserved region |
| `wmb_os_console_write(const char*, int)` | Emit diagnostics; must work before display init | UART TX, or semihosting |
| `hal_tick_ms(void)` | Monotonic milliseconds | A free‑running hardware counter |
| `wmb_os_abort(void)` | Terminate; never returns | Halt, or reset |

These are platform seams, not an exhaustive syscall ABI. Actual public
device/time/allocation/task and fiber-port headers remain authoritative.
P9a's local newlib glue is single-owner reference code; P9b must establish
real startup and shared allocator/newlib-state/diagnostic synchronization.

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

int hal_display_init(void)
{
    const uint32_t w = WMB_SCREEN_WIDTH, h = WMB_SCREEN_HEIGHT;
    const hal_pixfmt_t fmt = WMB_SCREEN_FORMAT; /* backend-private board config */
    display_controller_configure(w, h, fmt);
    fb.pixels = (void *)WMB_FRAMEBUFFER_BASE;
    fb.width  = w;  fb.height = h;
    fb.stride = WMB_FRAMEBUFFER_STRIDE; /* actual hardware/shadow row pitch */
    fb.bpp    = (fmt == HAL_PIXFMT_RGB565 ? 16 : 32);
    fb.format = fmt;
    return 0;
}

hal_display_status_t hal_display_pump(void)
{
    /* Advance queued DMA/scanout without waiting. Latch device failures
     * until shutdown/reinitialization; ordinary hardware stays active. */
    return HAL_DISPLAY_ACTIVE;
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

Eighteen contract functions plus five port symbols. Nothing else — if a port needs to define anything outside this list, either the list is wrong or the
port is doing work that belongs above the contract.

| Header | Symbols | Notes |
|---|---|---|
| `hal_display.h` | `hal_display_init` `hal_display_shutdown` `hal_display_get_fb` `hal_display_flush` `hal_display_pump` `hal_display_set_power` | §4.1. Native geometry/format; `set_power` may return −1 |
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

No `mmap` is planned on target. A P9 port must supply static/pool-backed task
stacks; today's hosted `hal_task_create()` still uses the guard-page port
allocator. The following is target placement design, not an already implemented
target task pool:

```c
static uint8_t stack_hal [WMB_STACK_HAL ] __attribute__((aligned(16)));
static uint8_t stack_xsrv[WMB_STACK_XSRV] __attribute__((aligned(16)));
static uint8_t stack_jwm [WMB_STACK_JWM ] __attribute__((aligned(16)));
static uint8_t stack_app [WMB_APP_MAX][WMB_STACK_APP] __attribute__((aligned(16)));
```

Overflow detection is `FIBER_CANARY` checked on every switch — cheaper than a guard page and available without an MMU. It detects overflow *after* the fact
rather than at the moment of the offending store, which is why stack sizing is validated on the host (where guard pages trap precisely) before the target
build.

#### P8 measured hosted stack data

The final 10,000-managed-window workload used GCC 13.3, the glibc ucontext port,
Debug, 800x480 RGB565. These are **sentinel-touched bytes including the hosted
context**, not proposed MCU stack reservations:

| Runtime task | Observed touched HWM (bytes) | Hosted reservation (bytes) |
|---|---:|---:|
| `xsrv` | 12,056 | 524,288 |
| `jwm` | 3,944 | 524,288 |
| Frozen-v1 SDK app | 3,912 | 262,144 |
| Current SDK app | 3,912 | 262,144 |
| Foreign Display worker `api0` | 2,312 | 262,144 |
| Foreign Display worker `api1` | 2,184 | 262,144 |
| Independent allocation tenant | 2,624 | 65,536 |
| OS main/root stack | **Unknown bounds**, not zero | Platform-owned |

The fresh port context/forged-frame footprint is 1,016 bytes. The canary is
initialized separately after filling the stack sentinel and is checked on every
switch; a corruption test reaches the actual scheduler fatal path. HWM is sampled
before owned tasks are destroyed. It does not account for untouched automatic
storage or predict another architecture's interrupt/register/libc frames.

`FIBER_MAX` remains the shared value 8. The workload's root, server, WM, two
SDK apps, two lazy gateway workers and one other tenant fit exactly; a different
deployment must budget its own simultaneous tasks. These host reservations
peak at 2,162,688 bytes plus seven guard pages and the OS main stack, outside
the tracked WMB heap. After the temporary gateway workers close, the normal
two-app desktop retains 1,572,864 reserved stack bytes.

P9 must choose margins and verify the actual target ABI/workload. Do not copy
the old 32-KiB external-stack or roughly 300-KiB total-stack estimates into a
board linker script.

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

[toolchain-arm-p9a.cmake](../cmake/toolchain-arm-p9a.cmake) uses the real
bare-metal compiler and matching binutils/sysroot. CMake compiler probes build
static libraries; they do not execute ARM code or force a false
`CMAKE_C_COMPILER_WORKS=1`. The native keysym generator is built separately.

```sh
cmake -S . -B build-p9a-arm -DWMB_PROFILE=p9a \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-p9a.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-p9a-arm -j4
ctest --test-dir build-p9a-arm --output-on-failure
```

The checked profile excludes Linux/SDL/native IPC targets entirely and shares
the real hosted core source groups. Repeat Debug in a separate build directory.
The raw SDK disables per-function/data section splitting privately: on ARM
those archive metadata sections doubled file size without adding API behavior.
All slots remain typed; the actual ARM archive is 15,600 bytes.

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
   │ fiber stacks (§5.2)            │     target-specific; host measurements below
   ├────────────────────────────────┤  ← __heap_start
   │ heap                           │     measured workload + target headroom (§8.1)
   ├────────────────────────────────┤  ← __heap_end
   │ shadow framebuffer (optional)  │     W×H×bpp/8
   └────────────────────────────────┘
```

### 8.1 Heap sizing

P8 replaces the old per-resource guesses with actual owner-tagged data.
Owner 1 covers selected TinyX/KDrive/Xlib/font/transport/JWM/ABI allocations,
including zlib's private decompression allocations. Counts include live payload
and tracking-node bytes, **not** libc arena overhead, SDL, process RSS or the
separately reserved port stacks.

| Measured workload/domain | Bytes / result |
|---|---|
| 10,000 managed lifecycles, after 32 warmup cycles and quiescent sampling | **348,881 -> 348,881** settled WMB bytes; blocks/payload also unchanged |
| Same workload WMB allocation HWM | **402,537 -> 402,537** |
| Concurrent other owner | **102,400 payload**, 105,600 including tracking; no effect on WMB's peak |
| After ordered WM/apps/server shutdown | 40,087 WMB bytes: 28,247 payload + 11,840 tracking; 370 reachable cache blocks |
| 800x480 RGB565 HAL framebuffer | 768,000 bytes per surface, outside the WMB owner |
| 800x480 XRGB8888 HAL framebuffer | 1,536,000 bytes per surface, outside the WMB owner |
| Actual 3,600-second input endurance | **351,745 -> 351,745** live WMB bytes; peak **402,602 -> 402,602** |

The 10,000 cycles include real create/map/JWM-parent/withdraw/destroy operations,
not fake resource allocation or just unmapped windows. They complete in 41.008
measured seconds with a busy workload-generating root loop; this is not an
idle-CPU certificate. A sentinel/metadata OOM suite and the pinned font's real
zlib/error paths are separately tested.

The final 60-second instrumented native/HAL/gateway run and real separate
current/v1 applications have zero Memcheck errors and zero lost bytes.
Reachable caches are not leaks. Instrumented mode intentionally disables only
inactive-stack HWM reads and does not certify normal timing or an allocation
peak under a different traffic backlog.

These are hosted workload facts, not a universal maximum. Screen size/format,
application-owned resources, simultaneous Displays/windows, request buffers,
allocator bookkeeping and target alignment change the requirement. Select target
headroom and remeasure rather than asserting the old approximately 2-MB working
set / 4-MB heap guess.

The completed normal hour processed **174,625** SDL input injections:
349,248 key events, 349,248 button events and 174,624 motion events reached the
SDK window. It ran for **3,600,000 measured input milliseconds** and
**3,604.442 independently measured wall seconds** including startup/cleanup.
The certified native binary was unchanged throughout. Root-poll maximum gap was
**8 ms**, WM callback maximum **113.870 ms**, and completed clock-draw maximum
**1,009.947 ms**; both application loops progressed over 3.19 million times.
No canary or starvation gate tripped, and cleanup drained all gateway calls.

[The machine-readable host record](reference/p8-host-verification.json) includes
the UTC interval, exact binary SHA-256, independent wall measurement, heap/stack
data, matrix results and scope limits. The raw run/controller logs remain in
the persistent hosted build. Interrupted or failed earlier attempts do not
count toward this certificate.

### 8.2 Image size

Actual GCC 13.3 hosted `size` output after the P8 implementation:

| Linked executable / configuration | Text | Data | BSS | Sum (bytes) |
|---|---:|---:|---:|---:|
| Native ABI workload, Debug/RGB565 | 1,771,402 | 24,576 | 47,304 | 1,843,282 |
| Native ABI workload, Release/XRGB8888 | 1,689,877 | 23,864 | 47,320 | 1,761,061 |
| Host-only RPC harness, Debug/RGB565 | 1,832,698 | 24,616 | 47,624 | 1,904,938 |
| Host-only RPC harness, Release/XRGB8888 | 1,722,028 | 23,912 | 47,608 | 1,793,548 |

These images include their validation entry/workload, not dynamically linked
SDL/libc's memory. They are not a target map or stripped file-size estimate.
The host-only RPC row must not become an embedded dependency.

The standalone/current **raw SDK archive is 15,924 bytes**, including archive
index and object metadata; the native v2 table itself is 1,000 bytes on this
64-bit data model (16-byte header + 123 pointers). P9 must measure its actual
linker sections and relocations on the selected architecture.

Rendering still dominates text. If target measurements are too large, reduction
levers, in order of value, remain:

1. `-Os` instead of `-O2` (typically 25–30 % on this kind of code)
2. `--gc-sections` (removes the unreachable shadow blitters and `fb` variants)
3. Drop RENDER if nothing uses it (7,962 LOC — but `fbpict.c` and JWM may)
4. Drop RANDR if `kdrive.c`'s references can be stubbed (5,217 LOC)
5. Drop the `mi` cursor/sprite path if the hardware has a cursor overlay

Levers 3–5 are behavioural changes and each needs a decision entry.

## 9. Boot Sequence

**P9b design sketches, not the P9a implementation.** Real reset/vector/CRT
initialization and loader/runtime ownership are not implemented by P9a.
The actual once-only component lifecycle is
[wmb_main.c](../transport/x11/wmb_main.c); use that implementation rather than
treating old illustrative configuration fields below as a public API.

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
    if (hal_display_init() != 0) return -1; /* geometry/format belong to the backend */
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

### 10.1 Verified P9a reference

`wmb_p9a_verify` is an ALL build target. It checks 544 actual ARM object/
dependency records, matching hosted source inventories, fourteen complete
archives without GC/LTO, real floor providers, no unresolved/forbidden symbols
or dynamic relocations, a readonly 508-byte 123-slot API and the separate ARM
SDK binding. Raw data is compared with every file-backed ELF load section.
Thirteen negative tests include actual linker ROM/RAM overflow and missing
regex-provider failures.

| Arm GNU 14.2 reference | ROM load span | RAM reserved | ELF file bytes |
|---|---:|---:|---:|
| Release component | 919,808 | 6,398,488 | 1,187,164 |
| Release whole-archive proof | 1,074,112 | 6,401,232 | 1,403,128 |
| Release separate SDK application | 44,608 | 591,960 | 75,012 |
| Debug component | 1,142,036 | 6,399,792 | 7,685,508 |
| Debug whole-archive proof | 1,320,968 | 6,402,752 | 10,698,516 |
| Debug separate SDK application | 45,984 | 591,960 | 120,272 |

These are load-span measurements, not file-size guesses or runtime allocation
HWM. Component RAM includes fixed 4-MiB heap and `0x210000` stack-area
reservations; app RAM includes 512-KiB heap and 64-KiB stack. No framebuffer or
working stack-pool implementation is present. Debug metadata does not occupy
the ROM load span.

The **synthetic**, disjoint map gives the component 4 MiB ROM at `0x08000000`
and 8 MiB RAM at `0x20000000`; the app has ROM at `0x08400000` and 1 MiB RAM at
`0x20800000`. `.wmb_api` starts the component load image, not a processor
vector table. Do not flash these non-runnable reference artifacts as firmware.

The ordinary hosted suite stays 52/52. The real bundled TRE/zlib floor passes
54/54 native tests and 54/54 strict ASan/UBSan tests, including the actual JWM
matcher, 210 stock-POSIX comparisons, tracked OOM and console/arena/fatal cases.
The only new TinyX native site is the approved private font prototype; all 45
patch sites, original notices, 36 native-reference probes and one/zero/empty
variants remain gated.

P9b must replace failing hardware/context/CRT stubs, implement startup/loader
execution and synchronization, choose the actual board budget, then run input,
desktop/client and one-hour hardware acceptance. P9a does not close Q6.

## 11. Deferred Decisions

| # | Item | Tracked as |
|---|---|---|
| D1 | Which target, and therefore the real values of `WMB_LOAD_BASE`, `WMB_FRAMEBUFFER_BASE`, heap size, screen geometry | [`91-`](91-open-questions.md) Q6 |
| D2 | Fiber port vs platform scheduler (§5.3) | **Closed** — WMB brings its own scheduling; no RTOS is used ([`90-`](90-decisions.md) D‑31). Coexistence with other components is D‑44 |
| D3 | Shadow framebuffer vs direct scanout (§4.1) | **Closed** — double buffered ([`90-`](90-decisions.md) D‑35) |
| D4 | Which libc | **P9a settled by user approval:** matching full newlib, because installed nano cannot represent current C99 diagnostics; board-specific revalidation remains P9b (D-57) |
| D5 | Font strategy | **Closed** — several built‑in bitmap fonts, FreeType later ([`90-`](90-decisions.md) D‑38) |
| D6 | Whether XKB is compiled in | **Closed** — no ([`90-`](90-decisions.md) D‑39) |

Only D1 remains, and it is an external input. Every target‑side decision that could be taken without knowing the hardware has been.
