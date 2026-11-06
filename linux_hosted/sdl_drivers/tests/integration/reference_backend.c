#include "hal_display.h"
#include "hal_input.h"
#include <stdio.h>
#include <string.h>

#define REFERENCE_WIDTH 13u
#define REFERENCE_HEIGHT 7u
#if REFERENCE_PIXEL_BITS == 16
_Alignas(4) static uint16_t pixels[REFERENCE_HEIGHT][REFERENCE_WIDTH + 1u];
#define REFERENCE_FORMAT HAL_PIXFMT_RGB565
#elif REFERENCE_PIXEL_BITS == 32
static uint32_t pixels[REFERENCE_HEIGHT][REFERENCE_WIDTH + 1u];
#define REFERENCE_FORMAT HAL_PIXFMT_XRGB8888
#else
#error "Invalid reference pixel depth"
#endif

static hal_framebuffer_t fb;
static bool display_open, input_open;
static size_t keyboard_offset, pointer_offset;
static const uint8_t keyboard_bytes[] = {HAL_CMD_MODIFIER, 0, HAL_CMD_KEY_DOWN, 'a', HAL_CMD_KEY_UP, 'a'};
static const uint8_t pointer_bytes[] = {HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, (uint8_t)-1, HAL_CMD_MOUSE_BUTTON, 0};

int hal_display_init(void)
{
    if (display_open)
    {
        fprintf(stderr, "Reference display already initialized\n");
        return -1;
    }
    memset(pixels, 0, sizeof(pixels));
    fb = (hal_framebuffer_t){pixels, REFERENCE_WIDTH, REFERENCE_HEIGHT,
                             sizeof(pixels[0]), REFERENCE_PIXEL_BITS, REFERENCE_FORMAT};
    display_open = true;
    return 0;
}

void hal_display_shutdown(void)
{
    display_open = false;
}

const hal_framebuffer_t *hal_display_get_fb(void)
{
    return display_open ? &fb : NULL;
}

void hal_display_flush(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    (void)x; (void)y; (void)width; (void)height;
    /* The reference buffer is the scanout; no transfer is needed. */
}

hal_display_status_t hal_display_pump(void)
{
    if (!display_open)
    {
        fprintf(stderr, "Reference display is not initialized\n");
        return HAL_DISPLAY_ERROR;
    }
    return HAL_DISPLAY_ACTIVE;
}

int hal_display_set_power(int on)
{
    (void)on;
    return -1;
}

int hal_input_init(void)
{
    if (input_open)
    {
        fprintf(stderr, "Reference input already initialized\n");
        return -1;
    }
    keyboard_offset = pointer_offset = 0;
    input_open = true;
    return 0;
}

void hal_input_shutdown(void)
{
    input_open = false;
}

bool hal_input_has_data(hal_input_dev_t dev)
{
    switch (dev)
    {
        case HAL_INPUT_KEYBOARD: return input_open && keyboard_offset < sizeof(keyboard_bytes);
        case HAL_INPUT_MOUSE: return input_open && pointer_offset < sizeof(pointer_bytes);
        default:
            fprintf(stderr, "Invalid reference input device\n");
            return false;
    }
}

bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out)
{
    if (out == NULL)
    {
        fprintf(stderr, "Invalid reference input output pointer\n");
        return false;
    }
    if (!hal_input_has_data(dev)) return false;
    *out = dev == HAL_INPUT_KEYBOARD ? keyboard_bytes[keyboard_offset++] : pointer_bytes[pointer_offset++];
    return true;
}

int hal_input_control(hal_input_dev_t dev, hal_input_control_t control, int32_t value)
{
    if (dev != HAL_INPUT_KEYBOARD && dev != HAL_INPUT_MOUSE)
    {
        fprintf(stderr, "Invalid reference input device\n");
        return HAL_INPUT_CONTROL_INVALID;
    }
    if (!input_open)
    {
        fprintf(stderr, "Reference input is not initialized\n");
        return HAL_INPUT_CONTROL_FAILED;
    }
    if ((control != HAL_INPUT_CTRL_SCALE_Q12 && control != HAL_INPUT_CTRL_CURSOR_VISIBLE &&
         control != HAL_INPUT_CTRL_POINTER_MODE) ||
        (control == HAL_INPUT_CTRL_CURSOR_VISIBLE && value != 0 && value != 1) ||
        (control == HAL_INPUT_CTRL_POINTER_MODE && value != HAL_POINTER_BUTTONS && value != HAL_POINTER_PRESSURE))
    {
        fprintf(stderr, "Invalid reference input control/value\n");
        return HAL_INPUT_CONTROL_INVALID;
    }
    return HAL_INPUT_CONTROL_UNSUPPORTED;
}

void hal_input_pump(void)
{
    /* Input bytes stand in for an interrupt-fed device queue. */
}
