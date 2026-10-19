/* SDL mouse events: maintain held buttons; export only through callbacks. */
#include <SDL.h>
#include "types.h"
#include "sdl_lib_export.h"
#include "sdl_window.h"
#include "hal_input.h"

static uint8_t held_buttons;

static uint8_t mask_for_sdl_button(uint8_t button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return HAL_MOUSE_MASK_LEFT;
    case SDL_BUTTON_MIDDLE: return HAL_MOUSE_MASK_MIDDLE;
    case SDL_BUTTON_RIGHT: return HAL_MOUSE_MASK_RIGHT;
    case SDL_BUTTON_X1: return HAL_MOUSE_MASK_X1;
    case SDL_BUTTON_X2: return HAL_MOUSE_MASK_X2;
    default: return 0;
    }
}

void sdl_mouse_event(uint32_t type, SDL_MouseButtonEvent *event,
                      void *sdl_data)
{
    SDL_MouseMotionEvent *motion = (SDL_MouseMotionEvent *)sdl_data;
    if (type == SDL_MOUSEMOTION) {
        sdl_send_mouse_delta_event(motion->windowID,
                                   motion->xrel, motion->yrel, 0,
                                   held_buttons);
    } else if (type == SDL_MOUSEBUTTONDOWN || type == SDL_MOUSEBUTTONUP) {
        uint8_t mask = mask_for_sdl_button(event->button);
        if (!mask) return;
        if (type == SDL_MOUSEBUTTONDOWN) held_buttons |= mask;
        else held_buttons &= (uint8_t)~mask;
        sdl_send_mouse_delta_event(event->windowID, 0, 0, 0, held_buttons);
    } else if (type == SDL_MOUSEWHEEL) {
        SDL_MouseWheelEvent *wheel = (SDL_MouseWheelEvent *)sdl_data;
        int sign = wheel->direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        sdl_send_mouse_wheel_event(wheel->windowID,
                                   sign * wheel->x, sign * wheel->y);
    }
}
