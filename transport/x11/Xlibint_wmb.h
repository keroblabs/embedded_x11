/*
 * Xlibint_wmb.h - request-builder hook for the in-process Xlib transport.
 *
 * This is an internal declaration header, NOT an alternative Xlibint.h.
 * The WMB_INPROC Data()/BufAlloc changes are in the matching canonical
 * libX11/include/X11/Xlibint.h, not the older workspace header copy.
 *
 * The former header replaced Xmalloc/Xrealloc/Xcalloc using a global
 * wmb_heap_data and replaced Data() with an unchecked buffer write.
 * Those overrides were unsafe and have been removed.  Allocation
 * ownership must be settled as part of the P5 libX11 integration.
 *
 * wmb_transport.c implements _XBufferGrow; the buffer/lease tests verify
 * allocation-failure preservation and reject changes while the server
 * owns the request lease.
 */
#ifndef WMB_X11_XLIBINT_WMB_H
#define WMB_X11_XLIBINT_WMB_H

#include <stddef.h>
#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ensure at least additional_bytes of capacity beyond dpy->bufptr.
 *
 * Return 0 on success and a negative value on failure.  On failure
 * preserve dpy->buffer, dpy->bufptr and dpy->bufmax unchanged; callers
 * must NOT write after a failed grow.
 *
 * The buffer may move on success.  P5 must audit Xlib call sites that
 * retain request pointers across a Data() call (TR-1 / D-46).
 */
int _XBufferGrow(Display *dpy, size_t additional_bytes);

#ifdef __cplusplus
}
#endif

#endif /* WMB_X11_XLIBINT_WMB_H */
