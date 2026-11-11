# Thin Xlib application SDK

Applications link `wmb_app_wrapper` (`libX11-app.a`), **not full Xlib**.
The library has 123 typed forwarding entry points plus `wmb_bind()`; the v1
archive has the frozen 122-slot prefix. The raw current archive is 15,924 bytes
with the documented GCC build, not a `size` estimate.

## Standalone build

No SDL, framework, font dependency, Python, running server or system Xlib is
needed to compile the checked-in generated SDK:

```sh
cmake -S wrapper/libX11-app -B build-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdk
stat -c '%s %n' build-sdk/libX11-app.a
```

The matching canonical protocol/public headers live in this checkout. Do not
substitute a different system `Xlib.h` or the older root private copies.

## Another CMake project

```cmake
add_subdirectory(/path/to/embedded_x11/wrapper/libX11-app wmb-sdk)
add_executable(my_application main.c)
target_link_libraries(my_application PRIVATE wmb_app_wrapper)
```

The target automatically supplies:

- Matching public X11 headers, `wmb_api.h` and utility types.
- The presence-based `WMB_INPROC` / `WMB_APP_OPAQUE` application profile.
- All 51 generated macro replacements, even for code including ordinary
  `<X11/Xlib.h>`.
- Compiler-level external-name pragmas. C source names and typed table field
  names remain Xlib names; the stable compact linker names `wba0`...`wba122`
  avoid duplicate long symbol strings in the small archive.

GCC/Clang pragma support is required and unsupported compilers diagnose this
explicitly. Do not manually define macros that rename Xlib field identifiers.
The wrapper's C11 requirement is private: a strict C99/C++11 consumer keeps its
own language settings. The SDK gate tests that import, both ABI generations and
source/build paths containing spaces.

For a non-CMake build, use the same per-application settings (and compile the
wrapper itself as C11):

```sh
cc -std=c11 -DWMB_INPROC=1 -DWMB_APP_OPAQUE=1 \
    -I"$WMB/transport/api" -I"$WMB/wrapper/libX11-app" \
    -I"$WMB/libX11/include" -I"$WMB" \
    -include "$WMB/wrapper/libX11-app/wmb_app_namespace.h" \
    -include "$WMB/wrapper/libX11-app/wmb_xlib_compat.h" \
    -c my_application.c
```

Here `WMB` is the checkout root. Link the resulting object with the matching
`libX11-app.a` and the platform's binding/entry scaffold. Keep these defines and
forced includes local to the application, never global server/framework flags;
in particular, do not export `_XSERVER64` or server configuration headers.

Your boot/loader supplies a trusted, immutable table pointer:

```c
#include "wmb_api.h"

int my_application_bind(const wmb_api_t *table)
{
    return wmb_bind(table);
}
```

Call that before opening a Display. A failed bind clears the binding;
subsequent unbound API use diagnoses/fails rather than jumping through NULL.
Version, slot count, C data layout and every required function pointer are
checked. A newer append-only provider is accepted; an older insufficient
provider is rejected before missing fields are read.

Select `wmb_sdk_v1_headers` / `wmb_app_wrapper_v1` only when intentionally
building against the independent frozen v1 contract.

## An application fiber in the WMB host

Keep application and server compilation environments separate. Compile the
application as its own target with SDK usage requirements; **do not propagate
opaque application headers into TinyX, retained Xlib, JWM or the host boot TU**.
For example:

```cmake
add_library(my_app OBJECT my_app.c)
target_link_libraries(my_app PRIVATE wmb_sdk_headers)
target_sources(my_host PRIVATE $<TARGET_OBJECTS:my_app>)
target_link_libraries(my_host PRIVATE "$<TARGET_FILE:wmb_app_wrapper>")
add_dependencies(my_host my_app wmb_app_wrapper)
```

Register its entry with `wmb_app_table`, inject `wmb_api_native_table()` on its
application side and bind before use. The raw archive reference here
deliberately avoids leaking SDK compile flags into `my_host`.

A preempting platform task instead receives `wmb_gateway_table()`; it must
never call the native table. Both tables require owner/foreign-wait
configuration through `wmb_gateway_start()`. The SDK itself contains no native
thread or process mechanism.

The selected API, data ownership, finite/fatal failure policies, worker budget,
stop-responsive event/yield behavior and extension procedure are documented in
[the runtime integration guide](../../transport/api/README.md).

The [host process bridge](../../linux_hosted/abi_bridge/README.md) is optional
validation scaffolding, not an embedded loader. Fixed address discovery and
relocation certification are P9.
