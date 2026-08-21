# Porting TinyX to a Non‑Linux / Bare‑Metal OS

**Last updated**: 2026-07-13
**Scope**: How to build TinyX (the X11 server in `tinyx/`) against a custom OS
or bare‑metal environment. Enumerates every OS/libc API TinyX depends on,
where each dependency lives, and how to satisfy it without Linux.

**Related docs**:
- [KDRIVE_CREATION_GUIDE.md](KDRIVE_CREATION_GUIDE.md) — how the display /
  input HAL bridges TinyX to hardware. This document is the *other half*: how
  the **OS/libc side** bridges TinyX to your kernel or runtime.
- [CLIENT_SERVER_TRANSPORT_REMOVAL.md](CLIENT_SERVER_TRANSPORT_REMOVAL.md) —
  the in‑process client/server bridge that eliminates the socket transport.
  **This is the assumed architecture for the target build**; all references
  below to Tier 3 / sockets are for the hosted (legacy) path only.
- Prior notes in [../archive_docs2/OS_LIBC_REQUIREMENTS.md](../archive_docs2/OS_LIBC_REQUIREMENTS.md)
  and [../archive_docs/HAL_SPECIFICATION.md](../archive_docs/HAL_SPECIFICATION.md)
  — earlier drafts, superseded by this file.

---

## Table of Contents

1. [The Big Picture](#1-the-big-picture)
2. [Dependency Layers in TinyX](#2-dependency-layers-in-tinyx)
3. [What Lives in `tinyx/os/`](#3-what-lives-in-tinyxos)
4. [The libc Surface TinyX Uses](#4-the-libc-surface-tinyx-uses)
5. [The POSIX Syscall Surface](#5-the-posix-syscall-surface)
6. [The Sockets / Transport Surface](#6-the-sockets--transport-surface)
7. [Timing and the Main Loop](#7-timing-and-the-main-loop)
8. [Signals, Process, Environment](#8-signals-process-environment)
9. [Filesystem: What TinyX Reads at Runtime](#9-filesystem-what-tinyx-reads-at-runtime)
10. [Three Porting Strategies](#10-three-porting-strategies)
11. [Step‑by‑Step Port to a Custom OS](#11-stepbystep-port-to-a-custom-os)
12. [Newlib Syscalls Reference Skeleton](#12-newlib-syscalls-reference-skeleton)
13. [Removing Linux Dependencies — File‑by‑File](#13-removing-linux-dependencies--filebyfile)
14. [Build System Changes](#14-build-system-changes)
15. [Verification Checklist](#15-verification-checklist)

---

## 1. The Big Picture

TinyX (the Kdrive‑based X server in `tinyx/`) was written for POSIX. To move
it off Linux you must satisfy **three separate contracts**, in decreasing
order of difficulty:

```
+-----------------------------------------------------------------+
|  Contract A — Display / Input HAL                               |
|    Covered by KDRIVE_CREATION_GUIDE.md                          |
|    Implemented via KdCardFuncs / KdKeyboardFuncs /              |
|    KdMouseFuncs / KdOsFuncs.                                    |
+-----------------------------------------------------------------+
|  Contract B — OS layer (tinyx/os/*.c)                           |
|    Networking, WaitFor(), auth, logging, argv parsing.          |
|    Choice: (a) port it, (b) replace it, or (c) stub it.         |
+-----------------------------------------------------------------+
|  Contract C — libc + syscalls                                   |
|    malloc, memcpy, printf, gettimeofday, open, read, ...        |
|    Provide via newlib + syscalls.c, or your own tiny libc.      |
+-----------------------------------------------------------------+
```

The Kdrive backend (Contract A) already exists —
[tinyx/kdrive/tima/](../../tinyx/kdrive/tima/). This document focuses on
Contracts B and C.

**Design goal**: zero `#include <linux/...>`, zero `#include <sys/...>` in
the final target binary. All OS interactions go through:

1. `hal_drivers.h` — display + input (already in place).
2. A new `os_api.h` — networking, file, time, memory syscalls.
3. Standard C headers (`<string.h>`, `<stdlib.h>`, `<stdio.h>`) — implemented
   by newlib against your syscalls.

---

## 2. Dependency Layers in TinyX

Where each kind of dependency lives in the tree:

| Layer                         | Directory                            | Linux‑specific? |
|-------------------------------|--------------------------------------|-----------------|
| Device‑independent X (DIX)    | [tinyx/dix/](../../tinyx/dix/)       | No              |
| Machine‑independent (MI)      | [tinyx/mi/](../../tinyx/mi/)         | No              |
| Framebuffer library           | [tinyx/fb/](../../tinyx/fb/)         | No              |
| Extensions                    | [tinyx/Xext/](../../tinyx/Xext/), `randr/`, `render/`, `xfixes/`, `damageext/` | No |
| Kdrive core                   | [tinyx/kdrive/src/](../../tinyx/kdrive/src/) | No |
| Kdrive backend (yours)        | [tinyx/kdrive/tima/](../../tinyx/kdrive/tima/) | No |
| **OS layer**                  | [tinyx/os/](../../tinyx/os/)         | **YES**         |
| Kdrive Linux input backend    | [tinyx/kdrive/linux/](../../tinyx/kdrive/linux/) | **YES** (evdev/console) — **don't compile it** |

The Linux dependency footprint is essentially: **`tinyx/os/` + `tinyx/kdrive/linux/`**.
Everything above them (DIX, MI, fb, extensions, Kdrive core) is portable C
with only libc/stdio requirements.

---

## 3. What Lives in `tinyx/os/`

Enumerated so you can see exactly what needs replacing/stubbing:

| File            | Purpose                                          | Portability                              |
|-----------------|--------------------------------------------------|------------------------------------------|
| `WaitFor.c`     | Main loop `select()` on client fds + timers      | **Rewrite** — see [§7](#7-timing-and-the-main-loop) |
| `connection.c`  | Accept + manage X client TCP/Unix connections    | **Replace** or gut — see [§6](#6-the-sockets--transport-surface) |
| `io.c`          | Read/write X protocol on client fds              | Portable if `read`/`writev` exist        |
| `access.c`      | Host‑based access control (`xhost`)              | **Stub** — allow local‑only              |
| `auth.c`, `mitauth.c`, `xdmauth.c`, `xauembed.c` | MIT‑MAGIC‑COOKIE, XDM auth | **Stub** — return "allow"                |
| `xdmcp.c`       | XDMCP protocol (remote login)                    | **Stub out entirely** (`#ifdef XDMCP` off) |
| `xstrans.c`     | Transport abstraction wrapper                    | **Replace** with your transport          |
| `oscolor.c`     | `/etc/X11/rgb.txt` parser                        | Keep, or replace with built‑in table     |
| `osinit.c`      | Global OS init (log file, umask, resource limits)| Simplify: drop rlimit, keep logging      |
| `log.c`         | `ErrorF`, `LogMessageVerb`, log file writing     | Portable if `fopen`/`vfprintf` exist     |
| `utils.c`       | `FatalError`, signal handlers, argv parsing      | Strip signal + `execv` bits              |
| `strlcpy.c`, `strlcat.c` | BSD string helpers                      | Fully portable — keep                    |
| `osdep.h`       | Central OS include hub                           | **Rewrite** as your `os_api.h` shim      |

**Rule of thumb**: everything under `tinyx/os/` is either OS‑integration glue
(portable with new syscalls) or network/security glue (mostly stubbed on a
single‑client embedded target).

---

## 4. The libc Surface TinyX Uses

TinyX assumes a C89/C99 libc with the following in scope. Details also in
[../archive_docs2/OS_LIBC_REQUIREMENTS.md](../archive_docs2/OS_LIBC_REQUIREMENTS.md).

### 4.1 Memory (`<stdlib.h>`, `<string.h>`)

`malloc`, `free`, `realloc`, `calloc`, `abort`, `exit`, `getenv` (optional),
`atoi`, `strtol`, `strtoul`, `qsort`, `bsearch`, `abs`.

`memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcpy`, `strncpy`,
`strcmp`, `strncmp`, `strcat`, `strncat`, `strchr`, `strrchr`, `strstr`,
`strdup`.

Heap sizing: budget ≥ 8 MB for a typical single‑display server + JWM; more if
you enable Render/Damage/RandR pixmaps.

### 4.2 Stdio (`<stdio.h>`)

`fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `feof`, `ferror`,
`fgets`, `fputs`, `printf`, `fprintf`, `vfprintf`, `sprintf`, `snprintf`,
`vsnprintf`, `perror`.

Only `fprintf(stderr, …)` / `printf` are used on the *hot path* (logging).
Everything else is startup: reading `rgb.txt`, config, and font files.

### 4.3 ctype / math

`isalpha`, `isdigit`, `isspace`, `isprint`, `isxdigit`, `toupper`, `tolower`.

Math (`sin`, `cos`, `sqrt`, `floor`, `ceil`) is only pulled in by the Render
extension's trapezoid code. Safe to leave out if you disable Render.

### 4.4 setjmp

`setjmp` / `longjmp` — used by `FatalError()` to unwind. Newlib provides
these unconditionally.

---

## 5. The POSIX Syscall Surface

Under libc, TinyX ultimately requires these syscalls. Split into three tiers.
The tiering below **assumes the in‑process transport architecture** from
[CLIENT_SERVER_TRANSPORT_REMOVAL.md](CLIENT_SERVER_TRANSPORT_REMOVAL.md) —
no sockets between client and server. If you instead keep the socket‑based
transport (hosted / development builds), also implement Tier 3 at the bottom.

### Tier 1 — Must implement (server won't start otherwise)

With no socket transport, the required syscall surface is very small.
Every entry below is called on the startup path; nothing here is optional.

| Syscall / libc entry             | Used for                                             |
|----------------------------------|------------------------------------------------------|
| `sbrk` (or `mmap`)               | Heap growth for `malloc` in libX11, TinyX, JWM       |
| `gettimeofday` / `clock_gettime` | Smart scheduler, screen saver, XKB repeat, JWM timing|
| `_write` for fd 1/2              | `ErrorF` / `printf` logging to the console/UART      |
| `_exit`                          | `FatalError()` termination                           |

Notes:

- **No `read` in Tier 1**. Previously required for client protocol bytes;
  in the in‑process build, requests arrive as direct function calls into
  `wmb_sync_handler` and never traverse a file descriptor.
- **No `close` in Tier 1**. Only meaningful when you have real fds; the
  in‑process transport uses `fd = -1` on every `Display`.
- **Monotonic time is mandatory**. `gettimeofday` must return values that
  never move backwards, or the smart scheduler mis‑behaves.
- **`_write` must be non‑blocking for the console**. If your console driver
  can block, the whole event loop stalls.

### Tier 2 — Needed if you keep filesystem access

Optional. Skip entirely if fonts, `rgb.txt`, and any config data are
embedded into the binary as C arrays (recommended for the target build
— see §9).

| Syscall             | Used for                                       |
|---------------------|------------------------------------------------|
| `open` / `openat`   | Fonts, `rgb.txt`, config                       |
| `read`              | Reading font / config data                     |
| `close`             | Closing the above                              |
| `lseek`             | Random access into font files                  |
| `fstat` / `stat`    | Font directory scanning                        |
| `opendir` / `readdir` / `closedir` | Font/config directory scans     |
| `access`            | Config file discovery                          |

### Tier 3 — Only for the legacy socket transport

**Not needed on the in‑process target.** Listed here for the hosted
development build that still runs external clients over sockets, and for
reference when comparing against upstream TinyX.

| Syscall             | Used for                                       |
|---------------------|------------------------------------------------|
| `socket`, `bind`, `listen`, `accept`, `connect` | Client transport     |
| `select` or `poll`  | `WaitFor()` in `tinyx/os/WaitFor.c`            |
| `setsockopt`, `getsockopt`, `getsockname`, `getpeername` | Transport config |
| `writev`, `readv`   | Batched protocol I/O                           |
| `fcntl` (F_SETFL)   | Non‑blocking mode                              |
| `sigaction`, `sigprocmask` | SIGPIPE / SIGCHLD handling             |

If your embedded target follows
[CLIENT_SERVER_TRANSPORT_REMOVAL.md](CLIENT_SERVER_TRANSPORT_REMOVAL.md)
(the intended path), Tier 3 is **eliminated entirely** — see [§10.C](#10-three-porting-strategies).

---

## 6. The Sockets / Transport Surface

TinyX accepts X protocol clients over sockets in `tinyx/os/connection.c` and
`tinyx/os/xstrans.c`, then does non‑blocking reads/writes in `io.c`.

**Recommended path for the target build**: eliminate the transport entirely.
The in‑process client/server bridge is documented separately in
[CLIENT_SERVER_TRANSPORT_REMOVAL.md](CLIENT_SERVER_TRANSPORT_REMOVAL.md). Under
that architecture, `libX11` calls the TinyX request dispatcher directly via
`SyncHandle` and a fixed‑address vtable — no sockets, no XCB, no `select()`.
This is the assumption behind the tier layout in §5.

For completeness, the three ways to satisfy the transport on a non‑Linux
target are:

**Option 1 — Provide BSD sockets.** Cleanest if your OS has (or can grow) a
sockets API. Reuse the code as‑is; just make sure `sys/socket.h`,
`netinet/in.h`, `sys/un.h`, `select()`/`poll()` and `sigaction` are available.

**Option 2 — Fake transport with in‑memory pipes.** Replace `xstrans.c` with
a shim that hands X clients running *in the same address space* a pair of
ring buffers instead of a socket. `io.c` then reads/writes those buffers.
Everything above stays untouched. Roughly 300 LOC.

**Option 3 — No transport at all.** Recommended for the target. See
[CLIENT_SERVER_TRANSPORT_REMOVAL.md](CLIENT_SERVER_TRANSPORT_REMOVAL.md) for
the full architecture and implementation plan (vtable, `SyncHandle` bridge,
event/reply routing, file list, phased roadmap). `WaitFor()` degenerates into
"call `pollEvents` + tick timers" (see §7).

---

## 7. Timing and the Main Loop

`tinyx/os/WaitFor.c` implements `WaitForSomething()` — the core dispatch
loop. It:

1. Computes the next timer deadline (via `smart_scheduler`, screen saver,
   XKB repeat).
2. Calls `select()` on all client fds + the listener fd, with that deadline.
3. On wake‑up, dispatches ready client fds and expired timers.
4. Calls the registered "block handlers" and "wakeup handlers" — this is
   where the **Kdrive backend's `pollEvents` runs**.

To port `WaitFor.c` to a custom OS you need **either**:

- A working `select()` / `poll()` implementation, **or**
- A rewrite that (a) polls Kdrive input via `KdOsFuncs.pollEvents`, (b) ticks
  a monotonic timer via `gettimeofday()`/`clock_gettime()`, and (c) reads any
  pending in‑process client buffers.

**Monotonic time is mandatory** — the smart scheduler compares deltas across
loop iterations. If your OS only has boot‑relative ticks, wrap them into a
`struct timeval` where `tv_sec` = ticks/HZ and `tv_usec` = (ticks%HZ)*(1e6/HZ).

Example minimal main loop for the "no sockets" target:

```c
void WaitForSomething(int *pClientsReady) {
    struct timeval now, deadline;
    long timeout_ms;

    /* 1. Run all block handlers (Kdrive pollEvents runs here) */
    BlockHandler(&timeout_ms, NULL);

    /* 2. Wait up to timeout_ms for the next timer or input event.
     *    On bare-metal this is typically a WFI or a short spin. */
    hal_sleep_ms(timeout_ms > 0 ? timeout_ms : 0);

    /* 3. Wake handlers dispatch pending input to X clients */
    WakeupHandler(0, NULL);

    /* 4. Report any in-process clients that produced protocol bytes */
    *pClientsReady = drain_inproc_clients();
}
```

---

## 8. Signals, Process, Environment

TinyX installs signal handlers for SIGINT/SIGTERM (clean shutdown), SIGPIPE
(client disconnect), SIGHUP (log rotation), SIGUSR1 (`-displayfd` signalling)
and SIGCHLD (reap children).

On a target without POSIX signals, **stub `signal()` to return `SIG_DFL`**
and make sure:

- `FatalError()` → calls your OS's abort/halt (`hal_panic()`).
- Ctrl+C in Kdrive input maps to a clean shutdown request (queue it and
  let the main loop check).

`fork`, `execv`, `wait`, `waitpid` — TinyX only calls these if built with
`-DHAS_LAUNCH_CLIENT` (running user commands from `-init` scripts). **Do not
define this macro** on your target; the calls disappear from the build.

`getenv("DISPLAY")`, `getenv("HOME")`, `getenv("XAUTHORITY")` — stub to
return `NULL`; TinyX has fallbacks for all of them.

---

## 9. Filesystem: What TinyX Reads at Runtime

TinyX will happily start without any of these files (fallbacks kick in), but
functionality degrades:

| Path                                         | Purpose                | Fallback                     |
|----------------------------------------------|------------------------|------------------------------|
| `/etc/X11/rgb.txt`                           | Color name → RGB       | Built‑in table (compile with `-DUSE_RGB_BUILTIN`) |
| `/etc/X11/XErrorDB`                          | Error message strings  | Generic messages             |
| `/usr/share/fonts/**/*.pcf`, `*.bdf`, `*.ttf`| Font glyphs            | Only built‑in `fixed` 6x13   |
| `~/.Xauthority`                              | Client auth cookies    | Local‑only, no auth          |

For a fully embedded target, either:

- Compile fonts and rgb.txt as C arrays and link them in (see
  [fonts/freetype/](../../fonts/freetype/) for the freetype path), **or**
- Provide a tiny in‑memory filesystem your `open`/`read` syscalls can serve
  from ROM.

---

## 10. Three Porting Strategies

Pick one based on how POSIX‑like your target OS is.

### A. "Almost POSIX" (has newlib + BSD sockets)

Effort: **small**. Approach:

- Cross‑compile with `riscv64-unknown-elf-gcc -specs=nosys.specs`.
- Provide newlib syscalls (see [§12](#12-newlib-syscalls-reference-skeleton)).
- Keep `tinyx/os/` mostly intact.
- Only change: `#undef XDMCP`, disable IPv6 in `connection.c` if no v6 stack.

### B. "Custom OS with threads and files, no sockets"

Effort: **medium**. Approach:

- Replace `xstrans.c` + strip `connection.c` down to a factory that hands out
  in‑memory pipe endpoints.
- Rewrite `WaitFor.c` around your OS's `select`‑equivalent on those pipes
  plus `KdOsFuncs.pollEvents`.
- Stub `access.c`, `auth.c`, `xdmcp.c`.

### C. "Single‑binary bare‑metal, X server + WM + apps all linked"

Effort: **larger up front, smallest at runtime**. Approach:

- No sockets at all. Custom Xlib in `libX11/` calls the dispatcher directly
  (see [../archive_docs2/CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md)).
- `WaitFor.c` reduced to the poll/tick loop shown in [§7](#7-timing-and-the-main-loop).
- `connection.c`, `xstrans.c`, `io.c`, `access.c`, `auth.c`, `xdmcp.c`,
  `xauembed.c`, `xdmauth.c`, `mitauth.c` — **not compiled**.
- Newlib provides libc; syscalls file only needs `_write` (log), `sbrk`
  (heap), and `_gettimeofday` (timers).

This is the strategy the archive docs recommend for the RISC‑V Tima OS target.

---

## 11. Step‑by‑Step Port to a Custom OS

### Step 1 — Isolate the OS abstraction

Create a single header, e.g. `framework/os_api.h`, that fronts every syscall
TinyX needs. Model it on `hal_drivers.h`:

```c
#ifndef _os_api_h_
#define _os_api_h_
#include "types.h"

/* Memory (backs malloc via newlib _sbrk) */
void *os_sbrk(intptr_t increment);

/* Time — used by the smart scheduler and screen saver */
int  os_gettimeofday(struct timeval *tv);
void os_usleep(uint32_t microseconds);

/* Logging — backs _write(1,..) and _write(2,..) */
int  os_console_write(const char *buf, int len);

/* Termination */
void os_abort(void);
void os_exit(int code);

/* Optional file API — omit if you don't do file I/O */
int  os_open (const char *path, int flags);
int  os_read (int fd, void *buf, int len);
int  os_write(int fd, const void *buf, int len);
int  os_close(int fd);
int  os_lseek(int fd, int offset, int whence);
int  os_stat (const char *path, void *statbuf);

#endif
```

Every implementation of `os_api.h` (Linux, Tima OS, QEMU‑virt, …) lives in
`framework/<os>_os/`. The libc syscall shim (§12) is a thin adapter over
these calls.

### Step 2 — Choose your strategy (§10) and gut `tinyx/os/`

For strategy C (bare‑metal single binary), the sources you keep are:

```
tinyx/os/log.c        # ErrorF, LogMessageVerb — uses fprintf
tinyx/os/utils.c      # FatalError, argv parsing — strip signal/exec bits
tinyx/os/osinit.c     # simplified: just call OsInitAllocator + open logfile
tinyx/os/oscolor.c    # keep, or replace with built-in rgb table
tinyx/os/strlcpy.c
tinyx/os/strlcat.c
tinyx/os/WaitFor.c    # rewritten to the loop in §7
```

Remove from the build: `connection.c`, `io.c`, `xstrans.c`, `access.c`,
`auth.c`, `mitauth.c`, `xdmcp.c`, `xdmauth.c`, `xauembed.c`.

### Step 3 — Kill the Linux Kdrive input backend

Do **not** compile `tinyx/kdrive/linux/{linux.c,keyboard.c,mouse.c}`. Your
`tinyx/kdrive/tima/` backend already handles input via `hal_drivers.h`.

### Step 4 — Add your libc

Two supported options:

- **Newlib** (`-specs=nosys.specs` + a real `syscalls.c` from §12). Standard,
  well‑tested, ~100 KB code.
- **Picolibc** — smaller, better suited to bare‑metal, same syscall shape.

Both give you `<stdio.h>`, `<string.h>`, `<stdlib.h>`, `<ctype.h>`,
`<setjmp.h>` for free.

### Step 5 — Write the syscalls shim (see §12)

### Step 6 — Wire it into CMake

See [framework/CMakeLists.txt](../../framework/CMakeLists.txt#L38-L94). Add:

```cmake
# OS syscalls shim
set(SOURCE_FILES ${SOURCE_FILES}
    ../framework/<os>_os/syscalls.c
    ../framework/<os>_os/os_time.c
)

# Trimmed TinyX OS layer
set(SOURCE_FILES ${SOURCE_FILES}
    ../tinyx/os/log.c
    ../tinyx/os/utils.c
    ../tinyx/os/osinit.c
    ../tinyx/os/oscolor.c
    ../tinyx/os/strlcpy.c
    ../tinyx/os/strlcat.c
    ../tinyx/os/WaitFor.c        # your rewritten version
)

# Build knobs to turn off unwanted subsystems
add_definitions(
    -DNO_LOCAL_CLIENT_CRED       # skip SO_PEERCRED
    -DNXAGENT                    # no server-side crypto/auth requirements
)
# Do NOT define: XDMCP, HAS_LAUNCH_CLIENT, HAS_GETPEEREID
```

### Step 7 — Build, link, boot

The link step must produce a binary whose only external references are the
symbols exported by your OS/HAL runtime. If you see references to
`__errno_location`, `fork`, `execve`, `socket` — go back and disable the
subsystem pulling them in.

---

## 12. Newlib Syscalls Reference Skeleton

### 12.1 Do the headers below add POSIX runtime dependencies?

**No.** The five `#include` lines used in the shim below come from the
**C library (newlib/picolibc)**, not from the OS. Including them is free —
they only declare types and constants; nothing is linked in.

| Header         | Provides                                              | Pulls in code? |
|----------------|-------------------------------------------------------|----------------|
| `<errno.h>`    | `errno` macro + error codes (`EBADF`, `ENOENT`, …)    | No — macros only          |
| `<sys/stat.h>` | `struct stat`, `S_IFCHR`, mode bits                   | No — types + macros       |
| `<sys/times.h>`| `struct tms`, `clock_t`                               | No — types                |
| `<sys/time.h>` | `struct timeval`, `struct timezone`                   | No — types                |
| `<unistd.h>`   | `_sbrk`/`_read`/`_write` prototypes, `off_t`, `intptr_t` | No — prototypes only  |

These headers live in the toolchain sysroot (e.g.
`.../riscv64-unknown-elf/include/`) and are available whenever newlib or
picolibc is present. **They do not require Linux, glibc, or any kernel
service.** What creates a runtime dependency is *calling* a function
declared in them — and since your syscall shim *defines* those functions
(`_open`, `_read`, `_stat`, …) rather than calling somebody else's copy,
the chain terminates at your code + `os_api.h`.

Two caveats:

- **Do not** include `<signal.h>`, `<sys/socket.h>`, `<netinet/in.h>`,
  `<sys/wait.h>`, `<sys/uio.h>`, or `<sys/resource.h>` in the target build.
  Those headers exist in newlib too, but the functions they declare
  (`sigaction`, `socket`, `waitpid`, `writev`, `setrlimit`) force additional
  syscalls into the link. §13 lists which TinyX files reach for them — keep
  those files out of the target build.
- On some newlib configs `struct timeval` requires `<time.h>` in addition to
  `<sys/time.h>`. Adding it is free.

If you are building strict freestanding (`-nostdinc`), you can drop the
`<sys/…>` headers entirely and forward‑declare only what you use:

```c
#include <stdint.h>
#include <stddef.h>

/* Zero-header shim — no dependency on any C library header */
struct timeval { long tv_sec; long tv_usec; };
struct stat    { unsigned int st_mode; /* ...pad... */ };
struct tms     { long tms_utime, tms_stime, tms_cutime, tms_cstime; };
typedef long clock_t;
extern int errno;
```

### 12.2 Reference shim

Drop‑in template for `framework/<os>_os/syscalls.c`. Wire each function to
`os_api.h`:

```c
#include <errno.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/time.h>
#include <unistd.h>
#include "os_api.h"

#undef errno
extern int errno;

/* --- Heap ------------------------------------------------------------ */
void *_sbrk(intptr_t incr)       { return os_sbrk(incr); }

/* --- Console I/O ----------------------------------------------------- */
int _write(int fd, char *buf, int len) {
    if (fd == 1 || fd == 2) return os_console_write(buf, len);
#ifdef ENABLE_FS
    return os_write(fd, buf, len);
#else
    errno = EBADF; return -1;
#endif
}

int _read(int fd, char *buf, int len) {
#ifdef ENABLE_FS
    return os_read(fd, buf, len);
#else
    return 0;                          /* EOF */
#endif
}

/* --- File I/O (stub if you have no FS) ------------------------------- */
int _open (const char *p, int f, ...) {
#ifdef ENABLE_FS
    return os_open(p, f);
#else
    errno = ENOENT; return -1;
#endif
}
int _close(int fd)                    { return os_close(fd); }
int _lseek(int fd, int o, int w)      { return os_lseek(fd, o, w); }
int _fstat(int fd, struct stat *st)   { st->st_mode = S_IFCHR; return 0; }
int _stat (const char *p, struct stat *st) {
#ifdef ENABLE_FS
    return os_stat(p, st);
#else
    errno = ENOENT; return -1;
#endif
}
int _isatty(int fd)                   { return fd <= 2; }

/* --- Process --------------------------------------------------------- */
void _exit(int c)                     { os_exit(c); for(;;); }
int  _kill (int pid, int sig)         { (void)pid; (void)sig; errno = EINVAL; return -1; }
int  _getpid(void)                    { return 1; }

/* --- Time (critical for main loop) ---------------------------------- */
int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    return os_gettimeofday(tv);
}
clock_t _times(struct tms *b) {
    struct timeval tv; os_gettimeofday(&tv);
    clock_t t = tv.tv_sec * CLOCKS_PER_SEC
              + (tv.tv_usec / (1000000 / CLOCKS_PER_SEC));
    if (b) { b->tms_utime = t; b->tms_stime = 0;
             b->tms_cutime = 0; b->tms_cstime = 0; }
    return t;
}

/* --- Unused but referenced ------------------------------------------ */
int _link  (const char *o, const char *n) { errno = EMLINK; return -1; }
int _unlink(const char *n)                { errno = ENOENT; return -1; }
int _wait  (int *st)                      { errno = ECHILD; return -1; }
int _fork  (void)                         { errno = EAGAIN; return -1; }
int _execve(const char *n, char *const a[], char *const e[])
                                          { errno = ENOMEM; return -1; }
```

Compile with `-specs=nosys.specs` **and** this file — the linker prefers your
strong symbols over newlib's weak stubs.

---

## 13. Removing Linux Dependencies — File‑by‑File

Grep‑verified list of files with Linux/POSIX headers today
(`tinyx/os/**` + `tinyx/kdrive/linux/**`):

| File                          | Bad includes                             | Action                                        |
|-------------------------------|------------------------------------------|-----------------------------------------------|
| `tinyx/os/utils.c`            | `<signal.h>`, `<sys/wait.h>`, `<sys/resource.h>`, `<sys/stat.h>` | Guard with `#ifdef HAVE_POSIX_SIGNALS`; drop rlimit code |
| `tinyx/os/osinit.c`           | `<sys/resource.h>`                       | Drop setrlimit; keep the rest                 |
| `tinyx/os/connection.c`       | `<signal.h>`, `<sys/socket.h>`, `<sys/uio.h>` | Not built in strategy C                  |
| `tinyx/os/io.c`               | `<sys/uio.h>`                            | Not built in strategy C                       |
| `tinyx/os/access.c`           | `<sys/socket.h>`, `<sys/ioctl.h>`, `<sys/types.h>` | Not built                            |
| `tinyx/os/xdmcp.c`            | `<sys/socket.h>`, `<netinet/in.h>`, `<sys/param.h>` | Not built                          |
| `tinyx/os/osdep.h`            | `<sys/param.h>`                          | Replace body with `#include "os_api.h"`       |
| `tinyx/os/strlcpy.c`, `strlcat.c` | `<sys/types.h>`                      | Trivial: use `stddef.h` size_t                |
| `tinyx/kdrive/linux/*.c`      | Everything                               | **Do not compile**                            |

After the edits, the entire target binary should compile with `-nostdinc` +
newlib's headers + `X11/` + `os_api.h` on the include path.

---

## 14. Build System Changes

Current hosted build lives in
[framework/CMakeLists.txt](../../framework/CMakeLists.txt). For a target
build, create a **sibling toolchain file** and gate OS sources on a variable:

```cmake
option(TINYX_TARGET_OS "Target OS: linux | tima | none" "linux")

if(TINYX_TARGET_OS STREQUAL "linux")
    # Original set: full tinyx/os/ + sockets
    file(GLOB TINYX_OS_SRCS ../tinyx/os/*.c)
    list(REMOVE_ITEM TINYX_OS_SRCS ../tinyx/os/xdmcp.c)   # if XDMCP off
elseif(TINYX_TARGET_OS STREQUAL "tima")
    # Strategy C — bare metal
    set(TINYX_OS_SRCS
        ../tinyx/os/log.c
        ../tinyx/os/utils.c
        ../tinyx/os/osinit.c
        ../tinyx/os/oscolor.c
        ../tinyx/os/strlcpy.c
        ../tinyx/os/strlcat.c
        ../framework/tima_os/WaitFor.c   # your rewrite
        ../framework/tima_os/syscalls.c  # newlib shim
        ../framework/tima_os/os_time.c
    )
    add_definitions(-DNXAGENT -DNO_LOCAL_CLIENT_CRED)
endif()

set(SOURCE_FILES ${SOURCE_FILES} ${TINYX_OS_SRCS})
```

Toolchain file for RISC‑V (`cmake/riscv-toolchain.cmake`):

```cmake
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  riscv64)
set(CMAKE_C_COMPILER        riscv64-unknown-elf-gcc)
set(CMAKE_CXX_COMPILER      riscv64-unknown-elf-g++)
set(CMAKE_C_FLAGS   "-march=rv64imac -mabi=lp64 -ffreestanding -O2")
set(CMAKE_EXE_LINKER_FLAGS  "-nostartfiles -specs=nosys.specs")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

Invoke:

```bash
cmake -B build_target \
      -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake \
      -DTINYX_TARGET_OS=tima
cmake --build build_target
```

---

## 15. Verification Checklist

Run through this list after each porting milestone.

**Link stage** (nothing should be undefined):

- [ ] `nm --undefined-only wmb.elf | c++filt` — every remaining symbol is
      exported by your OS/HAL runtime.
- [ ] No `fork`, `execve`, `pipe`, `socket`, `bind`, `accept`, `connect`,
      `select`, `poll`, `sigaction`, `mmap`, `ioctl` in the undefined list
      (unless your OS actually provides them).

**Boot stage** (server starts and stops cleanly):

- [ ] `_sbrk` returns aligned addresses, `malloc(1<<20)` succeeds.
- [ ] `gettimeofday()` returns monotonically increasing values.
- [ ] `ErrorF("hello\n")` appears on your console.
- [ ] `OsInit()` completes, then `KdInitOutput()` completes.

**Runtime stage** (X clients can draw):

- [ ] Kdrive `pollEvents` is called at least 30×/s (add a counter).
- [ ] A test client (in‑process, using your custom Xlib) can `XCreateWindow`,
      `XMapWindow`, `XFillRectangle`, and the pixels appear on the framebuffer.
- [ ] Keyboard events reach the client via `KeyPress` / `KeyRelease`.
- [ ] Mouse motion + buttons reach the client.

**Stability**:

- [ ] Server runs for ≥ 1 hour with no heap growth (`_sbrk` high‑water mark
      stable after warm‑up).
- [ ] Clean shutdown path frees allocated `KdCardInfo` / `KdScreenInfo`.

Once all boxes tick, TinyX is fully divorced from Linux and running on your
custom OS.

---

## Appendix — File Cross‑Reference

TinyX OS layer:
- [tinyx/os/WaitFor.c](../../tinyx/os/WaitFor.c) — main loop
- [tinyx/os/connection.c](../../tinyx/os/connection.c) — client accept
- [tinyx/os/io.c](../../tinyx/os/io.c) — protocol I/O
- [tinyx/os/xstrans.c](../../tinyx/os/xstrans.c) — transport wrapper
- [tinyx/os/access.c](../../tinyx/os/access.c) — host access control
- [tinyx/os/auth.c](../../tinyx/os/auth.c) — authorisation
- [tinyx/os/log.c](../../tinyx/os/log.c) — ErrorF / logging
- [tinyx/os/utils.c](../../tinyx/os/utils.c) — FatalError, argv, signals
- [tinyx/os/osinit.c](../../tinyx/os/osinit.c) — OS init
- [tinyx/os/osdep.h](../../tinyx/os/osdep.h) — OS include hub

Kdrive OS bridge:
- [tinyx/kdrive/src/kdrive.c](../../tinyx/kdrive/src/kdrive.c) — `KdOsInit`, `KdInitOutput`, `KdInitInput`
- [tinyx/kdrive/tima/tima_kdrive_system.c](../../tinyx/kdrive/tima/tima_kdrive_system.c) — `KdOsFuncs` + DDX externs

Linux Kdrive input (do not compile on target):
- [tinyx/kdrive/linux/linux.c](../../tinyx/kdrive/linux/linux.c)
- [tinyx/kdrive/linux/keyboard.c](../../tinyx/kdrive/linux/keyboard.c)
- [tinyx/kdrive/linux/mouse.c](../../tinyx/kdrive/linux/mouse.c)

Prior notes (superseded):
- [../archive_docs2/OS_LIBC_REQUIREMENTS.md](../archive_docs2/OS_LIBC_REQUIREMENTS.md)
- [../archive_docs/HAL_SPECIFICATION.md](../archive_docs/HAL_SPECIFICATION.md)
- [../archive_docs2/CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md)
- [../archive_docs2/XCB_STUB_ARCHITECTURE.md](../archive_docs2/XCB_STUB_ARCHITECTURE.md)
