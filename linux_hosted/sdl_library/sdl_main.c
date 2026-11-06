#include <SDL.h>
#include "types.h"
#include "sdl_lib_export.h"
#include "sdl_input.h"
#include "sdl_mouse.h"
#include "sdl_window.h"
#include "sdl_internal.h"

static bool_t is_running = FALSE;
static bool_t is_initialized = FALSE;

void sdl_init( void )
{
    if( is_initialized )
    {
        return;
    }

    SDL_SetMainReady();
    if( SDL_InitSubSystem( SDL_INIT_VIDEO ) < 0 )
    {
        SDL_LogError( SDL_LOG_CATEGORY_VIDEO, "SDL video initialization failed: %s", SDL_GetError() );
    }
    else
    {
        sdl_input_init();
        sdl_mouse_init();
        is_initialized = TRUE;
        is_running = TRUE;
    }
}

bool_t sdl_is_running( void )
{
    return is_running;
}

int sdl_poll_events( void )
{
    SDL_Event e;

    if( !is_running )
    {
        return 0;
    }

    while( SDL_PollEvent( &e ) != 0 )
    {
        switch( e.type )
        {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                sdl_keypad_down_event( &e );
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                sdl_mouse_event( e.type, &e.button, NULL );
                break;

            case SDL_MOUSEMOTION:
                sdl_mouse_event( e.type, NULL, &e.motion );
                break;

            case SDL_MOUSEWHEEL:
                sdl_mouse_event( e.type, NULL, &e.wheel );
                break;

            case SDL_WINDOWEVENT:
                if( sdl_find_window( e.window.windowID ) != NULL )
                {
                    if( e.window.event == SDL_WINDOWEVENT_FOCUS_LOST )
                    {
                        sdl_input_release_window( e.window.windowID );
                        sdl_mouse_release_window( e.window.windowID );
                    }
                    else if( e.window.event == SDL_WINDOWEVENT_CLOSE )
                    {
                        is_running = FALSE;
                    }
                    else if( e.window.event == SDL_WINDOWEVENT_EXPOSED )
                    {
                        sdl_window_invalidate( e.window.windowID, 0, 0,
                                              sdl_window_get_width( e.window.windowID ),
                                              sdl_window_get_height( e.window.windowID ) );
                    }
                }
                break;

            case SDL_QUIT:
                /* Keep framebuffers alive until the caller shuts down. */
                is_running = FALSE;
                break;
        }

        if( !is_running )
        {
            break;
        }
    }

    return is_running ? 1 : 0;
}

int sdl_event_loop( void )
{
    int running = sdl_poll_events();
    if( running ) sdl_window_process();
    return running;
}

void sdl_terminate( void )
{
    sdl_destroy_all_windows();

    if( is_initialized )
    {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    }

    is_initialized = FALSE;
    is_running = FALSE;
    sdl_input_init();
    sdl_mouse_init();
}

sdl_api_t * library_entry_point( void )
{
    static sdl_api_t api =
    {
        sdl_create_window,
        sdl_window_set_mouse_event,
        sdl_window_set_mouse_wheel_event,
        sdl_window_set_key_event,
        sdl_window_get_width,
        sdl_window_get_height,
        sdl_get_buffer,
        sdl_destroy_window,
        sdl_terminate,
        sdl_event_loop
    };

    sdl_init();
    return is_running ? &api : NULL;
}
