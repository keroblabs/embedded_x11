# 10 — OS Dependency Census

## 1. Purpose

This document is the **complete catalogue of every Linux / POSIX dependency in the code that will be compiled into WMB**. It is the first deliverable of
workstream 2 ([`00-introduction.md`](00-introduction.md) §5.2) and the input to [`11-portability-plan.md`](11-portability-plan.md), which assigns each
entry a removal mechanism, and to [`12-platform-contract.md`](12-platform-contract.md), which defines what replaces it.

It answers one question per dependency: *where is it, what needs it, and what class of thing is it?* It deliberately does **not** decide how to remove it —
that is [`11-`](11-portability-plan.md).

### 1.1 Method

Three passes over the four trees, scoped to the file set that [`01-component-inventory.md`](01-component-inventory.md) marks CARRY, GUARD or STUB. EXCLUDE
files are censused anyway, because an exclusion has to be *justified* by showing what it would otherwise drag in.

1. **Header pass.** Every `#include <…>` matching `sys/*`, `netinet/*`, `netdb.h`, `pwd.h`, `grp.h`, `unistd.h`, `signal.h`, `fcntl.h`, `dirent.h`,
   `termios.h`, `linux/*`, `poll.h`, `pthread.h`, `dlfcn.h`.
2. **Symbol pass.** Direct call sites for `select`, `poll`, `socket`, `fork`, `exec*`, `waitpid`, `sigaction`, `signal`, `mmap`, `ioctl`, `readv`,
   `writev`, `fcntl`, `getpeername`, `setrlimit`, `gettimeofday`, `getenv`, `fopen`, `opendir`, `setjmp`, `longjmp`.
3. **Semantic pass.** Dependencies that are not visible as a header or a symbol: blocking semantics, file‑descriptor identity, process identity,
   environment, filesystem paths, signal‑driven lifecycle.

Counts are from the tree as it stands and are reproducible with the commands in §9.

### 1.2 Dependency classes

| Class | Meaning | Typical fate |
|---|---|---|
| **TRANSPORT** | Exists only to move X11 protocol bytes between address spaces | Deleted with the transport ([`21-`](21-inprocess-transport.md)) |
| **BLOCKING** | Suspends the thread until an external event | Becomes `hal_yield_timeout()` |
| **PROCESS** | `fork`, `exec`, signals, `waitpid`, uid/gid, rlimits | Deleted; no process model on target |
| **FILESYSTEM** | `fopen`, `open`, `opendir`, path constants | Optional; compiled out or backed by `wmb_os.h` |
| **ENVIRONMENT** | `getenv`, `uname`, hostname | Replaced by compile‑time constants |
| **MEMORY** | `mmap`, `sbrk`, `malloc` | Routed through `wmb_os.h` |
| **HARDWARE** | `ioctl`, `/dev/*`, VT switching, MMIO via `mmap` | Excluded with the Linux DDX backends |
| **TYPEDEF‑ONLY** | The header is included solely for `size_t`, `off_t`, `pid_t` etc. | One‑line change to `<stddef.h>` / `<stdint.h>` |
| **LIBC‑CORE** | `memcpy`, `strlen`, `snprintf`, `qsort`, … | Kept; part of the documented freestanding floor |

## 2. Headline Findings

Five conclusions that shape the whole plan.

1. **`tinyx/` POSIX coupling lives in eleven files.** Out of 193 `.c` files, exactly eleven include a POSIX system header. Six are in `tinyx/os/`; five are
   in `tinyx/kdrive/{linux,fbdev,vesa}/` or `tinyx/Xext/`, which [`01-`](01-component-inventory.md) excludes wholesale.

2. **The server core is already portable.** `dix` (26.7 kLOC), `mi` (23.5 kLOC), `fb` (14.4 kLOC), `render`, `randr`, `xfixes`, `damageext`, `miext` — a
   combined ~78 kLOC — contain **zero** POSIX system includes and **zero** direct syscalls. This is the single most important fact in the census: the bulk
   of the code needs no portability work at all.

3. **`select()` appears in exactly three places** in the entire `tinyx/` tree: `os/WaitFor.c` (L215, L219 via the `Select` macro), `os/connection.c`
   (L679), and `kdrive/linux/mouse.c` (L66, L150 — excluded).

4. **`getenv()` appears once**: `tinyx/dix/main.c:250`, reading `XAUTHORITY`. That is the entire environment dependency of the X server.

5. **No `fork`, `exec`, `socket()`, `mmap()`, `ioctl()` or `poll()` call site exists anywhere in `tinyx/` outside the excluded directories and the three
   transport files.** The tree is much cleaner than an X server has any right to be, because KDrive was designed for embedded use.

The corollary: **the portability problem is small and the transport problem is the real work.** Effort should be budgeted accordingly.

## 3. `tinyx/os/` — The Concentration

Sixteen `.c` files, 11,082 LOC. This directory holds essentially all of the server's OS coupling.

### 3.1 Per‑file header census

| File | LOC | POSIX headers included | Disposition |
|---|---:|---|---|
| `access.c` | 1,952 | `<sys/types.h>` `<sys/socket.h>` `<sys/ioctl.h>` `<netinet/in.h>` `<netdb.h>` `<sys/param.h>` `<pwd.h>` `<grp.h>` | EXCLUDE |
| `xdmcp.c` | 1,618 | `<sys/param.h>` `<sys/socket.h>` `<netinet/in.h>` `<netdb.h>` `<X11/Xpoll.h>` | EXCLUDE |
| `utils.c` | 1,640 | `<signal.h>` ×3 `<sys/wait.h>` `<sys/resource.h>` `<sys/stat.h>` `<sys/param.h>` | GUARD |
| `connection.c` | 1,027 | `<signal.h>` `<sys/socket.h>` `<sys/uio.h>` `<X11/Xpoll.h>` | STUB |
| `io.c` | 1,026 | `<sys/uio.h>` `<X11/Xpoll.h>` | STUB |
| `WaitFor.c` | 670 | `<X11/Xpoll.h>` | STUB |
| `osinit.c` | 199 | `<sys/resource.h>` | GUARD |
| `log.c` | 196 | `<sys/stat.h>` | GUARD (trivial) |
| `oscolor.c` | 1,598 | — | CARRY |
| `strlcat.c` | 56 | `<sys/types.h>` | GUARD (TYPEDEF‑ONLY) |
| `strlcpy.c` | 51 | `<sys/types.h>` | GUARD (TYPEDEF‑ONLY) |
| `auth.c` | 236 | — | EXCLUDE |
| `mitauth.c` | 162 | — | EXCLUDE |
| `xdmauth.c` | 501 | — | EXCLUDE |
| `xauembed.c` | 120 | — | EXCLUDE |
| `xstrans.c` | 30 | — (pulls `X11/xtrans/*.c`) | EXCLUDE |
| `osdep.h` | 274 | `<sys/param.h>` `<X11/Xpoll.h>` | GUARD |

`<X11/Xpoll.h>` deserves a note: it is X.Org's portability wrapper around `fd_set` / `FD_SET` / `select`. Every file that includes it is transport‑adjacent
by construction. It appears in five files, four of which are STUB or EXCLUDE; the fifth is `osdep.h`, which the STUBs will stop needing.

### 3.2 Per‑file symbol census

| File | Symbol | Line | Class | Notes |
|---|---|---:|---|---|
| `WaitFor.c` | `Select()` | 215, 219 | BLOCKING / TRANSPORT | The macro resolves to `select()`. The **only** blocking point in the server |
| `WaitFor.c` | `XFD_COPYSET`, `XFD_UNSET` | 175, 176, 201 | TRANSPORT | `fd_set` manipulation |
| `WaitFor.c` | `GetErrno()` | 221 | TRANSPORT | `errno` after `select` |
| `connection.c` | `Select()` | 679 | BLOCKING | Zero‑timeout poll of a single fd during connection setup |
| `connection.c` | `getpeername` | in `ClientAuthorized` | TRANSPORT | Peer credential check |
| `connection.c` | `_XSERVTrans*` | throughout | TRANSPORT | `xtrans` calls: `SocketInit`, `MakeAllCOTSServerListeners`, `Accept`, `Close`, `GetConnectionNumber`, `IsLocal` |
| `connection.c` | signal handling | `<signal.h>` | PROCESS | `SIGUSR1` (readiness notification to parent), `SIGPIPE` |
| `io.c` | `_XSERVTransRead` / `_XSERVTransWritev` | 180, 804 | TRANSPORT | The only I/O in the server |
| `io.c` | `struct iovec` | `<sys/uio.h>` | TRANSPORT | Scatter/gather write |
| `utils.c` | `sigaction` | multiple | PROCESS | `SIGTERM`/`SIGINT`/`SIGHUP` server lifecycle |
| `utils.c` | `<sys/wait.h>` | — | PROCESS | Child reaping |
| `utils.c` | `setrlimit` / `getrlimit` | — | PROCESS | fd limit raising |
| `utils.c` | `X_GETTIMEOFDAY` | 388 | (kept) | `GetTimeInMillis()` — becomes `hal_tick_ms()` |
| `utils.c` | `Fopen()` | 1267–1351 | FILESYSTEM / PROCESS | Privilege‑dropping `fopen` wrapper. Used only by `auth.c` (EXCLUDE) |
| `utils.c` | `<netdb.h>` | — | ENVIRONMENT | Hostname lookup for display name |
| `osinit.c` | `setrlimit(RLIMIT_NOFILE)` | — | PROCESS | Raise fd limit |
| `osinit.c` | `fopen` | 117, 118 | FILESYSTEM | Redirect stderr to a log file or `/dev/null` |
| `log.c` | `<sys/stat.h>` | — | FILESYSTEM | Log file rotation |
| `access.c` | `fopen` | 867 | FILESYSTEM | `/etc/X*.hosts` |
| `auth.c` | `Fopen` | 106 | FILESYSTEM | `.Xauthority` |

### 3.3 Semantic dependencies in `tinyx/os/`

Not visible as headers or symbols, but real:

| # | Dependency | Where | Consequence |
|---|---|---|---|
| SD1 | **Client identity is a file descriptor.** `OsCommRec.fd`, `ClientsWithInput`, `AllSockets`, `MaxClients` are all fd‑indexed bitsets | `osdep.h`, `connection.c`, `WaitFor.c`, `io.c` | Internal clients need a synthetic fd‑like index, or the bitsets must be replaced by a client list. See [`21-`](21-inprocess-transport.md) §5.3 |
| SD2 | **Readiness is edge‑triggered by `select`.** `WaitForSomething` returns a *set* of ready clients | `WaitFor.c` | The stub must produce the same set from a different source |
| SD3 | **`BlockHandler`/`WakeupHandler` bracket the `select` call.** KDrive's `pollEvents` and `mieq` draining depend on being called there | `WaitFor.c:206` | **Must be preserved exactly.** Losing this loses all input |
| SD4 | **Output is buffered and flushed lazily.** `FlushAllOutput`, `SetCriticalOutputPending`, `oc->output` | `io.c` | In‑process delivery is immediate; the flush machinery becomes no‑ops but the *call sites* remain |
| SD5 | **The server is a process that can be signalled.** `GiveUp` on SIGTERM, reset on SIGHUP | `utils.c` | Removed; shutdown becomes an internal API call |
| SD6 | **`errno` is the error channel.** | `WaitFor.c`, `io.c`, `connection.c` | Stubs must not depend on `errno` semantics |

SD3 is the highest‑risk item in this document. It is easy to write a `WaitForSomething` stub that yields correctly and silently kills the mouse.

## 4. `tinyx/kdrive/` — Excluded Backends and Two Guards

### 4.1 Excluded backends

| Directory | Files | LOC | POSIX surface |
|---|---|---:|---|
| `kdrive/linux/` | `linux.c`, `keyboard.c`, `mouse.c` | 1,760 | `<linux/*>`, `<termios.h>`, `<sys/ioctl.h>`, `<signal.h>`, `<X11/Xpoll.h>`, `select()` ×2, VT switching, `/dev/tty`, `/dev/input/*` |
| `kdrive/fbdev/` | `fbdev.c`, `fbinit.c` | 884 | `<linux/fb.h>`, `<sys/mman.h>`, `<unistd.h>`, `<sys/ioctl.h>`, `/dev/fb0` |
| `kdrive/vesa/` | `vesa.c`, `vbe.c`, `vm86.c`, `vga.c`, `vesainit.c` | 3,472 | `<sys/mman.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/ioctl.h>`, `<sys/utsname.h>`, x86 VM86 mode |

**6,116 LOC and the large majority of the tree's Linux coupling, removed by not listing three directories in CMake.**

### 4.2 Retained KDrive — two dependencies

| File | LOC | Dependency | Class | Disposition |
|---|---:|---|---|---|
| `kdrive/src/kmap.c` | 151 | `<sys/mman.h>`, `<unistd.h>`, `<sys/ioctl.h>` — `KdMapDevice`/`KdUnmapDevice` | HARDWARE | **EXCLUDE.** Only the excluded backends call it. Verify at link |
| `kdrive/src/kinput.c` | 1,634 | `<signal.h>` — `KdSigio` / async input notification | PROCESS | **GUARD.** The SIGIO path is a Linux‑only optimisation; the polled path (`KdOsFuncs.pollEvents`) is what we use |

`kinput.c` is otherwise clean: `KdEnqueueKeyboardEvent`, `KdEnqueueMouseEvent`, keymap and repeat handling are all portable.

### 4.3 Our DDX — `tinyx/kdrive/tima/`

Zero POSIX includes. Zero SDL includes. Already portable in the sense that matters. **One layering defect**: all four `.c` files include `hal_drivers.h`,
which lives in `linux_hosted/sdl_drivers/` — i.e. the DDX is coupled to a *directory named for a backend*, even though it is not coupled to SDL itself. Fixed
by moving the contract to `framework/library/hal_*.h` ([`12-`](12-platform-contract.md)).

## 5. `tinyx/Xext/` and the Rest of the Server

| File | Dependency | Class | Disposition |
|---|---|---|---|
| `Xext/shm.c` | `<unistd.h>`, System V shared memory | MEMORY / TRANSPORT | EXCLUDE — meaningless in one address space |
| `Xext/xf86bigfont.c` | `<unistd.h>`, shared memory | MEMORY | EXCLUDE — obsolete |
| `dix/main.c` | `getenv("XAUTHORITY")` at L250 | ENVIRONMENT | GUARD — replace with a compile‑time constant or remove with auth |
| `dix/main.c` | `main(int, char**, char**)` at L227 | PROCESS | GUARD — becomes `TinyX_ServerFiber(void*)`. See [`90-`](90-decisions.md) D‑07 |
| `mi/miinitext.c` | registers `dbe`, `shm`, `xf86bigfont` | — | GUARD — three guarded lines for the excluded extensions |

**Everything else in `dix`, `mi`, `fb`, `render`, `randr`, `xfixes`, `damageext`, `miext`, `dbe` is clean.** No POSIX headers, no syscalls, no environment,
no filesystem.

## 6. `libX11/` — Concentrated in Six Files

262 `.c` files, 37,265 LOC. POSIX coupling appears in **six** of them.

| File | Headers | Class | Disposition |
|---|---|---|---|
| `xcb_io.c` | `<sys/select.h>` | TRANSPORT / BLOCKING | **EXCLUDE** — replaced by `wmb_transport.c` |
| `XlibInt.c` | `<sys/socket.h>` `<unistd.h>` `<sys/ioctl.h>` `<sys/utsname.h>` | TRANSPORT / ENVIRONMENT | **GUARD** — contains `_XDefaultIOError`, `_XEatData`, error handling. Keep the non‑transport half |
| `locking.c` | `<dlfcn.h>` | PROCESS | **EXCLUDE** — thread locking; single‑threaded |
| `CrGlCur.c` | `<dlfcn.h>` | PROCESS | **EXCLUDE** — dynamically loads the cursor library |
| `xlibi18n/lcFile.c` | `<unistd.h>` | FILESYSTEM | **EXCLUDE** — locale database lookup |
| `xlibi18n/{lcDynamic,XlcDL}.c` | `<dlfcn.h>` | PROCESS | **EXCLUDE** — locale module loading |
| `os2Stubs.c` | `<sys/select.h>` | — | **EXCLUDE** — OS/2 |

Beyond headers, the semantic dependencies:

| # | Dependency | Where | Fate |
|---|---|---|---|
| LX1 | `Display.fd` is a real file descriptor; `ConnectionNumber(dpy)` is public API | `Xlibint.h`, `OpenDis.c` | Set to `-1`. JWM's `select()` on it is removed anyway |
| LX2 | `XOpenDisplay` parses `DISPLAY`, connects, reads `xConnSetup` off the wire | `OpenDis.c` | Replaced: `wmb_setup.c` synthesises the blob locally |
| LX3 | Locale initialisation in `XOpenDisplay` (`_XlcCurrentLC`) | `OpenDis.c`, `SetLocale.c` | Stubbed |
| LX4 | `XErrorDB` file lookup for error strings | `XErrDes.c` | Built‑in generic strings |
| LX5 | The `Data()` macro calls `_XSend` on buffer overflow | `X11/Xlibint.h` | **The one required upstream header edit** — resize instead of flush |
| LX6 | XID allocation depends on the server‑supplied range and XC‑MISC top‑ups | `xcb_io.c` `_XAllocID` | Reimplemented in `wmb_transport.c` |

## 7. `jwm/` — Concentrated in Seven Files

50 `.c` files, 26,325 LOC. POSIX coupling is confined to `jwm/src/jwm.h` (the umbrella header) plus six `.c` files.

| File | Dependency | Class | Disposition |
|---|---|---|---|
| `jwm.h` | `<sys/select.h>` `<unistd.h>` `<signal.h>` `<sys/wait.h>` | — | GUARD — gate the POSIX include block on the profile |
| `event.c` | `select()` on `ConnectionNumber(display)` | BLOCKING | GUARD — becomes `hal_yield_timeout()` |
| `command.c` | `fork`, `execl`, `waitpid`, `<fcntl.h>` | PROCESS | GUARD — becomes an in‑tree application table lookup |
| `main.c` | `main()`, `sigaction` for SIGTERM/SIGINT/SIGHUP | PROCESS | GUARD — `main` → `Jwm_Main`; signals deleted |
| `misc.c` | `getenv` | ENVIRONMENT | GUARD — compile‑time constant |
| `timing.c` | `TZ` environment push/pop around `localtime` | ENVIRONMENT | GUARD — always UTC |
| `font.c` | Xft / fontconfig paths | FILESYSTEM | GUARD — core fonts only |
| `parse.c` | `<fcntl.h>`, config file reading | FILESYSTEM | GUARD — hard‑coded configuration |

**43 of 50 JWM files are pure Xlib and need nothing.** JWM's reputation for being dependency‑light is deserved.

Additionally, ten optional external libraries are auto‑detected by `jwm/configure.ac` and must all be off: `png`, `jpeg`, `Xpm`, `cairo`, `rsvg-2`, `Xft`,
`Xrender`, `pango-xft`, `Xmu`, `Xinerama`. Kept: `X11` core, XBM icon reading (`XReadBitmapFileData`), optionally `Xext` for SHAPE.

## 8. `framework/` — Our Own Code

| File | Dependency | Class | Disposition |
|---|---|---|---|
| `sdl_library/*.c` | SDL2 | HARDWARE (hosted) | Backend — allowed, and the only place SDL may appear |
| `sdl_drivers/*.c` | SDL2 (indirect) | HARDWARE (hosted) | Backend |
| `types.h` | — | TYPEDEF | Must be verified freestanding‑clean |
| `library/heap.c` | — | — | Clean |
| `library/buffer.c` | — | — | Clean |
| `fiber/fiber.c` | — | — | Clean. Needs an arch port that *will* be platform‑specific by design |
| `wmb_x11/wmb_io.c` | `Xlibint.h`, `dixstruct.h`, `Xxcbint.h` | — | Clean of POSIX, but architecturally superseded |
| `CMakeLists.txt` | `-D_USE_LINUX` | — | **Must be removed.** Nothing should key off "is this Linux" |

The `-D_USE_LINUX` define in `framework/CMakeLists.txt:30` is worth calling out: it is exactly the shape of dependency this project exists to eliminate.
Portability decisions must key off *capability* macros (`WMB_HAS_OS_TRANSPORT`) or *profile* macros (`WMB_HOSTED`), never off an OS name.

## 9. Reproduction Commands

Every number in this document is reproducible from the repository root.

```bash
# Header pass — which files include which POSIX headers
for h in sys/socket.h sys/select.h netinet/ unistd.h sys/mman.h pthread.h \
         dlfcn.h signal.h sys/ioctl.h termios.h linux/ fcntl.h sys/wait.h \
         sys/uio.h sys/resource.h pwd.h grp.h netdb.h sys/utsname.h poll.h; do
    echo "-- $h"
    grep -rl "include *<$h" tinyx libX11/src jwm/src --include='*.c' --include='*.h'
done

# Symbol pass
grep -rnE '\b(Select|select|poll|socket|fork|execv|execl|waitpid|sigaction|mmap|ioctl|readv|writev|fcntl|getpeername|setrlimit|getenv|fopen|opendir)\s*\(' \
     tinyx libX11/src jwm/src --include='*.c'

# Per-module cleanliness check (should print nothing)
grep -rl 'include *<sys/\|include *<linux/\|include *<netinet/' \
     tinyx/dix tinyx/mi tinyx/fb tinyx/render tinyx/randr tinyx/xfixes \
     tinyx/damageext tinyx/miext
```

## 10. Census Summary

### 10.1 By tree

| Tree | Files in scope | Files with POSIX coupling | % clean |
|---|---:|---:|---:|
| `tinyx/` (all) | 193 | 11 | 94.3 % |
| `tinyx/` (after excluding `kdrive/{linux,fbdev,vesa}`, `Xext/{shm,xf86bigfont}`) | 178 | 6 | 96.6 % |
| `libX11/src/` | 262 | 7 | 97.3 % |
| `jwm/src/` | 50 | 7 | 86.0 % |
| `framework/` | 21 | 0 (backends excepted) | 100 % |

### 10.2 By class

| Class | Sites | Concentrated in |
|---|---:|---|
| TRANSPORT | ~40 | `os/{connection,io,WaitFor,xstrans}.c`, `libX11/src/xcb_io.c` |
| BLOCKING | 4 | `os/WaitFor.c` ×2, `os/connection.c` ×1, `jwm/src/event.c` ×1 |
| PROCESS | ~20 | `os/utils.c`, `os/osinit.c`, `jwm/src/{main,command}.c`, `kdrive/src/kinput.c` |
| FILESYSTEM | ~12 | `os/{osinit,log,access,auth,utils}.c`, `jwm/src/{parse,font}.c` |
| ENVIRONMENT | 4 | `dix/main.c:250`, `os/utils.c`, `jwm/src/{misc,timing}.c` |
| MEMORY | 3 | `kdrive/src/kmap.c`, `Xext/shm.c`, `Xext/xf86bigfont.c` |
| HARDWARE | many | Entirely within the three excluded KDrive backends |
| TYPEDEF‑ONLY | 3 | `os/strlcat.c`, `os/strlcpy.c`, `os/osdep.h` |

### 10.3 The bottom line

| Metric | Value |
|---|---|
| LOC removed by directory exclusion alone | 6,116 (`kdrive/{linux,fbdev,vesa}`) + 4,589 (`os/` auth/access/xdmcp/xstrans) + ~1,700 (`Xext/shm`, `xf86bigfont`) + 2,620 (`dbe`) = **~15,000** |
| LOC requiring replacement stubs | 2,723 (`os/{connection,io,WaitFor}.c`) → ~370 LOC of stubs |
| Files requiring guarded upstream edits | **13** — see [`11-`](11-portability-plan.md) §9 |
| Files in the ~78 kLOC server core requiring any change | **0** |

## 11. Open Items Raised by the Census

Recorded here, tracked in [`91-open-questions.md`](91-open-questions.md).

| # | Item | Question |
|---|---|---|
| C1 | `<X11/Xpoll.h>` in `osdep.h` | Can `osdep.h` drop it entirely once the three transport files are stubbed, or do the fd bitsets leak into `dixstruct.h`? |
| C2 | `kdrive/src/kmap.c` exclusion | **CLOSED.** The only callers of `KdMapDevice` and friends are `kdrive/vesa/vbe.c`, which is excluded by D‑16. No stub needed — [`90-`](90-decisions.md) D‑40 |
| C3 | `dbe` exclusion | **CLOSED.** `jwm/src/` has zero DBE references; the only mention in retained code is `mi/miinitext.c`, already covered by the planned guard — [`90-`](90-decisions.md) D‑41 |
| C4 | `X_GETTIMEOFDAY` | `GetTimeInMillis()` currently derives from wall‑clock time. Does anything depend on wall clock rather than monotonic? Screen saver and XKB repeat should not |
| C5 | `setjmp`/`longjmp` | **CLOSED.** They stay in the libc floor unconditionally; `FatalError` unwinds on hosted and stub‑backed builds, and a real target substitutes its own fault infrastructure — [`90-`](90-decisions.md) D‑32 |
| C6 | libc floor | The exact set of `<string.h>` / `<stdio.h>` / `<stdlib.h>` functions actually referenced must be enumerated before the target build, not after |
