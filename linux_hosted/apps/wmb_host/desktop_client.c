#include "wmb_app_table.h"
#include "wmb.h"
#include "wmb_xlib.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>

static int demo(int argc, char **argv)
{
    static unsigned instance;
    unsigned number = ++instance;
    Display *display = XOpenDisplay(NULL);
    Window window;
    Atom delete_window;
    char title[48];
    bool closing = false;
    unsigned width, height;
    XSizeHints hints = {0};
    GC gc;
    (void)argc; (void)argv;
    if (!display) {
        fputs("WMB demo: display connection failed\n", stderr);
        return 1;
    }
    width = DisplayWidth(display, 0) > 500 ? 240u : 128u;
    height = DisplayHeight(display, 0) > 300 ? 120u : 60u;
    hints.x = number % 2 ? 25 : DisplayWidth(display, 0) / 2;
    hints.y = number % 2 ? 35 : 70;
    window = XCreateSimpleWindow(display, DefaultRootWindow(display),
        hints.x, hints.y, width, height, 0, BlackPixel(display, 0), WhitePixel(display, 0));
    snprintf(title, sizeof(title), "WMB application %u", number);
    XStoreName(display, window, title);
    hints.flags = USPosition | USSize | PMinSize;
    hints.width = (int)width;
    hints.height = (int)height;
    hints.min_width = 80;
    hints.min_height = 40;
    XSetWMNormalHints(display, window, &hints);
    delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &delete_window, 1);
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);
    XMapWindow(display, window);
    gc = DefaultGC(display, 0);
    XSetForeground(display, gc, BlackPixel(display, 0));
    while (!closing && !wmb_stop_requested()) {
        (void)wmb_xlib_wait(display, 20);
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == Expose && event.xexpose.count == 0) {
                XDrawString(display, window, gc, 10, 25, title, (int)strlen(title));
                XFillRectangle(display, window, gc, 12, 35, 44, 16);
            }
            if (event.type == ClientMessage &&
                (Atom)event.xclient.data.l[0] == delete_window)
                closing = true;
        }
    }
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

const wmb_app_t wmb_app_table[] = {
    {"demo", demo, 256 * 1024}
};
const size_t wmb_app_table_size = sizeof(wmb_app_table) / sizeof(wmb_app_table[0]);
