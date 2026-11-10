#ifndef WMB_XLIB_H
#define WMB_XLIB_H
#include <X11/Xlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Client-fiber wait/pump: 1 if events are queued, otherwise 0 after the timeout.
 * UINT32_MAX is the optional indefinite event-idle wait, never a reply timeout. */
int wmb_xlib_wait(Display *display, uint32_t milliseconds);
/* Scoped internal-client handler; native XSetErrorHandler remains the fallback. */
XErrorHandler wmb_xlib_set_error_handler(Display *display, XErrorHandler handler);
XErrorHandler wmb_xlib_error_handler(Display *display);
#ifdef __cplusplus
}
#endif
#endif
