#include "Xlibint.h"
#include "Xlcint.h"
#include "XlcPublic.h"
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* The selected profile uses core fonts and no locale/input-method backend. */
char *XDisplayName(const char *display)
{
    (void)display;
    return "WMB";
}

char *XGetDefault(Display *display, const char *program, const char *option)
{
    (void)display; (void)program; (void)option;
    fputs("WMB Xlib: resource defaults are unsupported\n", stderr);
    return NULL;
}

int XReadBitmapFileData(const char *filename, unsigned int *width,
                       unsigned int *height, unsigned char **data, int *x_hot, int *y_hot)
{
    (void)filename; (void)width; (void)height; (void)data; (void)x_hot; (void)y_hot;
    fputs("WMB Xlib: bitmap files are unsupported; use bitmap data\n", stderr);
    return BitmapOpenFailed;
}

int XReadBitmapFile(Display *display, Drawable drawable, const char *filename,
                    unsigned int *width, unsigned int *height, Pixmap *pixmap,
                    int *x_hot, int *y_hot)
{
    (void)display; (void)drawable; (void)filename; (void)width; (void)height;
    (void)pixmap; (void)x_hot; (void)y_hot;
    fputs("WMB Xlib: bitmap files are unsupported; use bitmap data\n", stderr);
    return BitmapOpenFailed;
}

int XWriteBitmapFile(Display *display, const char *filename, Pixmap bitmap,
                     unsigned int width, unsigned int height, int x_hot, int y_hot)
{
    (void)display; (void)filename; (void)bitmap; (void)width; (void)height;
    (void)x_hot; (void)y_hot;
    fputs("WMB Xlib: bitmap files are unsupported\n", stderr);
    return BitmapOpenFailed;
}

Status XInitThreads(void)
{
    fputs("WMB Xlib: native Xlib threading is unsupported; use client fibers\n", stderr);
    return 0;
}
Bool XSupportsLocale(void) { return False; }
char *XSetLocaleModifiers(const char *modifiers)
{
    if (modifiers && *modifiers) {
        fputs("WMB Xlib: locale modifiers are unsupported\n", stderr);
        return NULL;
    }
    return "";
}
XLCd _XlcCurrentLC(void) { return NULL; }
XLCd _XOpenLC(char *name)
{
    (void)name;
    fputs("WMB Xlib: locale-dependent font/input methods are unsupported\n", stderr);
    return NULL;
}
void _XCloseLC(XLCd locale)
{
    if (locale) {
        fputs("WMB Xlib: invalid locale handle\n", stderr);
        abort();
    }
}

int _XlcNCompareISOLatin1(const char *a, const char *b, int count)
{
    while (count-- > 0) {
        unsigned char left = (unsigned char)*a++, right = (unsigned char)*b++;
        if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
        if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
        if (left != right || !left) return (int)left - (int)right;
    }
    return 0;
}
int _XlcCompareISOLatin1(const char *a, const char *b)
{
    return _XlcNCompareISOLatin1(a, b, INT_MAX);
}

void XrmInitialize(void) {}
XrmDatabase XrmGetFileDatabase(const char *name)
{
    if (name && *name)
        fprintf(stderr, "WMB Xlib: resource database files are unsupported: %s\n", name);
    return NULL;
}
Bool XrmGetResource(XrmDatabase database, const char *name, const char *class_name,
                    char **type, XrmValue *value)
{
    (void)name; (void)class_name;
    if (database) fputs("WMB Xlib: resource database lookup is unsupported\n", stderr);
    *type = NULL;
    value->addr = NULL;
    value->size = 0;
    return False;
}
void XrmDestroyDatabase(XrmDatabase database)
{
    if (database) {
        fputs("WMB Xlib: invalid resource database handle\n", stderr);
        abort();
    }
}
