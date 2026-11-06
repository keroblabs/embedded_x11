#ifndef SDL_INPUT_H_
#define SDL_INPUT_H_

#include <SDL.h>
#include <stdint.h>
#include "hal_input.h"

#define SHIFT_FLAG      HAL_MOD_SHIFT
#define CAPS_FLAG       HAL_MOD_CAPS
#define CTRL_FLAG       HAL_MOD_CTRL
#define ALT_FLAG        HAL_MOD_ALT
#define ALTGR_FLAG      HAL_MOD_ALTGR
#define WIN_FLAG        HAL_MOD_SUPER
#define CLOSEAPL_FLAG   ALTGR_FLAG
#define OPENAPL_FLAG    ALT_FLAG

#define KEY_DOWN        0x01
#define KEY_UP          0x00

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif // SDL_INPUT_H_
