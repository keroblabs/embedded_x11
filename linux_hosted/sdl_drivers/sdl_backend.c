#include <SDL.h>
#include "sdl_backend.h"
#include "sdl_main.h"
#include "sdl_window.h"

static unsigned int references;

int sdl_backend_acquire(void)
{
    sdl_init();
    if (!sdl_is_running())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot acquire the stopped/unavailable SDL backend");
        return -1;
    }
    references++;
    return 0;
}

void sdl_backend_release(void)
{
    if (references == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL backend released without an owner");
        return;
    }
    references--;
    if (references == 0 && sdl_window_count() == 0)
    {
        sdl_terminate();
    }
}
