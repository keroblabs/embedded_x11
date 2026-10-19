/*
 * WMB KDrive pointer adapter. HAL emits command/value pairs.
 * Motion values are signed deltas; button values are held-state masks.
 */
#include "tima_kdrive.h"
#include "hal_input.h"
#include "hal_display.h"
#include <stdint.h>

static struct {
    Bool initialized;
    uint8_t buttons;
    uint8_t pending_cmd;
    uint8_t pending_value;
    int dx;
    int dy;
} pointer_state;

static unsigned long tima_button_flags(uint8_t mask)
{
    unsigned long flags = 0;
    if (mask & HAL_MOUSE_MASK_LEFT)   flags |= KD_BUTTON_1;
    if (mask & HAL_MOUSE_MASK_MIDDLE) flags |= KD_BUTTON_2;
    if (mask & HAL_MOUSE_MASK_RIGHT)  flags |= KD_BUTTON_3;
    /* Core X11 buttons 4/5 are reserved for vertical wheel pulses here.
     * Extra side buttons require an extended input mapping. */
    return flags;
}

static void tima_emit_motion(int dx, int dy, unsigned long extra_buttons)
{
    if (kdMouseInfo)
        KdEnqueueMouseEvent(kdMouseInfo,
                            tima_button_flags(pointer_state.buttons) |
                            extra_buttons | KD_MOUSE_DELTA, dx, dy);
}

static Bool tima_MouseInit(void)
{
    pointer_state.initialized = TRUE;
    pointer_state.buttons = 0;
    pointer_state.pending_cmd = 0;
    pointer_state.dx = pointer_state.dy = 0;
    /* Boot has already called hal_input_init(). */
    return TRUE;
}

static void tima_MouseFini(void)
{
    pointer_state.initialized = FALSE;
    pointer_state.pending_cmd = 0;
}

const KdMouseFuncs tima_MouseFuncs = {
    .Init = tima_MouseInit,
    .Fini = tima_MouseFini
};

static void tima_mouse_pair(uint8_t cmd, uint8_t value)
{
    int motion;
    switch (cmd) {
    case HAL_CMD_MOUSE_X:
        pointer_state.dx += (int)(int8_t)value;
        break;
    case HAL_CMD_MOUSE_Y:
        pointer_state.dy += (int)(int8_t)value;
        /* Input backend emits X before Y; dispatch the assembled delta. */
        tima_emit_motion(pointer_state.dx, pointer_state.dy, 0);
        pointer_state.dx = pointer_state.dy = 0;
        break;
    case HAL_CMD_MOUSE_BUTTON:
        pointer_state.buttons = value & HAL_MOUSE_MASK_ALL;
        tima_emit_motion(0, 0, 0);
        break;
    case HAL_CMD_MOUSE_WHEEL_Y:
        motion = (int)(int8_t)value;
        while (motion != 0) {
            unsigned long pulse = (motion > 0) ? KD_BUTTON_4 : KD_BUTTON_5;
            tima_emit_motion(0, 0, pulse);
            tima_emit_motion(0, 0, 0);
            motion += motion > 0 ? -1 : 1;
        }
        break;
    case HAL_CMD_MOUSE_WHEEL_X:
        /* Core KDrive in this tree handles only KD_BUTTON_1..5.
         * Horizontal wheel needs an upstream-safe X event hook. */
        break;
    case HAL_CMD_MOUSE_Z:
        /* Reserved for touch/pressure mode. */
        break;
    default:
        break;
    }
}

void tima_MousePoll(void)
{
    uint8_t b;
    if (!pointer_state.initialized)
        return;
    while (hal_input_read_byte(HAL_INPUT_MOUSE, &b)) {
        if (pointer_state.pending_cmd == 0) {
            pointer_state.pending_cmd = b;
        } else {
            uint8_t cmd = pointer_state.pending_cmd;
            pointer_state.pending_cmd = 0;
            tima_mouse_pair(cmd, b);
        }
    }
}
