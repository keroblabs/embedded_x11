# SDL2 window and callback wrapper

This library owns SDL windows, RGB565/XRGB8888 framebuffers, textures, event
translation, and a 50 Hz presentation pass. It is the lower layer used by the
[SDL HAL drivers](../sdl_drivers/README.md) and can also be used independently.
It does not link WMB allocation, timing, or fiber implementations.

For a portable application, use only the display/input HAL target described in
the [integration guide](../sdl_drivers/README.md). This wrapper is a private
implementation dependency of that target: its headers and pixel definitions
are not exported to HAL consumers. Direct use of this library is an explicit
SDL-specific choice, not an embedded-portable application API.

## Building and linking

Build just this library, from the repository root:

```sh
cmake -S linux_hosted/sdl_library -B build-sdl-library \
    -DWMB_SDL_PIXEL_BITS=16
cmake --build build-sdl-library
```

In another CMake project, add this directory as a subdirectory and link
`wmb_sdl_library`. The target exports SDL2, C11, matching pixel definitions, and
the legacy type/HAL include paths. Use `WMB_SDL_PIXEL_BITS=32` for XRGB8888.
See the [backend integration guide](../sdl_drivers/README.md) for dependencies,
vendoring layout, configuration, lifecycle rules, and licensing limitations.

## Public interfaces

| Header | Interface |
|---|---|
| [sdl_main.h](sdl_main.h) | Idempotent `sdl_init()`, `sdl_is_running()`, and non-waiting `sdl_event_loop()`. |
| [sdl_window.h](sdl_window.h) | Create/destroy windows; dimensions, actual byte stride and owned buffer; invalidation, immediate update, presentation, callback registration and synthetic event delivery; `sdl_terminate()` destroys all managed windows and releases this library's SDL video reference. |
| [sdl_lib_export.h](sdl_lib_export.h) | Callback types and legacy `sdl_api_t` vtable. `library_entry_point()` initializes the backend and returns the populated table, or `NULL` on failure. |
| [sdl_pixels.h](sdl_pixels.h) | `PIXEL_TYPE_T`, `WMB_SDL_PIXEL_BITS`, and the constant-expression `WMB_SDL_RGB(r, g, b)` colour packer. |
| [sdl_input.h](sdl_input.h) | SDL keyboard translation and legacy last-key polling helpers. |
| [sdl_mouse.h](sdl_mouse.h) | SDL button/motion/wheel translation and legacy last-position polling helpers. |

All declarations in the public headers are implemented and have C linkage for
C++ callers. The backend/internal headers are not application APIs.
The legacy polling helpers coalesce events to the **last** key/position since a
read; they are not lossless queues. Use callbacks or the HAL input streams when
individual press/release events matter.
The position snapshot includes synthetic absolute events as well as translated
SDL events. Legacy `OPENAPL_FLAG`/`CLOSEAPL_FLAG` names alias the canonical
Alt/AltGr modifier bits rather than separate, incompatible mask values.

## Direct window example

```c
#include <SDL.h>
#include <stdio.h>
#include "sdl_main.h"
#include "sdl_window.h"

int main(void)
{
    sdl_api_t *api = library_entry_point();
    if (api == NULL)
        return 1;
    uint32_t id = api->create_window("Standalone SDL", 321, 200);
    if (id == NEG_U32)
    {
        api->terminate();
        return 1;
    }

    uint8_t *pixels = api->get_buffer(id);
    uint32_t stride = sdl_window_get_stride(id);
    for (uint32_t y = 0; y < api->get_height(id); ++y)
    {
        PIXEL_TYPE_T *row = (PIXEL_TYPE_T *)(pixels + (size_t)y * stride);
        for (uint32_t x = 0; x < api->get_width(id); ++x)
            row[x] = WMB_SDL_RGB(255, 128, 64);
    }
    sdl_update_window(id);
    while (api->process())
        SDL_Delay(1);
    api->terminate();
    return 0;
}
```

Creation returns `NEG_U32` on failure; unknown/destroyed IDs have `NULL` buffers
and zero dimensions/stride. Destruction is idempotent. Never free the returned
pixels yourself, and respect row padding, including for odd RGB565 widths.

`sdl_present_window()` is the damage-driven per-window pump used by the HAL:
an unchanged window performs no texture upload. Exposure events invalidate the
window so it is repainted. Explicit `sdl_update_window()` and the legacy
`sdl_window_process()` automatic refresh retain their full-frame behavior.

Callbacks are window-specific and run synchronously on the event-pump thread.
The key callback uses `key_code == 0` for modifier masks, `key_code == 1` plus an
`EVENT_KEY_*` token for special keys, and an ASCII byte otherwise. Its `state` is
`KEY_DOWN` or `KEY_UP`. ASCII comes from key events, not SDL text-input events;
the explicit legacy `sdl_text_input_event()` helper emits a paired synthetic
ASCII press/release when called directly.

Absolute/delta mouse callbacks receive a single button **ID**, not an SDL button
bitmask, and emulated pressure `0` or `100`. Wheel callbacks receive signed
integer ticks with `SDL_MOUSEWHEEL_FLIPPED` normalized. Callbacks may destroy their
own window, but must not recursively pump the global SDL event queue.

Close/quit requests make `process()` return zero without freeing caller-held
framebuffers; call `terminate()` explicitly to finish cleanup. Any managed
window's close request stops the shared loop. There is no per-window close policy,
Unicode/IME API, native multitouch support, or thread-safe/reentrant event pump.
