#include <stdbool.h>
#include "hal_display.h"
#include "sdl_backend.h"
#include "sdl_internal.h"
#include "sdl_window.h"

#ifndef WMB_SDL_SCREEN_WIDTH
#define WMB_SDL_SCREEN_WIDTH 800
#endif
#ifndef WMB_SDL_SCREEN_HEIGHT
#define WMB_SDL_SCREEN_HEIGHT 480
#endif

_Static_assert(WMB_SDL_SCREEN_WIDTH > 0 && WMB_SDL_SCREEN_WIDTH <= UINT16_MAX, "Invalid display width");
_Static_assert(WMB_SDL_SCREEN_HEIGHT > 0 && WMB_SDL_SCREEN_HEIGHT <= UINT16_MAX, "Invalid display height");

static hal_framebuffer_t fb;
static uint32_t window_id = UINT32_MAX;
static bool is_open;
static bool has_error;

uint32_t sdl_backend_display_window(void)
{
    return window_id;
}

int hal_display_init(void)
{
    if (is_open)
    {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "HAL display is already initialized");
        return -1;
    }
    if (sdl_backend_acquire() < 0)
    {
        return -1;
    }

    window_id = sdl_create_window("WMB display", WMB_SDL_SCREEN_WIDTH, WMB_SDL_SCREEN_HEIGHT);
    if (window_id == UINT32_MAX)
    {
        sdl_backend_release();
        return -1;
    }
    fb.pixels = sdl_get_buffer(window_id);
    fb.width = sdl_window_get_width(window_id);
    fb.height = sdl_window_get_height(window_id);
    fb.stride = sdl_window_get_stride(window_id);
    fb.bpp = WMB_SDL_PIXEL_BITS;
    fb.format = WMB_SDL_PIXEL_BITS == 16 ? HAL_PIXFMT_RGB565 : HAL_PIXFMT_XRGB8888;

    sdl_window_set_key_event(window_id, sdl_backend_key_event);
    sdl_window_set_mouse_delta_event(window_id, sdl_backend_pointer_event);
    sdl_window_set_mouse_wheel_event(window_id, sdl_backend_wheel_event);
    has_error = false;
    is_open = true;
    return 0;
}

void hal_display_shutdown(void)
{
    if (!is_open) return;
    is_open = false;
    has_error = false;
    sdl_destroy_window(window_id);
    window_id = UINT32_MAX;
    fb = (hal_framebuffer_t){0};
    sdl_backend_release();
}

const hal_framebuffer_t *hal_display_get_fb(void)
{
    if (!is_open) return NULL;
    if (sdl_get_buffer(window_id) == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "HAL display framebuffer is no longer available");
        return NULL;
    }
    return &fb;
}

void hal_display_flush(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if (hal_display_get_fb() == NULL || width == 0 || height == 0) return;
    sdl_window_invalidate(window_id, x, y, width, height);
}

hal_display_status_t hal_display_pump(void)
{
    if (hal_display_get_fb() == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Cannot pump an uninitialized/unavailable HAL display");
        return HAL_DISPLAY_ERROR;
    }
    if (has_error) return HAL_DISPLAY_ERROR;
    if (!sdl_poll_events()) return HAL_DISPLAY_STOP_REQUESTED;
    if (sdl_present_window(window_id) < 0)
    {
        has_error = true;
        return HAL_DISPLAY_ERROR;
    }
    return HAL_DISPLAY_ACTIVE;
}

int hal_display_set_power(int on)
{
    (void)on;
    return -1;
}
