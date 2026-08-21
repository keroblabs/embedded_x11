#include <SDL.h>
#include "types.h"
#include "sdl_lib_export.h"
#include "sdl_window.h"
#include "hal_time.h"
#include "heap_mngr.h"

//////////////////////////////////////////////////////////////////////////////////////

typedef struct gui_instane_t_
{
    SDL_Window   * window;
    SDL_Renderer * renderer;
    SDL_Surface  * surface;
    SDL_Texture  * texture;

    event_mouse_t mouse_handler;
    event_delta_t delta_handler;
    event_wheel_t wheel_handler;
    event_key_t   key_handler;

    uint32_t id;

    uint16_t width;
    uint16_t height;

    uint32_t vsync_ms;

    struct gui_instane_t_ * p_next;

} gui_instance_t;

//////////////////////////////////////////////////////////////////////////////////////

static gui_instance_t * first_instance;
static gui_instance_t * last_instance;

//////////////////////////////////////////////////////////////////////////////////////

static void sdl_update_window_internal( gui_instance_t * instance )
{
    SDL_UpdateTexture( instance->texture, NULL, instance->surface->pixels, instance->surface->pitch );
    SDL_RenderClear( instance->renderer );
    SDL_RenderCopy( instance->renderer, instance->texture, NULL, NULL );
    SDL_RenderPresent( instance->renderer );
}

static gui_instance_t * find_window( uint32_t id )
{
    gui_instance_t * search_instance = first_instance;

    while( search_instance != NULL )
    {
        if( search_instance->id == id )
        {
            return search_instance;
        }

        search_instance = search_instance->p_next;
    }

    return NULL;
}

static void free_instance( uint32_t id )
{
    gui_instance_t * search_instance = first_instance;
    gui_instance_t * prev_instance = NULL;

    while( search_instance != NULL )
    {
        if( search_instance->id == id )
        {
            if( prev_instance != NULL )
            {
                prev_instance->p_next = search_instance->p_next;
            }
            else
            {
                first_instance = search_instance->p_next;
            }
            break;
        }

        prev_instance = search_instance;
        search_instance = search_instance->p_next;
    }

    if( search_instance != NULL )
    {
        SDL_DestroyTexture( search_instance->texture );
        SDL_DestroyRenderer( search_instance->renderer );
        SDL_DestroyWindow( search_instance->window );
        heap_mngr_free( search_instance );
    }
}

void sdl_send_key_event( uint32_t window_id, uint8_t key_code, uint16_t control_key, uint8_t state )
{
    gui_instance_t * instance = find_window( window_id );

    if( ( instance != NULL ) && ( instance->key_handler != NULL ) )
    {
        instance->key_handler( window_id, key_code, control_key, state );
    }
}

void sdl_send_mouse_event( uint32_t window_id, uint16_t posx, uint16_t posy, uint16_t posz, uint8_t button_state )
{
    gui_instance_t * instance = find_window( window_id );

    if( ( instance != NULL ) && ( instance->mouse_handler != NULL ) )
    {
        instance->mouse_handler( window_id, posx, posy, posz, button_state );
    }
}

void sdl_send_mouse_wheel_event( uint32_t window_id, int32_t wheel_x, int32_t wheel_y )
{
    gui_instance_t * instance = find_window( window_id );

    if( ( instance != NULL ) && ( instance->wheel_handler != NULL ) )
    {
        instance->wheel_handler( window_id, wheel_x, wheel_y );
    }
}

void sdl_send_mouse_delta_event( uint32_t window_id, int32_t deltax, int32_t deltay, uint16_t posz, uint8_t button_state )
{
    gui_instance_t * instance = find_window( window_id );

    if( ( instance != NULL ) && ( instance->delta_handler != NULL ) )
    {
        instance->delta_handler( window_id, deltax, deltay, posz, button_state );
    }
}

void sdl_window_set_mouse_event( uint32_t window_id, event_mouse_t handler )
{
    gui_instance_t * instance = find_window( window_id );

    if( instance != NULL )
    {
        instance->mouse_handler = handler;
    }
}

void sdl_window_set_mouse_wheel_event( uint32_t window_id, event_wheel_t handler )
{
    gui_instance_t * instance = find_window( window_id );

    if( instance != NULL )
    {
        instance->wheel_handler = handler;
    }
}

void sdl_window_set_mouse_delta_event( uint32_t window_id, event_delta_t handler )
{
    gui_instance_t * instance = find_window( window_id );

    if( instance != NULL )
    {
        instance->delta_handler = handler;
    }
}

void sdl_window_set_key_event( uint32_t window_id, event_key_t handler )
{
    gui_instance_t * instance = find_window( window_id );

    if( instance != NULL )
    {
        instance->key_handler = handler;
    }
}

uint16_t sdl_window_get_width( uint32_t window_id )
{
    uint16_t ret = 0;

    gui_instance_t * instance = find_window( window_id );

    if( instance != NULL )
    {
        ret = instance->width;
    }

    return ret;
}

uint16_t sdl_window_get_height( uint32_t window_id )
{
    uint16_t ret = 0;

    gui_instance_t * instance = find_window( window_id );

    if( instance != NULL )
    {
        ret = instance->height;
    }

    return ret;
}

uint32_t sdl_create_window( const char * name, uint16_t width, uint16_t height )
{
    Uint32 r_mask, g_mask, b_mask, a_mask;
    int bpp;

    printf( "SDL: allocate instance\n" );

    SDL_PixelFormatEnumToMasks( SDL_PIXEL_FORMAT, &bpp, &r_mask, &g_mask, &b_mask, &a_mask );

    gui_instance_t * instance = heap_mngr_malloc( sizeof( gui_instance_t ) );
    if( instance == NULL )
    {
        printf( "SDL: failed to allocate instance\n" );
        return NEG_U32;
    }

    memset( instance, 0x00, sizeof( gui_instance_t ) );
    instance->width = width;
    instance->height = height;
    instance->mouse_handler = NULL;
    instance->delta_handler = NULL;
    instance->key_handler = NULL;
    instance->wheel_handler = NULL;

    instance->window = SDL_CreateWindow( name,
                                         SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED,
                                         width, height, 0 );

    instance->id = SDL_GetWindowID( instance->window );

    if( instance->window == NULL )
    {
        printf( "SDL: failed to create window\n" );
        heap_mngr_free( instance );
        return NEG_U32;
    }

    instance->renderer = SDL_CreateRenderer( instance->window, -1, SDL_RENDERER_ACCELERATED );
    if( instance->renderer == NULL )
    {
        printf( "SDL: failed to create render\n" );
        heap_mngr_free( instance );
        return NEG_U32;
    }

    instance->surface = SDL_CreateRGBSurface( 0, width, height, bpp, r_mask, g_mask, b_mask, a_mask);
    if( instance->surface == NULL )
    {
        printf( "SDL: failed to create surface\n" );
        return NEG_U32;
    }

    instance->texture = SDL_CreateTexture( instance->renderer, SDL_PIXEL_FORMAT, SDL_TEXTUREACCESS_STREAMING, width, height);
    if( instance->texture == NULL )
    {
        printf( "SDL: failed to create texture\n" );
        return NEG_U32;
    }

    instance->vsync_ms = hal_tick_ms() + VSYNC_PERIOD_MS;

    //Fill the surface white
    SDL_FillRect( instance->surface, NULL, SDL_MapRGB( instance->surface->format, 0, 0, 0 ) );

    //Update the surface
    SDL_UpdateWindowSurface( instance->window );

    if( first_instance == NULL )
    {
        first_instance = instance;
    }
    else
    {
        last_instance->p_next = instance;
    }

    last_instance = instance;

    return instance->id;
}

void * sdl_get_buffer( uint32_t window_id )
{
    PIXEL_TYPE_T * ret = NULL;
    gui_instance_t * instance = find_window( window_id );

    if( instance != NULL )
    {
        ret = ( PIXEL_TYPE_T * )instance->surface->pixels;
    }

    return ret;
}

void sdl_destroy_window( uint32_t window_id )
{
    free_instance( window_id );
}

void sdl_update_window( uint32_t window_id )
{
    gui_instance_t * instance = find_window( window_id );
    sdl_update_window_internal( instance );
}

void sdl_terminate( void )
{
    gui_instance_t * search_instance = first_instance;

    while( search_instance != NULL )
    {
        sdl_destroy_window( search_instance->id );
        search_instance = search_instance->p_next;
    }
}

void sdl_window_process( void )
{
    gui_instance_t * search_instance = first_instance;

    while( search_instance != NULL )
    {
        if( ( search_instance->window != NULL ) && ( hal_tick_ms() >= search_instance->vsync_ms ) )
        {
            search_instance->vsync_ms = hal_tick_ms() + VSYNC_PERIOD_MS;
            sdl_update_window_internal( search_instance );
        }

        search_instance = search_instance->p_next;
    }
}
