#ifndef WMB_SDL_BACKEND_H
#define WMB_SDL_BACKEND_H

#include <stdint.h>

int sdl_backend_acquire(void);
void sdl_backend_release(void);
uint32_t sdl_backend_display_window(void);
void sdl_backend_key_event(uint32_t window, uint8_t key, uint16_t control, uint8_t state);
void sdl_backend_pointer_event(uint32_t window, int32_t dx, int32_t dy, uint16_t pressure, uint8_t button);
void sdl_backend_wheel_event(uint32_t window, int32_t x, int32_t y);

#endif
