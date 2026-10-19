/*
 * Standalone fake platform.  Intentionally no SDL, TinyX core, or OS
 * backend.  The test executable links only KDrive DDX objects and mocks.
 */
#include "wmb_mock_hal.h"
#include "hal_display.h"
#include "hal_alloc.h"
#include "hal_time.h"
#include <stdlib.h>
#include <string.h>

#define W 96u
#define H 64u
#define RING 1024u

typedef struct {
    uint8_t bytes[RING];
    unsigned in;
    unsigned out;
} mock_input_t;

static uint16_t pixels[W * H];
static hal_framebuffer_t framebuffer;
static mock_input_t inputs[HAL_INPUT_DEV_MAX];
static uint32_t flushes;
static int display_open;

void *wmb_alloc(size_t bytes) { return malloc(bytes); }
void wmb_free(void *p) { free(p); }

int hal_display_init(uint32_t w, uint32_t h, hal_pixfmt_t fmt)
{
    if (display_open || w != W || h != H || fmt != HAL_PIXFMT_RGB565)
        return -1;
    framebuffer.pixels = pixels;
    framebuffer.width = W;
    framebuffer.height = H;
    framebuffer.stride = W * 2;
    framebuffer.bpp = 16;
    framebuffer.format = HAL_PIXFMT_RGB565;
    memset(pixels, 0, sizeof(pixels));
    display_open = 1;
    flushes = 0;
    return 0;
}
void hal_display_shutdown(void) { display_open = 0; }
const hal_framebuffer_t *hal_display_get_fb(void)
{ return display_open ? &framebuffer : NULL; }
void hal_display_flush(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    (void)x; (void)y; (void)w; (void)h;
    ++flushes;
}
uint32_t wmb_mock_flush_count(void) { return flushes; }
int hal_display_set_power(int on) { (void)on; return -1; }

int hal_input_init(void)
{
    memset(inputs, 0, sizeof(inputs));
    return 0;
}
void hal_input_shutdown(void) { }
bool hal_input_has_data(hal_input_dev_t dev)
{
    return dev < HAL_INPUT_DEV_MAX && inputs[dev].out < inputs[dev].in;
}
bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out)
{
    if (!out || !hal_input_has_data(dev)) return false;
    *out = inputs[dev].bytes[inputs[dev].out++];
    return true;
}
int hal_input_control(hal_input_dev_t dev, const uint8_t *cmd, uint32_t len)
{ (void)dev; (void)cmd; (void)len; return -1; }
void hal_input_pump(void) { }

void wmb_mock_push(hal_input_dev_t dev, uint8_t cmd, uint8_t value)
{
    mock_input_t *input;
    if (dev >= HAL_INPUT_DEV_MAX) abort();
    input = &inputs[dev];
    if (input->in + 2u > RING) abort();
    input->bytes[input->in++] = cmd;
    input->bytes[input->in++] = value;
}

uint32_t hal_tick_ms(void) { return 0; }
uint64_t hal_tick_us(void) { return 0; }
