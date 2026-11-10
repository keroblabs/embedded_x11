#include "wire_script.h"
#include <X11/Xlibint.h>
#include <X11/Xatom.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void wmb_wire_begin(WmbWireLog *log, Display *display, FILE *file)
{
    unsigned int endian = 1;
    log->file = file;
    log->step = 0;
    fprintf(file, "WMBWIRE1 %c %lx %lx %lx %lx %lx %lu\n",
        *(unsigned char *)&endian ? 'l' : 'B',
        (unsigned long)DefaultRootWindow(display),
        (unsigned long)DefaultColormap(display, 0),
        (unsigned long)DefaultVisual(display, 0)->visualid,
        (unsigned long)display->resource_base,
        (unsigned long)display->resource_mask, XNextRequest(display) - 1);
}

void wmb_wire_record(void *argument, bool output, const void *bytes, size_t size)
{
    WmbWireLog *log = argument;
    unsigned char header[6] = {output ? 'T' : 'R', (unsigned char)log->step};
    uint32_t length = (uint32_t)size;
    unsigned int i;
    if (!size) return;
    for (i = 0; i < 4; i++) header[2 + i] = (unsigned char)(length >> (i * 8));
    if (fwrite(header, 1, sizeof(header), log->file) != sizeof(header) ||
        fwrite(bytes, 1, size, log->file) != size) {
        fputs("Wire capture write failed\n", stderr);
        abort();
    }
}

int wmb_wire_script(Display *dpy, WmbWireLog *log)
{
    Window window, child, root, parent, *children = NULL;
    unsigned int child_count;
    XWindowAttributes attributes;
    unsigned char property[4096], *value = NULL;
    Atom type;
    int format;
    unsigned long count, remaining;
    XPoint points[500];
    XGCValues values = {0};
    GC gc;
    int i, result = 1;
    log->step = 1;
    window = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 20, 20, 160, 100,
                                0, 0, 0xffff);
    XSync(dpy, False);
    log->step = 0;
    child = XCreateSimpleWindow(dpy, window, 1, 1, 10, 10, 0, 0, 0xffff);
    XSync(dpy, False);
    log->step = 2;
    XMapWindow(dpy, window);
    XSync(dpy, False);
    log->step = 3;
    XStoreName(dpy, window, "WMB WIRE");
    XSync(dpy, False);
    for (i = 0; i < 4096; i++) property[i] = (unsigned char)(i * 17);
    log->step = 4;
    XChangeProperty(dpy, window, XA_CUT_BUFFER0, XA_STRING, 8, PropModeReplace,
                    property, sizeof(property));
    XSync(dpy, False);
    log->step = 5;
    if (!XGetWindowAttributes(dpy, window, &attributes)) goto close;
    XSync(dpy, False);
    log->step = 6;
    if (XGetWindowProperty(dpy, window, XA_CUT_BUFFER0, 0, 1024, False, XA_STRING,
        &type, &format, &count, &remaining, &value) != Success ||
        type != XA_STRING || format != 8 || count != 4096 || remaining ||
        !value || memcmp(property, value, 4096)) goto close;
    XFree(value);
    value = NULL;
    XSync(dpy, False);
    log->step = 7;
    if (!XQueryTree(dpy, window, &root, &parent, &children, &child_count) ||
        child_count != 1 || children[0] != child) goto close;
    XFree(children);
    children = NULL;
    XSync(dpy, False);
    log->step = 8;
    if (XInternAtom(dpy, "WM_NAME", True) != XA_WM_NAME) goto close;
    XSync(dpy, False);
    for (i = 0; i < 500; i++) {
        points[i].x = (short)(i % 150);
        points[i].y = (short)(i % 90);
    }
    log->step = 9;
    XDrawLines(dpy, window, DefaultGC(dpy, 0), points, 500, CoordModeOrigin);
    XSync(dpy, False);
    log->step = 10;
    values.foreground = 0;
    values.background = 0xffff;
    values.line_width = 1;
    values.line_style = LineSolid;
    values.fill_style = FillSolid;
    values.graphics_exposures = True;
    values.subwindow_mode = IncludeInferiors;
    gc = XCreateGC(dpy, window, GCForeground | GCBackground | GCLineWidth |
                   GCLineStyle | GCFillStyle | GCGraphicsExposures | GCSubwindowMode,
                   &values);
    if (!gc) goto close;
    XSync(dpy, False);
    log->step = 0;
    XFreeGC(dpy, gc);
    result = 0;
close:
    log->step = 0;
    if (value) XFree(value);
    if (children) XFree(children);
    XDestroyWindow(dpy, window);
    XSync(dpy, False);
    return result;
}
