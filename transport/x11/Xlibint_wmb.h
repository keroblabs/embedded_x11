#ifndef _WMB_X11_XLIBINT_WMB_H_
#define _WMB_X11_XLIBINT_WMB_H_

#include "heap.h"
#include "wmb_io.h"

extern heap_t wmb_heap_data;

#ifdef Xmalloc
#undef Xmalloc
#endif

#define Xmalloc(size)           heap_malloc(&wmb_heap_data, (size))

#ifdef Xrealloc
#undef Xrealloc
#endif

#define Xrealloc(ptr, size)     heap_realloc(&wmb_heap_data, (ptr), (size))

#ifdef Xcalloc
#undef Xcalloc
#endif

#define Xcalloc(nelem, size)    heap_calloc(&wmb_heap_data, (nelem), (size))

#ifdef Data
#undef Data
#endif

#define Data(dpy, data, len) {\
    if (dpy->bufptr + (len) <= dpy->bufmax) {\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    } else {\
        _XBufferOverflow(dpy, (len));\
        memcpy(dpy->bufptr, data, (size_t)(len));\
        dpy->bufptr += ((size_t)((len) + 3) & (size_t)~3);\
    }\
}

void _XBufferOverflow(Display *dpy, long needed);

#endif // _WMB_X11_XLIBINT_WMB_H_