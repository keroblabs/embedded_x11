#ifndef WMB_XLIB_INTERNAL_H
#define WMB_XLIB_INTERNAL_H
#include "Xlibint.h"
#if !defined(WMB_XLIB_CANONICAL_HEADER)
#error "The WMB client requires the matching libX11/include/X11 headers"
#endif
#include "wmb_bridge.h"

typedef struct {
    WmbConnection *connection;
    WmbPacket *reply;
    size_t read_pos;
    bool awaiting;
    bool refilling_ids;
    unsigned long awaiting_sequence;
} WmbDisplay;

int wmb_xlib_connect(Display *display);
void wmb_xlib_disconnect(Display *display);
void wmb_xlib_free_state(Display *display);
const void *wmb_xlib_setup(Display *display);
int wmb_xlib_sync(Display *display);
int _XBufferGrow(Display *display, size_t additional);
void _XFreeDisplayStructure(Display *display);
void wmb_xlib_prepare_ids(Display *display);
unsigned long wmb_xlib_big_requests(Display *display);
#endif
