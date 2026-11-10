#include "wmb_test_client.h"
#include "wmb_xlib_internal.h"
#include "hal_time.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t started;

static int expected_timeout(Display *display)
{
    uint32_t elapsed = hal_tick_ms() - started;
    (void)display;
    fprintf(stderr, "P5 timeout: detected after %u ms\n", elapsed);
    fflush(stderr);
    _Exit(elapsed >= WMB_REPLY_TIMEOUT_MS && elapsed < WMB_REPLY_TIMEOUT_MS + 1000 ? 77 : 78);
}

void wmb_timeout_test_client(void *argument)
{
    Display *display = XOpenDisplay(NULL);
    xReply reply;
    (void)argument;
    if (!display) _Exit(79);
    XSetIOErrorHandler(expected_timeout);
    started = hal_tick_ms();
    if (!_XGetRequest(display, X_NoOperation, 4)) _Exit(79);
    (void)_XReply(display, &reply, 0, True);
    _Exit(79);
}
