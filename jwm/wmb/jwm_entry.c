#include "jwm.h"
#include "main.h"
#include "error.h"
#include "event.h"
#include "grab.h"
#include "jwm_port.h"
#include "hal_task.h"
#include "hal_time.h"
#include "fiber.h"
#include "wmb_xlib.h"

extern int Jwm_Main(int argc, char **argv);
static WmbJwmState *state;
static jmp_buf terminate;
static bool desired_grab, held_grab;
static uint64_t formatted_seconds;

const WmbJwmState *WmbJwm_Status(void) { return state; }
uint64_t WmbJwm_UptimeSeconds(void) { return (hal_tick_us() - state->started_us) / 1000000; }
void WmbJwm_TestGrab(bool held) { desired_grab = held; fiber_notify(); }
bool WmbJwm_TestGrabHeld(void) { return held_grab; }

void WmbJwm_ResetMetrics(void)
{
    state->loop_count = state->last_loop_us = state->max_loop_gap_us = 0;
    state->callbacks = state->last_callback_us = state->max_callback_gap_us = 0;
    state->clock_updates = state->last_clock_us = state->max_clock_gap_us = 0;
    state->min_clock_gap_us = UINT64_MAX;
    state->last_clock = UINT64_MAX;
}

static void callback(const struct TimeType *now, int x, int y, Window window, void *argument)
{
    uint64_t tick = hal_tick_us();
    (void)now; (void)x; (void)y; (void)window; (void)argument;
    if (state->last_callback_us && tick - state->last_callback_us > state->max_callback_gap_us)
        state->max_callback_gap_us = tick - state->last_callback_us;
    state->last_callback_us = tick;
    state->callbacks++;
}

void WmbJwm_ClockFormatted(uint64_t seconds)
{
    formatted_seconds = seconds;
}

void WmbJwm_ClockDrawn(void)
{
    if (formatted_seconds != state->last_clock) {
        uint64_t now = hal_tick_us();
        if (state->last_clock_us && now - state->last_clock_us > state->max_clock_gap_us)
            state->max_clock_gap_us = now - state->last_clock_us;
        if (state->last_clock_us && now - state->last_clock_us < state->min_clock_gap_us)
            state->min_clock_gap_us = now - state->last_clock_us;
        state->last_clock_us = now;
        state->last_clock = formatted_seconds;
        state->clock_updates++;
    }
}

const char *WmbJwm_Configuration(void)
{
    return state->configuration ? state->configuration : WmbJwm_DefaultConfiguration;
}

void WmbJwm_Run(void *argument)
{
    char *arguments[] = {"JWM", NULL};
    int result;
    state = argument;
    state->result = -1;
    state->started_us = hal_tick_us();
    state->last_clock = UINT64_MAX;
    result = setjmp(terminate);
    if (result == 0)
        state->result = Jwm_Main(1, arguments);
    else {
        state->result = result - 1;
        if (display) {
            XCloseDisplay(display);
            display = NULL;
        }
    }
    if (state->ready) UnregisterCallback(callback, NULL);
    state->finished = true;
    fiber_notify();
    for (;;) hal_task_wait();
}

_Noreturn void WmbJwm_Terminate(int result)
{
    fprintf(stderr, "WMB JWM: component terminated with status %d\n", result);
    longjmp(terminate, result ? result + 1 : 1);
}

void WmbJwm_RequestStop(void)
{
    shouldExit = 1;
    fiber_notify();
}

void WmbJwm_Ready(void)
{
    if (!state->ready) RegisterCallback(100, callback, NULL);
    state->ready = true;
    state->ready_us = hal_tick_us();
    fiber_notify();
}

void WmbJwm_Pulse(void)
{
    uint64_t now = hal_tick_us();
    if (state->last_loop_us && now - state->last_loop_us > state->max_loop_gap_us)
        state->max_loop_gap_us = now - state->last_loop_us;
    state->last_loop_us = now;
    state->loop_count++;
    if (desired_grab != held_grab) {
        if (desired_grab) GrabServer();
        else UngrabServer();
        held_grab = desired_grab;
        fiber_notify();
    }
}

void WmbJwm_Wait(uint32_t milliseconds)
{
    (void)wmb_xlib_wait(display, milliseconds > 10 ? 10 : milliseconds);
}

static int protocol_error(Display *connection, XErrorEvent *error)
{
    state->protocol_errors++;
    fprintf(stderr, "WMB JWM: protocol error=%u request=%u resource=0x%lx\n",
            error->error_code, error->request_code, error->resourceid);
    return ErrorHandler(connection, error);
}

void WmbJwm_InstallErrorHandler(void)
{
    wmb_xlib_set_error_handler(display, protocol_error);
}
