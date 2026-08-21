# X11 Client-Server Layer Analysis

## Document Purpose

This document provides a detailed technical analysis of how the X11 client-server communication layer currently works between JWM (window manager) and TinyX (X server). This analysis serves as the foundation for implementing the **Two-Block Architecture bypass** specified in BASE_REQUIREMENTS #1 for the WMB X11 Window System on Tima OS.

---

## ⚠️ CRITICAL UPDATE (2026-01-25) - WMB Implementation Strategy

### Evolution of Understanding

This document has been updated multiple times as we discovered the true architecture:

| Discovery | Impact |
|-----------|--------|
| **XCB Centralization** | ALL transport goes through `xcb_io.c` (885 lines) |
| **SyncHandle() Pattern** | Every API function calls `SyncHandle()` at the end |
| **dpy->synchandler Hook** | Built-in mechanism to intercept request completion |

### Final WMB Strategy: SyncHandle Hook

**We don't need to modify ANY libX11 API files!**

The solution uses the existing `SyncHandle()` mechanism:

```c
#define SyncHandle() \
    if (dpy->synchandler) (*dpy->synchandler)(dpy)
```

**WMB Approach:**
1. `_XGetRequest()` allocates buffer, records opcode
2. API code fills request structure (unchanged)
3. `SyncHandle()` → `wmb_sync_handler()` → `ProcVector[opcode](client)`

### Key Documents

| Document | Purpose |
|----------|---------|
| [XCB_STUB_ARCHITECTURE.md](XCB_STUB_ARCHITECTURE.md) | **RECOMMENDED** - Full implementation design |
| [XCB_DEPENDENCY_RISK_ASSESSMENT.md](XCB_DEPENDENCY_RISK_ASSESSMENT.md) | Risk analysis of XCB dependencies |
| [XOPENDISPLAY_ANALYSIS.md](XOPENDISPLAY_ANALYSIS.md) | XOpenDisplay specific analysis |

### Summary Statistics

| Metric | Value | WMB Impact |
|--------|-------|------------|
| Files using GetReq macros | 172/379 (45%) | **UNCHANGED** |
| Files using SyncHandle() | 168 | **UNCHANGED** (we hook it) |
| Files using _XReply | 67 | **UNCHANGED** |
| New files needed | ~2 | wmb_stubs.c, WMBclient.c |
| Estimated new code | ~650 lines | Much less than feared |

---

## 1. Architecture Overview

### Current X11 Communication Model

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         CURRENT ARCHITECTURE                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   ┌──────────────┐                              ┌──────────────────┐    │
│   │     JWM      │                              │      TinyX       │    │
│   │  (WM Client) │                              │    (X Server)    │    │
│   └──────┬───────┘                              └────────┬─────────┘    │
│          │                                               │              │
│          │ JX* wrappers                                  │              │
│          ▼                                               │              │
│   ┌──────────────┐                                       │              │
│   │   libX11     │                                       │              │
│   │  (Xlib API)  │                                       │              │
│   └──────┬───────┘                                       │              │
│          │                                               │              │
│          │ GetReq(), Data()                              │              │
│          ▼                                               │              │
│   ┌──────────────┐     Socket/IPC      ┌────────────────┤              │
│   │   libxcb     │ ◄──────────────────►│  Xtrans/OS I/O │              │
│   │  (Transport) │     Wire Protocol   │                │              │
│   └──────────────┘                     └────────────────┘              │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | Role | Key Files |
|-----------|------|-----------|
| **JWM** | Window Manager client application | `jwm/src/main.c`, `jwm/src/event.c` |
| **libX11** | Client-side X11 API implementation | `libX11/src/*.c`, `libX11/include/X11/Xlibint.h` |
| **libxcb** | Low-level transport (socket I/O) | `libX11/src/xcb_io.c` |
| **TinyX dix** | Device-independent X server core | `tinyx/dix/dispatch.c` |
| **TinyX os** | Server-side I/O and connections | `tinyx/os/io.c`, `tinyx/os/connection.c` |

---

## 2. JWM Client-Side Analysis

### 2.1 X11 Connection Establishment

JWM establishes connection to the X server in `main.c`:

```c
// jwm/src/main.c - Global display handle
Display *display = NULL;
int displayWidth, displayHeight;

void OpenConnection(void)
{
   display = JXOpenDisplay(NULL);    // Wrapper for XOpenDisplay
   if(JUNLIKELY(!display)) {
      if(displayString) {
         Warning(_("could not open display: %s"), displayString);
      } else {
         Warning(_("could not open display"));
      }
      DoExit(1);
   }
   displayFD = ConnectionNumber(display);  // Get socket file descriptor
}
```

### 2.2 JX* Wrapper Macros

JWM uses wrapper macros (likely defined via autoconf) that map to standard Xlib functions:

| JWM Wrapper | Xlib Function | Purpose |
|-------------|---------------|---------|
| `JXOpenDisplay()` | `XOpenDisplay()` | Open server connection |
| `JXCloseDisplay()` | `XCloseDisplay()` | Close connection |
| `JXNextEvent()` | `XNextEvent()` | Get next event from queue |
| `JXPending()` | `XPending()` | Check pending events |
| `JXSendEvent()` | `XSendEvent()` | Send event to window |
| `JXFlush()` | `XFlush()` | Flush output buffer |

### 2.3 Event Loop

JWM's main event loop in `event.c`:

```c
// jwm/src/event.c
void EventLoop(void)
{
   XEvent event;
   for(;;) {
      WaitForEvent(&event);      // Blocks waiting for X events
      ProcessEvent(&event);      // Dispatch event to handlers
   }
}

char WaitForEvent(XEvent *event)
{
   fd_set fds;
   int fd = displayFD;           // ConnectionNumber(display)
   
   // Wait for data on X connection
   FD_ZERO(&fds);
   FD_SET(fd, &fds);
   select(fd + 1, &fds, NULL, NULL, NULL);
   
   // Get event from Xlib queue
   JXNextEvent(display, event);
   return 1;
}
```

---

## 3. libX11 Request Building Analysis

### 3.1 Core Data Structures

#### Display Connection Structure (`Xlibint.h`)

```c
struct _XDisplay {
    XExtData *ext_data;          /* hook for extension to hang data */
    int fd;                       /* Network socket (from xcb) */
    
    /* Output buffer management */
    char *buffer;                 /* Output buffer starting address */
    char *bufptr;                 /* Current buffer pointer */
    char *bufmax;                 /* Buffer end address */
    
    /* Request tracking */
    unsigned long request;        /* sequence number of last request */
    unsigned long last_request_read; /* seq number of last event/reply/error */
    
    /* Screen info */
    Screen *screens;              /* pointer to list of screens */
    int default_screen;           /* default screen for operations */
    
    /* Connection state */
    Bool synchandler;             /* synchronous mode flag */
    struct _XLockInfo *lock;      /* multi-threading lock info */
    
    /* XCB integration */
    void *xcb;                    /* XCB connection */
    // ... more fields
};
```

#### Buffer Constants

```c
#define BUFSIZE 2048              /* X output buffer size */
#define PTSPERBATCH 1024          /* point/line batching */
#define WLNSPERBATCH 50          /* wide line batching */
#define FCTSPERBATCH 256         /* filled char text batch */
```

### 3.2 Request Macros

#### GetReq Macro - Allocates Request Buffer

```c
#define GetReq(name, req) \
    GetReqSized(name, SIZEOF(x##name##Req), req)

#define GetReqSized(name, sz, req) \
    req = (x##name##Req *) _XGetRequest(dpy, X_##name, sz)
```

The `_XGetRequest()` function:
1. Checks if buffer has space for request
2. If not, calls `_XFlush()` to send buffered data
3. Allocates space in buffer
4. Sets up request header (reqType, length)
5. Returns pointer to request structure

#### GetReqExtra Macro - Request with Variable Data

```c
#define GetReqExtra(name, n, req) \
    GetReqSized(name, SIZEOF(x##name##Req) + n, req)
```

#### Data Macro - Copy Data to Buffer

```c
#define Data(dpy, data, len) { \
    if (dpy->bufptr + (len) <= dpy->bufmax) { \
        memcpy(dpy->bufptr, data, (size_t)(len)); \
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3); /* pad to 4 bytes */ \
    } else \
        _XSend(dpy, (_Xconst char*)(data), (long)(len)); \
}
```

#### BufAlloc Macro - Allocate from Buffer

```c
#define BufAlloc(type, ptr, n) \
    if (dpy->bufptr + (n) > dpy->bufmax) \
        _XFlush(dpy); \
    ptr = (type) dpy->bufptr; \
    memset(ptr, '\0', n); \
    dpy->bufptr += (n);
```

### 3.3 Example: XCreateSimpleWindow Request Building

```c
// libX11/src/CrWindow.c
Window XCreateSimpleWindow(
    register Display *dpy,
    Window parent,
    int x, int y,
    unsigned int width, unsigned int height,
    unsigned int borderWidth,
    unsigned long border,
    unsigned long background)
{
    Window wid;
    register xCreateWindowReq *req;

    LockDisplay(dpy);                           // Thread safety
    
    GetReqExtra(CreateWindow, 8, req);          // Allocate req + 8 bytes for values
    
    // Fill in request fields
    req->parent = parent;
    req->x = x;
    req->y = y;
    req->width = width;
    req->height = height;
    req->borderWidth = borderWidth;
    req->depth = 0;
    req->class = CopyFromParent;
    req->visual = CopyFromParent;
    wid = req->wid = XAllocID(dpy);             // Allocate resource ID
    req->mask = CWBackPixel | CWBorderPixel;

    // Append attribute values after request header
    {
        register CARD32 *valuePtr = (CARD32 *) NEXTPTR(req, xCreateWindowReq);
        *valuePtr++ = background;
        *valuePtr = border;
    }

    UnlockDisplay(dpy);                         // Release lock
    SyncHandle();                               // Check for sync mode
    return (wid);
}
```

### 3.4 Transport Layer (XCB Integration)

libX11 uses XCB for actual socket I/O (`xcb_io.c`):

```c
// Acquire socket from XCB for direct I/O
static void require_socket(Display *dpy)
{
    // Take exclusive control of socket from XCB
    xcb_take_socket(dpy->xcb, return_socket, dpy, flags, sent);
    dpy->fd = xcb_get_file_descriptor(dpy->xcb);
}

// Release socket back to XCB
static void return_socket(void *closure)
{
    Display *dpy = closure;
    // Hand socket back to XCB for event handling
}
```

---

## 4. TinyX Server-Side Analysis

### 4.1 Main Dispatch Loop

```c
// tinyx/dix/dispatch.c
void Dispatch(void)
{
    while (!dispatchException) {
        // Wait for client activity (select on all client fds)
        WaitForSomething(TRUE);
        
        // Process each ready client
        while ((client = SmartScheduleClient()) != NULL) {
            // Read request from this client
            result = ReadRequestFromClient(client);
            
            if (result > 0) {
                // Get opcode from request header
                CARD8 opcode = client->requestBuffer[0];
                
                // Dispatch to appropriate handler via function table
                result = (*client->requestVector[opcode])(client);
            }
            
            // Handle client errors/completion
            if (result != Success) {
                CloseClient(client);
            }
        }
    }
}
```

### 4.2 Request Reading

```c
// tinyx/os/io.c
int ReadRequestFromClient(ClientPtr client)
{
    OsCommPtr oc = (OsCommPtr) client->osPrivate;
    int fd = oc->fd;                    // Client socket fd
    
    // Read from socket into client buffer
    bytes = read(fd, oc->input.buffer + oc->input.bufcnt, 
                 oc->input.bufsize - oc->input.bufcnt);
    
    // Parse request length from header
    xReq *request = (xReq *) oc->input.buffer;
    int reqLen = request->length * 4;   // Length in 4-byte units
    
    // Copy to client->requestBuffer
    client->requestBuffer = oc->input.buffer;
    client->req_len = reqLen;
    
    return reqLen;
}
```

### 4.3 Request Dispatch Table

```c
// tinyx/dix/tables.c (concept)
// Each entry in requestVector maps opcode to handler function
client->requestVector[X_CreateWindow]    = ProcCreateWindow;
client->requestVector[X_MapWindow]       = ProcMapWindow;
client->requestVector[X_UnmapWindow]     = ProcUnmapWindow;
client->requestVector[X_DestroyWindow]   = ProcDestroyWindow;
// ... etc for all 127+ core X11 requests
```

### 4.4 Example: ProcCreateWindow Handler

```c
// tinyx/dix/dispatch.c
int ProcCreateWindow(register ClientPtr client)
{
    WindowPtr pWin, pParent;
    REQUEST(xCreateWindowReq);  // Cast request buffer to typed struct
    int result;
    int len;

    // Validate request size
    REQUEST_AT_LEAST_SIZE(xCreateWindowReq);
    
    // Lookup parent window
    pParent = (WindowPtr) SecurityLookupWindow(stuff->parent, client,
                                                SecurityWriteAccess);
    if (!pParent)
        return (BadWindow);
    
    // Count value list items
    len = client->req_len - (sizeof(xCreateWindowReq) >> 2);
    
    // Create the actual window
    pWin = CreateWindow(stuff->wid, pParent, stuff->x, stuff->y,
                        stuff->width, stuff->height,
                        stuff->borderWidth, stuff->class,
                        stuff->mask, (XID *) &stuff[1],  // Values follow header
                        (int) stuff->depth, client, stuff->visual, &result);
    
    if (pWin) {
        // Register window resource
        if (!AddResource(stuff->wid, RT_WINDOW, (pointer) pWin))
            return BadAlloc;
    }
    
    return (result);
}
```

### 4.5 Connection Establishment

```c
// tinyx/os/connection.c
void WaitForSomething(Bool wait)
{
    fd_set readfds;
    
    // Add all client fds to read set
    for (int i = 0; i < currentMaxClients; i++) {
        if (clients[i])
            FD_SET(clients[i]->fd, &readfds);
    }
    
    // Also watch for new connections
    FD_SET(listenSocket, &readfds);
    
    // Block until activity
    select(maxfd + 1, &readfds, NULL, NULL, timeout);
    
    // Check for new connections
    if (FD_ISSET(listenSocket, &readfds)) {
        AcceptConnection();
    }
}
```

---

## 5. Wire Protocol Details

### 5.1 X Protocol Request Format

All X11 requests follow this structure:

```
┌────────────────────────────────────────────────────────────┐
│                      X11 Request Format                     │
├────────────────────────────────────────────────────────────┤
│ Byte 0     │ Major Opcode (1-127 core, 128-255 extension)  │
│ Byte 1     │ Minor opcode (or first data byte)             │
│ Bytes 2-3  │ Request length in 4-byte units (big-endian)   │
│ Bytes 4+   │ Request-specific data                         │
└────────────────────────────────────────────────────────────┘
```

### 5.2 CreateWindow Request Structure

```c
// X11/Xproto.h
typedef struct {
    CARD8   reqType;         /* X_CreateWindow = 1 */
    CARD8   depth;
    CARD16  length;          /* request length in 4-byte units */
    Window  wid;             /* new window ID */
    Window  parent;          /* parent window ID */
    INT16   x, y;
    CARD16  width, height;
    CARD16  borderWidth;
    CARD16  class;           /* InputOutput, InputOnly, CopyFromParent */
    VisualID visual;
    CARD32  mask;            /* attribute value mask */
    /* CARD32 values[] follow, based on mask bits set */
} xCreateWindowReq;
```

### 5.3 Reply Format

```
┌────────────────────────────────────────────────────────────┐
│                      X11 Reply Format                       │
├────────────────────────────────────────────────────────────┤
│ Byte 0     │ Reply indicator (always 1)                    │
│ Byte 1     │ Reply-specific data                           │
│ Bytes 2-3  │ Sequence number of original request           │
│ Bytes 4-7  │ Length of additional data (in 4-byte units)   │
│ Bytes 8+   │ Reply-specific data                           │
└────────────────────────────────────────────────────────────┘
```

### 5.4 Event Format

```
┌────────────────────────────────────────────────────────────┐
│                      X11 Event Format                       │
├────────────────────────────────────────────────────────────┤
│ Byte 0     │ Event type (2-34 core, 64+ extension)         │
│ Bytes 1-31 │ Event-specific data (32 bytes total)          │
└────────────────────────────────────────────────────────────┘
```

---

## 6. Complete Data Flow Example

### Creating a Window: End-to-End Flow

```
┌────────────────────────────────────────────────────────────────────────┐
│                        CREATE WINDOW DATA FLOW                          │
├────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. JWM calls JXCreateSimpleWindow(display, parent, x, y, w, h, ...)   │
│                              │                                          │
│                              ▼                                          │
│  2. XCreateSimpleWindow() in libX11:                                   │
│     - LockDisplay(dpy)                                                  │
│     - GetReqExtra(CreateWindow, 8, req) allocates 32+8 bytes in buffer │
│     - Fills req->parent, req->x, req->y, req->width, req->height, etc. │
│     - req->wid = XAllocID(dpy) generates unique ID                     │
│     - Appends background/border pixels after request header            │
│     - UnlockDisplay(dpy)                                               │
│                              │                                          │
│                              ▼                                          │
│  3. Buffer in dpy->buffer looks like:                                  │
│     [opcode=1][depth=0][len=10][wid][parent][x][y][w][h][bw][cls][vis] │
│     [mask][bg_pixel][border_pixel]                                     │
│                              │                                          │
│                              ▼                                          │
│  4. On next _XFlush() or SyncHandle():                                 │
│     - _XSend(dpy, buffer, bufptr-buffer) called                        │
│     - XCB sends data over socket (dpy->fd)                             │
│                              │                                          │
│                              ▼                                          │
│  5. TinyX WaitForSomething() returns (socket readable)                 │
│                              │                                          │
│                              ▼                                          │
│  6. ReadRequestFromClient(client):                                     │
│     - read(fd, buffer, ...) gets raw bytes                             │
│     - Parses length, validates request                                 │
│     - Sets client->requestBuffer pointing to data                      │
│                              │                                          │
│                              ▼                                          │
│  7. Dispatch() calls client->requestVector[X_CreateWindow](client)     │
│     which is ProcCreateWindow()                                        │
│                              │                                          │
│                              ▼                                          │
│  8. ProcCreateWindow():                                                │
│     - REQUEST(xCreateWindowReq) casts buffer to struct                 │
│     - Validates parent window exists                                   │
│     - Calls CreateWindow() to create WindowRec                         │
│     - AddResource() registers with resource manager                    │
│     - Returns Success                                                  │
│                              │                                          │
│                              ▼                                          │
│  9. JWM has window ID, can use for subsequent operations               │
│                                                                         │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 7. Bypass Architecture Strategy

### 7.1 Bypass Goals

Per BASE_REQUIREMENTS #1 (Two-Block Architecture):
- **Block A (libX11-wmb)**: Modified libX11 for WMB embedded systems
- **Block B (x-server-wmb)**: TinyX with Tima KDrive
- **Goal**: Eliminate socket/IPC overhead by direct function calls

### 7.2 Current vs Bypass Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                  CURRENT (Socket-Based)                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  JWM → libX11 → [buffer] → [socket write] → [socket read] → TinyX      │
│                                                                          │
│  Overhead: serialization, syscalls, context switches, deserialization   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│           BYPASS - EMBEDDED (JWM via libX11-wmb)                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  JWM → libX11-wmb → ProcXxx() directly                                  │
│                                                                          │
│  Benefits: No serialization, no syscalls, direct function call          │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│           BYPASS - EXTERNAL (Apps via libX11-app)                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  External App → libX11-app → wmb.* (fixed address) → TinyX ProcXxx()   │
│                                                                          │
│  Benefits: Apps compiled separately, call WMB at known address          │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 7.3 Two-Library Architecture

The bypass implementation uses **two separate libraries** for different use cases:

| Library | Used By | Size | How It Calls TinyX |
|---------|---------|------|--------------------|
| **libX11-wmb** | JWM (embedded in WMB) | ~400KB | Calls `Proc*` functions **directly** |
| **libX11-app** | External applications | ~15KB | Calls via `wmb.*` entry table at fixed address |

#### 7.3.1 libX11-wmb: Embedded JWM (Direct Calls)

JWM is compiled and linked into the WMB binary. libX11-wmb calls TinyX `Proc*` functions **directly**:

```
┌─────────────────────────────────────────────────────────────────────────┐
│           libX11-wmb: FOR EMBEDDED JWM (Direct Proc* Calls)             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  JWM (unchanged source code)                                            │
│       │                                                                  │
│       │ calls XCreateSimpleWindow(), XDrawArcs(), XDrawString16()...    │
│       ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  libX11-wmb (linked into WMB binary)                            │    │
│  │                                                                  │    │
│  │  XCreateSimpleWindow() {                                        │    │
│  │      xCreateWindowReq req;                                      │    │
│  │      // ... fill request struct ...                             │    │
│  │      wmbClient.requestBuffer = (char*)&req;                     │    │
│  │      ProcCreateWindow(&wmbClient);  ← DIRECT CALL               │    │
│  │  }                                                               │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│       │                                                                  │
│       │ Direct function call (same binary)                              │
│       ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  TinyX (x-server-wmb)                                           │    │
│  │                                                                  │    │
│  │  ProcCreateWindow(ClientPtr client) {                           │    │
│  │      REQUEST(xCreateWindowReq);  // client->requestBuffer       │    │
│  │      CreateWindow(stuff->wid, ...);                             │    │
│  │  }                                                               │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 7.3.2 libX11-app: External Applications (Entry Table)

External applications are compiled **separately** from WMB. They use the `wmb` entry table at a fixed memory address. The entry table exposes **Xlib API functions** directly - no intermediate wrappers.

```
┌─────────────────────────────────────────────────────────────────────────┐
│           libX11-app: FOR EXTERNAL APPS (Via Entry Table)               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  External Application (separate binary)                                 │
│       │                                                                  │
│       │ calls XCreateSimpleWindow()                                     │
│       ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  libX11-app (pure forwarding, ~15KB)                            │    │
│  │                                                                  │    │
│  │  // wmb is at fixed address: 0x80100000                         │    │
│  │  #define wmb (*(WmbEntryPointTable*)0x80100000)                 │    │
│  │                                                                  │    │
│  │  XCreateSimpleWindow(dpy, parent, x, y, w, h, bw, border, bg) { │    │
│  │      return wmb.XCreateSimpleWindow(dpy, parent, x, y, w, h,    │    │
│  │                                     bw, border, bg);            │    │
│  │  }  ← JUST FORWARDS CALL                                        │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│       │                                                                  │
│       │ Function pointer call to fixed address                          │
│       ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  WMB Binary (at 0x80100000)                                     │    │
│  │                                                                  │    │
│  │  WmbEntryPointTable wmb = {                                     │    │
│  │      .XCreateSimpleWindow = XCreateSimpleWindow,  // libX11-wmb │    │
│  │      .XMapWindow = XMapWindow,                                  │    │
│  │      .XDrawArcs = XDrawArcs,                                    │    │
│  │      ...                                                         │    │
│  │  };                                                              │    │
│  │                                                                  │    │
│  │  // Entry table points directly to libX11-wmb's Xlib functions  │    │
│  │  // which build request structs and call Proc* directly         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

**Key Insight**: 
- **libX11-wmb** contains all the implementation (builds request structs, calls `Proc*`)
- **Entry table** exposes libX11-wmb's Xlib functions directly (no wrapper layer)
- **libX11-app** is pure forwarding - just calls `wmb.XCreateSimpleWindow(...)` with same args
- **No code duplication** - request struct building exists only in libX11-wmb

### 7.4 Entry Point Table Design (For External Apps Only)

The entry point table is used **only by libX11-app** (external applications). It lives in the WMB binary at a fixed address.

**Key Design**: The entry table exposes **standard Xlib API functions** directly - the same functions that libX11-wmb implements. This means:
- No intermediate wrapper functions (`wmb_CreateWindow` etc.)
- libX11-app just forwards calls to libX11-wmb's implementations
- No code duplication - request struct building is only in libX11-wmb

```c
// wmb_entry_points.h - Entry points for EXTERNAL APPS to call into WMB
// These are standard Xlib function signatures - no custom wrappers!

typedef struct {
    // Connection (Display is opaque, but may need minimal stub)
    Display* (*XOpenDisplay)(const char *display_name);
    int (*XCloseDisplay)(Display *dpy);
    
    // Window operations - STANDARD XLIB SIGNATURES
    Window (*XCreateWindow)(Display *dpy, Window parent, int x, int y,
                            unsigned int width, unsigned int height,
                            unsigned int border_width, int depth,
                            unsigned int class, Visual *visual,
                            unsigned long valuemask, XSetWindowAttributes *attr);
    Window (*XCreateSimpleWindow)(Display *dpy, Window parent, int x, int y,
                                   unsigned int width, unsigned int height,
                                   unsigned int border_width,
                                   unsigned long border, unsigned long background);
    int (*XDestroyWindow)(Display *dpy, Window w);
    int (*XMapWindow)(Display *dpy, Window w);
    int (*XUnmapWindow)(Display *dpy, Window w);
    int (*XConfigureWindow)(Display *dpy, Window w, unsigned int mask,
                            XWindowChanges *changes);
    int (*XMoveWindow)(Display *dpy, Window w, int x, int y);
    int (*XResizeWindow)(Display *dpy, Window w, unsigned int width, unsigned int height);
    int (*XRaiseWindow)(Display *dpy, Window w);
    int (*XLowerWindow)(Display *dpy, Window w);
    
    // Drawing operations - STANDARD XLIB SIGNATURES
    int (*XDrawPoint)(Display *dpy, Drawable d, GC gc, int x, int y);
    int (*XDrawPoints)(Display *dpy, Drawable d, GC gc, XPoint *points, int npoints, int mode);
    int (*XDrawLine)(Display *dpy, Drawable d, GC gc, int x1, int y1, int x2, int y2);
    int (*XDrawLines)(Display *dpy, Drawable d, GC gc, XPoint *points, int npoints, int mode);
    int (*XDrawSegments)(Display *dpy, Drawable d, GC gc, XSegment *segments, int nsegments);
    int (*XDrawRectangle)(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h);
    int (*XDrawRectangles)(Display *dpy, Drawable d, GC gc, XRectangle *rects, int nrects);
    int (*XFillRectangle)(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h);
    int (*XFillRectangles)(Display *dpy, Drawable d, GC gc, XRectangle *rects, int nrects);
    int (*XDrawArc)(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h,
                    int angle1, int angle2);
    int (*XDrawArcs)(Display *dpy, Drawable d, GC gc, XArc *arcs, int narcs);
    int (*XFillArc)(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h,
                    int angle1, int angle2);
    int (*XFillArcs)(Display *dpy, Drawable d, GC gc, XArc *arcs, int narcs);
    int (*XFillPolygon)(Display *dpy, Drawable d, GC gc, XPoint *points, int npoints,
                        int shape, int mode);
    
    // Image operations
    int (*XPutImage)(Display *dpy, Drawable d, GC gc, XImage *image,
                     int src_x, int src_y, int dest_x, int dest_y,
                     unsigned int width, unsigned int height);
    XImage* (*XGetImage)(Display *dpy, Drawable d, int x, int y,
                         unsigned int width, unsigned int height,
                         unsigned long plane_mask, int format);
    int (*XCopyArea)(Display *dpy, Drawable src, Drawable dest, GC gc,
                     int src_x, int src_y, unsigned int width, unsigned int height,
                     int dest_x, int dest_y);
    
    // Text operations
    int (*XDrawString)(Display *dpy, Drawable d, GC gc, int x, int y, const char *str, int len);
    int (*XDrawString16)(Display *dpy, Drawable d, GC gc, int x, int y, const XChar2b *str, int len);
    int (*XDrawImageString)(Display *dpy, Drawable d, GC gc, int x, int y, const char *str, int len);
    int (*XDrawImageString16)(Display *dpy, Drawable d, GC gc, int x, int y, const XChar2b *str, int len);
    
    // GC operations
    GC (*XCreateGC)(Display *dpy, Drawable d, unsigned long valuemask, XGCValues *values);
    int (*XChangeGC)(Display *dpy, GC gc, unsigned long valuemask, XGCValues *values);
    int (*XFreeGC)(Display *dpy, GC gc);
    int (*XSetForeground)(Display *dpy, GC gc, unsigned long foreground);
    int (*XSetBackground)(Display *dpy, GC gc, unsigned long background);
    int (*XSetFont)(Display *dpy, GC gc, Font font);
    
    // Pixmap operations
    Pixmap (*XCreatePixmap)(Display *dpy, Drawable d, unsigned int width,
                            unsigned int height, unsigned int depth);
    int (*XFreePixmap)(Display *dpy, Pixmap pixmap);
    
    // Font operations
    Font (*XLoadFont)(Display *dpy, const char *name);
    int (*XUnloadFont)(Display *dpy, Font font);
    XFontStruct* (*XQueryFont)(Display *dpy, XID font_ID);
    
    // Color operations
    Colormap (*XCreateColormap)(Display *dpy, Window w, Visual *visual, int alloc);
    int (*XFreeColormap)(Display *dpy, Colormap colormap);
    Status (*XAllocColor)(Display *dpy, Colormap colormap, XColor *screen_in_out);
    
    // Atom/Property operations
    Atom (*XInternAtom)(Display *dpy, const char *atom_name, Bool only_if_exists);
    char* (*XGetAtomName)(Display *dpy, Atom atom);
    int (*XGetWindowProperty)(Display *dpy, Window w, Atom property, long long_offset,
                              long long_length, Bool delete, Atom req_type,
                              Atom *actual_type_return, int *actual_format_return,
                              unsigned long *nitems_return, unsigned long *bytes_after_return,
                              unsigned char **prop_return);
    int (*XChangeProperty)(Display *dpy, Window w, Atom property, Atom type,
                           int format, int mode, const unsigned char *data, int nelements);
    
    // Event operations
    int (*XNextEvent)(Display *dpy, XEvent *event_return);
    int (*XPending)(Display *dpy);
    int (*XFlush)(Display *dpy);
    int (*XSelectInput)(Display *dpy, Window w, long event_mask);
    int (*XSendEvent)(Display *dpy, Window w, Bool propagate, long event_mask, XEvent *event);
    
    // Grab operations
    int (*XGrabServer)(Display *dpy);
    int (*XUngrabServer)(Display *dpy);
    int (*XGrabPointer)(Display *dpy, Window grab_window, Bool owner_events,
                        unsigned int event_mask, int pointer_mode, int keyboard_mode,
                        Window confine_to, Cursor cursor, Time time);
    int (*XUngrabPointer)(Display *dpy, Time time);
    
    // Query operations
    Status (*XQueryTree)(Display *dpy, Window w, Window *root_return,
                         Window *parent_return, Window **children_return,
                         unsigned int *nchildren_return);
    Status (*XGetGeometry)(Display *dpy, Drawable d, Window *root_return,
                           int *x_return, int *y_return,
                           unsigned int *width_return, unsigned int *height_return,
                           unsigned int *border_width_return, unsigned int *depth_return);
    
} WmbEntryPointTable;

extern WmbEntryPointTable wmb;
```

### 7.5 Xlib Function to TinyX Proc Mapping

This table maps common Xlib functions to their request structs and TinyX handlers:

| Xlib Function | Request Struct | TinyX Proc | X Protocol Opcode |
|---------------|----------------|------------|-------------------|
| **Window Operations** |
| `XCreateWindow` | `xCreateWindowReq` | `ProcCreateWindow` | 1 |
| `XCreateSimpleWindow` | `xCreateWindowReq` | `ProcCreateWindow` | 1 |
| `XDestroyWindow` | `xResourceReq` | `ProcDestroyWindow` | 4 |
| `XMapWindow` | `xResourceReq` | `ProcMapWindow` | 8 |
| `XUnmapWindow` | `xResourceReq` | `ProcUnmapWindow` | 10 |
| `XConfigureWindow` | `xConfigureWindowReq` | `ProcConfigureWindow` | 12 |
| `XMoveWindow` | `xConfigureWindowReq` | `ProcConfigureWindow` | 12 |
| `XResizeWindow` | `xConfigureWindowReq` | `ProcConfigureWindow` | 12 |
| `XRaiseWindow` | `xConfigureWindowReq` | `ProcConfigureWindow` | 12 |
| `XLowerWindow` | `xConfigureWindowReq` | `ProcConfigureWindow` | 12 |
| `XChangeWindowAttributes` | `xChangeWindowAttributesReq` | `ProcChangeWindowAttributes` | 2 |
| `XGetWindowAttributes` | `xResourceReq` | `ProcGetWindowAttributes` | 3 |
| `XReparentWindow` | `xReparentWindowReq` | `ProcReparentWindow` | 7 |
| `XQueryTree` | `xResourceReq` | `ProcQueryTree` | 15 |
| `XGetGeometry` | `xResourceReq` | `ProcGetGeometry` | 14 |
| **Drawing - Lines/Points** |
| `XDrawPoint` | `xPolyPointReq` | `ProcPolyPoint` | 64 |
| `XDrawPoints` | `xPolyPointReq` | `ProcPolyPoint` | 64 |
| `XDrawLine` | `xPolyLineReq` | `ProcPolyLine` | 65 |
| `XDrawLines` | `xPolyLineReq` | `ProcPolyLine` | 65 |
| `XDrawSegments` | `xPolySegmentReq` | `ProcPolySegment` | 66 |
| **Drawing - Rectangles** |
| `XDrawRectangle` | `xPolyRectangleReq` | `ProcPolyRectangle` | 67 |
| `XDrawRectangles` | `xPolyRectangleReq` | `ProcPolyRectangle` | 67 |
| `XFillRectangle` | `xPolyFillRectangleReq` | `ProcPolyFillRectangle` | 70 |
| `XFillRectangles` | `xPolyFillRectangleReq` | `ProcPolyFillRectangle` | 70 |
| **Drawing - Arcs** |
| `XDrawArc` | `xPolyArcReq` | `ProcPolyArc` | 68 |
| `XDrawArcs` | `xPolyArcReq` | `ProcPolyArc` | 68 |
| `XFillArc` | `xPolyFillArcReq` | `ProcPolyFillArc` | 71 |
| `XFillArcs` | `xPolyFillArcReq` | `ProcPolyFillArc` | 71 |
| **Drawing - Polygons** |
| `XFillPolygon` | `xFillPolyReq` | `ProcFillPoly` | 69 |
| **Drawing - Images** |
| `XPutImage` | `xPutImageReq` | `ProcPutImage` | 72 |
| `XGetImage` | `xGetImageReq` | `ProcGetImage` | 73 |
| **Drawing - Copy** |
| `XCopyArea` | `xCopyAreaReq` | `ProcCopyArea` | 62 |
| `XCopyPlane` | `xCopyPlaneReq` | `ProcCopyPlane` | 63 |
| **Text Operations** |
| `XDrawString` | `xPolyTextReq` | `ProcPolyText` | 74 (PolyText8) |
| `XDrawString16` | `xPolyTextReq` | `ProcPolyText` | 75 (PolyText16) |
| `XDrawText` | `xPolyTextReq` | `ProcPolyText` | 74/75 |
| `XDrawText16` | `xPolyTextReq` | `ProcPolyText` | 75 |
| `XDrawImageString` | `xImageTextReq` | `ProcImageText8` | 76 |
| `XDrawImageString16` | `xImageTextReq` | `ProcImageText16` | 77 |
| **GC Operations** |
| `XCreateGC` | `xCreateGCReq` | `ProcCreateGC` | 55 |
| `XChangeGC` | `xChangeGCReq` | `ProcChangeGC` | 56 |
| `XCopyGC` | `xCopyGCReq` | `ProcCopyGC` | 57 |
| `XFreeGC` | `xResourceReq` | `ProcFreeGC` | 60 |
| `XSetForeground` | `xChangeGCReq` | `ProcChangeGC` | 56 |
| `XSetBackground` | `xChangeGCReq` | `ProcChangeGC` | 56 |
| `XSetLineAttributes` | `xChangeGCReq` | `ProcChangeGC` | 56 |
| `XSetClipRectangles` | `xSetClipRectanglesReq` | `ProcSetClipRectangles` | 59 |
| **Pixmap Operations** |
| `XCreatePixmap` | `xCreatePixmapReq` | `ProcCreatePixmap` | 53 |
| `XFreePixmap` | `xResourceReq` | `ProcFreePixmap` | 54 |
| **Font Operations** |
| `XLoadFont` | `xOpenFontReq` | `ProcOpenFont` | 45 |
| `XUnloadFont` | `xResourceReq` | `ProcCloseFont` | 46 |
| `XQueryFont` | `xResourceReq` | `ProcQueryFont` | 47 |
| `XQueryTextExtents` | `xQueryTextExtentsReq` | `ProcQueryTextExtents` | 48 |
| `XListFonts` | `xListFontsReq` | `ProcListFonts` | 49 |
| **Color Operations** |
| `XCreateColormap` | `xCreateColormapReq` | `ProcCreateColormap` | 78 |
| `XFreeColormap` | `xResourceReq` | `ProcFreeColormap` | 79 |
| `XAllocColor` | `xAllocColorReq` | `ProcAllocColor` | 84 |
| `XAllocNamedColor` | `xAllocNamedColorReq` | `ProcAllocNamedColor` | 85 |
| **Atom/Property Operations** |
| `XInternAtom` | `xInternAtomReq` | `ProcInternAtom` | 16 |
| `XGetAtomName` | `xResourceReq` | `ProcGetAtomName` | 17 |
| `XGetWindowProperty` | `xGetPropertyReq` | `ProcGetProperty` | 20 |
| `XChangeProperty` | `xChangePropertyReq` | `ProcChangeProperty` | 18 |
| `XDeleteProperty` | `xDeletePropertyReq` | `ProcDeleteProperty` | 19 |
| **Selection Operations** |
| `XSetSelectionOwner` | `xSetSelectionOwnerReq` | `ProcSetSelectionOwner` | 22 |
| `XGetSelectionOwner` | `xResourceReq` | `ProcGetSelectionOwner` | 23 |
| `XConvertSelection` | `xConvertSelectionReq` | `ProcConvertSelection` | 24 |
| **Grab Operations** |
| `XGrabServer` | `xReq` | `ProcGrabServer` | 36 |
| `XUngrabServer` | `xReq` | `ProcUngrabServer` | 37 |
| `XGrabPointer` | `xGrabPointerReq` | `ProcGrabPointer` | 26 |
| `XUngrabPointer` | `xResourceReq` | `ProcUngrabPointer` | 27 |
| `XGrabKeyboard` | `xGrabKeyboardReq` | `ProcGrabKeyboard` | 31 |
| `XUngrabKeyboard` | `xResourceReq` | `ProcUngrabKeyboard` | 32 |
| **Input Operations** |
| `XWarpPointer` | `xWarpPointerReq` | `ProcWarpPointer` | 41 |
| `XSetInputFocus` | `xSetInputFocusReq` | `ProcSetInputFocus` | 42 |
| `XGetInputFocus` | `xReq` | `ProcGetInputFocus` | 43 |
| **Miscellaneous** |
| `XTranslateCoordinates` | `xTranslateCoordsReq` | `ProcTranslateCoords` | 40 |
| `XClearArea` | `xClearAreaReq` | `ProcClearToBackground` | 61 |
| `XClearWindow` | `xClearAreaReq` | `ProcClearToBackground` | 61 |
| `XSync` | N/A (local) | N/A | N/A |
| `XFlush` | N/A (local) | N/A | N/A |

### 7.6 libX11-wmb Implementation Example (Direct Proc* Calls)

libX11-wmb is for **embedded JWM** - it calls TinyX `Proc*` functions **directly**:

```c
// libX11-wmb/src/CrWindow.c - Embedded JWM implementation

#include "tinyx/dix/dispatch.h"      // ProcCreateWindow, etc.
#include "tinyx/include/dixstruct.h" // ClientPtr, ClientRec
#include "wmb_client.h"              // wmbClient, wmb_AllocID

Window XCreateSimpleWindow(
    Display *dpy,
    Window parent,
    int x, int y,
    unsigned int width, unsigned int height,
    unsigned int borderWidth,
    unsigned long border,
    unsigned long background)
{
    // 1. Allocate request struct (same layout as X11 wire protocol)
    xCreateWindowReq req;
    CARD32 values[2];
    
    // 2. Fill in the request (mimics what GetReqExtra does)
    req.reqType = X_CreateWindow;
    req.depth = 0;
    req.length = (sizeof(xCreateWindowReq) + 8) >> 2;  // +8 for two CARD32 values
    req.wid = wmb_AllocID();     // Generate resource ID
    req.parent = parent;
    req.x = x;
    req.y = y;
    req.width = width;
    req.height = height;
    req.borderWidth = borderWidth;
    req.class = CopyFromParent;
    req.visual = CopyFromParent;
    req.mask = CWBackPixel | CWBorderPixel;
    
    // 3. Append values (background, border)
    values[0] = background;
    values[1] = border;
    
    // 4. Set up client request buffer and call Proc* DIRECTLY
    wmbClient.requestBuffer = (char *)&req;
    wmbClient.req_len = req.length;
    int result = ProcCreateWindow(&wmbClient);  // <-- DIRECT CALL
    
    return (result == Success) ? req.wid : None;
}

// Another example: XDrawArcs
int XDrawArcs(
    Display *dpy,
    Drawable d,
    GC gc,
    XArc *arcs,
    int narcs)
{
    // 1. Allocate request struct
    size_t req_size = sizeof(xPolyArcReq) + narcs * sizeof(xArc);
    xPolyArcReq *req = alloca(req_size);
    
    // 2. Fill in header
    req->reqType = X_PolyArc;
    req->drawable = d;
    req->gc = gc;
    req->length = req_size >> 2;
    
    // 3. Copy arcs data after header
    memcpy((char*)req + sizeof(xPolyArcReq), arcs, narcs * sizeof(xArc));
    
    // 4. Call TinyX Proc* DIRECTLY
    wmbClient.requestBuffer = (char *)req;
    wmbClient.req_len = req->length;
    return ProcPolyArc(&wmbClient);  // <-- DIRECT CALL
}

// Text example: XDrawString16
int XDrawString16(
    Display *dpy,
    Drawable d,
    GC gc,
    int x, int y,
    const XChar2b *string,
    int length)
{
    // Build PolyText16 request (opcode 75)
    xPolyTextReq *req = /* build request */;
    req->reqType = X_PolyText16;
    req->drawable = d;
    req->gc = gc;
    req->x = x;
    req->y = y;
    // ... encode text items after header ...
    
    wmbClient.requestBuffer = (char *)req;
    wmbClient.req_len = req->length;
    return ProcPolyText(&wmbClient);  // <-- DIRECT CALL
}
```

### 7.7 TinyX Entry Point Table (For External Apps)

The entry point table lives in WMB and is used **only by external applications** via libX11-app.

**Key Design**: The entry table points **directly to libX11-wmb's Xlib functions** - no intermediate wrapper layer.

```c
// tinyx/dix/wmb_entries.c - Entry points for EXTERNAL APPS

#include "wmb_entry_points.h"

// Import libX11-wmb's Xlib functions (these are the actual implementations)
// These are declared in libX11-wmb headers and linked into WMB binary
extern Window XCreateSimpleWindow(Display *dpy, Window parent, int x, int y,
                                   unsigned int width, unsigned int height,
                                   unsigned int border_width,
                                   unsigned long border, unsigned long background);
extern int XMapWindow(Display *dpy, Window w);
extern int XUnmapWindow(Display *dpy, Window w);
extern int XDestroyWindow(Display *dpy, Window w);
extern int XDrawArcs(Display *dpy, Drawable d, GC gc, XArc *arcs, int narcs);
extern int XDrawString16(Display *dpy, Drawable d, GC gc, int x, int y, 
                          const XChar2b *string, int length);
extern int XNextEvent(Display *dpy, XEvent *event_return);
extern int XPending(Display *dpy);
// ... etc

// Entry point table - placed at fixed address via linker script
// Points DIRECTLY to libX11-wmb's Xlib function implementations
__attribute__((section(".wmb_entry_table")))
WmbEntryPointTable wmb = {
    // These point directly to libX11-wmb's implementations
    // NO wrapper functions needed!
    .XCreateSimpleWindow = XCreateSimpleWindow,
    .XMapWindow = XMapWindow,
    .XUnmapWindow = XUnmapWindow,
    .XDestroyWindow = XDestroyWindow,
    .XDrawArcs = XDrawArcs,
    .XDrawString16 = XDrawString16,
    .XNextEvent = XNextEvent,
    .XPending = XPending,
    // ... all other Xlib functions from libX11-wmb
};
```

**No wrapper functions needed!** The entry table simply holds pointers to the same `XCreateSimpleWindow`, `XMapWindow`, etc. functions that JWM calls directly.

### 7.7.1 libX11-app Implementation Example (External Apps)

libX11-app is a **pure forwarding library** (~15KB) for external applications. It contains **no request struct building** - just one-liner forwarding calls:

```c
// libX11-app/src/CrWindow.c - External app implementation
// PURE FORWARDING - no request struct building!

#include <X11/Xlib.h>
#include "wmb_entry_points.h"

// wmb entry table is at fixed address in WMB binary
#define wmb (*(WmbEntryPointTable*)0x80100000)

// Pure forwarding - just call libX11-wmb's implementation via entry table
Window XCreateSimpleWindow(
    Display *dpy,
    Window parent,
    int x, int y,
    unsigned int width, unsigned int height,
    unsigned int borderWidth,
    unsigned long border,
    unsigned long background)
{
    return wmb.XCreateSimpleWindow(dpy, parent, x, y, width, height,
                                    borderWidth, border, background);
}

Window XCreateWindow(
    Display *dpy,
    Window parent,
    int x, int y,
    unsigned int width, unsigned int height,
    unsigned int border_width,
    int depth,
    unsigned int class,
    Visual *visual,
    unsigned long valuemask,
    XSetWindowAttributes *attributes)
{
    return wmb.XCreateWindow(dpy, parent, x, y, width, height,
                              border_width, depth, class, visual,
                              valuemask, attributes);
}

int XMapWindow(Display *dpy, Window w)
{
    return wmb.XMapWindow(dpy, w);
}

int XUnmapWindow(Display *dpy, Window w)
{
    return wmb.XUnmapWindow(dpy, w);
}

int XDestroyWindow(Display *dpy, Window w)
{
    return wmb.XDestroyWindow(dpy, w);
}
```

```c
// libX11-app/src/Drawing.c - Drawing functions (pure forwarding)

int XDrawArcs(Display *dpy, Drawable d, GC gc, XArc *arcs, int narcs)
{
    return wmb.XDrawArcs(dpy, d, gc, arcs, narcs);
}

int XDrawLine(Display *dpy, Drawable d, GC gc, int x1, int y1, int x2, int y2)
{
    return wmb.XDrawLine(dpy, d, gc, x1, y1, x2, y2);
}

int XFillRectangle(Display *dpy, Drawable d, GC gc, int x, int y, 
                   unsigned int width, unsigned int height)
{
    return wmb.XFillRectangle(dpy, d, gc, x, y, width, height);
}
```

```c
// libX11-app/src/Text.c - Text functions (pure forwarding)

int XDrawString(Display *dpy, Drawable d, GC gc, int x, int y, 
                const char *string, int length)
{
    return wmb.XDrawString(dpy, d, gc, x, y, string, length);
}

int XDrawString16(Display *dpy, Drawable d, GC gc, int x, int y,
                  const XChar2b *string, int length)
{
    return wmb.XDrawString16(dpy, d, gc, x, y, string, length);
}
```

```c
// libX11-app/src/Event.c - Event functions (pure forwarding)

int XNextEvent(Display *dpy, XEvent *event_return)
{
    return wmb.XNextEvent(dpy, event_return);
}

int XPending(Display *dpy)
{
    return wmb.XPending(dpy);
}

int XSelectInput(Display *dpy, Window w, long event_mask)
{
    return wmb.XSelectInput(dpy, w, event_mask);
}
```

**Benefits of pure forwarding:**
- **~15KB total size** - just function pointer calls
- **No code duplication** - request struct building only in libX11-wmb
- **Easy to maintain** - each function is a one-liner
- **ABI stable** - external apps don't depend on internal request formats

### 7.8 Event Handling

Events don't use the request/response model - they're generated by the server and queued.

#### 7.8.1 Event Flow Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         WMB EVENT FLOW                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ HAL Input Sources                                                 │   │
│  │   • Keyboard: tima_KeyboardRead() → key press/release            │   │
│  │   • Mouse: tima_MouseRead() → motion, button press/release       │   │
│  │   • Timer: tima_TimerTick() → for animations, cursor blink       │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│         │                                                                │
│         │ ProcessInputEvents() called periodically                      │
│         ▼                                                                │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ TinyX Input Processing (mi/mipointer.c, dix/events.c)            │   │
│  │   • Converts HAL input to X11 events                             │   │
│  │   • Determines target window (focus, pointer location)           │   │
│  │   • Generates xEvent structures                                  │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│         │                                                                │
│         │ Events queued per-client                                      │
│         ▼                                                                │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ Event Queue (wmbClient.events)                                    │   │
│  │   • FIFO queue of xEvent structures                              │   │
│  │   • One queue for embedded JWM (wmbClient)                       │   │
│  │   • External apps share same queue (single-client model)         │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│         │                                                                │
│         │ XNextEvent() dequeues and converts                            │
│         ▼                                                                │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ Application Event Loop                                            │   │
│  │   • JWM: ProcessEvent() dispatches to handlers                   │   │
│  │   • External App: Application-specific handling                  │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 7.8.2 The Waiting Mechanism - Where Does Blocking Happen?

**Key Question**: How does `XNextEvent()` wait efficiently without busy-polling?

**Traditional X11 (Two Processes)**:
```
┌─────────────────────────────────────────────────────────────────────────┐
│  CLIENT PROCESS                    │  SERVER PROCESS                    │
├────────────────────────────────────┼────────────────────────────────────┤
│                                    │                                    │
│  XNextEvent()                      │  Dispatch() main loop:             │
│      │                             │      │                             │
│      ▼                             │      ▼                             │
│  _XReadEvents()                    │  WaitForSomething()                │
│      │                             │      │                             │
│      ▼                             │      ▼                             │
│  read(socket_fd) ◄─── BLOCKS ───► │  select(client_fds, input_fds)    │
│                                    │      │                             │
│                                    │      ▼                             │
│                                    │  ProcessInputEvents()              │
│                                    │                                    │
└─────────────────────────────────────────────────────────────────────────┘
```

In standard X11:
- **Server** runs `Dispatch()` loop with `WaitForSomething()` using `select()`
- **Client** blocks on socket read in `_XReadEvents()`
- `XNextEvent()` is implemented in `libX11/src/NextEvent.c`

**WMB Bypass (Single Process)**:

In WMB, client and server are the same process. We create our own `XNextEvent()` in libX11-wmb that bypasses sockets entirely and calls directly into KDrive tima functions.

**Key Decision**: We do NOT include `libX11/src/NextEvent.c` in libX11-wmb. Instead, we implement our own `XNextEvent()` that:
1. Polls for input via `tima_OsPollEvents()` (existing KDrive function)
2. Blocks efficiently via `tima_WaitForEvents()` (new KDrive function)
3. Dequeues events from wmbClient's event queue

```
┌─────────────────────────────────────────────────────────────────────────┐
│  WMB SINGLE PROCESS (libX11-wmb + TinyX)                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  JWM calls XNextEvent(dpy, &event)                                      │
│      │                                                                   │
│      ▼                                                                   │
│  libX11-wmb: XNextEvent()    ◄─── OUR IMPLEMENTATION (not libX11's)    │
│      │                                                                   │
│      │  File: libX11-wmb/src/NextEvent.c (our own, not libX11's)        │
│      │                                                                   │
│      ├──► tima_OsPollEvents()  ──► Check keyboard/mouse via KDrive     │
│      │        │                                                          │
│      │        └──► tima_KeyboardPoll() + tima_MousePoll()               │
│      │                                                                   │
│      ├──► If no events: tima_WaitForEvents()  ◄─── BLOCKS HERE         │
│      │        │                                                          │
│      │        │ (SDL: SDL_WaitEvent, RISC-V: WFI instruction)           │
│      │        │                                                          │
│      │◄───────┘                                                          │
│      │                                                                   │
│      ▼                                                                   │
│  Dequeue event from wmbClient, convert xEvent → XEvent                  │
│      │                                                                   │
│      ▼                                                                   │
│  Return to JWM                                                          │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

**Why This Approach is Clean**:
- libX11-wmb owns its `XNextEvent()` completely - no modifications to TinyX core
- Blocking wait is a KDrive tima function, keeping all tima code together
- Follows existing KDrive pattern (`tima_OsFuncs.pollEvents`, etc.)
- Clear separation: KDrive handles hardware, libX11-wmb handles Xlib API

#### 7.8.2.1 New KDrive Tima API for Event Waiting

We add a new function to the tima KDrive driver. **Important**: This must NOT block the entire system in single-threaded mode. See [MULTI_CORE_GUI_ANALYSIS.md](MULTI_CORE_GUI_ANALYSIS.md) Section 5 for the cooperative execution model.

```c
// tinyx/kdrive/tima/tima_kdrive_system.c

// Existing: Non-blocking poll (called via KdOsFuncs.pollEvents)
static void tima_OsPollEvents(void)
{
    if (!os_enabled) return;
    
    /* Poll all input devices */
    tima_KeyboardPoll();
    tima_MousePoll();
}

// NEW: Wait for events - DOES NOT BLOCK THE SYSTEM
// Instead, it yields to OS so other services can run
void tima_WaitForEvents(void)
{
    if (!os_enabled) return;
    
    // If events already available, return immediately
    if (keypad_driver_has_data() || mouse_driver_has_data(0)) {
        return;
    }
    
    // No events - yield to OS with timeout
    // OS continues running (timers, network, serial, etc.)
    // Returns when: input available OR timeout OR OS signals us
    hal_yield_timeout(10);  // Max 10ms wait
    
    // Poll again after yield (OS may have filled buffers)
    tima_KeyboardPoll();
    tima_MousePoll();
}

// Wait with specified timeout (for animations, timers)
int tima_WaitForEventsTimeout(uint32_t timeout_ms)
{
    if (!os_enabled) return 0;
    
    if (keypad_driver_has_data() || mouse_driver_has_data(0)) {
        return 1;  // Events already available
    }
    
    // Yield to OS with caller-specified timeout
    return hal_yield_timeout(timeout_ms);
}
```

**Key Design**: `tima_WaitForEvents()` calls `hal_yield_timeout()` which:
1. Lets Tima OS run its services (timers, network, serial, etc.)
2. Polls hardware for new input
3. Returns early if input becomes available
4. Returns after timeout if no input (prevents starvation)

This cooperative model ensures the system never freezes waiting for GUI input.

#### 7.8.2.2 libX11-wmb XNextEvent() Implementation

libX11-wmb provides its own `XNextEvent()` - we do NOT use `libX11/src/NextEvent.c`:

```c
// libX11-wmb/src/NextEvent.c - OUR IMPLEMENTATION (replaces libX11's)

#include "wmb_client.h"
#include "tima_kdrive.h"  // For tima_OsPollEvents(), tima_WaitForEvents()

// Reference to WMB's client structure
extern ClientPtr wmbClient;

int XNextEvent(Display *dpy, XEvent *event_return)
{
    xEvent *serverEvent;
    
    // Wait for events - all via KDrive tima functions
    while (GetEventsQueued(wmbClient) == 0) {
        // 1. Poll for any pending input (non-blocking)
        tima_OsPollEvents();
        
        // 2. If still no events, block efficiently
        if (GetEventsQueued(wmbClient) == 0) {
            tima_WaitForEvents();  // CPU sleeps until input arrives
        }
    }
    
    // Dequeue oldest event from TinyX's client event queue
    serverEvent = DequeueEvent(wmbClient);
    
    // Convert from wire format (xEvent) to client format (XEvent)
    WireToEvent(serverEvent, event_return);
    
    return 0;  // Standard Xlib XNextEvent returns 0
}
```

**Comparison: Standard libX11 vs libX11-wmb**:

| Aspect | Standard libX11 (`NextEvent.c`) | libX11-wmb (`NextEvent.c`) |
|--------|----------------------------------|---------------------------|
| File included | `libX11/src/NextEvent.c` | `libX11-wmb/src/NextEvent.c` (our own) |
| Blocking mechanism | `_XReadEvents()` → `read(socket)` | `tima_WaitForEvents()` |
| Polling mechanism | N/A (server polls) | `tima_OsPollEvents()` |
| Event queue | `dpy->head` (socket-based) | `wmbClient` (TinyX internal) |
| Dependencies | Socket I/O functions | KDrive tima functions |

#### 7.8.3 Event Types in WMB

| Event Type | Source | Description |
|------------|--------|-------------|
| `KeyPress`, `KeyRelease` | Keyboard HAL | Key pressed/released |
| `ButtonPress`, `ButtonRelease` | Mouse HAL | Mouse button pressed/released |
| `MotionNotify` | Mouse HAL | Mouse pointer moved |
| `EnterNotify`, `LeaveNotify` | TinyX | Pointer entered/left window |
| `FocusIn`, `FocusOut` | TinyX | Window gained/lost input focus |
| `Expose` | TinyX | Window needs redrawing |
| `ConfigureNotify` | TinyX | Window moved/resized |
| `MapNotify`, `UnmapNotify` | TinyX | Window mapped/unmapped |
| `PropertyNotify` | TinyX | Window property changed |
| `ClientMessage` | Application | Inter-client communication |

#### 7.8.4 libX11-wmb Event Implementation (Full)

**Architecture Note**: libX11-wmb implements its own event functions. We do NOT include `libX11/src/NextEvent.c` or related files. Instead, we implement our own versions that call KDrive tima functions directly for polling and blocking.

**Files NOT included from libX11** (we provide our own):
- `libX11/src/NextEvent.c` → `libX11-wmb/src/NextEvent.c`
- `libX11/src/Pending.c` → `libX11-wmb/src/Pending.c`
- `libX11/src/PeekEvent.c` → `libX11-wmb/src/PeekEvent.c`
- `libX11/src/EvToWire.c` → `libX11-wmb/src/EvToWire.c`

```c
// libX11-wmb/src/Event.c - Embedded JWM (full implementation)

#include "tinyx/dix/dispatch.h"
#include "tinyx/dix/events.h"
#include "tinyx/kdrive/tima/tima_kdrive.h"  // KDrive tima functions
#include "wmb_client.h"

// Reference to WMB's client structure (managed by TinyX)
extern ClientPtr wmbClient;

// Convert server xEvent to client XEvent format
static void WireToEvent(xEvent *wire, XEvent *event)
{
    // Most fields map directly
    event->type = wire->u.u.type;
    event->xany.serial = wire->u.u.sequenceNumber;
    event->xany.send_event = (wire->u.u.type & 0x80) != 0;
    event->xany.display = wmbDisplay;
    
    switch (wire->u.u.type) {
    case KeyPress:
    case KeyRelease:
        event->xkey.window = wire->u.keyButtonPointer.event;
        event->xkey.root = wire->u.keyButtonPointer.root;
        event->xkey.subwindow = wire->u.keyButtonPointer.child;
        event->xkey.time = wire->u.keyButtonPointer.time;
        event->xkey.x = wire->u.keyButtonPointer.eventX;
        event->xkey.y = wire->u.keyButtonPointer.eventY;
        event->xkey.x_root = wire->u.keyButtonPointer.rootX;
        event->xkey.y_root = wire->u.keyButtonPointer.rootY;
        event->xkey.state = wire->u.keyButtonPointer.state;
        event->xkey.keycode = wire->u.u.detail;
        event->xkey.same_screen = wire->u.keyButtonPointer.sameScreen;
        break;
        
    case ButtonPress:
    case ButtonRelease:
        event->xbutton.window = wire->u.keyButtonPointer.event;
        event->xbutton.root = wire->u.keyButtonPointer.root;
        event->xbutton.time = wire->u.keyButtonPointer.time;
        event->xbutton.x = wire->u.keyButtonPointer.eventX;
        event->xbutton.y = wire->u.keyButtonPointer.eventY;
        event->xbutton.x_root = wire->u.keyButtonPointer.rootX;
        event->xbutton.y_root = wire->u.keyButtonPointer.rootY;
        event->xbutton.state = wire->u.keyButtonPointer.state;
        event->xbutton.button = wire->u.u.detail;
        break;
        
    case MotionNotify:
        event->xmotion.window = wire->u.keyButtonPointer.event;
        event->xmotion.root = wire->u.keyButtonPointer.root;
        event->xmotion.time = wire->u.keyButtonPointer.time;
        event->xmotion.x = wire->u.keyButtonPointer.eventX;
        event->xmotion.y = wire->u.keyButtonPointer.eventY;
        event->xmotion.x_root = wire->u.keyButtonPointer.rootX;
        event->xmotion.y_root = wire->u.keyButtonPointer.rootY;
        event->xmotion.state = wire->u.keyButtonPointer.state;
        event->xmotion.is_hint = wire->u.u.detail;
        break;
        
    case Expose:
        event->xexpose.window = wire->u.expose.window;
        event->xexpose.x = wire->u.expose.x;
        event->xexpose.y = wire->u.expose.y;
        event->xexpose.width = wire->u.expose.width;
        event->xexpose.height = wire->u.expose.height;
        event->xexpose.count = wire->u.expose.count;
        break;
        
    case ConfigureNotify:
        event->xconfigure.window = wire->u.configureNotify.window;
        event->xconfigure.x = wire->u.configureNotify.x;
        event->xconfigure.y = wire->u.configureNotify.y;
        event->xconfigure.width = wire->u.configureNotify.width;
        event->xconfigure.height = wire->u.configureNotify.height;
        event->xconfigure.border_width = wire->u.configureNotify.borderWidth;
        event->xconfigure.above = wire->u.configureNotify.aboveSibling;
        event->xconfigure.override_redirect = wire->u.configureNotify.
override;
        break;
        
    // ... handle other event types ...
    }
}

// Main event retrieval - BLOCKS until event available
// THIS IS OUR IMPLEMENTATION - NOT libX11's NextEvent.c
int XNextEvent(Display *dpy, XEvent *event_return)
{
    xEvent *serverEvent;
    
    // Wait for events - using KDrive tima functions directly
    while (GetEventsQueued(wmbClient) == 0) {
        // 1. Poll for pending input (non-blocking) via KDrive
        tima_OsPollEvents();
        
        // 2. If still no events, block efficiently via KDrive
        if (GetEventsQueued(wmbClient) == 0) {
            tima_WaitForEvents();  // Blocks until input arrives
        }
    }
    
    // Dequeue oldest event from TinyX's client event queue
    serverEvent = DequeueEvent(wmbClient);
    
    // Convert from wire format (xEvent) to client format (XEvent)
    WireToEvent(serverEvent, event_return);
    
    return 0;  // Standard Xlib XNextEvent returns 0
}

// Check event count WITHOUT blocking
// THIS IS OUR IMPLEMENTATION - NOT libX11's Pending.c
int XPending(Display *dpy)
{
    // Process any pending input first (non-blocking) via KDrive
    tima_OsPollEvents();
    return GetEventsQueued(wmbClient);
}

// Check events with optional flush
int XEventsQueued(Display *dpy, int mode)
{
    if (mode == QueuedAfterFlush || mode == QueuedAfterReading) {
        tima_OsPollEvents();  // Non-blocking poll via KDrive
    }
    return GetEventsQueued(wmbClient);
}

// Peek at next event without removing it
// THIS IS OUR IMPLEMENTATION - NOT libX11's PeekEvent.c
int XPeekEvent(Display *dpy, XEvent *event_return)
{
    // Wait for at least one event - using KDrive tima functions
    while (GetEventsQueued(wmbClient) == 0) {
        tima_OsPollEvents();
        if (GetEventsQueued(wmbClient) == 0) {
            tima_WaitForEvents();
        }
    }
    
    // Peek without dequeuing
    xEvent *serverEvent = PeekEvent(wmbClient);
    WireToEvent(serverEvent, event_return);
    return 0;
}

// Register for events on a window
int XSelectInput(Display *dpy, Window w, long event_mask)
{
    xChangeWindowAttributesReq req;
    CARD32 values[1];
    
    req.reqType = X_ChangeWindowAttributes;
    req.window = w;
    req.valueMask = CWEventMask;
    req.length = (sizeof(xChangeWindowAttributesReq) + 4) >> 2;
    values[0] = event_mask;
    
    wmbClient->requestBuffer = (char *)&req;
    wmbClient->req_len = req.length;
    return ProcChangeWindowAttributes(wmbClient);
}

// Flush output buffer (in WMB bypass, this is mostly a no-op)
int XFlush(Display *dpy)
{
    // In bypass mode, operations are synchronous
    // But we might need to process pending redraws
    WaitForSomething(FALSE);  // Non-blocking check for pending work
    return 1;
}

// Synchronize with server
int XSync(Display *dpy, Bool discard)
{
    if (discard) {
        // Clear event queue
        DiscardEvents(wmbClient);
    }
    WaitForSomething(FALSE);  // Process pending work
    return 1;
}
```

#### 7.8.5 libX11-app Event Implementation (Pure Forwarding)

```c
// libX11-app/src/Event.c - External apps (pure forwarding)

#include <X11/Xlib.h>
#include "wmb_entry_points.h"

#define wmb (*(WmbEntryPointTable*)0x80100000)

int XNextEvent(Display *dpy, XEvent *event_return)
{
    return wmb.XNextEvent(dpy, event_return);
}

int XPending(Display *dpy)
{
    return wmb.XPending(dpy);
}

int XEventsQueued(Display *dpy, int mode)
{
    return wmb.XEventsQueued(dpy, mode);
}

int XPeekEvent(Display *dpy, XEvent *event_return)
{
    return wmb.XPeekEvent(dpy, event_return);
}

int XSelectInput(Display *dpy, Window w, long event_mask)
{
    return wmb.XSelectInput(dpy, w, event_mask);
}

int XFlush(Display *dpy)
{
    return wmb.XFlush(dpy);
}

int XSync(Display *dpy, Bool discard)
{
    return wmb.XSync(dpy, discard);
}
```

#### 7.8.6 JWM Event Loop Example

JWM's event loop works **unchanged** with libX11-wmb:

```c
// jwm/src/event.c - JWM's event loop (UNCHANGED from original)

void EventLoop(void)
{
    XEvent event;
    
    for(;;) {
        // Wait for next event - blocks efficiently
        WaitForEvent(&event);
        
        // Dispatch to appropriate handler
        ProcessEvent(&event);
    }
}

char WaitForEvent(XEvent *event)
{
    // TRADITIONAL JWM: Uses select() on display fd
    // WMB JWM: XNextEvent() calls tima_WaitForEvents() for blocking
    //          (implemented in KDrive tima driver)
    
    // JWM doesn't know or care about the difference!
    // It just calls standard Xlib functions.
    
    // Check if events pending
    while (JXPending(display) == 0) {
        // In traditional X11: select() on connection fd
        // In WMB: XPending() calls tima_OsPollEvents() - non-blocking
        //         XNextEvent() calls tima_WaitForEvents() - blocking
        
        // JWM's timeout handling for animations
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  // 100ms
        
        // Note: In WMB, this fd_set/select approach is replaced by
        // tima_WaitForEvents() in KDrive - JWM code doesn't change
    }
    
    JXNextEvent(display, event);
    return 1;
}

void ProcessEvent(XEvent *event)
{
    switch (event->type) {
    case KeyPress:
        HandleKeyPress(&event->xkey);
        break;
        
    case KeyRelease:
        HandleKeyRelease(&event->xkey);
        break;
        
    case ButtonPress:
        HandleButtonPress(&event->xbutton);
        break;
        
    case ButtonRelease:
        HandleButtonRelease(&event->xbutton);
        break;
        
    case MotionNotify:
        HandleMotionNotify(&event->xmotion);
        break;
        
    case Expose:
        HandleExpose(&event->xexpose);
        break;
        
    case ConfigureNotify:
        HandleConfigureNotify(&event->xconfigure);
        break;
        
    case EnterNotify:
        HandleEnterNotify(&event->xcrossing);
        break;
        
    case LeaveNotify:
        HandleLeaveNotify(&event->xcrossing);
        break;
        
    case PropertyNotify:
        HandlePropertyNotify(&event->xproperty);
        break;
        
    case ClientMessage:
        HandleClientMessage(&event->xclient);
        break;
        
    case MapRequest:
        HandleMapRequest(&event->xmaprequest);
        break;
        
    case ConfigureRequest:
        HandleConfigureRequest(&event->xconfigurerequest);
        break;
        
    // ... more event types ...
    }
}
```

#### 7.8.7 External Application Event Loop Example

```c
// Example: Simple external X11 application using libX11-app

#include <X11/Xlib.h>
#include <stdio.h>

int main(void)
{
    Display *dpy;
    Window win;
    XEvent event;
    int running = 1;
    
    // Open display (via libX11-app → wmb.XOpenDisplay)
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Cannot open display\n");
        return 1;
    }
    
    // Create window
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                               100, 100, 400, 300, 1,
                               BlackPixel(dpy, 0), WhitePixel(dpy, 0));
    
    // Select events we care about
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask);
    
    // Map (show) window
    XMapWindow(dpy, win);
    
    // Event loop
    while (running) {
        // XNextEvent blocks efficiently until event available
        // libX11-app forwards to wmb.XNextEvent()
        // which calls TinyX's WaitForSomething() for blocking
        XNextEvent(dpy, &event);
        
        switch (event.type) {
        case Expose:
            // Window needs redrawing
            printf("Expose event: redraw window\n");
            // ... drawing code ...
            break;
            
        case KeyPress:
            printf("Key pressed: keycode=%d\n", event.xkey.keycode);
            if (event.xkey.keycode == 9) {  // Escape key
                running = 0;
            }
            break;
            
        case ButtonPress:
            printf("Button %d pressed at (%d, %d)\n",
                   event.xbutton.button,
                   event.xbutton.x, event.xbutton.y);
            break;
        }
    }
    
    // Cleanup
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    
    return 0;
}
```

#### 7.8.8 KDrive Input Integration (Existing TinyX Architecture)

TinyX already has a complete input system. The Tima KDrive drivers integrate via `KdInitInput()`:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                  TINYX INPUT ARCHITECTURE (EXISTING)                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │ TinyX Event Loop (dispatch.c)                                  │     │
│  │                                                                 │     │
│  │   WaitForSomething()                                           │     │
│  │       │                                                         │     │
│  │       ├──► ProcessInputEvents() ──► KdOsFuncs.pollEvents()    │     │
│  │       │                                    │                    │     │
│  │       │                                    ▼                    │     │
│  │       │                            tima_OsPollEvents()         │     │
│  │       │                                    │                    │     │
│  │       │                    ┌───────────────┴───────────────┐   │     │
│  │       │                    ▼                               ▼   │     │
│  │       │           tima_KeyboardPoll()             tima_MousePoll()   │
│  │       │                    │                               │   │     │
│  │       │                    ▼                               ▼   │     │
│  │       │           KdEnqueueKeyboardEvent()   KdEnqueuePointerEvent() │
│  │       │                    │                               │   │     │
│  │       │                    └───────────────┬───────────────┘   │     │
│  │       │                                    ▼                    │     │
│  │       │                            Event Queue                 │     │
│  │       │                                    │                    │     │
│  │       ◄────────────────────────────────────┘                   │     │
│  │                                                                 │     │
│  │   Dispatch() processes events, delivers to clients             │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

**Key Point**: `ProcessInputEvents()` already exists in TinyX (kinput.c). We don't reimplement it - we just provide the KDrive drivers it calls.

#### Registration via KdInitInput (tima_kdrive_system.c)

```c
// tinyx/kdrive/tima/tima_kdrive_system.c

void InitInput(int argc, char **argv)
{
    // Register Tima keyboard and mouse drivers with TinyX
    // TinyX will call these drivers' Poll functions
    KdInitInput(&tima_MouseFuncs, &tima_KeyboardFuncs);
}

void OsVendorInit(void)
{
    // Register OS functions including pollEvents
    KdOsInit(&tima_OsFuncs);
}

// OS poll function - called by TinyX's ProcessInputEvents()
static void tima_OsPollEvents(void)
{
    if (!os_enabled) return;
    
    // Poll all input devices via their registered drivers
    tima_KeyboardPoll();
    tima_MousePoll();
}

const KdOsFuncs tima_OsFuncs = {
    .Init       = tima_OsInit,
    .Enable     = tima_OsEnable,
    .SpecialKey = tima_OsSpecialKey,
    .Disable    = tima_OsDisable,
    .Fini       = tima_OsFini,
    .pollEvents = tima_OsPollEvents,   // ← TinyX calls this
};
```

#### Tima Keyboard Driver (tima_keyboard.c)

```c
// tinyx/kdrive/tima/tima_keyboard.c

#include "hal_drivers.h"

// Called by tima_OsPollEvents()
void tima_KeyboardPoll(void)
{
    uint8_t keycode;
    uint8_t pressed;
    
    // Read all pending keys from HAL
    while (hal_keyboard_read(&keycode, &pressed)) {
        // Enqueue into TinyX's event system
        // TinyX handles conversion to X11 KeyPress/KeyRelease events
        KdEnqueueKeyboardEvent(keycode, pressed);
    }
}

// KDrive keyboard driver structure
const KdKeyboardFuncs tima_KeyboardFuncs = {
    .Init    = tima_KeyboardInit,
    .Fini    = tima_KeyboardFini,
    .Enable  = tima_KeyboardEnable,
    .Disable = tima_KeyboardDisable,
    .Leds    = tima_KeyboardLeds,
    .Bell    = tima_KeyboardBell,
};
```

#### Tima Mouse Driver (tima_mouse.c)

```c
// tinyx/kdrive/tima/tima_mouse.c

#include "hal_drivers.h"

// Called by tima_OsPollEvents()
void tima_MousePoll(void)
{
    int16_t dx, dy;
    uint8_t buttons;
    
    // Read mouse state from HAL
    if (hal_mouse_read(&dx, &dy, &buttons)) {
        // Enqueue into TinyX's event system
        // TinyX handles conversion to X11 MotionNotify/ButtonPress events
        KdEnqueuePointerEvent(dx, dy, buttons);
    }
}

// KDrive mouse driver structure
const KdMouseFuncs tima_MouseFuncs = {
    .Init    = tima_MouseInit,
    .Fini    = tima_MouseFini,
    .Enable  = tima_MouseEnable,
    .Disable = tima_MouseDisable,
};
```

#### HAL Input Functions (hal_drivers.h)

The HAL provides the actual hardware interface:

```c
// hal_drivers.h - Low-level input HAL (called by KDrive tima)

// Keyboard HAL
int hal_keyboard_available(void);     // Check if key event pending
int hal_keyboard_read(uint8_t *keycode, uint8_t *pressed);  // Read key (returns 0 if none)

// Mouse HAL  
int hal_mouse_available(void);        // Check if mouse event pending
int hal_mouse_read(int16_t *dx, int16_t *dy, uint8_t *buttons);  // Read mouse

// Cooperative yield - allows OS to run while WMB waits for input
// Returns: 1 if input available, 0 if timeout
// This is the KEY function for single-threaded cooperative execution
int hal_yield_timeout(uint32_t timeout_ms);
```

**`hal_yield_timeout()` Implementation** (provided by Tima OS):

```c
// Tima OS provides this function

int hal_yield_timeout(uint32_t max_ms)
{
    uint32_t start = os_get_tick_ms();
    
    while ((os_get_tick_ms() - start) < max_ms) {
        // 1. Let OS do its work (THIS IS THE KEY!)
        os_process_timers();
        os_process_network();
        os_process_serial();
        os_run_background_tasks();
        
        // 2. Poll hardware input (fills driver buffers)
        os_poll_keyboard();
        os_poll_mouse();
        
        // 3. Check if input is now available
        if (keypad_driver_has_data() || mouse_driver_has_data(0)) {
            return 1;  // Events available - return immediately
        }
        
        // 4. Brief sleep if truly idle (optional power saving)
        if (os_all_idle()) {
            os_sleep_ms(1);  // Short sleep, keeps system responsive
        }
    }
    
    return 0;  // Timeout - no events
}
```

See [MULTI_CORE_GUI_ANALYSIS.md](MULTI_CORE_GUI_ANALYSIS.md) Section 5 for detailed discussion of cooperative execution.

#### libX11-wmb Integration with TinyX Events

The key design: libX11-wmb implements its own `XNextEvent()` that calls KDrive tima functions directly. We do NOT include `libX11/src/NextEvent.c` in the build.

```c
// libX11-wmb/src/NextEvent.c - OUR IMPLEMENTATION (replaces libX11's)

#include "tinyx/kdrive/tima/tima_kdrive.h"  // KDrive tima functions
#include "wmb_client.h"

extern ClientPtr wmbClient;

int XNextEvent(Display *dpy, XEvent *event_return)
{
    // Our implementation calls KDrive tima functions directly:
    // - tima_OsPollEvents() for non-blocking poll
    // - tima_WaitForEvents() for efficient blocking
    
    while (GetEventsQueued(wmbClient) == 0) {
        tima_OsPollEvents();   // Poll via KDrive (non-blocking)
        
        if (GetEventsQueued(wmbClient) == 0) {
            tima_WaitForEvents();  // Block via KDrive until input
        }
    }
    
    // Dequeue from wmbClient's event queue
    xEvent *serverEvent = DequeueEvent(wmbClient);
    
    // Convert wire format (xEvent) to client format (XEvent)
    WireToEvent(serverEvent, event_return);
    
    return 0;
}
```

**Important**: This is a clean integration with KDrive:

1. `XNextEvent()` is our own implementation in libX11-wmb (NOT libX11's)
2. `tima_OsPollEvents()` polls keyboard/mouse via KDrive (non-blocking)
3. `tima_WaitForEvents()` blocks efficiently until input arrives (new KDrive function)
4. `tima_KeyboardPoll()` and `tima_MousePoll()` read from HAL
5. `KdEnqueueKeyboardEvent()` / `KdEnqueuePointerEvent()` queue events
6. Events delivered to wmbClient via TinyX's existing event system
7. `XNextEvent()` dequeues, converts format, and returns to the application

#### 7.8.9 Summary: Event Handling Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    COMPLETE EVENT FLOW                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Hardware Interrupt (keyboard/mouse)                                    │
│         │                                                                │
│         ▼                                                                │
│  HAL buffers raw input (hal_keyboard_read, hal_mouse_read)             │
│         │                                                                │
│         │  ← JWM/App calls XNextEvent() or XPending()                   │
│         │                                                                │
│         ▼                                                                │
│  libX11-wmb XNextEvent()   ◄─── OUR IMPLEMENTATION (not libX11's)      │
│         │                                                                │
│         ├──► tima_OsPollEvents()  (non-blocking poll via KDrive)       │
│         │          │                                                     │
│         │          ├──► tima_KeyboardPoll() → hal_keyboard_read()      │
│         │          │          │                                          │
│         │          │          └──► KdEnqueueKeyboardEvent()             │
│         │          │                                                     │
│         │          └──► tima_MousePoll() → hal_mouse_read()            │
│         │                    │                                           │
│         │                    └──► KdEnqueuePointerEvent()               │
│         │                                                                │
│         └──► If no events: tima_WaitForEvents()  (blocking via KDrive) │
│                    │                                                     │
│                    │ (SDL: SDL_WaitEvent, RISC-V: WFI instruction)      │
│                    │                                                     │
│         ◄──────────┘                                                     │
│         │                                                                │
│         ▼                                                                │
│  Dequeue from TinyX event queue (xEvent structures)                     │
│         │                                                                │
│         ▼                                                                │
│  WireToEvent() - Convert to XEvent format                               │
│         │                                                                │
│         ▼                                                                │
│  Application receives XEvent                                            │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

| Step | Component | Function | Description |
|------|-----------|----------|-------------|
| 1 | Hardware | Interrupt | Keyboard/mouse generates interrupt |
| 2 | HAL | `hal_keyboard_read()` | Buffers raw input data |
| 3 | App | `XNextEvent()` | Calls libX11-wmb's implementation |
| 4 | libX11-wmb | `XNextEvent()` | Our implementation (not libX11's) |
| 5 | KDrive Tima | `tima_OsPollEvents()` | Non-blocking poll of input |
| 6 | KDrive Tima | `tima_KeyboardPoll()` | Reads HAL, enqueues events |
| 7 | KDrive Tima | `tima_WaitForEvents()` | Efficient blocking if no events |
| 8 | TinyX | Event queue | `KdEnqueueKeyboardEvent()` creates xEvent |
| 9 | libX11-wmb | `WireToEvent()` | Converts xEvent to XEvent |
| 10 | Application | Event loop | Processes the event |

**Key Points**:
- **Our own XNextEvent** - libX11-wmb has its own implementation (NOT `libX11/src/NextEvent.c`)
- **KDrive integration** - Blocking and polling via tima KDrive functions
- **HAL abstraction** - Only HAL read functions differ between SDL and RISC-V backends
- **Efficient waiting** - `tima_WaitForEvents()` sleeps CPU until hardware interrupt

### 7.9 Benefits of Two-Library Approach

| Benefit | Description |
|---------|-------------|
| **Zero JWM changes** | JWM links against libX11-wmb instead of standard libX11 |
| **Same API** | Both libraries provide standard Xlib functions |
| **No socket overhead** | Direct function calls, no serialization |
| **Optimal for embedded** | libX11-wmb makes direct `Proc*` calls - fastest possible path |
| **Supports external apps** | libX11-app allows separately-compiled applications |
| **No code duplication** | Request struct building exists only in libX11-wmb |
| **Tiny external lib** | libX11-app is ~15KB of pure forwarding one-liners |
| **ABI stable** | External apps don't depend on internal wire protocol formats |
| **Testable** | Can test functions individually |
| **Incremental** | Can implement functions one at a time as needed |

### 7.10 Build Architecture: Two Separate Libraries

**Important**: There are **two separate libX11 implementations** for different use cases:

| Library | Used By | Linked Into | Calls TinyX Via |
|---------|---------|-------------|------------------|
| **libX11-wmb** | JWM (embedded) | WMB binary | Direct `Proc*` calls |
| **libX11-app** | External apps | App binary | `wmb.*` entry table |

- **libX11-wmb**: Compiled and linked into the WMB binary along with JWM and TinyX
- **libX11-app**: Linked into separately-compiled applications

The JWM source code remains **100% unchanged**. Only the build instructions differ (which library to link).

### 7.11 Required Header: Proc Function Prototypes

libX11-wmb needs to call TinyX `Proc*` functions directly. **This header already exists**:

```
tinyx/dix/dispatch.h
```

The header uses the `DISPATCH_PROC` macro (defined in `tinyx/include/dixstruct.h`):

```c
// tinyx/include/dixstruct.h
#define DISPATCH_PROC(func) int func(ClientPtr /* client */)
```

And declares all Proc* functions:

```c
// tinyx/dix/dispatch.h (excerpt)
DISPATCH_PROC(ProcCreateWindow);
DISPATCH_PROC(ProcMapWindow);
DISPATCH_PROC(ProcUnmapWindow);
DISPATCH_PROC(ProcDestroyWindow);
DISPATCH_PROC(ProcPolyArc);
DISPATCH_PROC(ProcPolyText);
DISPATCH_PROC(ProcImageText8);
DISPATCH_PROC(ProcImageText16);
// ... 100+ more Proc functions
```

**libX11-wmb simply includes this existing header**:

```c
// libX11-wmb/src/wmb_client.c
#include "tinyx/dix/dispatch.h"      // Proc* function declarations
#include "tinyx/include/dixstruct.h" // ClientPtr, DISPATCH_PROC macro
```

No new header file needs to be created for Proc* prototypes.

### 7.12 Build Configuration

#### libX11-wmb (Embedded - No Special Configuration)

libX11-wmb is compiled directly into the WMB binary. It simply includes TinyX headers and calls `Proc*` functions directly:

```c
// libX11-wmb/src/wmb_client.c
#include "tinyx/dix/dispatch.h"      // Proc* function declarations
#include "tinyx/include/dixstruct.h" // ClientPtr, DISPATCH_PROC macro

// Shared client for all libX11-wmb functions
extern ClientRec wmbClient;
extern void InitWmbClient(void);
```

No special build flags needed - it's all in the same binary.

#### libX11-app (External Apps - Fixed Address Access)

libX11-app needs to know where the `wmb` entry table is located in the WMB binary:

```c
// libX11-app/include/wmb_link.h

#ifndef _WMB_LINK_H_
#define _WMB_LINK_H_

#include "wmb_entry_points.h"

// wmb entry table is at a fixed address in the WMB binary
// This address is defined in the WMB linker script
#ifndef WMB_ENTRY_TABLE_ADDR
    #error "WMB_ENTRY_TABLE_ADDR must be defined"
#endif

#define wmb  (*(WmbEntryPointTable *)WMB_ENTRY_TABLE_ADDR)

#endif /* _WMB_LINK_H_ */
```

Build external apps with: `-DWMB_ENTRY_TABLE_ADDR=0x80100000`

### 7.13 WMB Linker Script (For External App Access)

The entry table is only needed for **external applications**. The WMB linker script places it at a known address:

```ld
/* wmb.ld - WMB Linker Script (excerpt) */

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 64M
}

SECTIONS
{
    /* ... other sections ... */
    
    /* Entry point table at fixed address for EXTERNAL APPS */
    .wmb_entries 0x80100000 : {
        KEEP(*(.wmb_entry_table))
    } > RAM
    
    /* ... other sections ... */
}
```

**Note**: libX11-wmb (embedded JWM) does NOT use this entry table - it calls `Proc*` directly. The entry table exists only for libX11-app (external applications).

And in WMB binary:

```c
// wmb_entries.c (in WMB binary)

// Place wmb table in special section for linker script
// Points DIRECTLY to libX11-wmb's Xlib functions
__attribute__((section(".wmb_entry_table")))
WmbEntryPointTable wmb = {
    .XCreateSimpleWindow = XCreateSimpleWindow,  // libX11-wmb function
    .XMapWindow = XMapWindow,
    .XDrawArcs = XDrawArcs,
    // ... all Xlib functions from libX11-wmb
};
```

### 7.14 Implementation Plan

1. **Phase 1: Core Infrastructure**
   - Create `wmb_entry_points.h` with table definition
   - Create `wmb_entries.c` with embedded client setup
   - Implement `wmb_AllocID()` for resource ID generation

2. **Phase 2: Window Operations** (JWM startup)
   - `XOpenDisplay` / `XCloseDisplay`
   - `XCreateWindow` / `XCreateSimpleWindow`
   - `XMapWindow` / `XUnmapWindow`
   - `XDestroyWindow`

3. **Phase 3: Event Handling** (JWM event loop)
   - `XNextEvent` / `XPending`
   - `XSelectInput`
   - Event queue integration

4. **Phase 4: Drawing Operations** (JWM rendering)
   - `XDrawRectangle` / `XFillRectangle`
   - `XDrawLine` / `XDrawLines`
   - `XDrawString` / `XDrawString16`
   - `XDrawArc` / `XFillArc`

5. **Phase 5: Remaining Operations**
   - GC operations
   - Pixmap operations
   - Color operations
   - Property/Atom operations

---

## 8. Files Requiring Modification

### 8.1 libX11-wmb (For Embedded JWM - Direct Proc* Calls)

| File | Purpose |
|------|---------|
| `libX11-wmb/src/CrWindow.c` | `XCreateWindow`, `XCreateSimpleWindow` |
| `libX11-wmb/src/MapWindow.c` | `XMapWindow`, `XUnmapWindow`, etc. |
| `libX11-wmb/src/Drawing.c` | Drawing primitives (`XDrawLine`, etc.) |
| `libX11-wmb/src/Text.c` | Text operations (`XDrawString`, etc.) |
| `libX11-wmb/src/GC.c` | GC operations |
| `libX11-wmb/src/Pixmap.c` | Pixmap operations |
| `libX11-wmb/src/Color.c` | Color operations |
| `libX11-wmb/src/Atom.c` | Atom/Property operations |
| `libX11-wmb/src/Event.c` | `XNextEvent`, `XPending`, etc. |
| `libX11-wmb/src/Display.c` | `XOpenDisplay`, `XCloseDisplay` |
| `libX11-wmb/src/wmb_client.c` | Shared client setup and ID allocation |

### 8.1.1 libX11-app (For External Apps - Pure Forwarding)

| File | Purpose |
|------|---------|
| `libX11-app/include/wmb_link.h` | Entry table access at fixed address |
| `libX11-app/include/wmb_entry_points.h` | Entry table structure definition (copy from WMB) |
| `libX11-app/src/Window.c` | Window functions (pure forwarding) |
| `libX11-app/src/Drawing.c` | Drawing functions (pure forwarding) |
| `libX11-app/src/Text.c` | Text functions (pure forwarding) |
| `libX11-app/src/GC.c` | GC functions (pure forwarding) |
| `libX11-app/src/Event.c` | Event functions (pure forwarding) |
| `libX11-app/src/Display.c` | Display functions (pure forwarding) |

**Note**: libX11-app is ~15KB of **pure forwarding one-liners**. Each function just calls `wmb.XFunctionName(...)` - no request struct building, no logic duplication.

### 8.2 WMB/TinyX Additions

| File | Purpose |
|------|---------|
| `tinyx/dix/dispatch.h` | **Already exists** - Proc* function declarations |
| `tinyx/include/dixstruct.h` | **Already exists** - ClientPtr, DISPATCH_PROC macro |
| `wmb_entries.c` | **New** - Entry point table (points to libX11-wmb functions) |
| `libX11-wmb/src/wmb_client.c` | **New** - Shared client setup and ID allocation |
| `tinyx/kdrive/tima/tima.c` | Already completed (Tima HAL integration) |

### 8.3 Build System

| File | Purpose |
|------|---------|
| `libX11-wmb/CMakeLists.txt` | Build libX11-wmb (linked into WMB) |
| `libX11-app/CMakeLists.txt` | Build libX11-app.a for external apps |
| `tinyx/CMakeLists.txt` | TinyX with entry table |
| `wmb.ld` | Linker script placing wmb at fixed address |

---

## 9. Next Steps

1. **Create Shared Infrastructure** - `wmb_client.c` with client setup and ID allocation
2. **Implement libX11-wmb Functions** - Start with window operations (direct Proc* calls)
3. **Create Entry Point Table** - `wmb_entries.c` pointing to libX11-wmb's Xlib functions
4. **Create libX11-app** - Pure forwarding wrappers (one-liners calling `wmb.XFunctionName(...)`)
5. **Test Hosted Build** - Verify with SDL backend
6. **Test Target Build** - Verify fixed address linkage for external apps

---

## 10. References

- `docs/BASE_REQUIREMENTS.md` - Two-Block Architecture specification
- `docs/HAL_SPECIFICATION.md` - Tima HAL API
- `docs/WMB_API_DESIGN.md` - WMB API design
- `tinyx/dix/dispatch.c` - TinyX dispatch implementation
- `tinyx/include/dixstruct.h` - ClientPtr and ClientRec definitions

---

## Document History

| Date | Change |
|------|--------|
| 2026-01-XX | Initial creation - basic client-server analysis |
| 2026-01-25 | Added CRITICAL CORRECTION section about XCB centralization |
| 2026-01-25 | **MAJOR UPDATE**: Discovered SyncHandle() pattern |
|            | - Updated header with final WMB strategy |
|            | - Linked to XCB_STUB_ARCHITECTURE.md as recommended implementation |
|            | - Added summary statistics showing 0 API files need changes |

---

## ⚠️ IMPLEMENTATION NOTE

**This document contains historical analysis that is now superseded.**

For the **actual implementation plan**, refer to:
- [XCB_STUB_ARCHITECTURE.md](XCB_STUB_ARCHITECTURE.md) - The recommended approach using SyncHandle hooks

The sections below (libX11-wmb, libX11-app, Entry Point Table) represent earlier thinking before we discovered the SyncHandle pattern. The new approach is simpler and requires no libX11 API file modifications.
