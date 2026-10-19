#include <SDL.h>
#include "types.h"
#include "sdl_lib_export.h"
#include "sdl_input.h"
#include "sdl_mouse.h"
#include "sdl_window.h"
#include "heap_mngr.h"

static bool_t is_running = FALSE;

void sdl_init( void )
{
    //Initialize SDL
    if( SDL_Init( SDL_INIT_VIDEO ) < 0 )
    {
        printf( "SDL could not initialize! SDL_Error: %s\n", SDL_GetError() );
    }
    else
    {
        is_running = TRUE;
    }
}

bool_t sdl_is_running( void )
{
    return is_running;
}

int sdl_event_loop( void )
{
    int ret = 1;
    SDL_Event e;

    // console keyboard
    //sdl_input_event();
    sdl_window_process();

    //Handle events on queue
    while( SDL_PollEvent( &e ) != 0 )
    {
        switch( e.type )
        {
            case SDL_TEXTINPUT:
                sdl_text_input_event( &e.text, ( uint8_t )e.text.text[0] );
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                sdl_keypad_down_event( &e );
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEMOTION:
                sdl_mouse_event( e.type, &e.button, &e.motion );
                break;

            case SDL_MOUSEWHEEL:
                sdl_mouse_event( e.type, &e.button, &e.wheel );
                break;

            case SDL_QUIT:
                //system_PowerDown();
                sdl_terminate();
                SDL_Quit();
                ret = 0;
                is_running = FALSE;
                break;
        }
    }

    return ret;
}
