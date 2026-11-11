# 22 - Entry-Point ABI

## 1. Purpose

P8 provides a typed application boundary above retained Xlib. An application
links a thin SDK, not another full Xlib or another X transport. The same contract
can be supplied to a cooperative application fiber or to a foreign platform
task. Display/input hardware still implements the independent HAL.

The [runtime integration guide](../transport/api/README.md),
[standalone SDK guide](../wrapper/libX11-app/README.md) and
[host validation guide](../linux_hosted/abi_bridge/README.md) document commands,
ownership and failure policies. [D-56](90-decisions.md) records the approved
core-v1 scope and corrections to the original design sketches.

**Hosted P8 sign-off passed**, including the actual 3,600-second input run.
[The host verification record](reference/p8-host-verification.json) preserves
its independent wall timer, unchanged binary hash and measurements.

### 1.1 What this document is not

- The [P5 in-process X transport](21-inprocess-transport.md).
- A socket-based embedded architecture.
- A completed fixed-address loader or freestanding/hardware certificate: those
  remain [P9](31-target-build.md).

## 2. Two-Block Design

```text
application, using ordinary selected Xlib source names
    -> libX11-app.a: typed thunks + explicit wmb_bind(table)
        -> native table: application fiber -> retained Xlib
        OR
        -> foreign table: API-level MPSC mailbox
            -> runtime-owned Display worker -> retained Xlib
                -> P5 transport -> xsrv -> upstream Dispatch()
```

Only the selected wrapper functions and `wmb_bind` are exported by the SDK
archive. Stable compact linker names avoid duplicating long Xlib names in the
archive; compiler external-name pragmas do not rename C field identifiers.
The host can therefore link an SDK application without interposing its wrapper
on the server's actual Xlib.

Internal retained-Xlib clients, including JWM, need no SDK and retain their
existing P5/P7 path.

## 3. The Vtable

### 3.1 Layout rules

| Rule | Contract |
|---|---|
| A1 | Append-only: preserve every existing signature, position and ownership rule |
| A2 | First 16 bytes: `uint32_t magic, version, count, layout` |
| A3 | `count` is the number of populated typed function slots |
| A4 | Public Xlib/scalar types only; no server or private transport objects |
| A5 | Both `Display` and `Screen` are genuinely incomplete application types |
| A6 | No varargs or application callbacks in core v1 |

The schema is [api.def](../transport/api/api.def), not a manually maintained
prototype sketch. Its native initializer is compiled against the actual retained
headers with incompatible-pointer diagnostics enabled.

### 3.2 Structure and selected scope

[wmb_api.h](../transport/api/wmb_api.h) is generated. The fixed prefix is:

```c
typedef struct {
    uint32_t magic, version, count, layout;
} wmb_api_header_t;
```

It is followed by individually typed function pointers, never `void **` or
casts between incompatible function pointer signatures.

- Frozen v1: **122 slots**, including all **51** Display/Screen macro accessors.
- Current v2: the unchanged v1 prefix plus `uint32_t wmbApiRevision(void)`.
- Core window/GC/drawing/pixmap/event/property/color/cursor/hint/query operations
  are implemented. Core-font string drawing remains available.
- Rich font/image objects, extension event cookies and application callback
  APIs are deferred append-only extensions, not advertised NULL/stub slots.

The independently frozen [v1 schema](reference/api-v1.json) cannot be rewritten
by ordinary regeneration. Its SDK/header/wrapper are built separately for both
genuine same-address tests and the separate-process v1 application.

### 3.3 `wmbYield` and event waits

Both tables provide `wmbYield(max_ms)`. A native call yields the application
fiber; a foreign call waits using the supplied platform callback, never the
fiber scheduler. Long waits are sliced so even `UINT32_MAX` is stop-responsive.

`XNextEvent` blocks until a core event exists, but returns -1 on component stop
without producing an event. `wmbYield` also returns negative on stop. Ordinary
applications may use `XPending` plus `wmbYield` in a nonblocking loop.

`ConnectionNumber` returns the retained in-process display's descriptor
value, normally -1. It is not a descriptor on which an application should
`select` or `poll`.

## 4. Placement and Discovery

### 4.1 Current discovery and future section placement

P8 uses explicit pointer injection and `wmb_bind(const wmb_api_t *)`.
`wmb_api_native_table()` and `wmb_gateway_table()` supply trusted immutable
same-address tables after owner configuration.

P9 must place the chosen external table in `.wmb_api`, define the actual
`WMB_LOAD_BASE`, and verify agreement with loader/application discovery. The old
example address `0x80100000` was a placeholder, not a P8 implementation.

### 4.2 Validation

Binding copies only the readable 16-byte header before rejecting an older
version or insufficient slot count. It then verifies the C data model and every
required typed function pointer before accepting the table.

A newer append-only version is accepted. Failed binding clears the old
binding; unbound API use diagnoses/fails instead of calling NULL. Callers must
provide genuinely readable table storage matching its advertised count and
must not race binding with calls.

Compile and runtime tests include:

- A header-only older/short table, including sanitizer validation that missing
  fields are not read.
- Wrong magic/layout, insufficient version/count and a missing required slot.
- Independently compiled v1 on real v2, plus acceptance of a future appended
  provider.
- Rejection of the native table on an actual foreign thread before Xlib or
  fiber globals are accessed.

### 4.3 Non-PIC

Target linker placement, load-base agreement, compiler/runtime floor and absence
of dynamic relocations remain P9 gates. A working RPC scaffold does not prove
any of them.

## 5. The Wrapper Library

### 5.1 Generation

Run [generate_api.py](../tools/generate_api.py). It reads `api.def` and the
frozen v1 prefix and emits the public tables, native/foreign typed adapters,
thin wrappers, compatibility/namespace headers, macro-use tests and host RPC
metadata/adapters. `--check` requires byte-identical checked-in output.

Changing the old prefix, introducing an unhandled RPC parameter kind or adding
an unhandled dereferencing macro fails generation.

### 5.2 Macro replacements

The generator audits the canonical
[Xlib.h](../libX11/include/X11/Xlib.h), including transitive macro aliases,
and produces [xlib-display-macros.txt](xlib-display-macros.txt).

The SDK automatically includes the generated compatibility header. The
`WMB_APP_OPAQUE` profile omits Display internals and exposes only a forward
declaration of Screen. Actual Xlib sees the corresponding full project Screen
tag without changing layout; undefined native-reference preprocessing remains
identical. Private-header mixing and `XLIB_ILLEGAL_ACCESS` are explicit errors.

Tests compile every macro in C and C++ with selectors defined as one, zero and
empty. Separate negative TUs prove `sizeof(Display)`, `sizeof(Screen)` and field
access really fail; this is not merely an unchecked application convention.

### 5.3 Size and packaging

The **actual raw `libX11-app.a` is 15,924 bytes**, below the strict **16,384**
limit in both Debug/Release hosted builds and a standalone SDK build. This
includes the archive index, symbol/relocation tables and object headers, not
just text size. Instrumented sanitizer archives are not production size claims.

The SDK builds standalone without SDL, framework, font fetching or full Xlib.
Use its CMake target in another project so matching headers and both generated
forced includes travel with the archive.

## 6. Hosted Validation

The Linux-only [bridge](../linux_hosted/abi_bridge/README.md) uses framed Unix
IPC to drive the same API. The separate hello executable links only the SDK,
host proxy/copy codec and libc.

### 6.1 Wire format and object lifetime

Frames have bounded lengths, opcodes, sequence numbers and explicit ABI/layout/
byte-order negotiation. Display/Screen/GC values are typed, generation-checked
per-connection handles, never native addresses.

Strings, arrays, Visuals, hints, colors, window attributes and every supported
core event are copied field by field. Masked/uninitialized fields and structure
padding are not read. Format-32 properties pack 32-bit protocol words and rebuild
native-long arrays, preserving retained LP64 behavior.

The reader submits through the gateway; it never runs Xlib or DIX. Native
owned outputs are freed through that gateway after copying. Proxies preserve
borrowed identity/lifetime, local put-back events, `QLength`, `XPending` and
discarding `XSync`. Disconnect drains calls before reclaiming buffers and closes
GCs/managed windows/Displays on runtime-owned contexts.

The tests verify real JWM parent/frame geometry, actual green framebuffer
pixels and drawn core-font text, not just successful RPC return values.
[The actual SDL x11/software capture](reference/tinyx-abi-external-rgb565.png)
shows the separate application's managed window.

## 7. Threading and Ownership

Threadless Xlib and the global fiber runtime are **not safe on foreign native
stacks**. Making only the byte-queue producer atomic is insufficient.

| Rule | Implemented handling |
|---|---|
| E1 | Foreign producers use only C11 atomics and platform-safe callbacks; Xlib runs on runtime fibers |
| E2 | Each external Display has an ordinary native client connection, owned by a lazy gateway worker |
| E3 | Foreign stack use is the small admission path; Xlib stack use is measured on its runtime worker |
| E4 | MPSC API-call admission and a polled mailbox serialize ownership without a lock held across waits |

There are four lazy workers and 64 admitted calls, constrained further by the
shared `FIBER_MAX=8`. One outstanding call per Display is enforced; separate
Displays progress independently. No admin fiber or worker is allocated when
the gateway is unused.

Requests borrow caller storage until completion. Foreign cancellation is
forbidden while borrowed; ordinary service deadlines are fatal rather than
returning and leaving live pointers behind. An executing event wait can block
for input but stops cooperatively.

Shutdown remains WM -> applications/gateway Displays -> server/HAL, with
atomic final admission closure. Normal failure policy is explicit and
callback APIs are deferred. See the integration guide for exact contracts.

## 8. Versioning

| Application | Provider | Result |
|---|---|---|
| v1 / 122 slots | v1 / 122 slots | Works |
| independently frozen v1 / 122 | v2 / 123 | Works; all prior types/order/ownership preserved |
| v2 / 123 | v1 / 122 | Bind fails before reading missing fields or opening a Display |
| older supported prefix | newer appended version | Works when the complete required prefix is present |

Append implemented slots and increase version/count. Changing an old signature,
layout or ownership rule requires a new major contract/magic; reordering or
removing a slot is not allowed.

## 9. Open Items

| Item | Status |
|---|---|
| Typed versus flat function-pointer array | Closed: typed fields, D-28 |
| All Display/Screen macro replacements | Closed: 51 generated/audited accessors, Q20 |
| Foreign threading boundary | Closed for the selected profile: API-level gateway, D-56 |
| Rich `XImage`/font/callback support | Deferred append-only, not a v1 capability |
| Transparent multi-instance/reinitialization | Not implemented |
| Fixed address, target memory/atomic/libc floor, loader and hardware | P9; actual target still required |

## 10. Verification

| Gate | Evidence |
|---|---|
| E-V1 / S9 | Regeneration check plus frozen-prefix and unknown-shape mutation tests |
| E-V2 | Exact SDK symbol inventory; stable linker thunks plus `wmb_bind` only |
| E-V3 | Raw archive byte gate, 15,924 < 16,384 |
| E-V4 | Separate process, real JWM frame/text/pixels and actual SDL capture |
| E-V5 | Frozen v1 on v2 in both true same-address and process tests |
| E4 | Two preempting callers and two process readers, no lost property results; ownership/cancellation diagnostics |
| V-P8-4 | 10,000 managed lifecycles; settled WMB bytes/blocks/peak unchanged while another owner allocates |
| V-P8-5 | Passed actual 3,600,000-ms input endurance; 3,604.442 independent wall seconds, flat peak and no starvation/canary trip |
| V-P8-6 | [Recorded heap/stack/image measurements](31-target-build.md), not MCU guarantees |
| E-V6 / E-V7 | P9 section placement/load-base and relocation proofs |
