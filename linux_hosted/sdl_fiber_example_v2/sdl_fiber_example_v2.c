/*
 * sdl_fiber_example_v2.c - Proof of concept for the in-process transport.
 *
 * Four fibers.  Three clients each own a 50x50 box and, on their own
 * period (500 / 800 / 1000 ms), post a draw request to the fourth: the
 * render fiber, the only context that ever touches the framebuffer.
 *
 * The request path is the one docs/21-inprocess-transport.md specifies:
 * a client appends an owned node to a framework/library/lists.c queue,
 * calls fiber_notify() and parks on a predicate over its own request
 * (section 4.1); the render fiber parks on "queue not empty" with a
 * bounded idle tick (section 5.6), unlinks one request at a time, serves
 * it to completion, and notifies once per drain pass rather than once per
 * request (section 5.5).
 */
#include "hal_display.h"
#include "hal_input.h"
#include "hal_task.h"
#include "hal_time.h"
#include "lists.h"
#include "fiber.h"
#include "sdl_main.h"

#include <stdio.h>
#include <time.h>

#define DEMO_RUN_MS             10000u
#define DEMO_CLIENT_COUNT       3

/* No preemption source on this port yet; the tick is a backstop only. */
#define DEFAULT_SAFETY_TICK_MS  10u

/* Ceiling on how long the render fiber sleeps with an empty queue.  It is
 * a latency bound, not a period: a posted request wakes it immediately. */
#define RENDER_IDLE_TICK_MS     2000u

/* A completion that never arrives is a bug, so the client waits with a
 * deadline and reports it instead of hanging (section 4.1, risk R5). */
#define REQUEST_TIMEOUT_MS      2000u

#define SCREEN_WIDTH            800u
#define SCREEN_HEIGHT           480u

#define BOX_WIDTH               50u
#define BOX_HEIGHT              50u

#define RGB565(r, g, b)         ((uint16_t)((((r) & 0xF8u) << 8) | \
                                            (((g) & 0xFCu) << 3) | \
                                            (((b) & 0xF8u) >> 3)))

#define COLOUR_OFF              RGB565(0u, 0u, 0u)

typedef enum
{
    REQ_QUEUED = 0,
    REQ_DONE

} req_state_t;

typedef struct
{
    LIST_HEADER;                /* list_node_t node - MUST be the first member */

    uint32_t     posx;
    uint32_t     posy;
    uint32_t     width;
    uint32_t     height;
    uint16_t     colour;

    uint32_t     seq;
    const char  *origin;

    /* Written by the render fiber, read by the client's predicate. */
    volatile req_state_t state;

} draw_req_t;

typedef struct
{
    const char *name;
    uint32_t    period_ms;
    uint32_t    posx;
    uint32_t    posy;
    uint16_t    colour;
    uint32_t    sent;
    uint32_t    acked;
    uint32_t    timeouts;
    bool_t      lit;

} client_t;

static client_t g_clients[DEMO_CLIENT_COUNT] =
{
    { "white-1000ms", 1000u,  10u, 10u, RGB565(255u, 255u, 255u), 0u, 0u, 0u, FALSE },
    { "red-800ms",     800u,  70u, 10u, RGB565(255u, 128u,  64u), 0u, 0u, 0u, FALSE },
    { "green-500ms",   500u, 130u, 10u, RGB565( 64u, 255u,  64u), 0u, 0u, 0u, FALSE },
};

/* The transport: clients append at the tail, the render fiber unlinks
 * from the head.  Nodes are owned by the client that posted them. */
static list_data_t g_queue;

static uint32_t    g_served;
static uint32_t    g_max_depth;

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

/* ------------------------------------------------------------------
 * Render fiber - the server side
 * ------------------------------------------------------------------ */

static bool_t render_has_work(void *data)
{
    (void)data;
    return (list_is_empty(&g_queue) == FALSE) ? TRUE : FALSE;
}

static void render_entry(void *arg)
{
    (void)arg;

    while (TRUE)
    {
        (void)fiber_wait_for(render_has_work, NULL, RENDER_IDLE_TICK_MS);

        while (TRUE)
        {
            /* head-then-unlink is compound, and safe only because this
             * fiber is the sole consumer and clients only append. */
            draw_req_t *req   = (draw_req_t *)list_get_head(&g_queue);
            uint16_t    depth = list_size(&g_queue);

            if (req == NULL)
            {
                break;
            }

            if (depth > (uint16_t)g_max_depth)
            {
                g_max_depth = depth;
            }

            if (list_unlink(&g_queue, &req->node) == FALSE)
            {
                break;
            }

            fill_rect(req->posx, req->posy, req->width, req->height, req->colour);
            g_served++;

            printf("[%5u ms] render <- %-13s seq %3u  depth %u\n", hal_tick_ms(), req->origin, req->seq, (uint32_t)depth);
            fflush(stdout);

            /* Completion, and the only edge the client waits on. */
            req->state = REQ_DONE;
        }

        fiber_notify();     /* once per drain pass, never per request */
    }
}

/* ------------------------------------------------------------------
 * Client fibers
 * ------------------------------------------------------------------ */

static bool_t request_done(void *data)
{
    const draw_req_t *req = (const draw_req_t *)data;
    return (req->state == REQ_DONE) ? TRUE : FALSE;
}

static void client_dispatch(client_t *client)
{
    draw_req_t *req = (draw_req_t *)MMALLOC(sizeof(draw_req_t));

    if (req == NULL)
    {
        return;
    }

    req->posx   = client->posx;
    req->posy   = client->posy;
    req->width  = BOX_WIDTH;
    req->height = BOX_HEIGHT;
    req->colour = (client->lit == TRUE) ? client->colour : COLOUR_OFF;
    req->seq    = ++client->sent;
    req->origin = client->name;
    req->state  = REQ_QUEUED;

    list_insert_tail(&g_queue, &req->node);
    fiber_notify();

    if (fiber_wait_for(request_done, req, REQUEST_TIMEOUT_MS) == TRUE)
    {
        client->acked++;
    }
    else
    {
        client->timeouts++;

        /* Winning the unlink proves the render fiber never took the
         * request, so it is ours to free; losing it means the request is
         * in flight and only completion may release it. */
        if (list_unlink(&g_queue, &req->node) == FALSE)
        {
            (void)fiber_wait_for(request_done, req, FIBER_WAIT_FOREVER);
        }
    }

    MFREE(req);
}

static void client_entry(void *arg)
{
    client_t *client = (client_t *)arg;

    while (TRUE)
    {
        (void)hal_yield_timeout(client->period_ms);

        client->lit = (client->lit == TRUE) ? FALSE : TRUE;
        client_dispatch(client);
    }
}

int main(void)
{
    hal_task_t *render;
    hal_task_t *clients[DEMO_CLIENT_COUNT];
    const hal_framebuffer_t *fb;
    uint32_t start_ms;
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

    /* No delete callback: a request owns nothing beyond itself. */
    list_init(&g_queue, NULL);

    hal_task_runtime_init(DEFAULT_SAFETY_TICK_MS);

    render = hal_task_create("render", render_entry, NULL, 0u);
    if (render == NULL)
    {
        fprintf(stderr, "hal_task_create failed for render\n");
        hal_display_shutdown();
        return 1;
    }

    for (i = 0; i < DEMO_CLIENT_COUNT; i++)
    {
        clients[i] = hal_task_create(g_clients[i].name, client_entry, &g_clients[i], 0u);
        if (clients[i] == NULL)
        {
            fprintf(stderr, "hal_task_create failed for %s\n", g_clients[i].name);
            hal_display_shutdown();
            return 1;
        }
    }

    printf("SDL in-process transport demo: %ux%u @ %u bpp, %u fibers, %u ms run time\n",
           fb->width, fb->height, (uint32_t)fb->bpp,
           (uint32_t)DEMO_CLIENT_COUNT + 1u, (uint32_t)DEMO_RUN_MS);
    fflush(stdout);

    /* Tasks are created blocked; the server goes first so no request can
     * be posted before there is something to serve it. */
    hal_task_wake(render);
    for (i = 0; i < DEMO_CLIENT_COUNT; i++)
    {
        hal_task_wake(clients[i]);
    }

    start_ms = hal_tick_ms();
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

    for (i = 0; i < DEMO_CLIENT_COUNT; i++)
    {
        hal_task_destroy(clients[i]);
    }
    hal_task_destroy(render);
    hal_task_yield();   /* scheduler pass that reclaims the ZOMBIEs */

    /* Every client is gone, so anything still queued is unowned. */
    list_clear(&g_queue);

    if (sdl_is_running() == TRUE)
    {
        hal_input_shutdown();
        hal_display_shutdown();
    }

    printf("finished after %u ms\n", hal_tick_ms() - start_ms);
    printf("  render      %u requests served, peak queue depth %u\n",
           g_served, g_max_depth);
    for (i = 0; i < DEMO_CLIENT_COUNT; i++)
    {
        printf("  %-13s %u sent, %u completed, %u timed out (period %u ms)\n",
               g_clients[i].name, g_clients[i].sent, g_clients[i].acked,
               g_clients[i].timeouts, g_clients[i].period_ms);
    }

    return 0;
}
