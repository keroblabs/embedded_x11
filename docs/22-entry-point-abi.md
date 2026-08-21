# 22 — Entry‑Point ABI

## 1. Purpose

This document specifies **how an application that is a separate binary reaches into WMB**. It is the third deliverable of workstream 3
([`00-introduction.md`](00-introduction.md) §5.3).

Internal clients — JWM, in‑tree applications, `wmb_test_client` — need none of this; they are compiled into the image and use the in‑process transport
directly ([`21-inprocess-transport.md`](21-inprocess-transport.md)). This document is only about the **external** case: a program loaded by the target
platform at some other address, which must not carry its own copy of Xlib.

### 1.1 What this document is not

- **Not the in‑process transport.** [`21-`](21-inprocess-transport.md).
- **Not a requirement for the hosted milestone.** Everything here is phase P8, after JWM works. It is documented early so that nothing in P1–P7
  accidentally makes it impossible.

### 1.2 The problem

An external application `#include <X11/Xlib.h>` and calls `XCreateWindow(dpy, …)`. Three ways to satisfy that:

| Option | Approach | Verdict |
|---|---|---|
| A | Link a full copy of libX11 into each application | ~34 kLOC and a separate `Display` universe per app, with no way to reach the server. **Impossible** — the transport is in‑process by definition |
| B | Restore a socket between the app and WMB | Reintroduces exactly what the project removes |
| C | **The application links a thin wrapper whose functions jump into WMB's Xlib through a vtable at a known address** | Chosen |

Option C is the only one consistent with "one image contains the whole windowing system".

## 2. Two‑Block Design

```
   ┌────────────────────────────────────────────────────────────────┐
   │ APPLICATION BINARY                                             │
   │                                                                │
   │   app.c        XCreateSimpleWindow(dpy, root, ...)             │
   │      │                                                         │
   │      ▼                                                         │
   │   libX11-app.a   ← the ONLY thing the app links                │
   │      Window XCreateSimpleWindow(Display *d, Window p, ...) {   │
   │          return WMB_API->XCreateSimpleWindow(d, p, ...);       │
   │      }                                                         │
   │      ~20 KB · one line per entry point · no protocol logic     │
   └───────────────────────────┬────────────────────────────────────┘
                               │ load WMB_API_ADDRESS, indirect call
   ┌───────────────────────────▼────────────────────────────────────┐
   │ WMB IMAGE (fixed load address, non-PIC)                        │
   │                                                                │
   │   .wmb_api section  ──►  const wmb_api_t wmb_api = { ... }     │
   │                              │                                 │
   │                              ▼                                 │
   │   real libX11-wmb  →  wmb_transport.c  →  TinyX                │
   └────────────────────────────────────────────────────────────────┘
```

The application ships with `Xlib.h` (unchanged, upstream) and `libX11-app.a`. It never sees `Xlibint.h`, never touches `dpy->buffer`, and never learns what
a request looks like.

## 3. The Vtable

### 3.1 Layout rules

| # | Rule | Why |
|---|---|---|
| A1 | **Append‑only.** New entries go at the end. Existing entries never move, never change signature | An application built against version *n* must run against version *n+k* |
| A2 | **First two fields are `magic` and `version`.** | An application can validate before calling |
| A3 | **`count` records how many function slots are populated.** | Forward compatibility: an old WMB with a new app fails cleanly instead of jumping into `.bss` |
| A4 | **Only Xlib public types cross the boundary.** `Display*`, `Window`, `GC`, `XEvent*`, `Atom`, … Never `WmbDisplay*`, never `ClientPtr` | The application must not depend on WMB internals |
| A5 | **`Display*` is opaque to the application.** It is allocated inside WMB | An app must not `sizeof(Display)` or dereference it |
| A6 | **No varargs entries.** | Impossible to forward through a thin wrapper portably |

A5 has a consequence worth stating plainly: the application must not use any Xlib **macro** that dereferences `Display` — `DefaultScreen(dpy)`,
`RootWindow(dpy, s)`, `BlackPixel(dpy, s)`, `DisplayWidth(dpy, s)` and friends all expand to `((dpy)->…)`. They are therefore provided as **functions** in
the vtable, and `libX11-app.a` ships a header fragment that `#undef`s the macros and declares the functions. This is the one place the application's
compile environment differs from stock Xlib.

### 3.2 Structure

```c
/* framework/wmb_api/wmb_api.h — shipped to applications */
#ifndef WMB_API_H
#define WMB_API_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>

#define WMB_API_MAGIC    0x574D4241u   /* 'W' 'M' 'B' 'A' */
#define WMB_API_VERSION  1u

typedef struct wmb_api_s {
    uint32_t magic;                     /* WMB_API_MAGIC   */
    uint32_t version;                   /* WMB_API_VERSION */
    uint32_t count;                     /* populated slots */
    uint32_t reserved;

    /* ---- display ------------------------------------------------ */
    Display *(*XOpenDisplay)(const char *);
    int      (*XCloseDisplay)(Display *);
    int      (*XFlush)(Display *);
    int      (*XSync)(Display *, Bool);
    int      (*XConnectionNumber)(Display *);

    /* ---- display accessors (macros in stock Xlib — A5) ---------- */
    int      (*wmbDefaultScreen)(Display *);
    Window   (*wmbRootWindow)(Display *, int);
    unsigned long (*wmbBlackPixel)(Display *, int);
    unsigned long (*wmbWhitePixel)(Display *, int);
    int      (*wmbDisplayWidth)(Display *, int);
    int      (*wmbDisplayHeight)(Display *, int);
    int      (*wmbDefaultDepth)(Display *, int);
    Visual  *(*wmbDefaultVisual)(Display *, int);
    Colormap (*wmbDefaultColormap)(Display *, int);
    GC       (*wmbDefaultGC)(Display *, int);

    /* ---- windows ------------------------------------------------ */
    Window   (*XCreateSimpleWindow)(Display *, Window, int, int, unsigned, unsigned,
                                    unsigned, unsigned long, unsigned long);
    Window   (*XCreateWindow)(Display *, Window, int, int, unsigned, unsigned,
                              unsigned, int, unsigned, Visual *, unsigned long,
                              XSetWindowAttributes *);
    int      (*XDestroyWindow)(Display *, Window);
    int      (*XMapWindow)(Display *, Window);
    int      (*XMapRaised)(Display *, Window);
    int      (*XUnmapWindow)(Display *, Window);
    int      (*XMoveWindow)(Display *, Window, int, int);
    int      (*XResizeWindow)(Display *, Window, unsigned, unsigned);
    int      (*XMoveResizeWindow)(Display *, Window, int, int, unsigned, unsigned);
    int      (*XConfigureWindow)(Display *, Window, unsigned, XWindowChanges *);
    int      (*XRaiseWindow)(Display *, Window);
    int      (*XLowerWindow)(Display *, Window);
    Status   (*XGetWindowAttributes)(Display *, Window, XWindowAttributes *);
    int      (*XSetWindowBackground)(Display *, Window, unsigned long);
    int      (*XClearWindow)(Display *, Window);
    int      (*XClearArea)(Display *, Window, int, int, unsigned, unsigned, Bool);

    /* ---- graphics context --------------------------------------- */
    GC       (*XCreateGC)(Display *, Drawable, unsigned long, XGCValues *);
    int      (*XFreeGC)(Display *, GC);
    int      (*XSetForeground)(Display *, GC, unsigned long);
    int      (*XSetBackground)(Display *, GC, unsigned long);
    int      (*XSetLineAttributes)(Display *, GC, unsigned, int, int, int);
    int      (*XSetFunction)(Display *, GC, int);
    int      (*XSetFont)(Display *, GC, Font);

    /* ---- drawing ------------------------------------------------ */
    int      (*XDrawPoint)(Display *, Drawable, GC, int, int);
    int      (*XDrawLine)(Display *, Drawable, GC, int, int, int, int);
    int      (*XDrawLines)(Display *, Drawable, GC, XPoint *, int, int);
    int      (*XDrawRectangle)(Display *, Drawable, GC, int, int, unsigned, unsigned);
    int      (*XFillRectangle)(Display *, Drawable, GC, int, int, unsigned, unsigned);
    int      (*XFillPolygon)(Display *, Drawable, GC, XPoint *, int, int, int);
    int      (*XDrawArc)(Display *, Drawable, GC, int, int, unsigned, unsigned, int, int);
    int      (*XFillArc)(Display *, Drawable, GC, int, int, unsigned, unsigned, int, int);
    int      (*XDrawString)(Display *, Drawable, GC, int, int, const char *, int);
    int      (*XDrawImageString)(Display *, Drawable, GC, int, int, const char *, int);
    int      (*XCopyArea)(Display *, Drawable, Drawable, GC, int, int,
                          unsigned, unsigned, int, int);

    /* ---- pixmaps & images --------------------------------------- */
    Pixmap   (*XCreatePixmap)(Display *, Drawable, unsigned, unsigned, unsigned);
    int      (*XFreePixmap)(Display *, Pixmap);
    XImage  *(*XCreateImage)(Display *, Visual *, unsigned, int, int, char *,
                             unsigned, unsigned, int, int);
    int      (*XPutImage)(Display *, Drawable, GC, XImage *, int, int, int, int,
                          unsigned, unsigned);
    int      (*XDestroyImage)(XImage *);

    /* ---- events -------------------------------------------------- */
    int      (*XSelectInput)(Display *, Window, long);
    int      (*XNextEvent)(Display *, XEvent *);
    Bool     (*XCheckMaskEvent)(Display *, long, XEvent *);
    Bool     (*XCheckTypedWindowEvent)(Display *, Window, int, XEvent *);
    int      (*XPending)(Display *);
    int      (*XPutBackEvent)(Display *, XEvent *);
    int      (*XSendEvent)(Display *, Window, Bool, long, XEvent *);

    /* ---- atoms & properties -------------------------------------- */
    Atom     (*XInternAtom)(Display *, const char *, Bool);
    char    *(*XGetAtomName)(Display *, Atom);
    int      (*XChangeProperty)(Display *, Window, Atom, Atom, int, int,
                                const unsigned char *, int);
    int      (*XDeleteProperty)(Display *, Window, Atom);
    int      (*XGetWindowProperty)(Display *, Window, Atom, long, long, Bool, Atom,
                                   Atom *, int *, unsigned long *, unsigned long *,
                                   unsigned char **);

    /* ---- colours -------------------------------------------------- */
    Status   (*XAllocColor)(Display *, Colormap, XColor *);
    Status   (*XAllocNamedColor)(Display *, Colormap, const char *, XColor *, XColor *);
    int      (*XFreeColors)(Display *, Colormap, unsigned long *, int, unsigned long);

    /* ---- fonts & cursors ------------------------------------------ */
    XFontStruct *(*XLoadQueryFont)(Display *, const char *);
    int      (*XFreeFont)(Display *, XFontStruct *);
    int      (*XTextWidth)(XFontStruct *, const char *, int);
    Cursor   (*XCreateFontCursor)(Display *, unsigned);
    int      (*XDefineCursor)(Display *, Window, Cursor);
    int      (*XFreeCursor)(Display *, Cursor);

    /* ---- WM hints -------------------------------------------------- */
    int      (*XSetWMProperties)(Display *, Window, XTextProperty *, XTextProperty *,
                                 char **, int, XSizeHints *, XWMHints *, XClassHint *);
    int      (*XStoreName)(Display *, Window, const char *);
    Status   (*XSetWMProtocols)(Display *, Window, Atom *, int);

    /* ---- error handling & misc ------------------------------------- */
    XErrorHandler (*XSetErrorHandler)(XErrorHandler);
    int      (*XFree)(void *);
    int      (*XGrabServer)(Display *);
    int      (*XUngrabServer)(Display *);
    int      (*XBell)(Display *, int);

    /* ---- WMB-specific ---------------------------------------------- */
    /* Cooperative yield, so an external app's event loop does not spin. */
    int      (*wmbYield)(uint32_t max_ms);
} wmb_api_t;

#ifndef WMB_API_ADDRESS
#  define WMB_API_ADDRESS  ((uintptr_t)0x80100000u)   /* build-time override */
#endif
#define WMB_API  ((const wmb_api_t *)WMB_API_ADDRESS)

#endif /* WMB_API_H */
```

Roughly **75 entries**. That is enough for JWM's own needs plus a typical application; the append‑only rule means growing it later is free.

### 3.3 `wmbYield` — the entry that is not Xlib

An external application's event loop looks like:

```c
for (;;) {
    while (XPending(dpy)) { XNextEvent(dpy, &ev); handle(&ev); }
    /* ... and then what? */
}
```

On a normal system it blocks in `XNextEvent`. In WMB, blocking inside the vtable would suspend the *platform's* task, not a WMB fiber. `wmbYield(ms)` gives
the application an explicit, honest way to give time back:

```c
for (;;) {
    while (WMB_API->XPending(dpy)) { WMB_API->XNextEvent(dpy, &ev); handle(&ev); }
    WMB_API->wmbYield(10);
}
```

`XNextEvent` **does** block: it loops on `wmbYield` internally until an event is available ([`90-`](90-decisions.md) D‑27), so an ordinary Xlib event loop
behaves as its author expected. `wmbYield` remains in the vtable for applications that want an explicit non‑blocking loop — they are no longer *obliged*
to write one. The consequence for a port is that `wmbYield(ms)` must be callable from a platform‑owned task; it is the one contract function a platform
cannot implement as a no‑op.

## 4. Placement and Discovery

### 4.1 Section placement

```c
/* framework/wmb_api/wmb_api_table.c */
__attribute__((section(".wmb_api"), used))
const wmb_api_t wmb_api = {
    .magic   = WMB_API_MAGIC,
    .version = WMB_API_VERSION,
    .count   = WMB_API_SLOT_COUNT,
    .XOpenDisplay        = XOpenDisplay,
    .XCloseDisplay       = XCloseDisplay,
    /* ... */
    .wmbYield            = wmb_yield_external,
};
```

```ld
/* framework/wmb_api/wmb.ld — linker fragment */
SECTIONS
{
    . = WMB_LOAD_BASE;
    .wmb_api : { KEEP(*(.wmb_api)) }    /* first section, at offset 0 */
    .text    : { *(.text*) }
    /* ... */
}
```

Placing `.wmb_api` at offset 0 of the image means an application needs exactly one number — the load base — and no symbol table.

### 4.2 Discovery and validation

```c
/* wrapper/libX11-app/wmb_bind.c */
static const wmb_api_t *api;

int wmb_bind(void)
{
    const wmb_api_t *p = WMB_API;
    if (p->magic   != WMB_API_MAGIC)   return -1;
    if (p->version >  WMB_API_VERSION) return -2;  /* WMB newer than we know: OK */
    if (p->count   <  WMB_API_SLOT_COUNT) return -3; /* WMB older: some slots missing */
    api = p;
    return 0;
}
```

Called from the wrapper's constructor or from the first `XOpenDisplay`. A validation failure is a hard error — better than jumping to address zero.

### 4.3 Non‑PIC

The WMB image is built non‑PIC and loaded at a fixed base:

```
-fno-pic -fno-pie -no-pie -Wl,-T,framework/wmb_api/wmb.ld -Wl,--defsym,WMB_LOAD_BASE=0x80100000
```

`WMB_LOAD_BASE` and `WMB_API_ADDRESS` are the same number and are supplied by one CMake variable, `WMB_LOAD_BASE`, so they cannot drift.

## 5. The Wrapper Library

### 5.1 Shape

`wrapper/libX11-app/wrappers.c` — one line per entry, generated rather than hand‑written:

```c
#define WRAP0(ret, name)             ret name(void) { return api->name(); }
#define WRAP1(ret, name, T1)         ret name(T1 a) { return api->name(a); }
#define WRAP2(ret, name, T1, T2)     ret name(T1 a, T2 b) { return api->name(a, b); }
/* ... up to WRAP12 ... */

WRAP1(Display *, XOpenDisplay, const char *)
WRAP2(int,       XMapWindow,   Display *, Window)
WRAP9(Window,    XCreateSimpleWindow, Display *, Window, int, int,
                 unsigned, unsigned, unsigned, unsigned long, unsigned long)
/* ... */
```

Generated from a single table so that `wmb_api.h`, `wrappers.c` and the slot count cannot get out of step. The generator (`tools/gen_wmb_api.py`) reads
`framework/wmb_api/api.def` and emits all three.

### 5.2 Macro replacements

Ships `wrapper/libX11-app/include/X11/Xlib_wmb_compat.h`, included automatically via `-include`:

```c
#undef  DefaultScreen
#define DefaultScreen(d)      wmbDefaultScreen(d)
#undef  RootWindow
#define RootWindow(d, s)      wmbRootWindow((d), (s))
#undef  BlackPixel
#define BlackPixel(d, s)      wmbBlackPixel((d), (s))
/* ... one per dereferencing macro ... */
```

The full list of `Display`‑dereferencing macros in `X11/Xlib.h` must be enumerated exhaustively; any that is missed produces a segfault at the
application's first use, which is at least loud.

### 5.3 Size budget

| Component | Size |
|---|---|
| `wrappers.o` (75 one‑line functions) | ~6 KB |
| `wmb_bind.o` | ~0.5 KB |
| Headers | — |
| **Total `libX11-app.a`** | **< 10 KB** |

Compare: linking real libX11 would be ~400 KB per application.

## 6. Hosted Validation

Target hardware does not exist yet, so the ABI is validated on the host with a **bridge**: a socket carrying a trivially framed remote‑call encoding of the
same vtable.

```
   test_apps/wmb_hello (separate host process)
       links libX11-app-bridge.a   ← same wrappers, different binding
           XMapWindow(dpy, w)
             → bridge_call(SLOT_XMapWindow, dpy, w)
                 → write(sock, {slot, args})   ← /tmp/.wmb-N
   ────────────────────────────────────────────────────────
   WMB host process
       wmb_bridge.c accepts, decodes, invokes wmb_api.XMapWindow(...)
```

This proves the **slot table, the argument marshalling and the wrapper generator** — everything except the fixed‑address load, which cannot be tested
without a loader.

**The bridge is scaffolding.** It is compiled only under `WMB_HAS_OS_TRANSPORT` and is explicitly not part of the architecture
([`03-target-architecture.md`](03-target-architecture.md) §7). No capability may be declared working on the strength of a bridge test alone.

### 6.1 Bridge wire format

Deliberately trivial — it is a test harness, not a protocol:

```c
typedef struct {
    uint32_t length;     /* total bytes including this header */
    uint16_t slot;       /* index into wmb_api_t              */
    uint16_t flags;      /* WMB_BR_CALL | WMB_BR_RETURN       */
    uint32_t seq;
    /* followed by packed arguments or the return value       */
} wmb_bridge_hdr_t;
```

Pointer arguments (`Display*`, `XEvent*`) are handled by slot‑specific marshalling stubs, generated from the same `api.def`.

## 7. Threading and Ownership

An external application runs on a task owned by the **platform**, not by WMB ([`90-`](90-decisions.md) D‑42 — WMB does not assume it owns the system). When
it calls through the vtable, WMB code executes on that task's stack. Three consequences:

| # | Consequence | Handling |
|---|---|---|
| E1 | WMB cannot `hal_task_wait()` on a platform‑owned task | Vtable entries that would block instead poll: `wmb_dispatch` uses a bounded cooperative spin when `hal_task_self()` is not a WMB fiber |
| E2 | The external app's `Display` still needs a `ClientRec` and slots | `XOpenDisplay` through the vtable registers one exactly as an internal client would; the `owner` field is NULL |
| E3 | Stack depth is the platform's problem | Documented requirement: an external app's task needs ≥ 32 KB of stack to survive a call into `fb` |
| E4 | **Two platform tasks can be in the transport at once** if the platform preempts between them | The request queue is MPSC on the producer side (one atomic on the ready mask) and the server wake is a polled mailbox flag. [`90-`](90-decisions.md) D‑44 |

E1 is the substantive one and is the reason `wmbYield` exists (§3.3). `XNextEvent` uses it internally rather than returning early
([`90-`](90-decisions.md) D‑27).

**E4 is narrower than it first looks, and the narrowing matters.** An external caller never reaches `dix` concurrently: D‑04 applies to it exactly as it
applies to an internal client — it posts the request, wakes the server fiber and waits, so the server fiber serialises every client in the image. That is
the same role the socket and dispatch loop play in a stock X server, and it is why `dix` never has to be made concurrency‑safe (it is not, and neither is
upstream's: `Dispatch()` sets `requestingClient`, `grabState` and `spriteTrace` as per‑server globals for the request in flight).

What the external caller *does* execute on its own task is the transport. Request building is per‑`Display` and safe (D‑05); reply reading is per‑client
and safe (D‑08). Only two steps touch shared state — posting into the queue, and waking the server — and those are what E4 guards. **The external caller
is the only actor in the image that is not a fiber**; everything else is serialised by cooperative scheduling for free.

Guarding the whole vtable instead would be worse than unnecessary: the guard would be held across `wmb_dispatch`'s wait for the reply, which yields.

## 8. Versioning

| Change | Allowed | Requires |
|---|---|---|
| Append a new function slot | ✓ | Bump `WMB_API_VERSION`, bump `WMB_API_SLOT_COUNT` |
| Change an existing signature | ✗ | A new major ABI and a new magic |
| Reorder slots | ✗ | — |
| Remove a slot | ✗ | Replace the entry with a stub that returns an error |
| Change `WMB_API_ADDRESS` | ✓ (build‑time) | Rebuild both sides; there is no runtime negotiation |

Compatibility matrix:

| App built for | WMB provides | Result |
|---|---|---|
| v1 (60 slots) | v1 (60 slots) | Works |
| v1 (60 slots) | v2 (75 slots) | Works — slots 0–59 unchanged |
| v2 (75 slots) | v1 (60 slots) | `wmb_bind` returns −3; the app must fail cleanly |

## 9. Open Items

| # | Item | Tracked as |
|---|---|---|
| A1 | Is `0x80100000` the right load base? It is inherited from prior documents and is a placeholder until target memory is known | [`91-`](91-open-questions.md) Q6 |
| A2 | May `XNextEvent` block for an external caller? | **Closed** — yes, by yielding internally ([`90-`](90-decisions.md) D‑27) |
| A3 | Flat `void*` slot array instead of a struct of typed pointers? | **Closed** — typed struct ([`90-`](90-decisions.md) D‑28) |
| A4 | Does any application need `XCreateImage`/`XPutImage` on the target, or is drawing entirely through primitives? Affects whether image handling stays in the vtable | [`91-`](91-open-questions.md) Q9 |
| A5 | Exhaustive list of `Display`‑dereferencing macros in `X11/Xlib.h` — generated from the header, not typed | [`91-`](91-open-questions.md) Q20; P8 deliverable ([`40-`](40-implementation-plan.md) §9.1) |
| A6 | Who schedules WMB, and may the platform preempt it? | **Closed** — the caller of `wmb_main()`; preemptible as a unit, never within ([`90-`](90-decisions.md) D‑44) |

## 10. Verification

| # | Gate | Phase |
|---|---|---|
| E‑V1 | `wmb_api.h`, `wrappers.c` and the slot count are all generated from `api.def`; regenerating produces no diff | P8 |
| E‑V2 | `nm libX11-app.a` shows no symbol other than the wrapper entry points and `wmb_bind` | P8 |
| E‑V3 | `libX11-app.a` is < 16 KB | P8 |
| E‑V4 | `wmb_hello` runs over the bridge and produces a visible window | P8 |
| E‑V5 | An app built against v1 runs against a v2 WMB | P8 |
| E‑V6 | The `.wmb_api` section is at offset 0 of the image and `WMB_API_ADDRESS == WMB_LOAD_BASE` | P9 |
| E‑V7 | `readelf -r wmb.elf` shows no dynamic relocations (non‑PIC confirmed) | P9 |
