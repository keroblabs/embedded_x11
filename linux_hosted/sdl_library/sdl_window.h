#ifndef SDL_WINDOW_H_
#define SDL_WINDOW_H_

//////////////////////////////////////////////////////////////////////////////////////

#include <SDL.h>
#include "sdl_lib_export.h"
#include "sdl_pixels.h"

#if WMB_SDL_PIXEL_BITS == 32
#define SDL_PIXEL_FORMAT SDL_PIXELFORMAT_RGB888
#else
#define SDL_PIXEL_FORMAT SDL_PIXELFORMAT_RGB565
#endif

#define VSYNC_RATE_HZ		50
#define VSYNC_PERIOD_MS		( 1000 / VSYNC_RATE_HZ )

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////

uint32_t sdl_create_window( const char * name, uint16_t width, uint16_t height );

void * sdl_get_buffer( uint32_t window_id );

void sdl_destroy_window( uint32_t window_id );

void sdl_terminate( void );

void sdl_window_process( void );

void sdl_update_window( uint32_t window_id );
void sdl_window_invalidate( uint32_t window_id, int32_t x, int32_t y, uint32_t width, uint32_t height );

void sdl_send_key_event( uint32_t window_id, uint8_t key_code, uint16_t control_key, uint8_t state );
void sdl_send_mouse_event( uint32_t window_id, uint16_t posx, uint16_t posy, uint16_t posz, uint8_t button_state );
void sdl_send_mouse_delta_event( uint32_t window_id, int32_t deltax, int32_t deltay, uint16_t posz, uint8_t button_state );
void sdl_send_mouse_wheel_event( uint32_t window_id, int32_t wheel_x, int32_t wheel_y );

void sdl_window_set_mouse_delta_event( uint32_t window_id, event_delta_t handler );
void sdl_window_set_mouse_event( uint32_t window_id, event_mouse_t handler );
void sdl_window_set_key_event( uint32_t window_id, event_key_t handler );
void sdl_window_set_mouse_wheel_event( uint32_t window_id, event_wheel_t handler );

uint16_t sdl_window_get_width( uint32_t window_id );
uint16_t sdl_window_get_height( uint32_t window_id );
uint32_t sdl_window_get_stride( uint32_t window_id );
uint32_t sdl_window_count( void );

#ifdef __cplusplus
}
#endif

//////////////////////////////////////////////////////////////////////////////////////

#endif // SDL_WINDOW_H_
