#include "hal_display.h"
#include "hal_input.h"

#if defined(SDL_MAIN_HANDLED) || defined(WMB_SDL_PIXEL_BITS) || defined(PIXEL_TYPE_T) || \
    defined(_USE_SCREEN_WIDTH) || defined(WMB_SDL_SCREEN_WIDTH)
#error "Backend configuration leaked into a HAL-only consumer"
#endif

int (*public_display_init)(void) = hal_display_init;
hal_display_status_t (*public_display_pump)(void) = hal_display_pump;
const hal_framebuffer_t *(*public_display_fb)(void) = hal_display_get_fb;
bool (*public_input_read)(hal_input_dev_t, uint8_t *) = hal_input_read_byte;
int (*public_input_control)(hal_input_dev_t, hal_input_control_t, int32_t) = hal_input_control;

_Static_assert(sizeof(uint32_t) == 4, "HAL requires an exact uint32_t");
