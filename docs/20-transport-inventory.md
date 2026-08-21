# 20 — Transport Inventory

## 1. Purpose

This document enumerates **everything that constitutes the X11 client/server transport** in this tree: every symbol, every structure field, every semantic
assumption, on both sides of the socket. It is the first deliverable of workstream 3 ([`00-introduction.md`](00-introduction.md) §5.3) and it is the
complete work item list that [`21-inprocess-transport.md`](21-inprocess-transport.md) must satisfy.

The rule it exists to enforce: **you may not claim the transport is removed until every row in this document has a disposition.** Transport removal fails
when something subtle — XID allocation, sequence numbers, the `Data()` overflow path, request byte swapping — is discovered three months in.

### 1.1 What this document is not

- **Not the design.** The replacement is [`21-inprocess-transport.md`](21-inprocess-transport.md).
- **Not the external ABI.** [`22-entry-point-abi.md`](22-entry-point-abi.md).
- **Not the general OS census.** Non‑transport OS dependencies are [`10-os-dependency-census.md`](10-os-dependency-census.md).

### 1.2 Disposition vocabulary

| Tag | Meaning |
|---|---|
| **REPLACE** | A WMB implementation with the same signature and the same contract |
| **NOP** | Retained as an empty function; call sites remain, behaviour is vacuous |
| **DELETE** | Neither the function nor its call sites survive |
| **KEEP** | Not actually transport; survives unchanged |
| **REDEFINE** | The concept survives but is expressed against a different substrate |

## 2. The Transport in One Picture

```
   CLIENT SIDE                            │            SERVER SIDE
                                          │
  Xlib API file  (245 files, UNCHANGED)   │   dix/dispatch.c   Dispatch()
        │ GetReq → _XGetRequest           │         │ WaitForSomething()  ← os/WaitFor.c
        │ Data()                          │         │ ReadRequestFromClient() ← os/io.c
        │ SyncHandle → dpy->synchandler   │         │ ProcVector[op](client)
        ▼                                 │         ▼
  ┌───────────────────┐                   │   ┌──────────────────┐
  │  xcb_io.c         │  13 symbols       │   │ os/io.c          │  7 symbols
  │  xcb_disp.c       │                   │   │ os/connection.c  │ 13 symbols
  │  OpenDis.c        │                   │   │ os/WaitFor.c     │  1 symbol
  └─────────┬─────────┘                   │   │ os/xstrans.c     │  (xtrans)
            │ libxcb                      │   └────────┬─────────┘
            ▼                             │            ▼
        writev(fd) ─────────── AF_UNIX socket ─────── read(fd)
```

Four numbers define the problem: **13 client symbols, 21 server symbols, 1 header macro, 6 semantic assumptions.**

## 3. Client Side — `libX11`

The mechanism mirrors the server side, and for the same reason ([`90-`](90-decisions.md) D‑49): **one transport per build, selected by the source list.**
`xcb_io.c` and `x11_xcb.c` are not compiled, `xcb_disp.c` is reduced to a stub, and `framework/wmb_x11/wmb_transport.c` defines the *same* `_X*` symbols
the 245 API files already call — `_XGetRequest`, `_XReply`, `_XRead`, `_XFlush` and the rest. **Nothing is renamed to `wmb_*`**; that prefix belongs only to
internals the API files never see (`wmb_sync_handler`, `wmb_dispatch`, `WmbDisplay`). The `xcb_*` names disappear because their files are gone, which is
exactly what obligation T2 (§8) checks.

One asymmetry with §4 is deliberate: the server‑side replacements live beside their originals in `tinyx/os/`, but the client‑side replacement lives
**outside** `libX11/`, in `framework/wmb_x11/`. The upstream tree therefore gains no WMB‑owned files, and the only `libX11/` edits are the two guarded ones
in [`11-`](11-portability-plan.md) §8.2.

### 3.1 `libX11/src/xcb_io.c` — the thirteen

| # | Symbol | Line | What it does today | Disposition |
|--:|---|---:|---|---|
| 1 | `_XGetRequest(Display*, CARD8, size_t)` | — | Reserve request bytes in XCB's buffer; assign a sequence number | **REPLACE** — reserve in `dpy->buffer` |
| 2 | `_XSend(Display*, const char*, long)` | 540 | Push `dpy->buffer` into XCB's output queue | **NOP** — but see §3.3, the `Data()` interaction |
| 3 | `_XFlush(Display*)` | 604 | Force the queue to the socket | **REPLACE** — dispatch any pending request |
| 4 | `_XReply(Display*, xReply*, int, Bool)` | 664 | Flush, then block for the matching reply or error | **REPLACE** — dispatch, then wait for the server fiber to deposit the reply |
| 5 | `_XRead(Display*, char*, long)` | 826 | Read `n` bytes of reply payload | **REPLACE** — copy from the reply slot |
| 6 | `_XReadPad(Display*, char*, long)` | 846 | `_XRead` + skip to a 4‑byte boundary | **REPLACE** |
| 7 | `_XEatData(Display*, unsigned long)` | 854 | Discard `n` bytes | **REPLACE** |
| 8 | `_XEatDataWords(Display*, unsigned long)` | 866 | Discard `n` words | **REPLACE** |
| 9 | `_XReadEvents(Display*)` | 454 | Block until ≥1 event is available | **REPLACE** — `fiber_wait_for` on "event queue non‑empty"; the one `FIBER_WAIT_FOREVER` in the transport ([`21-`](21-inprocess-transport.md) §3.4) |
| 10 | `_XEventsQueued(Display*, int mode)` | 423 | Count queued events; optionally pull more | **REPLACE** — count the local queue; on `QueuedAfterFlush`/`QueuedAfterReading`, dispatch pending and pump |
| 11 | `_XNextRequest(Display*)` | 877 | Next sequence number | **REPLACE** — trivial counter |
| 12 | `_XAllocID(Display*)` | 623 | Allocate one XID from the server‑granted range | **REPLACE** — see §3.4 |
| 13 | `_XAllocIDs(Display*, XID*, int)` | 633 | Allocate a batch | **REPLACE** |
| — | `_XIDHandler(Display*)` | 616 | XID exhaustion callback | **REPLACE** — XC‑MISC top‑up |

### 3.2 `libX11/src/xcb_disp.c`, `x11_xcb.c`, `OpenDis.c`, `ClDisplay.c`

| Symbol | File | What it does | Disposition |
|---|---|---|---|
| `_XConnectXCB` | `xcb_disp.c` | Establish the XCB connection; obtain the setup blob | **REPLACE** — register an internal client, synthesise the setup |
| `xcb_get_setup` | `xcb_disp.c` (libxcb) | Return the `xConnSetup` blob | **REPLACE** — the blob is produced by the *server*, through the normal request/reply path, and read out of the reply slot ([`21-`](21-inprocess-transport.md) §3.3). Not baked at build time |
| `_XFreeDisplayStructure` | `OpenDis.c` | Teardown | **KEEP** |
| `XOpenDisplay` | `OpenDis.c` | Parse `DISPLAY`, connect, read setup, build `Screen`/`Visual`/`Depth`/`Colormap` arrays, init locale, install extensions | **REPLACE** — everything except the parsing and the connection is reusable; see [`21-`](21-inprocess-transport.md) §3.2 |
| `XCloseDisplay` | `ClDisplay.c` | Flush, close fd, free | **REPLACE** |
| `XGetXCBConnection` | `x11_xcb.c` | XCB interop accessor | **DELETE** |
| `XInitThreads`, `_XCreateMutex_fn` … | `locking.c` | Thread locking | **NOP** |

### 3.3 `X11/Xlibint.h` — the macros

| Macro | Behaviour today | Disposition |
|---|---|---|
| `GetReq` / `GetReqExtra` / `GetResReq` / `GetEmptyReq` | `req = (xReq*)_XGetRequest(...)` | **KEEP** — they route through `_XGetRequest`, which we replace |
| `Data(dpy, data, len)` | Append inline if it fits; otherwise `_XSend(dpy, data, len)` | **REDEFINE** — on overflow, grow `dpy->buffer` via `_XBufferOverflow(dpy, n)` instead of flushing. **This is the one required upstream header edit** |
| `Data16` / `Data32` | Byte‑swapping variants of `Data` | **KEEP** — they call `Data` |
| `SyncHandle()` | `if (dpy->synchandler) (*dpy->synchandler)(dpy)` | **KEEP** — the interception point. Unchanged |
| `LockDisplay` / `UnlockDisplay` | Thread lock | **NOP** |
| `FlushGC(dpy, gc)` | Flush deferred GC changes | **KEEP** — routes through normal request building |

Why `Data()` matters so much: **17 upstream files call `Data()` with payloads that can exceed the buffer**, and the default behaviour is to *flush the
buffer to the socket mid‑request*. In the in‑process model there is nowhere to flush to; the request must remain contiguous in `dpy->buffer` until
`SyncHandle()`. Changing the overflow branch from "flush" to "grow" is what makes 245 API files work with zero edits.

### 3.4 XID allocation

Normally: the server grants a client an XID base and mask in the connection setup; `_XAllocID` walks the range; when exhausted, XC‑MISC
(`XCMiscGetXIDRange`) requests more.

In WMB the client and server share an address space, so the range is still meaningful (it namespaces resources per client) but the exhaustion path is
trivial. Disposition: **REPLACE** with the same algorithm, sourcing base/mask from the synthesised setup blob, and keeping XC‑MISC as the top‑up mechanism
— because `tinyx/Xext/xcmisc.c` is already compiled and it is less work to keep the protocol than to special‑case it.

### 3.5 `Display` structure fields with transport meaning

| Field | Meaning | Disposition |
|---|---|---|
| `dpy->fd` | Socket file descriptor; exposed publicly as `ConnectionNumber(dpy)` | **REDEFINE** — set to `-1`. Every consumer in our tree is removed (JWM's `select`, `xcb_io.c`) |
| `dpy->buffer`, `bufptr`, `bufmax` | The request assembly buffer | **KEEP** — now the *only* request storage |
| `dpy->synchandler` | Optional post‑request hook | **KEEP** — becomes mandatory and non‑NULL |
| `dpy->request`, `last_request_read` | Sequence numbers | **KEEP** — maintained by the replacement |
| `dpy->head`, `tail`, `qlen` | Event queue | **KEEP** — filled by `WriteToClient` instead of by the socket reader |
| `dpy->xcb` (`_X11XCBPrivate`) | XCB state | **REDEFINE** — the slot carries a `WmbDisplay *`, so the `Display` layout is untouched. See [`21-`](21-inprocess-transport.md) §3.1 |
| `dpy->resource_id`, `resource_base`, `resource_mask`, `resource_shift` | XID allocation state | **KEEP** |
| `dpy->flags` (`XlibDisplayIOError`, `XlibDisplayWriting`, …) | I/O state bits | **KEEP** — only the ones we set |
| `dpy->lock` | Thread lock | **NOP** |

## 4. Server Side — `tinyx/os/`

### 4.1 `tinyx/os/io.c` — seven symbols

| # | Symbol | Line | What it does | Disposition |
|--:|---|---:|---|---|
| 1 | `ReadRequestFromClient(ClientPtr)` | 180 | Read from `oc->fd` into `oc->input->buffer`; handle partial requests, `BIG-REQUESTS`, byte swapping; set `client->requestBuffer` and `client->req_len` | **REPLACE** — point `requestBuffer` at the client's already‑complete buffer, set `req_len`, return the length |
| 2 | `WriteToClient(ClientPtr, int, const char*)` | 664 | Append to `oc->output`; flush when full. **Every reply, event and error passes through here** | **REPLACE** — append into the target client's reply slot, or enqueue on its event queue. **No wake**: release is a predicate over the completed slot ([`21-`](21-inprocess-transport.md) §5.5) |
| 3 | `FlushClient(ClientPtr, OsCommPtr, const char*, int)` | 804 | `writev()` to the socket | **NOP** |
| 4 | `FlushAllOutput(void)` | 592 | Flush every client with pending output | **NOP** |
| 5 | `SetCriticalOutputPending(void)` | 647 | Scheduling hint for the next flush | **NOP** |
| 6 | `InsertFakeRequest(ClientPtr, char*, int)` | 457 | Push a synthetic request in front of the real stream. Used by XKB and `Xext/sleepuntil.c` | **REPLACE** — same semantics against the in‑process buffer. **Must not be forgotten** |
| 7 | `ResetCurrentRequest(ClientPtr)` | 531 | Re‑dispatch the current request later. Used when a request must block (e.g. `sleepuntil`) | **REPLACE** — same semantics |

Items 6 and 7 are the ones most likely to be overlooked. They exist because a few requests need to suspend and resume, and they manipulate the *input
buffer position*, a concept that must survive the substitution.

### 4.2 `tinyx/os/connection.c` — thirteen symbols

| # | Symbol | Line | Disposition | Note |
|--:|---|---:|---|---|
| 1 | `CreateWellKnownSockets` | 214 | **NOP** | The file is not compiled in any profile; `connection_wmb.c` supplies an empty one. Its `FatalError`‑if‑it‑cannot‑listen path therefore cannot fire |
| 2 | `ResetWellKnownSockets` | 298 | **NOP** | |
| 3 | `ClientAuthorized` | 461 | **REPLACE** | Always returns authorized; no `getpeername` |
| 4 | `AllocNewConnection` | 535 | **REPLACE** | Becomes `wmb_register_internal_client()` — allocate `ClientRec`, no fd |
| 5 | `EstablishNewConnections` | 587 | **DELETE** | |
| 6 | `ErrorConnMax` | 662 | **DELETE** | |
| 7 | `CloseDownFileDescriptor` | 712 | **REPLACE** | Frees `OsCommRec`, no fd close |
| 8 | `CheckConnections` | 747 | **NOP** | Probes for dead fds |
| 9 | `CloseDownConnection` | 813 | **REPLACE** | Client teardown; wake/destroy the owning fiber |
| 10 | `OnlyListenToOneClient` | 872 | **REDEFINE** | Server grab: mark all other internal clients non‑dispatchable |
| 11 | `ListenToAllClients` | 904 | **REDEFINE** | Release grab |
| 12 | `IgnoreClient` | 922 | **REDEFINE** | Flow control: mark one client non‑dispatchable |
| 13 | `AttendClient` | 957 | **REDEFINE** | Re‑enable |

Symbols 10–13 are **not optional**. `XGrabServer` is used by JWM during reparenting, and if `OnlyListenToOneClient` is a no‑op, JWM's window decoration
will race against clients. Today they manipulate `fd_set`s; in WMB they must manipulate a per‑client dispatchable flag.

### 4.3 `tinyx/os/WaitFor.c` — one symbol, six mechanisms

| Mechanism | Lines | Disposition |
|---|---|---|
| `WaitForSomething(int *pClientsReady)` | 130 | **REPLACE** |
| `Select()` / `fd_set` / `XFD_COPYSET` / `XFD_UNSET` | 175, 176, 201, 215, 219 | **DELETE** |
| `BlockHandler(&wt, &LastSelectMask)` | 206 | **KEEP** — critical, see [`11-`](11-portability-plan.md) §7.1 |
| `WakeupHandler(i, &LastSelectMask)` | after `Select` | **KEEP** — critical |
| Timer list: `TimerForce`, `CheckAllTimers`, `NextDPMSTimeout` | throughout | **KEEP** — but they live *in this file*, so not compiling it leaves them unowned: [`91-`](91-open-questions.md) Q22 |
| `SmartScheduleTime` / smart scheduler | throughout | **KEEP** — client round‑robin fairness |
| `ClientsWithInput`, `ClientsWriteBlocked`, `OutputPending`, `AllSockets`, `AllClients`, `LastSelectMask` (globals) | file scope | **REDEFINE** — become client‑index bitsets or a list, not fd sets |

### 4.4 `tinyx/os/xstrans.c` and `X11/xtrans/`

Everything. `_XSERVTransSocketINETFuncs`, `_XSERVTransSocketLocalFuncs`, `MakeAllCOTSServerListeners`, `Accept`, `Read`, `Writev`, `Close`,
`GetConnectionNumber`, `IsLocal`, `SetOption`, `GetPeerBytes`. **DELETE**, entirely, by not compiling `xstrans.c`.

### 4.5 `OsCommRec` — `tinyx/os/osdep.h`

```c
typedef struct _osComm {
    int fd;                             /* socket fd                  */
    ConnectionInputPtr  input;          /* buffered inbound bytes     */
    ConnectionOutputPtr output;         /* buffered outbound bytes    */
    XID auth_id;                        /* authorization id           */
    CARD32 conn_time;                   /* connection timestamp       */
    struct _XtransConnInfo *trans_conn; /* xtrans handle              */
} OsCommRec, *OsCommPtr;
```

| Field | Disposition |
|---|---|
| `fd` | **REDEFINE** — becomes an internal client index, or `-1`. Retained because `dixstruct.h` and the smart scheduler reference `oc->fd` |
| `input` | **REDEFINE** — points at the client's `dpy->buffer`, not a server‑owned copy |
| `output` | **DELETE** — replaced by the per‑client reply/event slots |
| `auth_id` | **KEEP** — set to a constant |
| `conn_time` | **KEEP** |
| `trans_conn` | **DELETE** |

**No new fields are added to `OsCommRec`.** The reply slot, the event queue and the dispatchable flag live in parallel arrays keyed by `client->index`, so
the shared layout in `osdep.h` is not disturbed ([`21-`](21-inprocess-transport.md) §5.3). There is deliberately **no owning‑task field**: the server never
names a client fiber, which removes both the bookkeeping and the stale‑handle hazard.

### 4.6 `ClientRec` — `tinyx/include/dixstruct.h`

The dispatcher's view of a client. Confirmed field offsets from the tree:

| Field | Line | Meaning | Disposition |
|---|---:|---|---|
| `index` | 82 | Client slot number | **KEEP** |
| `requestBuffer` | 84 | Pointer to the current request bytes | **KEEP** — the substitution point |
| `osPrivate` | 85 | `OsCommPtr` | **KEEP** |
| `swapped` | 86 | Byte order differs from server | **KEEP** — always FALSE in‑process |
| `sequence` | 89 | Request sequence number | **KEEP** |
| `clientGone` | 91 | Teardown flag | **KEEP** |
| `requestVector` | 101 | The `Proc*` table for this client | **KEEP** — the call target |
| `req_len` | 103 | Length of the current request | **KEEP** |

**`ClientRec` needs no changes at all.** That is the strongest evidence that this substitution is architecturally sound: the dispatcher's contract is
already transport‑agnostic.

## 5. Semantic Assumptions

Six assumptions the transport makes that are invisible in any symbol list. Each must be explicitly satisfied or explicitly voided.

| # | Assumption | Where it lives | Disposition |
|---|---|---|---|
| SA1 | **Requests arrive as a byte stream and may be split across reads.** `ReadRequestFromClient` handles partial requests and re‑entry | `io.c:180` | **VOID** — a request is complete before `SyncHandle()` fires. But `InsertFakeRequest` / `ResetCurrentRequest` still need a position concept |
| SA2 | **Client byte order may differ from the server's.** `client->swapped`, `swapreq.c`, `swaprep.c` | `dix/` | **VOID** — client and server share an address space, so `client->swapped` is always FALSE. The machinery is nevertheless **kept**; see below |
| SA3 | **Output is buffered and flushed lazily.** A reply may sit in `oc->output` until the next `FlushAllOutput` | `io.c` | **VOID** — delivery is immediate. Call sites remain as no‑ops |
| SA4 | **The client blocks in `_XReply` and the server is a different process.** No re‑entrancy concern | `xcb_io.c:664` | **REDEFINE** — the client fiber blocks; the server fiber runs. Re‑entrancy is prevented by running all server code on one fiber ([`90-`](90-decisions.md) D‑04) |
| SA5 | **`ConnectionNumber(dpy)` is a usable file descriptor**, pollable by applications | Public Xlib API | **VOID** — becomes `-1`. JWM is the only consumer in our tree and it is edited |
| SA6 | **A dead client is detected by a socket error.** `CheckConnections`, `EOF` on read | `connection.c:747`, `io.c` | **REDEFINE** — a client dies by calling `XCloseDisplay` or by its fiber exiting |

SA2 deserves emphasis: **do not delete the byte‑swapping machinery.** It is ~2,500 LOC of `dix/swapreq.c` + `dix/swaprep.c` that costs nothing at runtime
when `client->swapped` is FALSE. Removing it would be a large, risky edit to upstream `dix/` for no runtime gain — precisely what D‑14's preference order
forbids. It is dead code that pays its rent by not needing to be touched.

## 6. Work Item Summary

| Side | Component | REPLACE | NOP | DELETE | KEEP | REDEFINE |
|---|---|---:|---:|---:|---:|---:|
| Client | `xcb_io.c` | 13 | 1 | 0 | 0 | 0 |
| Client | `xcb_disp.c` / `OpenDis.c` / `ClDisplay.c` / `x11_xcb.c` / `locking.c` | 4 | 4 | 1 | 1 | 0 |
| Client | `X11/Xlibint.h` macros | 0 | 2 | 0 | 4 | 1 |
| Client | `Display` fields | 0 | 1 | 0 | 6 | 2 |
| Server | `os/io.c` | 4 | 3 | 0 | 0 | 0 |
| Server | `os/connection.c` | 4 | 2 | 2 | 0 | 4 |
| Server | `os/WaitFor.c` | 1 | 0 | 1 | 3 | 1 |
| Server | `os/xstrans.c` + `xtrans` | 0 | 0 | all | 0 | 0 |
| Server | `OsCommRec` fields | 0 | 0 | 2 | 2 | 2 |
| Server | `ClientRec` fields | 0 | 0 | 0 | 8 | 0 |
| — | Semantic assumptions | — | — | 3 void | — | 3 |
| **Total** | | **26** | **13** | **6+** | **24** | **13** |

**Twenty‑six functions to write.** That is the size of the transport replacement, and it happens to be the same number as the platform contract
([`12-`](12-platform-contract.md) §9). Coincidence, but a useful calibration: the whole project's *new code* surface is roughly fifty functions.

## 7. Estimated New Code

| File | Est. LOC | Covers |
|---|---:|---|
| `framework/wmb_x11/wmb_transport.c` | 280 | The 13 client symbols |
| `framework/wmb_x11/wmb_open.c` | 140 | `XOpenDisplay` in‑process variant, client registration |
| `framework/wmb_x11/wmb_close.c` | 60 | `XCloseDisplay` |
| `framework/wmb_x11/wmb_setup.c` | 120 | Synthesised `xConnSetup` |
| `framework/wmb_x11/wmb_queue.c` | 150 | Request slots + ready bitmask, reply slots, event queues, the completion predicate |
| `tinyx/os/io_wmb.c` | 160 | The 7 server I/O symbols |
| `tinyx/os/connection_wmb.c` | 120 | The 13 connection symbols |
| `tinyx/os/waitfor_wmb.c` | 90 | `WaitForSomething` |
| `tinyx/os/excluded_stubs_wmb.c` | 180 | Auth / access / XDMCP stubs ([`11-`](11-portability-plan.md) §4.2) |
| **Total** | **~1,300** | |

Against ~4,500 LOC of transport being removed. A net reduction, which is the expected shape: the socket transport is mostly buffering, framing and error
handling that a shared address space does not need.

## 8. Verification Obligations

| # | Obligation | Method |
|---|---|---|
| T1 | Every row in §3 and §4 has an implementation or a justified NOP | Checklist review at P5 |
| T2 | No `xcb_*` symbol is referenced from the link | `nm --undefined-only | grep xcb` empty |
| T3 | `InsertFakeRequest` / `ResetCurrentRequest` work — XKB repeat and `Xext/sleepuntil` exercised | Targeted test at P5 |
| T4 | Server grab (`XGrabServer`/`XUngrabServer`) actually blocks other clients | Two‑client test at P7 |
| T5 | Request bytes produced in‑process are byte‑identical to what stock Xlib produces for the same API call | **Golden capture.** Record the reference once with `xtrace` against any system X server, check the bytes in, and diff `host-pure`'s buffer dump against them. No WMB profile has a socket to compare against ([`90-`](90-decisions.md) D‑49) |
| T6 | 10,000 window create/destroy cycles leave the heap high‑water mark flat | Leak test at P8 |
| T7 | XID allocation, including XC‑MISC top‑up, works past the initial range | Allocate > `resource_mask` resources |
