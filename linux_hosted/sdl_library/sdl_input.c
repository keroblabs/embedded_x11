/* SDL keyboard translation for the WMB core-keyboard subset.
 * Keysyms above ASCII and text composition are deferred to a future
 * international input method; avoid duplicating KEYDOWN with TEXTINPUT.
 */
#include <SDL.h>
#include "types.h"
#include "sdl_input.h"
#include "sdl_window.h"

static uint8_t last_modifiers;

static uint8_t mod_mask(SDL_Keymod m)
{
    uint8_t mask = 0;
    if (m & KMOD_SHIFT) mask |= SHIFT_FLAG;
    if (m & KMOD_CAPS)  mask |= CAPS_FLAG;
    if (m & KMOD_CTRL)  mask |= CTRL_FLAG;
    if (m & KMOD_LALT)  mask |= ALT_FLAG;
    if (m & KMOD_RALT)  mask |= ALTGR_FLAG;
    if (m & KMOD_GUI)   mask |= WIN_FLAG;
    return mask;
}

static int special_key(SDL_Keycode sym)
{
    switch (sym) {
    case SDLK_BACKSPACE: return EVENT_KEY_BACKSPACE;
    case SDLK_RETURN: return EVENT_KEY_ENTER;
    case SDLK_ESCAPE: return EVENT_KEY_ESC;
    case SDLK_UP: return EVENT_KEY_UP;
    case SDLK_DOWN: return EVENT_KEY_DOWN;
    case SDLK_RIGHT: return EVENT_KEY_RIGHT;
    case SDLK_LEFT: return EVENT_KEY_LEFT;
    default: return 0;
    }
}

void sdl_keypad_down_event(SDL_Event *e)
{
    uint8_t new_mods = mod_mask((SDL_Keymod)e->key.keysym.mod);
    uint8_t state = (e->type == SDL_KEYDOWN) ? KEY_DOWN : KEY_UP;
    int special;
    SDL_Keycode sym = e->key.keysym.sym;
    if (e->type == SDL_KEYDOWN && e->key.repeat) return;

    if (new_mods != last_modifiers) {
        last_modifiers = new_mods;
        sdl_send_key_event(e->key.windowID, 0, new_mods, state);
    }
    special = special_key(sym);
    if (special) {
        sdl_send_key_event(e->key.windowID, 1, (uint16_t)special, state);
    } else if (sym >= 0x20 && sym <= 0x7d) {
        /* ASCII scancode; KDrive supplies the shifted X11 keysym. */
        sdl_send_key_event(e->key.windowID, (uint8_t)sym, 0, state);
    }
}

void sdl_text_input_event(SDL_TextInputEvent *event, uint8_t keycode)
{
    /* Core-keyboard mode uses matched SDL key down/up, not TEXTINPUT. */
    (void)event; (void)keycode;
}
