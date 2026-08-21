/*
 * hal_display.h - Platform contract: the framebuffer.
 *
 * docs/12-platform-contract.md section 4.  Must compile standalone
 * under -ffreestanding -nostdinc with only <stdint.h> / <stddef.h>
 * (gate PC-V1).
 */
#ifndef WMB_HAL_DISPLAY_H
#define WMB_HAL_DISPLAY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pixel formats.  Only these are supported; adding one is a decision. */
typedef enum {
    HAL_PIXFMT_RGB565   = 0,   /* 16 bpp, the default target format */
    HAL_PIXFMT_XRGB8888 = 1    /* 32 bpp, convenient on the host    */
} hal_pixfmt_t;

typedef struct {
    void        *pixels;   /* base of the linear buffer WMB draws into */
    uint32_t     width;    /* pixels  */
    uint32_t     height;   /* pixels  */
    uint32_t     stride;   /* BYTES per row; may exceed width * bpp/8  */
    uint8_t      bpp;      /* bits per pixel: 16 or 32                 */
    hal_pixfmt_t format;
} hal_framebuffer_t;

/* Lifecycle.  Returns 0 on success, negative on failure. */
int  hal_display_init(uint32_t width, uint32_t height, hal_pixfmt_t format);
void hal_display_shutdown(void);

/* The buffer WMB renders into.  Valid between init and shutdown.
 * Never NULL after a successful init. */
const hal_framebuffer_t *hal_display_get_fb(void);

/* Publish a damaged rectangle to the physical display.
 * Coordinates are clipped by the implementation.  Must not block. */
void hal_display_flush(int32_t x, int32_t y, uint32_t w, uint32_t h);

/* Optional power management.  A backend that cannot do this returns -1
 * and WMB carries on. */
int  hal_display_set_power(int on);

#ifdef __cplusplus
}
#endif

#endif /* WMB_HAL_DISPLAY_H */
