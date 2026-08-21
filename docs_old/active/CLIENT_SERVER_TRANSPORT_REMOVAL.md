# Removing the X11 Socket Transport — In‑Process Client/Server Bridge

**Last updated**: 2026-07-13
**Scope**: Architecture and implementation plan for collapsing the X11
client/server transport so that an application linked against `libX11` calls
directly into the TinyX server code in the same address space. No sockets,
no XCB, no protocol serialization. The whole windowing system ships as **one
binary** with a fixed‑address vtable that apps use to reach `libX11`.

**Related docs**:
- [KDRIVE_CREATION_GUIDE.md](KDRIVE_CREATION_GUIDE.md) — display + input HAL.
- [TINYX_OS_PORTING_GUIDE.md](TINYX_OS_PORTING_GUIDE.md) — libc/OS layer; updated
  in tandem with this document.
- Prior notes (kept for reference, superseded by this file):
  [../archive_docs2/CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md),
  [../archive_docs2/XCB_STUB_ARCHITECTURE.md](../archive_docs2/XCB_STUB_ARCHITECTURE.md),
  [../archive_docs2/X11_CLIENT_SERVER_ANALYSIS.md](../archive_docs2/X11_CLIENT_SERVER_ANALYSIS.md),
  [../archive_docs2/XCB_DEPENDENCY_RISK_ASSESSMENT.md](../archive_docs2/XCB_DEPENDENCY_RISK_ASSESSMENT.md),
  [../archive_docs2/WMB_API_DESIGN.md](../archive_docs2/WMB_API_DESIGN.md).

---

## Table of Contents

1. [Motivation and Goal](#1-motivation-and-goal)
2. [Baseline: How X11 Talks Today](#2-baseline-how-x11-talks-today)
3. [Target: In‑Process Architecture](#3-target-inprocess-architecture)
4. [Two‑Block Design (App Wrapper + WMB Vtable)](#4-twoblock-design-app-wrapper--wmb-vtable)
5. [The `wmb_api_t` Fixed‑Address Vtable](#5-the-wmb_api_t-fixedaddress-vtable)
6. [The SyncHandle Bridge — Where Requests Cross Over](#6-the-synchandle-bridge--where-requests-cross-over)
7. [Files to Add, Modify, and Delete](#7-files-to-add-modify-and-delete)
8. [Event Delivery Path (Server → Client)](#8-event-delivery-path-server--client)
9. [Reply Path (Server → Client)](#9-reply-path-server--client)
10. [Implementation Plan — Phased Roadmap](#10-implementation-plan--phased-roadmap)
11. [Impact on `tinyx/os/`](#11-impact-on-tinyxos)
12. [Impact on libc/Syscalls (updates §5 of the Porting Guide)](#12-impact-on-libcsyscalls-updates-5-of-the-porting-guide)
13. [Testing Strategy](#13-testing-strategy)
14. [Open Questions / Risks](#14-open-questions--risks)

---

## 1. Motivation and Goal

The project's stated goal (see the archive docs and
[../active/IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)) is:

> One single binary containing X server + window manager + client
> applications, running on bare‑metal RISC‑V (Tima OS).

A standard X11 stack achieves client/server separation with:
- **Sockets** (`AF_UNIX`, `AF_INET`, TCP), plus
- **XCB** protocol encoding on the client, and
- A **request/reply/event queue** on both sides.

On a single‑binary target this is dead weight. Every socket read is a
`memcpy` from one region of RAM to another; every protocol byte is encoded
just to be decoded 3 µs later. It also imports POSIX sockets, `select()`,
non‑blocking I/O, and `SIGPIPE` handling — all of which we spent effort
removing in the OS porting guide.

**Goal**: replace the transport with a **direct function call across a
fixed‑address vtable**. The application still writes `XCreateWindow(dpy, …)`;
under the hood, that call reaches the TinyX request dispatcher in the same
address space, in the same thread, with the same request buffer.

Success criteria:

- Zero application‑source changes. `#include <X11/Xlib.h>` still works.
- Zero socket / XCB dependency in the final binary.
- Deterministic latency: every request completes before its API function
  returns.
- Apps ship as thin binaries; the whole `libX11` implementation lives in the
  WMB (Window Manager Binary) at a known address.

---

## 2. Baseline: How X11 Talks Today

Before we tear anything out, understand the pattern that every one of the
~200 API entry points in `libX11/src/*.c` follows:

```c
int XSomeRequest(Display *dpy, /* args */)
{
    LockDisplay(dpy);
    GetReq(SomeRequest, req);      /* macro → _XGetRequest(dpy, X_SomeRequest, sz) */
    req->field1 = arg1;
    req->field2 = arg2;
    /* optional: Data(dpy, buf, len) — appends variable payload */
    UnlockDisplay(dpy);
    SyncHandle();                  /* macro → if (dpy->synchandler) dpy->synchandler(dpy) */
    return 1;
}
```

For synchronous requests (need a reply) there is an extra step:

```c
Status XGetSomething(Display *dpy, /* args */, ReplyType *out)
{
    LockDisplay(dpy);
    GetReq(GetSomething, req);
    req->win = w;
    if (!_XReply(dpy, (xReply*)&rep, 0, xTrue)) { UnlockDisplay(dpy); return 0; }
    /* _XRead() to pull variable payload of the reply */
    UnlockDisplay(dpy);
    SyncHandle();                  /* now a no‑op */
    return 1;
}
```

The five hook functions where "transport" lives are:

| Symbol           | Standard implementation | Role                               |
|------------------|-------------------------|------------------------------------|
| `_XGetRequest`   | `xcb_io.c`              | Allocate space in `dpy->buffer`    |
| `_XSend`         | `xcb_io.c`              | Push `dpy->buffer` onto the socket |
| `_XFlush`        | `xcb_io.c`              | Force flush of pending bytes       |
| `_XReply`        | `xcb_io.c`              | Wait on socket for a reply         |
| `_XReadEvents`   | `xcb_io.c`              | Read events from socket into queue |

If we replace those five functions and set `dpy->synchandler` to our own
callback, every existing `libX11/src/*.c` file works untouched.

On the **server** side, TinyX has a matching dispatch table:

```c
extern int (*ProcVector[256])(ClientPtr);   /* tinyx/dix/tables.c */
```

The server's normal socket path calls `Dispatch()`, which calls
`ProcVector[client->requestBuffer[0]](client)`. We are going to call it
directly.

---

## 3. Target: In‑Process Architecture

```
+------------------------------------------------------------------+
| APPLICATION (bare-metal task or thread)                          |
|                                                                  |
|   XCreateWindow(dpy, parent, x, y, ...);                         |
+------------------------------+-----------------------------------+
                               |
                               | direct function call
                               v
+------------------------------------------------------------------+
| libX11-app.a   —   Thin wrappers linked into every app           |
|   XCreateWindow(...) {                                           |
|       return wmb_api->XCreateWindow(dpy, parent, x, y, ...);     |
|   }                                                              |
+------------------------------+-----------------------------------+
                               |
                               | Read fixed vtable address,
                               | jump into WMB code
                               v
+------------------------------------------------------------------+
| WMB BINARY  (fixed address, contains everything below)           |
|                                                                  |
|   wmb_api_t at 0x80100000 --- fixed vtable ---                   |
|             |                                                    |
|             v                                                    |
|   libX11 API implementation (unchanged upstream files)           |
|     CrWindow.c, MapWindow.c, DrLine.c, XNextEvent.c, ...         |
|             |                                                    |
|             | Fill dpy->buffer with request bytes                |
|             | Call SyncHandle()                                  |
|             v                                                    |
|   WMB transport shim (replaces xcb_io.c)                         |
|     _XGetRequest / _XSend=NOP / _XReply / _XFlush /              |
|     _XReadEvents / wmb_sync_handler                              |
|             |                                                    |
|             | dispatch: ProcVector[opcode](internalClient)       |
|             v                                                    |
|   TinyX server (dix / mi / fb / render / kdrive)                 |
|             |                                                    |
|             v                                                    |
|   Kdrive backend  ->  hal_drivers.h  ->  framebuffer + input     |
+------------------------------------------------------------------+
```

Key structural points:

- Only **one** `ClientPtr` exists per app: the "internal client" created at
  `XOpenDisplay` time. All requests from that app carry that pointer.
- The request buffer is `dpy->buffer` itself. The server reads it in place.
- `Display *dpy` is a real `libX11` `Display` struct, but its `fd = -1` and
  its `synchandler` is our function.
- Multiple in‑process apps just create multiple `Display` objects, each
  with their own `ClientRec`.

---

## 4. Two‑Block Design (App Wrapper + WMB Vtable)

Following [../archive_docs2/CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md):

**Block 1 — `libX11-app.a`** (thin, links into every app, ~10‑20 KB)

Contains **only wrappers**. Each wrapper reads the fixed‑address vtable and
jumps into WMB.

```c
/* libX11-app/src/wrappers.c */
#include <X11/Xlib.h>
#include "wmb_api.h"

Display *XOpenDisplay(const char *n) { return WMB_API->XOpenDisplay(n); }
Window   XCreateSimpleWindow(Display *d, Window p, int x, int y,
                             unsigned w, unsigned h, unsigned b,
                             unsigned long bd, unsigned long bg)
{
    return WMB_API->XCreateSimpleWindow(d, p, x, y, w, h, b, bd, bg);
}
int      XMapWindow(Display *d, Window w) { return WMB_API->XMapWindow(d, w); }
/* ... one wrapper per Xlib symbol ... */
```

**Block 2 — WMB binary** contains:

- The **entry table** at a fixed address (a `.wmb_api` section placed by
  the linker script).
- The **full `libX11` implementation** compiled from `libX11/src/` with
  `xcb_io.c` replaced by `wmb_transport.c`.
- The **TinyX server** (dix, mi, fb, kdrive, extensions).
- The **window manager** (JWM), also compiled as a normal client of the
  in‑process libX11.
- The **HAL + Kdrive backend** documented in
  [KDRIVE_CREATION_GUIDE.md](KDRIVE_CREATION_GUIDE.md).

An app's link line becomes:

```
gcc app.o libX11-app.a  -Ttext=<app_base>  -o app.elf
```

The WMB is linked separately, once, with the vtable pinned:

```
gcc wmb_objs... libX11.a libtinyx.a libjwm.a  -T wmb.ld  -o wmb.elf
```

Both live in the same address space at runtime — the loader (or the boot
image builder) places the WMB such that `wmb_api_table` lands at exactly
`WMB_API_ADDRESS`.

---

## 5. The `wmb_api_t` Fixed‑Address Vtable

Header shared by apps and WMB:

```c
/* wmb_api.h */
#ifndef _wmb_api_h_
#define _wmb_api_h_
#include <X11/Xlib.h>

/* Every Xlib symbol used by JWM/apps has a slot here.               */
/* Add/remove entries by copying prototype from Xlib.h — no changes  */
/* needed inside the WMB when a slot is unused.                      */
typedef struct wmb_api_s {
    /* --- Display lifecycle --- */
    Display *(*XOpenDisplay)(const char *);
    int      (*XCloseDisplay)(Display *);
    int      (*XFlush)(Display *);
    int      (*XSync)(Display *, Bool);

    /* --- Windows --- */
    Window   (*XCreateSimpleWindow)(Display *, Window, int, int,
                                    unsigned, unsigned, unsigned,
                                    unsigned long, unsigned long);
    Window   (*XCreateWindow)(Display *, Window, int, int,
                              unsigned, unsigned, unsigned, int, unsigned,
                              Visual *, unsigned long, XSetWindowAttributes *);
    int      (*XMapWindow)(Display *, Window);
    int      (*XUnmapWindow)(Display *, Window);
    int      (*XDestroyWindow)(Display *, Window);
    int      (*XMoveWindow)(Display *, Window, int, int);
    int      (*XResizeWindow)(Display *, Window, unsigned, unsigned);
    int      (*XConfigureWindow)(Display *, Window, unsigned int,
                                 XWindowChanges *);
    int      (*XRaiseWindow)(Display *, Window);
    int      (*XLowerWindow)(Display *, Window);

    /* --- Graphics context --- */
    GC       (*XCreateGC)(Display *, Drawable, unsigned long, XGCValues *);
    int      (*XFreeGC)(Display *, GC);
    int      (*XSetForeground)(Display *, GC, unsigned long);
    int      (*XSetBackground)(Display *, GC, unsigned long);

    /* --- Drawing --- */
    int      (*XDrawLine)(Display *, Drawable, GC, int, int, int, int);
    int      (*XDrawRectangle)(Display *, Drawable, GC, int, int,
                               unsigned, unsigned);
    int      (*XFillRectangle)(Display *, Drawable, GC, int, int,
                               unsigned, unsigned);
    int      (*XDrawString)(Display *, Drawable, GC, int, int,
                            const char *, int);
    int      (*XCopyArea)(Display *, Drawable, Drawable, GC, int, int,
                          unsigned, unsigned, int, int);

    /* --- Events --- */
    int      (*XSelectInput)(Display *, Window, long);
    int      (*XNextEvent)(Display *, XEvent *);
    Bool     (*XCheckMaskEvent)(Display *, long, XEvent *);
    int      (*XPending)(Display *);

    /* --- Atoms & properties --- */
    Atom     (*XInternAtom)(Display *, const char *, Bool);
    int      (*XChangeProperty)(Display *, Window, Atom, Atom, int, int,
                                const unsigned char *, int);
    int      (*XGetWindowProperty)(Display *, Window, Atom, long, long,
                                   Bool, Atom, Atom *, int *,
                                   unsigned long *, unsigned long *,
                                   unsigned char **);

    /* --- Pixmap / colormap / cursor / font (add as needed) --- */
    Pixmap   (*XCreatePixmap)(Display *, Drawable, unsigned, unsigned,
                              unsigned);
    int      (*XFreePixmap)(Display *, Pixmap);
    Cursor   (*XCreateFontCursor)(Display *, unsigned);
    XFontStruct *(*XLoadQueryFont)(Display *, const char *);
    int      (*XSetFont)(Display *, GC, Font);

    /* --- Utility --- */
    int      (*XFree)(void *);

    /* Extension entry points can be tacked on below. The struct is    */
    /* versioned via .version so apps can query capabilities.          */
    uint32_t version;   /* bump every schema change; check on init */
    uint32_t reserved;
} wmb_api_t;

/* Fixed load address (matches linker script) */
#define WMB_API_ADDRESS ((uintptr_t)0x80100000)
#define WMB_API         ((const wmb_api_t *)WMB_API_ADDRESS)

/* Sanity check apps should run at start */
static inline int wmb_api_check(uint32_t expected_version) {
    return WMB_API->version == expected_version;
}

#endif /* _wmb_api_h_ */
```

**Vtable characteristics**:

- **Fixed address, fixed layout**. Apps built against version *N* still work
  as long as WMB exposes *≥ N* slots and never reorders existing ones.
- **Function pointers only** — no data. Apps do not read WMB state
  directly; they only call through it.
- **`version` field** so apps can refuse to run against an incompatible WMB.
- **One vtable, many callers**. Concurrent in‑process apps read the same
  pointers; they never mutate the table.

Linker script fragment for the WMB:

```ld
MEMORY {
    ROM (rx)  : ORIGIN = 0x80000000, LENGTH = 8M
    API (r)   : ORIGIN = 0x80100000, LENGTH =  4K
    RAM (rwx) : ORIGIN = 0x80200000, LENGTH = 64M
}

SECTIONS {
    .text : { *(.text*) } > ROM
    .wmb_api 0x80100000 : { KEEP(*(.wmb_api)) } > API
    .rodata : { *(.rodata*) } > ROM
    .data : { *(.data*) } > RAM
    .bss  : { *(.bss*) }  > RAM
}
```

And the WMB‑side table:

```c
/* wmb_api_table.c — in the WMB */
#include "wmb_api.h"

__attribute__((section(".wmb_api"), used))
const wmb_api_t wmb_api_table = {
    .XOpenDisplay        = XOpenDisplay,
    .XCloseDisplay       = XCloseDisplay,
    .XFlush              = XFlush,
    .XSync               = XSync,
    .XCreateSimpleWindow = XCreateSimpleWindow,
    /* ... etc ... */
    .version             = WMB_API_VERSION,
};
```

---

## 6. The SyncHandle Bridge — Where Requests Cross Over

This is the mechanism that makes the whole thing work with **zero changes to
the 200+ files in `libX11/src/`**.

Every Xlib API function ends with `SyncHandle()`, which expands to:

```c
#define SyncHandle() if (dpy->synchandler) (*dpy->synchandler)(dpy)
```

At `XOpenDisplay` time we set `dpy->synchandler = wmb_sync_handler`.
`wmb_sync_handler` looks at the pending opcode we stashed in `_XGetRequest`,
adjusts the request length in the header, and calls the server's
`ProcVector[opcode](internal_client)` directly.

Full replacement for `xcb_io.c`:

```c
/* wmb_transport.c — replaces libX11/src/xcb_io.c inside the WMB */
#include "Xlibint.h"
#include "dispatch.h"    /* ProcVector */
#include "dixstruct.h"   /* ClientRec  */
#include "wmb_internal.h"

/* Per-Display state — one WmbDpy per XOpenDisplay */
typedef struct {
    ClientRec *client;         /* server-side identity for this display */
    CARD8      pending_opcode; /* opcode set by _XGetRequest, cleared on dispatch */
    char      *reply;          /* last reply, from server                  */
    size_t     reply_size;
    size_t     reply_pos;
} WmbDpy;

/* Retrieved via dpy->xcb (repurposed) or via dpy->ext_data — TBD */
static inline WmbDpy *wmb(Display *dpy) { return (WmbDpy *)dpy->xcb; }

/* --- request allocation ------------------------------------------ */
void *_XGetRequest(Display *dpy, CARD8 opcode, size_t len)
{
    WmbDpy *w = wmb(dpy);

    if (dpy->buffer == NULL || (size_t)(dpy->bufmax - dpy->buffer) < len) {
        size_t nsz = (len > 4096) ? len : 4096;
        char *nb = Xrealloc(dpy->buffer, nsz);
        if (!nb) return NULL;
        dpy->buffer = nb;
        dpy->bufmax = nb + nsz;
    }
    dpy->bufptr = dpy->buffer;
    memset(dpy->buffer, 0, len);
    ((xReq *)dpy->buffer)->reqType = opcode;
    ((xReq *)dpy->buffer)->length  = len >> 2;
    dpy->bufptr = dpy->buffer + len;
    w->pending_opcode = opcode;
    return dpy->buffer;
}

/* --- the actual bridge ------------------------------------------- */
static int wmb_dispatch(Display *dpy)
{
    WmbDpy *w = wmb(dpy);
    if (!w->pending_opcode) return Success;
    size_t sz = dpy->bufptr - dpy->buffer;
    ((xReq *)dpy->buffer)->length = (sz + 3) >> 2;
    w->client->requestBuffer = dpy->buffer;
    int r = (*w->client->requestVector[w->pending_opcode])(w->client);
    w->pending_opcode = 0;
    dpy->bufptr = dpy->buffer;
    return r;
}

/* Set as dpy->synchandler at XOpenDisplay time */
int wmb_sync_handler(Display *dpy) { return wmb_dispatch(dpy); }

/* --- reply path -------------------------------------------------- */
Status _XReply(Display *dpy, xReply *rep, int extra, Bool discard)
{
    WmbDpy *w = wmb(dpy);
    if (wmb_dispatch(dpy) != Success) return 0;
    if (w->reply && rep) {
        memcpy(rep, w->reply, sizeof(xReply));
        w->reply_pos = sizeof(xReply);
    }
    return 1;
}

int _XRead(Display *dpy, char *data, long size)
{
    WmbDpy *w = wmb(dpy);
    if (!w->reply) return -1;
    long avail = (long)(w->reply_size - w->reply_pos);
    if (size > avail) size = avail;
    memcpy(data, w->reply + w->reply_pos, size);
    w->reply_pos += size;
    return 0;
}
void _XReadPad(Display *dpy, char *data, long size) { _XRead(dpy, data, size); }

/* --- no-op transport primitives ---------------------------------- */
void _XSend (Display *dpy, const char *d, long n) { /* Data() resizes buffer instead */ (void)dpy; (void)d; (void)n; }
void _XFlush(Display *dpy) { if (wmb(dpy)->pending_opcode) wmb_dispatch(dpy); }

/* --- event delivery: see §8 -------------------------------------- */
```

That's the whole shim — a few hundred lines including error paths. No
sockets, no XCB, no non‑blocking I/O.

There is **one** required change to `Xlibint.h`: the `Data()` macro must
resize `dpy->buffer` on overflow rather than calling `_XSend`. See
[../archive_docs2/XCB_STUB_ARCHITECTURE.md §2.4](../archive_docs2/XCB_STUB_ARCHITECTURE.md)
for the exact one‑line change.

---

## 7. Files to Add, Modify, and Delete

### Add (new files)

| File                                | Purpose                                              |
|-------------------------------------|------------------------------------------------------|
| `libX11/src/wmb_transport.c`        | Replaces `xcb_io.c` (§6)                             |
| `libX11/src/wmb_open.c`             | Replaces `OpenDis.c` — creates in‑process client     |
| `libX11/src/wmb_close.c`            | Replaces `ClDisplay.c` — releases the client         |
| `libX11/include/wmb_api.h`          | Vtable definition (shipped to apps too)              |
| `libX11/src/wmb_api_table.c`        | Vtable instance placed in `.wmb_api`                 |
| `libX11-app/src/wrappers.c`         | Thin app‑side wrappers (§4)                          |
| `libX11-app/wmb.ld` (or excerpt)    | Linker script for the WMB binary                     |
| `framework/wmb_bridge/wmb_client.c` | Server‑side helper to spawn an internal `ClientRec`  |

### Modify

| File                          | Change                                                          |
|-------------------------------|-----------------------------------------------------------------|
| `libX11/include/Xlibint.h`    | `Data()` macro: on overflow, call `_XBufferOverflow(dpy, n)`    |
| `libX11/src/xcb_disp.c`       | Reduce to `xcb_get_setup()` returning our static setup blob     |
| `libX11/src/xcb_lock.c`       | Stub — locks become no‑ops in single‑thread WMB                 |
| `libX11/Makefile.am` / CMake  | Compile `wmb_transport.c` instead of `xcb_io.c`                 |
| `tinyx/os/WaitFor.c`          | Rewritten (see §11)                                             |

### Delete from the build

| File                                  | Reason                        |
|---------------------------------------|-------------------------------|
| `libX11/src/xcb_io.c`                 | Replaced by `wmb_transport.c` |
| `libX11/src/xcb_conn.c`               | No socket connection          |
| `libX11/src/xcb_xlib.c`               | No cross‑lock with XCB        |
| `tinyx/os/connection.c`               | No client accept              |
| `tinyx/os/io.c`                       | No socket I/O                 |
| `tinyx/os/xstrans.c`                  | No transport wrapper          |
| `tinyx/os/access.c`                   | No host access control        |
| `tinyx/os/auth.c` / `mitauth.c` / `xdmauth.c` / `xauembed.c` | Auth irrelevant local‑only |
| `tinyx/os/xdmcp.c`                    | No remote login               |

---

## 8. Event Delivery Path (Server → Client)

The server generates events (input, expose, focus, …) via
`WriteEventsToClient(client, count, evs)`. In the socket path this eventually
writes bytes onto the client's socket; in the WMB path we instead **enqueue
directly into the client's `Display` event queue**.

Approach:

- The internal client's `ClientRec` gets a small back‑pointer to its
  `Display *`:

  ```c
  typedef struct {
      ClientRec  rec;         /* dix expects this at offset 0    */
      Display   *dpy;         /* our extension                   */
  } WmbClientRec;
  ```

- Override the client's `ops->pushMessages` / `WriteToClient` (`tinyx/os/io.c`
  callback) with a WMB version that:

  ```c
  int wmb_WriteToClient(ClientPtr c, int count, const void *buf)
  {
      WmbClientRec *wc = (WmbClientRec *)c;
      /* Distinguish reply vs event by first byte or by a per-client state */
      if (buf_is_event(buf)) {
          _XEnq(wc->dpy, (xEvent *)buf);      /* enters the app's queue */
      } else {
          wmb(wc->dpy)->reply      = (char *)buf;
          wmb(wc->dpy)->reply_size = count;
          wmb(wc->dpy)->reply_pos  = 0;
      }
      return count;
  }
  ```

- `XNextEvent` / `XPending` on the app side already read from `dpy->head` /
  `dpy->tail`. No changes needed there.

- **Input events** flow: HAL → `KdOsFuncs.pollEvents` →
  `KdEnqueueMouseEvent` → dix → `WriteEventsToClient` → `wmb_WriteToClient`
  → `_XEnq(dpy, …)` → app's `XNextEvent` sees the event.

---

## 9. Reply Path (Server → Client)

Synchronous requests (`XGetWindowAttributes`, `XInternAtom`, …) call
`_XReply` which in the WMB shim:

1. Calls `wmb_dispatch(dpy)` (§6) — the server's `Proc*` writes the reply
   into the client via the same `wmb_WriteToClient` path.
2. `wmb_WriteToClient` recognises "this is a reply" and stashes it in
   `WmbDpy::reply` / `reply_size`.
3. `_XReply` `memcpy`s the fixed portion out; `_XRead` / `_XReadPad`
   subsequently pull the variable tail.

No blocking. No timeouts.

---

## 10. Implementation Plan — Phased Roadmap

Each phase is independently testable.

### Phase 0 — Preparation (no code churn)

- Freeze the current hosted (SDL) build so we can regression‑test against it.
- Capture a working `xtrace` log of `JWM` running against stock TinyX for
  later byte‑level comparison.

### Phase 1 — Vtable & wrappers only (host build)

- Define `wmb_api.h`.
- Build `libX11-app.a` (wrappers) linking against the *current* `libX11.so`
  as a placeholder for `WMB_API` — proves the wrapper mechanism is source‑
  transparent for apps.

### Phase 2 — SyncHandle shim on hosted build

- Add `wmb_transport.c`, `wmb_open.c`, `wmb_close.c` to a new `libX11-wmb`
  built alongside the existing `libX11`.
- Set `dpy->synchandler = wmb_sync_handler` in the new `XOpenDisplay`.
- Register the app as an internal `ClientRec` inside the same process.
- Run a tiny test client that only calls fire‑and‑forget requests
  (`XCreateSimpleWindow`, `XMapWindow`, `XFillRectangle`). Verify pixels
  appear on the framebuffer.

### Phase 3 — Reply path

- Implement `_XReply`, `_XRead`, `_XReadPad`.
- Hook `wmb_WriteToClient` to distinguish reply vs event.
- Run `XInternAtom`, `XGetWindowAttributes`, `XQueryTree` from the test client.

### Phase 4 — Event path

- Route `KdEnqueueMouseEvent` / `KdEnqueueKeyboardEvent` all the way to
  `_XEnq(dpy, …)`.
- Run `xev` (built with `libX11-app.a` + our wrappers) — visually confirm
  motion / key events.

### Phase 5 — JWM integration

- Compile JWM against `libX11-app.a`. Everything except link addresses
  should be identical.
- Bring up JWM inside the WMB process. Verify window frames, menus,
  focus.

### Phase 6 — Strip the transport out of the build

- Remove sources from §7 "Delete" list.
- Rewrite `WaitFor.c` per §11 (or apply the version already in
  [TINYX_OS_PORTING_GUIDE.md §7](TINYX_OS_PORTING_GUIDE.md)).
- Rebuild; verify with `nm --undefined-only` that no socket / select / signal
  symbols remain.

### Phase 7 — Cross‑compile for target

- Switch to the RISC‑V toolchain, apply the linker script (§5).
- Place `wmb_api_table` at `WMB_API_ADDRESS`; boot on hardware or QEMU.

### Phase 8 — Multi‑client, extensions, robustness

- Register multiple internal clients (JWM + other apps).
- Test Render / Damage / RandR / XFixes extensions across the boundary.
- Add fault handling: what happens if `Proc*` returns an error? — must
  synthesize a proper `xError` reply through the same reply path.

---

## 11. Impact on `tinyx/os/`

With no client sockets and no `select()`, the OS layer collapses to almost
nothing. Compared to
[TINYX_OS_PORTING_GUIDE.md §3](TINYX_OS_PORTING_GUIDE.md#3-what-lives-in-tinyxos):

| File                | Fate in the in‑process build                       |
|---------------------|----------------------------------------------------|
| `WaitFor.c`         | **Rewrite** — see below                            |
| `connection.c`      | **Delete**                                         |
| `io.c`              | **Delete** — replaced by `wmb_WriteToClient`       |
| `xstrans.c`         | **Delete**                                         |
| `access.c`          | **Delete**                                         |
| `auth.c` / `mitauth.c` / `xdmauth.c` / `xauembed.c` | **Delete**       |
| `xdmcp.c`           | **Delete**                                         |
| `oscolor.c`         | Keep (or replace with built‑in RGB table)          |
| `osinit.c`          | Keep, simplified                                   |
| `log.c`             | Keep                                               |
| `utils.c`           | Keep, without signal handlers                      |
| `strlcpy.c` / `strlcat.c` | Keep                                         |

`WaitFor.c` — the *minimum viable* main loop when there is no socket:

```c
/* framework/wmb_bridge/wmb_waitfor.c */
#include "os.h"
#include "opaque.h"
#include "hal_drivers.h"

void WaitForSomething(int *pClientsReady)
{
    long timeout_ms = 0;

    /* 1. Block handlers — Kdrive pollEvents runs here, drains HAL       */
    BlockHandler(&timeout_ms, NULL);

    /* 2. Timer / screen-saver work                                      */
    if (SmartScheduleTime) SmartScheduleTime = GetTimeInMillis();

    /* 3. Yield to HAL (idle) if we have nothing to do                   */
    if (timeout_ms > 0) hal_sleep_ms(timeout_ms);

    /* 4. Wake handlers — dispatch enqueued input to the right Displays  */
    WakeupHandler(0, NULL);

    /* 5. There are no external clients. Report zero ready.              */
    *pClientsReady = 0;
}
```

The DIX main loop still calls `Dispatch()` occasionally to flush pending
work, but since all in‑process client requests are already executed
synchronously by `wmb_sync_handler`, `Dispatch()` finds an empty request
queue and returns immediately.

---

## 12. Impact on libc/Syscalls (updates §5 of the Porting Guide)

With the socket transport gone, **Tier 3** of
[TINYX_OS_PORTING_GUIDE.md §5](TINYX_OS_PORTING_GUIDE.md) disappears in its
entirety, and **Tier 1** shrinks. New tier layout for the in‑process build:

**Tier 1 — Must implement (server won't start otherwise)**

| Syscall / libc entry             | Used for                                        |
|----------------------------------|-------------------------------------------------|
| `sbrk` / `mmap`                  | Heap for `malloc` in libX11, TinyX, JWM         |
| `gettimeofday` / `clock_gettime` | Smart scheduler, screen saver, XKB repeat, JWM  |
| `_write` to fd 1/2               | `ErrorF` / `printf` logging                     |
| `_exit`                          | `FatalError()` shutdown                         |

That's it. `read`, `close`, `select`, `poll`, `socket`, `sigaction`,
`writev`, `fcntl` — **none required** on the in‑process target.

**Tier 2** (filesystem access) is unchanged from the porting guide — still
optional and only needed if fonts / config live on a real FS instead of ROM.

**Tier 3** (sockets / networking) is **removed** for this architecture.

The porting guide document is updated to reflect this change; see
§11 there.

---

## 13. Testing Strategy

Because the shim is small (~500 LOC) and every request goes through it,
tests can be layered:

1. **Byte‑equality test**: dump `dpy->buffer` right before
   `wmb_sync_handler` executes and compare with the socket‑era bytes
   captured in Phase 0. They should be identical.
2. **Reply structural test**: after each `_XReply`, dump the reply and
   compare against the reference log.
3. **Event ordering test**: feed a scripted input stream into
   `keypad_driver`/`mouse_driver`, capture the events observed by a test
   app, and verify order + timestamp monotonicity.
4. **Fault injection**: force `_XGetRequest` to return NULL, force
   `Proc*` to fail, and verify `xError` is delivered to the app.
5. **Long‑run leak test**: 1‑hour run with 10k window create/destroy
   cycles; `_sbrk` high‑water mark must stabilise.

---

## 14. Open Questions / Risks

- **Locking**. `LockDisplay/UnlockDisplay` are irrelevant in single‑thread
  WMB but must be defined (as no‑ops). If we ever run multiple app threads
  concurrently sharing a `Display`, we need real locks — see
  [../archive_docs2/XCB_DEPENDENCY_RISK_ASSESSMENT.md](../archive_docs2/XCB_DEPENDENCY_RISK_ASSESSMENT.md).
- **Extensions**. Some extensions (XKB, RandR) install their own
  request/reply logic. Verify each installed extension writes replies via
  `WriteToClient` (they do — this is DIX contract).
- **`Data()` macro reach**. 17 upstream files use `Data()`. All must pick up
  the modified `Xlibint.h`; a stale header would silently call the old
  `_XSend` and lose the payload.
- **Error path**. A `Proc*` returning `BadWindow` needs to synthesise an
  `xError` reply and enqueue it as if it came from the wire. Test coverage
  required.
- **Setup blob**. `xcb_get_setup()` must return a valid `xConnSetup` for the
  `Display`. Either capture one from a real TinyX run and bake it in, or
  build it programmatically at `XOpenDisplay` time using
  `screenInfo`.
- **Multiple apps**. Each `XOpenDisplay` allocates a new `WmbClientRec`.
  Client IDs must not collide with each other or with the WM's own client.

---

## Appendix — File Cross‑Reference

Existing archive material this document distils:

- [../archive_docs2/CUSTOM_XLIB.md](../archive_docs2/CUSTOM_XLIB.md) —
  original two‑block description.
- [../archive_docs2/XCB_STUB_ARCHITECTURE.md](../archive_docs2/XCB_STUB_ARCHITECTURE.md) —
  full stub function specification (used as the basis of §6).
- [../archive_docs2/X11_CLIENT_SERVER_ANALYSIS.md](../archive_docs2/X11_CLIENT_SERVER_ANALYSIS.md) —
  survey of every socket call site in the tree.
- [../archive_docs2/XCB_DEPENDENCY_RISK_ASSESSMENT.md](../archive_docs2/XCB_DEPENDENCY_RISK_ASSESSMENT.md) —
  risks and mitigations for peeling libX11 off XCB.
- [../archive_docs2/WMB_API_DESIGN.md](../archive_docs2/WMB_API_DESIGN.md) —
  earlier vtable design.

Key source files referenced:

- [libX11/src/xcb_io.c](../../libX11/src/xcb_io.c) — the file being replaced.
- [libX11/include/X11/Xlibint.h](../../libX11/include/X11/Xlibint.h) —
  contains `SyncHandle`, `GetReq`, `Data` macros.
- [tinyx/dix/dispatch.c](../../tinyx/dix/dispatch.c) — server `Dispatch()` /
  `ProcVector`.
- [tinyx/os/io.c](../../tinyx/os/io.c) — server's `WriteToClient` (being replaced).
- [tinyx/os/WaitFor.c](../../tinyx/os/WaitFor.c) — being reduced per §11.
- [tinyx/kdrive/tima/tima_kdrive_system.c](../../tinyx/kdrive/tima/tima_kdrive_system.c) —
  where events originate on the HAL side.
