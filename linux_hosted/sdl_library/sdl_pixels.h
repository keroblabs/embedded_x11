#ifndef WMB_SDL_PIXELS_H
#define WMB_SDL_PIXELS_H

#include <stdint.h>

#ifndef WMB_SDL_PIXEL_BITS
#ifdef TIMA_LINUX
#define WMB_SDL_PIXEL_BITS 32
#else
#define WMB_SDL_PIXEL_BITS 16
#endif
#endif

#if WMB_SDL_PIXEL_BITS == 16
#ifndef PIXEL_TYPE_T
#define PIXEL_TYPE_T uint16_t
#endif
#define WMB_SDL_RGB(r, g, b) \
    ((PIXEL_TYPE_T)((((uint32_t)(r) & 0xF8u) << 8) | \
                    (((uint32_t)(g) & 0xFCu) << 3) | \
                    (((uint32_t)(b) & 0xF8u) >> 3)))
#elif WMB_SDL_PIXEL_BITS == 32
#ifndef PIXEL_TYPE_T
#define PIXEL_TYPE_T uint32_t
#endif
#define WMB_SDL_RGB(r, g, b) \
    ((PIXEL_TYPE_T)((((uint32_t)(r) & 0xFFu) << 16) | \
                    (((uint32_t)(g) & 0xFFu) << 8) | \
                    ((uint32_t)(b) & 0xFFu)))
#else
#error "WMB_SDL_PIXEL_BITS must be 16 (RGB565) or 32 (XRGB8888)"
#endif

#ifdef __cplusplus
static_assert(sizeof(PIXEL_TYPE_T) * 8 == WMB_SDL_PIXEL_BITS,
              "PIXEL_TYPE_T disagrees with WMB_SDL_PIXEL_BITS");
#else
_Static_assert(sizeof(PIXEL_TYPE_T) * 8 == WMB_SDL_PIXEL_BITS,
               "PIXEL_TYPE_T disagrees with WMB_SDL_PIXEL_BITS");
#endif

#endif
