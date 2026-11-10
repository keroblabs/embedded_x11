# P5 in-process Xlib transport

The retained Xlib API now talks to actual TinyX through a bounded in-process
bridge. Clients are cooperative fibers in the same address space. All client
registration, dispatch, timers, input and resource teardown run on `xsrv`;
the client never calls a DIX request handler directly.

## Run the real client

```sh
cmake -S . -B build-p5 -DWMB_TINYX_BUILD_SERVER=ON \
    -DWMB_KDRIVE_BUILD_TESTS=ON -DWMB_SDL_BUILD_TESTS=ON
cmake --build build-p5
./build-p5/bin/wmb_host --client-test
ctest --test-dir build-p5 --output-on-failure
```

Dependencies are the [P4 server prerequisites](../../tinyx/wmb/README.md), plus
Python 3.10+ for the wire comparison. Source CMake files stay in version control;
generated headers, tables and downloaded dependencies stay in the build tree.

`--client-test` opens a white client window, draws a black rectangle and
`WMB TEST OK`, receives real backend keyboard/mouse events, holds the window
for ten seconds, verifies framebuffer pixels/idle CPU/polling, then closes it.
The [captured framebuffer](../../docs/reference/tinyx-client-rgb565.png) is from
the actual desktop run, not an application-drawn substitute for X rendering.

The window is intentionally unmanaged. JWM/decorations are P7, not P5.

Other bounded host modes:

| Mode | Purpose |
|---|---|
| no arguments | P4 root window until the display is closed |
| `--self-test` | Existing ten-second P4 initialization/root/idle gate |
| `--raw-test` | Real connection setup, raw NoOperation, and BadRequest completion |
| `--flow-test` | Three clients, contended impervious grab/replay, blocked peer, delayed XTEST input, BIG-REQUESTS, serial wrap, XC-MISC refill |
| `--timeout-test` | Deliberately expect a reply to NoOperation; diagnose after five seconds and exit with test code 77 |
| `--client-soak` | Sixty-second client lifetime/event wait for memory analysis |
| `--wire-test FILE` | Execute the shared ten-call script and capture its wire bytes |

Use `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software` for headless runs.

## Build and ABI boundary

[The client CMake target](../../libX11/CMakeLists.txt) freezes 256 translation
units: 253 retained native API/support files plus the transport, close adapter
and explicit profile support. XCB, native locking and locale/resource-database
backends are not selected.

Use the **matching** `libX11/include/X11` client headers. The older `X11/Xlibint.h`
copy does not match these sources. The client target exports the correct include
order and `WMB_INPROC`; its private header rejects `_XSERVER64`.

The server still uses `wmb_tinyx_project`/`wmb_tinyx_config`. Never propagate that
configuration prefix to client translation units. Shared bridge structures contain
standard C types and opaque server pointers, not `Display` or `ClientRec`.

The in-process Xlib archive is hidden from dynamic symbol resolution and is
linked **before SDL's native backend dependencies**. Otherwise SDL's own system
Xlib can bind to the in-process implementation, or drag optional native APIs
into the wrong archive. The host
[link target](../../linux_hosted/apps/wmb_host/CMakeLists.txt) demonstrates the
ordering, server/KDrive rescan group and archive visibility.

The no-native-X-transport guarantee is above HAL. SDL can own system X/Wayland
connections and, on this host, even dummy video initializes D-Bus/udev sockets.
Use syscall call-stack attribution rather than a blanket whole-process socket
count. The traced client run attributes every native IPC call to SDL.

Keep application Xlib code in its own client-cohort target, as
`wmb_xlib_test_client` does. The host communicates with it through a standard-C
result/control structure, not mixed client/server X headers.

## Ownership and progress

- One outstanding buffer lease per connection; up to 32 live connections.
- Requests remain in the client's builder while `xsrv` consumes them. Building,
  growing or reusing a leased buffer is rejected.
- `Data`, `Data32`, `BufAlloc` and request allocation preserve complete batches;
  no partial request is flushed merely because the builder fills.
- `_XSend` is a real append/send operation: retained APIs such as named-color
  allocation call it directly. It does not prematurely deliver a synchronous
  error before `_XReply` establishes its target.
- Completion is observed both at the next read and at the server wait boundary.
  Error exits and grab kickout do not necessarily perform another read.
- Suspended requests keep their lease; replay storage lives through handler return.
  Existing `Dispatch()` and native grab/sleep logic remain in charge.
- Output is framed across writes. Reply payload fragments cannot become fake
  events. Errors, replies, events and setup have distinct framing.
- Full serial metadata accompanies copied packets internally, so wire sequence
  rollover and replay do not lose the client's full request serial.
- Incoming records are limited to 1 MiB and queued packet storage to 2 MiB per
  connection. Event queues retain the newest motions, never discard an older
  critical event to admit motion, and fail/close rather than block the server
  when critical storage is exhausted.
- Reply/request waits diagnose failure after five seconds. Empty event waits
  are predicate-blocked indefinitely; KDrive polling retains its bounded cadence.

The client owns its `Display`: close it before its application fiber exits.
Foreign OS-thread callers, transparent process-exit semantics, and the external
wrapper/ABI remain P8/lifecycle work. Native `XInitThreads` is not supported.
This profile uses core fonts/C-locale behavior, not XIM, fontsets, XKB, Xcms or
resource-database files. Standard named colors still use the server path.

## Verification

The P5 tests cover:

- Actual setup and raw dispatch, including the error completion edge.
- Window mapping/Expose, drawing, image readback, attributes, named-color failure,
  4 KiB and 32-bit property payloads.
- Async errors delivered during the next event read.
- 1,000 queued motions retaining the newest 128; critical-event preservation.
- Backend keyboard and mouse delivery through KDrive/MI/DIX.
- Three-client grabs, native ignored-client replay and delayed input.
- Automatic BIG-REQUESTS negotiation with a 280,000-byte property.
- More than 65,536 requests and allocation beyond the initial XID range.
- Builder growth, allocation-failure preservation, lease rejection and padding.
- A bounded, explicitly diagnosed missing reply.
- Independent stock-Xlib wire comparison and negative tests of its normalizer.

The existing P4/HAL/KDrive gates remain in the same suite. Repeat with
ASan/strict UBSan and XRGB8888/odd native geometry. The actual 500-point line test
exposed negative signed shifts in the framebuffer Bresenham path; its small
arithmetic repair is guarded by `WMB_DEVEL`, not hidden by disabling UBSan.

Valgrind example (sixty-second memory gate; CPU performance is measured without
Valgrind instrumentation):

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    valgrind --max-stackframe=65536 --error-exitcode=42 --leak-check=full \
    --errors-for-leak-kinds=definite,possible ./build-p5/bin/wmb_host --client-soak
```

For nearby concurrent fiber stacks, Memcheck's default 2 MiB stack-switch
heuristic misidentifies a switch as one large frame and reports invalid stack
accesses. The 64 KiB threshold distinguishes this test workload's 128 KiB
worker stacks; it does not suppress memory errors. The final sixty-second
client and multi-client flow checks have zero errors/lost bytes. About 42 KiB
of server/font caches remain reachable at process exit.

See [the wire-reference procedure](../../docs/reference/README.md). Protocol
padding is undefined, so the comparator zeroes only documented padding and
normalizes resource IDs/sequence fields; all defined payload bytes still compare
exactly. Deliberate payload corruption is rejected by its tests.

Remaining phases are P6 portability closure, P7 JWM, P8 external ABI/stability,
and P9 hardware/freestanding bring-up. P5 does not claim those gates.
