#include "hal_display.h"
#include "hal_input.h"
#include "../../../apps/common/framebuffer_draw.h"

#include <cstdio>

static int drain_input(hal_input_dev_t dev)
{
    uint8_t command, value;
    while (hal_input_read_byte(dev, &command))
    {
        if (command < HAL_CMD_MODIFIER || command > HAL_CMD_MOUSE_WHEEL_Y ||
            !hal_input_read_byte(dev, &value))
        {
            std::fprintf(stderr, "Invalid/incomplete input packet\n");
            return -1;
        }
    }
    return 0;
}

int main()
{
    if (hal_display_init() != 0) return 1;
    if (hal_input_init() != 0)
    {
        hal_display_shutdown();
        return 1;
    }

    const hal_framebuffer_t *fb = hal_display_get_fb();
    int result = 0;
    if (fb == nullptr || fb->pixels == nullptr || fb->width == 0 || fb->height == 0 ||
        framebuffer_fill_rect(fb, 0, 0, fb->width, fb->height, FRAMEBUFFER_RGB(255, 0, 0)) < 0)
    {
        std::fprintf(stderr, "Framebuffer contract/drawing failed\n");
        result = 1;
    }
    else
    {
        for (uint32_t y = 0; y < fb->height; ++y)
        {
            const uint8_t *row = static_cast<const uint8_t *>(fb->pixels) + (size_t)y * fb->stride;
            for (uint32_t x = 0; x < fb->width; ++x)
            {
                bool red = fb->format == HAL_PIXFMT_RGB565
                    ? reinterpret_cast<const uint16_t *>(row)[x] == 0xF800u
                    : reinterpret_cast<const uint32_t *>(row)[x] == 0xFF0000u;
                if (!red) result = 1;
            }
        }
        hal_display_flush(0, 0, fb->width, fb->height);
        hal_input_pump();
        if (hal_display_pump() != HAL_DISPLAY_ACTIVE ||
            drain_input(HAL_INPUT_KEYBOARD) < 0 || drain_input(HAL_INPUT_MOUSE) < 0)
            result = 1;

        const int control = hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, HAL_INPUT_SCALE_ONE);
        if (control != HAL_INPUT_CONTROL_OK && control != HAL_INPUT_CONTROL_UNSUPPORTED)
            result = 1;
    }

    hal_input_shutdown();
    hal_display_shutdown();
    if (hal_display_get_fb() != nullptr || hal_input_has_data(HAL_INPUT_KEYBOARD))
        result = 1;
    if (result == 0) std::printf("PASS platform-independent HAL client\n");
    return result;
}
