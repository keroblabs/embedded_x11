# Versioned WMB application API

This is the portable, same-address-space application boundary above retained
Xlib. It is not a replacement display/input HAL, and it does not add sockets or
threads to TinyX. Hardware still implements [the HAL contracts](../../hal/).

## What is supported

[api.def](api.def) is the authoritative list of signatures, ownership, counted
arrays and masked structures:

- **Core v1:** 122 typed slots, including every one of the 51 Display/Screen
  dereferencing macros in the canonical vendored Xlib header.
- **Current v2:** the identical v1 prefix plus `wmbApiRevision()`.
- Window creation/configuration, GCs, primitive drawing and core-font strings,
  pixmaps, core events, atoms/properties, colors, glyph cursors, normal hints,
  protocol lists, tree/focus/pointer queries, grabs and cooperative yield.
- Rich font/image objects, extension event cookies, application callbacks and
  native descriptor polling are **not** v1 features. They have no placeholder
  slots. Additions must be implemented and appended, not silently stubbed.

The [SDK](../../wrapper/libX11-app/README.md) makes both `Display` and `Screen`
incomplete types. `Visual` remains public scalar data. Code using ordinary
`X11/Xlib.h`, `DefaultScreen`, `RootWindow` and the other supported macros works
through generated function replacements. `ConnectionNumber()` is the retained
in-process display's value, normally **-1**, not a host bridge descriptor.

## Runtime integration

Initialize the shared runtime, then call `wmb_main()`. Continue calling
`wmb_poll()` and yielding on the same runtime thread. The component remains
once-only; this API does not introduce reinitialization or multiple servers.

Before using either ABI table, call `wmb_gateway_start()` once from that owner
thread, after readiness, with [wmb_gateway_port_t](wmb_gateway.h):

| Callback | Contract |
|---|---|
| `is_runtime_thread(context)` | Identify the owner **without consulting fiber globals from a foreign thread** |
| `wait_ms(context, ms)` | Wait/yield on the foreign platform task; thread-safe, no WMB scheduler calls, no cancellation/termination |
| `tick_ms(context)` | Thread-safe monotonic wrapping millisecond clock |

These are platform-supplied callbacks, not POSIX dependencies in the windowing
library. [The hosted scaffold](../../linux_hosted/abi_bridge/server_main.c)
supplies pthread identity and hosted waits below this boundary.

Choose the table according to the calling context:

| Table | Caller | Execution |
|---|---|---|
| `wmb_api_native_table()` | An application fiber on the runtime thread | Typed calls into retained Xlib on that fiber |
| `wmb_gateway_table()` | A foreign OS/platform task | Submit an API call; a runtime-owned worker executes Xlib |

Inject the chosen immutable pointer into the application and call `wmb_bind()`
before any SDK operation. Its lifetime must exceed all calls. Binding, rebinding
and application teardown must not race with wrapper calls. Never give the
native table to an OS thread: misuse is diagnosed before Xlib or fiber state
is touched.

The foreign table is not a lock around Xlib. Producers use a C11 atomic MPSC
mailbox and borrow their arguments until completion; the owner polls it from
`wmb_poll()`. Each Display has a lazy, persistent worker fiber. **All DIX still
executes on `xsrv` through unchanged upstream `Dispatch()`.** P5's byte transport
remains single-runtime-thread code.

### Capacity, ownership and cancellation

- One outstanding call per Display. Two different Displays may make progress
  concurrently; simultaneous use of the same Display is explicitly rejected.
  NULL Display/Screen calls cannot claim an empty worker slot.
  Borrowed Screen/GC/Visual objects must not outlive their Display or race its
  close.
- There are at most four gateway Display workers and 64 admitted calls. Worker
  creation also needs a shared fiber slot. `FIBER_MAX` remains **8**, not an
  assumed private budget: root + server + WM consume three slots. Exhaustion
  diagnoses the failure and `XOpenDisplay()` returns NULL.
- Foreign callers must disable cancellation while a call is borrowed. Neither a
  timeout nor disconnect may abandon its stack descriptor or buffers.
- Ordinary gateway calls have a six-second fatal service deadline; retained
  transport waits also have their own bounded failure policy. An executing
  `XNextEvent()` may wait indefinitely for input, but checks stop every 10 ms.
  `wmbYield()`, including a `UINT32_MAX` wait, is also stop-responsive.
- `wmbYield()` returns negative on stop. `XNextEvent()` returns -1 on stop and
  does not produce an event. Close resources/Displays and return.
- Shutdown stays **WM -> applications/gateway Displays -> server/HAL**. Existing
  Display cleanup is admitted until all calls/workers drain; an atomic admission
  close prevents a late producer from racing final shutdown. The host must keep
  pumping the runtime during this drain.
- This is not a recoverable error-callback ABI. Native external-client X errors
  are diagnosed and fatal. The RPC scaffold can copy gateway error status to
  its client, whose wrapper diagnoses/fails. Invalid input is not converted into
  a successful Xlib result.

## Application data lifetime

Use the ordinary selected Xlib conventions:

- `XGetAtomName`, `XQueryTree` children and `XGetWindowProperty` data are owned
  results: release them with `XFree()`.
- Display/Screen/Visual accessors, `ServerVendor`, `DisplayString` and default
  GCs are borrowed. Do not free or modify them.
- Free created GCs, pixmaps and cursors before closing their Display.
- Property format 32 uses **native `long`/`unsigned long` elements**, including
  on LP64, not a packed `uint32_t[]`. Interpret their low 32 bits; retained
  `_XRead32` sign-extends returned protocol words.
- Only members selected by masks/flags need initialization in `XGCValues`,
  `XWindowChanges`, `XSetWindowAttributes` and `XSizeHints`. The host codec never
  serializes their other members or padding. Input `XColor` requires RGB only.

The [hello application](../../apps/wmb_hello/hello.c) uses these conventions,
including proper withdrawal before closing a live managed window. The
[native ABI probes](../../linux_hosted/abi_bridge/sdk_probe.c) independently
execute both frozen v1 and current wrappers on real application fibers.

## Building and verifying

From the repository root:

```sh
cmake -S . -B build-p4 -DWMB_TINYX_BUILD_SERVER=ON \
    -DWMB_KDRIVE_BUILD_TESTS=ON -DWMB_SDL_BUILD_TESTS=ON
cmake --build build-p4
ctest --test-dir build-p4 -LE endurance --output-on-failure
ctest --test-dir build-p4 -R '^tinyx_p8_cycles$' --output-on-failure
ctest --test-dir build-p4 -R '^tinyx_p8_hour$' --output-on-failure
```

The hour gate runs for **3,600 actual input-processing seconds**, after startup
and warmup, not accelerated scheduler time. Do not run builds, sanitizers or
unrelated CPU-heavy jobs during normal timing certification. Failed timing
attempts are not performance evidence.

The cycle workload creates, maps, verifies JWM reparenting, withdraws and
destroys **10,000 managed windows after 32 warmup cycles**. It compares settled
WMB bytes/blocks and peak bytes while another generic allocation owner retains
100 KiB. Its busy root loop is workload generation, not an idle-CPU measurement.
`--cycle-smoke` exercises 100 cycles in the normal test suite.

Generic [allocation accounting](../../framework/library/allocation.h) tracks
payload plus tracking-node bytes, not libc arena overhead or process RSS.
Owner 1 is the selected windowing cohorts, owner 2 the independent test tenant.
The pinned font's zlib allocations use explicit tracked callbacks. Backend
framebuffers and port stack reservations are reported separately.

`fiber_stack_usage()` / `hal_task_stack_usage()` report sentinel-touched stack
HWM including the hosted port context; main's unknown bounds are not reported as
zero. This is a touched-memory estimate, not proof about unwritten automatic
arrays or a different target ABI. Scheduler canaries remain active on every
switch and identify the offending FCB slot/generation.

For Memcheck use `wmb_abi_host --memory-test 60` with the existing
`--max-stackframe=65536` setting. That explicit instrumented mode lowers input
rate and disables **only inactive-stack HWM sampling**, because Memcheck marks
expired stack frames inaccessible. It does not suppress memory errors, disable
canaries, or certify normal latency/heap peaks. Normal runs provide stack sizing
evidence; memory runs provide error/leak evidence.

## Extending the ABI

```sh
/usr/bin/python3 tools/generate_api.py
/usr/bin/python3 tools/generate_api.py --check
/usr/bin/python3 tools/test_generate_api.py
```

Edit [api.def](api.def), append slots with a new `since` version, and implement
all native/gateway/host copy semantics. Generation fails on an unhandled
parameter kind or a new dereferencing macro. Never edit generated C/headers,
the macro inventory or [the frozen v1 prefix](../../docs/reference/api-v1.json).
`--freeze-v1` is one-time creation and refuses to overwrite an existing baseline.

The current raw archive is **15,924 bytes**, below the strict 16,384-byte gate.
The byte/export, C/C++ opacity, version-rejection and foreign-thread misuse
checks run in `tinyx_p8_sdk`; generated prefix/ownership regressions run separately.

See [the ABI contract](../../docs/22-entry-point-abi.md),
[host-only process validation](../../linux_hosted/abi_bridge/README.md) and
[target measurements](../../docs/31-target-build.md).
[P9a](../../freestanding/README.md) now verifies actual ARM fixed-base placement,
the separate SDK binding, no relocations and the reviewed target-library floor.
Real board loader/runtime execution, synchronization and hardware certification
remain P9b.
