#include "api_internal.h"
#include "wmb_gateway.h"
#include "wmb.h"
#include "wmb_bridge.h"
#include "wmb_xlib.h"
#include "hal_task.h"
#include "hal_time.h"
#include <X11/Xlibint.h>
#include <stdlib.h>

_Noreturn void wmb_api_fatal(const char *reason)
{
    fprintf(stderr, "WMB ABI: %s\n", reason);
    abort();
}

void wmb_api_require_owner(void)
{
    if (!wmb_gateway_is_owner())
        wmb_api_fatal("native table requires the runtime thread; foreign tasks must use the gateway");
    if (wmb_bridge_is_server())
        wmb_api_fatal("the server fiber cannot execute external client calls");
}

static int external_error(Display *display, XErrorEvent *event)
{
    (void)display;
    fprintf(stderr, "WMB ABI: X error %u request=%u serial=%lu\n",
            event->error_code, event->request_code, event->serial);
    wmb_api_fatal("external application X error (callback APIs are outside core v1)");
}

Display *wmb_api_open(const char *name)
{
    Display *display;
    if (!wmb_ready()) {
        fputs("WMB ABI: display open requires a ready component\n", stderr);
        return NULL;
    }
    display = XOpenDisplay(name);
    if (display && !wmb_ready()) {
        XCloseDisplay(display);
        fputs("WMB ABI: component stopped during display open\n", stderr);
        return NULL;
    }
    if (display) wmb_xlib_set_error_handler(display, external_error);
    return display;
}

int wmb_api_next_event(Display *display, XEvent *event)
{
    while (!XPending(display)) {
        if (wmb_gateway_stopping() || wmb_stop_requested()) {
            fputs("WMB ABI: event wait interrupted by component stop\n", stderr);
            return -1;
        }
        (void)wmb_xlib_wait(display, 10);
    }
    return XNextEvent(display, event);
}

XID wmb_api_alloc_id(Display *display) { return XAllocID(display); }
int wmb_api_yield(uint32_t milliseconds)
{
    uint32_t started = hal_tick_ms();
    if (wmb_stop_requested() || wmb_gateway_stopping()) return -1;
    if (!milliseconds) (void)hal_yield_timeout(0);
    while (milliseconds) {
        uint32_t elapsed;
        (void)hal_yield_timeout(milliseconds > 10 ? 10 : milliseconds);
        if (wmb_stop_requested() || wmb_gateway_stopping()) return -1;
        elapsed = hal_tick_ms() - started;
        if (elapsed >= milliseconds) break;
        milliseconds -= elapsed;
        started += elapsed;
    }
    return wmb_stop_requested() || wmb_gateway_stopping() ? -1 : 0;
}
uint32_t wmb_api_revision(void) { return WMB_API_VERSION; }
