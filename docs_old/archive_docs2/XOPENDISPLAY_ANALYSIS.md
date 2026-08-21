# XOpenDisplay Analysis for WMB

**Date**: January 25, 2026
**Purpose**: Detailed analysis of `XOpenDisplay()` modifications required for WMB internal client model

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Implementation Analysis](#2-current-implementation-analysis)
3. [XCB Dependencies Map](#3-xcb-dependencies-map)
4. [Line-by-Line Code Review](#4-line-by-line-code-review)
5. [Required Modifications for WMB](#5-required-modifications-for-wmb)
6. [Related Files and Dependencies](#6-related-files-and-dependencies)
7. [Proposed WMB Implementation](#7-proposed-wmb-implementation)
8. [Risk Assessment](#8-risk-assessment)

---

## 1. Executive Summary

### The Problem

The current `XOpenDisplay()` in `libX11/src/OpenDis.c` is **deeply integrated with XCB** for socket-based client-server communication. For WMB's internal client model (no sockets, direct function calls), we cannot simply "remove the `_XConnectXCB` block" - there are **multiple XCB dependencies** throughout the function.

### Key Finding

**It's NOT just removing `_XConnectXCB()`**. The following also depend on XCB:

| Line | Code | Issue |
|------|------|-------|
| 129 | `_XConnectXCB()` | Creates XCB socket connection |
| 242 | `dpy->xcb->real_bufmax` | Accesses XCB-allocated structure |
| 259-265 | `xcb_get_setup()` | Gets server setup data FROM XCB |
| 497 | `xcb_get_maximum_request_length()` | Queries XCB for big requests |
| 522-545 | `GetReq`, `_XReply` | Macros that use XCB transport |

### Recommendation

Create a **separate WMB version** of `XOpenDisplay()` that:
1. Gets setup data directly from TinyX server state
2. Uses memory queues instead of sockets
3. Links to OS context for scheduling

---

## 2. Current Implementation Analysis

### 2.1 File Location

```
libX11/src/OpenDis.c
```

### 2.2 Function Signature

```c
Display* XOpenDisplay(register _Xconst char *display)
```

### 2.3 High-Level Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    XOpenDisplay() CURRENT FLOW                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. Parse display name (or use DISPLAY env var)                         │
│     └── Lines 89-102                                                    │
│                                                                         │
│  2. Set default error handlers                                          │
│     └── Lines 107-108                                                   │
│                                                                         │
│  3. Allocate Display structure                                          │
│     └── Lines 113-120                                                   │
│                                                                         │
│  4. ══════ XCB CONNECTION ══════                                        │
│     │   _XConnectXCB() - Create socket to X server                      │
│     └── Lines 127-131  ◄─────────────── MUST REPLACE                    │
│                                                                         │
│  5. Initialize Display pointers to NULL                                 │
│     └── Lines 134-190 (UNCHANGED for WMB)                               │
│                                                                         │
│  6. Setup display info (protocol version, etc.)                         │
│     └── Lines 195-211                                                   │
│                                                                         │
│  7. ══════ XCB BUFFER SETUP ══════                                      │
│     │   dpy->xcb->real_bufmax = ...                                     │
│     └── Lines 240-243  ◄─────────────── MUST MODIFY                     │
│                                                                         │
│  8. ══════ XCB SETUP DATA ══════                                        │
│     │   xcb_get_setup() - Get server configuration                      │
│     └── Lines 259-265  ◄─────────────── MUST REPLACE                    │
│                                                                         │
│  9. Parse setup data (screens, formats, visuals)                        │
│     └── Lines 270-493 (UNCHANGED - just parses data)                    │
│                                                                         │
│  10. ══════ XCB BIG REQUESTS ══════                                     │
│      │   xcb_get_maximum_request_length()                               │
│      └── Lines 497-498  ◄─────────────── MUST REPLACE                   │
│                                                                         │
│  11. Create default GCs for each screen                                 │
│      └── Lines 503-515 (Uses XCreateGC - needs review)                  │
│                                                                         │
│  12. ══════ XCB REQUEST/REPLY ══════                                    │
│      │   GetReq(), _XReply() - Resource manager query                   │
│      └── Lines 522-545  ◄─────────────── NEEDS REVIEW                   │
│                                                                         │
│  13. Return Display pointer                                             │
│      └── Line 558                                                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. XCB Dependencies Map

### 3.1 Direct XCB Function Calls

| Line | Function | Purpose | WMB Replacement |
|------|----------|---------|-----------------|
| 129 | `_XConnectXCB()` | Create socket connection | `wmb_connect_internal()` |
| 259 | `xcb_get_setup()` | Get server setup data | `wmb_get_setup()` |
| 497 | `xcb_get_maximum_request_length()` | Query max request size | `wmb_get_max_request_size()` |

### 3.2 XCB Structure Access

| Line | Code | Purpose | WMB Replacement |
|------|------|---------|-----------------|
| 242 | `dpy->xcb->real_bufmax` | Set buffer limit | `dpy->wmb->bufmax` or remove |
| 243 | `dpy->bufmax = dpy->buffer` | XCB buffer management | Review if needed |

### 3.3 XCB-Aware Macros (in Xlibint.h)

| Line | Macro | Purpose | WMB Replacement |
|------|-------|---------|-----------------|
| 524 | `GetReq()` | Allocate request buffer | Direct TinyX call |
| 534 | `_XReply()` | Wait for server reply | Direct TinyX call |
| 537 | `_XReadPad()` | Read padded data | Direct memory copy |

### 3.4 Dependency Graph

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    XOpenDisplay XCB DEPENDENCY GRAPH                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  XOpenDisplay()                                                         │
│       │                                                                 │
│       ├──► _XConnectXCB() ──────► xcb_connect()                         │
│       │         │                      │                                │
│       │         └──► Allocates dpy->xcb structure                       │
│       │                    │                                            │
│       ├──► dpy->xcb->real_bufmax ◄─────┘                                │
│       │                                                                 │
│       ├──► xcb_get_setup() ──────► Returns server config                │
│       │         │                                                       │
│       │         └──► prefix, u.setup, setuplength                       │
│       │                    │                                            │
│       │                    ▼                                            │
│       │         ┌──────────────────────────────────────┐                │
│       │         │  Setup Parsing (lines 270-493)       │                │
│       │         │  - Parses screens, depths, visuals   │                │
│       │         │  - INDEPENDENT of XCB source         │                │
│       │         │  - Just needs the DATA               │                │
│       │         └──────────────────────────────────────┘                │
│       │                                                                 │
│       ├──► xcb_get_maximum_request_length()                             │
│       │                                                                 │
│       └──► GetReq() / _XReply() ──────► XCB transport layer             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Line-by-Line Code Review

### 4.1 Lines 89-120: Display Name and Allocation (NO CHANGE)

```c
// Lines 89-102: Parse display name
if (display == NULL || *display == '\0') {
    if ((display_name = getenv("DISPLAY")) == NULL) {
        return(NULL);  // WMB: Could return default ":0" instead
    }
}
else {
    display_name = (char *)display;
}

// Lines 107-108: Error handlers (NO CHANGE)
if (_XErrorFunction == NULL) (void) XSetErrorHandler (NULL);
if (_XIOErrorFunction == NULL) (void) XSetIOErrorHandler (NULL);

// Lines 113-120: Allocate Display (NO CHANGE)
if ((dpy = Xcalloc(1, sizeof(Display))) == NULL) {
    return(NULL);
}
if ((dpy->display_name = strdup(display_name)) == NULL) {
    OutOfMemory(dpy);
    return(NULL);
}
```

**WMB Status**: ✅ No changes needed

### 4.2 Lines 127-131: XCB Connection (MUST REPLACE)

```c
// CURRENT CODE - Socket connection via XCB
if(!_XConnectXCB(dpy, display, &iscreen)) {
    OutOfMemory(dpy);
    return NULL;
}
```

**WMB Replacement**:
```c
// WMB CODE - Internal client connection
WmbClientPtr wmb_client = wmb_connect_internal();
if (!wmb_client) {
    OutOfMemory(dpy);
    return NULL;
}
dpy->wmb = wmb_client;  // Store WMB client pointer
iscreen = 0;            // TinyX default screen

// Link to OS context for scheduling
AppContext *ctx = os_get_current_context();
if (ctx) {
    wmb_client->os_context = ctx;
}
```

### 4.3 Lines 134-190: Pointer Initialization (NO CHANGE)

```c
dpy->keysyms        = (KeySym *) NULL;
dpy->modifiermap    = NULL;
dpy->lock_meaning   = NoSymbol;
// ... (all NULL initializations)
dpy->xkb_info       = NULL;
dpy->exit_handler_data = NULL;
dpy->in_ifevent     = 0;
```

**WMB Status**: ✅ No changes needed - these are Display structure fields

### 4.4 Lines 195-211: Display Setup (MINOR CHANGE)

```c
dpy->vnumber = X_PROTOCOL;
dpy->resource_alloc = _XAllocID;
dpy->idlist_alloc = _XAllocIDs;
dpy->synchandler = NULL;
dpy->savedsynchandler = NULL;
X_DPY_SET_REQUEST(dpy, 0);
X_DPY_SET_LAST_REQUEST_READ(dpy, 0);
dpy->default_screen = iscreen;  // ◄── Uses iscreen from connection
dpy->last_req = (char *)&_dummy_request;
dpy->error_threads = NULL;
dpy->exit_handler = _XDefaultIOErrorExit;

// Display lock - may need review for single-threaded WMB
if (InitDisplayLock(dpy) != 0) {
    OutOfMemory (dpy);
    return(NULL);
}

// Pollfd cache - WMB doesn't need this (no file descriptors)
if (!_XPollfdCacheInit(dpy)) {
    OutOfMemory (dpy);
    return(NULL);
}
```

**WMB Status**: ⚠️ Review `InitDisplayLock` and `_XPollfdCacheInit` - may not be needed

### 4.5 Lines 228-243: Buffer Setup (MUST MODIFY)

```c
// Buffer allocation (can keep)
if ((dpy->bufptr = dpy->buffer = Xcalloc(1, conn_buf_size)) == NULL) {
    OutOfMemory (dpy);
    return(NULL);
}

// XCB-specific (MUST CHANGE)
dpy->xcb->real_bufmax = dpy->buffer + conn_buf_size;  // ◄── Uses dpy->xcb
dpy->bufmax = dpy->buffer;
```

**WMB Replacement**:
```c
// Buffer allocation (keep as-is)
if ((dpy->bufptr = dpy->buffer = Xcalloc(1, conn_buf_size)) == NULL) {
    OutOfMemory (dpy);
    return(NULL);
}

// WMB version - no dpy->xcb
dpy->bufmax = dpy->buffer + conn_buf_size;  // Direct assignment
```

### 4.6 Lines 248-265: Setup Data Retrieval (MUST REPLACE)

```c
// Event queue setup (NO CHANGE)
dpy->head = dpy->tail = NULL;
dpy->qlen = 0;

// Free-function record (NO CHANGE)
if ((dpy->free_funcs = Xcalloc(1, sizeof(_XFreeFuncRec))) == NULL) {
    OutOfMemory (dpy);
    return(NULL);
}

// XCB SETUP RETRIEVAL (MUST REPLACE)
{
    const struct xcb_setup_t *xcbsetup = xcb_get_setup(dpy->xcb->connection);
    memcpy(&prefix, xcbsetup, sizeof(prefix));
    setuplength = prefix.length << 2;
    setup = (char *) xcbsetup;
    setup += SIZEOF(xConnSetupPrefix);
    u.setup = (xConnSetup *) setup;
}
```

**WMB Replacement**:
```c
// Event queue setup (NO CHANGE)
dpy->head = dpy->tail = NULL;
dpy->qlen = 0;

// Free-function record (NO CHANGE)
if ((dpy->free_funcs = Xcalloc(1, sizeof(_XFreeFuncRec))) == NULL) {
    OutOfMemory (dpy);
    return(NULL);
}

// WMB SETUP RETRIEVAL - Get data directly from TinyX
{
    // TinyX exposes server setup via wmb_get_setup()
    const void *tinyx_setup = wmb_get_setup_data(&setuplength);
    memcpy(&prefix, tinyx_setup, sizeof(prefix));
    setup = (char *)tinyx_setup;
    setup += SIZEOF(xConnSetupPrefix);
    u.setup = (xConnSetup *) setup;
}
```

### 4.7 Lines 270-493: Setup Parsing (NO CHANGE)

This large section parses the setup data into Display structure fields:
- Protocol version
- Resource IDs
- Vendor string
- Pixmap formats
- Screens, depths, visuals

**WMB Status**: ✅ No changes needed - it just parses data, doesn't care about source

### 4.8 Lines 497-498: Big Requests Query (MUST REPLACE)

```c
// XCB query (MUST REPLACE)
dpy->bigreq_size = xcb_get_maximum_request_length(dpy->xcb->connection);
if(dpy->bigreq_size <= dpy->max_request_size)
    dpy->bigreq_size = 0;
```

**WMB Replacement**:
```c
// WMB - Get from TinyX server configuration
dpy->bigreq_size = wmb_get_max_request_size();
if(dpy->bigreq_size <= dpy->max_request_size)
    dpy->bigreq_size = 0;
```

### 4.9 Lines 503-515: Default GC Creation (NEEDS REVIEW)

```c
for (i = 0; i < dpy->nscreens; i++) {
    register Screen *sp = &dpy->screens[i];
    XGCValues values;
    values.foreground = sp->black_pixel;
    values.background = sp->white_pixel;
    if ((sp->default_gc = XCreateGC (dpy, sp->root,
                                     GCForeground|GCBackground,
                                     &values)) == NULL) {
        OutOfMemory(dpy);
        return (NULL);
    }
}
```

**WMB Status**: ⚠️ `XCreateGC()` will use WMB transport - needs to work with internal client

### 4.10 Lines 522-545: Resource Manager Query (NEEDS REVIEW)

```c
LockDisplay(dpy);
{
    xGetPropertyReply reply;
    xGetPropertyReq *req;

    GetReq (GetProperty, req);  // ◄── XCB-aware macro
    req->window = RootWindow(dpy, 0);
    req->property = XA_RESOURCE_MANAGER;
    req->type = XA_STRING;
    req->delete = False;
    req->longOffset = 0;
    req->longLength = 100000000L;

    if (_XReply (dpy, (xReply *) &reply, 0, xFalse)) {  // ◄── XCB transport
        if (reply.format == 8 && reply.propertyType == XA_STRING &&
            (reply.nItems + 1 > 0) &&
            (reply.nItems <= req->longLength * 4) &&
            (dpy->xdefaults = Xmalloc (reply.nItems + 1))) {
            _XReadPad (dpy, dpy->xdefaults, reply.nItems);  // ◄── XCB read
            dpy->xdefaults[reply.nItems] = '\0';
        }
        else if (reply.propertyType != None)
            _XEatDataWords(dpy, reply.length);
    }
}
UnlockDisplay(dpy);
```

**WMB Status**: ⚠️ This uses `GetReq`, `_XReply`, `_XReadPad` - all XCB-transport macros.

**Options**:
1. Replace with direct TinyX call to get root window property
2. Skip entirely (WMB may not need RESOURCE_MANAGER)
3. Implement WMB versions of these macros

---

## 5. Required Modifications for WMB

### 5.1 Summary Table

| Section | Lines | Change Type | Complexity |
|---------|-------|-------------|------------|
| Display name parsing | 89-102 | None | - |
| Error handlers | 107-108 | None | - |
| Display allocation | 113-120 | None | - |
| **XCB connection** | **127-131** | **Replace** | **Medium** |
| Pointer init | 134-190 | None | - |
| Display setup | 195-211 | Minor | Low |
| **Buffer setup** | **240-243** | **Modify** | **Low** |
| Event queue | 248-250 | None | - |
| **Setup retrieval** | **259-265** | **Replace** | **Medium** |
| Setup parsing | 270-493 | None | - |
| **Big requests** | **497-498** | **Replace** | **Low** |
| Default GC | 503-515 | Review | Medium |
| **Resource manager** | **522-545** | **Replace/Skip** | **High** |

### 5.2 New Functions Required

```c
// wmb/wmb_client.h - New WMB functions needed

// Connection management
WmbClientPtr wmb_connect_internal(void);
void wmb_disconnect_internal(WmbClientPtr client);

// Setup data access
const void* wmb_get_setup_data(long *length_return);
unsigned long wmb_get_max_request_size(void);

// Client-to-context linkage
void wmb_set_os_context(WmbClientPtr client, void *os_context);
```

### 5.3 Display Structure Considerations

The `Display` structure (in `Xlibint.h`) contains:

```c
struct _XDisplay {
    // ... many fields ...
    struct _X11XCBPrivate *xcb;  // ◄── XCB-specific
    // ... more fields ...
};
```

**Options for WMB**:
1. **Reuse `xcb` field**: Point to WMB structure instead (risky, type mismatch)
2. **Add `wmb` field**: Add `WmbClientPtr wmb` to Display (requires Xlibint.h change)
3. **Use `trans_conn`**: Some Display fields are for transport - could reuse
4. **Separate build**: WMB has its own Xlibint.h with WMB-specific fields

**Recommendation**: Option 2 or 4 - cleanest separation

---

## 6. Related Files and Dependencies

### 6.1 Header Dependencies

| File | Contains | WMB Impact |
|------|----------|------------|
| `libX11/src/Xlibint.h` | Display struct, macros | May need WMB version |
| `libX11/src/Xxcbint.h` | XCB private struct | Not used in WMB |
| `libX11/src/Xintconn.h` | Connection internals | Not used in WMB |

### 6.2 XCB-Aware Macros in Xlibint.h

These macros use `dpy->xcb` internally:

```c
// Request/Reply macros
GetReq(name, req)           // Allocates request in XCB buffer
GetReqExtra(name, n, req)   // Allocates request with extra data
GetResReq(name, rid, req)   // Request with resource ID
GetEmptyReq(name, req)      // Empty request

// Data transfer
Data(dpy, data, len)        // Write data to XCB buffer
Data16(dpy, data, len)      // Write 16-bit data
Data32(dpy, data, len)      // Write 32-bit data
_XRead(dpy, data, len)      // Read from XCB
_XReadPad(dpy, data, len)   // Read with padding
_XReply(dpy, rep, extra, discard)  // Wait for reply

// Sync/Flush
SyncHandle()                // Sync with server
FlushGC(dpy, gc)            // Flush GC changes
```

**WMB Impact**: All of these need WMB versions that use memory queues instead of XCB.

### 6.3 Other libX11 Files Using XCB

```
libX11/src/xcb_disp.c   - _XConnectXCB implementation
libX11/src/xcb_io.c     - XCB I/O functions
libX11/src/x11_xcb.c    - XCB interop
```

**WMB Impact**: These files are NOT used in WMB - we replace this entire layer.

---

## 7. Proposed WMB Implementation

### 7.1 New File: libX11-wmb/src/OpenDis.c

```c
/*
 * WMB Internal Client Version of XOpenDisplay
 *
 * This replaces the standard XCB-based XOpenDisplay with a version
 * that creates an internal client connection to TinyX.
 */

#include "Xlibint.h"
#include "wmb_client.h"
#include <X11/Xatom.h>

static xReq _dummy_request = { 0, 0, 0 };

static void OutOfMemory(Display *dpy);

Display *
XOpenDisplay(register _Xconst char *display)
{
    register Display *dpy;
    register int i;
    int j, k;
    char *display_name;
    int iscreen;
    xConnSetupPrefix prefix;
    int vendorlen;
    union {
        xConnSetup *setup;
        char *vendor;
        xPixmapFormat *sf;
        xWindowRoot *rp;
        xDepth *dp;
        xVisualType *vp;
    } u;
    long setuplength;
    long usedbytes = 0;
    unsigned long mask;
    long int conn_buf_size;

    /*
     * WMB: Display name is optional for internal client
     * Default to ":0" (local display)
     */
    if (display == NULL || *display == '\0') {
        display_name = ":0";
    } else {
        display_name = (char *)display;
    }

    /* Set default error handlers */
    if (_XErrorFunction == NULL) (void) XSetErrorHandler(NULL);
    if (_XIOErrorFunction == NULL) (void) XSetIOErrorHandler(NULL);

    /* Allocate Display structure */
    if ((dpy = Xcalloc(1, sizeof(Display))) == NULL) {
        return NULL;
    }

    if ((dpy->display_name = strdup(display_name)) == NULL) {
        OutOfMemory(dpy);
        return NULL;
    }

    /*
     * ═══════════════════════════════════════════════════════════════════
     * WMB CHANGE: Create internal client connection (replaces _XConnectXCB)
     * ═══════════════════════════════════════════════════════════════════
     */
    WmbClientPtr wmb_client = wmb_connect_internal();
    if (!wmb_client) {
        OutOfMemory(dpy);
        return NULL;
    }

    /* Store WMB client in Display structure */
    dpy->wmb = wmb_client;  /* Requires adding 'wmb' field to Display */
    iscreen = 0;            /* TinyX default screen */

    /* Link to OS context for scheduling */
    AppContext *ctx = os_get_current_context();
    if (ctx) {
        wmb_client->os_context = ctx;
    }

    /*
     * Initialize Display pointers to NULL
     * (UNCHANGED from standard libX11)
     */
    dpy->keysyms        = (KeySym *) NULL;
    dpy->modifiermap    = NULL;
    dpy->lock_meaning   = NoSymbol;
    dpy->keysyms_per_keycode = 0;
    dpy->xdefaults      = (char *)NULL;
    dpy->scratch_length = 0L;
    dpy->scratch_buffer = NULL;
    dpy->key_bindings   = NULL;
    dpy->ext_procs      = (_XExtension *)NULL;
    dpy->ext_data       = (XExtData *)NULL;
    dpy->ext_number     = 0;
    dpy->event_vec[X_Error] = _XUnknownWireEvent;
    dpy->event_vec[X_Reply] = _XUnknownWireEvent;
    dpy->wire_vec[X_Error]  = _XUnknownNativeEvent;
    dpy->wire_vec[X_Reply]  = _XUnknownNativeEvent;
    for (i = KeyPress; i < LASTEvent; i++) {
        dpy->event_vec[i] = _XWireToEvent;
        dpy->wire_vec[i]  = NULL;
    }
    for (i = LASTEvent; i < 128; i++) {
        dpy->event_vec[i] = _XUnknownWireEvent;
        dpy->wire_vec[i]  = _XUnknownNativeEvent;
    }
    dpy->resource_id    = 0;
    dpy->db             = (struct _XrmHashBucketRec *)NULL;
    dpy->cursor_font    = None;
    dpy->flags          = 0;
    dpy->async_handlers = NULL;
    dpy->screens        = NULL;
    dpy->vendor         = NULL;
    dpy->buffer         = NULL;
    dpy->atoms          = NULL;
    dpy->error_vec      = NULL;
    dpy->context_db     = NULL;
    dpy->free_funcs     = NULL;
    dpy->pixmap_format  = NULL;
    dpy->cms.clientCmaps = NULL;
    dpy->cms.defaultCCCs = NULL;
    dpy->cms.perVisualIntensityMaps = NULL;
    dpy->im_filters     = NULL;
    dpy->bigreq_size    = 0;
    dpy->lock           = NULL;
    dpy->lock_fns       = NULL;
    dpy->qfree          = NULL;
    dpy->next_event_serial_num = 1;
    dpy->im_fd_info     = NULL;
    dpy->im_fd_length   = 0;
    dpy->conn_watchers  = NULL;
    dpy->watcher_count  = 0;
    dpy->filedes        = NULL;
    dpy->flushes        = NULL;
    dpy->xcmisc_opcode  = 0;
    dpy->xkb_info       = NULL;
    dpy->exit_handler_data = NULL;
    dpy->in_ifevent     = 0;

    /*
     * Setup Display info
     */
    dpy->vnumber = X_PROTOCOL;
    dpy->resource_alloc = _XAllocID;
    dpy->idlist_alloc = _XAllocIDs;
    dpy->synchandler = NULL;
    dpy->savedsynchandler = NULL;
    dpy->request = 0;
    dpy->last_request_read = 0;
    dpy->default_screen = iscreen;
    dpy->last_req = (char *)&_dummy_request;
    dpy->error_threads = NULL;
    dpy->exit_handler = _XDefaultIOErrorExit;

    /* WMB: Skip display lock for single-threaded operation */
    /* WMB: Skip pollfd cache (no file descriptors) */

    /*
     * Setup output buffers
     */
    conn_buf_size = 16384;  /* 16k default */

    if ((dpy->bufptr = dpy->buffer = Xcalloc(1, conn_buf_size)) == NULL) {
        OutOfMemory(dpy);
        return NULL;
    }
    dpy->bufmax = dpy->buffer + conn_buf_size;

    /* Event queue */
    dpy->head = dpy->tail = NULL;
    dpy->qlen = 0;

    /* Free-function record */
    if ((dpy->free_funcs = Xcalloc(1, sizeof(_XFreeFuncRec))) == NULL) {
        OutOfMemory(dpy);
        return NULL;
    }

    /*
     * ═══════════════════════════════════════════════════════════════════
     * WMB CHANGE: Get setup data from TinyX (replaces xcb_get_setup)
     * ═══════════════════════════════════════════════════════════════════
     */
    {
        const void *tinyx_setup = wmb_get_setup_data(&setuplength);
        if (!tinyx_setup) {
            OutOfMemory(dpy);
            return NULL;
        }
        memcpy(&prefix, tinyx_setup, sizeof(prefix));
        u.setup = (xConnSetup *)((char *)tinyx_setup + SIZEOF(xConnSetupPrefix));
    }

    /*
     * Parse setup data
     * (UNCHANGED from standard libX11 - this just parses the data)
     */
    usedbytes = sz_xConnSetup;
    if (setuplength < usedbytes) {
        OutOfMemory(dpy);
        return NULL;
    }

    dpy->proto_major_version = prefix.majorVersion;
    dpy->proto_minor_version = prefix.minorVersion;
    dpy->release        = u.setup->release;
    dpy->resource_base  = u.setup->ridBase;
    dpy->resource_mask  = u.setup->ridMask;
    dpy->min_keycode    = u.setup->minKeyCode;
    dpy->max_keycode    = u.setup->maxKeyCode;
    dpy->motion_buffer  = u.setup->motionBufferSize;
    dpy->nformats       = u.setup->numFormats;
    dpy->nscreens       = u.setup->numRoots;
    dpy->byte_order     = u.setup->imageByteOrder;
    dpy->bitmap_unit    = u.setup->bitmapScanlineUnit;
    dpy->bitmap_pad     = u.setup->bitmapScanlinePad;
    dpy->bitmap_bit_order = u.setup->bitmapBitOrder;
    dpy->max_request_size = u.setup->maxRequestSize;

    mask = dpy->resource_mask;
    dpy->resource_shift = 0;
    if (!mask) {
        OutOfMemory(dpy);
        return NULL;
    }
    while (!(mask & 1)) {
        dpy->resource_shift++;
        mask = mask >> 1;
    }
    dpy->resource_max = (dpy->resource_mask >> dpy->resource_shift) - 5;

    /* Parse vendor string */
    if (u.setup->nbytesVendor > 256) {
        OutOfMemory(dpy);
        return NULL;
    }
    dpy->vendor = Xmalloc(u.setup->nbytesVendor + 1);
    if (dpy->vendor == NULL) {
        OutOfMemory(dpy);
        return NULL;
    }
    vendorlen = u.setup->nbytesVendor;

    usedbytes += (vendorlen + 3) & ~3;
    if (setuplength < usedbytes) {
        OutOfMemory(dpy);
        return NULL;
    }

    u.setup = (xConnSetup *)(((char *)u.setup) + sz_xConnSetup);
    (void)strncpy(dpy->vendor, u.vendor, (size_t)vendorlen);
    dpy->vendor[vendorlen] = '\0';
    vendorlen = (vendorlen + 3) & ~3;
    u.vendor += vendorlen;

    /* Parse pixmap formats */
    dpy->pixmap_format = Xcalloc(dpy->nformats, sizeof(ScreenFormat));
    if (dpy->pixmap_format == NULL) {
        OutOfMemory(dpy);
        return NULL;
    }

    usedbytes += dpy->nformats * sz_xPixmapFormat;
    if (setuplength < usedbytes) {
        OutOfMemory(dpy);
        return NULL;
    }

    for (i = 0; i < dpy->nformats; i++) {
        register ScreenFormat *fmt = &dpy->pixmap_format[i];
        fmt->depth = u.sf->depth;
        fmt->bits_per_pixel = u.sf->bitsPerPixel;
        fmt->scanline_pad = u.sf->scanLinePad;
        fmt->ext_data = NULL;
        u.sf = (xPixmapFormat *)(((char *)u.sf) + sz_xPixmapFormat);
    }

    /* Parse screens */
    dpy->screens = Xcalloc(dpy->nscreens, sizeof(Screen));
    if (dpy->screens == NULL) {
        OutOfMemory(dpy);
        return NULL;
    }

    for (i = 0; i < dpy->nscreens; i++) {
        register Screen *sp = &dpy->screens[i];
        VisualID root_visualID;

        usedbytes += sz_xWindowRoot;
        if (setuplength < usedbytes) {
            OutOfMemory(dpy);
            return NULL;
        }

        root_visualID = u.rp->rootVisualID;
        sp->display     = dpy;
        sp->root        = u.rp->windowId;
        sp->cmap        = u.rp->defaultColormap;
        sp->white_pixel = u.rp->whitePixel;
        sp->black_pixel = u.rp->blackPixel;
        sp->root_input_mask = u.rp->currentInputMask;
        sp->width       = u.rp->pixWidth;
        sp->height      = u.rp->pixHeight;
        sp->mwidth      = u.rp->mmWidth;
        sp->mheight     = u.rp->mmHeight;
        sp->min_maps    = u.rp->minInstalledMaps;
        sp->max_maps    = u.rp->maxInstalledMaps;
        sp->backing_store = u.rp->backingStore;
        sp->save_unders = u.rp->saveUnders;
        sp->root_depth  = u.rp->rootDepth;
        sp->ndepths     = u.rp->nDepths;
        sp->ext_data    = NULL;
        u.rp = (xWindowRoot *)(((char *)u.rp) + sz_xWindowRoot);

        /* Parse depths */
        sp->depths = Xcalloc(sp->ndepths, sizeof(Depth));
        if (sp->depths == NULL) {
            OutOfMemory(dpy);
            return NULL;
        }

        for (j = 0; j < sp->ndepths; j++) {
            Depth *dp = &sp->depths[j];

            usedbytes += sz_xDepth;
            if (setuplength < usedbytes) {
                OutOfMemory(dpy);
                return NULL;
            }

            dp->depth = u.dp->depth;
            dp->nvisuals = u.dp->nVisuals;
            u.dp = (xDepth *)(((char *)u.dp) + sz_xDepth);

            if (dp->nvisuals > 0) {
                dp->visuals = Xcalloc(dp->nvisuals, sizeof(Visual));
                if (dp->visuals == NULL) {
                    OutOfMemory(dpy);
                    return NULL;
                }
                for (k = 0; k < dp->nvisuals; k++) {
                    register Visual *vp = &dp->visuals[k];

                    usedbytes += sz_xVisualType;
                    if (setuplength < usedbytes) {
                        OutOfMemory(dpy);
                        return NULL;
                    }

                    vp->visualid    = u.vp->visualID;
                    vp->class       = u.vp->class;
                    vp->bits_per_rgb = u.vp->bitsPerRGB;
                    vp->map_entries = u.vp->colormapEntries;
                    vp->red_mask    = u.vp->redMask;
                    vp->green_mask  = u.vp->greenMask;
                    vp->blue_mask   = u.vp->blueMask;
                    vp->ext_data    = NULL;
                    u.vp = (xVisualType *)(((char *)u.vp) + sz_xVisualType);
                }
            } else {
                dp->visuals = (Visual *)NULL;
            }
        }
        sp->root_visual = _XVIDtoVisual(dpy, root_visualID);
    }

    /* Validate screen */
    if (iscreen >= dpy->nscreens) {
        OutOfMemory(dpy);
        return NULL;
    }

    /*
     * ═══════════════════════════════════════════════════════════════════
     * WMB CHANGE: Get max request size from TinyX
     * ═══════════════════════════════════════════════════════════════════
     */
    dpy->bigreq_size = wmb_get_max_request_size();
    if (dpy->bigreq_size <= dpy->max_request_size)
        dpy->bigreq_size = 0;

    /*
     * Create default GCs
     * (Uses XCreateGC which will use WMB transport)
     */
    for (i = 0; i < dpy->nscreens; i++) {
        register Screen *sp = &dpy->screens[i];
        XGCValues values;
        values.foreground = sp->black_pixel;
        values.background = sp->white_pixel;
        if ((sp->default_gc = XCreateGC(dpy, sp->root,
                                        GCForeground|GCBackground,
                                        &values)) == NULL) {
            OutOfMemory(dpy);
            return NULL;
        }
    }

    /*
     * WMB: Skip resource manager query
     * (Not needed for embedded systems, can add later if required)
     */
    dpy->xdefaults = NULL;

    return dpy;
}

/* OutOfMemory - cleanup on failure */
static void OutOfMemory(Display *dpy)
{
    if (dpy->wmb) {
        wmb_disconnect_internal(dpy->wmb);
    }
    _XFreeDisplayStructure(dpy);
}
```

---

## 8. Risk Assessment

### 8.1 High Risk Areas

| Area | Risk | Mitigation |
|------|------|------------|
| Display structure changes | May break other libX11 code | Use separate WMB Xlibint.h |
| Macro dependencies | Many macros use XCB | Implement WMB macro versions |
| XCreateGC dependency | Uses full X11 request/reply | Ensure WMB transport works |
| Thread safety | Current code not thread-safe | WMB is single-threaded |

### 8.2 Medium Risk Areas

| Area | Risk | Mitigation |
|------|------|------------|
| Setup data format | Must match X11 wire protocol | Use TinyX's existing setup |
| Resource ID allocation | Must not conflict with server | Coordinate with TinyX |
| Error handling | OutOfMemory path complexity | Test thoroughly |

### 8.3 Low Risk Areas

| Area | Risk | Mitigation |
|------|------|------------|
| Display name parsing | Simple string handling | N/A |
| Pointer initialization | Just NULL assignments | N/A |
| Setup data parsing | Independent of source | N/A |

### 8.4 Recommended Testing Strategy

1. **Unit test `wmb_connect_internal()`** - Verify client creation
2. **Unit test `wmb_get_setup_data()`** - Verify setup data matches wire format
3. **Integration test XOpenDisplay()** - Full connection flow
4. **Test XCreateGC()** - First real X11 request after connection
5. **Test XCreateWindow()** - Verify full functionality

---

## Document History

| Date | Author | Changes |
|------|--------|---------|
| 2026-01-25 | Claude | Initial document - XOpenDisplay analysis |
