#define _POSIX_C_SOURCE 200809L
#include "jwm_port.h"
#include "wmb.h"
#include "wmb_app_table.h"
#include "wmb_apps.h"
#include "wmb_xlib.h"
#include "hal_task.h"
#include "hal_time.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static WmbJwmState manager;
static bool stopping;
static unsigned launches;
static Display *observer;

bool wmb_ready(void) { return manager.ready && !stopping; }
bool wmb_stop_requested(void) { return stopping; }
int wmb_xlib_wait(Display *display, uint32_t milliseconds)
{
    if (XPending(display)) return 1;
    hal_yield_timeout(milliseconds > 2 ? 2 : milliseconds);
    return XPending(display) != 0;
}
XErrorHandler wmb_xlib_set_error_handler(Display *display, XErrorHandler handler)
{
    (void)display;
    return XSetErrorHandler(handler);
}

static int demo(int argc, char **argv)
{
    (void)argc; (void)argv;
    launches++;
    return 0;
}
const wmb_app_t wmb_app_table[] = {{"demo", demo, 64 * 1024}};
const size_t wmb_app_table_size = 1;

static int idle(void)
{
    struct timespec delay = {0, 1000000};
    while (nanosleep(&delay, &delay) != 0)
        if (errno != EINTR) { perror("standalone idle"); return -1; }
    return 0;
}

int main(void)
{
    Window windows[2] = {None, None};
    hal_task_t *task;
    uint32_t started;
    unsigned decorated = 0;
    int result = 1;
    if (hal_task_runtime_init(0) != 0) return 1;
    observer = XOpenDisplay(NULL);
    if (!observer) { fputs("Standalone: stock X display unavailable\n", stderr); return 1; }
    task = hal_task_create("jwm", WmbJwm_Run, &manager, 512 * 1024);
    if (!task) { fputs("Standalone: task allocation failed\n", stderr); goto close; }
    hal_task_wake(task);
    started = hal_tick_ms();
    while (!manager.ready && !manager.finished && hal_tick_ms() - started < 2000) {
        hal_task_yield();
        if (idle() != 0) goto stop;
    }
    if (!manager.ready || manager.finished) goto stop;
    for (unsigned index = 0; index < 2; index++) {
        windows[index] = XCreateSimpleWindow(observer, DefaultRootWindow(observer),
            20 + (int)index * 250, 40, 200, 100, 0,
            BlackPixel(observer, 0), WhitePixel(observer, 0));
        XStoreName(observer, windows[index], "Stock Xlib reference client");
        XMapWindow(observer, windows[index]);
    }
    XFlush(observer);
    while (hal_tick_ms() - started < 2000) {
        decorated = 0;
        hal_task_yield();
        wmb_apps_reap();
        for (unsigned index = 0; index < 2; index++) {
            Window root, parent, *children;
            unsigned count;
            if (XQueryTree(observer, windows[index], &root, &parent, &children, &count)) {
                if (parent != root) decorated++;
                XFree(children);
            }
        }
        if (decorated == 2) break;
        if (idle() != 0) goto stop;
    }
    if (decorated != 2 || manager.protocol_errors) goto stop;
    {
        char *arguments[] = {"demo"};
        hal_task_t *app = wmb_app_launch(&wmb_app_table[0], 1, arguments);
        if (!app) goto stop;
        hal_task_wake(app);
        for (unsigned attempt = 0; attempt < 20 && wmb_apps_count(); attempt++) {
            hal_task_yield();
            wmb_apps_reap();
        }
        if (launches != 1 || wmb_apps_count()) goto stop;
    }
    result = 0;
stop:
    WmbJwm_RequestStop();
    started = hal_tick_ms();
    while (!manager.finished && hal_tick_ms() - started < 2000) {
        hal_task_yield();
        if (idle() != 0) break;
    }
    if (!manager.finished || manager.result) result = 1;
    else hal_task_destroy(task);
close:
    for (unsigned index = 0; index < 2; index++)
        if (windows[index] != None) XDestroyWindow(observer, windows[index]);
    XCloseDisplay(observer);
    printf("P7 standalone: genuine stock Xlib, two decorated clients, launch and cleanup %s\n",
           result ? "FAIL" : "PASS");
    return result;
}
