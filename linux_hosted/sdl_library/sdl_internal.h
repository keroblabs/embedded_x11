#ifndef WMB_SDL_INTERNAL_H
#define WMB_SDL_INTERNAL_H

#include <SDL.h>
#include "types.h"
#include "sdl_lib_export.h"

typedef struct gui_instance_t_
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *surface;
    SDL_Texture *texture;

    event_mouse_t mouse_handler;
    event_delta_t delta_handler;
    event_wheel_t wheel_handler;
    event_key_t key_handler;

    uint32_t id;
    uint16_t width;
    uint16_t height;
    uint32_t last_present_ms;
    SDL_Rect dirty;
    bool_t has_damage;

    uint8_t modifier_mask;
    uint8_t pressed_keys[SDL_NUM_SCANCODES];
    uint8_t pressed_controls[SDL_NUM_SCANCODES];
    uint32_t mouse_buttons;
    uint8_t mouse_button;

    struct gui_instance_t_ *p_next;
} gui_instance_t;

gui_instance_t *sdl_find_window(uint32_t id);
int sdl_poll_events(void);
int sdl_present_window(uint32_t id);
void sdl_destroy_all_windows(void);
void sdl_input_release_window(uint32_t id);
void sdl_mouse_release_window(uint32_t id);
void sdl_mouse_record_position(uint16_t x, uint16_t y, uint16_t z);
void sdl_input_record_key(uint8_t key_code, uint16_t control_key, uint8_t state);

#endif
