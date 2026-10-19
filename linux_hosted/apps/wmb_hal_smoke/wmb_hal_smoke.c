/*
 * Visual hosted HAL smoke: no TinyX, KDrive or Xlib is involved.
 * Compile with the SDL backend and use a finite run for automation.
 */
#include "hal_display.h"
#include "hal_input.h"
#include "hal_time.h"
#include "hal_task.h"
#include "sdl_main.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define WIDTH 800u
#define HEIGHT 480u

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                      ((uint16_t)(g & 0xfcu) << 3) | (b >> 3));
}

static void draw_gradient(const hal_framebuffer_t *fb)
{
    uint32_t x, y;
    for (y = 0; y < fb->height; ++y) {
        uint16_t *line = (uint16_t *)((uint8_t *)fb->pixels + y * fb->stride);
        for (x = 0; x < fb->width; ++x)
            line[x] = rgb565((uint8_t)(x * 255u / WIDTH),
                             (uint8_t)(y * 255u / HEIGHT),
                             96);
    }
    hal_display_flush(0, 0, fb->width, fb->height);
}

static void draw_box(const hal_framebuffer_t *fb, uint32_t x, uint16_t colour)
{
    uint32_t row, col;
    for (row = 40; row < 90; ++row) {
        uint16_t *line = (uint16_t *)((uint8_t *)fb->pixels + row * fb->stride);
        for (col = x; col < x + 50 && col < fb->width; ++col)
            line[col] = colour;
    }
    hal_display_flush((int32_t)x, 40, 50, 50);
}

static void drain_input(hal_input_dev_t dev)
{
    static uint8_t pending[HAL_INPUT_DEV_MAX];
    static uint8_t command[HAL_INPUT_DEV_MAX];
    uint8_t value;
    while (hal_input_read_byte(dev, &value)) {
        if (!pending[dev]) {
            pending[dev] = 1;
            command[dev] = value;
        } else {
            printf("%s: command 0x%02x value 0x%02x\n",
                   dev == HAL_INPUT_KEYBOARD ? "keyboard" : "mouse",
                   command[dev], value);
            pending[dev] = 0;
        }
    }
}

int main(int argc, char **argv)
{
    const hal_framebuffer_t *fb;
    uint32_t duration = 15000u, start, prev = 0, box_x = 0;
    const struct timespec idle = { 0, 10000000L };

    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == '-' &&
        argv[1][2] == 's') {
        unsigned long seconds = strtoul(argv[2], NULL, 10);
        if (seconds < 1 || seconds > 300) {
            fprintf(stderr, "expected --seconds 1..300\n");
            return 2;
        }
        duration = (uint32_t)(seconds * 1000u);
    }

    if (hal_display_init(WIDTH, HEIGHT, HAL_PIXFMT_RGB565) != 0) {
        fprintf(stderr, "FAIL: SDL display init (need RGB565 800x480)\n");
        return 1;
    }
    if (hal_input_init() != 0) {
        fprintf(stderr, "FAIL: SDL input init\n");
        hal_display_shutdown();
        return 1;
    }
    fb = hal_display_get_fb();
    if (!fb || !fb->pixels || fb->width != WIDTH || fb->height != HEIGHT ||
        fb->bpp != 16 || fb->stride < WIDTH * 2u) {
        fprintf(stderr, "FAIL: framebuffer contract\n");
        hal_input_shutdown();
        hal_display_shutdown();
        return 1;
    }

    puts("PASS: HAL framebuffer RGB565 800x480");
    puts("Expect gradient and a moving square; press keys/click/scroll to");
    puts("see HAL command/data pairs. Close window or wait for timeout.");
    draw_gradient(fb);
    start = hal_tick_ms();
    while (hal_tick_ms() - start < duration) {
        uint32_t now = hal_tick_ms();
        if (now - prev >= 100u) {
            /* Redraw background on each move so stale boxes disappear. */
            draw_gradient(fb);
            box_x = (box_x + 10u) % (WIDTH - 50u);
            draw_box(fb, box_x, rgb565(255, 255, 255));
            prev = now;
        }
        hal_input_pump();
        drain_input(HAL_INPUT_KEYBOARD);
        drain_input(HAL_INPUT_MOUSE);
        if (sdl_is_running() == FALSE) break;
        (void)nanosleep(&idle, NULL); /* Hosted test only, not KDrive. */
    }
    if (sdl_is_running() == TRUE) {
        hal_input_shutdown();
        hal_display_shutdown();
    }
    puts("PASS: HAL display/input smoke completed");
    return 0;
}
