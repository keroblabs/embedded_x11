#include "desktop.h"
#include "wmb_xlib_internal.h"
#include "wmb_app_table.h"
#include "jwm_port.h"
#include "hal_task.h"
#include "hal_time.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/xtestproto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { \
    if (!(value)) { fprintf(stderr, "P7 WM FAIL %d: %s\n", __LINE__, #value); goto close; } \
} while (0)

typedef struct {
    Display *display;
    bool started, done, success;
} Peer;

static int xtest;
static unsigned client_errors;

static int expected_error(Display *display, XErrorEvent *event)
{
    (void)display;
    if (event->error_code != BadWindow && event->error_code != BadDrawable) {
        fprintf(stderr, "P7: unexpected test-client error %u\n", event->error_code);
        abort();
    }
    client_errors++;
    return 0;
}

static unsigned clients(Display *display, Window *windows)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    unsigned result = 0;
    if (XGetWindowProperty(display, DefaultRootWindow(display),
            XInternAtom(display, "_NET_CLIENT_LIST", False), 0, 16, False,
            XA_WINDOW, &type, &format, &count, &remaining, &data) == Success &&
        type == XA_WINDOW && format == 32 && count <= 16) {
        for (result = 0; result < count; result++)
            windows[result] = ((unsigned long *)(void *)data)[result];
    }
    XFree(data);
    return result;
}

static Window typed_window(Display *display, const char *name, XWindowAttributes *attributes)
{
    Window root, parent, *children, menu = None;
    unsigned count;
    if (!XQueryTree(display, DefaultRootWindow(display), &root, &parent, &children, &count))
        return None;
    for (unsigned index = 0; index < count && menu == None; index++) {
        Atom type;
        int format;
        unsigned long length, remaining;
        unsigned char *data = NULL;
        if (!XGetWindowAttributes(display, children[index], attributes) ||
            attributes->map_state != IsViewable) continue;
        if (XGetWindowProperty(display, children[index],
            XInternAtom(display, "_NET_WM_WINDOW_TYPE", False), 0, 1, False,
            XA_ATOM, &type, &format, &length, &remaining, &data) == Success &&
            type == XA_ATOM && format == 32 && length == 1 &&
            *(unsigned long *)(void *)data ==
                XInternAtom(display, name, False))
            menu = children[index];
        XFree(data);
    }
    XFree(children);
    return menu;
}

static bool frame(Display *display, Window window, Window *parent,
                  XWindowAttributes *attributes, unsigned long *extents)
{
    Window root, *children;
    unsigned count;
    Atom type;
    int format;
    unsigned long length, remaining;
    unsigned char *data = NULL;
    bool valid = false;
    if (!XQueryTree(display, window, &root, parent, &children, &count)) return false;
    XFree(children);
    if (*parent == root || !XGetWindowAttributes(display, *parent, attributes)) return false;
    if (XGetWindowProperty(display, window, XInternAtom(display, "_NET_FRAME_EXTENTS", False),
            0, 4, False, XA_CARDINAL, &type, &format, &length, &remaining, &data) == Success &&
        type == XA_CARDINAL && format == 32 && length == 4) {
        memcpy(extents, data, 4 * sizeof(*extents));
        valid = extents[2] > extents[3] && extents[0] > 0;
    }
    XFree(data);
    return valid;
}

static void input(Display *display, unsigned type, unsigned detail, int x, int y)
{
    xXTestFakeInputReq *request = _XGetRequest(display, (CARD8)xtest, sizeof(*request));
    if (!request) {
        fputs("P7: XTEST request allocation failed\n", stderr);
        abort();
    }
    *request = (xXTestFakeInputReq){
        .reqType = (CARD8)xtest, .xtReqType = X_XTestFakeInput,
        .length = sizeof(*request) / 4, .type = (CARD8)type,
        .detail = (CARD8)detail, .root = (CARD32)DefaultRootWindow(display),
        .rootX = (INT16)x, .rootY = (INT16)y
    };
    _XFlush(display);
}

static void drag(Display *display, Window frame, bool resize,
                 int x, int y, int dx, int dy)
{
    XWindowAttributes before, after;
    uint32_t started;
    if (!XGetWindowAttributes(display, frame, &before)) return;
    input(display, MotionNotify, False, x, y);
    input(display, ButtonPress, 1, 0, 0);
    if (resize) {
        started = hal_tick_ms();
        while (typed_window(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", &after) == None &&
               hal_tick_ms() - started < 2000)
            hal_yield_timeout(2);
    } else hal_yield_timeout(20);
    input(display, MotionNotify, False, x + dx / 2, y + dy / 2);
    input(display, MotionNotify, False, x + dx, y + dy);
    started = hal_tick_ms();
    do {
        if (!XGetWindowAttributes(display, frame, &after)) break;
        if (resize ? after.width == before.width + dx && after.height == before.height + dy :
                     after.x == before.x + dx && after.y == before.y + dy) break;
        hal_yield_timeout(2);
    } while (hal_tick_ms() - started < 2000);
    input(display, ButtonRelease, 1, 0, 0);
    started = hal_tick_ms();
    do {
        int status = XGrabPointer(display, DefaultRootWindow(display), False, 0,
            GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        if (status == GrabSuccess) {
            XUngrabPointer(display, CurrentTime);
            started = hal_tick_ms();
            while (typed_window(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", &after) != None &&
                   hal_tick_ms() - started < 2000)
                hal_yield_timeout(2);
            return;
        }
        hal_yield_timeout(2);
    } while (hal_tick_ms() - started < 2000);
    fputs("P7: native drag did not release its pointer grab\n", stderr);
}

static bool keyboard_action(Display *display, Window window, Window parent, bool resize)
{
    XEvent event = {0};
    XWindowAttributes before, after, status;
    uint32_t started;
    KeySym keys[3] = {XK_Right, resize ? XK_Down : XK_Up, XK_Return};
    bool changed = false;
    if (!XGetWindowAttributes(display, parent, &before)) return false;
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
    event.xclient.format = 32;
    event.xclient.data.l[2] = resize ? 9 : 10;
    if (!XSendEvent(display, DefaultRootWindow(display), False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event))
        return false;
    started = hal_tick_ms();
    while (typed_window(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", &status) == None &&
           hal_tick_ms() - started < 2000)
        hal_yield_timeout(2);
    if (typed_window(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", &status) == None)
        return false;
    for (unsigned index = 0; index < 3; index++) {
        KeyCode code = XKeysymToKeycode(display, keys[index]);
        if (!code) {
            fputs("P7: required keyboard action has no keycode\n", stderr);
            return false;
        }
        if (index == 2) {
            started = hal_tick_ms();
            do {
                if (!XGetWindowAttributes(display, parent, &after)) break;
                changed = resize ?
                    after.width == before.width + 10 && after.height == before.height + 10 :
                    after.x == before.x + 10 && after.y == before.y - 10;
                if (changed) break;
                hal_yield_timeout(2);
            } while (hal_tick_ms() - started < 2000);
        }
        input(display, KeyPress, code, 0, 0);
        input(display, KeyRelease, code, 0, 0);
    }
    started = hal_tick_ms();
    while (typed_window(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", &status) != None &&
           hal_tick_ms() - started < 2000)
        hal_yield_timeout(2);
    return changed && typed_window(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", &status) == None;
}

static void peer_query(void *argument)
{
    Peer *peer = argument;
    XWindowAttributes attributes;
    peer->started = true;
    peer->success = XGetWindowAttributes(peer->display,
        DefaultRootWindow(peer->display), &attributes) != 0;
    XCloseDisplay(peer->display);
    peer->done = true;
    fiber_notify();
    for (;;) hal_task_wait();
}

static bool above(Display *display, Window first, Window second)
{
    Window root, parent, *children;
    unsigned count, a = 0, b = 0, index;
    if (!XQueryTree(display, DefaultRootWindow(display), &root, &parent, &children, &count))
        return false;
    for (index = 0; index < count; index++) {
        if (children[index] == first) a = index + 1;
        if (children[index] == second) b = index + 1;
    }
    XFree(children);
    return a > b && b != 0;
}

void wmb_wm_test(void *argument)
{
    WmbDesktopTest *result = argument;
    Display *display = XOpenDisplay(NULL);
    Window windows[16], parents[2], focused = None;
    XWindowAttributes attributes[2], moved, resized, client;
    unsigned long borders[2][4];
    uint32_t started;
    int event, error, revert;
    hal_task_t *peer_task = NULL;
    Peer peer = {0};
    result->result = 1;
    CHECK(display != NULL);
    XSetErrorHandler(expected_error);
    CHECK(!XGetWindowAttributes(display, 0x7fffffffu, &client));
    CHECK(client_errors == 1 && WmbJwm_Status()->protocol_errors == 0);
    CHECK(XQueryExtension(display, "XTEST", &xtest, &event, &error));
    started = hal_tick_ms();
    while (clients(display, windows) < 2 && hal_tick_ms() - started < 2000)
        hal_yield_timeout(2);
    CHECK(clients(display, windows) == 2);
    for (unsigned index = 0; index < 2; index++) {
        CHECK(frame(display, windows[index], &parents[index], &attributes[index], borders[index]));
        CHECK(XGetWindowAttributes(display, windows[index], &client));
        CHECK(attributes[index].width == client.width + (int)(borders[index][0] + borders[index][1]));
        CHECK(attributes[index].height == client.height + (int)(borders[index][2] + borders[index][3]));
        {
            unsigned different = 0;
            XImage *image = XGetImage(display, parents[index], 0, 0,
                (unsigned)attributes[index].width, (unsigned)borders[index][2],
                AllPlanes, ZPixmap);
            CHECK(image != NULL);
            for (int y = 0; y < (int)borders[index][2]; y++)
                for (int x = 0; x < attributes[index].width; x++)
                    if (XGetPixel(image, x, y) != WhitePixel(display, 0)) different++;
            XDestroyImage(image);
            CHECK(different >= (unsigned)attributes[index].width * borders[index][2] / 2);
        }
    }
    if (attributes[0].x > attributes[1].x) {
        Window swap = windows[0]; windows[0] = windows[1]; windows[1] = swap;
        CHECK(frame(display, windows[0], &parents[0], &attributes[0], borders[0]));
        CHECK(frame(display, windows[1], &parents[1], &attributes[1], borders[1]));
    }
    result->first_window = (uint32_t)windows[0];
    result->first_frame = (uint32_t)parents[0];
    puts("P7 PASS: two real reparented/decorated clients and frame extents");

    for (unsigned index = 2; index > 0; index--) {
        unsigned selected = index - 1;
        input(display, MotionNotify, False,
              attributes[selected].x + (int)borders[selected][0] + 10,
              attributes[selected].y + (int)borders[selected][2] + 10);
        input(display, ButtonPress, 1, 0, 0);
        input(display, ButtonRelease, 1, 0, 0);
        started = hal_tick_ms();
        do {
            XGetInputFocus(display, &focused, &revert);
            if (focused == windows[selected] &&
                above(display, parents[selected], parents[1 - selected])) break;
            hal_yield_timeout(2);
        } while (hal_tick_ms() - started < 2000);
        CHECK(focused == windows[selected] &&
              above(display, parents[selected], parents[1 - selected]));
    }
    puts("P7 PASS: click focus and stacking");

    drag(display, parents[0], false, attributes[0].x + attributes[0].width / 3,
         attributes[0].y + (int)borders[0][2] / 2, 11, 7);
    started = hal_tick_ms();
    do {
        CHECK(XGetWindowAttributes(display, parents[0], &moved));
        if (moved.x == attributes[0].x + 11 && moved.y == attributes[0].y + 7) break;
        hal_yield_timeout(2);
    } while (hal_tick_ms() - started < 2000);
    CHECK(moved.x == attributes[0].x + 11 && moved.y == attributes[0].y + 7);
    puts("P7 PASS: native title-bar drag moves the frame");

    drag(display, parents[0], true,
         moved.x + moved.width - 2, moved.y + moved.height - 2, 13, 7);
    started = hal_tick_ms();
    do {
        CHECK(XGetWindowAttributes(display, parents[0], &resized));
        if (resized.width == moved.width + 13 && resized.height == moved.height + 7) break;
        hal_yield_timeout(2);
    } while (hal_tick_ms() - started < 2000);
    if (resized.width != moved.width + 13 || resized.height != moved.height + 7)
        fprintf(stderr, "P7 resize: before=%d,%d %dx%d after=%d,%d %dx%d expected=%dx%d\n",
            moved.x, moved.y, moved.width, moved.height,
            resized.x, resized.y, resized.width, resized.height,
            moved.width + 13, moved.height + 7);
    CHECK(resized.width == moved.width + 13 && resized.height == moved.height + 7);
    puts("P7 PASS: native corner drag resizes the client/frame");
    CHECK(keyboard_action(display, windows[0], parents[0], false));
    CHECK(keyboard_action(display, windows[0], parents[0], true));
    puts("P7 PASS: native keyboard move/resize and Enter completion");

    {
        Window menu = None;
        XWindowAttributes menu_attributes;
        input(display, MotionNotify, False, 3, 3);
        input(display, ButtonPress, 3, 0, 0);
        started = hal_tick_ms();
        do {
            menu = typed_window(display, "_NET_WM_WINDOW_TYPE_MENU", &menu_attributes);
            if (menu != None) break;
            hal_yield_timeout(2);
        } while (hal_tick_ms() - started < 2000);
        CHECK(menu != None);
        CHECK(XGetWindowAttributes(display, menu, &menu_attributes));
        input(display, MotionNotify, False,
              menu_attributes.x + menu_attributes.width / 2,
              menu_attributes.y + menu_attributes.height / 4);
        hal_yield_timeout(20);
        input(display, ButtonRelease, 3, 0, 0);
        started = hal_tick_ms();
        while (clients(display, windows) != 3 && hal_tick_ms() - started < 2000)
            hal_yield_timeout(2);
        CHECK(clients(display, windows) == 3);
    }
    puts("P7 PASS: root menu opens and launches a registered application");

    peer.display = XOpenDisplay(NULL);
    CHECK(peer.display != NULL);
    WmbJwm_TestGrab(true);
    started = hal_tick_ms();
    while (!WmbJwm_TestGrabHeld() && hal_tick_ms() - started < 2000)
        hal_yield_timeout(2);
    CHECK(WmbJwm_TestGrabHeld());
    peer_task = hal_task_create("grab-peer", peer_query, &peer, 128 * 1024);
    CHECK(peer_task != NULL);
    hal_task_wake(peer_task);
    hal_yield_timeout(60);
    CHECK(peer.started && !peer.done);
    WmbJwm_TestGrab(false);
    started = hal_tick_ms();
    while (!peer.done && hal_tick_ms() - started < 2000) hal_yield_timeout(2);
    CHECK(peer.done && peer.success);
    hal_task_destroy(peer_task);
    peer_task = NULL;
    puts("P7 PASS: JWM's native server grab blocks and releases a real peer");
    result->idle = true;
    while (!result->release) hal_yield_timeout(20);
    CHECK(WmbJwm_Status()->protocol_errors == 0);
    if (result->cancellation == 1) {
        input(display, MotionNotify, False, 3, 3);
        input(display, ButtonPress, 3, 0, 0);
        started = hal_tick_ms();
        while (typed_window(display, "_NET_WM_WINDOW_TYPE_MENU", &client) == None &&
               hal_tick_ms() - started < 2000)
            hal_yield_timeout(2);
        CHECK(typed_window(display, "_NET_WM_WINDOW_TYPE_MENU", &client) != None);
        puts("P7 cancellation: stopping with the native root menu active");
    } else if (result->cancellation >= 2) {
        bool resizing = result->cancellation == 3;
        int x, y;
        unsigned total = clients(display, windows);
        CHECK(total == 3);
        for (unsigned index = 0; index < total; index++) {
            Window candidate;
            XWindowAttributes geometry;
            unsigned long extents[4];
            CHECK(frame(display, windows[index], &candidate, &geometry, extents));
            if (index == 0 || above(display, candidate, parents[0])) {
                parents[0] = candidate;
                attributes[0] = geometry;
                memcpy(borders[0], extents, sizeof(extents));
            }
        }
        CHECK(XGetWindowAttributes(display, parents[0], &attributes[0]));
        x = attributes[0].x + (resizing ? attributes[0].width - 2 : attributes[0].width / 3);
        y = attributes[0].y + (resizing ? attributes[0].height - 2 : (int)borders[0][2] / 2);
        input(display, MotionNotify, False, x, y);
        input(display, ButtonPress, 1, 0, 0);
        if (resizing) {
            started = hal_tick_ms();
            while (typed_window(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", &client) == None &&
                   hal_tick_ms() - started < 2000)
                hal_yield_timeout(2);
        } else hal_yield_timeout(20);
        input(display, MotionNotify, False, x + 3, y + 3);
        input(display, MotionNotify, False, x + 7, y + 5);
        started = hal_tick_ms();
        do {
            CHECK(XGetWindowAttributes(display, parents[0], &moved));
            if (resizing ? moved.width == attributes[0].width + 7 :
                           moved.x == attributes[0].x + 7) break;
            hal_yield_timeout(2);
        } while (hal_tick_ms() - started < 2000);
        CHECK(resizing ? moved.width == attributes[0].width + 7 :
                         moved.x == attributes[0].x + 7);
        printf("P7 cancellation: stopping during native %s\n", resizing ? "resize" : "move");
    }
    result->result = 0;
close:
    WmbJwm_TestGrab(false);
    if (display && result->result) {
        input(display, ButtonRelease, 1, 0, 0);
        input(display, ButtonRelease, 3, 0, 0);
    }
    if (peer_task && peer.done) hal_task_destroy(peer_task);
    if (peer.display && !peer.started) XCloseDisplay(peer.display);
    if (display) XCloseDisplay(display);
    result->done = true;
    fiber_notify();
    for (;;) hal_task_wait();
}
