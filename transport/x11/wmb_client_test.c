#include "wmb_test_client.h"
#include "wmb_bridge.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int errors;
static int last_error;
static int handle_error(Display *dpy, XErrorEvent *event)
{
    (void)dpy;
    errors++;
    last_error = event->error_code;
    return 0;
}

#define CHECK(condition) do { \
    if (!(condition)) { fprintf(stderr, "P5 client FAIL line %d: %s\n", __LINE__, #condition); goto close; } \
} while (0)

static bool_t released(void *argument) { return ((WmbTestResult *)argument)->release; }

void wmb_xlib_test_client(void *argument)
{
    WmbTestResult *result = argument;
    Display *dpy = XOpenDisplay(NULL);
    Window window = None;
    GC gc;
    XColor screen_color, exact_color;
    XEvent event;
    XWindowAttributes attributes;
    unsigned char property[4096], *value = NULL;
    Atom atom, type;
    int format;
    unsigned long count, remaining;
    int i, motions = 0, first = -1, last = -1;
    int (*after)(Display *);
    result->result = 1;
    if (!dpy) {
        fputs("P5 client FAIL: XOpenDisplay\n", stderr);
        result->done = true;
        return;
    }
    XSetErrorHandler(handle_error);
    CHECK(ConnectionNumber(dpy) == -1 && ScreenCount(dpy) == 1);
    CHECK(XAllocNamedColor(dpy, DefaultColormap(dpy, 0), "black",
                          &screen_color, &exact_color));
    CHECK(screen_color.pixel == BlackPixel(dpy, 0));
    CHECK(!XAllocNamedColor(dpy, DefaultColormap(dpy, 0), "wmb-no-such-color",
                           &screen_color, &exact_color));
    CHECK(errors == 0);
    puts("P5 client PASS: open display");
    window = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 20, 20, 240, 110,
        0, BlackPixel(dpy, 0), WhitePixel(dpy, 0));
    CHECK(window != None);
    {
        Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
        XClassHint hints = {.res_name = NULL, .res_class = "WMB"};
        XClassHint actual;
        XTextProperty machine;
        char *arguments[] = {"wmb-client"};
        CHECK(cursor != None);
        XFreeCursor(dpy, cursor);
        XSetWMProperties(dpy, window, NULL, NULL, arguments, 1, NULL, NULL, &hints);
        CHECK(XGetClassHint(dpy, window, &actual));
        CHECK(strcmp(actual.res_name, "wmb-client") == 0 &&
              strcmp(actual.res_class, "WMB") == 0);
        XFree(actual.res_name);
        XFree(actual.res_class);
        CHECK(XGetWMClientMachine(dpy, window, &machine));
        CHECK(machine.format == 8 && machine.nitems == 3 &&
              memcmp(machine.value, "WMB", 3) == 0);
        XFree(machine.value);
        XSync(dpy, False);
        CHECK(errors == 0);
    }
    result->window = (uint32_t)window;
    puts("P5 client PASS: create window");
    XSelectInput(dpy, window, ExposureMask | StructureNotifyMask | KeyPressMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    XMapWindow(dpy, window);
    puts("P5 client PASS: map");
    do { XNextEvent(dpy, &event); } while (event.type != Expose);
    puts("P5 client PASS: receive Expose");
    gc = DefaultGC(dpy, 0);
    XSetForeground(dpy, gc, BlackPixel(dpy, 0));
    XFillRectangle(dpy, window, gc, 10, 10, 60, 30);
    XDrawString(dpy, window, gc, 10, 75, "WMB TEST OK", 11);
    XSync(dpy, False);
    puts("P5 client PASS: fill rectangle and draw string");
    {
        XImage *image = XGetImage(dpy, window, 5, 5, 1, 1, AllPlanes, ZPixmap);
        CHECK(image != NULL);
        CHECK(XGetPixel(image, 0, 0) == WhitePixel(dpy, 0));
        XDestroyImage(image);
    }
    CHECK(XGetWindowAttributes(dpy, window, &attributes));
    CHECK(attributes.width == 240 && attributes.height == 110 &&
          attributes.map_state == IsViewable);
    for (i = 0; i < (int)sizeof(property); i++) property[i] = (unsigned char)(i * 17);
    atom = XInternAtom(dpy, "WMB_P5_PROPERTY", False);
    XChangeProperty(dpy, window, atom, XA_STRING, 8, PropModeReplace,
                    property, sizeof(property));
    CHECK(XGetWindowProperty(dpy, window, atom, 0, 1024, False, XA_STRING,
          &type, &format, &count, &remaining, &value) == Success);
    CHECK(type == XA_STRING && format == 8 && count == sizeof(property) &&
          remaining == 0 && value && !memcmp(value, property, sizeof(property)));
    XFree(value);
    value = NULL;
    for (i = 0; i < 2; i++) {
        Atom empty = i == 0 ? atom : XInternAtom(dpy, "WMB_P7_EMPTY", False);
        XChangeProperty(dpy, window, empty, XA_STRING, 8, PropModeReplace, NULL, 0);
        CHECK(XGetWindowProperty(dpy, window, empty, 0, 1, False, XA_STRING,
              &type, &format, &count, &remaining, &value) == Success);
        CHECK(type == XA_STRING && format == 8 && count == 0 &&
              remaining == 0 && value && value[0] == 0);
        XFree(value);
        value = NULL;
    }
    {
        unsigned long *words = malloc(8192 * sizeof(*words));
        CHECK(words != NULL);
        for (i = 0; i < 8192; i++) words[i] = 0xf1230000UL + (unsigned long)i;
        XChangeProperty(dpy, window, atom, XA_CARDINAL, 32, PropModeReplace,
                        (const unsigned char *)words, 8192);
        free(words);
        CHECK(XGetWindowProperty(dpy, window, atom, 0, 8192, False, XA_CARDINAL,
            &type, &format, &count, &remaining, &value) == Success);
        CHECK(type == XA_CARDINAL && format == 32 && count == 8192 && remaining == 0);
        for (i = 0; i < 8192; i++)
            CHECK((uint32_t)((unsigned long *)(void *)value)[i] == 0xf1230000u + (uint32_t)i);
        XFree(value);
        value = NULL;
    }
    errors = 0;
    CHECK(!XGetWindowAttributes(dpy, 0x7fffffffu, &attributes));
    CHECK(errors >= 1 && (last_error == BadWindow || last_error == BadDrawable));
    XSync(dpy, True);
    errors = 0;
    after = XSetAfterFunction(dpy, NULL);
    XDestroyWindow(dpy, 0x7fffffffu);
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = dpy;
    event.xclient.window = window;
    event.xclient.message_type = XA_WM_NAME;
    event.xclient.format = 32;
    CHECK(XSendEvent(dpy, window, False, 0, &event));
    XSetAfterFunction(dpy, after);
    XNextEvent(dpy, &event);
    CHECK(errors == 1 && last_error == BadWindow && event.type == ClientMessage);
    XSync(dpy, True);
    after = XSetAfterFunction(dpy, NULL);
    for (i = 0; i < 1000; i++) {
        memset(&event, 0, sizeof(event));
        event.xmotion.type = MotionNotify;
        event.xmotion.display = dpy;
        event.xmotion.window = window;
        event.xmotion.x = i;
        CHECK(XSendEvent(dpy, window, False, 0, &event));
    }
    XSync(dpy, False);
    XSetAfterFunction(dpy, after);
    while (XPending(dpy)) {
        XNextEvent(dpy, &event);
        if (event.type == MotionNotify && event.xmotion.send_event) {
            if (first < 0) first = event.xmotion.x;
            last = event.xmotion.x;
            motions++;
        }
    }
    CHECK(motions == WMB_EVENT_LIMIT && first == 1000 - WMB_EVENT_LIMIT && last == 999);
    XWarpPointer(dpy, None, window, 0, 0, 0, 0, 20, 20);
    XSetInputFocus(dpy, window, RevertToParent, CurrentTime);
    XSync(dpy, False);
    result->visible = true;
    result->awaiting_key = true;
    while (!result->keyboard_received || !result->mouse_received) {
        XNextEvent(dpy, &event);
        if (event.type == KeyPress) {
            CHECK(event.xkey.window == window && event.xkey.keycode >= 8);
            result->keyboard_received = true;
        }
        if (event.type == ButtonPress) {
            CHECK(event.xbutton.window == window && event.xbutton.button == Button1);
            result->mouse_received = true;
        }
    }
    puts("P5 details: attributes, 4KB/32-bit payloads, newest 128 motions, backend keyboard/mouse verified");
    CHECK(fiber_wait_for(released, result, result->hold_ms + 5000u));
    result->result = 0;
close:
    if (value) XFree(value);
    if (window != None) XDestroyWindow(dpy, window);
    XCloseDisplay(dpy);
    if (result->result == 0) puts("P5 client PASS: close");
    result->done = true;
}
