/*
 * hal_input_sdl.c - hosted implementation of hal_input.h.
 *
 * Thin adapter binding the keypad/mouse driver pair to the contract,
 * per docs/12-platform-contract.md section 10.  The drivers already
 * emit the HAL_CMD_* byte protocol, so this layer only routes.
 */
#include "hal_input.h"
#include "hal_drivers.h"
#include "sdl_main.h"

///////////////////////////////////////////////////////////////////////////

#define MOUSE_DEV_INDEX     0

static int is_open = 0;

///////////////////////////////////////////////////////////////////////////

int hal_input_init(void)
{
    if (is_open)
    {
        return -1;
    }

    keypad_driver_init();
    mouse_driver_init(MOUSE_DEV_INDEX);

    is_open = 1;

    return 0;
}

void hal_input_shutdown(void)
{
    is_open = 0;
}

bool hal_input_has_data(hal_input_dev_t dev)
{
    if (!is_open)
    {
        return false;
    }

    switch (dev)
    {
        case HAL_INPUT_KEYBOARD:
            return (keypad_driver_has_data() != FALSE);

        case HAL_INPUT_MOUSE:
            return (mouse_driver_has_data(MOUSE_DEV_INDEX) != FALSE);

        default:
            return false;
    }
}

bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out)
{
    if ((out == NULL) || !hal_input_has_data(dev))
    {
        return false;
    }

    switch (dev)
    {
        case HAL_INPUT_KEYBOARD:
            *out = keypad_driver_read_data();
            return true;

        case HAL_INPUT_MOUSE:
            *out = mouse_driver_read_data(MOUSE_DEV_INDEX);
            return true;

        default:
            return false;
    }
}

int hal_input_control(hal_input_dev_t dev, const uint8_t *cmd, uint32_t len)
{
    if (!is_open || (cmd == NULL) || (len == 0u))
    {
        return -1;
    }

    /* The keyboard has no control channel on this backend. */
    if (dev != HAL_INPUT_MOUSE)
    {
        return -1;
    }

    mouse_driver_write_command(MOUSE_DEV_INDEX, cmd, len);

    return 0;
}

void hal_input_pump(void)
{
    if (!is_open)
    {
        return;
    }

    /* sdl_event_loop() also runs the backend's vsync pass, so this is
     * what publishes damage queued by hal_display_flush(). */
    sdl_event_loop();

    mouse_driver_process();
}
