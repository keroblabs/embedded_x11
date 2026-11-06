#ifndef WMB_EXAMPLE_FRAMEBUFFER_DRAW_H
#define WMB_EXAMPLE_FRAMEBUFFER_DRAW_H

#include "hal_display.h"

#define FRAMEBUFFER_RGB(r, g, b) \
    ((((uint32_t)(r) & 0xFFu) << 16) | (((uint32_t)(g) & 0xFFu) << 8) | \
     ((uint32_t)(b) & 0xFFu))

static inline int framebuffer_fill_rect(const hal_framebuffer_t *fb, uint32_t x,
                                        uint32_t y, uint32_t width, uint32_t height,
                                        uint32_t rgb)
{
    uint32_t bytes, row, column;
    if (fb == NULL || fb->pixels == NULL) return -1;
    switch (fb->format)
    {
        case HAL_PIXFMT_RGB565: bytes = 2; break;
        case HAL_PIXFMT_XRGB8888: bytes = 4; break;
        default: return -1;
    }
    if (fb->bpp != bytes * 8u || (uint64_t)fb->width * bytes > fb->stride ||
        fb->stride % bytes != 0 || (uintptr_t)fb->pixels % bytes != 0 ||
        (uint64_t)fb->stride * fb->height > SIZE_MAX) return -1;
    if (x >= fb->width || y >= fb->height || width == 0 || height == 0) return 0;
    if (width > fb->width - x) width = fb->width - x;
    if (height > fb->height - y) height = fb->height - y;

    for (row = 0; row < height; row++)
    {
        uint8_t *line = (uint8_t *)fb->pixels + (size_t)(y + row) * fb->stride;
        if (fb->format == HAL_PIXFMT_RGB565)
        {
            uint16_t colour = (uint16_t)(((rgb & 0xF80000u) >> 8) |
                                         ((rgb & 0x00FC00u) >> 5) |
                                         ((rgb & 0x0000F8u) >> 3));
            for (column = 0; column < width; column++)
                ((uint16_t *)line)[x + column] = colour;
        }
        else
        {
            for (column = 0; column < width; column++)
                ((uint32_t *)line)[x + column] = rgb & 0xFFFFFFu;
        }
    }
    return 0;
}

#endif
