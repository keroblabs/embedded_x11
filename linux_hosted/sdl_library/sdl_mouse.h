#ifndef SDL_MOUSE_H_
#define SDL_MOUSE_H_

#include <SDL.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////////////////////////

void sdl_mouse_init( void );
bool_t sdl_mouse_get_pos( uint16_t * posx, uint16_t * posy, uint16_t * posz );
void sdl_mouse_event( uint32_t type, SDL_MouseButtonEvent * event, void * sdl_data );

#ifdef __cplusplus
}
#endif

//////////////////////////////////////////////////////////////////////////////////////

#endif // SDL_MOUSE_H_
