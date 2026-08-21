# 02 — Baseline Architecture

## 1. Purpose

This document describes **how the stack works today**, before any WMB change. It is the grounding fact base for the transport removal
([`20-`](20-transport-inventory.md), [`21-`](21-inprocess-transport.md)) and the portability plan ([`11-`](11-portability-plan.md)): you cannot cut a seam
you cannot see.

Everything here is a statement about the code as it sits in the tree, with file and line references. Where a claim is about upstream X11 behaviour in
general rather than about this tree specifically, it is marked *(X11 general)*.

### 1.1 What this document is not

- **Not the design.** What we build is [`03-target-architecture.md`](03-target-architecture.md).
- **Not the file inventory.** Which files are in or out is [`01-component-inventory.md`](01-component-inventory.md).
- **Not a tutorial on X11.** It covers only the mechanisms WMB touches.

## 2. The Four Trees and How They Currently Relate

Today the trees do **not** relate at all in this repository — they are four independent checkouts plus a framework directory that compiles none of them.
The relationships described below are how the code *is written to* work, i.e. how it works when built by its own autotools/Makefile build.

```
   ┌────────────────┐         X11 wire protocol          ┌──────────────────┐
   │  application   │ ─────── over AF_UNIX socket ─────► │  TinyX X server  │
   │  (e.g. JWM)    │ ◄────── events / replies  ──────── │                  │
   └───────┬────────┘                                    └────────┬─────────┘
           │ links                                                │ links
   ┌───────▼────────┐                                    ┌────────▼─────────┐
   │    libX11      │                                    │  dix / os / mi   │
   │  + libxcb      │                                    │  fb / kdrive     │
   └────────────────┘                                    └────────┬─────────┘
                                                                  │
                                                         ┌────────▼─────────┐
                                                         │ Linux fbdev /    │
                                                         │ evdev / VESA     │
                                                         └──────────────────┘
```

WMB collapses the horizontal arrow (§3–§5) and replaces the bottom box (§6).

## 3. The Client Side: How Xlib Issues a Request

*(X11 general, verified against `libX11/src/`)*

Every one of the ~245 request‑building files in `libX11/src/` follows one of two shapes.

### 3.1 Fire‑and‑forget (no reply)

```c
int XMapWindow(Display *dpy, Window w)
{
    register xResourceReq *req;
    LockDisplay(dpy);
    GetReq(MapWindow, req);        /* → _XGetRequest(dpy, X_MapWindow, SIZEOF(xResourceReq)) */
    req->id = w;
    UnlockDisplay(dpy);
    SyncHandle();                  /* → if (dpy->synchandler) (*dpy->synchandler)(dpy) */
    return 1;
}
```

Four macros carry the whole protocol:

| Macro | Defined in | Expands to | What it does |
|---|---|---|---|
| `GetReq(name, req)` | `X11/Xlibint.h` | `req = (x##name##Req *) _XGetRequest(dpy, X_##name, SIZEOF(x##name##Req))` | Reserve request bytes |
| `Data(dpy, data, len)` | `X11/Xlibint.h` | inline append, or `_XSend(dpy, data, len)` on overflow | Append variable payload |
| `UnlockDisplay(dpy)` | `X11/Xlibint.h` | thread lock release | No‑op single‑threaded |
| `SyncHandle()` | `X11/Xlibint.h` | `if (dpy->synchandler) (*dpy->synchandler)(dpy)` | **The interception point** |

`dpy->synchandler` is normally NULL and only set by `XSynchronize()`. That it exists, is per‑`Display`, and is invoked after *every* request is the single
fact that makes the entire WMB transport strategy possible without touching 245 files.

### 3.2 Round‑trip (reply expected)

```c
Status XGetWindowAttributes(Display *dpy, Window w, XWindowAttributes *attr)
{
    xGetWindowAttributesReply rep;
    LockDisplay(dpy);
    GetReq(GetWindowAttributes, req);
    req->id = w;
    if (!_XReply(dpy, (xReply *)&rep, 3, xTrue)) { UnlockDisplay(dpy); SyncHandle(); return 0; }
    /* ... unpack rep ... */
    UnlockDisplay(dpy);
    SyncHandle();                  /* now a no-op: _XReply already flushed */
    return 1;
}
```

`_XReply()` is where the client blocks on the socket.

### 3.3 The transport floor — `libX11/src/xcb_io.c`

All socket contact is confined to one file, 24,950 bytes, defining thirteen non‑static functions:

| Symbol | Line | Role |
|---|---:|---|
| `_XEventsQueued` | 423 | How many events are buffered; pulls from XCB if asked |
| `_XReadEvents` | 454 | Block until at least one event arrives |
| `_XSend` | 540 | Push `dpy->buffer` bytes into XCB's output queue |
| `_XFlush` | 604 | Force the queue out to the socket |
| `_XIDHandler` | 616 | XID range exhaustion callback |
| `_XAllocID` | 623 | Allocate one XID |
| `_XAllocIDs` | 633 | Allocate a batch |
| `_XReply` | 664 | Send pending, then block for the matching reply or error |
| `_XRead` | 826 | Read `n` bytes of reply payload |
| `_XReadPad` | 846 | Read with 4‑byte padding |
| `_XEatData` | 854 | Discard `n` bytes |
| `_XEatDataWords` | 866 | Discard `n` 32‑bit words |
| `_XNextRequest` | 877 | Next sequence number |

Beneath it, `libX11/src/xcb_disp.c` owns `xcb_connect()` / `xcb_get_setup()`, and `libX11/src/OpenDis.c` drives connection establishment: connect, read the
`xConnSetup` blob, populate `Display`, build `Screen`/`Visual`/`Depth` arrays.

**This is a thirteen‑symbol surface.** That is the entire size of the client‑side transport problem.

## 4. The Server Side: How TinyX Consumes a Request

### 4.1 The dispatch loop — `tinyx/dix/dispatch.c`

```
Dispatch()
  for (;;)
      nready = WaitForSomething(clientReady);       /* dispatch.c:377 */
      for each ready client:
          ReadRequestFromClient(client)             /* os/io.c:180  */
          while (client has a complete request):
              result = (*client->requestVector[MAJOROP])(client)
```

`client->requestVector` normally points at `ProcVector[]` in `tinyx/dix/tables.c` — a 256‑entry table of `int (*)(ClientPtr)`. Three facts matter:

1. The dispatcher's contract with a request handler is *entirely* `client->requestBuffer` + `client->req_len`. It does not care where those bytes came
   from.
2. `WaitForSomething()` is the only place the server blocks.
3. `ReadRequestFromClient()` is the only place bytes enter the server.

Points 1–3 are why the in‑process path is a *substitution*, not a rewrite: put bytes in `requestBuffer`, call `ProcVector[op]`, done.

### 4.2 `WaitForSomething()` — `tinyx/os/WaitFor.c`

670 lines, one job: block until a client has input or a timer expires.

```
WaitForSomething(int *pClientsReady)              /* WaitFor.c:130 */
    ...
    XFD_COPYSET(&AllSockets, &LastSelectMask);    /* L175, L201 */
    BlockHandler(&wt, &LastSelectMask);           /* L206 — KDrive's pollEvents hangs off here */
    i = Select(MaxClients, &LastSelectMask, ...);  /* L215 / L219 */
    selecterr = GetErrno();                        /* L221 */
    WakeupHandler(i, &LastSelectMask);
    → fills pClientsReady with clients that have data
```

Two mechanisms inside it survive into WMB and two do not:

| Mechanism | Fate |
|---|---|
| `BlockHandler` / `WakeupHandler` registration lists | **Survives.** This is how KDrive's `pollEvents` gets called, and how `mieq` drains |
| Timer list (`TimerForce`, `SmartScheduleTime`) | **Survives.** Screen saver and XKB repeat depend on it |
| `fd_set` / `Select()` / `AllSockets` / `ClientsWithInput` | **Removed.** No file descriptors exist |
| `pClientsReady` population from readable fds | **Removed.** Internal clients signal readiness directly |

### 4.3 `tinyx/os/connection.c`

1,027 lines. Thirteen external entry points:

| Symbol | Line | Role in WMB |
|---|---:|---|
| `CreateWellKnownSockets` | 214 | Opens `/tmp/.X11-unix/XN`. **Becomes a no‑op** |
| `ResetWellKnownSockets` | 298 | **No‑op** |
| `ClientAuthorized` | 461 | Auth + `getpeername`. **Always returns authorized** |
| `AllocNewConnection` | 535 | Builds `OsCommRec` + `ClientRec` from an accepted fd. **Becomes `wmb_register_internal_client()`** |
| `EstablishNewConnections` | 587 | `accept()` loop. **Removed** |
| `ErrorConnMax` | 662 | Connection‑limit refusal. **Removed** |
| `CloseDownFileDescriptor` | 712 | fd teardown. **Simplified** |
| `CheckConnections` | 747 | Probes for dead fds. **No‑op** |
| `CloseDownConnection` | 813 | Client teardown. **Retained, fd parts removed** |
| `OnlyListenToOneClient` | 872 | Grab support. **Retained as a flag** |
| `ListenToAllClients` | 904 | Grab release. **Retained as a flag** |
| `IgnoreClient` | 922 | Flow control. **Retained as a flag** |
| `AttendClient` | 957 | Flow control. **Retained as a flag** |

Note that the grab/flow‑control quartet (`OnlyListenToOneClient`, `ListenToAllClients`, `IgnoreClient`, `AttendClient`) is *semantically meaningful* in WMB
— server grabs still need to stop other clients being serviced — but is currently implemented by manipulating `fd_set`s. It must be re‑expressed against
the internal client list.

### 4.4 `tinyx/os/io.c`

1,026 lines. Seven external entry points:

| Symbol | Line | Role in WMB |
|---|---:|---|
| `ReadRequestFromClient` | 180 | Reads from the fd into `oc->input->buffer`, handles partial requests and byte swapping. **Becomes: point at `dpy->buffer`, return length** |
| `InsertFakeRequest` | 457 | Used by XKB / `sleepuntil`. **Retained** |
| `ResetCurrentRequest` | 531 | Re‑dispatch support. **Retained** |
| `FlushAllOutput` | 592 | Push all pending output. **No‑op** |
| `SetCriticalOutputPending` | 647 | Scheduling hint. **No‑op** |
| `WriteToClient` | 664 | **The one that matters.** Every reply, event and error reaches the client through here |
| `FlushClient` | 804 | `writev()` to the socket. **No‑op** |

`WriteToClient(ClientPtr who, int count, const char *buf)` is called by `WriteEventsToClient`, by every `Proc*` that produces a reply, and by
`SendErrorToClient`. Intercepting this single function captures **the entire server→client direction**.

### 4.5 `tinyx/os/xstrans.c` and `X11/xtrans`

`xstrans.c` is 30 lines that `#include` the `X11/xtrans/*.c` transport implementation with the right macros. It is the abstraction layer over
UNIX/TCP/local transports. Excluded wholesale.

## 5. The Transport Sandwich, End to End

Putting §3 and §4 together — the current path for `XMapWindow` from an external client:

```
 XMapWindow()                                   libX11/src/MapWindow.c
   GetReq → _XGetRequest                        libX11/src/xcb_io.c:?  (allocates in xcb buffer)
   SyncHandle → (usually NULL)
   ... later, at flush point ...
   _XSend / _XFlush                             libX11/src/xcb_io.c:540/604
     xcb_writev                                 libxcb
       writev(fd, ...)                          kernel
─────────────────────────────── AF_UNIX socket ───────────────────────────────
       select() returns readable                tinyx/os/WaitFor.c:215
   WaitForSomething → pClientsReady             tinyx/os/WaitFor.c:130
   ReadRequestFromClient                        tinyx/os/io.c:180
     _XSERVTransRead → read(fd, ...)            X11/xtrans
     byte swap if needed
   Dispatch loop                                tinyx/dix/dispatch.c:377
     ProcVector[X_MapWindow](client)            tinyx/dix/tables.c
       ProcMapWindow                            tinyx/dix/dispatch.c
         MapWindow                              tinyx/dix/window.c
           → damage, → shadow, → framebuffer
```

Roughly **14 layers and two address‑space crossings** to set a bit in a window structure. In WMB this becomes three layers and zero crossings.

## 6. The Bottom: KDrive and Hardware

### 6.1 The four vtables — `tinyx/kdrive/src/kdrive.h`

KDrive is X.Org's "miniature X server" framework. It abstracts the hardware behind four function tables that a *backend* (a DDX) fills in.

| Vtable | Members | Filled by |
|---|---|---|
| `KdCardFuncs` | `cardinit`, `scrinit`, `initScreen`, `finishInitScreen`, `createRes`, `preserve`, `enable`, `dpms`, `disable`, `restore`, `scrfini`, `cardfini`, `getColors`, `putColors` | `tima_kdrive_fb.c` |
| `KdKeyboardFuncs` | `Load`, `Init`, `Leds`, `Bell`, `Fini`, `LockLed` | `tima_kdrive_kbd.c` |
| `KdMouseFuncs` | `Init`, `Fini` | `tima_kdrive_mouse.c` |
| `KdOsFuncs` | `Init`, `Enable`, `SpecialKey`, `Disable`, `Fini`, `pollEvents` | `tima_kdrive_system.c` |

And five DDX entry points the server calls directly:

| Symbol | Called from | Our implementation |
|---|---|---|
| `InitCard(char *name)` | `dix/main.c` | `KdCardInfoAdd(&tima_Funcs, NULL)` |
| `InitOutput(ScreenInfo*, argc, argv)` | `dix/main.c` | `KdInitOutput(...)` |
| `InitInput(argc, argv)` | `dix/main.c` | `KdInitInput(&tima_MouseFuncs, &tima_KeyboardFuncs)` |
| `OsVendorInit(void)` | `os/utils.c` | `KdOsInit(&tima_OsFuncs)` |
| `ddxUseMsg/ddxProcessArgument/ddxGiveUp/AbortDDX` | various | stubs |

All of the above already exist in `tinyx/kdrive/tima/tima_kdrive_system.c`.

### 6.2 How input reaches a client today

```
  hardware IRQ / SDL event
    → mouse_driver_read_data() / keypad_driver_read_data()      linux_hosted/sdl_drivers/
      → tima_MousePoll() / tima_KeyboardPoll()                  tinyx/kdrive/tima/
        → KdEnqueueMouseEvent() / KdEnqueueKeyboardEvent()      tinyx/kdrive/src/kinput.c
          → mieqEnqueue()                                       tinyx/mi/mieq.c
            ... WakeupHandler → mieqProcessInputEvents() ...
              → DeliverEvents()                                 tinyx/dix/events.c
                → WriteEventsToClient()
                  → WriteToClient()                             tinyx/os/io.c:664
                    → FlushClient() → writev(fd)
```

`tima_OsPollEvents()` — the `KdOsFuncs.pollEvents` member — is what drives the top of that chain, and it is invoked from KDrive's `BlockHandler`, which
`WaitForSomething()` calls at `WaitFor.c:206`. **Removing `select()` therefore also removes the thing that currently drives input polling**, which is why
`hal_yield_timeout` must preserve `BlockHandler`/`WakeupHandler` semantics. This is the subtlest coupling in the whole project.

### 6.3 The HAL as it exists — `linux_hosted/sdl_drivers/hal_drivers.h`

18 functions and a byte‑stream protocol.

```c
void     keypad_driver_init(void);
bool_t   keypad_driver_has_data(void);
uint8_t  keypad_driver_read_data(void);

void     lcd_driver_init(void);        void lcd_driver_deinit(void);
uint16_t lcd_driver_get_width(void);   uint16_t lcd_driver_get_height(void);
uint32_t lcd_driver_get_bits_per_pixel(void);
PIXEL_TYPE_T *lcd_driver_get_framebuffer(void);
void     lcd_driver_clear_screen(void);      void lcd_driver_update_window(void);
void     lcd_driver_set_pixel(...);          void lcd_driver_fill_colour(...);
void     lcd_driver_show_bitmap_rgb(...);    void lcd_driver_do_scroll(uint16_t);

void     mouse_driver_init(uint8_t index);
bool_t   mouse_driver_has_data(uint8_t index);
uint8_t  mouse_driver_read_data(uint8_t index);
void     mouse_driver_process(void);
void     mouse_driver_write_command(uint8_t index, uint8_t *buffer, uint32_t max_size);
```

Input is a **byte stream of command/data pairs**, not structured events:

| Command | Value | Payload |
|---|---|---|
| `CMD_MODIFIER` | 0x01 | new modifier mask |
| `CMD_KEY_DOWN` | 0x02 | key data (ASCII‑ish; `KEY_DATA_*` for specials) |
| `CMD_KEY_UP` | 0x03 | key data |
| `CMD_MOUSE_X` | 0x04 | signed 8‑bit dx |
| `CMD_MOUSE_Y` | 0x05 | signed 8‑bit dy |
| `CMD_MOUSE_Z` | 0x06 | z / touch pressure |
| `CMD_MOUSE_BUTTON` | 0x07 | `MOUSE_BUTTON_LEFT`…`X2` |
| `CMD_MOUSE_WHEEL_X` | 0x08 | signed 8‑bit |
| `CMD_MOUSE_WHEEL_Y` | 0x09 | signed 8‑bit |

This protocol is a **target artefact** — it is what real hardware will emit — and is therefore preserved verbatim in the new contract
([`12-`](12-platform-contract.md) §5), even though the function names around it change.

The SDL side (`linux_hosted/sdl_library/`) synthesises this byte stream from SDL events: `sdl_input.c` (261 LOC) for keys, `sdl_mouse.c` (108 LOC) for the
pointer, `sdl_window.c` (336 LOC) for the window/texture/present path.

## 7. Threading and Blocking Model Today

| Component | Model |
|---|---|
| TinyX | Single‑threaded, blocks in `select()` inside `WaitForSomething()` |
| libX11 | Single‑threaded unless `XInitThreads()`; blocks in `poll()`/`recvmsg()` inside `_XReply` / `_XReadEvents` |
| JWM | Single‑threaded, blocks in `select()` on `ConnectionNumber(display)` inside `jwm/src/event.c` |
| `framework/fiber/` | Cooperative; the preemption tick is disarmed ([`90-`](90-decisions.md) D‑47). Hosted port shipped and exercised by the `linux_hosted/` smoke programs, including a proof of concept of the in‑process client–server handshake ([`21-`](21-inprocess-transport.md) §11); **not yet connected to TinyX, libX11 or JWM** |
| SDL | Owns its own event thread internally |

Three independent blocking points, all of which must become the same cooperative yield. That is what makes the fiber runtime load‑bearing rather than
decorative: without it, collapsing three processes into one produces immediate deadlock — JWM blocks waiting for the server, the server blocks waiting for
JWM.

## 8. Where the Seams Are

Summary of the whole document. Six seams; cutting all six is the project.

| # | Seam | Location | Cut by |
|---|---|---|---|
| S1 | **Client transport** | `dpy->synchandler` + 13 symbols in `xcb_io.c` | [`21-`](21-inprocess-transport.md) §3 |
| S2 | **Server ingress** | `ReadRequestFromClient` (`io.c:180`) | [`21-`](21-inprocess-transport.md) §5 |
| S3 | **Server egress** | `WriteToClient` (`io.c:664`) | [`21-`](21-inprocess-transport.md) §6 |
| S4 | **Blocking / scheduling** | `WaitForSomething` (`WaitFor.c:130`), preserving `BlockHandler`/`WakeupHandler` | [`21-`](21-inprocess-transport.md) §7 |
| S5 | **Client lifecycle** | `AllocNewConnection` (`connection.c:535`) | [`21-`](21-inprocess-transport.md) §5.2 |
| S6 | **Hardware** | `KdCardFuncs`/`KdKeyboardFuncs`/`KdMouseFuncs`/`KdOsFuncs` | [`12-`](12-platform-contract.md) §4–§5 |

S6 is already cut in principle — KDrive is designed for exactly this. S1–S5 are the work.

## 9. Facts Worth Remembering

Five observations from this analysis that shape every downstream decision:

1. **The client transport is thirteen functions in one file.** Not a distributed refactor.
2. **The server egress is one function.** `WriteToClient`.
3. **The dispatcher does not know what a socket is.** It only knows `client->requestBuffer`.
4. **`select()` removal breaks input polling**, because `pollEvents` hangs off `BlockHandler` which is called from `WaitForSomething`. Preserve the handler
   lists or lose the mouse.
5. **The Linux coupling in `tinyx/` is eleven files**, of which five are in directories we exclude wholesale. The server core — `dix`, `mi`, `fb`,
   `render`, `randr`, `xfixes`, `damageext`, `miext` — is already portable. See [`10-os-dependency-census.md`](10-os-dependency-census.md).
