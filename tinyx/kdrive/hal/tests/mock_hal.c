#include "mock_hal.h"
#include <stdio.h>
#include <string.h>

MockHalState mock_hal;
static uint32_t pixels[2048];
static hal_framebuffer_t fb;
static bool display_open, input_open;
static struct { uint8_t bytes[1024]; size_t head, size; } queues[HAL_INPUT_DEV_MAX];

void MockHalReset(hal_pixfmt_t format)
{
    mock_hal = (MockHalState){0};
    mock_hal.cursor_result = HAL_INPUT_CONTROL_UNSUPPORTED;
    mock_hal.status = HAL_DISPLAY_ACTIVE;
    fb = (hal_framebuffer_t){pixels, 13, 7, format == HAL_PIXFMT_RGB565 ? 32 : 64,
                             format == HAL_PIXFMT_RGB565 ? 16 : 32, format};
    memset(pixels, 0, sizeof(pixels));
    memset(queues, 0, sizeof(queues));
    display_open = input_open = false;
}

hal_framebuffer_t *MockHalFramebuffer(void) { return &fb; }

bool MockHalPush(hal_input_dev_t dev, const uint8_t *bytes, size_t size)
{
    size_t i;
    if (dev < 0 || dev >= HAL_INPUT_DEV_MAX || size > sizeof(queues[0].bytes) - queues[dev].size)
    {
        fprintf(stderr, "Invalid mock input write\n");
        return false;
    }
    for (i = 0; i < size; i++)
        queues[dev].bytes[(queues[dev].head + queues[dev].size + i) % sizeof(queues[dev].bytes)] = bytes[i];
    queues[dev].size += size;
    return true;
}

int hal_display_init(void)
{
    if (display_open || mock_hal.fail_display_init) return -1;
    display_open = true;
    mock_hal.display_inits++;
    return 0;
}

void hal_display_shutdown(void)
{
    if (display_open) mock_hal.display_shutdowns++;
    display_open = false;
}

const hal_framebuffer_t *hal_display_get_fb(void) { return display_open ? &fb : NULL; }

void hal_display_flush(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if (!display_open || mock_hal.flush_count >= 64)
    {
        fprintf(stderr, "Invalid mock display flush\n");
        return;
    }
    mock_hal.flushes[mock_hal.flush_count++] = (MockFlush){x, y, width, height};
}

hal_display_status_t hal_display_pump(void)
{
    mock_hal.display_pumps++;
    return display_open ? mock_hal.status : HAL_DISPLAY_ERROR;
}

int hal_display_set_power(int on)
{
    mock_hal.last_power = on;
    return mock_hal.power_result;
}

int hal_input_init(void)
{
    if (input_open || mock_hal.fail_input_init) return -1;
    input_open = true;
    mock_hal.input_inits++;
    return 0;
}

void hal_input_shutdown(void)
{
    if (input_open) mock_hal.input_shutdowns++;
    input_open = false;
    memset(queues, 0, sizeof(queues));
}

bool hal_input_has_data(hal_input_dev_t dev)
{
    if (dev < 0 || dev >= HAL_INPUT_DEV_MAX)
    {
        fprintf(stderr, "Invalid mock input device\n");
        return false;
    }
    return input_open && queues[dev].size != 0;
}

bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out)
{
    if (out == NULL)
    {
        fprintf(stderr, "Invalid mock input output pointer\n");
        return false;
    }
    if (!hal_input_has_data(dev)) return false;
    *out = queues[dev].bytes[queues[dev].head];
    queues[dev].head = (queues[dev].head + 1) % sizeof(queues[dev].bytes);
    queues[dev].size--;
    return true;
}

int hal_input_control(hal_input_dev_t dev, hal_input_control_t control, int32_t value)
{
    if (!input_open) return HAL_INPUT_CONTROL_FAILED;
    if (dev != HAL_INPUT_MOUSE || control != HAL_INPUT_CTRL_CURSOR_VISIBLE || (value != 0 && value != 1))
        return HAL_INPUT_CONTROL_INVALID;
    return mock_hal.cursor_result;
}

void hal_input_pump(void) { mock_hal.input_pumps++; }
