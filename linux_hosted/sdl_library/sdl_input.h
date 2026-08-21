#ifndef SDL_INPUT_H_
#define SDL_INPUT_H_

#define SHIFT_FLAG      0x01
#define CAPS_FLAG       0x02
#define CTRL_FLAG       0x04
#define ALT_FLAG		0x08
#define ALTGR_FLAG		0x10
#define WIN_FLAG		0x20
#define CLOSEAPL_FLAG   0x40
#define OPENAPL_FLAG    0x80

#define KEY_DOWN        0x01
#define KEY_UP          0x00

enum
{
    EVENT_KEY_ESC = 'A',
    EVENT_KEY_TAB,
    EVENT_KEY_ENTER,
    EVENT_KEY_BACKSPACE,
    EVENT_KEY_DELETE,

    EVENT_KEY_PAGEUP,
    EVENT_KEY_PAGEDOWN,

    EVENT_KEY_HOME,
    EVENT_KEY_END,

    EVENT_KEY_UP,
    EVENT_KEY_DOWN,
    EVENT_KEY_LEFT,
    EVENT_KEY_RIGHT,
};

void sdl_keypad_down_event( SDL_Event * sdl_event );
void sdl_text_input_event( SDL_TextInputEvent *e, uint8_t keycode );

uint8_t sdl_input_read_key( uint8_t * state );
uint8_t sdl_input_check_key( void );

void sdl_input_init( void );

#endif // SDL_INPUT_H_
