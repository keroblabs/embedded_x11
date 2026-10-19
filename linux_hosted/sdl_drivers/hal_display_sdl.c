/*
 * SDL implementation of hal_display.h.  DDX never refers to SDL here.
 * The SDL event/presentation fiber owns SDL_RenderPresent.
 */
#include "hal_display.h"
#include "hal_drivers.h"
#include <stdint.h>

static hal_framebuffer_t fb;
static int is_open;
static uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;

int hal_display_init(uint32_t width, uint32_t height, hal_pixfmt_t format)
{
    uint32_t bpp;
    if (is_open) return -1;
    if (format != HAL_PIXFMT_RGB565 && format != HAL_PIXFMT_XRGB8888)
        return -1;
    lcd_driver_init();
    bpp = lcd_driver_get_bits_per_pixel();
    if (width != (uint32_t)lcd_driver_get_width() ||
        height != (uint32_t)lcd_driver_get_height() ||
        bpp != ((format == HAL_PIXFMT_RGB565) ? 16u : 32u)) {
        lcd_driver_deinit();
        return -1;
    }
    fb.pixels = lcd_driver_get_framebuffer();
    if (!fb.pixels) {
        lcd_driver_deinit();
        return -1;
    }
    fb.width = width;
    fb.height = height;
    fb.stride = width * (bpp / 8u);
    fb.bpp = (uint8_t)bpp;
    fb.format = format;
    lcd_driver_clear_screen();
    dirty_x0 = dirty_y0 = dirty_x1 = dirty_y1 = 0;
    is_open = 1;
    return 0;
}

void hal_display_shutdown(void)
{
    if (!is_open) return;
    is_open = 0;
    fb.pixels = NULL;
    lcd_driver_deinit();
}
const hal_framebuffer_t *hal_display_get_fb(void)
{
    return is_open ? &fb : NULL;
}

void hal_display_flush(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    int64_t left = x, top = y;
    int64_t right = left + width, bottom = top + height;
    uint32_t x0, y0, x1, y1;
    if (!is_open || !width || !height) return;
    /* Clip the true rectangle, not (clamped origin + width). */
    if (right <= 0 || bottom <= 0 ||
        left >= fb.width || top >= fb.height) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > fb.width) right = fb.width;
    if (bottom > fb.height) bottom = fb.height;
    if (right <= left || bottom <= top) return;
    x0 = (uint32_t)left; y0 = (uint32_t)top;
    x1 = (uint32_t)right; y1 = (uint32_t)bottom;
    /* Cooperative-only update. The preemption tick remains disabled. */
    if (dirty_x0 == dirty_x1 || dirty_y0 == dirty_y1) {
        dirty_x0 = x0; dirty_y0 = y0;
        dirty_x1 = x1; dirty_y1 = y1;
    } else {
        if (x0 < dirty_x0) dirty_x0 = x0;
        if (y0 < dirty_y0) dirty_y0 = y0;
        if (x1 > dirty_x1) dirty_x1 = x1;
        if (y1 > dirty_y1) dirty_y1 = y1;
    }
    /* SDL currently repaints its texture on each presentation pass.
     * Consuming the accumulated dirty rectangle is a later optimisation. */
}

int hal_display_set_power(int on)
{
    (void)on;
    return -1;
}
