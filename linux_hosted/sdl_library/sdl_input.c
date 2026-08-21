#include <SDL.h>
#include "types.h"
#include "sdl_lib_export.h"
#include "sdl_input.h"
#include "sdl_window.h"

typedef struct translate_key_t_
{
    SDL_Keycode sdl;
    uint8_t key_code;

} translate_key_t;

//////////////////////////////////////////////////////////////////////////////////////

static const translate_key_t sdl_translate[] =
{
    { SDLK_ESCAPE, 0x1B },
    { SDLK_DOWN,   10   },
    { SDLK_UP,     11   },
    { SDLK_LEFT,   8    },
    { SDLK_RIGHT,  21   },

    { 13         , 13   },
    { 9          , 9    },
    { 8          , 8    },
    { SDLK_DELETE, 2    },
    { SDLK_ESCAPE, 0x1B },
    { 0xC2       , '#'  },
    { 127        , 0x7F },
    { 0          , 0    },
};

static const translate_key_t sdl_event_key[] = 
{
    { SDLK_ESCAPE,      EVENT_KEY_ESC },
    { SDLK_DELETE,      EVENT_KEY_DELETE },
    { SDLK_BACKSPACE,   EVENT_KEY_BACKSPACE },
    { SDLK_TAB,         EVENT_KEY_TAB },
    { SDLK_RETURN,      EVENT_KEY_ENTER },

    { SDLK_DOWN,        EVENT_KEY_DOWN },
    { SDLK_UP,          EVENT_KEY_UP },
    { SDLK_LEFT,        EVENT_KEY_LEFT },
    { SDLK_RIGHT,       EVENT_KEY_RIGHT },

    { SDLK_PAGEDOWN,    EVENT_KEY_PAGEDOWN },
    { SDLK_PAGEUP,      EVENT_KEY_PAGEUP },
    { SDLK_HOME,        EVENT_KEY_HOME },
    { SDLK_END,         EVENT_KEY_END },
    { 0,                0    },
};

//////////////////////////////////////////////////////////////////////////////////////

//static bool_t button0 = FALSE;
//static bool_t button1 = FALSE;
static bool_t pending_key = FALSE;
static bool_t pending_ctrl = FALSE;
static uint8_t text_key;

//////////////////////////////////////////////////////////////////////////////////////

void sdl_text_input_event( SDL_TextInputEvent *event, uint8_t keycode )
{
    // remap delete key
    uint8_t key_code;

    if( keycode == 0x08 ) keycode = 0x7F;
    if( keycode == 0xC2 ) keycode = '#';
    
    //if( ( key_code >= 'A' ) && ( key_code <= 'Z' ) )
    //{
    //    key_code |= 0x20;
    //}
    
    key_code = keycode;
    text_key = keycode;
    pending_key = TRUE;

    //key_flag = 1;
    //printf( "key code = %04x\n", _event.type );
    sdl_send_key_event( event->windowID, key_code, 0, KEY_DOWN );
}

static uint8_t translate_sym( SDL_Keycode sym )
{
    int index = 0;
    while( sdl_event_key[index].sdl != 0 )
    {
        if( sdl_event_key[index].sdl == sym ) return sdl_event_key[index].key_code;
        index++;
    }

    return 0;
}


static uint8_t translate_key( SDL_Keycode sym )
{
    int index = 0;
    while( sdl_translate[index].sdl != 0 )
    {
        if( sdl_translate[index].sdl == sym ) return sdl_translate[index].key_code;
        index++;
    }

    return 0;
}

void sdl_keypad_down_event( SDL_Event * sdl_event )
{
    uint8_t read_sym;
    uint8_t read_key;
    uint8_t key_code;
    uint8_t key_flag = 0;
    uint8_t key_mask = 0;
    uint8_t key_mode = 0;

    switch( sdl_event->key.keysym.sym )
    {
        case SDLK_LALT:
            //button0 = ( type == SDL_KEYDOWN ) ? TRUE : FALSE;
            if( sdl_event->type == SDL_KEYDOWN ) key_mask |= OPENAPL_FLAG;
            else key_mask &= ~OPENAPL_FLAG;
            key_code = 0;
            key_flag = 1;
            key_mode = ( sdl_event->type == SDL_KEYDOWN ) ? KEY_DOWN : KEY_UP;
            break;

        case SDLK_RALT:
            //button1 = ( type == SDL_KEYDOWN ) ? TRUE : FALSE;
            if( sdl_event->type == SDL_KEYDOWN ) key_mask |= CLOSEAPL_FLAG;
            else key_mask &= ~CLOSEAPL_FLAG;
            key_code = 0;
            key_flag = 1;
            key_mode = ( sdl_event->type == SDL_KEYDOWN ) ? KEY_DOWN : KEY_UP;
            break;

        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            if( sdl_event->type == SDL_KEYDOWN ) key_mask |= SHIFT_FLAG;
            else key_mask &= ~SHIFT_FLAG;
            key_code = 0;
            key_flag = 1;
            key_mode = ( sdl_event->type == SDL_KEYDOWN ) ? KEY_DOWN : KEY_UP;
            break;

        case SDLK_LGUI:
            if( sdl_event->type == SDL_KEYDOWN ) key_mask |= WIN_FLAG;
            else key_mask &= ~WIN_FLAG;
            break;

        case SDLK_LCTRL:
        case SDLK_RCTRL:
            if( sdl_event->type == SDL_KEYDOWN ) key_mask |= CTRL_FLAG;
            else key_mask &= ~CTRL_FLAG;
            key_code = 0;
            key_flag = 1;
            key_mode = ( sdl_event->type == SDL_KEYDOWN ) ? KEY_DOWN : KEY_UP;
            pending_ctrl = ( sdl_event->type == SDL_KEYDOWN ) ? TRUE : FALSE;
            break;

        case SDLK_CAPSLOCK:
            if( sdl_event->type == SDL_KEYDOWN )
            {
                if( key_mask & CAPS_FLAG ) key_mask &= ~CAPS_FLAG;
                else key_mask |= CAPS_FLAG;
                key_code = 0;
                key_flag = 1;
                key_mode = ( sdl_event->type == SDL_KEYDOWN ) ? KEY_DOWN : KEY_UP;
            }
            break;

        default:
            if( ( ( sdl_event->type == SDL_KEYDOWN ) || ( sdl_event->type == SDL_KEYUP ) ) && ( sdl_event->key.repeat == FALSE ) )
            {
                key_mode = ( sdl_event->type == SDL_KEYDOWN ) ? KEY_DOWN : KEY_UP;

                read_sym = translate_sym( sdl_event->key.keysym.sym );
                if( read_sym != 0 )
                {
                    key_code = 1;
                    key_flag = 1;
                    key_mask = read_sym;
                    break;
                }
                read_key = translate_key( sdl_event->key.keysym.sym );
                if( read_key != 0 )
                {
                    key_code = read_key;
                    key_flag = 1;
                    break;
                }
                else if( ( sdl_event->key.keysym.sym >= 0x20 ) && ( sdl_event->key.keysym.sym <= 125 ) )
                {
                    //if( ( key_mode == KEY_DOWN ) && ( pending_key == FALSE ) )
                    //{
                    //    pending_key = TRUE;
                    //}
                    //else 
                    if( ( key_mode == KEY_UP ) && ( pending_key == TRUE ) )
                    {
                        key_code = text_key;
                        key_flag = 1;
                    }
                    else if( pending_ctrl == TRUE )
                    {
                        key_code = sdl_event->key.keysym.sym;
                        key_flag = 1;
                    }

                    break;
                }
            }
            break;
    }

    if( key_flag == TRUE )
    {
        sdl_send_key_event( sdl_event->window.windowID, key_code, key_mask, key_mode );
    }
}

#if 0
void sdl_text_input_event( uint8_t keycode )
{
    // remap delete key
    if( keycode == 0x08 ) keycode = 0x7F;
    if( keycode == 0xC2 ) keycode = '#';

    //if( ( key_code >= 'A' ) && ( key_code <= 'Z' ) )
    //{
    //    key_code |= 0x20;
    //}

    key_code = keycode;
    key_flag = 1;
    //printf( "key code = %04x\n", keycode );
    //fflush(stdout);
}

void sdl_input_init( void )
{
    key_flag = 0;
    key_code = 0;
    key_mode = KEY_UP;
}

uint8_t sdl_input_read_key( uint8_t * state )
{
    key_flag = 0;
    if( state != NULL ) *state = key_mode;
    return key_code;
}

uint8_t sdl_input_check_key( void )
{
    return key_flag;
}
#endif
