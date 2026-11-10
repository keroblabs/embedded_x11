#include "Xlibint.h"
#include "Xlcint.h"
#include "XlcPublic.h"
#include <X11/Xresource.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* The selected profile uses core fonts and no locale/input-method backend. */
Status XInitThreads(void)
{
    fputs("WMB Xlib: native Xlib threading is unsupported; use client fibers\n", stderr);
    return 0;
}
void XLockDisplay(Display *display) { (void)display; }
void XUnlockDisplay(Display *display) { (void)display; }
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
