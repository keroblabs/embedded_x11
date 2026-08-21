#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "kdrive.h"
#include "hal_drivers.h"

/* Mouse state */
static struct {
    Bool initialized;
    int  x, y;
    int  buttons;
    uint8_t mouse_driver_id;
} mouse_state;

/*
 * tima_MouseInit - Initialize mouse hardware
 */
static Bool tima_MouseInit(void)
{
    mouse_state.initialized = TRUE;
    mouse_state.x = lcd_driver_get_width() / 2;
    mouse_state.y = lcd_driver_get_height() / 2;
    mouse_state.buttons = 0;
    mouse_state.mouse_driver_id = 0; /* Assuming single mouse device at index 0 */

    #ifdef DEBUG
    ErrorF("tima_MouseInit: Mouse initialized\n");
    #endif
    return TRUE;
}

static void tima_MouseFini(void)
{
    mouse_state.initialized = FALSE;
}

const KdMouseFuncs tima_MouseFuncs =
{
    .Init = tima_MouseInit,
    .Fini = tima_MouseFini,
};

/*
 * tima_MousePoll - Read and process mouse events
 *
 * Called from event polling loop.
 */
void tima_MousePoll(void)
{
    extern KdMouseInfo *kdMouseInfo;
    uint8_t mouse_byte;
    int dx, dy;
    unsigned long flags;

    if (!mouse_state.initialized)
        return;

    while (mouse_driver_has_data(mouse_state.mouse_driver_id))
    {
        mouse_byte = mouse_driver_read_data(mouse_state.mouse_driver_id);

        switch (mouse_byte)
        {
            case CMD_MOUSE_X: /* always the first byte for mouse coordinates */
                dx = (int8_t)mouse_driver_read_data(mouse_state.mouse_driver_id);
                continue; // continue read loop to get Y

            case CMD_MOUSE_Y:
                dy = (int8_t)mouse_driver_read_data(mouse_state.mouse_driver_id);
                continue; // continue to process after getting both X and Y

            case CMD_MOUSE_BUTTON:
                mouse_byte = mouse_driver_read_data(mouse_state.mouse_driver_id);

                /* Determine button state */
                if (mouse_byte == MOUSE_BUTTON_LEFT) flags |= KD_BUTTON_1;
                else if (mouse_byte == MOUSE_BUTTON_MIDDLE) flags |= KD_BUTTON_2;
                else if (mouse_byte == MOUSE_BUTTON_RIGHT) flags |= KD_BUTTON_3;
                else if (mouse_byte == MOUSE_BUTTON_X1) flags |= KD_BUTTON_4;
                else if (mouse_byte == MOUSE_BUTTON_X2) flags |= KD_BUTTON_5;
                break; // in mouse mode, break to process event because we have all data

            case CMD_MOUSE_Z:
                /* Ignore Z for now */
                (void)mouse_driver_read_data(mouse_state.mouse_driver_id);
                break; // in touch mode, break to process event because we have all data

            case CMD_MOUSE_WHEEL_X: /* always first for mouse wheel event*/
                /* Ignore wheel X for now */
                (void)mouse_driver_read_data(mouse_state.mouse_driver_id);
                continue; // continue read loop to get Y

            case CMD_MOUSE_WHEEL_Y:
                /* Ignore wheel Y for now */
                (void)mouse_driver_read_data(mouse_state.mouse_driver_id);
                break; // break to process event after getting both wheel X and Y

            default:
                break;
        }

        /* Update position with bounds */
        mouse_state.x += dx;
        mouse_state.y += dy;

        /* Clamp to screen bounds */
        if (mouse_state.x < 0) mouse_state.x = 0;
        if (mouse_state.y < 0) mouse_state.y = 0;
        if (mouse_state.x >= lcd_driver_get_width()) mouse_state.x = lcd_driver_get_width() - 1;
        if (mouse_state.y >= lcd_driver_get_height()) mouse_state.y = lcd_driver_get_height() - 1;

        /* Enqueue mouse event */
        KdEnqueueMouseEvent(kdMouseInfo, flags | KD_MOUSE_DELTA, mouse_state.x, mouse_state.y);
    }
}
