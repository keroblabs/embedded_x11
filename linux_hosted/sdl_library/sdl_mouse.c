#include "sdl_mouse.h"
#include "sdl_window.h"
#include "sdl_internal.h"

static uint16_t last_x, last_y, last_z;
static bool_t mouse_pending;

void sdl_mouse_init( void )
{
    last_x = last_y = last_z = 0;
    mouse_pending = FALSE;
}

void sdl_mouse_record_position( uint16_t x, uint16_t y, uint16_t z )
{
    last_x = x;
    last_y = y;
    last_z = z;
    mouse_pending = TRUE;
}

bool_t sdl_mouse_get_pos( uint16_t *posx, uint16_t *posy, uint16_t *posz )
{
    if( posx == NULL || posy == NULL || posz == NULL )
    {
        SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Mouse position needs three output pointers" );
        return FALSE;
    }
    if( !mouse_pending ) return FALSE;
    *posx = last_x;
    *posy = last_y;
    *posz = last_z;
    mouse_pending = FALSE;
    return TRUE;
}

static uint16_t clip_position( int32_t value, uint16_t extent )
{
    if( value < 0 ) return 0;
    if( (uint32_t)value >= extent ) return (uint16_t)(extent - 1u);
    return (uint16_t)value;
}

static uint8_t active_button( uint32_t buttons, uint8_t previous )
{
    uint8_t button;
    if( previous >= SDL_BUTTON_LEFT && previous <= SDL_BUTTON_X2 &&
        (buttons & SDL_BUTTON( previous )) != 0 ) return previous;
    for( button = SDL_BUTTON_LEFT; button <= SDL_BUTTON_X2; button++ )
    {
        if( (buttons & SDL_BUTTON( button )) != 0 ) return button;
    }
    return 0;
}

static int32_t flipped_wheel( int32_t value )
{
    if( value == INT32_MIN )
    {
        SDL_LogWarn( SDL_LOG_CATEGORY_INPUT, "Flipped wheel value exceeds int32 range; clipping" );
        return INT32_MAX;
    }
    return -value;
}

void sdl_mouse_release_window( uint32_t id )
{
    gui_instance_t *instance = sdl_find_window( id );
    if( instance == NULL || instance->mouse_buttons == 0 ) return;
    instance->mouse_buttons = 0;
    instance->mouse_button = 0;
    last_z = 0;
    mouse_pending = TRUE;
    sdl_send_mouse_delta_event( id, 0, 0, 0, 0 );
}

void sdl_mouse_event( uint32_t type, SDL_MouseButtonEvent *event, void *sdl_data )
{
    gui_instance_t *instance;
    uint32_t id;
    int32_t x, y, dx = 0, dy = 0;
    uint16_t posx, posy, z;
    uint8_t button;

    if( type == SDL_MOUSEWHEEL )
    {
        SDL_MouseWheelEvent *wheel = sdl_data;
        if( wheel == NULL )
        {
            SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Mouse wheel event is NULL" );
            return;
        }
        dx = wheel->x;
        dy = wheel->y;
        if( wheel->direction == SDL_MOUSEWHEEL_FLIPPED )
        {
            dx = flipped_wheel( dx );
            dy = flipped_wheel( dy );
        }
        sdl_send_mouse_wheel_event( wheel->windowID, dx, dy );
        return;
    }
    if( type == SDL_MOUSEMOTION )
    {
        SDL_MouseMotionEvent *motion = sdl_data;
        if( motion == NULL )
        {
            SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Mouse motion event is NULL" );
            return;
        }
        id = motion->windowID;
        x = motion->x;
        y = motion->y;
        dx = motion->xrel;
        dy = motion->yrel;
        instance = sdl_find_window( id );
        if( instance == NULL ) return;
        instance->mouse_buttons = motion->state;
    }
    else if( type == SDL_MOUSEBUTTONDOWN || type == SDL_MOUSEBUTTONUP )
    {
        if( event == NULL )
        {
            SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Mouse button event is NULL" );
            return;
        }
        if( event->button < SDL_BUTTON_LEFT || event->button > SDL_BUTTON_X2 ) return;
        id = event->windowID;
        x = event->x;
        y = event->y;
        instance = sdl_find_window( id );
        if( instance == NULL ) return;
        if( type == SDL_MOUSEBUTTONDOWN )
        {
            instance->mouse_buttons |= SDL_BUTTON( event->button );
            instance->mouse_button = event->button;
        }
        else instance->mouse_buttons &= ~SDL_BUTTON( event->button );
    }
    else
    {
        SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Invalid SDL mouse event type %u", type );
        return;
    }

    instance->mouse_button = active_button( instance->mouse_buttons, instance->mouse_button );
    button = instance->mouse_button;
    z = button != 0 ? 100 : 0;
    posx = clip_position( x, instance->width );
    posy = clip_position( y, instance->height );

    sdl_send_mouse_event( id, posx, posy, z, button );
    sdl_send_mouse_delta_event( id, dx, dy, z, button );
}
