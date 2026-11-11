#include "wmb_xlib_compat.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wmb_test_all_macros(Display *, Screen *, int);
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "wmb_hello: line %d failed: %s\n", __LINE__, #expression); \
        goto cleanup; \
    } \
} while (0)

static int parent_is(Display *display, Window window, Window root, bool managed, Window *parent)
{
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        Window actual_root, *children = NULL;
        unsigned count;
        if (!XQueryTree(display, window, &actual_root, parent, &children, &count)) return -1;
        if (children) XFree(children);
        if (actual_root != root) return -1;
        if ((*parent != root) == managed) return 0;
        if (wmbYield(1) < 0 && managed) return -1;
    }
    fputs("wmb_hello: WM reparent deadline\n", stderr);
    return -1;
}

int wmb_hello_main(unsigned iterations)
{
    Display *display = NULL;
    Window window = 0, root = None, parent;
    GC gc = NULL;
    Cursor cursor = None;
    int result = 1, number;
    XColor color;
    Atom property, actual;
    unsigned long count, after;
    int format;
    unsigned char *data = NULL;
    display = XOpenDisplay(NULL);
    CHECK(display);
    number = DefaultScreen(display); root = DefaultRootWindow(display);
    wmb_test_all_macros(display, DefaultScreenOfDisplay(display), number);
    CHECK(DisplayOfScreen(ScreenOfDisplay(display, number)) == display);
    CHECK(DefaultGCOfScreen(DefaultScreenOfDisplay(display)) == DefaultGC(display, number));
#if WMB_API_VERSION >= 2
    CHECK(wmbApiRevision() >= WMB_API_VERSION);
#endif
    unsigned width = DisplayWidth(display, number) > 400 ? 240u : 180u;
    unsigned height = DisplayHeight(display, number) > 220 ? 110u : 70u;
    window = XCreateSimpleWindow(display, root, 40, 50, width, height, 0, 0, 0);
    CHECK(window);
    XStoreName(display, window, "External WMB ABI application");
    {
        XSizeHints hints;
        hints.flags = PMinSize; hints.min_width = 100; hints.min_height = 50;
        XSetWMNormalHints(display, window, &hints);
    }
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | KeyPressMask |
                 ButtonPressMask | PointerMotionMask);
    XMapWindow(display, window);
    CHECK(parent_is(display, window, root, true, &parent) == 0);
    color.red = 0; color.green = 65535; color.blue = 0;
    CHECK(XAllocColor(display, DefaultColormap(display, number), &color));
    {
        XGCValues values;
        values.foreground = color.pixel; values.background = BlackPixel(display, number);
        gc = XCreateGC(display, window, GCForeground | GCBackground, &values);
        CHECK(gc);
    }
    cursor = XCreateFontCursor(display, 68);
    CHECK(cursor);
    XDefineCursor(display, window, cursor);
    property = XInternAtom(display, "WMB_HELLO_PROPERTY", False);
    {
        char *name = XGetAtomName(display, property);
        CHECK(name && strcmp(name, "WMB_HELLO_PROPERTY") == 0);
        XFree(name);
    }
    {
        unsigned char bytes[] = {'a', 0, 'b', 0xff, 0x7f};
        unsigned short words[] = {0, 0xbeef, 0xffff};
        XChangeProperty(display, window, property, property, 8, PropModeReplace, bytes, 5);
        CHECK(XGetWindowProperty(display, window, property, 0, 4, False, property,
                                &actual, &format, &count, &after, &data) == Success);
        CHECK(format == 8 && count == 5 && !after && memcmp(data, bytes, 5) == 0 && data[5] == 0);
        XFree(data); data = NULL;
        XChangeProperty(display, window, property, property, 16, PropModeReplace,
                        (const unsigned char *)words, 3);
        CHECK(XGetWindowProperty(display, window, property, 0, 4, False, property,
                                &actual, &format, &count, &after, &data) == Success);
        CHECK(format == 16 && count == 3 && memcmp(data, words, sizeof(words)) == 0);
        XFree(data); data = NULL;
    }
    for (unsigned iteration = 0; iteration < iterations; iteration++) {
        unsigned long words[] = {iteration, 0xfedcba98, 0xffffffff, window};
        XChangeProperty(display, window, property, property, 32, PropModeReplace,
                        (const unsigned char *)words, 4);
        CHECK(XGetWindowProperty(display, window, property, 0, 4, False, property,
                                &actual, &format, &count, &after, &data) == Success);
        CHECK(actual == property && format == 32 && count == 4 && after == 0 && data);
        for (size_t index = 0; index < 4; index++)
            CHECK((((unsigned long *)(void *)data)[index] & 0xffffffffUL) == words[index]);
        XFree(data); data = NULL;
    }
    {
        XEvent event = {0}, received;
        event.xclient.type = ClientMessage; event.xclient.display = display;
        event.xclient.window = window; event.xclient.message_type = property;
        event.xclient.format = 32; event.xclient.data.l[0] = 1234567;
        XPutBackEvent(display, &event);
        CHECK(XCheckTypedWindowEvent(display, window, ClientMessage, &received));
        CHECK(received.xclient.display == display && received.xclient.data.l[0] == 1234567);
        CHECK(XSendEvent(display, window, False, NoEventMask, &event));
        XSync(display, False);
        CHECK(XCheckTypedWindowEvent(display, window, ClientMessage, &received));
        CHECK(received.xclient.display == display && received.xclient.data.l[0] == 1234567);
        event = (XEvent){0};
        event.xkey.type = KeyPress; event.xkey.window = window; event.xkey.display = display;
        int queued = QLength(display);
        XPutBackEvent(display, &event);
        CHECK(QLength(display) == queued + 1);
        CHECK(XPending(display) >= 1);
        CHECK(!XCheckMaskEvent(display, 0, &received));
        CHECK(XCheckMaskEvent(display, KeyPressMask, &received));
        CHECK(received.xany.display == display && received.type == KeyPress);
        XPutBackEvent(display, &event);
        XSync(display, True);
        CHECK(QLength(display) == 0);
    }
    XFillRectangle(display, window, gc, 0, 0, width, height);
    XSetForeground(display, gc, BlackPixel(display, number));
    XDrawString(display, window, gc, 12, 38, "External ABI: hello", 19);
    XFlush(display); XSync(display, False);
    {
        XWindowAttributes child, frame;
        CHECK(XGetWindowAttributes(display, window, &child));
        CHECK(XGetWindowAttributes(display, parent, &frame));
        CHECK(child.screen == DefaultScreenOfDisplay(display));
        CHECK(child.visual == DefaultVisual(display, number));
        printf("P8 HELLO READY: ABI=%u window=%lu parent=%lu root=%lu x=%d y=%d width=%u height=%u "
               "pixel=%lu iterations=%u\n", WMB_API_VERSION, window, parent, root,
               frame.x + frame.border_width + child.x + child.border_width,
               frame.y + frame.border_width + child.y + child.border_width,
               width, height, color.pixel, iterations);
        fflush(stdout);
    }
    for (unsigned tick = 0; tick < 300 && wmbYield(10) >= 0; tick++) {
        while (XPending(display)) {
            XEvent event;
            if (XNextEvent(display, &event) < 0) break;
            if (event.type == Expose) {
                XSetForeground(display, gc, color.pixel);
                XFillRectangle(display, window, gc, 0, 0, width, height);
                XSetForeground(display, gc, BlackPixel(display, number));
                XDrawString(display, window, gc, 12, 38, "External ABI: hello", 19);
                XFlush(display);
            }
        }
    }
    result = 0;
cleanup:
    if (data) XFree(data);
    if (display && gc) XFreeGC(display, gc);
    if (display && cursor) XFreeCursor(display, cursor);
    if (display && window) {
        XUnmapWindow(display, window);
        if (parent_is(display, window, root, false, &parent) != 0) result = 1;
        XDestroyWindow(display, window);
    }
    if (display) XCloseDisplay(display);
    printf("P8 HELLO EXIT: ABI=%u %s\n", WMB_API_VERSION, result ? "FAIL" : "PASS");
    return result;
}
