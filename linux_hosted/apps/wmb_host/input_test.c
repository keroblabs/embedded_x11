#include "sdl_backend.h"
#include <SDL.h>

int wmb_host_test_key(void)
{
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN;
    event.key.windowID = sdl_backend_display_window();
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = SDLK_a;
    event.key.keysym.scancode = SDL_SCANCODE_A;
    if (SDL_PushEvent(&event) != 1) return -1;
    event.type = SDL_KEYUP;
    event.key.state = SDL_RELEASED;
    if (SDL_PushEvent(&event) != 1) return -1;
    event = (SDL_Event){0};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = sdl_backend_display_window();
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state = SDL_PRESSED;
    event.button.x = event.button.y = 40;
    if (SDL_PushEvent(&event) != 1) return -1;
    event.type = SDL_MOUSEBUTTONUP;
    event.button.state = SDL_RELEASED;
    return SDL_PushEvent(&event) == 1 ? 0 : -1;
}
