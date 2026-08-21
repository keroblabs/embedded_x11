/*
 * sdl_fiber_example.c - Cooperative tasks driving the platform contract.
 *
 * Three tasks each own a 50x50 box on an 800x480 framebuffer and
 * toggle it between its colour and black on their own period
 * (1000 ms, 800 ms and 500 ms) for ten seconds.  Scheduling is purely
 * cooperative: every switch comes from hal_yield_timeout().
 *
 * Everything below the application goes through the contract:
 * hal_display.h, hal_input.h, hal_task.h and hal_time.h.
 */
#include "hal_display.h"
#include "hal_input.h"
#include "hal_task.h"
#include "hal_time.h"
#include "sdl_main.h"

#include <stdio.h>
#include <time.h>

#define DEMO_RUN_MS         10000u
#define DEMO_BOX_COUNT      3

/* No preemption source on this port yet; the tick is a backstop only. */
#define DEFAULT_SAFETY_TICK_MS  10u

#define SCREEN_WIDTH        800u
#define SCREEN_HEIGHT       480u

#define BOX_WIDTH           50u
#define BOX_HEIGHT          50u

#define RGB565(r, g, b)     ((uint16_t)((((r) & 0xF8u) << 8) | \
                                        (((g) & 0xFCu) << 3) | \
                                        (((b) & 0xF8u) >> 3)))

#define COLOUR_OFF          RGB565(0u, 0u, 0u)

typedef struct
{
    const char *name;
    uint32_t    period_ms;
    uint32_t    posx;
    uint32_t    posy;
    uint16_t    colour;
    uint32_t    ticks;
    bool        lit;

} box_t;

static box_t g_boxes[DEMO_BOX_COUNT] =
{
    { "white-1000ms", 1000u,  10u, 10u, RGB565(255u, 255u, 255u), 0u, false },
    { "red-800ms",     800u,  70u, 10u, RGB565(255u, 128u,  64u), 0u, false },
    /* Requested green is (64,256,64); 256 does not fit a channel, clamped to 255. */
    { "green-500ms",   500u, 130u, 10u, RGB565( 64u, 255u,  64u), 0u, false },
};

/* Hands the CPU back to the OS once per scheduling round; without it
 * the cooperative ring would spin a core flat out for ten seconds. */
static void host_idle_1ms(void)
{
    struct timespec ts = { 0, 1000000L };
    (void)nanosleep(&ts, NULL);
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    uint32_t row;
    uint32_t col;

    if (fb == NULL)
    {
        return;
    }

    for (row = 0u; row < h; row++)
    {
        uint16_t *line = (uint16_t *)((uint8_t *)fb->pixels + ((y + row) * fb->stride));

        for (col = 0u; col < w; col++)
        {
            line[x + col] = colour;
        }
    }

    hal_display_flush((int32_t)x, (int32_t)y, w, h);
}

static void box_entry(void *arg)
{
    box_t *box = (box_t *)arg;

    for (;;)
    {
        (void)hal_yield_timeout(box->period_ms);

        box->lit = !box->lit;
        box->ticks++;

        fill_rect(box->posx, box->posy, BOX_WIDTH, BOX_HEIGHT,
                  box->lit ? box->colour : COLOUR_OFF);

        printf("[%5u ms] %-13s toggle %u -> %s\n", hal_tick_ms(),
               hal_task_name(hal_task_self()), box->ticks,
               box->lit ? "colour" : "black");
        fflush(stdout);
    }
}

int main(void)
{
    hal_task_t *tasks[DEMO_BOX_COUNT];
    const hal_framebuffer_t *fb;
    int i;

    if (hal_display_init(SCREEN_WIDTH, SCREEN_HEIGHT, HAL_PIXFMT_RGB565) != 0)
    {
        fprintf(stderr, "hal_display_init failed\n");
        return 1;
    }

    if (hal_input_init() != 0)
    {
        fprintf(stderr, "hal_input_init failed\n");
        hal_display_shutdown();
        return 1;
    }

    fb = hal_display_get_fb();

    /* The Linux port has no preemption source yet; the safety tick is
     * accepted for API parity with the bare-metal ports.            */
    hal_task_runtime_init(DEFAULT_SAFETY_TICK_MS);

    for (i = 0; i < DEMO_BOX_COUNT; i++)
    {
        tasks[i] = hal_task_create(g_boxes[i].name, box_entry, &g_boxes[i], 0u);
        if (tasks[i] == NULL)
        {
            fprintf(stderr, "hal_task_create failed for %s\n", g_boxes[i].name);
            hal_display_shutdown();
            return 1;
        }
    }

    printf("SDL task demo: %ux%u @ %u bpp, %u tasks, %u ms run time\n",
           fb->width, fb->height, (uint32_t)fb->bpp,
           (uint32_t)DEMO_BOX_COUNT, (uint32_t)DEMO_RUN_MS);
    fflush(stdout);

    /* Tasks are created blocked; nothing runs until it is released. */
    for (i = 0; i < DEMO_BOX_COUNT; i++)
    {
        hal_task_wake(tasks[i]);
    }

    uint32_t start_ms = hal_tick_ms();
    while ((hal_tick_ms() - start_ms) < DEMO_RUN_MS)
    {
        hal_task_yield();

        hal_input_pump();

        /* Backend-internal: the contract has no "user closed the window"
         * notion, and this smoke test lives in the hosted port. */
        if (sdl_is_running() == FALSE)
        {
            break;
        }

        host_idle_1ms();
    }

    for (i = 0; i < DEMO_BOX_COUNT; i++)
    {
        hal_task_destroy(tasks[i]);
    }
    hal_task_yield();   /* scheduler pass that reclaims the ZOMBIEs */

    if (sdl_is_running() == TRUE)
    {
        hal_input_shutdown();
        hal_display_shutdown();
    }

    printf("finished after %u ms\n", hal_tick_ms() - start_ms);
    for (i = 0; i < DEMO_BOX_COUNT; i++)
    {
        printf("  %-13s %u toggles (period %u ms)\n",
               g_boxes[i].name, g_boxes[i].ticks, g_boxes[i].period_ms);
    }

    return 0;
}
