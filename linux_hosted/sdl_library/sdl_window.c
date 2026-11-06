#include "sdl_window.h"
#include "sdl_main.h"
#include "sdl_internal.h"

static gui_instance_t *first_instance;
static gui_instance_t *last_instance;

static int sdl_update_window_internal( gui_instance_t *instance, bool_t full_frame )
{
    const SDL_Rect *rect = (!full_frame && instance->has_damage) ? &instance->dirty : NULL;
    const uint8_t *pixels = instance->surface->pixels;

    if( rect != NULL )
    {
        pixels += (size_t)rect->y * (size_t)instance->surface->pitch +
                  (size_t)rect->x * sizeof( PIXEL_TYPE_T );
    }

    if( SDL_UpdateTexture( instance->texture, rect, pixels, instance->surface->pitch ) < 0 ||
        SDL_RenderClear( instance->renderer ) < 0 ||
        SDL_RenderCopy( instance->renderer, instance->texture, NULL, NULL ) < 0 )
    {
        SDL_LogError( SDL_LOG_CATEGORY_RENDER, "SDL window update failed: %s", SDL_GetError() );
        return -1;
    }

    SDL_RenderPresent( instance->renderer );
    instance->has_damage = FALSE;
    return 0;
}

gui_instance_t *sdl_find_window( uint32_t id )
{
    gui_instance_t *instance = first_instance;

    while( instance != NULL )
    {
        if( instance->id == id )
        {
            return instance;
        }
        instance = instance->p_next;
    }
    return NULL;
}

static void destroy_instance( gui_instance_t *instance )
{
    if( instance->texture != NULL ) SDL_DestroyTexture( instance->texture );
    if( instance->surface != NULL ) SDL_FreeSurface( instance->surface );
    if( instance->renderer != NULL ) SDL_DestroyRenderer( instance->renderer );
    if( instance->window != NULL ) SDL_DestroyWindow( instance->window );
    SDL_free( instance );
}

void sdl_destroy_window( uint32_t window_id )
{
    gui_instance_t *instance = first_instance;
    gui_instance_t *previous = NULL;

    while( instance != NULL && instance->id != window_id )
    {
        previous = instance;
        instance = instance->p_next;
    }
    if( instance == NULL )
    {
        return;
    }

    if( previous != NULL ) previous->p_next = instance->p_next;
    else first_instance = instance->p_next;
    if( last_instance == instance ) last_instance = previous;

    destroy_instance( instance );
}

void sdl_destroy_all_windows( void )
{
    while( first_instance != NULL )
    {
        sdl_destroy_window( first_instance->id );
    }
}

void sdl_send_key_event( uint32_t window_id, uint8_t key_code, uint16_t control_key, uint8_t state )
{
    gui_instance_t *instance = sdl_find_window( window_id );

    if( instance != NULL )
    {
        sdl_input_record_key( key_code, control_key, state );
        if( instance->key_handler != NULL )
        {
            instance->key_handler( window_id, key_code, control_key, state );
        }
    }
}

void sdl_send_mouse_event( uint32_t window_id, uint16_t posx, uint16_t posy, uint16_t posz, uint8_t button_state )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance != NULL )
    {
        sdl_mouse_record_position( posx, posy, posz );
        if( instance->mouse_handler != NULL )
        {
            instance->mouse_handler( window_id, posx, posy, posz, button_state );
        }
    }
}

void sdl_send_mouse_wheel_event( uint32_t window_id, int32_t wheel_x, int32_t wheel_y )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance != NULL && instance->wheel_handler != NULL )
    {
        instance->wheel_handler( window_id, wheel_x, wheel_y );
    }
}

void sdl_send_mouse_delta_event( uint32_t window_id, int32_t deltax, int32_t deltay, uint16_t posz, uint8_t button_state )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance != NULL && instance->delta_handler != NULL )
    {
        instance->delta_handler( window_id, deltax, deltay, posz, button_state );
    }
}

void sdl_window_set_mouse_event( uint32_t window_id, event_mouse_t handler )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance != NULL ) instance->mouse_handler = handler;
    else SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Cannot set mouse handler for unknown window %u", window_id );
}

void sdl_window_set_mouse_wheel_event( uint32_t window_id, event_wheel_t handler )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance != NULL ) instance->wheel_handler = handler;
    else SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Cannot set wheel handler for unknown window %u", window_id );
}

void sdl_window_set_mouse_delta_event( uint32_t window_id, event_delta_t handler )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance != NULL ) instance->delta_handler = handler;
    else SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Cannot set delta handler for unknown window %u", window_id );
}

void sdl_window_set_key_event( uint32_t window_id, event_key_t handler )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance != NULL ) instance->key_handler = handler;
    else SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Cannot set key handler for unknown window %u", window_id );
}

uint16_t sdl_window_get_width( uint32_t window_id )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    return instance != NULL ? instance->width : 0;
}

uint16_t sdl_window_get_height( uint32_t window_id )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    return instance != NULL ? instance->height : 0;
}

uint32_t sdl_window_get_stride( uint32_t window_id )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    return instance != NULL ? (uint32_t)instance->surface->pitch : 0;
}

uint32_t sdl_window_count( void )
{
    uint32_t count = 0;
    gui_instance_t *instance = first_instance;
    while( instance != NULL )
    {
        count++;
        instance = instance->p_next;
    }
    return count;
}

uint32_t sdl_create_window( const char *name, uint16_t width, uint16_t height )
{
    gui_instance_t *instance;

    if( name == NULL || width == 0 || height == 0 )
    {
        SDL_LogError( SDL_LOG_CATEGORY_VIDEO, "SDL window needs a name and nonzero geometry" );
        return NEG_U32;
    }

    sdl_init();
    if( !sdl_is_running() )
    {
        SDL_LogError( SDL_LOG_CATEGORY_VIDEO, "SDL backend is not running" );
        return NEG_U32;
    }

    instance = SDL_calloc( 1, sizeof( *instance ) );
    if( instance == NULL )
    {
        SDL_LogError( SDL_LOG_CATEGORY_VIDEO, "SDL window allocation failed: %s", SDL_GetError() );
        return NEG_U32;
    }
    instance->width = width;
    instance->height = height;

    instance->window = SDL_CreateWindow( name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                         width, height, 0 );
    if( instance->window == NULL ) goto fail;
    instance->id = SDL_GetWindowID( instance->window );
    if( instance->id == 0 ) goto fail;

    /* Let SDL select accelerated or software rendering, including headless drivers. */
    instance->renderer = SDL_CreateRenderer( instance->window, -1, 0 );
    if( instance->renderer == NULL ) goto fail;

    instance->surface = SDL_CreateRGBSurfaceWithFormat( 0, width, height, WMB_SDL_PIXEL_BITS, SDL_PIXEL_FORMAT );
    if( instance->surface == NULL ) goto fail;

    instance->texture = SDL_CreateTexture( instance->renderer, SDL_PIXEL_FORMAT,
                                           SDL_TEXTUREACCESS_STREAMING, width, height );
    if( instance->texture == NULL ) goto fail;
    if( SDL_FillRect( instance->surface, NULL, 0 ) < 0 ||
        sdl_update_window_internal( instance, TRUE ) < 0 ) goto fail;

    instance->last_present_ms = SDL_GetTicks();
    if( first_instance == NULL ) first_instance = instance;
    else last_instance->p_next = instance;
    last_instance = instance;
    return instance->id;

fail:
    SDL_LogError( SDL_LOG_CATEGORY_VIDEO, "SDL window creation failed: %s", SDL_GetError() );
    destroy_instance( instance );
    return NEG_U32;
}

void *sdl_get_buffer( uint32_t window_id )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    return instance != NULL ? instance->surface->pixels : NULL;
}

void sdl_update_window( uint32_t window_id )
{
    gui_instance_t *instance = sdl_find_window( window_id );
    if( instance == NULL )
    {
        SDL_LogError( SDL_LOG_CATEGORY_RENDER, "Cannot update unknown window %u", window_id );
        return;
    }
    instance->last_present_ms = SDL_GetTicks();
    (void)sdl_update_window_internal( instance, TRUE );
}

void sdl_window_invalidate( uint32_t window_id, int32_t x, int32_t y, uint32_t width, uint32_t height )
{
    int64_t x0 = x, y0 = y;
    int64_t x1 = x0 + width, y1 = y0 + height;
    gui_instance_t *instance = sdl_find_window( window_id );

    if( instance == NULL )
    {
        SDL_LogError( SDL_LOG_CATEGORY_RENDER, "Cannot invalidate unknown window %u", window_id );
        return;
    }
    if( x0 < 0 ) x0 = 0;
    if( y0 < 0 ) y0 = 0;
    if( x1 > instance->width ) x1 = instance->width;
    if( y1 > instance->height ) y1 = instance->height;
    if( x0 >= x1 || y0 >= y1 ) return;

    if( instance->has_damage )
    {
        int64_t old_x1 = instance->dirty.x + instance->dirty.w;
        int64_t old_y1 = instance->dirty.y + instance->dirty.h;
        if( instance->dirty.x < x0 ) x0 = instance->dirty.x;
        if( instance->dirty.y < y0 ) y0 = instance->dirty.y;
        if( old_x1 > x1 ) x1 = old_x1;
        if( old_y1 > y1 ) y1 = old_y1;
    }
    instance->dirty.x = (int)x0;
    instance->dirty.y = (int)y0;
    instance->dirty.w = (int)(x1 - x0);
    instance->dirty.h = (int)(y1 - y0);
    instance->has_damage = TRUE;
}

static int present_if_due( gui_instance_t *instance, uint32_t now )
{
    if( (uint32_t)(now - instance->last_present_ms) < VSYNC_PERIOD_MS ) return 0;
    instance->last_present_ms = now;
    return sdl_update_window_internal( instance, FALSE );
}

int sdl_present_window( uint32_t id )
{
    gui_instance_t *instance = sdl_find_window( id );
    if( instance == NULL )
    {
        SDL_LogError( SDL_LOG_CATEGORY_RENDER, "Cannot present unknown window %u", id );
        return -1;
    }
    return present_if_due( instance, SDL_GetTicks() );
}

void sdl_window_process( void )
{
    uint32_t now = SDL_GetTicks();
    gui_instance_t *instance = first_instance;

    while( instance != NULL )
    {
        (void)present_if_due( instance, now );
        instance = instance->p_next;
    }
}
