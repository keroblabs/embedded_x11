#include "sdl_input.h"
#include "sdl_window.h"
#include "sdl_internal.h"

typedef struct translate_key_t_
{
    SDL_Keycode sdl;
    uint8_t key_code;
} translate_key_t;

static const translate_key_t sdl_event_key[] =
{
    { SDLK_ESCAPE, EVENT_KEY_ESC },
    { SDLK_DELETE, EVENT_KEY_DELETE },
    { SDLK_BACKSPACE, EVENT_KEY_BACKSPACE },
    { SDLK_TAB, EVENT_KEY_TAB },
    { SDLK_RETURN, EVENT_KEY_ENTER },
    { SDLK_DOWN, EVENT_KEY_DOWN },
    { SDLK_UP, EVENT_KEY_UP },
    { SDLK_LEFT, EVENT_KEY_LEFT },
    { SDLK_RIGHT, EVENT_KEY_RIGHT },
    { SDLK_PAGEDOWN, EVENT_KEY_PAGEDOWN },
    { SDLK_PAGEUP, EVENT_KEY_PAGEUP },
    { SDLK_HOME, EVENT_KEY_HOME },
    { SDLK_END, EVENT_KEY_END }
};

static uint8_t last_key;
static uint8_t last_state;
static bool_t key_pending;

void sdl_input_init( void )
{
    last_key = 0;
    last_state = KEY_UP;
    key_pending = FALSE;
}

void sdl_input_record_key( uint8_t key_code, uint16_t control_key, uint8_t state )
{
    if( key_code == 0 ) return;
    if( key_code == 1 )
    {
        switch( control_key )
        {
            case EVENT_KEY_BACKSPACE: key_code = HAL_KEY_BACKSPACE; break;
            case EVENT_KEY_ENTER: key_code = HAL_KEY_ENTER; break;
            case EVENT_KEY_ESC: key_code = HAL_KEY_ESCAPE; break;
            case EVENT_KEY_LEFT: key_code = HAL_KEY_LEFT; break;
            case EVENT_KEY_DOWN: key_code = HAL_KEY_DOWN; break;
            case EVENT_KEY_UP: key_code = HAL_KEY_UP; break;
            case EVENT_KEY_RIGHT: key_code = HAL_KEY_RIGHT; break;
            case EVENT_KEY_TAB: key_code = 9; break;
            default: return;
        }
    }
    last_key = key_code;
    last_state = state;
    key_pending = TRUE;
}

uint8_t sdl_input_check_key( void )
{
    return key_pending;
}

uint8_t sdl_input_read_key( uint8_t *state )
{
    if( state != NULL ) *state = last_state;
    if( !key_pending ) return 0;
    key_pending = FALSE;
    return last_key;
}

void sdl_text_input_event( SDL_TextInputEvent *event, uint8_t keycode )
{
    if( event == NULL )
    {
        SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Text input event is NULL" );
        return;
    }
    if( keycode < 0x20 || keycode > 0x7E )
    {
        SDL_LogWarn( SDL_LOG_CATEGORY_INPUT, "The SDL key protocol supports ASCII text only" );
        return;
    }
    sdl_send_key_event( event->windowID, keycode, 0, KEY_DOWN );
    sdl_send_key_event( event->windowID, keycode, 0, KEY_UP );
}

static uint8_t translate_sym( SDL_Keycode sym )
{
    size_t index;
    for( index = 0; index < sizeof( sdl_event_key ) / sizeof( sdl_event_key[0] ); index++ )
    {
        if( sdl_event_key[index].sdl == sym ) return sdl_event_key[index].key_code;
    }
    return 0;
}

static uint16_t modifier_bit( SDL_Keycode sym )
{
    switch( sym )
    {
        case SDLK_LSHIFT: return KMOD_LSHIFT;
        case SDLK_RSHIFT: return KMOD_RSHIFT;
        case SDLK_LCTRL: return KMOD_LCTRL;
        case SDLK_RCTRL: return KMOD_RCTRL;
        case SDLK_LALT: return KMOD_LALT;
        case SDLK_RALT: return KMOD_RALT;
        case SDLK_LGUI: return KMOD_LGUI;
        case SDLK_RGUI: return KMOD_RGUI;
        default: return 0;
    }
}

static uint8_t modifier_mask( uint16_t mods )
{
    uint8_t mask = 0;
    if( mods & KMOD_SHIFT ) mask |= SHIFT_FLAG;
    if( mods & KMOD_CAPS ) mask |= CAPS_FLAG;
    if( mods & KMOD_CTRL ) mask |= CTRL_FLAG;
    if( mods & KMOD_LALT ) mask |= ALT_FLAG;
    if( mods & KMOD_RALT ) mask |= ALTGR_FLAG;
    if( mods & KMOD_GUI ) mask |= WIN_FLAG;
    return mask;
}

void sdl_keypad_down_event( SDL_Event *event )
{
    gui_instance_t *instance;
    uint16_t mods, bit;
    uint8_t mask, key_code, control_key;
    SDL_Scancode scancode;
    bool_t down;

    if( event == NULL || (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) )
    {
        SDL_LogError( SDL_LOG_CATEGORY_INPUT, "Invalid SDL keyboard event" );
        return;
    }
    instance = sdl_find_window( event->key.windowID );
    if( instance == NULL || event->key.repeat != 0 ) return;

    down = event->type == SDL_KEYDOWN;
    mods = event->key.keysym.mod;
    bit = modifier_bit( event->key.keysym.sym );
    if( bit != 0 )
    {
        if( down ) mods |= bit;
        else mods &= (uint16_t)~bit;
    }
    mask = modifier_mask( mods );
    if( mask != instance->modifier_mask )
    {
        instance->modifier_mask = mask;
        sdl_send_key_event( instance->id, 0, mask, down ? KEY_DOWN : KEY_UP );
        instance = sdl_find_window( event->key.windowID );
        if( instance == NULL ) return;
    }
    if( bit != 0 || event->key.keysym.sym == SDLK_CAPSLOCK ) return;

    scancode = event->key.keysym.scancode;
    if( scancode == SDL_SCANCODE_UNKNOWN ) scancode = SDL_GetScancodeFromKey( event->key.keysym.sym );
    if( scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES ) return;

    if( down )
    {
        if( instance->pressed_keys[scancode] != 0 ) return;
        control_key = translate_sym( event->key.keysym.sym );
        if( control_key != 0 ) key_code = 1;
        else if( event->key.keysym.sym >= 0x20 && event->key.keysym.sym <= 0x7E )
        {
            key_code = (uint8_t)event->key.keysym.sym;
        }
        else return;

        instance->pressed_keys[scancode] = key_code;
        instance->pressed_controls[scancode] = control_key;
    }
    else
    {
        key_code = instance->pressed_keys[scancode];
        control_key = instance->pressed_controls[scancode];
        if( key_code == 0 ) return;
        instance->pressed_keys[scancode] = 0;
        instance->pressed_controls[scancode] = 0;
    }
    sdl_send_key_event( instance->id, key_code, control_key, down ? KEY_DOWN : KEY_UP );
}

void sdl_input_release_window( uint32_t id )
{
    int scancode;
    gui_instance_t *instance = sdl_find_window( id );
    if( instance == NULL ) return;

    for( scancode = 1; scancode < SDL_NUM_SCANCODES; scancode++ )
    {
        if( instance->pressed_keys[scancode] != 0 )
        {
            uint8_t key_code = instance->pressed_keys[scancode];
            uint8_t control_key = instance->pressed_controls[scancode];
            instance->pressed_keys[scancode] = 0;
            instance->pressed_controls[scancode] = 0;
            sdl_send_key_event( id, key_code, control_key, KEY_UP );
            instance = sdl_find_window( id );
            if( instance == NULL ) return;
        }
    }
    if( instance->modifier_mask != 0 )
    {
        instance->modifier_mask = 0;
        sdl_send_key_event( id, 0, 0, KEY_UP );
    }
}
