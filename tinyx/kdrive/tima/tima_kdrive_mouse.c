#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <limits.h>
#include "tima_kdrive.h"

static struct {
    Bool initialized;
    int x, y;
    int dx, dy;
    tima_input_packet_t pending;
} mouse_state;

static Bool tima_MouseInit(void)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    if (fb == NULL || fb->width == 0 || fb->height == 0 ||
        fb->width > INT_MAX || fb->height > INT_MAX)
    {
        ErrorF("tima_MouseInit: invalid framebuffer geometry\n");
        return FALSE;
    }
    mouse_state.x = (int)(fb->width / 2);
    mouse_state.y = (int)(fb->height / 2);
    mouse_state.dx = mouse_state.dy = 0;
    mouse_state.pending = (tima_input_packet_t){0};
    mouse_state.initialized = TRUE;
    return TRUE;
}

static void tima_MouseFini(void)
{
    mouse_state.initialized = FALSE;
    mouse_state.pending = (tima_input_packet_t){0};
}

const KdMouseFuncs tima_MouseFuncs =
{
    .Init = tima_MouseInit,
    .Fini = tima_MouseFini,
};

void tima_MousePoll(void)
{
    extern KdMouseInfo *kdMouseInfo;
    const hal_framebuffer_t *fb = hal_display_get_fb();
    uint8_t command, data;
    if (!mouse_state.initialized || fb == NULL) return;

    while (tima_ReadInputPacket(HAL_INPUT_MOUSE, &mouse_state.pending, &command, &data))
    {
        unsigned long flags = 0;
        switch (command)
        {
            case HAL_CMD_MOUSE_X: mouse_state.dx = (int8_t)data; continue;
            case HAL_CMD_MOUSE_Y: mouse_state.dy = (int8_t)data; continue;
            case HAL_CMD_MOUSE_WHEEL_X:
            case HAL_CMD_MOUSE_WHEEL_Y:
                continue;
            case HAL_CMD_MOUSE_BUTTON:
                switch (data)
                {
                    case 0: break;
                    case HAL_MOUSE_BUTTON_LEFT: flags = KD_BUTTON_1; break;
                    case HAL_MOUSE_BUTTON_MIDDLE: flags = KD_BUTTON_2; break;
                    case HAL_MOUSE_BUTTON_RIGHT: flags = KD_BUTTON_3; break;
                    case HAL_MOUSE_BUTTON_X1: flags = KD_BUTTON_4; break;
                    case HAL_MOUSE_BUTTON_X2: flags = KD_BUTTON_5; break;
                    default:
                        ErrorF("tima_MousePoll: invalid button %u\n", data);
                        continue;
                }
                break;
            case HAL_CMD_MOUSE_Z:
                if (data != 0) flags = KD_BUTTON_1;
                break;
            default:
                ErrorF("tima_MousePoll: invalid command %u\n", command);
                continue;
        }

        int64_t x = (int64_t)mouse_state.x + mouse_state.dx;
        int64_t y = (int64_t)mouse_state.y + mouse_state.dy;
        mouse_state.dx = mouse_state.dy = 0;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= fb->width) x = fb->width - 1;
        if (y >= fb->height) y = fb->height - 1;
        mouse_state.x = (int)x;
        mouse_state.y = (int)y;
        KdEnqueueMouseEvent(kdMouseInfo, flags, mouse_state.x, mouse_state.y);
    }
}
