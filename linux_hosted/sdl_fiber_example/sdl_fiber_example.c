/*
 * Cooperative display tasks driving the platform contract.
 *
 * Three tasks each own a 50x50 box on the configured framebuffer and
 * toggle it between its colour and black on their own period
 * (1000 ms, 800 ms and 500 ms) for ten seconds.  Scheduling is purely
 * cooperative: every switch comes from hal_yield_timeout().
 *
 * Everything below the application goes through the contract:
 * hal_display.h, hal_input.h, hal_task.h and hal_time.h.
 */
#include "hal_display.h"
#include "hal_task.h"
#include "hal_time.h"
#include "../apps/common/framebuffer_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEMO_RUN_MS         10000u
#define DEMO_BOX_COUNT      3

/* No preemption source on this port yet; the tick is a backstop only. */
#define DEFAULT_SAFETY_TICK_MS  10u

#define BOX_WIDTH           50u
#define BOX_HEIGHT          50u

#define DEMO_RGB(r, g, b)   FRAMEBUFFER_RGB(r, g, b)

#define COLOUR_OFF          DEMO_RGB(0u, 0u, 0u)

typedef struct
{
    const char *name;
    uint32_t    period_ms;
    uint32_t    posx;
    uint32_t    posy;
    uint32_t    colour;
    uint32_t    ticks;
    bool        lit;

} box_t;

static box_t g_boxes[DEMO_BOX_COUNT] =
{
    { "white-1000ms", 1000u,  10u, 10u, DEMO_RGB(255u, 255u, 255u), 0u, false },
    { "red-800ms",     800u,  70u, 10u, DEMO_RGB(255u, 128u,  64u), 0u, false },
    /* Requested green is (64,256,64); 256 does not fit a channel, clamped to 255. */
    { "green-500ms",   500u, 130u, 10u, DEMO_RGB( 64u, 255u,  64u), 0u, false },
};

/* Hands the CPU back to the OS once per scheduling round; without it
 * the cooperative ring would spin a core flat out for ten seconds. */
static void host_idle_1ms(void)
{
    struct timespec ts = { 0, 1000000L };
    (void)nanosleep(&ts, NULL);
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    if (framebuffer_fill_rect(fb, x, y, w, h, colour) < 0)
    {
        fprintf(stderr, "Invalid framebuffer in graphics example\n");
        abort();
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
    int result = 0;

    if (hal_display_init() != 0)
    {
        fprintf(stderr, "hal_display_init failed\n");
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

    printf("Framebuffer task demo: %ux%u @ %u bpp, %u tasks, %u ms run time\n",
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

        hal_display_status_t status = hal_display_pump();
        if (status == HAL_DISPLAY_STOP_REQUESTED)
        {
            break;
        }
        if (status == HAL_DISPLAY_ERROR)
        {
            fprintf(stderr, "Display pump failed\n");
            result = 1;
            break;
        }

        host_idle_1ms();
    }

    for (i = 0; i < DEMO_BOX_COUNT; i++)
    {
        hal_task_destroy(tasks[i]);
    }
    hal_task_yield();   /* scheduler pass that reclaims the ZOMBIEs */

    hal_display_shutdown();

    printf("finished after %u ms\n", hal_tick_ms() - start_ms);
    for (i = 0; i < DEMO_BOX_COUNT; i++)
    {
        printf("  %-13s %u toggles (period %u ms)\n",
               g_boxes[i].name, g_boxes[i].ticks, g_boxes[i].period_ms);
    }

    return result;
}
