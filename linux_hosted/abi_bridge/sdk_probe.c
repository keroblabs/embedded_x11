#include "wmb_api.h"
#if defined(WMB_TEST_LEGACY)
#include "wmb_test_wrappers.h"
#define PREPARE wmb_probe_v1_prepare
#define ENTRY wmb_probe_v1_entry
#define BIND wmb_app_wmb_bind
#define ALL_MACROS wmb_v1_all_macros
#define TITLE "Frozen ABI v1"
#else
#define PREPARE wmb_probe_v2_prepare
#define ENTRY wmb_probe_v2_entry
#define BIND wmb_bind
#define ALL_MACROS wmb_v2_all_macros
#define TITLE "Typed ABI v2"
#endif
#include "wmb_xlib_compat.h"
#include "probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WmbAbiProbe *state;
void ALL_MACROS(Display *display, Screen *screen, int number);
void PREPARE(WmbAbiProbe *configuration) { state = configuration; }
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "P8 " TITLE " FAIL line %d: %s\n", __LINE__, #expression); \
        goto failed; \
    } \
} while (0)

static int await_parent(Display *display, Window window, Window root, Window *parent, bool managed)
{
    uint32_t started = state->now_ms;
    while (state->now_ms - started < 2000) {
        Window actual_root, *children = NULL;
        unsigned count;
        if (!XQueryTree(display, window, &actual_root, parent, &children, &count))
            return -1;
        if (children) XFree(children);
        if (actual_root != root) return -1;
        if ((*parent != root) == managed) return 0;
        if (wmbYield(1) < 0) return -1;
    }
    fputs("P8 SDK: JWM decoration/withdrawal deadline exceeded\n", stderr);
    return -1;
}

static int cycle(Display *display, Window root)
{
    Window parent;
    Window window = XCreateSimpleWindow(display, root, 40, 50, 80, 48, 0, 0, 0);
    if (!window) return -1;
    XStoreName(display, window, "ABI cycle");
    XMapWindow(display, window);
    if (await_parent(display, window, root, &parent, true) != 0) {
        XDestroyWindow(display, window);
        return -1;
    }
    XUnmapWindow(display, window);
    if (await_parent(display, window, root, &parent, false) != 0) {
        XDestroyWindow(display, window);
        return -1;
    }
    XDestroyWindow(display, window);
    XSync(display, False);
    return wmbYield(1);
}

int ENTRY(int argc, char **argv)
{
    Display *display = NULL;
    GC gc = NULL;
    Window window = 0, extra = 0, root, parent;
    Pixmap pixmap = 0;
    Cursor cursor = 0;
    XColor color = {.red = 0, .green = 65535, .blue = 0};
    int number, result = -1;
    (void)argc;
    (void)argv;
    if (!state) {
        fputs("P8 SDK probe: missing configuration\n", stderr);
        return -1;
    }
    CHECK(BIND(state->table) == 0);
    display = XOpenDisplay(NULL);
    CHECK(display);
    number = DefaultScreen(display);
    root = RootWindow(display, number);
    ALL_MACROS(display, ScreenOfDisplay(display, number), number);
    CHECK(ScreenCount(display) == 1 && DefaultRootWindow(display) == root);
    CHECK(DisplayOfScreen(DefaultScreenOfDisplay(display)) == display);
    CHECK(DisplayWidth(display, number) == WidthOfScreen(DefaultScreenOfDisplay(display)));
    CHECK(DisplayHeight(display, number) == HeightOfScreen(DefaultScreenOfDisplay(display)));
    CHECK(DefaultGC(display, number) == DefaultGCOfScreen(DefaultScreenOfDisplay(display)));
    CHECK(DefaultVisual(display, number) == DefaultVisualOfScreen(DefaultScreenOfDisplay(display)));
    CHECK(XVisualIDFromVisual(DefaultVisual(display, number)) != 0);
    CHECK(ServerVendor(display) && DisplayString(display));
    CHECK(ProtocolVersion(display) == 11 && NextRequest(display) > LastKnownRequestProcessed(display));
#if !defined(WMB_TEST_LEGACY)
    CHECK(wmbApiRevision() == 2);
#endif
    window = XCreateSimpleWindow(display, root, 24 + number, 64, 176, 88, 0, 0, 0);
    CHECK(window);
    XStoreName(display, window, TITLE);
    {
        XSizeHints hints = {.flags = PMinSize, .min_width = 100, .min_height = 60};
        Atom protocol = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMNormalHints(display, window, &hints);
        CHECK(XSetWMProtocols(display, window, &protocol, 1));
    }
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | KeyPressMask |
                 KeyReleaseMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask);
    XMapWindow(display, window);
    CHECK(await_parent(display, window, root, &parent, true) == 0);
    {
        XWindowAttributes child, frame;
        CHECK(XGetWindowAttributes(display, window, &child));
        CHECK(XGetWindowAttributes(display, parent, &frame));
        state->x = frame.x + frame.border_width + child.x + child.border_width + 12;
        state->y = frame.y + frame.border_width + child.y + child.border_width + 12;
        state->window = (uint32_t)window;
        state->parent = (uint32_t)parent;
    }
    CHECK(XAllocColor(display, DefaultColormap(display, number), &color));
    state->green = (uint32_t)color.pixel;
    {
        XGCValues values = {.foreground = color.pixel, .background = BlackPixel(display, number)};
        gc = XCreateGC(display, window, GCForeground | GCBackground, &values);
        CHECK(gc);
    }
    XSetForeground(display, gc, color.pixel);
    XSetBackground(display, gc, BlackPixel(display, number));
    XSetLineAttributes(display, gc, 1, LineSolid, CapButt, JoinMiter);
    XSetFunction(display, gc, GXcopy);
    pixmap = XCreatePixmap(display, window, 32, 24, (unsigned)DefaultDepth(display, number));
    CHECK(pixmap);
    XFillRectangle(display, pixmap, gc, 0, 0, 32, 24);
    XCopyArea(display, pixmap, window, gc, 0, 0, 32, 24, 0, 0);
    XDrawPoint(display, window, gc, 3, 3);
    XDrawLine(display, window, gc, 2, 2, 25, 16);
    {
        XPoint points[] = {{2, 2}, {20, 10}, {4, 18}};
        XDrawLines(display, window, gc, points, 3, CoordModeOrigin);
        XFillPolygon(display, window, gc, points, 3, Convex, CoordModeOrigin);
    }
    XDrawRectangle(display, window, gc, 1, 1, 30, 20);
    XDrawArc(display, window, gc, 2, 2, 20, 16, 0, 180 * 64);
    XFillArc(display, window, gc, 4, 4, 8, 8, 0, 360 * 64);
    XDrawImageString(display, window, gc, 2, 16, "ABI", 3);
    XClearArea(display, window, 0, 0, 10, 10, False);
    {
        Atom atom = XInternAtom(display, "WMB_P8_PROPERTY", False), actual;
        char *name = XGetAtomName(display, atom);
        unsigned long words[] = {0, 0x12345678, 0xfedcba98, 0xffffffff}, count, after;
        unsigned char *data = NULL;
        int format;
        CHECK(name && strcmp(name, "WMB_P8_PROPERTY") == 0);
        XFree(name);
        XChangeProperty(display, window, atom, atom, 32, PropModeReplace,
                        (const unsigned char *)words, 4);
        CHECK(XGetWindowProperty(display, window, atom, 0, 4, False, atom,
                                &actual, &format, &count, &after, &data) == Success);
        CHECK(actual == atom && format == 32 && count == 4 && after == 0 && data);
        for (size_t index = 0; index < count; index++)
            CHECK((((unsigned long *)(void *)data)[index] & 0xffffffffUL) == words[index]);
        XFree(data);
        XDeleteProperty(display, window, atom);
    }
    cursor = XCreateFontCursor(display, 68);
    CHECK(cursor);
    XDefineCursor(display, window, cursor);
    {
        XColor screen_color, exact;
        CHECK(XAllocNamedColor(display, DefaultColormap(display, number), "green",
                               &screen_color, &exact));
        XFreeColors(display, DefaultColormap(display, number), &screen_color.pixel, 1, 0);
    }
    extra = XCreateWindow(display, root, 0, 0, 20, 20, 0, CopyFromParent, InputOutput,
                           CopyFromParent, 0, NULL);
    CHECK(extra);
    {
        XWindowChanges changes = {.x = 4, .y = 5, .width = 28, .height = 24};
        XConfigureWindow(display, extra, CWX | CWY | CWWidth | CWHeight, &changes);
        XMoveWindow(display, extra, 5, 6);
        XResizeWindow(display, extra, 30, 26);
        XMoveResizeWindow(display, extra, 6, 7, 32, 28);
        XMapRaised(display, extra);
        CHECK(await_parent(display, extra, root, &parent, true) == 0);
        XLowerWindow(display, extra);
        XRaiseWindow(display, extra);
        XUnmapWindow(display, extra);
        CHECK(await_parent(display, extra, root, &parent, false) == 0);
    }
    XDestroyWindow(display, extra);
    extra = 0;
    XGrabServer(display);
    XUngrabServer(display);
    XBell(display, 0);
    XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
    {
        Window focus, root_return, child;
        int revert, rx, ry, wx, wy;
        unsigned mask;
        XGetInputFocus(display, &focus, &revert);
        CHECK(focus == window);
        CHECK(XQueryPointer(display, window, &root_return, &child, &rx, &ry, &wx, &wy, &mask));
        XWarpPointer(display, None, window, 0, 0, 0, 0, 10, 10);
    }
    {
        XEvent event = {0}, received;
        event.xclient.type = ClientMessage;
        event.xclient.display = display;
        event.xclient.window = window;
        event.xclient.message_type = XInternAtom(display, "WMB_P8_EVENT", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1234567;
        XPutBackEvent(display, &event);
        CHECK(XCheckTypedWindowEvent(display, window, ClientMessage, &received));
        CHECK(received.xclient.data.l[0] == 1234567);
        CHECK(XSendEvent(display, window, False, NoEventMask, &event));
        XSync(display, False);
        CHECK(XCheckTypedWindowEvent(display, window, ClientMessage, &received));
        CHECK(received.xclient.data.l[0] == 1234567 && received.xclient.display == display);
        (void)XCheckMaskEvent(display, ExposureMask, &received);
    }
    XSetWindowBackground(display, window, color.pixel);
    XClearWindow(display, window);
    XFillRectangle(display, window, gc, 0, 0, 176, 88);
    XSetForeground(display, gc, BlackPixel(display, number));
    XDrawString(display, window, gc, 18, 38, TITLE, (int)strlen(TITLE));
    XFlush(display);
    XSync(display, False);
    state->ready = true;
    printf("P8 " TITLE ": decorated=%lu parent=%lu pixel=%u all 51 macros PASS\n",
           window, (unsigned long)state->parent, state->green);
    fflush(stdout);
    while (wmbYield(1) >= 0) {
        if (state->arm_input && !state->input_armed) {
            Window pointer_root, child;
            int wx, wy;
            unsigned mask;
            XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
            XWarpPointer(display, None, window, 0, 0, 0, 0, 12, 12);
            CHECK(XQueryPointer(display, window, &pointer_root, &child,
                                &state->x, &state->y, &wx, &wy, &mask));
            state->input_armed = true;
        }
        while (XPending(display)) {
            XEvent event;
            if (XNextEvent(display, &event) < 0) goto clean;
            if (event.type == KeyPress || event.type == KeyRelease) state->keys++;
            if (event.type == MotionNotify) state->motions++;
            if (event.type == ButtonPress || event.type == ButtonRelease) state->buttons++;
            if (event.type == Expose) {
                XSetForeground(display, gc, color.pixel);
                XFillRectangle(display, window, gc, 0, 0, 176, 88);
                XSetForeground(display, gc, BlackPixel(display, number));
                XDrawString(display, window, gc, 18, 38, TITLE, (int)strlen(TITLE));
                XFlush(display);
            }
        }
        state->polls++;
        if (state->cycles_completed < state->cycles_requested) {
            CHECK(cycle(display, root) >= 0);
            state->cycles_completed++;
        }
    }
clean:
    result = 0;
failed:
    if (extra) XDestroyWindow(display, extra);
    if (cursor) XFreeCursor(display, cursor);
    if (pixmap) XFreePixmap(display, pixmap);
    if (gc) XFreeGC(display, gc);
    if (display) XCloseDisplay(display);
    state->result = result;
    state->done = true;
    return result;
}
