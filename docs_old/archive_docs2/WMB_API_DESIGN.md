# WMB X11 API Design

## Overview

This document defines the WMB (Window Manager Binary) X11 API architecture using a **two-block design**:
- **WMB binary** loaded at fixed address contains X11 server + JWM + full Xlib
- **Applications** link against thin **libX11-app.a** wrapper library (~15 KB)
- **Entry Point Table** at fixed address routes calls to full Xlib in WMB
- **No client/server** - direct function calls, no IPC/sockets
- **Minimal patching** - clean API layer for updates
- **X11 API compatible** - Developers familiar with Linux X11 can program this platform

## Two-Block Architecture

Per BASE_REQUIREMENTS #1: *"For code size efficiency, the application should use entry point function calls in the WMB, so the xlib should have only wrappers."*

```
APPLICATION SPACE                           WMB SPACE (Fixed Address)
┌─────────────────────────┐                ┌─────────────────────────┐
│      Application        │                │   WMB Entry Table       │
│                         │                │   (Fixed Address)       │
│  ┌───────────────────┐  │                └───────────┬─────────────┘
│  │  libX11-app.a     │  │                            │
│  │  (wrappers only)  │──┼──────────────────────────▶│
│  │  ~15 KB           │  │  Function call to          │
│  └───────────────────┘  │  fixed address             │
└─────────────────────────┘                ┌───────────▼─────────────┐
                                           │   Full Xlib (libX11-wmb)│
                                           │   (all X11 functions)   │
                                           ├─────────────────────────┤
                                           │   X Server (TinyX)      │
                                           ├─────────────────────────┤
                                           │   JWM + HAL             │
                                           └─────────────────────────┘
```

### Block 1: Application Wrappers (libX11-app.a)
- **Size**: ~10-20 KB per application
- **Content**: Wrapper functions that call WMB entry points
- **Linked into**: Each application binary

### Block 2: WMB with Full Xlib
- **Size**: ~600-800 KB (includes full libX11 implementation)
- **Content**: Entry table + libX11-wmb + X server + JWM + HAL
- **Location**: Fixed memory address (loaded by OS)

## OS-Specific Adaptation Strategy

To port X11/TinyX/JWM to bare-metal, focus on customizing the following areas:

1. **X11/Xos.h**: Central header for platform abstraction. Redirect POSIX APIs (gettimeofday, open, read, etc.) to HAL/syscalls. Moderate effort.
2. **TinyX/src/os/**: Platform-dependent code for event loop, timers, signals, file I/O, networking. Stub/replace unsupported features, use HAL for input/timer. High effort.
3. **libc/syscalls.c**: Minimal POSIX layer for bare-metal. Implement required functions for Xos.h and os/. Moderate effort.
4. **JWM/src/timing.c**: Uses time, gettimeofday, localtime, etc. Ensure APIs are available via syscalls.c/Xos.h. Low to moderate effort.
5. **Makefile/configure scripts**: Adjust macros/build rules for platform. Low effort.
6. **X11 headers**: Ensure compatibility with platform/HAL. Low effort.

**Effort Summary:**
- Xos.h: Moderate
- os/: High
- syscalls.c: Moderate
- JWM/timing.c: Low to moderate
- Makefile/configure: Low
- X11 headers: Low

See HAL_SPECIFICATION.md, ARCHITECTURE_ANALYSIS.md, and TINYX_MIGRATION.md for details on how each area is adapted.

## Key Design Principle: X11 API Compatibility

**Requirement**: Developers familiar with Linux/X11 should feel comfortable programming this platform.

**Solution**: Expose standard Xlib API at WMB entry points:
- Use standard X11 function names (XOpenDisplay, XCreateWindow, etc.)
- Use standard X11 data structures (Display, Window, GC, XEvent, etc.)
- Follow standard X11 programming patterns
- Existing X11 tutorials and documentation apply

**Difference from Linux**:
- No network - Display* points to local server only
- Smaller apps - Only thin wrappers linked, full Xlib in WMB
- Faster - Direct calls instead of protocol encoding

## Two-Block Xlib Implementation

### How Applications Call WMB

```
Application Code:
  XCreateWindow(dpy, ...)
        │
        ▼
libX11-app.a (wrapper):
  return WMB_API->XCreateWindow(dpy, ...)
        │
        ▼ (function pointer to fixed address)
WMB Entry Table:
  .XCreateWindow = XCreateWindow_impl
        │
        ▼
libX11-wmb (full implementation):
  XCreateWindow_impl() → calls X server directly
```

### Build Integration

```bash
# Linux build (standard Xlib, ~400 KB added to app)
gcc myapp.c -lX11 -o myapp

# RISC-V WMB build (thin wrappers, ~15 KB added to app)
riscv64-unknown-elf-gcc myapp.c -lX11-app -o myapp.elf
```

Applications are **source-compatible** - same code works on both platforms.

## Architecture Model (Two-Block)

```
┌─────────────────────────────────────────────────────────────┐
│  APPLICATION SPACE                                          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  libX11-app.a (wrappers, ~15 KB)                      │  │
│  │  XOpenDisplay() → WMB_API->XOpenDisplay()             │  │
│  │  XCreateWindow() → WMB_API->XCreateWindow()           │  │
│  │  ... (all Xlib functions wrapped)                     │  │
│  └───────────────────┬───────────────────────────────────┘  │
└──────────────────────┼──────────────────────────────────────┘
                       │ Function calls to fixed address
┌──────────────────────▼──────────────────────────────────────┐
│  WMB @ Fixed Address (0x80100000 example)                   │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Entry Point Table (wmb_entry_t)                      │  │
│  │  .XOpenDisplay = XOpenDisplay                         │  │
│  │  .XCreateWindow = XCreateWindow                       │  │
│  │  .XMapWindow = XMapWindow                             │  │
│  │  ... (~200 function pointers)                         │  │
│  └───────────────────┬───────────────────────────────────┘  │
│                      ▼                                      │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Full Xlib Implementation (libX11-wmb, ~400 KB)       │  │
│  │  - All window management functions                    │  │
│  │  - All graphics operations                            │  │
│  │  - All event handling                                 │  │
│  │  - Direct calls to X server (no protocol encoding)    │  │
│  └───────────────────┬───────────────────────────────────┘  │
│                      ▼                                      │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Window Manager (JWM Core)                            │  │
│  │  - Direct function calls to X11 (no Xlib)             │  │
│  │  - Window decorations, menus                          │  │
│  └───────────────────┬───────────────────────────────────┘  │
│                      ▼                                      │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  X11 Server Core (TinyX/Kdrive, no networking)        │  │
│  │  - Window tree, resources                             │  │
│  │  - Graphics operations                                │  │
│  └───────────────────┬───────────────────────────────────┘  │
│                      ▼                                      │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  HAL (Hardware Abstraction)                           │  │
│  │  - Fixed framebuffer config                           │  │
│  │  - Input polling                                      │  │
│  └───────────────────┬───────────────────────────────────┘  │
└────────────────────┬─┴──────────────────────────────────────┘
                     ▼
              Hardware (RISC-V)

┌─────────────────────────────────────────────────────────────┐
│  Application Space (RAM)                                    │
│  #include <X11/Xlib.h>      /* Standard X11 header */       │
│  #include <X11/Xutil.h>                                     │
│                                                             │
│  int main() {                                               │
│      Display *dpy = XOpenDisplay(NULL);  /* WMB call */     │
│      Window root = DefaultRootWindow(dpy);                  │
│      Window win = XCreateSimpleWindow(dpy, root,            │
│                       0, 0, 640, 480, 1, 0, 0xFFFFFF);      │
│      XMapWindow(dpy, win);                                  │
│      XEvent ev;                                             │
│      while(1) {                                             │
│          XNextEvent(dpy, &ev);  /* Standard X11 event */    │
│          // Standard X11 event handling                     │
│      }                                                      │
│  }                                                          │
│  /* This is standard X11 code! Works on Linux too! */       │
└──Xlib-Compatible WMB API Definition

### Standard X11 Headers (Provided with WMB)

Applications use standard X11 headers from the `X11/` directory you already have:
- `X11/Xlib.h` - Core Xlib API
- `X11/Xutil.h` - Utility functions
- `X11/X.h` - Protocol definitions
- `X11/Xatom.h` - Standard atoms

### Core Xlib Functions in WMB

```c
/* X11/Xlib.h - Standard Xlib API (implemented in WMB) */

/* ESSENTIAL FUNCTIONS (~50 most commonly used) */

/* Display Management */
Display* XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *display);
int XFlush(Display *display);
int XSync(Display *display, Bool discard);

/* Window Management */
Window XCreateWindow(Display *display, Window parent,
                    int x, int y, unsigned int width, unsigned int height,
                    unsigned int border_width, int depth, unsigned int class,
                    Visual *visual, unsigned long valuemask,
                    XSetWindowAttributes *attributes);

Window XCreateSimpleWindow(Display *display, Window parent,
                          int x, int y, unsigned int width, unsigned int height,
                          unsigned int border_width,
                          unsigned long border, unsigned long background);

int XDestroyWindow(Display *display, Window w);
int XMapWindow(Display *display, Window w);
int XUnmapWindow(Display *display, Window w);
int XMoveWindow(Display *display, Window w, int x, int y);
int XResizeWindow(Display *display, Window w, unsigned int width, unsigned int height);
int XRaiseWindow(Display *display, Window w);
int XLowerWindow(Display *display, Window w);

/* Graphics Context */
GC XCreateGC(Display *display, Drawable d,
            unsigned long valuemask, XGCValues *values);
int XFreeGC(Display *display, GC gc);
int XSetForeground(Display *display, GC gc, unsigned long foreground);
int XSetBackground(Display *display, GC gc, unsigned long background);

/* Drawing Operations */
int XDrawPoint(Display *display, Drawable d, GC gc, int x, int y);
int XDrawLine(Display *display, Drawable d, GC gc,
             int x1, int y1, int x2, int y2);
int XDrawRectangle(Display *display, Drawable d, GC gc,
                  int x, int y, unsigned int width, unsigned int height);
int XFillRectangle(Display *display, Drawable d, GC gc,
                  int x, int y, unsigned int width, unsigned int height);
int XDrawString(Display *display, Drawable d, GC gc,
               int x, int y, const char *string, int length);
int XClearWindow(Display *display, Window w);
int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc,
             int src_x, int src_y, unsigned int width, unsigned int height,
             int dest_x, int dest_y);

/* Event Handling */
int XNextEvent(Display *display, XEvent *event_return);
int XPending(Display *display);
Bool XCheckWindowEvent(Display *display, Window w, long event_mask,
                      XEvent *event_return);
int XSelectInput(Display *display, Window w, long event_mask);

/* Atoms and Properties */
Atom XInternAtom(Display *display, const char *atom_name, Bool only_if_exists);
char* XGetAtomName(Display *display, Atom atom);
int XChangeProperty(Display *display, Window w, Atom property, Atom type,
                   int format, int mode, const unsigned char *data, int nelements);
int XGetWindowProperty(Display *display, Window w, Atom property,
                      long long_offset, long long_length, Bool delete,
                      Atom req_type, Atom *actual_type_return,
                      int *actual_format_return, unsigned long *nitems_return,
                      unsigned long *bytes_after_return,
                      unsigned char **prop_return);

/* Utility */
int XStoreName(Display *display, Window w, const char *window_name);
int XFetchName(Display *display, Window w, char **window_name_return);
Window DefaultRootWindow(Display *display);
int DefaultScreen(Display *display);
unsigned long BlackPixel(Display *display, int screen_number);
unsigned long WhitePixel(Display *display, int screen_number);

/* Standard X11 data structures (from X11/Xlib.h) */
typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long Drawable;
typedef unsigned long Atom;
typedef struct _XGC *GC;

/* XEvent is standard X11 event union */
typedef union _XEvent {
    int type;
    XAnyEvent xany;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XExposeEvent xexpose;
    XConfigureEvent xconfigure;
    /* ... standard X11 event types ... */
} XEvent;
```

## Custom Xlib Implementation Strategy

### What Needs to Be Implemented

You must create a **custom Xlib library** (`libX11-wmb.a`) that:

1. **Provides standard Xlib API** - All function signatures match standard Xlib
2. **Replaces networking** - Instead of sockets, calls X server functions directly
3. **Shares memory space** - Client and server in same address space
4. **Uses standard headers** - Applications include standard `X11/Xlib.h`

### Implementation Layers

```
┌────────────────────────────────────────────────┐
│ Application Code                               │
│ Uses: XOpenDisplay(), XCreateWindow(), etc.    │
└────────────────┬───────────────────────────────┘
                 │ #include <X11/Xlib.h>
┌────────────────▼───────────────────────────────┐
│ libX11-wmb.a (Custom Xlib Implementation)      │
│                                                │
│ Sources to write:                              │
│ - Display.c    (XOpenDisplay, XCloseDisplay)   │
│ - Window.c     (XCreateWindow, XMapWindow)     │
│ - Events.c     (XNextEvent, XSelectInput)      │
│ - Graphics.c   (XDrawLine, XFillRectangle)     │
│ - GC.c         (XCreateGC, XSetForeground)     │
│ - Atoms.c      (XInternAtom, XChangeProperty)  │
│                                                │
│ Implementation: Direct calls to server         │
└────────────────┬───────────────────────────────┘
                 │ Function calls (no X protocol)
┌────────────────▼───────────────────────────────┐
│ X11 Server Internal Functions                  │
│ (TinyX/Kdrive ported code)                     │
│                                                │
│ - x11_create_window_internal()                 │
│ - x11_map_window_internal()                    │
│ - x11_draw_rectangle_internal()                │
│ - x11_get_next_event_internal()                │
└────────────────┬───────────────────────────────┘
                 │
┌────────────────▼───────────────────────────────┐
│ HAL (Hardware Abstraction Layer)               │
└────────────────────────────────────────────────┘
```

### File Organization

```
x11-wmb-project/
├── include/
│   └── X11/              # Standard X11 headers (use existing X11/ dir)
│       ├── Xlib.h
│       ├── Xutil.h
│       ├── X.h
│       └── ...
│
├── libX11-wmb/          # Custom Xlib implementation (NEW)
│   ├── Makefile
│   ├── Display.c        # XOpenDisplay, XCloseDisplay
│   ├── Window.c         # XCreate/Destroy/Map/Unmap Window
│   ├── Events.c         # XNextEvent, XPending, XSelectInput
│   ├── Graphics.c       # XDrawLine, XFillRectangle, XCopyArea
│   ├── GC.c             # XCreateGC, XSetForeground, etc.
│   ├── Atoms.c          # XInternAtom, XChangeProperty
│   └── x11_internal.h   # Interface to server internal functions
│
├── x11-server/          # X11 server core (TinyX/Kdrive ported)
│   ├── Makefile
│   ├── kdrive/          # Kdrive interface implementation
│   ├── event.c          # Event queue and dispatch
│   ├── grph.c           # Graphics operations (using HAL)
│   └── ...
│
├── jwm/                 # Window manager (existing)
│   └── src/
│
└── hal/                 # Hardware abstraction layer
    └── framebuffer.c
```

### Compatibility with X11 Code

```c
/* This code works on both Linux and your RISC-V platform! */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>

int main(void) {
    Display *dpy;
    Window win;
    XEvent event;
    GC gc;

    /* Open display - on RISC-V this is WMB call, on Linux it's socket */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
       Xlib Implementation Layer (Xlib.c)

This is a minimal libX11 implementation that calls X11 server directly (no protocol encoding).

```c
/* Xlib.c - Xlib-compatible WMB API implementation */

#include <X11/Xlib.h>
#include "x11_internal.h" /* X11 server internal API */
#include "wm_internal.h"  /* JWM internal API */

/* Display structure - simplified for local server */
struct _XDisplay {
    int screen_number;
    WINDOW *root_window;      /* Pointer to X11 server root window */
    void *server_context;     /* X11 server state */
    XEvent event_queue[256];  /* Local event queue */
    int event_count;
    /* ... other display state ... */
};

/* Global display (single local display) */
static struct _XDisplay local_display;
static int display_initialized = 0;

/* === Display Management === */

Display* XOpenDisplay(const char *display_name)
{
    (void)display_name;  /* Ignored - always local display */

    if (display_initialized) {
        return &local_display;  /* Return existing display */
    }

    /* Initialize HAL */
    if (hal_init() != HAL_OK) {
        return NULL;
    }

    /* Initialize X11 server (no networking) */
    local_display.server_context = x11_server_init_internal();
    if (!local_display.server_context) {
        return NULL;
    }

    /* Get root window from server */
    local_display.root_window = x11_get_root_window_internal();
    local_display.screen_number = 0;
    local_display.event_count = 0;

    /* Initialize window manager */
    if (!wm_init_internal()) {
        return NULL;
    }

    display_initialized = 1;
    return &local_display;
}

int XCloseDisplay(Display *display)
{
    if (display != &local_display) return BadValue;

    wm_shutdown_internal();
    x11_server_shutdown_internal();
    display_initialized = 0;

    return Success;
}

/* === Window Management === */

Window XCreateWindow(Display *display, Window parent,
                    int x, int y, unsigned int width, unsigned int height,
                    unsigned int border_width, int depth, unsigned int class,
                    Visual *visual, unsigned long valuemask,
                    XSetWindowAttributes *attributes)
{
    if (display != &local_display) return None;

    /* Direct call to X11 server (no protocol encoding!) */
    WINDOW *win = x11_create_window_internal(
        (WINDOW*)parent,
        x, y, width, height,
        border_width,
        depth,
        class,
        (VISUAL*)visual,
        valuemask,
        attributes
    );

    if (!win) return None;

    /* Let WM manage the window (add decorations) */
    wm_manage_window_internal(win);

    return (Window)win;
}

Window XCreateSimpleWindow(Display *display, Window parent,
                          int x, int y, unsigned int width, unsigned int height,
                          unsigned int border_width,
                          unsigned long border, unsigned long background)
{
    XSetWindowAttributes attrs;
    attrs.border_pixel = border;
    attrs.background_pixel = background;

    return XCreateWindow(display, parent, x, y, width, height,
                        border_width, CopyFromParent, InputOutput,
                        CopyFromParent,
                        CWBorderPixel | CWBackPixel,
                        &attrs);
}

int XMapWindow(Display *display, Window w)
{
    if (display != &local_display) return BadValue;

    /* Direct call to server */
    x11_map_window_internal((WINDOW*)w);

    return Success;
}

int XDestroyWindow(Display *display, Window w)
{
    if (display != &local_display) return BadValue;

    /* Unmanage from WM first */
    wm_unmanage_window_internal((WINDOW*)w);

    /* Destroy in server */
    x11_destroy_window_internal((WINDOW*)w);

    return Success;
}

/* === Graphics Context === */

GC XCreateGC(Display *display, Drawable d,
            unsigned long valuemask, XGCValues *values)
{
    if (display != &local_display) return NULL;

    /* Direct call to server */
    GCONTEXT *gc = x11_create_gc_internal((DRAWABLE*)d, valuemask, values);

    return (GC)gc;
}

int XSetForeground(Display *display, GC gc, unsigned long foreground)
{
    if (display != &local_display) return BadValue;

    x11_change_gc_internal((GCONTEXT*)gc, GCForeground, foreground);

    return Success;
}

/* === Drawing Operations === */

int XFillRectangle(Display *display, Drawable d, GC gc,
                  int x, int y, unsigned int width, unsigned int height)
{
    if (display != &local_display) return BadValue;

    /* Direct call to server drawing code */
    x11_poly_fill_rectangle_internal((DRAWABLE*)d, (GCONTEXT*)gc,
                                    x, y, width, height);

    return Success;
}

int XDrawString(Display *display, Drawable d, GC gc,
               int x, int y, const char *string, int length)
{
    if (display != &local_display) return BadValue;

    x11_draw_image_string_internal((DRAWABLE*)d, (GCONTEXT*)gc,
                                  x, y, string, length);

    return Success;
}

/* === Event Handling === */

int XNextEvent(Display *display, XEvent *event_return)
{
    if (display != &local_display) return BadValue;

    while (1) {
        /* Check local queue first */
        if (display->event_count > 0) {
            *event_return = display->event_queue[0];
            /* Shift queue */
            for (int i = 0; i < display->event_count - 1; i++) {
                display->event_queue[i] = display->event_queue[i + 1];
            }
            display->event_count--;

            /* Let WM process event first */
            if (wm_handle_event_internal(event_return)) {
                /* WM consumed it, get next */
                continue;
            }

            return Success;
        }

        /* Get events from HAL (keyboard, mouse) */
        hal_event_t hal_ev;
        if (hal_input_wait(&hal_ev, 100) == HAL_OK) {
            /* Convert HAL event to X11 event */
            convert_hal_to_xevent(&hal_ev, event_return);

            /* Generate expose events for dirty regions */
            x11_generate_expose_events_internal(display);

            return Success;
        }
    }
}

/* === Utility Functions === */

Window DefaultRootWindow(Display *display)
{
    if (display != &local_display) return None;
    return (Window)display->root_window;
}

int DefaultScreen(Display *display)
{
    if (display != &local_display) return 0;
    return display->screen_number;
}

unsigned long BlackPixel(Display *display, int screen_number)
{
    (void)display; (void)screen_number;
    return hal_color_rgb(0, 0, 0);
}

unsigned long WhitePixel(Display *display, int screen_number)
{
    (void)display; (void)screen_number;
    return hal_color_rgb(255, 255, 255);
}

int XStoreName(Display *display, Window w, const char *window_name)
{
    if (display != &local_display) return BadValue;

    Atom name_atom = XInternAtom(display, "WM_NAME", False);
    return XChangeProperty(display, w, name_atom, XA_STRING, 8,
                          PropModeReplace,
                          (unsigned char*)window_name,
                          strlen(window_name));
}  /* Get event from X11 server event queue */
    void *x11_event = x11_get_next_event(timeout_ms);

    if (!x11_event) {
        event->type = GUI_EVENT_NONE;
        return 0;
    }

    /* Let WM process first (for decorations, menus, etc.) */
    if (wm_handle_event(x11_event)) {
        /* WM consumed the event (e.g., clicked on decoration) */
        return gui_wait_event_impl(event, timeout_ms);
    }

    /* Convert X11 event to GUI event */
    convert_x11_to_gui_event(x11_event, event);

    return 1;
}

/* API Table at fixed WMB address */
__attribute__((section(".wmb_api")))
const gui_api_t GUI_API_TABLE = {
    .version = (GUI_API_VERSION_MAJOR << 16) | GUI_API_VERSION_MINOR,
    .api_revision = 1,

    .init = gui_init_impl,
    .shutdown = gui_shutdown_impl,
    .flush = gui_flush_impl,

    .create_window = gui_create_window_impl,
    .destroy_window = gui_destroy_window_impl,
    .show_window = gui_show_window_impl,
    .hide_window = gui_hide_window_impl,
    .move_window = gui_move_window_impl,
    .resize_window = gui_resize_window_impl,
    .raise_window = gui_raise_window_impl,
    .lower_window = gui_lower_window_impl,
    .set_title = gui_set_title_impl,

    .draw_point = gui_draw_point_impl,
    .draw_line = gui_draw_line_impl,
    .draw_rect = gui_draw_rect_impl,
    .fill_rect = gui_fill_rect_impl,
    .draw_text = gui_draw_text_impl,
    .clear_window = gui_clear_window_impl,
    .copy_area = gui_copy_area_impl,

    .wait_event = gui_wait_event_impl,
    .poll_event = gui_poll_event_impl,
    .peek_event = gui_peek_event_impl,

    .rgb_to_pixel = gui_rgb_to_pixel_impl,
    .get_screen_size = gui_get_screen_size_impl,
};
```

### 2. Modified X11 Server Interface

```c
/* x11_internal.h - Internal X11 API (not exposed to apps) */

/* Direct internal functions - no protocol encoding */
void* x11_server_init(void);
void* x11_create_window_internal(int x, int y, int w, int h, int border, uint32_t bg);
void x11_map_window_internal(void *window);
void x11_destroy_window_internal(void *window);
void* x11_create_gc_in (Standard X11 Code)

```c
/* xhello.c - Standard X11 application */
/* Works on both Linux and RISC-V WMB platform! */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    Display *display;
    Window window;
    XEvent event;
    GC gc;
    int screen;

    /* Open display connection */
    /* On Linux: connects via Unix socket */
    /* On RISC-V: direct WMB call */
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    screen = DefaultScreen(display);

    /* Create window - standard X11 API */
    window = XCreateSimpleWindow(display,
                                 DefaultRootWindow(display),
                                 100, 100,   /* x, y */
                                 640, 480,   /* width, height */
                                 2,          /* border width */
                                 BlackPixel(display, screen),  /* border */
                                 WhitePixel(display, screen)); /* background */

    /* Set window title - standard X11 */
    XStoreName(display, window, "Hello X11 World");

    /* Select events we want */
    XSelectInput(display, window,
                ExposureMask | KeyPressMask | ButtonPressMask);

    /* Display the window */
    XMapWindow(display, window);

    /* Create graphics context */
    gc = XCreateGC(display, window, 0, NULL);

    /* Set drawing color to black */
    XSetForeground(display, gc, BlackPixel(display, screen));

    /* Event loop - standard X11 */
    while (1) {
        XNextEvent(display, &event);

        switch (event.type) {
            case Expose:
                /* Redraw on expose */
                if (event.xexpose.count == 0) {
                    /* Draw text - standard X11 */
                    XDrawString(display, window, gc,
                               50, 50,
                               "Hello, X11 World!", 17);

                    /* Draw rectangle - standard X11 */
                    XSetForeground(display, gc,
                                 BlackPixel(display, screen));
                    XDrawRectangle(display, window, gc,
                                  20, 20, 200, 100);

                    /* Fill rectangle - standard X11 */
                    XSetForeground(display, gc, 0xFF0000); /* Red */
               Xlib-Compatible Architecture

1. ✅ **Standard X11 API** - Developers use familiar Xlib functions
2. ✅ **Code portability** - Same code works on Linux and RISC-V
3. ✅ **No learning curve** - X11 tutorials and books apply directly
4. ✅ **No IPC overhead** - Direct function calls (faster than Linux X11!)
5. ✅ **Small binary** - No protocol encoding/decoding needed
6. ✅ **Easy debugging** - Use existing X11 knowledge and tools
7. ✅ **Stable API** - Standard X11 API doesn't change
8. ✅ **Rich ecosystem** - Existing X11 code/libraries can be ported
   Developer Experience Comparison

### Linux X11 Development
```c
/* On Linux workstation */
#include <X11/Xlib.h>
Display *dpy = XOpenDisplay(":0");  /* Connect to X server via socket */
Window win = XCreateSimpleWindow(dpy, ...);
XNextEvent(dpy, &event);  /* Decoded from X11 wire protocol */
```

### RISC-V WMB Development (Exactly the Same!)
```c
/* On RISC-V bare metal */
#include <X11/Xlib.h>  /* Same header! */
Display *dpy = XOpenDisplay(NULL);  /* Direct WMB call, no socket */
Window win = XCreateSimpleWindow(dpy, ...);  /* Same API! */
XNextEvent(dpy, &event);  /* Direct function call, no protocol! */
```

**Result**: Developers write the same code, test on Linux, deploy to RISC-V!

## Summary

This Xlib-compatible WMB architecture:
- ✅ Uses **standard X11 API** (XOpenDisplay, XCreateWindow, etc.)
- ✅ Developers **use existing X11 knowledge** - no new API to learn
- ✅ Applications **portable between Linux and RISC-V**
- ✅ Same code, same headers, same programming model
- ✅ **Faster than Linux** X11 (no sockets, no protocol encoding)
- ✅ **Smaller binary** - Xlib implementation is minimal (~50 functions)
- ✅ Perfect for embedded/bare-metal RISC-V systems
- ✅ Allows using existing X11 tutorials, books, and documentation
                XCloseDisplay(display);
                exit(0);
                break;
        }
    }

    return 0;
}

/*
 * Compilation:
 *
 * For Linux:
 *   gcc -o xhello xhello.c -lX11
 *   ./xhello
 *
 * For RISC-V WMB platform:
 *   riscv64-unknown-elf-gcc -o xhello.elf xhello.c -T wmb_app.ld
 *   # No -lX11 needed - Xlib is in WMB!
 *
 * The SAME source code works on both platforms!
 */
```

## Advanced Example: Using Xlib Utilities

```c
/* xclock_simple.c - Simple clock using standard X11 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define WIDTH 200
#define HEIGHT 200

void draw_clock(Display *dpy, Window win, GC gc)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int cx = WIDTH / 2;
    int cy = HEIGHT / 2;
    int radius = 80;

    /* Clear window */
    XClearWindow(dpy, win);

    /* Draw clock face */
    XDrawArc(dpy, win, gc, cx - radius, cy - radius,
            radius * 2, radius * 2, 0, 360 * 64);

    /* Hour hand */
    double hour_angle = (tm->tm_hour % 12 + tm->tm_min / 60.0) * 30 * M_PI / 180;
    int hx = cx + (int)(radius * 0.5 * sin(hour_angle));
    int hy = cy - (int)(radius * 0.5 * cos(hour_angle));
    XDrawLine(dpy, win, gc, cx, cy, hx, hy);

    /* Minute hand */
    double min_angle = tm->tm_min * 6 * M_PI / 180;
    int mx = cx + (int)(radius * 0.7 * sin(min_angle));
    int my = cy - (int)(radius * 0.7 * cos(min_angle));
    XDrawLine(dpy, win, gc, cx, cy, mx, my);
}

int main(void)
{
    Display *dpy = XOpenDisplay(NULL);
    Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                                    100, 100, WIDTH, HEIGHT, 1, 0, 0xFFFFFF);
    XStoreName(dpy, win, "Clock");
    XSelectInput(dpy, win, ExposureMask);
    XMapWindow(dpy, win);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, 0x000000);

    /* This is standard X11 code - works everywhere! */
    while (1) {
        XEvent ev;
        if (XCheckWindowEvent(dpy, win, ExposureMask, &ev)) {
            draw_clock(dpy, win, gc);
        }
        /* Simple timer - in real code use XtAppAddTimeOut */
        usleep(1000000);  /* 1 second */
        draw_clock(dpy, win, gc);
    }ader for applications
gui_api.h: api_generator
	./api_generator > gui_api.h
```

## Minimal Patching Strategy

### Updates to X11 Server or JWM

```
To update TinyX/Kdrive or JWM:

1. Update source files in their directories
   ├── tinyx/kdrive/*.c     (just update these)
   └── jwm/src/*.c          (just update these)

2. Keep adapter layer separate:
   ├── api_impl.c           (rarely changes)
   ├── x11_internal.h       (stable interface)
   └── wm_internal.h        (stable interface)

3. Rebuild WMB binary
   make clean
   make wmb.bin

4. Applications don't change (API is stable)
```

### Interface Stability

```c
/* Stable interfaces between layers */

/* APP → API: gui_api.h */
/* Never changes (or only adds functions) */

/* API → WM: wm_internal.h */
/* Isolates WM implementation details */

/* API → X11: x11_internal.h */
/* Isolates X11 implementation details */

/* WM → X11: Direct function calls */
/* Internal to WMB, can change freely */

/* X11 → HAL: hal_*.h */
/* Platform-specific, change per platform */
```

## Example Application

```c
/* example_app.c */

#include "gui_api.h"
#include <stdio.h>

int main(void)
{
    /* Initialize GUI system */
    if (GUI_API->init() != 0) {
        printf("Failed to initialize GUI\n");
        return 1;
    }

    /* Create window */
    gui_window_attr_t attr = {
        .x = 100,
        .y = 100,
        .width = 640,
        .height = 480,
        .title = "Hello World",
        .background_color = GUI_API->rgb_to_pixel(255, 255, 255),
        .border_width = 2,
        .resizable = 1,
        .closable = 1,
    };

    int win = GUI_API->create_window(&attr);
    if (win < 0) {
        printf("Failed to create window\n");
        return 1;
    }

    /* Show window */
    GUI_API->show_window(win);

    /* Draw some graphics */
    GUI_API->fill_rect(win, 50, 50, 200, 100,
                       GUI_API->rgb_to_pixel(255, 0, 0));
    GUI_API->draw_text(win, 60, 80, "Hello, World!",
                       GUI_API->rgb_to_pixel(255, 255, 255));

    /* Event loop */
    while (1) {
        gui_event_t event;

        if (GUI_API->wait_event(&event, -1) > 0) {
            switch (event.type) {
                case GUI_EVENT_WINDOW_CLOSE:
                    GUI_API->destroy_window(win);
                    return 0;

                case GUI_EVENT_MOUSE_PRESS:
                    /* Draw circle at click position */
                    GUI_API->fill_rect(win,
                                      event.data.mouse.x - 5,
                                      event.data.mouse.y - 5,
                                      10, 10,
                                      GUI_API->rgb_to_pixel(0, 0, 255));
                    break;

                case GUI_EVENT_KEY_PRESS:
                    if (event.data.key.keycode == 'q') {
                        GUI_API->destroy_window(win);
                        return 0;
                    }
                    break;
            }
        }
    }

    return 0;
}
```

## Benefits of This Architecture

1. ✅ **No IPC overhead** - Direct function calls
2. ✅ **Simple updates** - Clean interface layers
3. ✅ **Small binary** - No protocol encoding/decoding
4. ✅ **Fast** - No context switching or socket overhead
5. ✅ **Stable API** - Applications don't break with WMB updates
6. ✅ **Fixed WMB address** - Easy to call from any application
7. ✅ **Minimal patching** - Update X11/JWM independently

## Summary

**The Key Implementation Requirement: Custom Xlib**

To maintain X11 API compatibility while running on bare-metal RISC-V, you must:

1. **Implement a custom Xlib library** (`libX11-wmb.a`)
   - Provides exact same API as standard Xlib (XOpenDisplay, XCreateWindow, etc.)
   - Implementation calls directly into X server functions instead of using sockets
   - No X protocol encoding/decoding - direct function calls

2. **Keep standard X11 headers unchanged**
   - Applications include standard `<X11/Xlib.h>`
   - Use standard X11 data structures (Display, Window, GC, XEvent)
   - Standard X11 documentation applies

3. **Implement Kdrive interface**
   - Remove networking code (already done in TinyX)
   - Implement KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs
   - Expose internal functions for Xlib to call

4. **Link everything into WMB binary**
   - Custom Xlib + X11 Server + JWM + HAL
   - Applications link against `libX11-wmb.a` instead of `libX11.so`
   - Same application source code works on Linux and RISC-V

**Result**: Developers familiar with X11 on Linux can write code for your RISC-V platform without learning new APIs. The same code compiles for both platforms - just link against different Xlib implementations.

```
# Linux
gcc myapp.c -lX11 -o myapp          # Uses system libX11.so (sockets)

# RISC-V
riscv64-elf-gcc myapp.c -lX11-wmb   # Uses custom libX11-wmb.a (direct calls)
```
