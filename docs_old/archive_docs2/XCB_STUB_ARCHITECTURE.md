# XCB Stub Architecture for WMB

## Document Purpose

This document describes the **XCB Stub Architecture** - an elegant approach to implementing libX11-WMB that minimizes code changes while enabling direct client-server communication within a single address space.

---

## 1. Executive Summary

### The Key Insight

libX11 was designed with a clean separation:
1. **API functions** (XCreateWindow, XMapWindow, etc.) build request structures
2. **Transport layer** (XCB) sends those structures over sockets
3. **SyncHandle()** is called at the end of every API function

**Our approach**: Use `dpy->synchandler` as the execution trigger - when `SyncHandle()` is called, the request is COMPLETE and ready for dispatch.

### Benefits

| Aspect | Benefit |
|--------|---------|
| **libX11 API files** | **ZERO changes** - all 172 API files completely unchanged |
| **libX11 macros** | **ONE change** - Data() macro in Xlibint.h (resize instead of _XSend) |
| **Request compatibility** | Proc* functions receive properly formatted requests |
| **Variable-length data** | Data() macro appends to dpy->buffer naturally |
| **Architecture clarity** | Hook existing mechanism, don't replace it |
| **Timing guarantee** | SyncHandle() called AFTER request is fully built |
| **Maintenance** | Single sync handler function for all requests |

---

## 2. Architecture Overview

### 2.1 Current X11 Flow (Socket-Based)

```
┌────────────────────────────────────────────────────────────────────────┐
│ APPLICATION (JWM)                                                      │
│                                                                        │
│   XCreateWindow(dpy, parent, x, y, w, h, ...)                          │
└────────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
┌────────────────────────────────────────────────────────────────────────┐
│ libX11 API LAYER                                                       │
│                                                                        │
│   LockDisplay(dpy);                                                    │
│   GetReq(CreateWindow, req);        ← Macro calls _XGetRequest()       │
│   req->wid = wid;                                                      │
│   req->parent = parent;             ← Fills in request structure       │
│   req->x = x; req->y = y; ...                                          │
│   UnlockDisplay(dpy);                                                  │
│   SyncHandle();                     ← Calls synchandler if set         │
└────────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
┌────────────────────────────────────────────────────────────────────────┐
│ XCB TRANSPORT LAYER (xcb_io.c)                                         │
│                                                                        │
│   _XGetRequest()  → Allocates buffer from XCB                          │
│   _XSend()        → xcb_send_request() over socket                     │
│   _XReply()       → xcb_wait_for_reply() from socket                   │
│   _XRead()        → Read reply data from socket                        │
└────────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼ Socket/IPC
┌────────────────────────────────────────────────────────────────────────┐
│ TinyX SERVER                                                           │
│                                                                        │
│   ReadRequestFromClient()          ← Reads from socket                 │
│   Dispatch()                       ← Looks up opcode                   │
│   ProcCreateWindow(client)         ← Executes request                  │
│   WriteReplyToClient()             ← Sends reply back                  │
└────────────────────────────────────────────────────────────────────────┘
```

### 2.2 WMB Flow (SyncHandle-Based) ⭐ KEY DISCOVERY

```
┌────────────────────────────────────────────────────────────────────────┐
│ APPLICATION (JWM)                                                      │
│                                                                        │
│   XCreateWindow(dpy, parent, x, y, w, h, ...)                          │
└────────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼
┌────────────────────────────────────────────────────────────────────────┐
│ libX11 API LAYER  ← COMPLETELY UNCHANGED!                              │
│                                                                        │
│   LockDisplay(dpy);                                                    │
│   GetReq(CreateWindow, req);        ← wmb: allocate buffer, save opcode│
│   req->wid = wid;                                                      │
│   req->parent = parent;             ← Same request building            │
│   req->x = x; req->y = y; ...       ← Request buffer being filled      │
│   UnlockDisplay(dpy);               ← Request is NOW COMPLETE          │
│   SyncHandle();                     ← ⭐ TRIGGERS wmb_sync_handler()   │
│        │                                                               │
│        └──────────────────────────────────────────────────────────────┐│
└───────────────────────────────────────────────────────────────────────┘│
                                     │                                   │
┌────────────────────────────────────┼───────────────────────────────────┘
│ WMB SYNC HANDLER (set during XOpenDisplay)                             │
│                                                                        │
│   wmb_sync_handler(dpy):                                               │
│       if (wmb_state.pending_opcode != 0) {                             │
│           client->requestBuffer = dpy->buffer;                         │
│           ProcVector[pending_opcode](client);  ← DIRECT CALL!          │
│           wmb_state.pending_opcode = 0;                                │
│       }                                                                │
└────────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     ▼ Direct function call (same process)
┌────────────────────────────────────────────────────────────────────────┐
│ TinyX SERVER                                                           │
│                                                                        │
│   ProcCreateWindow(client)         ← Executes request directly         │
│   (No socket, no encoding/decoding overhead)                           │
└────────────────────────────────────────────────────────────────────────┘
```

### 2.3 The SyncHandle Pattern

**Every libX11 API function follows this pattern:**

```c
int XSomeFunction(Display *dpy, ...)
{
    LockDisplay(dpy);              // 1. Lock

    GetReq(SomeRequest, req);      // 2. Allocate buffer
    req->field1 = value1;          // 3. Fill request
    req->field2 = value2;

    UnlockDisplay(dpy);            // 4. Unlock (request COMPLETE)
    SyncHandle();                  // 5. ⭐ OUR HOOK - execute now!

    return result;
}
```

**Verified:** 168 files use `SyncHandle()`, all 155 files using `GetReq` have it.

The `SyncHandle()` macro:
```c
#define SyncHandle() \
    if (dpy->synchandler) (*dpy->synchandler)(dpy)
```

### 2.4 The Data() Macro Challenge ⭐ CRITICAL DISCOVERY

Many X11 API functions append variable-length data after the fixed request header using the `Data()` macro:

```c
// Example: XStoreColors - sends array of color items
int XStoreColors(Display *dpy, Colormap cmap, XColor *defs, int ncolors)
{
    LockDisplay(dpy);
    GetReq(StoreColors, req);           // Fixed 8-byte header
    req->cmap = cmap;

    for (i = 0; i < ncolors; i++) {
        xColorItem citem;               // Local variable!
        citem.pixel = defs[i].pixel;
        citem.red = defs[i].red;
        citem.flags = defs[i].flags;
        Data(dpy, &citem, sizeof(citem));  // Append to buffer
    }

    UnlockDisplay(dpy);
    SyncHandle();                        // Execute complete request
    return 1;
}
```

**The original `Data()` macro:**
```c
#define Data(dpy, data, len) {\
    if (dpy->bufptr + (len) <= dpy->bufmax) {\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    } else\
        _XSend(dpy, (_Xconst char*)(data), (long)(len));\
}
```

**Problem:** The original calls `_XSend()` when buffer overflows, which would bypass our SyncHandle execution path.

**Solution:** Modify `Data()` to resize `dpy->buffer` instead of calling `_XSend()`:

```c
// Modified Data() macro for WMB
#define Data(dpy, data, len) {\
    if (dpy->bufptr + (len) <= dpy->bufmax) {\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    } else {\
        _XBufferOverflow(dpy, (len));\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    }\
}
```

**17 files** use `Data()` macro: StColors.c, PutImage.c, Font.c, IntAtom.c, etc.

### 2.5 The Buffer Architecture

**Complete request flow using `dpy->buffer`:**

```
┌─────────────────────────────────────────────────────────────────────────┐
│  dpy->buffer                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │ Fixed Request Header │ Variable Data (from Data() calls)            ││
│  │ (8-32 bytes)         │                                              ││
│  └──────────────────────┴──────────────────────────────────────────────┘│
│  ▲                       ▲                                    ▲         │
│  dpy->buffer             dpy->bufptr (advances)               dpy->bufmax
└─────────────────────────────────────────────────────────────────────────┘

_XGetRequest():  Reset bufptr = buffer, return pointer to buffer
API code:        Fill fixed header at buffer
Data() calls:    Append variable data, advance bufptr
SyncHandle():    Execute with complete request (length = bufptr - buffer)
```

---

## 3. Execution Triggers

### 3.1 Three Execution Paths

| Trigger | When Called | Request Type | WMB Action |
|---------|-------------|--------------|------------|
| **`SyncHandle()`** | End of every API function | Fire-and-forget | Execute via synchandler |
| **`_XReply()`** | Before SyncHandle for sync requests | Needs reply | Execute immediately, return reply |
| **`_XFlush()`** | Explicit flush request | Batch execute | Safety net for any pending |

### 3.2 Fire-and-Forget Requests (Most Common)

```c
// Example: XMapWindow - no reply needed
int XMapWindow(Display *dpy, Window w)
{
    LockDisplay(dpy);
    GetResReq(MapWindow, w, req);    // ← _XGetRequest() records opcode
    UnlockDisplay(dpy);
    SyncHandle();                     // ← wmb_sync_handler() executes ProcMapWindow
    return 1;
}
```

**WMB Flow:**
1. `_XGetRequest()` allocates buffer, records `X_MapWindow` opcode
2. Code fills `req->id = w`
3. `SyncHandle()` → `wmb_sync_handler()` → `ProcMapWindow(client)`

### 3.3 Synchronous Requests (Need Reply)

```c
// Example: XGetWindowAttributes - needs reply
Status XGetWindowAttributes(Display *dpy, Window w, XWindowAttributes *attr)
{
    LockDisplay(dpy);
    GetResReq(GetWindowAttributes, w, req);

    if (!_XReply(dpy, &rep, 0, xTrue))   // ← Execute HERE, get reply
        return 0;

    // ... process reply ...

    UnlockDisplay(dpy);
    SyncHandle();                         // ← No-op (already executed)
    return 1;
}
```

**WMB Flow:**
1. `_XGetRequest()` allocates buffer, records opcode
2. Code fills request
3. `_XReply()` executes `ProcGetWindowAttributes(client)`, copies reply
4. `SyncHandle()` sees `pending_opcode == 0`, does nothing

---

## 4. Stub Functions Specification

### 4.1 Core Stubs Required

These functions are defined in `Xlibint.h` and implemented in `xcb_io.c`. We replace them:

| Function | Original Purpose | WMB Stub Behavior |
|----------|------------------|-------------------|
| `_XGetRequest()` | Allocate request buffer from XCB | Allocate local buffer, record opcode |
| `_XReply()` | Wait for reply from server | Execute Proc*, return reply |
| `_XRead()` | Read data from socket | Copy from reply buffer |
| `_XReadPad()` | Read with padding | Copy from reply buffer with padding |
| `_XSend()` | Send buffered data | No-op (SyncHandle executes) |
| `_XFlush()` | Flush output buffer | Safety: execute if pending |
| `_XReadEvents()` | Read pending events | Copy from event queue |
| `_XEventsQueued()` | Check event count | Return wmb event queue count |

### 4.2 Connection Stubs Required

| Function | Original Purpose | WMB Stub Behavior |
|----------|------------------|-------------------|
| `_XConnectXCB()` | Create XCB socket connection | Register as internal client, set synchandler |
| `xcb_get_setup()` | Get setup from XCB | Return pointer to TinyX setup data |
| `xcb_get_maximum_request_length()` | Query max request size | Return TinyX constant |
| `xcb_disconnect()` | Close connection | Unregister internal client |

### 4.3 Functions That Can Be NOPs

| Function | Why NOP is Safe |
|----------|-----------------|
| `xcb_take_socket()` | No socket to manage |
| `return_socket()` | No socket to return |
| `require_socket()` | No socket required |
| `_XSeqSyncFunction()` | Synchronization not needed (same process) |

---

## 5. Detailed Stub Implementation

### 5.1 State Management

```c
// wmb_stubs.c

#include "Xlibint.h"
#include "dixstruct.h"      // ClientRec
#include "dispatch.h"       // ProcVector

/* Configuration */
#define WMB_INITIAL_BUFFER_SIZE  4096   /* Initial dpy->buffer size */
#define WMB_MAX_BUFFER_SIZE      (1 << 20)  /* 1MB hard limit */

/* WMB Internal State */
typedef struct {
    ClientPtr       client;          /* TinyX client record */
    CARD8           pending_opcode;  /* Opcode of pending request (0 = none) */
    char           *reply_buffer;    /* Reply from last Proc* call */
    size_t          reply_size;      /* Size of reply data */
    size_t          reply_offset;    /* Read position in reply */
} WmbState;

static WmbState wmb_state = {0};

/*
 * NOTE: Request buffer is now dpy->buffer (not separate wmb_state.request_buffer)
 * This allows Data() macro to append variable-length data naturally.
 *
 * Buffer layout:
 *   dpy->buffer  = start of request buffer
 *   dpy->bufptr  = current write position (advances with Data() calls)
 *   dpy->bufmax  = end of allocated buffer
 *
 * Complete request length = dpy->bufptr - dpy->buffer
 */
```

### 5.2 The Sync Handler (⭐ KEY FUNCTION)

```c
/*
 * wmb_sync_handler - Called by SyncHandle() after every X11 API call
 *
 * This is the execution trigger. When called:
 * - The request buffer (dpy->buffer) has been completely filled
 * - Data() has appended any variable-length data
 * - UnlockDisplay() has been called
 * - Total request length = dpy->bufptr - dpy->buffer
 */
int wmb_sync_handler(Display *dpy)
{
    if (wmb_state.pending_opcode == 0)
        return 0;  /* No pending request (already executed by _XReply) */

    if (wmb_state.client == NULL || dpy->buffer == NULL)
        return 0;  /* Not initialized */

    /* Calculate actual request length (fixed header + variable data) */
    size_t request_length = dpy->bufptr - dpy->buffer;

    /* Update length field in request header (in 4-byte units) */
    xReq *req = (xReq *)dpy->buffer;
    req->length = (request_length + 3) >> 2;  /* Round up to 4-byte boundary */

    /* Point client to dpy->buffer (the complete request) */
    wmb_state.client->requestBuffer = dpy->buffer;

    /* Execute the Proc* function directly! */
    CARD8 opcode = wmb_state.pending_opcode;
    int result = (*wmb_state.client->requestVector[opcode])(wmb_state.client);

    /* Clear pending - request has been executed */
    wmb_state.pending_opcode = 0;

    /* Reset buffer for next request */
    dpy->bufptr = dpy->buffer;

    /* TODO: Handle errors - generate X error events if result != Success */

    return 0;
}
```

### 5.3 _XGetRequest Implementation (⭐ Uses dpy->buffer)

```c
/*
 * _XGetRequest - Allocate buffer for building a request
 *
 * In standard libX11, this allocates from XCB's buffer.
 * In WMB, we use dpy->buffer directly! This allows Data() macro
 * to append variable-length data naturally.
 *
 * The complete request will be at dpy->buffer when SyncHandle() is called.
 */
void *_XGetRequest(Display *dpy, CARD8 type, size_t len)
{
    /* Ensure buffer is large enough for initial request */
    if (dpy->buffer == NULL || (dpy->bufmax - dpy->buffer) < len) {
        size_t new_size = (len > WMB_INITIAL_BUFFER_SIZE) ? len : WMB_INITIAL_BUFFER_SIZE;
        char *new_buffer = Xrealloc(dpy->buffer, new_size);
        if (new_buffer == NULL)
            return NULL;
        dpy->buffer = new_buffer;
        dpy->bufmax = new_buffer + new_size;
    }

    /* Reset buffer pointer to start - new request begins here */
    dpy->bufptr = dpy->buffer;

    /* Clear initial request area */
    memset(dpy->buffer, 0, len);

    /* Initialize request header */
    xReq *req = (xReq *)dpy->buffer;
    req->reqType = type;
    req->length = len >> 2;  /* Length in 4-byte units (updated by SyncHandle) */

    /* Advance bufptr past fixed header - Data() will append here */
    dpy->bufptr = dpy->buffer + len;

    /* Record opcode - will be executed by SyncHandle() */
    wmb_state.pending_opcode = type;

    /* Update display request counter */
    X_DPY_REQUEST_INCREMENT(dpy);

    return dpy->buffer;
}
```

### 5.4 _XReply Implementation (For Synchronous Requests)

```c
/*
 * _XReply - Execute request and get reply
 *
 * For requests that need a reply (XGetWindowAttributes, etc.),
 * this executes the request immediately rather than waiting
 * for SyncHandle().
 */
Status _XReply(Display *dpy, xReply *rep, int extra, Bool discard)
{
    if (wmb_state.pending_opcode == 0)
        return 0;  /* No pending request */

    /* Calculate actual request length (fixed header + variable data) */
    size_t request_length = dpy->bufptr - dpy->buffer;

    /* Update length field in request header (in 4-byte units) */
    xReq *req = (xReq *)dpy->buffer;
    req->length = (request_length + 3) >> 2;

    /* Point client to dpy->buffer (the complete request) */
    wmb_state.client->requestBuffer = dpy->buffer;

    /* Execute the Proc* function */
    CARD8 opcode = wmb_state.pending_opcode;
    int result = (*wmb_state.client->requestVector[opcode])(wmb_state.client);

    /* Clear pending - we just executed it */
    wmb_state.pending_opcode = 0;

    /* Reset buffer for next request */
    dpy->bufptr = dpy->buffer;

    if (result != Success) {
        /* TODO: Generate error event */
        return 0;
    }

    /* Copy reply from client's reply buffer */
    if (wmb_state.reply_buffer && rep) {
        memcpy(rep, wmb_state.reply_buffer, sizeof(xReply));
        wmb_state.reply_offset = sizeof(xReply);
    }

    return 1;  /* Success */
}
```

### 5.5 Request Dispatch Helper

```c
/*
 * wmb_dispatch_request - Execute the pending request
 *
 * This is the magic: instead of sending over a socket,
 * we call the Proc* function directly.
 *
 * NOTE: This helper is used by _XFlush() as a safety net.
 * Normal execution goes through wmb_sync_handler().
 */
static int wmb_dispatch_request(Display *dpy)
{
    if (wmb_state.client == NULL || dpy->buffer == NULL)
        return BadImplementation;

    /* Calculate actual request length */
    size_t request_length = dpy->bufptr - dpy->buffer;
    xReq *req = (xReq *)dpy->buffer;
    req->length = (request_length + 3) >> 2;

    /* Point client to dpy->buffer (the complete request) */
    wmb_state.client->requestBuffer = dpy->buffer;

    /* Get the appropriate Proc* function from dispatch table */
    CARD8 opcode = wmb_state.pending_opcode;

    /* Call the Proc* function directly! */
    int result = (*wmb_state.client->requestVector[opcode])
                    (wmb_state.client);

    return result;
}
```

### 4.4 _XReply Implementation

```c
/*
 * _XReply - Get reply from server
 *
 * In standard libX11, this waits on the socket for a reply.
 * In WMB, we execute the pending request and copy the reply.
 */
Status _XReply(Display *dpy, xReply *rep, int extra, Bool discard)
{
    /* Execute the pending request */
    int result = wmb_dispatch_request();

    if (result != Success) {
        /* TODO: Generate error event */
        return 0;
    }

    /* Copy reply from client's reply buffer */
    /* Note: Need to determine where TinyX stores replies */
    if (wmb_state.reply_buffer && rep) {
        memcpy(rep, wmb_state.reply_buffer, sizeof(xReply));
        wmb_state.reply_offset = sizeof(xReply);
    }

    /* Reset for next request */
    wmb_state.pending_opcode = 0;

    return 1;  /* Success */
}
```

### 4.5 _XRead Implementation

```c
/*
 * _XRead - Read additional data from reply
 *
 * Some replies have variable-length data after the fixed header.
 */
int _XRead(Display *dpy, char *data, long size)
{
    if (wmb_state.reply_buffer == NULL)
        return -1;

    /* Copy from reply buffer starting at current offset */
    size_t available = wmb_state.reply_size - wmb_state.reply_offset;
    if (size > available)
        size = available;

    memcpy(data, wmb_state.reply_buffer + wmb_state.reply_offset, size);
    wmb_state.reply_offset += size;

    return 0;
}

void _XReadPad(Display *dpy, char *data, long size)
{
    _XRead(dpy, data, size);
    /* Padding is handled internally, no action needed */
}
```

### 5.6 _XBufferOverflow - Buffer Resize (⭐ NEW FOR WMB)

```c
/*
 * _XBufferOverflow - Resize dpy->buffer when Data() exceeds bufmax
 *
 * This replaces the _XSend() call in the original Data() macro.
 * Instead of sending data over socket, we resize the buffer.
 */
void _XBufferOverflow(Display *dpy, long needed)
{
    /* Calculate current usage and required size */
    size_t current_offset = dpy->bufptr - dpy->buffer;
    size_t current_size = dpy->bufmax - dpy->buffer;
    size_t required = current_offset + needed + 256;  /* Extra headroom */

    /* Double the buffer size (up to max) */
    size_t new_size = current_size * 2;
    if (new_size < required)
        new_size = required;

    /* Enforce hard limit to prevent runaway allocations */
    if (new_size > WMB_MAX_BUFFER_SIZE) {
        /* TODO: Set error flag, request will fail */
        fprintf(stderr, "WMB: Buffer overflow limit reached\n");
        return;
    }

    /* Reallocate buffer */
    char *new_buffer = Xrealloc(dpy->buffer, new_size);
    if (new_buffer == NULL) {
        /* TODO: Set error flag */
        fprintf(stderr, "WMB: Buffer reallocation failed\n");
        return;
    }

    /* Update pointers (bufptr offset preserved) */
    dpy->buffer = new_buffer;
    dpy->bufptr = new_buffer + current_offset;
    dpy->bufmax = new_buffer + new_size;
}

/*
 * _XSend - Send data to server
 *
 * In WMB, this should NOT be called (Data() macro calls _XBufferOverflow).
 * Provided as safety net / debugging hook.
 */
void _XSend(Display *dpy, const char *data, long size)
{
    /* This should not be called in WMB architecture */
    fprintf(stderr, "WMB WARNING: _XSend called - should use Data() buffer\n");

    /* Append data to buffer as fallback */
    if (data && size > 0) {
        if (dpy->bufptr + size > dpy->bufmax) {
            _XBufferOverflow(dpy, size);
        }
        memcpy(dpy->bufptr, data, size);
        dpy->bufptr += ((size + 3) & ~3);
    }
}

/*
 * _XFlush - Flush output buffer
 *
 * In WMB, requests are executed by SyncHandle(), so this is mostly a NOP.
 */
void _XFlush(Display *dpy)
{
    /* Execute any pending request (safety net) */
    if (wmb_state.pending_opcode != 0) {
        wmb_sync_handler(dpy);
    }
}
```

---

## 6. Connection Handling (XOpenDisplay)

### 6.1 _XConnectXCB Stub (⭐ Sets Up SyncHandler)

```c
/*
 * _XConnectXCB - Establish connection to X server
 *
 * In standard libX11, this creates an XCB socket connection.
 * In WMB, we register as an internal client with TinyX and
 * SET THE SYNCHANDLER for request execution.
 */
Bool _XConnectXCB(Display *dpy, const char *display, int *screenp)
{
    /* No socket connection needed - we ARE the server */

    /* Register as internal WMB client with TinyX */
    wmb_state.client = wmb_create_internal_client();
    if (wmb_state.client == NULL)
        return False;

    /* ⭐ KEY: Set the sync handler for request execution */
    dpy->synchandler = wmb_sync_handler;

    /* Default screen */
    *screenp = 0;

    /* Initialize minimal XCB structure (to prevent NULL crashes) */
    dpy->xcb = Xcalloc(1, sizeof(_X11XCBPrivate));
    if (dpy->xcb == NULL) {
        wmb_destroy_internal_client(wmb_state.client);
        return False;
    }

    /* Mark as WMB internal connection */
    dpy->xcb->connection = NULL;  /* No real XCB connection */

    return True;
}
```

### 6.2 Setup Data Access

```c
/*
 * Stub for xcb_get_setup()
 *
 * Returns the connection setup data (screen info, formats, etc.)
 * In WMB, we build this from TinyX's screenInfo global.
 */

/* Forward declaration - defined in TinyX */
extern ScreenInfo screenInfo;

static xConnSetup *wmb_setup_data = NULL;

const struct xcb_setup_t *xcb_get_setup(xcb_connection_t *c)
{
    if (wmb_setup_data == NULL) {
        wmb_setup_data = wmb_build_setup_data();
    }
    return (const struct xcb_setup_t *)wmb_setup_data;
}

/*
 * Build setup data from TinyX globals
 */
static xConnSetup *wmb_build_setup_data(void)
{
    /* Calculate total size needed */
    size_t size = sizeof(xConnSetupPrefix) + sizeof(xConnSetup);
    /* Add space for vendor, formats, screens, depths, visuals... */

    xConnSetup *setup = Xcalloc(1, size);
    if (setup == NULL)
        return NULL;

    /* Fill from TinyX screenInfo */
    setup->release = VENDOR_RELEASE;
    setup->ridBase = /* from client */;
    setup->ridMask = RESOURCE_ID_MASK;
    setup->motionBufferSize = 0;
    setup->maxRequestSize = MAX_REQUEST_SIZE;
    setup->numRoots = screenInfo.numScreens;
    setup->numFormats = screenInfo.numPixmapFormats;
    setup->imageByteOrder = screenInfo.imageByteOrder;
    setup->bitmapBitOrder = screenInfo.bitmapBitOrder;
    setup->bitmapScanlineUnit = screenInfo.bitmapScanlineUnit;
    setup->bitmapScanlinePad = screenInfo.bitmapScanlinePad;
    setup->minKeyCode = /* from input */;
    setup->maxKeyCode = /* from input */;

    /* Copy vendor string */
    /* Copy pixmap formats */
    /* Copy screen info */

    return setup;
}
```

### 5.3 Maximum Request Length

```c
/*
 * Stub for xcb_get_maximum_request_length()
 */
uint32_t xcb_get_maximum_request_length(xcb_connection_t *c)
{
    /* Return TinyX's maximum, or BIG_REQUESTS extension value */
    return MAX_BIG_REQUEST_SIZE;  /* Typically 4MB / 4 = 1M units */
}
```

---

## 6. Event Handling

### 6.1 Event Queue Integration

Events flow from TinyX to the client. In socket-based X11, events are written to the socket and read by `_XReadEvents()`. In WMB:

```c
/*
 * WMB Event Queue
 *
 * TinyX will call wmb_queue_event() instead of WriteEventsToClient()
 * for internal WMB clients.
 */
typedef struct _WmbEventNode {
    xEvent event;
    struct _WmbEventNode *next;
} WmbEventNode;

static WmbEventNode *wmb_event_head = NULL;
static WmbEventNode *wmb_event_tail = NULL;
static int wmb_event_count = 0;

/*
 * Called by TinyX to deliver event to WMB client
 */
void wmb_queue_event(xEvent *event)
{
    WmbEventNode *node = Xmalloc(sizeof(WmbEventNode));
    if (node == NULL) return;

    memcpy(&node->event, event, sizeof(xEvent));
    node->next = NULL;

    if (wmb_event_tail)
        wmb_event_tail->next = node;
    else
        wmb_event_head = node;
    wmb_event_tail = node;
    wmb_event_count++;
}

/*
 * _XReadEvents - Read events from server
 */
void _XReadEvents(Display *dpy)
{
    while (wmb_event_head) {
        WmbEventNode *node = wmb_event_head;
        wmb_event_head = node->next;
        if (wmb_event_head == NULL)
            wmb_event_tail = NULL;
        wmb_event_count--;

        /* Convert wire event to Xlib event and queue */
        _XEnq(dpy, (xEvent *)&node->event);
        Xfree(node);
    }
}

/*
 * _XEventsQueued - Check for pending events
 */
int _XEventsQueued(Display *dpy, int mode)
{
    if (mode != QueuedAlready) {
        _XReadEvents(dpy);
    }
    return dpy->qlen + wmb_event_count;
}
```

---

## 7. TinyX Modifications Required

### 7.1 Internal Client Registration

```c
// tinyx/os/WMBclient.c (NEW FILE)

#include "dixstruct.h"
#include "dispatch.h"

/*
 * Create an internal WMB client (no socket)
 */
ClientPtr wmb_create_internal_client(void)
{
    ClientPtr client;

    /* Allocate client structure */
    client = Xcalloc(1, sizeof(ClientRec));
    if (client == NULL)
        return NULL;

    /* Initialize client */
    client->index = /* assign client index */;
    client->clientState = ClientStateRunning;
    client->requestVector = ProcVector;  /* Standard dispatch table */

    /* Allocate resource ID range */
    client->clientAsMask = /* allocate ID range */;

    /* Mark as internal WMB client (no osPrivate socket) */
    client->osPrivate = NULL;  /* Or use a marker */

    /* Add to client list */
    /* ... */

    return client;
}

void wmb_destroy_internal_client(ClientPtr client)
{
    if (client == NULL)
        return;

    /* Free client resources */
    FreeClientResources(client);

    /* Remove from client list */
    /* ... */

    Xfree(client);
}
```

### 7.2 Event Delivery Modification

```c
// tinyx/dix/events.c - MODIFY WriteEventsToClient

void WriteEventsToClient(ClientPtr client, int count, xEvent *events)
{
    /* Check if this is a WMB internal client */
    if (wmb_is_internal_client(client)) {
        /* Direct delivery to WMB event queue */
        for (int i = 0; i < count; i++) {
            wmb_queue_event(&events[i]);
        }
        return;
    }

    /* Standard socket-based delivery */
    /* ... existing code ... */
}
```

---

## 8. Build System Integration

### 8.1 Conditional Compilation

```makefile
# libX11/src/Makefile.am

if WMB_BUILD
# WMB build - use stubs instead of XCB
libX11_la_SOURCES += wmb_stubs.c
else
# Standard build - use XCB
libX11_la_SOURCES += xcb_io.c xcb_disp.c x11_xcb.c
libX11_la_LIBADD += $(XCB_LIBS)
endif
```

### 8.2 Header Configuration

```c
// config_wmb.h

#define WMB_INTERNAL_CLIENT 1

/* Disable XCB-specific features */
#undef HAVE_XCB
#undef XCB_USE_XLIB
```

---

## 9. Implementation Phases

### Phase 1: Minimal Stub (XOpenDisplay only)
1. Implement `_XConnectXCB()` stub
2. Implement `xcb_get_setup()` stub
3. Implement `xcb_get_maximum_request_length()` stub
4. Verify XOpenDisplay() returns valid Display*

### Phase 2: Request/Reply Stubs
1. Implement `_XGetRequest()`
2. Implement `_XReply()`
3. Implement `_XRead()` / `_XReadPad()`
4. Test simple request: XCreateGC()

### Phase 3: Event Handling
1. Implement `_XReadEvents()`
2. Implement `wmb_queue_event()`
3. Modify TinyX event delivery
4. Test: XNextEvent()

### Phase 4: Full Integration
1. Test with JWM
2. Handle edge cases
3. Performance optimization

---

## 11. File Summary

### New Files

| File | Purpose | Lines (est.) |
|------|---------|--------------|
| `libX11/src/wmb_stubs.c` | XCB replacement stubs + synchandler + _XBufferOverflow | ~450 |
| `tinyx/os/WMBclient.c` | Internal client management | ~200 |
| `config_wmb.h` | WMB build configuration | ~50 |

### Modified Files

| File | Change | Impact |
|------|--------|--------|
| `libX11/include/X11/Xlibint.h` | Modify Data() macro - call _XBufferOverflow() instead of _XSend() | 3 lines |
| `tinyx/dix/events.c` | Check for WMB client in WriteEventsToClient | ~10 lines |
| `libX11/src/Makefile.am` | Conditional xcb vs wmb | ~5 lines |

### Modified Xlibint.h (Single Macro Change)

```c
// BEFORE (original Data() macro):
#define Data(dpy, data, len) {\
    if (dpy->bufptr + (len) <= dpy->bufmax) {\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    } else\
        _XSend(dpy, (_Xconst char*)(data), (long)(len));\
}

// AFTER (WMB Data() macro):
#define Data(dpy, data, len) {\
    if (dpy->bufptr + (len) <= dpy->bufmax) {\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    } else {\
        _XBufferOverflow(dpy, (len));\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    }\
}
```

### Unchanged Files

**All 172 files using GetReq macros** - the entire libX11 API layer!

**All 168 files using SyncHandle()** - we hook, not replace!

**All 17 files using Data()** - macro change is transparent to them!

---

## 12. Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Reply buffer management | Medium | Careful study of how TinyX returns replies |
| Event timing | Medium | May need synchronization for multi-core |
| Request sequence numbers | Low | Stubs maintain sequence in Display |
| Error handling | Medium | Must generate proper X errors |
| Big requests | Low | Test with large requests |

---

## 13. Comparison: SyncHandle Approach vs. Alternatives

| Aspect | SyncHandle Hook | Replace xcb_io.c | Rewrite libX11 |
|--------|-----------------|------------------|----------------|
| libX11 changes | **0 files** | ~5 files | ~370 files |
| Execution trigger | Built-in | Custom | Custom |
| Timing guarantee | **Perfect** | Needs tracking | Needs tracking |
| Proc* compatibility | **Natural** | Needs adaptation | Full rewrite |
| Debugging | **Easy** | Medium | Hard |
| Risk | **Lowest** | Medium | High |
| Effort | **~650 lines** | ~1000 lines | ~50000 lines |

---

## Document History

| Date | Change |
|------|--------|
| 2026-01-25 | Initial creation - XCB Stub Architecture proposal |
| 2026-01-25 | **MAJOR UPDATE**: Discovered SyncHandle() as perfect execution trigger |
|            | - Added Section 2.3: The SyncHandle Pattern |
|            | - Added Section 3: Execution Triggers with three paths |
|            | - Added Section 5.2: wmb_sync_handler() as key function |
|            | - Updated _XConnectXCB to set dpy->synchandler |
|            | - Reduced estimated effort from ~750 to ~650 lines |
| 2026-01-25 | **CRITICAL UPDATE**: Data() macro handling for variable-length requests |
|            | - Added Section 2.4: The Data() Macro Challenge |
|            | - Added Section 2.5: The Buffer Architecture |
|            | - Changed from separate wmb_state.request_buffer to dpy->buffer |
|            | - Added _XBufferOverflow() to resize buffer instead of _XSend() |
|            | - Only ONE Xlibint.h change needed (Data() macro) |
|            | - All 17 files using Data() work unchanged |
