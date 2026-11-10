# 21 - In-Process Transport

## 1. Purpose

This is the implemented P5 contract between retained Xlib and actual TinyX in
one cooperative address space. It replaces the socket transport enumerated in
[20-transport-inventory.md](20-transport-inventory.md), not the X11 protocol or
the upstream dispatcher. Build/run instructions are in the
[transport README](../transport/x11/README.md).

The original design's illustrative code is superseded here by the actual
ownership/framing contract. In particular, resetting the builder at every
request, making `_XSend` a no-op, classifying each server write independently,
and completing only on the next read were not correct implementations.

### 1.1 What this document is not

- The external application ABI: [22-](22-entry-point-abi.md), P8.
- The runtime/scheduling API: [12-](12-platform-contract.md).
- Freestanding certification or the hardware port: P9; [P6](48-p6-portability.md)
  now verifies the selected hosted source/patch boundary.
- A JWM integration claim: JWM is P7.

### 1.2 The core flow

```text
client fiber                                  xsrv fiber
retained Xlib builds a complete batch
post borrowed bytes, notify, predicate-wait -> collect an eligible native client
                                              upstream Dispatch()
                                              ReadRequestFromClient()
                                              native request handler
                                              WriteToClient() -> copy/frame output
                                           <- complete at read/wait boundary
consume replies/errors/events; reuse buffer
```

Only `xsrv` executes DIX. A completion notification never transfers server
execution onto a client's stack.

## 2. Design Decisions

| Decision | Implemented contract |
|---|---|
| D-04 / D-46 | One dedicated server fiber; upstream `Dispatch()` remains unmodified |
| D-05 | Private state per `Display`, not one global client |
| D-06 | Borrow complete client request batches without copying their normal ingress |
| D-08 | Copy output before the server's stack buffers expire |
| D-09 | Preserve X11 request/reply/event/error bytes and native server semantics |
| D-48 | Use framework `lists.c` for owned packets/events, not a byte FIFO pretending to be records |
| D-49 | Select only the in-process subject transport, never an alternate native listener |
| D-53 | Actual P5 behavior, separate client/server ABIs, and necessary guarded repairs |

### 2.1 Structures and containers

[wmb_bridge.h](../transport/x11/wmb_bridge.h) contains standard-C shared state:
connection slots, borrowed input, pushed/retired storage, streaming setup/output,
owned response/event lists, full serial metadata and a diagnostic trace hook.
It contains no `Display`, `ClientRec`, `XID` or other ABI-sensitive X type.

[wmb_xlib_internal.h](../transport/x11/wmb_xlib_internal.h) is client-only.
[wmb_connection.h](../tinyx/os/wmb_connection.h) is server-only. Those two
translation-unit cohorts must never share private X headers.

Packet nodes embed `LIST_HEADER`. Taking a packet transfers ownership;
`wmb_packet_free` updates its connection's accounting. Disconnect clears owned
lists and byte buffers after the server has released borrowed storage.

## 3. Client Side

### 3.1 Per-Display state

`Display.xcb` is an opaque pointer to the owned `WmbDisplay`, not an XCB
connection. It holds the bridge connection, current reply/cursor, awaiting
serial and XC-MISC recursion guard. The public `Display` layout is unchanged.
`ConnectionNumber(dpy)` is `-1`, not a pollable descriptor.

Use the matching [client header](../libX11/include/X11/Xlibint.h). The older
workspace `X11/Xlibint.h` does not match these API sources. The client header
rejects `_XSERVER64`; the internal adapter also checks the canonical-header
marker. Client sources never inherit the server's configuration prefix.

### 3.2 XOpenDisplay

[OpenDis.c](../libX11/src/OpenDis.c) retains native allocation, setup parsing,
Screen/Depth/Visual construction, resource initialization and destructor.
Guarded hooks replace native connection/setup acquisition; the display name
does not choose a remote server.

The opening control request is serviced on `xsrv`. It creates a native client,
executes the real handshake, and returns its setup bytes. Opening installs the
in-process synchronization hook and negotiates BIG-REQUESTS. Native threads,
XIM/locale/resource-database files and optional color-management facilities
are outside this core-font profile; unsupported threading is diagnosed.

### 3.3 Actual setup, not a fabricated screen description

`NextAvailableClient` inserts the native initial request. The server adapter
appends `xConnClientPrefix`; ordinary dispatch executes
`ProcInitialConnection` and `ProcEstablishConnection`, including native replay.
The eight-byte setup prefix and declared setup body are framed separately from
normal output. Xlib consumes that real blob through its existing parser.

### 3.4 Transport replacements

[wmb_transport.c](../transport/x11/wmb_transport.c) supplies the retained API's
request allocation, send/flush, reply/read, event wait/count and XID hooks.

- `_XGetRequest` appends to the builder, advances native sequence state and
  preserves already-built requests. It does not reset the batch.
- `_XFlush` leases all complete built bytes to the server and waits for release.
  A valid empty-request sentinel preserves `last_req` invariants afterward.
- `_XSend` appends its extra bytes/padding and dispatches; named-color/image
  APIs use it directly. It does not drain an expected synchronous error before
  `_XReply` establishes its target.
- `_XReply` uses full serial metadata to select replies/errors, handles native
  asynchronous reply callbacks, and preserves expected error special cases.
- `_XRead`/padding/discard helpers consume the owned reply cursor, never a socket.
- `_XReadEvents` flushes and predicate-waits only while neither a queued event,
  error nor terminal connection state can make progress.
- `_XEventsQueued` honors Xlib's queue modes without a native fd.
- XID allocation starts with the native setup range and safely refills through
  XC-MISC before a new request is constructed.

The default after-function encourages immediate cooperative progress.
Explicit flushes/round trips still support real multi-request batches, including
the sequence-rollover test; correctness does not depend on one request per post.

### 3.5 Complete-builder growth and pointer lifetime

The guarded `Data`, `Data32`, `BufAlloc` and request preparation paths grow
complete batches instead of flushing an unfinished request. `_XBufferGrow`
preserves builder offsets/`last_req`, rejects mutation during a lease, and leaves
the previous allocation intact on OOM.

Applications must not retain internal request pointers across growth. The
selected API paths are exercised by explicit growth/OOM tests, 32-bit property
data, large requests and independent wire comparisons. ASan checks stale-pointer
use; there is no claimed `WMB_DEBUG_BUFFER_POISON` feature in the shipping build.

## 4. The Bridge

### 4.1 Posting, predicates and completion

[wmb_queue.c](../transport/x11/wmb_queue.c) owns up to 32 connection slots.
Opening, closing and borrowed-batch submission notify the framework; waits are
predicates over control/completion/packet/terminal state, not targeted fiber
handles. No server structure owns or wakes a stale client task handle.

Completion occurs at the next request read **and** at the server wait boundary.
Native error exits and grab kickout can leave the dispatcher without another
read. Ignored/replayed requests keep their lease instead of completing early.

### 4.2 Where waiting happens

Only clients wait for replies/completion. The server never waits for a client to
consume output. Request/control/reply waits are bounded to 5,000 ms; only an
empty event-idle wait is indefinite. Failure is logged and follows Xlib's normal
fatal I/O path where applicable, not a success-shaped empty reply.

The timeout test installs an I/O handler that exits with code 77, allowing CTest
to verify the approximately five-second failure without treating it as success.
Production fatal Xlib I/O is not a recoverable per-window error.

### 4.3 Re-entry and delivery

Native handlers can write to their own or other clients. Output is copied into
that connection's queues while its owner is suspended. Queue notification does
not permit nested client-side entry into the server.

### 4.4 What the server fiber runs

The existing server entry calls ordinary `Dispatch()`. Controls, timers,
block/wakeup handlers, HAL input, eligible-client collection, and native client
resource teardown all remain on that fiber. P5 adds no alternate dispatcher or
direct `ProcVector` calls in a client.

At P6, block handlers share a typed 32-bit millisecond budget, not a native
`timeval`. The SYNC callback/shared type and `AdjustWaitForDelay` agree; real
SERVERTIME waits and the original XTEST sleep path verify that seam.

### 4.5 Recovering diagnostics

Timeouts identify the failed transport operation. Inspect the client's waiting
serial/connection plus the separate `xsrv` stack in a debugger. The split stacks
are deliberate. A synthetic combined-stack debugger helper remains optional
future tooling, not an implemented public API.

## 5. Server Side

### 5.1 Request slots and native client indices

A connection slot is not a native client index. The server binds an allocated
`ClientRec` to its owned `WmbOsConnection`, whose first member is `OsCommRec`.
Ready collection scans bounded connections, honors native ignore/grab/impervious
state and returns the highest eligible priority, as the dispatcher expects.

### 5.2 Registration and teardown

[connection_wmb.c](../tinyx/os/connection_wmb.c) processes open/close controls on
`xsrv`, using native allocation/handshake and `CloseDownClient`. No listener,
accept, credential lookup or native descriptor close is required.

The application owns its `Display` and must close it before its fiber exits.
Automatic cleanup of an abandoned `Display` on arbitrary fiber destruction is
not implemented. P8/lifecycle work must not assume socket EOF will do that job.

### 5.3 OsCommRec boundary

The core layout is not extended. The owned server wrapper holds a bridge pointer;
the base descriptor is `-1`, with native input/output buffers unused. Shared
bridge fields are fixed-width integers/standard-C sizes/pointers, not server X
types. This prevents LP64 client XIDs/KeySyms from becoming 32-bit server values
through a mixed include order.

### 5.4 Ingress, replay and BIG-REQUESTS

[io_wmb.c](../tinyx/os/io_wmb.c) validates each complete request inside a borrowed
batch, sets `requestBuffer`/`req_len`, and returns its native length. Malformed or
over-limit framing fails explicitly.

Fake/current-request replay uses owned pushed storage. Old storage is retired,
not freed while a native handler still borrows it. Replayed requests retain their
original full serial.

BIG-REQUESTS is negotiated and capped to the bridge limit. The extended header
is normalized in place for native dispatch, restored before releasing borrowed
bytes, and reconstructed when replay requires it.

### 5.5 Egress: streaming framing, not per-write classification

`WriteToClient` feeds a stream parser. Each native write receives standard
four-byte padding. Normal replies use the 32-byte header plus declared payload;
errors/events use their protocol record size. Setup has its separate prefix/body.

The parser waits for a complete record before queueing it. A payload fragment
beginning with an error/event-looking byte remains payload. Notifications cannot
release a client on only the first header of a multipart reply.

`FlushAllOutput` needs no buffered native flush. `FlushClient` forwards any
provided extra bytes through the same framing path; it is not allowed to discard
them merely because ordinary output is immediate.

### 5.6 Cooperative waiting

[waitfor_wmb.c](../tinyx/os/waitfor_wmb.c) preserves timers and registered
`BlockHandler`/`WakeupHandler` calls. KDrive's `pollEvents` runs through that seam;
its 20 ms maximum must not replace an earlier timer/deadline. Predicate waits
release `xsrv` when there is no eligible client, rather than spinning on ignored
clients or dropping the input hooks.

### 5.7 Grabs and flow control

`OnlyListenToOneClient`, `ListenToAllClients`, `IgnoreClient`, `AttendClient` and
impervious state affect readiness. The three-client test includes an impervious
contender that reaches the **native contended-grab/replay** path, not merely a
peer filtered out before dispatch. Delayed XTEST input exercises the native
sleep/ignore/timer/attend/reset path independently.

## 6. The Event Path

Backend key/pointer events pass through HAL -> KDrive -> MI/DIX -> framed output
-> bridge event list -> native Xlib queue -> client API. `Expose` from actual
window mapping uses the same server output path.

### 6.1 Queue sizing and overflow

The bridge's default event limit is 128. At overflow it drops the oldest motion
first; 1,000 motions retain the newest 128. Critical events are not discarded to
admit motion. If no replaceable motion exists, critical storage may grow within
the 4,096-record and aggregate byte bounds; exhaustion fails the connection
rather than blocking `xsrv`.

The wire/request record limit is 1 MiB and queued packet storage is 2 MiB per
connection. These bound bridge storage, not every allocation in retained Xlib
or an application's own event cache.

## 7. Error Path

Wire errors are distinct from events/replies. A matching synchronous error reaches
`_XReply` with native special-case behavior; other errors reach `_XError` through
ordinary pumping, including the next `XNextEvent`. Native async reply handlers
observe the correct `last_request_read` before callbacks.

Framing, OOM, lease misuse, queue limits and missing replies have explicit
diagnostics. They never manufacture a normal response or silently drop a
critical record.

## 8. Sequence Numbers

X11's 16-bit wire serial is unchanged. Bridge packets additionally carry full
64-bit serial metadata, preserving the original serial on replay.
`dpy->request`, `last_request_read` and native callback/error serial context use
that full value. Widening a 16-bit reply alone cannot distinguish more than
65,535 outstanding no-reply requests; the 65,540-NoOperation test covers that
case before its synchronization reply.

## 9. Risks and checks

| Risk | Implemented mitigation/check |
|---|---|
| R1 Multipart payload classified as events | Streaming parser, fragmented-output unit test, variable property wire comparison |
| R2 Builder movement/stale request pointer | Correct offsets, no leased growth, OOM tests, actual large requests, ASan |
| R3 Lost input hooks | Real backend input plus measured >=30 Hz polling |
| R4 Replay lifetime/flow errors | Native contended grab and delayed input tests |
| R5 Missing reply/deadlock | Five-second diagnostic failure in every build |
| R6 Nested server execution | Only `xsrv` enters DIX |
| R7 Fabricated setup mismatch | Real native handshake parsed by retained Xlib |
| R8 Motion/critical overflow | Newest-motion retention and bounded fail-fast critical storage |
| R9 List ownership/lifetime | Owned packet type, accounting, close cleanup, allocation-failure tests |
| R10 Sequence rollover | Full internal serial metadata; >65,536-request test |
| R11 SDL/native Xlib interposition | Hidden private archive, link order, no exported private Xlib API |

SDL may use system Xlib and sockets **below HAL**. That is not the subject's
transport. Whole-process socket counts on a desktop cannot distinguish them;
check the subject source cohort, private symbols and headless behavior instead.

## 10. Verification and implementation order

The implementation proceeded through raw setup/NoOperation/error completion,
retained Xlib open/window/draw, replies/events/errors, native flow control, large
requests/serials/IDs, failure tests and independent stock-Xlib wire capture.

The integrated suite has 27 gates including retained HAL/KDrive/P4 regressions.
Debug, Release/XRGB8888, ASan/strict UBSan, and odd-width/padded formats pass.
The final sixty-second client Valgrind run has zero errors and zero lost bytes;
server/font caches remain reachable at process exit. See
[41-](41-verification-plan.md) for precise evidence and
[reference/](reference/README.md) for stock-Xlib capture/normalization.

## 11. Prior transport-shape proof

The independent [SDL fiber example](../linux_hosted/sdl_fiber_example_v2/README.md)
already demonstrated post -> notify -> predicate wait with one render fiber,
three clients, owned list records and 40/40 completions without timeout.
That proved the runtime handshake before P5; it was never evidence that Xlib,
native server dispatch or protocol framing worked. The real P5 tests now supply
that separate evidence.
