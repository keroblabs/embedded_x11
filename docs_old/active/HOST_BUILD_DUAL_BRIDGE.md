# Host Build — Dual Bridge Architecture

**Status:** Approved approach (Option C)
**Date:** 2026-05-29
**Scope:** How the WMB hosted build (Linux + SDL) supports both stock X11 applications and WMB-linked applications simultaneously, while JWM runs as an internal fiber client.

---

## 1. Background — How We Got Here

### 1.1 The Target Model (Tima OS)

WMB (Window Manager Binary) is a **single standalone binary** for Tima OS (a future RISC-V target). It bundles TinyX (KDrive X server) + JWM (window manager) + libX11-wmb into one linked image at a fixed load address (non-PIC).

On the target:

```
┌──────────────────────────────────────────────────────┐
│  Single WMB Binary (non-PIC, fixed load address)     │
│                                                      │
│  Fiber: JWM            ←─┐                           │
│  Fiber: App A          ←─┤ internal clients          │
│  Fiber: App B          ←─┘                           │
│         │                                            │
│         │ direct function call (no transport)        │
│         │ wmb_sync_handler → ProcVector[opcode]      │
│         ▼                                            │
│  Fiber: TinyX server                                 │
│         │                                            │
│         ▼                                            │
│  HAL → single framebuffer + input drivers            │
└──────────────────────────────────────────────────────┘
```

- Every application (including JWM) is a **fiber** inside WMB — cooperative context switching, no threads.
- Client → server communication is a **C function call**: the client's `wmb_sync_handler` calls `ProcVector[opcode]` directly, triggered by `SyncHandle()` after each Xlib request.
- External apps loaded by Tima OS use **one-liner wrappers** through a fixed entry-point table. They execute on their own Tima-OS-provided task context.
- **No socket. No IPC. No wire protocol. No serialization.** This is a non-negotiable requirement (§2.2 of PROJECT_KNOWLEDGE_BASE).

### 1.2 The Host Build Problem

On the target, "each app is a fiber" works because:

1. Tima OS controls app loading — no `main()` conflict.
2. Everything shares one address space by design.
3. The fiber runtime provides cooperative scheduling.

On Linux (the host development environment), the situation is fundamentally different:

| Constraint | Impact |
|---|---|
| Each X11 app has its own `main()` | Cannot link multiple `main()` symbols into one binary |
| Apps are separate compiled programs | They have their own address space, libc, global state |
| We need to test with real/existing X11 apps | `xterm`, `xclock`, etc. are pre-compiled binaries — cannot turn them into fibers |
| Developer workflow | Run WMB in terminal A, launch apps in terminal B — standard Unix development pattern |

**The fundamental contradiction:** The target has zero transport (everything is in-process), but the host **must** have inter-process communication because apps are separate processes.

### 1.3 Options Considered

We evaluated four approaches for the host build:

#### Option A — Keep TinyX's Native Socket Listener Only

Leave TinyX's existing Unix domain socket listener enabled. External apps connect using standard X11 wire protocol. JWM runs internally as a fiber.

**Rejected as sole approach:** Only tests the standard X11 path. Does not exercise the WMB-specific app-side stubs (`libX11-wmb`) or the entry-table mechanism that will be used on the target. Surprises are guaranteed when moving to target.

#### Option B — WMB Custom Socket Bridge Only

Create a thin socket bridge that mirrors the target's entry-table API. Apps link against `libX11-wmb` (app-side stubs) which serializes requests over a Unix socket to WMB.

**Rejected as sole approach:** Only works with apps compiled against `libX11-wmb`. Cannot test stock X11 apps (`xterm`, `xclock`) which are valuable for validating TinyX rendering and event handling.

#### Option C — Hybrid: Both Bridges Coexisting (CHOSEN)

WMB on host has **two listener paths** — the standard X11 socket for stock apps **and** a WMB-specific bridge for apps linked against `libX11-wmb`. Both coexist. Both can be enabled/disabled independently via preprocessor guards.

**Chosen because:**
- Maximum flexibility during development.
- Stock X11 apps validate that TinyX rendering and event handling work correctly.
- WMB-stub apps validate the exact target path (app-side stubs, entry-table protocol) before moving to hardware.
- JWM always runs as an internal fiber client — testing the target's internal-client path.
- Preprocessor guards allow building WMB in any combination: both bridges, X11-only, WMB-bridge-only, or standalone (target mode with zero listeners).

#### Option D — Single Binary with Renamed Mains (Fiber-Only)

Rename each app's `main()` to `app_X_main()`, compile everything into WMB as fibers. No socket at all.

**Rejected:** Cannot test stock X11 apps. JWM's `main()` uses `argc/argv`, signal handlers, `exit()` — needs substantial adaptation. Every test app must be recompiled into WMB — slow iteration. Cannot run different app combinations without rebuilding.

### 1.4 Why the Socket Is Not a Contradiction

The socket is **explicitly a host-development tool**. It does not exist on the target. The architectural principle "no IPC inside WMB" still holds:

- **JWM is always internal** — fiber, no socket, direct function call. This is the target path.
- **On target, ALL apps are internal** — fibers or entry-table calls. Zero sockets.
- **On host, socket bridges exist only for development** — to test external apps without recompiling WMB every time. They are compiled out on target via preprocessor guards.

The transport layer is **scaffolding**, not architecture. It lets us validate the system incrementally without requiring everything to be a fiber from day one.

---

## 2. Architecture: Three Client Categories

The host build serves three distinct client categories, each testing a different aspect of the target:

```
Terminal 1:                      Terminal 2:            Terminal 3:
┌────────────────────┐          ┌──────────────┐      ┌──────────────────┐
│ WMB executable     │          │ stock X11    │      │ WMB-linked app   │
│                    │          │ app (xterm)  │      │ (our test app)   │
│ TinyX server core  │          │              │      │                  │
│  ├─ X11 listener   │◄──X11───►│ system libX11│      │ libX11-wmb       │
│  │  (:N socket)    │  wire    │ (unmodified) │      │ (app-side stubs) │
│  │                 │  proto   │              │      │                  │
│  └─ WMB bridge     │◄──WMB───►│              │      │ wmb_client_stub  │
│     listener       │  proto   └──────────────┘      │ (thin transport) │
│     (:N.wmb socket)│                                │                  │
│                    │◄──WMB─────────────────────────►│                  │
│ Fiber: JWM         │  proto                         └──────────────────┘
│  (internal, direct │
│   function call)   │
└────────────────────┘
```

| Category | Client type | Transport | What it tests |
|---|---|---|---|
| **Internal** | JWM (compiled into WMB) | None — direct `wmb_sync_handler` → `ProcVector[opcode]` function call | Target internal-client path exactly |
| **Stock X11** | `xterm`, `xclock`, any pre-compiled X11 app | Standard X11 wire protocol over Unix socket (TinyX native) | TinyX rendering, event delivery, protocol correctness |
| **WMB-linked** | Apps compiled against `libX11-wmb` | WMB bridge protocol over Unix socket | Target external-app path — app-side stubs, entry-table protocol, response handling |

All three categories converge at the same point inside TinyX: **`ProcVector[opcode]`**. The difference is only how the request reaches that point:

```
Stock X11 app:
  system libX11 → X11 wire → socket → TinyX os/io.c → ReadRequestFromClient
    → ProcVector[opcode]

WMB-linked app:
  libX11-wmb stubs → wmb_msg envelope → WMB bridge socket → WMB bridge receiver
    → ProcVector[opcode]

Internal client (JWM):
  libX11-wmb stubs → wmb_sync_handler(dpy) → ProcVector[opcode]
    (zero serialization, zero transport)
```

---

## 3. Preprocessor Guards

All listener code is controlled by two independent preprocessor macros. This allows any combination:

```c
/* Build-time defines (CMake or command-line -D flags) */

/* WMB_ENABLE_X11_LISTENER
 *   When defined: TinyX socket listener is active.
 *   Stock X11 apps can connect via DISPLAY=:N.
 *   This is TinyX's native code — zero new code required.
 *   Undefined on target (WMB_STANDALONE_NO_LISTEN). */
#define WMB_ENABLE_X11_LISTENER

/* WMB_ENABLE_WMB_BRIDGE
 *   When defined: WMB bridge listener is active.
 *   WMB-linked apps connect via a Unix socket speaking
 *   the WMB bridge protocol.
 *   Undefined on target. */
#define WMB_ENABLE_WMB_BRIDGE
```

### 3.1 Build Profiles

| Profile | `WMB_ENABLE_X11_LISTENER` | `WMB_ENABLE_WMB_BRIDGE` | Use case |
|---|---|---|---|
| `WMB_HOSTED_FULL` | ✓ | ✓ | Full development — test everything |
| `WMB_HOSTED_X11_ONLY` | ✓ | ✗ | Quick smoke test with stock X11 apps |
| `WMB_HOSTED_WMB_ONLY` | ✗ | ✓ | Test WMB-specific path in isolation |
| `WMB_STANDALONE` (target) | ✗ | ✗ | Production — zero listeners, fiber-only |

### 3.2 CMake Integration

```cmake
# framework/CMakeLists.txt additions:

option(WMB_ENABLE_X11_LISTENER "Enable TinyX native X11 socket listener" ON)
option(WMB_ENABLE_WMB_BRIDGE   "Enable WMB bridge socket listener"       ON)

if(WMB_ENABLE_X11_LISTENER)
    add_definitions(-DWMB_ENABLE_X11_LISTENER)
endif()
if(WMB_ENABLE_WMB_BRIDGE)
    add_definitions(-DWMB_ENABLE_WMB_BRIDGE)
    list(APPEND SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/wmb_x11/wmb_bridge.c
    )
endif()

# Target build: both OFF
# cmake -DWMB_ENABLE_X11_LISTENER=OFF -DWMB_ENABLE_WMB_BRIDGE=OFF
```

### 3.3 Guard Placement in TinyX

The X11 listener is already in TinyX's `os/connection.c`. The guard wraps the socket creation in `CreateWellKnownSockets()`:

```c
/* tinyx/os/connection.c — guarded, upstream-safe patch */
void CreateWellKnownSockets(void)
{
#ifdef WMB_ENABLE_X11_LISTENER
    /* ... existing TinyX socket listener code ... */
    if ((_XSERVTransMakeAllCOTSServerListeners(...) >= 0) && ...)
    {
        /* ... set up ListenTransFds, WellKnownConnections ... */
    }
    if (!XFD_ANYSET(&WellKnownConnections))
        FatalError("Cannot establish any listening sockets...");
#endif

#ifdef WMB_ENABLE_WMB_BRIDGE
    wmb_bridge_listener_init();
#endif

#if !defined(WMB_ENABLE_X11_LISTENER) && !defined(WMB_ENABLE_WMB_BRIDGE)
    /* Standalone mode: no listeners. Internal clients only. */
    /* Suppress the FatalError that TinyX normally raises. */
#endif

    /* ... signal handlers, XDMCP, etc. ... */
}
```

This is the **single guarded edit** in `CreateWellKnownSockets` — matching the M3 milestone's `WMB_STANDALONE_NO_LISTEN` concept but extending it to support the dual-bridge model. The edit is behind `#ifdef`, upstream-safe, and counts as one modified TinyX core file.

---

## 4. X11 Bridge — Stock App Support (Native TinyX)

### 4.1 How It Works

This is **zero new code**. TinyX already implements the full X11 socket listener, connection handshake, request read, dispatch, and reply/event write paths:

```
tinyx/os/connection.c  — CreateWellKnownSockets, EstablishNewConnections,
                         AllocNewConnection, ClientAuthorized
tinyx/os/io.c          — ReadRequestFromClient, WriteToClient, FlushAllOutput
tinyx/dix/dispatch.c   — Dispatch, NextAvailableClient, ProcVector[]
```

The flow for a stock X11 app:

1. WMB starts, `CreateWellKnownSockets()` opens Unix domain socket at `/tmp/.X11-unix/XN`.
2. External app calls `XOpenDisplay(":N")` — standard libX11 connects to that socket.
3. TinyX's `WaitForSomething()` sees the new FD, calls `EstablishNewConnections()`.
4. `AllocNewConnection()` creates `OsCommRec`, calls `NextAvailableClient()` → `ClientPtr`.
5. `ReadRequestFromClient()` reads X11 wire-format requests from the socket.
6. `Dispatch()` calls `ProcVector[opcode](client)` — same code path as internal clients.
7. `WriteToClient()` writes replies/events back to the socket.

### 4.2 What It Tests

- TinyX protocol correctness (all opcodes, events, errors).
- Rendering: windows, pixmaps, GCs, text, images — visible on SDL display.
- Input: keyboard and mouse events delivered to external apps.
- Multi-client interaction: stock app windows managed by JWM (which is internal).
- The server core: all the code that will be in the target binary.

### 4.3 What It Does NOT Test

- The `libX11-wmb` app-side stubs (external apps use system libX11).
- The `wmb_sync_handler` direct-call path (only JWM uses that).
- The entry-table protocol and one-liner wrappers.
- Any code specific to the WMB internal-client transport.

### 4.4 How to Run

```bash
# Terminal 1: Start WMB
cd build && cmake --build . && ./wmb_tima_build :1

# Terminal 2: Run stock X11 apps against it
export DISPLAY=:1
xterm &
xclock &
xeyes &
```

---

## 5. WMB Bridge — Target-Path Validation

### 5.1 Purpose

The WMB bridge is the critical piece for **advanced debug and minimising surprises when moving to the target**. It exercises the exact code path that external apps will use on Tima OS, but over a socket instead of a shared address space:

```
                    TARGET                          HOST (WMB bridge)
                    ──────                          ─────────────────
App calls:          XMapWindow(dpy, w)              XMapWindow(dpy, w)
                         │                               │
libX11-wmb stubs:   _XGetRequest → dpy->buffer     _XGetRequest → dpy->buffer
                    SyncHandle()                    SyncHandle()
                         │                               │
Transport:          wmb_sync_handler(dpy)           wmb_client_stub_sync(dpy)
                    direct ProcVector call            serialize → socket
                    (zero copy)                           │
                                                    ┌─────▼─────────────┐
                                                    │ WMB bridge recv   │
                                                    │ deserialize       │
                                                    │ ProcVector[op]()  │
                                                    │ serialize reply   │
                                                    └───────────────────┘
```

The only difference between target and host bridge is the **transport between `SyncHandle()` and `ProcVector[]`**:
- Target: function call (the `wmb_sync_handler` already in `wmb_io.c`).
- Host bridge: serialize → socket → deserialize → same `ProcVector[]` call.

### 5.2 WMB Bridge Protocol

The protocol is intentionally minimal. It is NOT the X11 wire protocol — it is a thin envelope around the same `dpy->buffer` content that `wmb_sync_handler` would consume directly:

```c
/* wmb_bridge_protocol.h */

#define WMB_BRIDGE_MAGIC    0x574D4231  /* "WMB1" */
#define WMB_BRIDGE_VERSION  1

/* Message types */
#define WMB_MSG_CONNECT     0x01    /* Client → Server: connection setup */
#define WMB_MSG_REQUEST     0x02    /* Client → Server: X11 request */
#define WMB_MSG_REPLY       0x03    /* Server → Client: X11 reply */
#define WMB_MSG_EVENT       0x04    /* Server → Client: X11 event */
#define WMB_MSG_ERROR       0x05    /* Server → Client: X11 error */
#define WMB_MSG_DISCONNECT  0x06    /* Either direction: clean shutdown */

/* Wire format — fixed 12-byte header + variable payload */
typedef struct {
    uint32_t  length;       /* Total message size including header */
    uint8_t   msg_type;     /* WMB_MSG_* */
    uint8_t   opcode;       /* X11 opcode (for REQUEST) or event code */
    uint16_t  sequence;     /* Request sequence number */
    uint32_t  reserved;     /* Alignment padding / future use */
    /* followed by payload[] — raw X11 request/reply/event body */
} wmb_bridge_header_t;
```

The payload for `WMB_MSG_REQUEST` is the **exact content of `dpy->buffer`** — the same bytes that `wmb_sync_handler` reads directly on the target. This means:
- The request-building code in `libX11-wmb` is **identical** between target and host.
- The only new code is the serialization/deserialization of the envelope header.
- Bugs in request building will manifest identically on host bridge and target.

### 5.3 WMB Bridge — Server Side (Inside WMB)

```c
/* framework/wmb_x11/wmb_bridge.c — server side */

#ifdef WMB_ENABLE_WMB_BRIDGE

#include <sys/socket.h>
#include <sys/un.h>

/* Socket path: /tmp/.wmb-N where N is the display number */
#define WMB_BRIDGE_SOCKET_FMT  "/tmp/.wmb-%d"

static int bridge_listen_fd = -1;

/* Called from CreateWellKnownSockets or fiber init */
void wmb_bridge_listener_init(void)
{
    /* Create Unix domain socket */
    /* bind to /tmp/.wmb-N */
    /* listen() */
    /* Register FD with TinyX WaitForSomething or fiber poll */
}

/* Called when a WMB-linked app connects */
static void wmb_bridge_accept(int client_fd)
{
    /* Read WMB_MSG_CONNECT */
    /* Create internal ClientPtr via NextAvailableClient */
    /* Associate client_fd with a WMB bridge OsCommRec variant */
}

/* Called when data arrives on a bridge client FD */
static void wmb_bridge_read(int client_fd, ClientPtr client)
{
    wmb_bridge_header_t hdr;
    /* Read header */
    /* Read payload into client->requestBuffer */
    /* Call ProcVector[hdr.opcode](client) */
    /* If reply generated: wrap in WMB_MSG_REPLY, send back */
}

/* Called when TinyX generates an event for a bridge client */
void wmb_bridge_write_event(ClientPtr client, xEvent *event)
{
    /* Wrap in WMB_MSG_EVENT header */
    /* Write to client's bridge FD */
}

#endif /* WMB_ENABLE_WMB_BRIDGE */
```

### 5.4 WMB Bridge — Client Side (In External App)

The external app links against `libX11-wmb` which provides two sync-handler implementations:

```c
/* In libX11-wmb app-side stubs */

#ifdef WMB_INTERNAL_CLIENT
    /* Target path: direct function call */
    dpy->synchandler = wmb_sync_handler;     /* calls ProcVector directly */
#else
    /* Host bridge path: serialize and send over socket */
    dpy->synchandler = wmb_client_stub_sync; /* serialize → socket → WMB */
#endif
```

`wmb_client_stub_sync` implementation:

```c
/* wmb_client_stub.c — linked into external WMB apps on host */

static int wmb_bridge_fd = -1;  /* connection to WMB bridge socket */

int wmb_client_stub_sync(Display *dpy)
{
    if (wmb_bridge_fd < 0) return -1;

    /* The request is already built in dpy->buffer by _XGetRequest + Data()
     * This is IDENTICAL to what wmb_sync_handler would see on target. */
    size_t payload_len = dpy->bufptr - dpy->buffer;

    /* Wrap in WMB bridge header and send */
    wmb_bridge_header_t hdr;
    hdr.length   = sizeof(hdr) + payload_len;
    hdr.msg_type = WMB_MSG_REQUEST;
    hdr.opcode   = ((xReq *)dpy->buffer)->reqType;
    hdr.sequence = dpy->request;
    hdr.reserved = 0;

    write(wmb_bridge_fd, &hdr, sizeof(hdr));
    write(wmb_bridge_fd, dpy->buffer, payload_len);

    /* Reset buffer for next request */
    dpy->bufptr = dpy->buffer;

    return 0;
}
```

The key property: **`dpy->buffer` content is byte-for-byte identical** to what the target's `wmb_sync_handler` would receive. The only difference is how it reaches `ProcVector[]`.

### 5.5 Connection Setup

When a WMB-linked app calls `XOpenDisplay()`:

```c
/* In libX11-wmb _XConnectXCB stub (host bridge path) */

int _XConnectXCB(Display *dpy, ...)
{
#ifdef WMB_INTERNAL_CLIENT
    /* Target: register as internal client */
    wmb_state.client = wmb_register_internal_client();
    dpy->synchandler = wmb_sync_handler;
#else
    /* Host: connect to WMB bridge socket */
    char path[64];
    snprintf(path, sizeof(path), WMB_BRIDGE_SOCKET_FMT, display_number);

    wmb_bridge_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    connect(wmb_bridge_fd, (struct sockaddr *)&addr, sizeof(addr));

    /* Send connection handshake */
    wmb_bridge_header_t hdr = {
        .length   = sizeof(hdr),
        .msg_type = WMB_MSG_CONNECT,
        .opcode   = 0,
    };
    write(wmb_bridge_fd, &hdr, sizeof(hdr));

    dpy->synchandler = wmb_client_stub_sync;
#endif
    return 1;
}
```

### 5.6 What the WMB Bridge Tests

- The `libX11-wmb` app-side stubs — same `_XGetRequest`, `Data()`, `_XReply`, `SyncHandle()` code as the target.
- The request-building logic — `dpy->buffer` content is identical to target.
- The `ProcVector[]` dispatch — same server-side code.
- The reply/event delivery path — validates that replies reach the app correctly.
- The `wmb_sync_handler` contract — the bridge handler mirrors it exactly, just with a transport step.

### 5.7 What the WMB Bridge Does NOT Test

- The fiber context-switch path (only internal clients do that).
- Zero-copy performance (the socket adds copy overhead).
- The fixed-address entry table (that's M5, tested differently).

---

## 6. Internal Client Path — JWM as Fiber

JWM always runs as an **internal client** inside WMB, regardless of build profile. This is the exact target path.

### 6.1 JWM Integration

JWM's `main()` is wrapped to become a fiber entry point:

```c
/* jwm/src/main.c */
#ifndef UNIT_TEST
#ifdef WMB_INTERNAL_CLIENT
/* When compiled into WMB, jwm_main is a fiber entry function */
void jwm_main(void *arg)
{
    /* Skip argc/argv parsing — hard-coded config */
    /* Skip signal handlers — fiber runtime handles scheduling */

    StartupConnection();    /* XOpenDisplay → wmb_sync_handler path */
    do {
        Initialize();
        ParseConfig(configPath);
        Startup();
        EventLoop();        /* WaitForEvent → hal_yield_timeout inside */
        Shutdown();
        Destroy();
    } while(shouldRestart);
    ShutdownConnection();
}
#else
int main(int argc, char *argv[])
{
    /* ... existing JWM main ... */
}
#endif
#endif
```

### 6.2 JWM's EventLoop and WaitForEvent

JWM's `EventLoop()` calls `WaitForEvent()` which internally calls `XNextEvent()` / `XPending()`. On the internal-client path:

- `XPending()` checks the internal event queue (populated by TinyX when events are dispatched to this client).
- `XNextEvent()` calls `hal_task_wait()` if no events are pending — yielding the fiber to other tasks.
- When TinyX generates an event for JWM, it calls `hal_task_wake(jwm_task)` — resuming JWM's fiber.

This is the cooperative yield model described in FIBER_PORTABLE_ARCHITECTURE §5.3 and PROJECT_KNOWLEDGE_BASE §5.1.4.

### 6.3 JWM Adaptations for WMB

| JWM feature | Adaptation |
|---|---|
| `main()` / `argc/argv` | Wrapped in `#ifdef WMB_INTERNAL_CLIENT`; fiber entry `jwm_main(void *arg)` |
| Signal handlers (`SIGTERM`, `SIGINT`, `SIGHUP`) | Disabled inside WMB — fiber runtime manages scheduling |
| `exit()` / `DoExit()` | Replaced with fiber self-termination or flag-based loop exit |
| `execl()` for restart | Removed — WMB controls lifecycle |
| `waitpid()` for child processes | Removed — no `fork/exec` inside WMB |
| Config parsing | Hard-coded config path, no command-line override |

---

## 7. WMB Startup Sequence (Hosted)

```c
/* framework/wmb_x11/wmb_main.c */

#include "fiber.h"
#include "hal_task.h"

/* Forward declarations */
extern void tinyx_server_main(void *arg);
extern void wmb_test_client_main(void *arg);
extern void jwm_main(void *arg);

int main(int argc, char *argv[])
{
    /* 1. Initialize SDL display + input */
    sdl_init(screen_width, screen_height);

    /* 2. Initialize fiber runtime */
    fiber_runtime_init(DEFAULT_PREEMPTION_TIME_MS);

    /* 3. Create TinyX server task */
    hal_task_create("tinyx", tinyx_server_main, NULL, 256 * 1024);

    /* 4. Dry-test client (M2) — validates direct API path first */
    hal_task_create("test", wmb_test_client_main, NULL, 64 * 1024);

    /* 5. Create JWM internal client task (M4 onwards) */
    hal_task_create("jwm", jwm_main, NULL, 128 * 1024);

    /* 6. Main fiber becomes HAL pump — polls SDL events,
     *    drives keyboard/mouse input, advances monotonic clock */
    for (;;) {
        sdl_pump_events();      /* poll SDL → HAL input buffers */
        hal_yield_timeout(1);   /* yield for 1ms, let other fibers run */
    }
}
```

When `tinyx_server_main` runs, it calls `CreateWellKnownSockets()` which — depending on preprocessor guards — opens the X11 listener, the WMB bridge listener, both, or neither. This is the single control point for all three client categories.

---

## 8. Socket Paths and Display Naming

| Listener | Socket path | `DISPLAY` value | Who connects |
|---|---|---|---|
| X11 (TinyX native) | `/tmp/.X11-unix/XN` | `:N` | Stock X11 apps (system libX11) |
| WMB bridge | `/tmp/.wmb-N` | N/A (set via env or compile flag) | WMB-linked apps (libX11-wmb) |

The WMB bridge uses a **separate socket namespace** (`/tmp/.wmb-N`) to avoid any confusion with the X11 listener. WMB-linked apps know to connect to the bridge socket because `libX11-wmb`'s `_XConnectXCB` stub is compiled to use that path.

---

## 9. Integration with Existing Milestones

The dual-bridge model maps cleanly to the existing milestone plan (PROJECT_KNOWLEDGE_BASE §6):

| Milestone | Relevance |
|---|---|
| **M1** — Whole TinyX hosted baseline | X11 listener left enabled. Add `WMB_ENABLE_X11_LISTENER` guard. **Add Linux abstraction guards for target portability.** Server boots, SDL window works, stock X11 apps connect. |
| **M2** — Internal client path | `wmb_sync_handler` + `libX11-wmb` stubs. **Dry-test app (`wmb_test_client`) runs as internal fiber** — minimum viable validation of the direct API call path before any socket-based testing. |
| **M2.5** — WMB bridge | Implement bridge. Test with WMB-linked hello-world app from separate terminal. |
| **M3** — Strict standalone | `WMB_STANDALONE_NO_LISTEN` ≡ both guards undefined. `CreateWellKnownSockets` skips all listener code. Internal clients only. |
| **M4** — JWM integration | JWM uses `WMB_INTERNAL_CLIENT` path. `jwm_main` fiber entry. |
| **M5** — Entry-point API | WMB bridge protocol is a stepping stone toward the entry table. The bridge's `wmb_bridge_header_t` envelope maps 1:1 to entry-table dispatch. |

---

### 9.1 M1 — TinyX Linux Dependency Abstraction Guards

TinyX was written for Linux/Unix. It will need to build for Tima OS (bare-metal RISC-V) in the future. M1 must introduce **preprocessor guards** around every Linux-specific dependency inside TinyX so the same source tree compiles for both profiles.

The guard macro is `WMB_HAS_OS_TRANSPORT` — defined on hosted (Linux), undefined on target (Tima OS).

#### 9.1.1 Linux Dependencies in TinyX `os/` Layer

The following subsystems are Linux-specific and must be guarded:

| TinyX `os/` file | Linux dependency | What it does | Target replacement |
|---|---|---|---|
| `connection.c` | `<sys/socket.h>`, `<sys/un.h>`, Xtrans library | Socket creation, listen, accept, FD management | N/A — no sockets on target. Entire file guarded or stubbed. |
| `connection.c` | `<signal.h>` — `SIGUSR1`, `SIGPIPE`, `SIGHUP`, `SIGINT`, `SIGTERM` | Server lifecycle signals | N/A — fiber runtime handles lifecycle on target |
| `io.c` | `<sys/uio.h>`, Xtrans `_XSERVTransRead/Write` | Read/write to client FDs | Replaced by internal-client direct buffer access (`wmb_io.c`) |
| `WaitFor.c` | `select()` / `fd_set` / `struct timeval` | Wait for client data or timeout | Replaced by `hal_yield_timeout()` — cooperative wait |
| `WaitFor.c` | `<X11/Xpoll.h>` — `XFD_COPYSET`, `XFD_ANYSET`, FD macros | FD set manipulation for select | Eliminated — no FDs on target |
| `access.c` | `getpeername()`, `<netinet/in.h>`, peer credential checks | Client authorization by host/uid | Stubbed — all internal clients are trusted on target |
| `osinit.c` | `getpid()`, `getuid()`, file locking | Process management | Stubbed or removed |
| `xstrans.c` | Full Xtrans transport library | X transport abstraction (socket, pipe, named) | Eliminated entirely on target |
| `xdmcp.c` | XDMCP protocol over UDP | Display manager connection protocol | Eliminated on target |

#### 9.1.2 Guard Strategy

The approach is **file-level granularity with stub fallbacks**, not line-by-line `#ifdef`:

```c
/* Example: tinyx/os/connection.c */
#ifdef WMB_HAS_OS_TRANSPORT

/* === Full Linux implementation === */
/* ... existing code: CreateWellKnownSockets, EstablishNewConnections,
 *     AllocNewConnection, CloseDownConnection, etc. ... */

#else /* WMB_HAS_OS_TRANSPORT */

/* === Target stubs === */
/* No sockets, no FDs. Internal clients register directly. */

void CreateWellKnownSockets(void)
{
#ifdef WMB_ENABLE_WMB_BRIDGE
    wmb_bridge_listener_init();  /* hosted-only bridge, if enabled */
#endif
    /* No FatalError — standalone mode is normal on target */
}

void ResetWellKnownSockets(void) { /* no-op */ }
void CloseWellKnownConnections(void) { /* no-op */ }

/* EstablishNewConnections, CheckConnections, etc. — stubbed out.
 * Internal clients use wmb_register_internal_client() instead. */

#endif /* WMB_HAS_OS_TRANSPORT */
```

Same pattern for:
- `WaitFor.c` — hosted uses `select()` loop; target uses `hal_yield_timeout()` with internal-client ready-queue check.
- `io.c` — hosted uses `_XSERVTransRead/Write`; target uses direct buffer pointer from `wmb_io.c`.
- `xstrans.c` — entire file excluded from target build (`#ifdef WMB_HAS_OS_TRANSPORT`).
- `xdmcp.c` — excluded from target build.

#### 9.1.3 Build Integration

```cmake
# framework/CMakeLists.txt
if(WMB_HOSTED)
    add_definitions(-DWMB_HAS_OS_TRANSPORT)
    # Include all tinyx/os/*.c
else()
    # Exclude: xstrans.c, xdmcp.c
    # connection.c, WaitFor.c, io.c included but compile target stubs
endif()
```

#### 9.1.4 M1 Deliverables for Abstraction

1. Add `WMB_HAS_OS_TRANSPORT` guard to `connection.c`, `WaitFor.c`, `io.c` at file level.
2. Exclude `xstrans.c` and `xdmcp.c` from CMake when `WMB_HOSTED` is not set.
3. Create `tinyx/os/connection_wmb.c` — target stubs for `CreateWellKnownSockets`, etc.
4. Create `tinyx/os/waitfor_wmb.c` — target stub: check internal-client ready queue + `hal_yield_timeout`.
5. Create `tinyx/os/io_wmb.c` — target stub: `ReadRequestFromClient` reads from `wmb_state.requestBuffer`; `WriteToClient` writes to internal-client event queue.
6. All guards are upstream-safe — guarded code, not deleted code. The Linux path still compiles and works identically when `WMB_HAS_OS_TRANSPORT` is defined.

**DoD for this sub-milestone:** TinyX compiles in **both** profiles from a single source tree — `WMB_HAS_OS_TRANSPORT` (hosted, working as today) and without it (target profile, links against stubs, no Linux headers required).

---

### 9.2 M2 — Internal Dry-Test App (`wmb_test_client`)

Before testing with stock X11 apps (which require the full socket path) or WMB-linked apps (which require the bridge), we need a **minimum viable internal test** that validates the direct `wmb_sync_handler → ProcVector` call path.

This is `wmb_test_client` — a compiled-in fiber that exercises the same code path JWM will use, but with a trivial, deterministic test sequence. It runs as part of WMB (no separate process, no socket) and proves that the internal-client machinery works before any external complexity is introduced.

#### 9.2.1 What It Tests

1. `XOpenDisplay()` → internal client registration (`wmb_register_internal_client`) succeeds.
2. `XCreateSimpleWindow()` → `ProcCreateWindow` runs, returns valid Window ID.
3. `XMapWindow()` → `ProcMapWindow` runs, window becomes visible in framebuffer.
4. `XDrawString()` / `XFillRectangle()` → `ProcPolyText8` / `ProcPolyFillRectangle` render to framebuffer.
5. Event delivery: test client selects `ExposureMask`, receives `Expose` event after map.
6. `XDestroyWindow()` + `XCloseDisplay()` → clean teardown, client unregistered.

If this works, we know:
- `_XGetRequest` builds correct requests in `dpy->buffer`.
- `wmb_sync_handler` dispatches them to `ProcVector[]` correctly.
- TinyX processes requests and generates side effects (windows, rendering).
- Event delivery back to internal clients works.
- The full round-trip from Xlib API call to server execution to event return is functional.

#### 9.2.2 Implementation

```c
/* framework/wmb_x11/wmb_test_client.c */

#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
#include "hal_task.h"

/* Internal test client — runs as a fiber inside WMB.
 * This is the FIRST validation of the direct API path.
 * Must pass before any socket-based testing is attempted. */

void wmb_test_client_main(void *arg)
{
    printf("[wmb_test] Starting internal client test...\n");

    /* Step 1: Open display (internal path) */
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("[wmb_test] FAIL: XOpenDisplay returned NULL\n");
        return;
    }
    printf("[wmb_test] PASS: XOpenDisplay succeeded\n");

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    unsigned long black = BlackPixel(dpy, screen);
    unsigned long white = WhitePixel(dpy, screen);

    /* Step 2: Create window */
    Window win = XCreateSimpleWindow(dpy, root,
        50, 50, 200, 150, 1, black, white);
    if (win == 0) {
        printf("[wmb_test] FAIL: XCreateSimpleWindow returned 0\n");
        XCloseDisplay(dpy);
        return;
    }
    printf("[wmb_test] PASS: XCreateSimpleWindow → Window 0x%lx\n", win);

    /* Step 3: Select events and map */
    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XMapWindow(dpy, win);
    printf("[wmb_test] PASS: XMapWindow called\n");

    /* Step 4: Wait for Expose event */
    XEvent event;
    int got_expose = 0;
    int timeout_loops = 100;
    while (!got_expose && timeout_loops-- > 0) {
        if (XPending(dpy) > 0) {
            XNextEvent(dpy, &event);
            if (event.type == Expose) {
                got_expose = 1;
            }
        } else {
            hal_task_yield();  /* cooperative yield to server fiber */
        }
    }
    if (got_expose) {
        printf("[wmb_test] PASS: Received Expose event\n");
    } else {
        printf("[wmb_test] FAIL: No Expose event after map\n");
    }

    /* Step 5: Draw something */
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, black);
    XFillRectangle(dpy, win, gc, 10, 10, 80, 60);
    XDrawString(dpy, win, gc, 20, 100, "WMB TEST OK", 11);
    printf("[wmb_test] PASS: Draw operations completed\n");

    /* Step 6: Cleanup */
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    printf("[wmb_test] PASS: Cleanup complete — all tests passed\n");
}
```

#### 9.2.3 Integration into WMB Startup

```c
/* framework/wmb_x11/wmb_main.c */

extern void tinyx_server_main(void *arg);
extern void wmb_test_client_main(void *arg);

int main(int argc, char *argv[])
{
    sdl_init(screen_width, screen_height);
    fiber_runtime_init(DEFAULT_PREEMPTION_TIME_MS);

    /* Start TinyX server */
    hal_task_create("tinyx", tinyx_server_main, NULL, 256 * 1024);

    /* Start dry-test client — validates direct API path */
    hal_task_create("test", wmb_test_client_main, NULL, 64 * 1024);

    /* HAL pump */
    for (;;) {
        sdl_pump_events();
        hal_yield_timeout(1);
    }
}
```

Note: JWM is NOT started until `wmb_test_client` passes. The test client is the **minimum viable proof** that the internal-client path works.

#### 9.2.4 Test Progression

The testing order is strict — each level proves the one below before adding complexity:

```
Level 0: wmb_test_client (internal fiber, direct API call)
    ↓ proves wmb_sync_handler → ProcVector works
Level 1: JWM as internal fiber client
    ↓ proves full window manager operates on direct path
Level 2: Stock X11 app via TinyX socket (DISPLAY=:N xterm)
    ↓ proves TinyX socket I/O + rendering + JWM management
Level 3: WMB-linked app via WMB bridge socket
    ↓ proves libX11-wmb stubs + bridge protocol
Level 4: Both stock + WMB-linked simultaneously
    ↓ proves coexistence of all client types
```

You cannot proceed to Level N+1 until Level N passes. This eliminates debugging ambiguity — if Level 2 fails but Level 1 passes, the bug is in the socket/transport path, not in the server core.

#### 9.2.5 M2 Dry-Test DoD

- `wmb_test_client` runs as a fiber inside WMB.
- All six steps (open, create, map, event, draw, close) print PASS.
- The created window is visible in the SDL display (white rectangle with black border, filled rectangle, text).
- No crashes, no memory leaks (valgrind clean).
- The test runs without any socket listener enabled (can build with both bridges OFF).

---

### 9.3 M2.5 — WMB Bridge Implementation

Inserted between M2 and M3:

- Implement `framework/wmb_x11/wmb_bridge.c` — server-side bridge listener, accept, read, dispatch, reply/event write.
- Implement `framework/wmb_x11/wmb_client_stub.c` — client-side sync handler over socket.
- Define `wmb_bridge_protocol.h` — header format, message types.
- Create `test_apps/wmb_hello/` — minimal WMB-linked app that opens display, creates window, draws, handles events.
- **DoD:** WMB-linked hello app runs from a separate terminal, creates a visible window managed by JWM, receives expose/key/mouse events.
- **Patch budget:** 0 additional TinyX core files (bridge code is entirely additive in `framework/`).

---

## 10. Testing Plan

### 10.1 Testing a Stock X11 App

**Prerequisites:** WMB built with `WMB_ENABLE_X11_LISTENER=ON`.

```bash
# Build WMB
cd build
cmake .. -DWMB_ENABLE_X11_LISTENER=ON -DWMB_ENABLE_WMB_BRIDGE=ON
cmake --build .

# Terminal 1: Start WMB (display :1)
./wmb_tima_build :1

# Terminal 2: Run stock apps
export DISPLAY=:1

# Test 1: Basic window
xterm &
# Expected: xterm window appears in SDL display, managed by JWM
#   - Title bar, borders drawn by JWM
#   - Keyboard input works (type in xterm)
#   - Mouse click/drag works (move/resize window)

# Test 2: Simple rendering
xclock &
# Expected: clock face renders, updates

# Test 3: Multiple windows
xeyes &
xcalc &
# Expected: all windows visible, JWM taskbar shows all apps
#   - Alt+Tab or root menu switches between them
#   - Close button works
```

**What to verify:**
- Windows appear with JWM decorations (title bar, borders, buttons).
- Keyboard events are delivered correctly to the focused window.
- Mouse events: click to focus, drag to move, resize handles work.
- Expose events: windows redraw correctly after being covered/uncovered.
- JWM's taskbar, pager, root menu all functional.
- No crashes or protocol errors in WMB's console output.

### 10.2 Testing a WMB-Linked App

**Prerequisites:** WMB built with `WMB_ENABLE_WMB_BRIDGE=ON`. Test app linked against `libX11-wmb`.

```bash
# Build the WMB-linked test app
cd test_apps/wmb_hello
cmake .. && cmake --build .

# Terminal 1: Start WMB
./wmb_tima_build :1

# Terminal 2: Run WMB-linked app
export WMB_DISPLAY=1
./wmb_hello

# Expected: window appears in SDL display, same as stock X11 app
```

**Test app source (`test_apps/wmb_hello/main.c`):**

```c
#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    Display *dpy = XOpenDisplay(NULL);  /* connects via WMB bridge */
    if (!dpy) { fprintf(stderr, "Cannot open display\n"); return 1; }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    Window win = XCreateSimpleWindow(dpy, root,
        100, 100, 400, 300, 2,
        BlackPixel(dpy, screen), WhitePixel(dpy, screen));

    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask);
    XMapWindow(dpy, win);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, BlackPixel(dpy, screen));

    XEvent event;
    int running = 1;
    while (running) {
        XNextEvent(dpy, &event);
        switch (event.type) {
        case Expose:
            XDrawString(dpy, win, gc, 50, 50, "WMB Hello", 9);
            break;
        case KeyPress:
            if (event.xkey.keycode == 9) running = 0;  /* Escape */
            break;
        case ButtonPress:
            printf("Click at (%d, %d)\n", event.xbutton.x, event.xbutton.y);
            break;
        }
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
```

**What to verify:**
- `XOpenDisplay` connects via WMB bridge socket (not X11 socket).
- Window appears, managed by JWM.
- `XDrawString` renders text.
- Key and mouse events arrive.
- The `dpy->buffer` content logged on both sides (app and WMB) is **byte-for-byte identical** — confirming that the WMB bridge exercises the same request-building code as the target.

### 10.3 Testing Both Bridges Simultaneously

```bash
# Terminal 1: WMB with both bridges
./wmb_tima_build :1

# Terminal 2: Stock X11 app
DISPLAY=:1 xterm &

# Terminal 3: WMB-linked app
WMB_DISPLAY=1 ./wmb_hello &

# Expected: both windows appear in the same SDL display
#   - JWM manages both windows
#   - They can overlap, be moved, resized
#   - Input routing works correctly to whichever is focused
#   - No interference between bridge types
```

### 10.4 Testing Standalone Mode (Target Simulation)

```bash
# Build with no listeners
cmake .. -DWMB_ENABLE_X11_LISTENER=OFF -DWMB_ENABLE_WMB_BRIDGE=OFF
cmake --build .

./wmb_tima_build

# Expected: WMB starts, SDL window shows JWM desktop
#   - JWM runs as internal fiber client
#   - No external apps can connect (no socket exists)
#   - This is the target behaviour
```

---

## 11. Debugging Advantages of the WMB Bridge

The WMB bridge is specifically designed to **minimise surprises when moving to target**:

| Debug capability | How the bridge helps |
|---|---|
| Request byte inspection | Log `dpy->buffer` on client side and `requestBuffer` on server side. They must match. Any mismatch = bug in the transport, not in request building. |
| Opcode coverage | The bridge header exposes `opcode` explicitly. Easy to log which Xlib calls an app actually makes. |
| Reply validation | Bridge serializes replies back. Compare against what `_XReply` returns. Any corruption = bug in reply path. |
| Event delivery | Bridge sends events as discrete messages. Easy to log, filter, replay. |
| Sequence tracking | Bridge header carries sequence numbers. Easy to correlate requests with replies/errors. |
| Fault isolation | If a WMB-linked app works over the bridge but fails on target (fiber/direct path), the bug is in the internal-client registration or fiber scheduling — not in request building or dispatch. |
| Incremental testing | Develop the app against the bridge first (separate process, easy to restart). Once working, move it to fiber (compile into WMB). The request bytes are guaranteed identical. |

---

## 12. File Inventory

### New Files

| File | Role | Lines (est.) |
|---|---|---|
| `framework/wmb_x11/wmb_test_client.c` | Internal dry-test app — minimum viable validation of direct API path | ~80 |
| `framework/wmb_x11/wmb_bridge.c` | Server-side bridge: listener, accept, read, dispatch, reply/event write | ~300 |
| `framework/wmb_x11/wmb_bridge.h` | Bridge API (init, write_event) | ~30 |
| `framework/wmb_x11/wmb_bridge_protocol.h` | Wire format: header struct, message types, magic | ~40 |
| `framework/wmb_x11/wmb_client_stub.c` | Client-side: connect, sync handler, event read | ~200 |
| `framework/wmb_x11/wmb_client_stub.h` | Client stub API | ~20 |
| `tinyx/os/connection_wmb.c` | Target stubs for connection functions (no sockets) | ~50 |
| `tinyx/os/waitfor_wmb.c` | Target stub: internal-client ready-queue + `hal_yield_timeout` | ~80 |
| `tinyx/os/io_wmb.c` | Target stub: read from `requestBuffer`, write to internal event queue | ~120 |
| `test_apps/wmb_hello/main.c` | Minimal WMB-linked test app (external process) | ~60 |
| `test_apps/wmb_hello/CMakeLists.txt` | Build for test app (links libX11-wmb) | ~15 |

### Modified Files

| File | Change | Guard |
|---|---|---|
| `tinyx/os/connection.c` | Wrap entire socket implementation in `#ifdef WMB_HAS_OS_TRANSPORT`; add bridge init under `#ifdef WMB_ENABLE_WMB_BRIDGE` | Upstream-safe, file-level guard |
| `tinyx/os/WaitFor.c` | Wrap `select()`-based implementation in `#ifdef WMB_HAS_OS_TRANSPORT` | Upstream-safe, file-level guard |
| `tinyx/os/io.c` | Wrap Xtrans read/write in `#ifdef WMB_HAS_OS_TRANSPORT` | Upstream-safe, file-level guard |
| `framework/CMakeLists.txt` | Add `WMB_HOSTED`, `WMB_HAS_OS_TRANSPORT`, `WMB_ENABLE_X11_LISTENER`, `WMB_ENABLE_WMB_BRIDGE` options; conditional sources | N/A |
| `framework/wmb_x11/wmb_main.c` | Replace empty stub with fiber-based startup (tinyx + test_client + jwm) | N/A |

### Unchanged Files

- `framework/fiber/*` — the fiber core is untouched.
- `framework/wmb_x11/wmb_io.c` — the existing `wmb_sync_handler` and stubs are unchanged. The bridge reuses them.
- `jwm/src/main.c` — guarded with `#ifdef WMB_INTERNAL_CLIENT` (already has `#ifndef UNIT_TEST`).
- All TinyX core files other than the three `os/` files listed above.

---

## 13. Summary

| Aspect | Decision |
|---|---|
| Approach | Option C — Dual bridge (X11 + WMB), both coexisting, independently toggleable |
| X11 bridge | TinyX native socket — zero new code, stock X11 apps work unmodified |
| WMB bridge | Custom thin protocol over Unix socket — exercises `libX11-wmb` stubs and entry-table dispatch |
| Internal clients | JWM + `wmb_test_client` as fibers — direct function call, zero transport, exact target path |
| Dry test | `wmb_test_client` validates direct API before any socket testing (Level 0) |
| Linux abstraction | `WMB_HAS_OS_TRANSPORT` — file-level guards in TinyX `os/` for target portability |
| Preprocessor guards | `WMB_ENABLE_X11_LISTENER`, `WMB_ENABLE_WMB_BRIDGE` — independent, both default ON for hosted |
| Target build | All guards undefined → zero listeners, zero Linux deps, fiber-only, internal clients only |
| TinyX patches | File-level `#ifdef` in 3 `os/` files + separate `_wmb.c` stubs — upstream-safe |
| New milestone | M2.5 — WMB bridge implementation, inserted between M2 (stubs) and M3 (standalone) |
| Test progression | Level 0 (dry-test) → Level 1 (JWM) → Level 2 (stock X11) → Level 3 (WMB bridge) → Level 4 (all coexist) |
| Bridge purpose | Advanced debug + target-path validation — the transport is scaffolding, not architecture |
