# Custom Xlib Implementation Guide

## Objective

Create a **two-block Xlib architecture** that:
1. Provides **minimal application-side wrappers** (thin library for apps)
2. Keeps **all Xlib implementation in WMB** (code size efficiency)

This achieves **100% X11 client API compatibility** while minimizing application binary size.

## Architecture: Two-Block Design

Per BASE_REQUIREMENTS #1: *"For code size efficiency, the application should use entry point function calls in the WMB, so the xlib should have only wrappers. Everything else will be implemented in the WMB."*

```
┌─────────────────────────────────────────────────────────────────────┐
│                        APPLICATION SPACE                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                  Application Code                           │    │
│  │  XOpenDisplay(), XCreateWindow(), XDrawLine(), etc.         │    │
│  └──────────────────────────┬──────────────────────────────────┘    │
│                             │                                       │
│  ┌──────────────────────────▼──────────────────────────────────┐    │
│  │              libX11-app.a (THIN WRAPPERS ONLY)              │    │
│  │  ~50 wrapper functions → call WMB entry points              │    │
│  │  Size: ~10-20 KB per application                            │    │
│  └──────────────────────────┬──────────────────────────────────┘    │
│                             │ Function calls to fixed address       │
├─────────────────────────────┼───────────────────────────────────────┤
│                             │                                       │
│                        WMB SPACE (Fixed Address)                    │
│                             │                                       │
│  ┌──────────────────────────▼──────────────────────────────────┐    │
│  │                WMB Entry Point Table                        │    │
│  │  Fixed address jump table for all Xlib functions            │    │
│  └──────────────────────────┬──────────────────────────────────┘    │
│                             │                                       │
│  ┌──────────────────────────▼──────────────────────────────────┐    │
│  │            Full Xlib Implementation (libX11-wmb)            │    │
│  │  - All window management (CrWindow.c, MapWindow.c, etc.)    │    │
│  │  - All graphics (DrLine.c, FillRect.c, CopyArea.c, etc.)    │    │
│  │  - All GC operations (CrGC.c, SetFore.c, SetBack.c, etc.)   │    │
│  │  - All event handling (NextEvent.c, Pending.c, etc.)        │    │
│  │  - Direct calls to X server (no transport layer)            │    │
│  └──────────────────────────┬──────────────────────────────────┘    │
│                             │                                       │
│  ┌──────────────────────────▼──────────────────────────────────┐    │
│  │                   X11 Server (TinyX/Kdrive)                 │    │
│  └──────────────────────────┬──────────────────────────────────┘    │
│                             │                                       │
│  ┌──────────────────────────▼──────────────────────────────────┐    │
│  │                          JWM                                │    │
│  └──────────────────────────┬──────────────────────────────────┘    │
│                             │                                       │
│  ┌──────────────────────────▼──────────────────────────────────┐    │
│  │                     HAL (Hardware)                          │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

## Benefits of Two-Block Design

| Aspect | Traditional (Xlib in App) | Two-Block (Wrappers Only) |
|--------|---------------------------|---------------------------|
| App binary size | +200-400 KB per app | +10-20 KB per app |
| Code duplication | Xlib in every app | Xlib only in WMB |
| Memory usage | N × Xlib size | 1 × Xlib size |
| Updates | Rebuild all apps | Update WMB only |
| Bug fixes | Propagate to all apps | Fix once in WMB |

## Two-Block Components

### Block 1: Application Wrappers (libX11-app.a)

**Location**: Linked into each application
**Size**: ~10-20 KB
**Purpose**: Thin wrappers that forward calls to WMB

```c
/* libX11-app/src/wrappers.c */
/* Application-side wrappers - call WMB entry points */

#include <X11/Xlib.h>
#include "wmb_api.h"

/* WMB entry point table at fixed address (per BASE_REQUIREMENTS #2) */
#define WMB_ENTRY_TABLE ((wmb_entry_t*)0x80100000)  /* Example address */

/* Wrapper: XOpenDisplay */
Display* XOpenDisplay(const char* display_name)
{
    return WMB_ENTRY_TABLE->XOpenDisplay(display_name);
}

/* Wrapper: XCreateSimpleWindow */
Window XCreateSimpleWindow(Display* dpy, Window parent,
                           int x, int y,
                           unsigned int width, unsigned int height,
                           unsigned int border_width,
                           unsigned long border, unsigned long background)
{
    return WMB_ENTRY_TABLE->XCreateSimpleWindow(dpy, parent, x, y,
                                                 width, height, border_width,
                                                 border, background);
}

/* Wrapper: XMapWindow */
int XMapWindow(Display* dpy, Window w)
{
    return WMB_ENTRY_TABLE->XMapWindow(dpy, w);
}

/* ... ~200 more wrappers for all Xlib functions ... */
```

### Block 2: WMB Implementation (libX11-wmb inside WMB)

**Location**: Inside WMB binary
**Purpose**: Full Xlib implementation + X server + JWM

The WMB contains:
- Entry point table (fixed address jump table)
- Complete libX11 implementation (patched from standard libX11)
- Direct-call transport layer (no sockets)
- TinyX/Kdrive X server
- JWM window manager
- HAL interface

## OS-Specific Handling in Xlib

1. **Time and Environment APIs**: Functions like gettimeofday, time, localtime, getenv, putenv, tzset, strftime (used in JWM/src/timing.c and Xlib) must be available via syscalls.c and Xos.h. Stub or document limitations for unsupported features (e.g., time zones).
2. **POSIX API Redirection**: Xos.h and syscalls.c provide the minimal POSIX layer for Xlib and X11 headers.
3. **Effort**: Low to moderate for time/env APIs, moderate for POSIX layer.

**Critical Requirement**: No learning curve for X11 developers. Code written for Linux X11 must compile unchanged for RISC-V WMB platform.

---

## WMB Entry Point Table

Per BASE_REQUIREMENTS #2: *"The final WMB will be sitting in a memory space and will have a fixed address entry point to access the API."*

```c
/* wmb_api.h - Entry point table structure */

#ifndef WMB_API_H
#define WMB_API_H

#include <X11/Xlib.h>

/* Entry point table - all Xlib functions */
typedef struct {
    /* Display management */
    Display* (*XOpenDisplay)(const char*);
    int (*XCloseDisplay)(Display*);

    /* Window management */
    Window (*XCreateSimpleWindow)(Display*, Window, int, int,
                                   unsigned int, unsigned int,
                                   unsigned int, unsigned long, unsigned long);
    Window (*XCreateWindow)(Display*, Window, int, int,
                            unsigned int, unsigned int, unsigned int,
                            int, unsigned int, Visual*,
                            unsigned long, XSetWindowAttributes*);
    int (*XMapWindow)(Display*, Window);
    int (*XUnmapWindow)(Display*, Window);
    int (*XDestroyWindow)(Display*, Window);
    int (*XMoveWindow)(Display*, Window, int, int);
    int (*XResizeWindow)(Display*, Window, unsigned int, unsigned int);
    int (*XRaiseWindow)(Display*, Window);
    int (*XLowerWindow)(Display*, Window);

    /* Graphics Context */
    GC (*XCreateGC)(Display*, Drawable, unsigned long, XGCValues*);
    int (*XFreeGC)(Display*, GC);
    int (*XSetForeground)(Display*, GC, unsigned long);
    int (*XSetBackground)(Display*, GC, unsigned long);
    int (*XSetFunction)(Display*, GC, int);
    int (*XSetLineAttributes)(Display*, GC, unsigned int, int, int, int);

    /* Drawing */
    int (*XDrawLine)(Display*, Drawable, GC, int, int, int, int);
    int (*XDrawLines)(Display*, Drawable, GC, XPoint*, int, int);
    int (*XDrawRectangle)(Display*, Drawable, GC, int, int, unsigned int, unsigned int);
    int (*XFillRectangle)(Display*, Drawable, GC, int, int, unsigned int, unsigned int);
    int (*XDrawArc)(Display*, Drawable, GC, int, int, unsigned int, unsigned int, int, int);
    int (*XFillArc)(Display*, Drawable, GC, int, int, unsigned int, unsigned int, int, int);
    int (*XDrawString)(Display*, Drawable, GC, int, int, const char*, int);
    int (*XDrawImageString)(Display*, Drawable, GC, int, int, const char*, int);
    int (*XCopyArea)(Display*, Drawable, Drawable, GC, int, int, unsigned int, unsigned int, int, int);
    int (*XClearWindow)(Display*, Window);
    int (*XClearArea)(Display*, Window, int, int, unsigned int, unsigned int, Bool);

    /* Events */
    int (*XSelectInput)(Display*, Window, long);
    int (*XNextEvent)(Display*, XEvent*);
    int (*XPeekEvent)(Display*, XEvent*);
    Bool (*XCheckWindowEvent)(Display*, Window, long, XEvent*);
    Bool (*XCheckTypedEvent)(Display*, int, XEvent*);
    int (*XPending)(Display*);
    int (*XEventsQueued)(Display*, int);
    int (*XFlush)(Display*);
    int (*XSync)(Display*, Bool);

    /* Atoms and Properties */
    Atom (*XInternAtom)(Display*, const char*, Bool);
    char* (*XGetAtomName)(Display*, Atom);
    int (*XChangeProperty)(Display*, Window, Atom, Atom, int, int, const unsigned char*, int);
    int (*XGetWindowProperty)(Display*, Window, Atom, long, long, Bool, Atom, Atom*, int*, unsigned long*, unsigned long*, unsigned char**);
    int (*XDeleteProperty)(Display*, Window, Atom);

    /* Pixmap */
    Pixmap (*XCreatePixmap)(Display*, Drawable, unsigned int, unsigned int, unsigned int);
    int (*XFreePixmap)(Display*, Pixmap);

    /* Colormap */
    Colormap (*XCreateColormap)(Display*, Window, Visual*, int);
    int (*XFreeColormap)(Display*, Colormap);
    Status (*XAllocColor)(Display*, Colormap, XColor*);
    Status (*XAllocNamedColor)(Display*, Colormap, const char*, XColor*, XColor*);
    int (*XFreeColors)(Display*, Colormap, unsigned long*, int, unsigned long);

    /* Cursor */
    Cursor (*XCreateFontCursor)(Display*, unsigned int);
    int (*XDefineCursor)(Display*, Window, Cursor);
    int (*XFreeCursor)(Display*, Cursor);

    /* Font */
    XFontStruct* (*XLoadQueryFont)(Display*, const char*);
    int (*XFreeFont)(Display*, XFontStruct*);
    int (*XSetFont)(Display*, GC, Font);
    int (*XTextWidth)(XFontStruct*, const char*, int);

    /* Keyboard/Pointer */
    int (*XGrabKeyboard)(Display*, Window, Bool, int, int, Time);
    int (*XUngrabKeyboard)(Display*, Time);
    int (*XGrabPointer)(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time);
    int (*XUngrabPointer)(Display*, Time);
    int (*XWarpPointer)(Display*, Window, Window, int, int, unsigned int, unsigned int, int, int);

    /* Selection */
    int (*XSetSelectionOwner)(Display*, Atom, Window, Time);
    Window (*XGetSelectionOwner)(Display*, Atom);
    int (*XConvertSelection)(Display*, Atom, Atom, Atom, Window, Time);

    /* Utility */
    int (*XFree)(void*);
    Status (*XGetGeometry)(Display*, Drawable, Window*, int*, int*, unsigned int*, unsigned int*, unsigned int*, unsigned int*);
    Status (*XQueryTree)(Display*, Window, Window*, Window*, Window**, unsigned int*);

    /* ... additional functions as needed ... */

} wmb_entry_t;

/* Fixed address of WMB entry table */
#define WMB_API_ADDRESS 0x80100000  /* Must match linker script */
#define WMB_API ((wmb_entry_t*)WMB_API_ADDRESS)

#endif /* WMB_API_H */
```

---

## WMB-Side Implementation

### Entry Point Table Initialization

```c
/* wmb_entry.c - Initialize entry point table in WMB */

#include "wmb_api.h"
#include "Xlibint.h"  /* Internal Xlib declarations */

/* Place entry table at fixed address via linker script */
__attribute__((section(".wmb_api")))
wmb_entry_t wmb_entry_table = {
    /* Display management */
    .XOpenDisplay = XOpenDisplay,
    .XCloseDisplay = XCloseDisplay,

    /* Window management */
    .XCreateSimpleWindow = XCreateSimpleWindow,
    .XCreateWindow = XCreateWindow,
    .XMapWindow = XMapWindow,
    .XUnmapWindow = XUnmapWindow,
    .XDestroyWindow = XDestroyWindow,
    .XMoveWindow = XMoveWindow,
    .XResizeWindow = XResizeWindow,
    .XRaiseWindow = XRaiseWindow,
    .XLowerWindow = XLowerWindow,

    /* Graphics Context */
    .XCreateGC = XCreateGC,
    .XFreeGC = XFreeGC,
    .XSetForeground = XSetForeground,
    .XSetBackground = XSetBackground,
    /* ... all other functions ... */
};
```

### Linker Script for Fixed Address

```ld
/* wmb.ld - Linker script placing entry table at fixed address */

MEMORY
{
    ROM (rx)  : ORIGIN = 0x80000000, LENGTH = 2M
    API (r)   : ORIGIN = 0x80100000, LENGTH = 4K   /* Entry point table */
    RAM (rwx) : ORIGIN = 0x80200000, LENGTH = 8M
}

SECTIONS
{
    .text : { *(.text*) } > ROM

    /* Entry point table at fixed address */
    .wmb_api 0x80100000 : {
        KEEP(*(.wmb_api))
    } > API

    .rodata : { *(.rodata*) } > ROM
    .data : { *(.data*) } > RAM
    .bss : { *(.bss*) } > RAM
}
```

---

## Internal Transport: Direct Calls (No Sockets)

Inside WMB, the Xlib implementation calls X server functions directly (no network transport):

```
WMB Internal Architecture:
┌─────────────────────────────────────────────┐
│ Xlib Implementation (from libX11)           │
│ CrWindow.c, MapWindow.c, DrLine.c, etc.     │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│ WMB Direct Call Layer (wmb_io.c)            │
│ - Direct function calls to X server         │
│ - No protocol encoding/decoding             │
└─────────────────┬───────────────────────────┘
                  │ Direct function calls
                  ▼
         X Server (TinyX/Kdrive)
```

### Files to Replace (Transport Layer)

**Replace these files** with WMB-specific implementations:

1. **`libX11/src/xcb_io.c`** → **`libX11-wmb/src/wmb_io.c`**
   - Current: XCB I/O, socket operations
   - New: Direct calls to X server internal functions

2. **`libX11/src/xcb_disp.c`** → **`libX11-wmb/src/wmb_disp.c`**
   - Current: Display management via XCB
   - New: Direct pointer to server Display structure

3. **`libX11/src/OpenDis.c`** → **`libX11-wmb/src/OpenDis.c`** (modified)
   - Current: Network connection setup
   - New: Initialize local server instance

4. **`libX11/src/ClDisplay.c`** → **`libX11-wmb/src/ClDisplay.c`** (modified)
   - Current: Close network connection
   - New: Cleanup local server

### Files to Keep Unchanged (API Layer)

**Keep these files as-is** - they already implement correct X11 API:

- All window management: `CrWindow.c`, `MapWindow.c`, `DestroyWindow.c`, etc.
- All graphics: `DrLine.c`, `FillRect.c`, `CopyArea.c`, etc.
- All GC operations: `CrGC.c`, `SetFore.c`, `SetBack.c`, etc.
- All event handling: `NextEvent.c`, `Pending.c`, `SelInput.c`, etc.
- All atoms/properties: `IntAtom.c`, `GetProp.c`, `ChProp.c`, etc.

**These files call internal `_X*` functions which we redirect to X server.**

## WMB Transport Layer Implementation

### 1. Replace XCB Transport with Direct Calls

Create `libX11-wmb/src/wmb_io.c` to replace `xcb_io.c`:

```c
/* libX11-wmb/src/wmb_io.c */
/* Replacement for xcb_io.c - Direct calls instead of XCB/sockets */

#include "Xlibint.h"
#include "Xprivate.h"
#include "wmb_internal.h"  /* X server internal API */

/* External references to X server internals */
extern void* x11_server_get_display_internal(void);
extern void x11_server_send_request_internal(void *dpy, void *req);
extern void* x11_server_get_reply_internal(void *dpy);
extern int x11_server_get_event_internal(void *dpy, void *event);

/* Replace XCB connection with direct server pointer */
typedef struct {
    void *server_display;    /* Pointer to X server's Display structure */
    int fd;                  /* Fake fd = -1 (no socket) */
} wmb_connection;

static wmb_connection wmb_conn;

/* Called by XOpenDisplay to establish "connection" */
void *_X11_wmb_connect(void)
{
    wmb_conn.server_display = x11_server_get_display_internal();
    wmb_conn.fd = -1;  /* No real file descriptor */
    return &wmb_conn;
}

/* Replace _XSend (normally sends to socket) */
void _XSend(Display *dpy, const char *data, long size)
{
    /* Direct call instead of socket write */
    x11_server_send_request_internal(wmb_conn.server_display, (void*)data);
}

/* Replace _XReply (normally reads from socket) */
Status _XReply(Display *dpy, xReply *rep, int extra, Bool discard)
{
    /* Direct call instead of socket read */
    void *reply = x11_server_get_reply_internal(wmb_conn.server_display);
    if (reply) {
        memcpy(rep, reply, sizeof(xReply));
        return True;
    }
    return False;
}

/* Replace _XReadEvents (normally reads from socket) */
void _XReadEvents(Display *dpy)
{
    XEvent event;
    /* Direct call instead of socket read */
    while (x11_server_get_event_internal(wmb_conn.server_display, &event)) {
        /* Queue event in Display structure */
        _XEnq(dpy, &event);
    }
}

/* Replace _XFlush (normally flushes socket buffer) */
void _XFlush(Display *dpy)
{
    /* No-op for direct calls, or trigger server event processing */
    x11_server_process_requests_internal(wmb_conn.server_display);
}
```

### 2. Modify XOpenDisplay for WMB

Create `libX11-wmb/src/OpenDis.c` (modified from original):

```c
/* libX11-wmb/src/OpenDis.c */
/* Modified XOpenDisplay for WMB server */

#include "Xlibint.h"
#include "wmb_internal.h"

extern void* x11_server_init_internal(void);
extern void* x11_server_get_screen_info_internal(int screen);

Display *XOpenDisplay(const char *display_name)
{
    Display *dpy;

    /* Ignore display_name - always local server */
    (void)display_name;

    /* Allocate Display structure */
    dpy = Xcalloc(1, sizeof(Display));
    if (!dpy) return NULL;

    /* Initialize WMB connection (no network) */
    dpy->fd = -1;  /* No socket */
    dpy->conn_checker = NULL;

    /* Initialize X server if not already running */
    void *server_ctx = x11_server_init_internal();
    if (!server_ctx) {
        Xfree(dpy);
        return NULL;
    }

    /* Get server display info (screen, visual, etc.) */
    void *screen_info = x11_server_get_screen_info_internal(0);

    /* Setup Display structure with server info */
    dpy->proto_major_version = 11;
    dpy->proto_minor_version = 0;
    dpy->vendor = "WMB-based X11";
    dpy->nscreens = 1;

    /* Initialize screen structure */
    dpy->screens = Xcalloc(1, sizeof(Screen));
    /* ... fill screen info from server ... */

    /* Setup default visual, colormap, etc. */
    /* ... */

    /* Initialize event queue */
    dpy->head = dpy->tail = NULL;
    dpy->qlen = 0;

    return dpy;
}
```

### 3. X Server Internal API

Create `libX11-wmb/include/rom_internal.h` defining interface to X server:

```c
/* libX11-wmb/include/rom_internal.h */
/* Interface between custom Xlib and X server internals */

#ifndef ROM_INTERNAL_H
#define ROM_INTERNAL_H

/* Initialize X server (called once by XOpenDisplay) */
void* x11_server_init_internal(void);

/* Get server's Display structure */
void* x11_server_get_display_internal(void);

/* Get screen information */
void* x11_server_get_screen_info_internal(int screen);

/* Send request to server (replaces X protocol) */
void x11_server_send_request_internal(void *dpy, void *req);

/* Get reply from server */
void* x11_server_get_reply_internal(void *dpy);

/* Get event from server's event queue */
int x11_server_get_event_internal(void *dpy, void *event);

/* Process pending requests */
void x11_server_process_requests_internal(void *dpy);

/* Window operations (direct calls to server) */
void* x11_server_create_window_internal(void *parent, int x, int y,
                                         unsigned int width, unsigned int height,
                                         unsigned int border_width,
                                         unsigned long border,
                                         unsigned long background);
void x11_server_map_window_internal(void *win);
void x11_server_destroy_window_internal(void *win);

/* Graphics operations */
void* x11_server_create_gc_internal(void *drawable);
void x11_server_fill_rectangle_internal(void *drawable, void *gc,
                                         int x, int y,
                                         unsigned int width, unsigned int height);
void x11_server_draw_line_internal(void *drawable, void *gc,
                                    int x1, int y1, int x2, int y2);

/* Event operations */
void x11_server_select_input_internal(void *win, long mask);

#endif /* WMB_INTERNAL_H */
```

## Build System

### Directory Structure (Two-Block)

```
x11-wmb-project/
├── include/
│   └── X11/              # Standard X11 headers (from X11/ directory)
│       ├── Xlib.h        # UNCHANGED - standard API
│       ├── Xutil.h
│       └── ...
│   └── wmb_api.h         # WMB entry point table definition
│
├── libX11-app/           # BLOCK 1: Application wrappers (thin library)
│   ├── src/
│   │   ├── wrappers.c    # All Xlib function wrappers (~200 functions)
│   │   └── display.c     # Display struct accessors
│   ├── include/
│   │   └── wmb_api.h     # Entry point table structure
│   └── Makefile
│   # Output: libX11-app.a (~10-20 KB) - linked into applications
│
├── libX11-wmb/           # BLOCK 2: Full Xlib in WMB (patched libX11)
│   ├── src/
│   │   ├── wmb_io.c      # NEW - replaces xcb_io.c (direct calls)
│   │   ├── wmb_disp.c    # NEW - replaces xcb_disp.c
│   │   ├── OpenDis.c     # MODIFIED - no network
│   │   ├── ClDisplay.c   # MODIFIED - cleanup only
│   │   │
│   │   # All other files from libX11/src/ UNCHANGED:
│   │   ├── CrWindow.c    # Keep as-is
│   │   ├── MapWindow.c   # Keep as-is
│   │   ├── NextEvent.c   # Keep as-is
│   │   ├── DrLine.c      # Keep as-is
│   │   └── ...           # ~400 files kept unchanged
│   │
│   ├── include/
│   │   └── wmb_internal.h  # X server interface
│   │
│   └── Makefile
│   # Output: libX11-wmb.a - compiled into WMB binary
│
├── wmb/                  # Entry point table + glue
│   ├── src/
│   │   └── wmb_entry.c   # Entry point table initialization
│   └── wmb.ld            # Linker script (fixed addresses)
│
├── x11-server/           # TinyX/Kdrive ported
│   ├── kdrive/
│   │   ├── riscv_screen.c   # KdCardFuncs implementation
│   │   ├── riscv_kbd.c      # KdKeyboardFuncs implementation
│   │   ├── riscv_mouse.c    # KdPointerFuncs implementation
│   │   └── riscv_init.c     # Kdrive initialization
│   └── Makefile
│
└── Makefile              # Build both blocks + WMB
```

### Makefile for Application Wrappers (Block 1)

```makefile
# libX11-app/Makefile - Thin wrapper library for applications

CC = riscv64-unknown-elf-gcc
AR = riscv64-unknown-elf-ar
CFLAGS = -O2 -I../include

SRCS = src/wrappers.c src/display.c
OBJS = $(SRCS:.c=.o)

libX11-app.a: $(OBJS)
	$(AR) rcs $@ $^
	@echo "Application wrapper library: $(shell ls -lh $@)"

clean:
	rm -f $(OBJS) libX11-app.a
```

### Makefile for WMB Xlib (Block 2)

```makefile
# libX11-wmb/Makefile - Full Xlib for WMB

CC = riscv64-unknown-elf-gcc
AR = riscv64-unknown-elf-ar
CFLAGS = -O2 -I../include -I./include -Ilibx11/include

# Sources from standard libX11 (unchanged)
LIBX11_SRCS = $(wildcard libx11/src/*.c)
LIBX11_SRCS := $(filter-out libx11/src/xcb_io.c libx11/src/xcb_disp.c \
                            libx11/src/OpenDis.c libx11/src/ClDisplay.c, \
                            $(LIBX11_SRCS))

# WMB-specific sources (replacements)
WMB_SRCS = src/wmb_io.c src/wmb_disp.c src/OpenDis.c src/ClDisplay.c

ALL_SRCS = $(LIBX11_SRCS) $(WMB_SRCS)
OBJS = $(ALL_SRCS:.c=.o)

libX11-wmb.a: $(OBJS)
	$(AR) rcs $@ $^

clean:
	rm -f $(OBJS) libX11-wmb.a
```

## Migration Path

### Step 1: Create Application Wrapper Library (Block 1)

Create the thin wrapper library (~500 lines):

```bash
cd x11-wmb-project/
mkdir -p libX11-app/src libX11-app/include

# Create wrappers.c with all Xlib function wrappers
# Create wmb_api.h with entry point table structure
```

**Files to create:**
- `libX11-app/src/wrappers.c` (~1,000 lines - all wrapper functions)
- `libX11-app/src/display.c` (~100 lines - Display struct helpers)
- `include/wmb_api.h` (~300 lines - entry point table)

**Total new code for Block 1: ~1,400 lines**

### Step 2: Copy Standard libX11 for WMB (Block 2)

```bash
cd x11-wmb-project/
mkdir -p libX11-wmb/libx11
cp -r ../libX11/src libX11-wmb/libx11/src
cp -r ../libX11/include libX11-wmb/libx11/include
```

### Step 3: Create WMB-Specific Transport Files

Create transport layer replacements:
- `libX11-wmb/src/wmb_io.c` (400 lines)
- `libX11-wmb/src/wmb_disp.c` (200 lines)
- `libX11-wmb/src/OpenDis.c` (modified, 300 lines)
- `libX11-wmb/src/ClDisplay.c` (modified, 50 lines)
- `libX11-wmb/include/wmb_internal.h` (100 lines)

**Total new code for Block 2 transport: ~1,050 lines**

### Step 4: Create Entry Point Table

Create entry point initialization:
- `wmb/src/wmb_entry.c` (~200 lines)
- `wmb/wmb.ld` (~50 lines)

### Step 5: Implement X Server Internal Functions

In `x11-server/src/`, implement functions declared in `wmb_internal.h`:

```c
/* x11-server/src/server_xlib.c */

#include "wmb_internal.h"
#include "kdrive.h"  /* TinyX/Kdrive internal structures */

static X11_SERVER_CTX server_ctx;

void* x11_server_init_internal(void)
{
    /* Initialize HAL */
    if (hal_init() != 0) return NULL;

    /* Initialize server structures (from TinyX/Kdrive) */
    server_ctx.root_window = create_root_window();
    server_ctx.event_queue = create_event_queue();
    /* ... */

    return &server_ctx;
}

void* x11_server_create_window_internal(void *parent, int x, int y,
                                         unsigned int width, unsigned int height,
                                         unsigned int border_width,
                                         unsigned long border,
                                         unsigned long background)
{
    /* Call TinyX/Kdrive window creation code */
    WindowPtr win = CreateWindow(/* TinyX internal call */
                                 (WindowPtr)parent, x, y, width, height,
                                 border_width, border, background,
                                 /* ... */);\n    return win;
}

/* ... implement other functions ... */
```

### Step 6: Build and Link

```bash
# Build application wrapper library (Block 1)
cd libX11-app
make
# Output: libX11-app.a (~10-20 KB)

# Build WMB Xlib (Block 2)
cd ../libX11-wmb
make
# Output: libX11-wmb.a (~400 KB)

# Build X server
cd ../x11-server
make

# Build WMB binary (includes everything except app wrappers)
cd ..
riscv64-unknown-elf-gcc -T wmb/wmb.ld -o wmb.elf \
    wmb/src/wmb_entry.o \
    libX11-wmb/libX11-wmb.a \
    x11-server/libx11server.a \
    jwm/libjwm.a \
    hal/libhal.a
```

## Application Compatibility

### Example Application

```c
/* app.c - Identical code for Linux and RISC-V */

#include <X11/Xlib.h>
#include <stdio.h>

int main(void)
{
    Display *dpy;
    Window win;
    XEvent event;
    GC gc;

    /* Standard X11 code - works on both platforms */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                              100, 100, 400, 300, 1,
                              BlackPixel(dpy, 0),
                              WhitePixel(dpy, 0));

    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, BlackPixel(dpy, 0));

    while (1) {
        XNextEvent(dpy, &event);

        if (event.type == Expose) {
            XDrawString(dpy, win, gc, 50, 50, "Hello X11!", 11);
        }

        if (event.type == KeyPress) {
            break;
        }
    }

    XCloseDisplay(dpy);
    return 0;
}
```

### Building for Different Platforms (Two-Block)

```bash
# Linux (standard Xlib - application links full Xlib)
gcc app.c -o app -lX11
./app

# RISC-V WMB (application links ONLY thin wrappers)
riscv64-unknown-elf-gcc app.c -o app.elf \
    -I../include \
    -L../libX11-app -lX11-app    # Only ~10-20 KB added to app!
```

**Same source code. Application binary is much smaller because it only includes wrappers.**

## Advantages of Two-Block Approach

### 1. Minimal Application Size
- Application links only wrapper library (~10-20 KB)
- Full Xlib resides in WMB (shared by all apps)
- Memory efficient: N apps × 10KB vs N apps × 400KB

### 2. Single Point of Maintenance
- Update Xlib in WMB only
- All applications automatically get updates
- No need to rebuild applications for Xlib fixes

### 3. Code Reuse
- Reuse ~99% of libX11 code (~400 files) in WMB
- Only replace transport layer (4 files)
- Only create thin wrappers for apps (~200 functions)

### 4. Perfect API Compatibility
- Use actual libX11 code for API
- Standard X11 headers unchanged
- All Xlib functions available

### 5. Zero Learning Curve
- X11 developers use familiar API
- Existing documentation applies
- Same code works on Linux and RISC-V

## Two-Block Summary

### Block 1: Application Wrappers (libX11-app.a)
**Files to create:**
1. `libX11-app/src/wrappers.c` - All Xlib function wrappers
2. `libX11-app/src/display.c` - Display struct accessors
3. `include/wmb_api.h` - Entry point table structure

**Total: ~1,400 lines, ~10-20 KB binary**

### Block 2: WMB Xlib (libX11-wmb.a + entry table)
**Files to create:**
1. `libX11-wmb/src/wmb_io.c` - Replace XCB I/O
2. `libX11-wmb/src/wmb_disp.c` - Replace XCB display
3. `libX11-wmb/src/OpenDis.c` - Modified for WMB
4. `libX11-wmb/src/ClDisplay.c` - Modified for WMB
5. `libX11-wmb/include/wmb_internal.h` - Server interface
6. `wmb/src/wmb_entry.c` - Entry point table initialization
7. `wmb/wmb.ld` - Linker script for fixed addresses

**Files to copy unchanged:** ~400 files from libX11

**Total new code: ~1,300 lines**

### Files in X Server (~20 files, ~3,000 lines)
- Implement `wmb_internal.h` interface
- Implement Kdrive interface for TinyX
- Integrate JWM window manager

## Size Comparison

| Component | Traditional | Two-Block |
|-----------|-------------|-----------|
| WMB binary | ~800 KB | ~1.2 MB (includes libX11-wmb) |
| Per-app Xlib | ~400 KB | ~15 KB (wrappers only) |
| 5 applications total | 800 + 5×400 = 2.8 MB | 1200 + 5×15 = 1.275 MB |
| 10 applications total | 800 + 10×400 = 4.8 MB | 1200 + 10×15 = 1.35 MB |

**Savings grow with each additional application!**

## Total Effort Estimate

- **Block 1 (App wrappers)**: 1 week (1,400 lines)
- **Block 2 (WMB transport)**: 1-2 weeks (1,300 lines)
- **X server internal API**: 2-3 weeks (3,000 lines)
- **Testing and integration**: 1-2 weeks

**Total: 5-8 weeks** to complete two-block Xlib architecture.

## Next Steps

1. Define entry point table structure (`wmb_api.h`)
2. Create application wrapper functions (`wrappers.c`)
3. Copy libX11 source to `libX11-wmb/`
4. Implement WMB transport layer (`wmb_io.c`, etc.)
5. Create entry point table initialization (`wmb_entry.c`)
6. Create linker script for fixed addresses (`wmb.ld`)
7. Implement X server internal functions
8. Build and test with simple X11 application
9. Iterate until full compatibility achieved
3. Implement `wmb_io.c` and `wmb_disp.c`
4. Modify `OpenDis.c` for WMB initialization
5. Define `wmb_internal.h` interface
6. Implement X server internal functions
7. Build and test with simple X11 application
8. Iterate until full compatibility achieved
