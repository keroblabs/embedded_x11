# Hosted ABI validation scaffold

These Linux-only executables validate the portable application ABI. None is a
dependency of `wmb_desktop`, `wmb_api_runtime`, TinyX, retained Xlib or the HAL.
Unix sockets, pthreads, process control and SDL test injection stay here, not in
the portable windowing cohorts.

## Run a separate application

Build the hosted server as described in [the API guide](../../transport/api/README.md).
In two terminals:

```sh
./build-p4/bin/wmb_rpc_host /tmp/my-wmb-session.sock
./build-p4/bin/wmb_hello /tmp/my-wmb-session.sock
```

Use a fresh endpoint owned by your session: the server does not unlink an
existing path. It creates a private socket, and removes only its own endpoint
on clean shutdown. Closing the SDL window requests ordered shutdown. The
bounded server harness also stops after 120 seconds.

The application links the small SDK, a host proxy/copy codec and libc, **not**
full subject/system Xlib, TinyX, JWM or SDL. The core
[hello source](../../apps/wmb_hello/hello.c) has no socket or pthread calls;
[client_main.c](client_main.c) performs the host-only binding.

`wmb_hello_v1` is compiled against independently frozen signatures/order and a
different generated wrapper archive. It runs on the real v2 service; a current
hello refuses a service advertising only v1 before opening a Display.

## What crosses the socket

- A little-endian, length/opcode/sequence-checked frame, bounded to 1 MiB.
  Handshake data records ABI magic/version/count, pointer/long/event layout and
  byte order; incompatible data models are diagnosed.
- Scalar values and copied arrays, strings and output data.
- Typed, per-connection, generation-checked IDs for Display/Screen/GC objects.
  **No process address crosses the wire.**
- Visual snapshots contain public scalar fields, with `ext_data = NULL`.
  Window attributes reconstruct client-local Screen/Visual objects.
- Masked structures read only selected fields. Core events are encoded field
  by field, never by copying `XEvent` padding or its native Display pointer.
  `XEvent.display` is reconstructed locally; extension cookies are not v1.
- Format-32 properties pack protocol words and rebuild native-long arrays,
  preserving retained Xlib's LP64 sign-extension behavior.

The server decodes a typed request and submits it through the API-level MPSC
gateway. A socket reader never executes Xlib, DIX, or scheduler operations.
Owned native outputs are copied then freed through the gateway. Every advertised
slot has generated copy/handle semantics; there are no success-shaped stubs.
Client-local `XFree`, Visual/Screen identity and put-back events stay local.
`XPending`, `QLength`, event matching and `XSync(..., True)` account for that
local event queue.

The scaffold bounds references to 128 objects, tracked top-level windows to
128, readers to 16, and local put-back events to 4,096. The runtime's shared
fiber/worker budget is an additional limit. These are explicit host-validation
bounds, not claims about an embedded loader or an unbounded service.

Readers disable cancellation while arguments are borrowed. Disconnect and stop
drain live calls before reclaiming connection state; created GCs are released,
managed windows are withdrawn, and Displays close on their runtime workers.
Socket shutdown only releases the reader after gateway stop is observable.
This scaffold is not a sandbox, an authenticated production service, or a
replacement for the target loader.

## Tests

```sh
ctest --test-dir build-p4 -R '^tinyx_p8_' -LE endurance --output-on-failure
ctest --test-dir build-p4 -R '^tinyx_p8_cycles$' --output-on-failure
ctest --test-dir build-p4 -R '^tinyx_p8_hour$' --output-on-failure
```

Important independent evidence:

| Test | What it actually runs |
|---|---|
| `tinyx_p8_native` | Genuine typed same-address tables, frozen/current opaque SDK applications and two preempting foreign callers |
| `tinyx_p8_sdk` | Actual archive bytes/exports, C/C++ macro use, opacity compile failures and native-table misuse from a real OS thread |
| `tinyx_p8_process_bridge` | Separate current/legacy hello processes, real JWM frames/text/framebuffer pixels, two process readers, compatibility and malformed-frame rejection |
| `tinyx_p8_gateway_cancel` | NULL Display/Screen and busy Display rejection; event waits and `UINT32_MAX` native/foreign yields stop without abandoning borrowed state |
| `tinyx_p8_rpc_startup` | Failed listen releases its own bound endpoint while preserving a pre-existing file |
| `tinyx_p8_rpc_codec` | Every core event, native event-mask matching, masked/uninitialized input fields and LP64 property conversions |
| `tinyx_p8_font_allocation` | Real pinned zlib callbacks, malformed input and allocation/metadata OOM cleanup |
| `tinyx_p8_cycles` | 10,000 fully managed window lifecycles, unchanged settled WMB bytes/blocks/peak while another owner allocates |
| `tinyx_p8_hour` | A real hour of SDL -> HAL -> KDrive -> DIX -> SDK keyboard/mouse traffic with heap, clock, progress and canary checks |

The native harness accepts `--cycle-smoke`, `--cycles`, `--stability SECONDS`,
`--sampling-test`, or explicitly instrumented `--memory-test SECONDS`. A short
run is not a substitute for the hour gate.

The first long attempt exposed a **harness** timestamp error: a WM draw could
occur after the root's pre-yield timestamp, making unsigned subtraction look
like starvation. `tinyx_p8_sampling` reproduces that ordering against a real
WM draw and verifies fresh sampling. Normal timing gates were not relaxed.

Use the existing `--max-stackframe=65536` Valgrind setting. Instrumented memory
mode skips only diagnostic reads of inactive stack frames, not memory checks
or scheduler canaries. Actual separate v1/v2 clients and the complete core
event/masked codecs are also checked directly under Memcheck, without error
suppressions.

ABI generation and protocol metadata come from
[api.def](../../transport/api/api.def) through
[generate_api.py](../../tools/generate_api.py) and
[generate_rpc.py](../../tools/generate_rpc.py). The macro inventory and
frozen v1 contract are checked-in generation gates.
