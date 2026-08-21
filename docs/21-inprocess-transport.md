# 21 — In‑Process Transport

## 1. Purpose

This document specifies **what replaces the X11 socket transport**: the data structures, the control flow, and the exact semantics of every path between an
X client and the X server when both live in one address space and one thread. It is the second deliverable of workstream 3
([`00-introduction.md`](00-introduction.md) §5.3) and it discharges the 26 REPLACE items enumerated in
[`20-transport-inventory.md`](20-transport-inventory.md) §6.

### 1.1 What this document is not

- **Not the inventory.** What must be replaced is [`20-`](20-transport-inventory.md).
- **Not the external ABI.** Applications that are separate binaries are [`22-entry-point-abi.md`](22-entry-point-abi.md).
- **Not the runtime.** `hal_task_*` semantics are [`12-platform-contract.md`](12-platform-contract.md) §6.

### 1.2 The core idea in six lines

```c
/* client fiber */                         /* server fiber */
XMapWindow(dpy, w);
  → dpy->buffer filled by GetReq
  → SyncHandle() → wmb_sync_handler(dpy)
      → post the buffer to the server queue
      → fiber_notify()                        ──►  client->requestBuffer = buffer
      → fiber_wait_for(dispatch_done, …)            ProcVector[opcode](client)
                                              ◄──   fiber_notify()   /* in_flight drops */
```

Everything below is the detail required to make those six lines correct in the presence of replies, events, errors, grabs, sequence numbers, XID ranges and
re‑entrant requests.

## 2. Design Decisions

Five decisions shape everything. Each is recorded with its rationale in [`90-decisions.md`](90-decisions.md).

| # | Decision | Rejected alternative | Why |
|---|---|---|---|
| **D‑04** | **All server code runs on one dedicated fiber.** A client fiber never calls `ProcVector` on its own stack | Direct call from the client fiber (what `transport/x11/wmb_io.c` sketches today) | Fourteen sites in the code we compile suspend a client *inside* a request handler and rely on the dispatcher to resume it — `ClientSleep`, `IgnoreClient`, `ResetCurrentRequest`. A direct call has nowhere to put that wait. Census in [`90-`](90-decisions.md) D‑46 |
| **D‑05** | **Per‑`Display` state, never global** | `static WmbState wmb_state` (the current sketch) | JWM + N applications each have a `Display` |
| **D‑06** | **The request buffer is `dpy->buffer` and is not copied** | Copy into a server‑owned buffer | Zero‑copy is the entire point; the buffer is stable because the client fiber is suspended while the server reads it |
| **D‑08** | **Replies and events are copied into per‑client slots** | Zero‑copy on the return path too | The server's reply lives on the server fiber's stack. It must be copied before the server fiber continues |
| **D‑09** | **The X11 wire format is preserved end to end** | Bypass encoding and call `dix` functions directly | Preserving the format means the 245 Xlib API files, the `swapreq`/`swaprep` machinery, XKB, and every extension work unchanged. Bypassing it means rewriting Xlib |
| **v2.0** | **Every wait is a predicate** (`fiber_wait_for`), never a targeted wake | `hal_task_wake(owner)` on each `WriteToClient` | A wake is an edge, so a multi‑part reply can release a client after its header and before its data — the failure §5.5 calls hardest to trace. A predicate is a level condition on the completed slot, so no early release is expressible. It also deletes the per‑client fiber handle and its stale‑handle hazard, and gives every wait a timeout ([`90-`](90-decisions.md) D‑47‑era runtime, `framework/fiber/docs/fiber-architecture.md` §10) |

D‑09 is worth dwelling on. It would be *possible* to make `XCreateWindow` call `CreateWindow()` in `dix/window.c` directly and skip the protocol entirely.
That saves a `memcpy` and costs a rewrite of every Xlib entry point, every extension, and all compatibility with X11 semantics (error codes, sequence
numbers, event ordering). We keep the wire format and delete only the wire.

### 2.1 The structures the transport does not write itself

The transport holds four collections, and only one of them wants a container
the framework does not already have. `framework/library/` supplies three, all
in `WMB_RUNTIME` ([`01-`](01-component-inventory.md) §4.3), so reusing them
adds no dependency and no new synchronisation argument:

| Transport structure | Backed by | Why that one |
|---|---|---|
| Per‑client **event queue** (§6.1) | **`lists.c` / `lists.h`** | Overflow must drop the oldest *motion* event — one record from the **middle** of the queue. `pipe.c` can only discard its own head; `buffer.c` cannot see records at all ([`90-`](90-decisions.md) D‑48) |
| **Reply slot** (§3.1, §5.5) | A grown byte buffer | `_XRead` needs one contiguous range and a cursor. Fragments are appended and never removed, so there is nothing for a container to do |
| **Request slots** + `g_ready_mask` (§5.1) | Plain array + bitmask | Reached as `&g_slots[client->index]` once per request, and the dispatcher wants an *array of indices*. A list would make the hot path O(n) to buy nothing |
| **Pushed‑back request** (§5.4) | One buffer inside the slot | Upstream pushes at most one request per client, so there is no sequence to hold |

`lists.c` is a singly linked, counted list of *owned* nodes: an entry embeds
`LIST_HEADER` as its first member, `list_insert_*` takes ownership, `list_remove`
and `list_clear` free the node (after an optional `delete_cb`), and `list_unlink`
hands ownership back. Like `pipe.c` it brackets each entry point with
`fiber_critical_enter`/`fiber_critical_exit` under `USE_FIBER`
([`90-`](90-decisions.md) D‑47.4) — which matters for exactly one structure
here, and §6.1 says why.

## 3. Client Side

### 3.1 Per‑`Display` state

```c
/* transport/x11/wmb_x11_internal.h */

typedef enum {
    WMB_REPLY_NONE = 0,
    WMB_REPLY_DATA,          /* a normal reply is waiting     */
    WMB_REPLY_ERROR          /* an xError is waiting          */
} wmb_reply_state_t;

typedef struct {
    /* identity */
    Display        *dpy;             /* back-pointer                        */
    ClientPtr       client;          /* the server-side ClientRec           */
    hal_task_t     *owner;           /* diagnostics only - see D-46 amend. 4 */

    /* request */
    CARD8           pending_opcode;  /* 0 = nothing pending                 */
    Bool            expects_reply;   /* set by _XReply before dispatch      */

    /* reply slot (D-08) */
    wmb_reply_state_t reply_state;
    char           *reply;           /* heap buffer, grown as needed        */
    size_t          reply_cap;
    size_t          reply_len;
    size_t          reply_pos;       /* read cursor for _XRead              */

    /* event queue — wmb_event_t nodes, deposited by WriteToClient (§6.1) */
    list_data_t     events;
    uint16_t        ev_limit;        /* cap; overflow policy in §6.1        */
} WmbDisplay;
```

Attachment to `Display`: WMB reuses the `dpy->xcb` pointer slot (`_X11XCBPrivate*`) to carry a `WmbDisplay*`, because that slot exists in every `Display`,
is private to the transport, and is otherwise dead once XCB is gone. A small accessor keeps the cast in one place:

```c
static inline WmbDisplay *wmb(Display *dpy) { return (WmbDisplay *)dpy->xcb; }
```

This avoids touching `Xlib.h` or `Xlibint.h`'s `Display` layout — no ABI change, no additional patch budget.

### 3.2 `XOpenDisplay` — in‑process

`libX11/src/OpenDis.c` does eight things. Six of them are reusable.

| Step | Stock behaviour | WMB |
|---|---|---|
| 1. Parse `DISPLAY` | `_XConnectDisplay` parses `host:N.S` | **Replaced** — ignore the argument entirely |
| 2. Connect | `xcb_connect` → socket | **Replaced** — `wmb_register_internal_client()` (§5.2) |
| 3. Read `xConnSetup` | Read the blob off the wire | **Replaced** — `wmb_build_setup()` (§3.3) |
| 4. Allocate `Display` | `Xcalloc` | **Reused** |
| 5. Parse setup into `Screen`/`Depth`/`Visual`/`Colormap` arrays | ~300 LOC of `OpenDis.c` | **Reused verbatim** — this is why we synthesise a real blob rather than filling the structures by hand |
| 6. Init XID allocator | From `setup->ridBase` / `ridMask` | **Reused** |
| 7. Locale init | `_XlcCurrentLC` etc. | **Stubbed** |
| 8. Install `synchandler` | Only under `XSynchronize()` | **Always**: `dpy->synchandler = wmb_sync_handler` |

Step 5 is the reason for the design in §3.3: reproducing `OpenDis.c`'s setup parsing by hand would be ~300 LOC of error‑prone structure filling. Producing
a correct `xConnSetup` blob and letting upstream parse it is both less code and more likely to be right.

### 3.3 Synthesising `xConnSetup` — `wmb_setup.c`

The server already knows everything the blob contains; it just never serialises it for an internal client. Two options:

| Option | Approach | Verdict |
|---|---|---|
| A | Call TinyX's own connection‑setup generator | The generator lives in `dix/dispatch.c` (`ProcEstablishConnection` path) and writes through `WriteToClient` |
| B | Bake a static blob at build time | Fragile: it must match the actual screen, visual and pixmap format list |

**Option A wins**, and it is nearly free: `wmb_open.c` registers the client, then posts a synthetic `xConnClientPrefix` through the normal request path.
The server responds with a real `xConnSetup` through `WriteToClient`, which lands in the client's reply slot, which `OpenDis.c`'s existing parser consumes.

This has a pleasing consequence: **the connection handshake is exercised by the same code path as every other request**, so a bug in the reply path shows
up immediately at `XOpenDisplay` rather than three requests later.

### 3.4 The thirteen replacements

```c
/* transport/x11/wmb_transport.c */

void *_XGetRequest(Display *dpy, CARD8 opcode, size_t len)
{
    WmbDisplay *w = wmb(dpy);

    if ((size_t)(dpy->bufmax - dpy->buffer) < len)
        if (_XBufferGrow(dpy, len) != 0) return NULL;

    dpy->bufptr = dpy->buffer;
    memset(dpy->buffer, 0, len);
    ((xReq *)dpy->buffer)->reqType = opcode;
    ((xReq *)dpy->buffer)->length  = (CARD16)(len >> 2);
    dpy->bufptr = dpy->buffer + len;

    dpy->request++;
    w->pending_opcode = opcode;
    w->expects_reply  = False;
    return dpy->buffer;
}

int wmb_sync_handler(Display *dpy)
{
    WmbDisplay *w = wmb(dpy);
    if (w->pending_opcode == 0) return 0;          /* already dispatched */
    return wmb_dispatch(dpy, /*wait_for_reply=*/False);
}

Status _XReply(Display *dpy, xReply *rep, int extra, Bool discard)
{
    WmbDisplay *w = wmb(dpy);
    w->expects_reply = True;

    if (wmb_dispatch(dpy, /*wait_for_reply=*/True) != 0) return 0;

    if (w->reply_state == WMB_REPLY_ERROR) {
        _XError(dpy, (xError *)w->reply);          /* upstream error dispatch */
        w->reply_state = WMB_REPLY_NONE;
        return 0;
    }
    if (w->reply_state != WMB_REPLY_DATA) return 0;

    memcpy(rep, w->reply, SIZEOF(xReply));
    w->reply_pos = SIZEOF(xReply);
    if (extra) {                                   /* fixed extra words     */
        memcpy((char *)rep + SIZEOF(xReply), w->reply + w->reply_pos, extra * 4);
        w->reply_pos += extra * 4;
    }
    if (discard) w->reply_pos = w->reply_len;
    dpy->last_request_read = dpy->request;
    return 1;
}

int _XRead(Display *dpy, char *data, long size)
{
    WmbDisplay *w = wmb(dpy);
    long avail = (long)(w->reply_len - w->reply_pos);
    if (size > avail) return -1;                   /* protocol desync       */
    memcpy(data, w->reply + w->reply_pos, (size_t)size);
    w->reply_pos += (size_t)size;
    return 0;
}

void _XSend(Display *dpy, const char *d, long n)   /* NOP — see §3.5 */
{ (void)dpy; (void)d; (void)n; }

void _XFlush(Display *dpy)
{
    if (wmb(dpy)->pending_opcode) wmb_dispatch(dpy, False);
}

static bool_t wmb_events_queued(void *data)
{
    WmbDisplay *w = (WmbDisplay *)data;
    return (list_is_empty(&w->events) == FALSE) ? TRUE : FALSE;
}

void _XReadEvents(Display *dpy)
{
    WmbDisplay *w = wmb(dpy);
    _XFlush(dpy);
    (void)fiber_wait_for(wmb_events_queued, w, FIBER_WAIT_FOREVER);
    wmb_drain_events_into_display_queue(dpy);      /* → _XEnq per event    */
}
```

`FIBER_WAIT_FOREVER` is correct **here and nowhere else** in the transport: an
application sitting in `XNextEvent` with nothing to do genuinely should wait
indefinitely, and D‑27 requires that it does so without burning the CPU. Every
other wait in this document carries a timeout because a missing reply is a bug,
whereas a missing event is just an idle user.

This is the application's cooperative yield point, and in the common case the *only* long one: a client blocked in `XNextEvent` is what lets the server
fiber pump input and other clients run. An application that never returns to its event loop starves the UI — the same contract every other component owes
([`90-`](90-decisions.md) D‑44). A future `KeepAliveEvent`, which the scheduler injects and a live application must answer, would turn that from a
convention into something enforceable — and would give the familiar "application is not responding" behaviour. Out of scope for now; recorded so the
event‑loop shape does not foreclose it.

`_XEventsQueued`, `_XReadPad`, `_XEatData`, `_XEatDataWords`, `_XNextRequest`, `_XAllocID`, `_XAllocIDs`, `_XIDHandler` follow the same shape and are
omitted here for brevity; all are ≤ 20 lines.

### 3.5 Why `_XSend` is a NOP but `Data()` still has to change

`_XSend` is only reached from the `Data()` macro's overflow branch. If `Data()` grows the buffer instead of overflowing, `_XSend` is never called. The NOP
is a belt‑and‑braces measure for any upstream call site we have not found.

The `Data()` change itself is the single upstream header edit:

```c
/* X11/Xlibint.h — guarded by WMB_INPROC */
#define Data(dpy, data, len) \
    do { \
        if (dpy->bufptr + (len) <= dpy->bufmax) { \
            memcpy(dpy->bufptr, data, (size_t)(len)); \
            dpy->bufptr += ((len) + 3) & ~3; \
        } else if (_XBufferGrow(dpy, (size_t)(len)) == 0) { \
            memcpy(dpy->bufptr, data, (size_t)(len)); \
            dpy->bufptr += ((len) + 3) & ~3; \
        } \
    } while (0)
```

`_XBufferGrow` reallocates `dpy->buffer` preserving `bufptr`'s *offset*, and updates `bufmax`. Growth policy: double, minimum 4 KB, hard cap 1 MB (a
request larger than that is a bug, and BIG‑REQUESTS is capped anyway).

**Danger**: `dpy->buffer` may move. Any code holding a `xReq*` obtained from `GetReq` across a `Data()` call has a dangling pointer. This pattern **does**
occur in upstream Xlib. Mitigation in §9, risk R2.

## 4. The Bridge

> **Reverted to [`90-`](90-decisions.md) D‑04 by D‑46.** This section briefly described D‑45's model — dispatch on the client's own stack, no queue. That
> decision is retired: fourteen sites in the code we compile suspend a client from inside a request handler, and `Dispatch()`'s per‑request contract is
> larger than the call it replaces. The census and the four amendments are in D‑46.

### 4.1 `wmb_dispatch` — the one function that matters

```c
/* Post the request, wake the server, wait for it to finish.  Returns 0 on success. */
typedef struct { WmbDisplay *w; int index; Bool wait_for_reply; } wmb_done_t;

static bool_t wmb_dispatch_done(void *data)
{
    wmb_done_t *d = (wmb_done_t *)data;
    if (wmb_request_in_flight(d->index))                    return FALSE;
    if (d->wait_for_reply && d->w->reply_state == WMB_REPLY_NONE) return FALSE;
    return TRUE;
}

static int wmb_dispatch(Display *dpy, Bool wait_for_reply)
{
    WmbDisplay *w = wmb(dpy);
    if (w->pending_opcode == 0) return 0;

    size_t len = (size_t)(dpy->bufptr - dpy->buffer);
    ((xReq *)dpy->buffer)->length = (CARD16)((len + 3) >> 2);

    w->reply_state = WMB_REPLY_NONE;                    /* reset the slot   */
    w->reply_len = w->reply_pos = 0;

    wmb_server_post(w->client->index, dpy->buffer, len);   /* slot + ready bit */
    fiber_notify();                                        /* server's predicate */

    wmb_done_t done = { w, w->client->index, wait_for_reply };
    if (fiber_wait_for(wmb_dispatch_done, &done, WMB_REPLY_TIMEOUT_MS) == FALSE) {
        return wmb_reply_timeout(w);                    /* diagnosable, not a hang */
    }

    w->pending_opcode = 0;
    dpy->bufptr = dpy->buffer;
    return 0;
}
```

The client is suspended for the whole time the server holds the slot, which is what makes D‑06's zero copy safe: `dpy->buffer` cannot move or be rewritten
while `dix` is reading it.

**Why a predicate rather than a wake.** An earlier draft of this section span
on `do { hal_task_yield(); } while (…)` and had the server call
`hal_task_wake(slot->owner)` after every `WriteToClient`. That combination was
both wasteful and redundant:

| | poll loop + wake | `fiber_wait_for` |
|---|---|---|
| Cost of a waiting client, per scheduler round | **2 context switches** — it is dispatched and re-dispatched to re-test | 1 state compare; it is `BLOCKED` |
| The wake | **did nothing.** A spinning client is `READY`, never `BLOCKED`, so `hal_task_wake` set a flag nobody read | not needed at all |
| Server must hold `hal_task_t *owner` per client | yes (§5.3) | **no** |
| Reply never arrives (risk R2) | hangs, no diagnostic | returns `FALSE` at `WMB_REPLY_TIMEOUT_MS` |
| Multi-part reply released early | possible — the wake is an edge, and `WriteToClient` fires several times per reply | impossible — the predicate is a level condition on the completed slot |

The last row is the one that matters most. §5.5 calls the multi-part reply
"the detail that a naive implementation gets wrong"; a predicate removes the
class of error entirely, because there is no moment at which the client can be
released other than the moment its state is actually complete.

The first row is what protects the *"server idles at < 5 % CPU"* gates
(V‑P4‑2, V‑P5‑2): with four application clients waiting on replies, the poll
form costs eight context switches per scheduler round to discover that nothing
has changed.

**The completion signal costs no patch**, and this is the detail that makes an unmodified `Dispatch()` viable. Upstream's inner loop calls
`ReadRequestFromClient` again after each request and breaks when it returns 0 (`dix/dispatch.c:407–429`). That returning edge *is* "this client has nothing
more in flight", and `ReadRequestFromClient` is our file:

```c
/* transport/x11/io_wmb.c */
int ReadRequestFromClient(ClientPtr client)
{
    wmb_request_slot_t *s = &g_slots[client->index];

    if (s->pushed_len) return wmb_take_pushed_request(client, s);  /* §5.4 */

    if (!s->pending) {                    /* nothing more from this client   */
        if (s->in_flight) {               /* ...and the last one is complete */
            s->in_flight = FALSE;
            fiber_notify();               /* the client's predicate is now true */
        }
        return 0;
    }

    client->requestBuffer = (pointer)s->buffer;
    client->req_len       = ((xReq *)s->buffer)->length;   /* 4-byte units  */
    client->swapped       = FALSE;
    s->pending            = FALSE;
    g_ready_mask         &= ~(1u << client->index);
    return (int)s->length;
}
```

The suspended‑client cases fall out for free. When a handler calls `ResetCurrentRequest` and `IgnoreClient` — contended `XGrabServer`, `ProcAwaitSync` —
`isItTimeToYield` breaks the inner loop *before* `ReadRequestFromClient` is called again, so `in_flight` stays set and the client stays parked. It resumes
when `AttendClient` returns it to the ready set and the dispatcher re‑reads the pushed‑back request. Nothing in the transport has to know that happened.

### 4.2 Where waiting happens

| Wants to wait | Where |
|---|---|
| Its request to complete | `wmb_dispatch`'s yield loop, on the client fiber |
| A server grab held by another client | Inside `dix`, in `ProcGrabServer` — upstream's own path (D‑46 amendment 2). The transport does **not** pre‑check grabs |
| `XNextEvent` on an empty queue | Xlib, after the request returned ([`90-`](90-decisions.md) D‑27) |
| A slow client's event queue is full | **Must not wait.** `io_wmb.c` grows the buffer or kills the client — X's existing semantics for a client that will not read (D‑46 amendment 3) |

The last row is the one that will look like the natural thing to get wrong: the obvious implementation waits for the reader to drain, and that deadlocks
the server fiber against the very client it is trying to feed.

### 4.3 Re‑entrancy inside `dix` is expected and safe

`UpdateCurrentTime()` calls `ProcessInputEvents()`, and it is reached from 19 sites in `dix/events.c`, `dix/property.c`, `Xext/shape.c`, `Xext/sync.c`,
`Xext/saver.c` and `Xext/xtest.c` — from *inside* request handlers. So a request can deliver input events, which can deliver events to other clients,
mid‑request.

That is a **nested call on one stack**, which is exactly what upstream does in the socket build. It is not concurrency: `WriteToClient` copies into the
target client's slot (D‑08) and that client is a suspended fiber, not a running one. Nothing in the path yields.

### 4.4 What the server fiber runs

`Dispatch()`, unmodified (D‑46 amendment 1). Everything below stays upstream's problem rather than ours, and stays identical between `host-dev`'s socket
path and the in‑process path — which is what makes D‑15's oracle an independent check:

| Responsibility | Where |
|---|---|
| `ReadRequestFromClient`, `client->sequence++`, `BadLength` check | `Dispatch()` inner loop |
| `SendErrorToClient` / `CloseDownClient` when a handler fails | Same |
| `isItTimeToYield`, `grabState` kickout, round robin, smart schedule | Same |
| `ProcessInputEvents()` when input is pending | Same; also reached re‑entrantly via `UpdateCurrentTime()` |
| `FlushAllOutput` / `FlushIfCriticalOutputPending` | Same |
| `BlockHandler` / `WakeupHandler`, which drives KDrive's `pollEvents` | `WaitForSomething` — §5.6, gate V‑Q4‑3, the highest‑risk omission in the plan |
| Screen saver and smart‑schedule timers | `WaitForSomething` |

Requests from **external callers** need no separate path: they post into the same slots and the same fiber executes them ([`90-`](90-decisions.md) D‑44).
One execution model, one stack inside `dix`.

### 4.5 Recovering the backtrace

Executing on the server fiber severs the stack at the queue: a fault in `fb` shows `xsrv → Dispatch → ProcPolyFillRectangle → fb`, with no sign of who
asked. The slot therefore records the requesting client's fiber handle, and one debug helper prints both halves (D‑46 amendment 4):

```c
void wmb_dump_client_stack(void);   /* requestingClient's fiber, then this one */
```

Roughly 30 LOC against `framework/fiber/`'s existing walker, and it buys back the only thing D‑45 was clearly better at. It is compiled in every profile —
on target it is the difference between a usable fault report and an address.

## 5. Server Side

### 5.1 The request queue

Single‑slot per client, because a client fiber is always suspended while its request is in flight:

```c
/* transport/x11/wmb_queue.c */
typedef struct {
    ClientPtr    client;
    const char  *buffer;      /* points into the client's dpy->buffer */
    size_t       length;
    Bool         pending;     /* a request is waiting to be read      */
    Bool         in_flight;   /* posted and not yet completed         */
    char        *pushed;      /* InsertFakeRequest / ResetCurrentRequest */
    size_t       pushed_len;
} wmb_request_slot_t;

static wmb_request_slot_t g_slots[WMB_MAX_INTERNAL_CLIENTS];
static uint32_t           g_ready_mask;        /* bit per client index      */
```

`wmb_server_post()` fills the slot and sets the bit. `wmb_collect_ready_clients()` (called from `waitfor_wmb.c`) turns the mask into the `pClientsReady`
array the dispatcher expects.

A bitmask rather than a list because the dispatcher wants an *array of client indices* and because it maps directly onto the fd‑set semantics it replaces
(census item SD1).

**The producer side is the one place in WMB that is not serialised by cooperative scheduling.** Every internal client is a fiber, so its post cannot
interleave with another's — and `fiber_notify()` is safe from any of them, because it runs under the runtime lock. An *external* client is a task the
platform owns and may preempt ([`22-`](22-entry-point-abi.md) §7 E4), so from P8 onward `g_ready_mask` is updated with a single atomic OR and the server is
woken through a polled mailbox flag rather than a call into the scheduler ([`90-`](90-decisions.md) D‑44).

**`fiber_notify()` must not be called from an external caller's task.** It walks the fiber pool and evaluates predicates under the *fiber* runtime's lock,
which does not exclude a foreign OS thread. The external path therefore relies on the atomic OR plus the server's `WMB_IDLE_TICK_MS` timeout: correctness
comes from the predicate, latency from the tick. That is the same trade the internal path makes, with the notify removed.

The slot itself needs no guard — it is per client. The consumer stays single: the server fiber.

### 5.2 Internal client registration — `connection_wmb.c`

```c
ClientPtr wmb_register_internal_client(Display *dpy)
{
    OsCommPtr oc = (OsCommPtr)xcalloc(1, sizeof(OsCommRec));
    oc->fd        = WMB_INTERNAL_FD_BASE + wmb_alloc_slot();  /* synthetic  */
    oc->input     = NULL;                 /* we never buffer input          */
    oc->output    = NULL;                 /* we never buffer output         */
    oc->auth_id   = None;
    oc->conn_time = 0;
    oc->trans_conn = NULL;

    ClientPtr client = NextAvailableClient((pointer)oc);
    if (!client) { xfree(oc); return NULL; }

    wmb_slot_bind(client->index, dpy);
    return client;
}
```

`NextAvailableClient()` is upstream `dix/dispatch.c` and is **additive‑friendly**: it takes an opaque `osPrivate` and does not care what is behind it. No
patch required.

### 5.3 `OsCommRec` extension

Rather than editing `osdep.h`'s `OsCommRec` (which would consume patch budget and change a shared layout), WMB keeps a **parallel array** indexed by
`client->index`:

```c
typedef struct {
    Display     *dpy;             /* the client-side Display               */
    Bool         dispatchable;    /* cleared by IgnoreClient / grabs        */
    Bool         in_use;
} wmb_client_slot_t;

static wmb_client_slot_t g_clients[WMB_MAX_INTERNAL_CLIENTS];
```

This is the same trick the smart scheduler uses and it keeps the upstream structure untouched.

**There is no `owner` field.** An earlier draft carried a `hal_task_t *owner`
so the server could wake the client. With §4.1's predicate the server never
names a client fiber, which removes both the bookkeeping and a lifetime hazard:
a stale task handle left behind by a disconnected client would otherwise wake
whichever task later occupied that slot.

### 5.4 `io_wmb.c` — ingress

`ReadRequestFromClient` is given in full in §4.1, because its returning‑zero edge is also the client's completion signal. Two points belong here.

**`pending` versus `in_flight`.** `pending` means "there are bytes the dispatcher has not taken yet"; `in_flight` means "the client is parked waiting for
this request to finish". They are distinct because a handler may push the request back and park the client, at which point `pending` becomes true again
while `in_flight` never dropped.

**`InsertFakeRequest` and `ResetCurrentRequest`** operate on the slot's small pushed‑request buffer, which `ReadRequestFromClient` checks first:

```c
Bool InsertFakeRequest(ClientPtr client, char *data, int count);  /* connection setup */
void ResetCurrentRequest(ClientPtr client);                       /* push the current one back */
```

Roughly 40 LOC. They must exist or contended `XGrabServer`, `ProcAwaitSync` and `Xext/xtest.c`'s delayed input break silently — and, per
[`90-`](90-decisions.md) D‑46, they are exactly the mechanism that makes dispatch‑on‑the‑client's‑stack unworkable.

### 5.5 `io_wmb.c` — egress, the single most important function

```c
int WriteToClient(ClientPtr who, int count, const char *buf)
{
    if (count <= 0 || who->clientGone) return 0;

    wmb_client_slot_t *slot = &g_clients[who->index];
    WmbDisplay        *w    = wmb(slot->dpy);

    if (wmb_is_event(buf, count)) {
        wmb_event_enqueue(w, (const xEvent *)buf, count / sizeof(xEvent));
    } else {
        wmb_reply_store(w, buf, (size_t)count);      /* appends; see below */
        w->reply_state = wmb_is_error(buf) ? WMB_REPLY_ERROR : WMB_REPLY_DATA;
    }

    return count;                                     /* no wake: see below */
}
```

**No wake here, deliberately.** `WriteToClient` is called from deep inside
`dix`, **several times for one reply** and often for a client other than the one
whose request is executing. Waking on each call is the naive design, and it is
wrong twice over: the wake is an edge, so a client can be released after the
32‑byte header and before its data arrives; and it is redundant anyway, because
the client is parked on a *predicate* over the completed slot (§4.1).

The release happens once, where completion is actually known: `fiber_notify()`
in `ReadRequestFromClient` when `in_flight` drops (§4.1). One notify per
dispatch rather than N per reply.

Hence `wmb_reply_store` *appends* rather than overwrites, and the reply slot is
only reset by the client at the start of the next `wmb_dispatch`.

**`fiber_notify()` must mean "re-evaluate", never "switch now".** It is called
from `ReadRequestFromClient`, i.e. from inside the server fiber's dispatch
loop. A notify that transferred control immediately would suspend the server
fiber mid-request — the one thing the whole design exists to prevent
([`12-`](12-platform-contract.md) §6.2 W6 states the same rule for
`hal_task_wake`).

**Distinguishing replies from events is the crux.** X11's wire format makes it decidable:

| First byte of the record | Meaning |
|---|---|
| `0` (`X_Error`) | Error — 32 bytes |
| `1` (`X_Reply`) | Reply — 32 bytes plus `length` × 4 additional bytes |
| `2 … 127` | Event — 32 bytes |
| `≥ 128` | Event with the send‑event bit set |

But `WriteToClient` is also called **multiple times per reply** for replies with variable payload: once with the 32‑byte header, then again with the data.
Hence `wmb_reply_store` *appends* rather than overwrites, and the reply slot is only reset by the client at the start of the next `wmb_dispatch`. This is
the detail that a naive implementation gets wrong and that produces "reply structure corrupted" bugs that are very hard to trace.

### 5.6 `waitfor_wmb.c`

```c
static bool_t wmb_server_has_work(void *data)
{
    (void)data;
    return (g_ready_mask != 0u) ? TRUE : FALSE;
}

void WaitForSomething(int *pClientsReady)
{
    unsigned long wait_ms = WMB_IDLE_TICK_MS;      /* default idle period   */

    for (;;) {
        BlockHandler((pointer)&wait_ms, NULL);     /* → KdBlockHandler →    */
                                                   /*   KdOsFuncs.pollEvents */
        if (g_ready_mask != 0) { WakeupHandler(1, NULL); break; }

        if (wmb_timers_pending(&wait_ms) == 0 && wait_ms == 0) wait_ms = 1;

        /* Parks until a client posts (fiber_notify) or wait_ms elapses.
         * The timeout is what keeps the handler cadence, so it is a
         * ceiling on input latency, not a sleep. */
        (void)fiber_wait_for(wmb_server_has_work, NULL, (uint32_t)wait_ms);

        WakeupHandler(0, NULL);                    /* → mieqProcessInputEvents */
        if (g_ready_mask != 0) break;
    }

    *pClientsReady = wmb_collect_ready_clients();
}
```

**The `BlockHandler` / `WakeupHandler` pair is non‑negotiable** —
[`11-portability-plan.md`](11-portability-plan.md) §7.1. It is also why the
server is the **one** waiter that keeps a bounded timeout rather than
`FIBER_WAIT_FOREVER`: those handlers are the only thing driving
`KdOsFuncs.pollEvents`, so the loop must come round at `WMB_IDLE_TICK_MS`
whether or not a client posted. A server parked forever on a predicate would
satisfy every request correctly and lose the mouse
([`02-`](02-baseline-architecture.md) §9).

Waiting on `g_ready_mask` rather than on a per‑fiber wake flag means **no part
of the transport names another fiber**. `wmb_dispatch` sets the bit and calls
`fiber_notify()`; it does not need `g_server_fiber`, and the server does not
need the client's handle (§5.3).

### 5.7 Grabs and flow control

`OnlyListenToOneClient`, `ListenToAllClients`, `IgnoreClient`, `AttendClient` become manipulations of `g_clients[i].dispatchable`, and
`wmb_collect_ready_clients()` filters on it:

```c
void IgnoreClient(ClientPtr client)      { g_clients[client->index].dispatchable = FALSE;
                                           isItTimeToYield = TRUE; }
void AttendClient(ClientPtr client)      { g_clients[client->index].dispatchable = TRUE;
                                           if (g_slots[client->index].pushed_len ||
                                               g_slots[client->index].pending) {
                                               g_ready_mask |= 1u << client->index;
                                               fiber_notify();   /* server may proceed */
                                           } }
void OnlyListenToOneClient(ClientPtr c)  { g_grab_client = c->index; }
void ListenToAllClients(void)            { g_grab_client = -1; }
```

Required for correctness, not just completeness: JWM calls `XGrabServer` during reparenting.

**Every site that sets a bit in `g_ready_mask` must call `fiber_notify()`.**
There are two — `wmb_server_post` (§4.1) and `AttendClient` above. Omitting it
does not lose the request: the server's wait carries `WMB_IDLE_TICK_MS`, so the
cost is up to one idle tick of latency, not a stall.

**These four are what make [`90-`](90-decisions.md) D‑46 amendment 2 work.** A contended `ProcGrabServer` pushes its request back and calls
`IgnoreClient`; `UngrabServer` walks `grabWaiters` and calls `AttendClient`, which must put the client's ready bit back or the pushed‑back request is never
re‑read and the client waits forever. That is the whole of the grab implementation — upstream's, not ours — and it is why the transport does no grab
checking of its own. `IgnoreClient` setting `isItTimeToYield` matches `os/connection.c:927` and is what breaks `Dispatch()`'s inner loop before the client
is re‑read.

## 6. The Event Path

```
 hal fiber                          xsrv fiber                        client fiber
 ─────────                          ──────────                        ────────────
 hal_input_pump()
   ring buffers filled
   (no notify - see below) ~~~~~~~► WaitForSomething
                                      BlockHandler       (timeout brings it here)
                                        KdBlockHandler
                                          tima_OsPollEvents
                                            tima_KeyboardPoll
                                            tima_MousePoll
                                              hal_input_read_byte
                                              KdEnqueue*Event
                                                mieqEnqueue
                                      WakeupHandler
                                        mieqProcessInputEvents
                                          DeliverEvents (dix/events.c)
                                            WriteEventsToClient
                                              WriteToClient  ──────► event queue
                                      fiber_notify() ─────────────► XNextEvent's
                                        (once, after the dispatch pass)     predicate
                                                                            returns
```

Note that **input does not go through the request queue at all** — it arrives via the KDrive/`mieq` path that already exists and is entirely
transport‑agnostic. The only change is the last hop, `WriteToClient`.

**The `hal` fiber does not notify the server, and must not.** `xsrv`'s predicate
is `g_ready_mask != 0`, which tracks *client requests*; filling an input ring
does not set it, so a notify would evaluate the predicate, find it false, and
achieve nothing. Input reaches the server through `BlockHandler` on the next
pass of the `WaitForSomething` loop instead — which is precisely why that loop's
wait carries `WMB_IDLE_TICK_MS` rather than `FIBER_WAIT_FOREVER` (§5.6).

That makes the tick a **hard bound on input latency**, not a tuning knob:
gate V‑P5‑4 requires `KdOsFuncs.pollEvents` at ≥ 30 Hz, so `WMB_IDLE_TICK_MS`
must be ≤ 33 ms.

### 6.1 Event queue sizing

Per client, and the one transport structure built on `lists.c` (§2.1). An event queue that overflows loses input, which is worse than blocking.

```c
/* transport/x11/wmb_event.c */
typedef struct {
    LIST_HEADER;              /* list_node_t node - MUST be the first member */
    xEvent   ev;              /* copied out of WriteToClient's buffer (D-08)  */
} wmb_event_t;

/* server fiber, from WriteToClient (§5.5) */
static void wmb_event_enqueue(WmbDisplay *w, const xEvent *ev, int count)
{
    for (int i = 0; i < count; i++) {
        wmb_event_t *e;

        if (list_size(&w->events) >= w->ev_limit) wmb_event_make_room(w);

        e = (wmb_event_t *)MMALLOC(sizeof(wmb_event_t));
        if (e == NULL) { wmb_log_event_drop(w, &ev[i]); return; }

        e->ev = ev[i];
        list_insert_tail(&w->events, &e->node);        /* takes ownership */
    }
}

/* client fiber, from _XReadEvents (§3.4) */
static void wmb_drain_events_into_display_queue(Display *dpy)
{
    WmbDisplay  *w = wmb(dpy);
    wmb_event_t *e;

    while ((e = (wmb_event_t *)list_get_head(&w->events)) != NULL) {
        _XEnq(dpy, &e->ev);
        list_remove(&w->events, &e->node);             /* frees the node  */
    }
}
```

A node owns nothing beyond itself, so the list is created with
`list_init(&w->events, NULL)` — no delete callback — and client teardown is one
call, `list_clear(&w->events)`, in `wmb_slot_release()`.

Capacity and overflow:

| Client | `ev_limit` | Overflow |
|---|---:|---|
| JWM | 256 events | Drop the oldest **motion** event first (X11 permits motion compression); if none, log and drop oldest |
| Application | 128 events | Same |

```c
static void wmb_event_make_room(WmbDisplay *w)
{
    static const BYTE motion = MotionNotify;
    wmb_event_t *e = list_find_value(&w->events, wmb_event_t, ev.u.u.type,
                                     sizeof(BYTE), &motion);
    if (e == NULL) {
        e = (wmb_event_t *)list_get_head(&w->events);  /* oldest, whatever it is */
        wmb_log_event_drop(w, &e->ev);
    }
    list_remove(&w->events, &e->node);
}
```

`list_find` walks from the head and enqueue appends at the tail, so the first match **is** the oldest motion event: the policy falls out of the structure
instead of needing a scan and a compaction. Dropping one record from the middle is precisely what neither `pipe.c` nor `buffer.c` can express, and it is the
whole reason the transport takes a dependency on a third container.

Motion compression is what makes a bounded queue safe: a client that is slow to drain accumulates `MotionNotify` faster than anything else, and collapsing
them is protocol‑legal.

**A failed `MMALLOC` drops the event and does not wait.** Waiting for memory on the server fiber is the same deadlock §4.2's last row rules out for a full
queue; the outcome and the log line are the outcome and the log line of overflow.

**This queue is where `lists.c`'s locking earns its place.** Every other structure in this document has a single fiber touching it — the slots and the reply
buffer are written by the server while their client is suspended (§4.1). The event queue is not: `WriteToClient` appends on the **server** fiber for a
client that is parked in `XNextEvent`, and `_XReadEvents` drains on the **client** fiber. Each `list_*` call brackets its own update
([`90-`](90-decisions.md) D‑47.4), so enqueue and drain need to know nothing about each other. What the module does *not* cover is a compound sequence, and
there is one: `wmb_event_make_room` does `list_find` then `list_remove`. It is safe because both halves run on the server fiber, which is the only producer
— if a second producer is ever added, that pair needs its own `fiber_critical_enter`/`exit`.

Node allocation on the server fiber and release on the client fiber is the same split `pipe.c`'s buffers already have, and rests on the same
cooperative‑scheduling argument (D‑47).

## 7. Error Path

An X error is a 32‑byte `xError` record delivered through `WriteToClient`, exactly like a reply. Two cases:

| Case | Server behaviour | Client behaviour |
|---|---|---|
| Error for a request with a reply | `SendErrorToClient` → `WriteToClient` | `_XReply` sees `WMB_REPLY_ERROR`, calls `_XError`, returns 0 |
| Error for a fire‑and‑forget request | Same | The error must be delivered **asynchronously**, as X11 does: it goes into the *event* queue as one more `wmb_event_t` node — an `xError` is 32 bytes, exactly like an `xEvent` — and surfaces at the next `XNextEvent`/`XPending` via the standard `_XError` path |

The second case is what makes `XSetErrorHandler` work as applications expect. Detection: an `xError` whose sequence number does not match a request the
client is currently waiting on is asynchronous.

## 8. Sequence Numbers

X11 numbers requests from 1 and both sides track them. In WMB:

| Counter | Owner | Maintained by |
|---|---|---|
| `dpy->request` | client | `_XGetRequest` increments |
| `dpy->last_request_read` | client | `_XReply` sets |
| `client->sequence` | server | `dix/dispatch.c` increments |

They must stay in step, because `XSync`, error reporting and `XSetAfterFunction` all depend on it. Since every request goes through exactly one
`_XGetRequest` and exactly one dispatch, they do — but the invariant is worth an assertion in debug builds:

```c
assert(dpy->request == w->client->sequence + 1);   /* before dispatch */
```

## 9. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | **`WriteToClient` misclassifies a multi‑part reply**, producing corrupt data for variable‑length replies (`XGetWindowProperty`, `XListFonts`, `XQueryTree`) | **High** | **High** | §5.5 append semantics; gate T5 in [`20-`](20-transport-inventory.md) §8 compares bytes against a socket reference |
| R2 | **`dpy->buffer` reallocation invalidates a live `xReq*`.** Upstream code does `GetReq(...); req->field = x; Data(...); req->other = y;` | **High** | **High** | Grow policy allocates generously up front (16 KB initial) so growth is rare; add a debug canary that poisons the old buffer and traps on use; audit the 17 `Data()` call sites |
| R3 | **`BlockHandler`/`WakeupHandler` omitted** → no input | Medium | **High** | §5.6 explicit; gate V‑Q4‑3 |
| R4 | **`InsertFakeRequest` / `ResetCurrentRequest` forgotten** → XKB repeat and `sleepuntil` break subtly | Medium | Medium | §5.4; gate T3 |
| R5 | **Deadlock**: client waits for a reply the server will never produce (e.g. the request was dropped because the client was not `dispatchable`) | Medium | **High** | Closed by construction: `wmb_dispatch` waits with `fiber_wait_for(…, WMB_REPLY_TIMEOUT_MS)` in **every** build, not just debug (§4.1). A missed reply becomes a logged, diagnosable failure instead of a frozen UI |
| R6 | **Re‑entrancy**: a `Proc*` handler causes a `WriteToClient` to the *same* client that is currently dispatching | Low | Medium | Legal and handled: the reply slot is per client and the client fiber is suspended. The predicate cannot fire early, because release happens once, when `in_flight` drops (§4.1) |
| R7 | **Setup blob mismatch**: `OpenDis.c`'s parser rejects the synthesised blob | Medium | Medium | §3.3 option A generates it with the server's own code, so mismatch is impossible by construction |
| R8 | **Event queue overflow** under motion flood | Medium | Low | §6.1 motion compression: `list_size` is the cap and `list_find_value` finds the oldest motion event to drop in one call |
| R9 | **`lists.c`'s node contract broken** — an entry whose first member is not `LIST_HEADER`, a node freed while still linked, or a queue left behind by a closing `Display` | Low | **High** | One entry type (`wmb_event_t`, §6.1) and one owner; `list_unlink` clears `next`, so a second unlink of the same node fails visibly instead of corrupting the chain; teardown is `list_clear(&w->events)` in `wmb_slot_release()` |

R1 and R2 are the two that will actually cost time. Both are addressed by gate T5 (byte‑level differential testing against the `host-dev` socket path),
which is the single highest‑value test in the project.

## 10. Implementation Order

| Step | Deliverable | Proven by |
|---|---|---|
| 1 | `wmb_queue.c`: slots, ready mask, client registration | Unit test: register 4 clients, post, collect |
| 2 | `connection_wmb.c` + `excluded_stubs_wmb.c` | Whole‑stack link succeeds in `host-pure` |
| 3 | `waitfor_wmb.c` | Server idles; `pollEvents` observed at ≥ 30 Hz |
| 4 | `io_wmb.c` ingress | A hand‑built `X_NoOperation` request reaches `ProcNoOperation` |
| 5 | `wmb_open.c` + `wmb_setup.c` | `XOpenDisplay(NULL)` returns a valid `Display*` with correct `Screen`/`Visual` |
| 6 | `wmb_transport.c` fire‑and‑forget | `XCreateSimpleWindow` + `XMapWindow` produce a window |
| 7 | `io_wmb.c` egress + `_XReply` | `XGetWindowAttributes` returns correct data |
| 8 | Event path — `wmb_event.c`, the list‑backed queue of §6.1 | `wmb_test_client` receives `Expose`; 1,000 `MotionNotify` posted into a 128‑entry queue leave 128 events, the newest ones, and nothing allocated after `list_clear` |
| 9 | Error path | A deliberate `BadWindow` reaches `XSetErrorHandler` |
| 10 | Grabs, `InsertFakeRequest`, XID top‑up | Gates T3, T4, T7 |
| 11 | Timeout path | Drop a reply deliberately; `wmb_dispatch` returns at `WMB_REPLY_TIMEOUT_MS` with a diagnosable error rather than hanging (risk R5) |

Step 4 — dispatching a hand‑built `X_NoOperation` — is the cheapest possible end‑to‑end proof and should be reached as early as possible. It requires no
Xlib at all, just a 4‑byte buffer and a `ClientRec`.
