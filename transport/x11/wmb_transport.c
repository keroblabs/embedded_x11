#include "wmb_xlib_internal.h"
#include "wmb_xlib.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/xcmiscproto.h>
#include <X11/extensions/bigreqsproto.h>

_Static_assert(sizeof(XID) == sizeof(unsigned long), "Client Xlib must not use the server ABI");
static xReq empty_request;

static WmbDisplay *state(Display *dpy) { return (WmbDisplay *)(void *)dpy->xcb; }

XErrorHandler wmb_xlib_set_error_handler(Display *dpy, XErrorHandler handler)
{
    XErrorHandler previous;
    if (!dpy || !dpy->xcb) {
        fputs("WMB Xlib: invalid display for private error handler\n", stderr);
        abort();
    }
    previous = state(dpy)->error_handler;
    state(dpy)->error_handler = handler;
    return previous;
}

XErrorHandler wmb_xlib_error_handler(Display *dpy)
{
    return dpy && dpy->xcb ? state(dpy)->error_handler : NULL;
}

static void io_failure(Display *dpy, const char *reason)
{
    fprintf(stderr, "WMB Xlib: %s (%s, serial %lu)\n", reason,
            dpy->display_name ? dpy->display_name : "opening", dpy->request);
    errno = EIO;
    _XIOError(dpy);
    abort();
}

int wmb_xlib_connect(Display *dpy)
{
    WmbDisplay *w = calloc(1, sizeof(*w));
    if (!w) {
        fputs("WMB Xlib: display transport allocation failed\n", stderr);
        return -1;
    }
    w->connection = wmb_connection_open();
    if (!w->connection) { free(w); return -1; }
    dpy->xcb = (struct _X11XCBPrivate *)(void *)w;
    dpy->fd = -1;
    return 0;
}

const void *wmb_xlib_setup(Display *dpy) { return state(dpy)->connection->setup.data; }

unsigned long wmb_xlib_big_requests(Display *dpy)
{
    int opcode, event, error_code;
    xBigReqEnableReq *request;
    xBigReqEnableReply reply;
    if (!XQueryExtension(dpy, XBigReqExtensionName, &opcode, &event, &error_code)) return 0;
    request = _XGetRequest(dpy, (CARD8)opcode, sizeof(*request));
    if (!request) io_failure(dpy, "BIG-REQUESTS negotiation allocation failed");
    request->brReqType = X_BigReqEnable;
    if (!_XReply(dpy, (xReply *)&reply, 0, True)) return 0;
    return reply.max_request_size < WMB_WIRE_LIMIT / 4u ?
           reply.max_request_size : WMB_WIRE_LIMIT / 4u;
}

void wmb_xlib_disconnect(Display *dpy)
{
    WmbDisplay *w = state(dpy);
    if (w && wmb_connection_close(w->connection) != 0)
        io_failure(dpy, "connection close failed");
}

void wmb_xlib_free_state(Display *dpy)
{
    WmbDisplay *w = state(dpy);
    if (!w) return;
    wmb_xlib_disconnect(dpy);
    wmb_packet_free(w->reply);
    wmb_connection_destroy(w->connection);
    free(w);
    dpy->xcb = NULL;
}

int _XBufferGrow(Display *dpy, size_t additional)
{
    WmbDisplay *w = state(dpy);
    size_t used = (size_t)(dpy->bufptr - dpy->buffer);
    size_t capacity = (size_t)(dpy->bufmax - dpy->buffer);
    size_t new_capacity = capacity ? capacity : 16384;
    uintptr_t base = (uintptr_t)dpy->buffer;
    uintptr_t last = (uintptr_t)dpy->last_req;
    size_t last_offset = last >= base && last < base + used ? last - base : SIZE_MAX;
    char *data;
    if (w && w->connection->in_flight) {
        fputs("WMB Xlib: cannot modify a leased request buffer\n", stderr);
        return -1;
    }
    if (additional > WMB_WIRE_LIMIT || used > WMB_WIRE_LIMIT - additional) {
        fprintf(stderr, "WMB Xlib: request buffer exceeds %u-byte limit\n", WMB_WIRE_LIMIT);
        return -1;
    }
    if (used + additional <= capacity) return 0;
    while (new_capacity < used + additional) new_capacity *= 2;
    data = realloc(dpy->buffer, new_capacity);
    if (!data) {
        fputs("WMB Xlib: request buffer growth failed\n", stderr);
        return -1;
    }
    dpy->buffer = data;
    dpy->bufptr = data + used;
    dpy->bufmax = data + new_capacity;
    if (last_offset != SIZE_MAX) dpy->last_req = data + last_offset;
    return 0;
}

static void events(Display *dpy)
{
    WmbPacket *packet;
    while ((packet = wmb_packet_take(&state(dpy)->connection->events)) != NULL) {
        X_DPY_SET_LAST_REQUEST_READ(dpy, packet->serial);
        _XEnq(dpy, (xEvent *)(void *)packet->bytes);
        wmb_packet_free(packet);
    }
}

static int error(Display *dpy, WmbPacket *packet, bool replying)
{
    _XExtension *extension;
    int result = 0;
    X_DPY_SET_LAST_REQUEST_READ(dpy, packet->serial);
    if (replying)
        for (extension = dpy->ext_procs; extension; extension = extension->next)
            if (extension->error && extension->error(dpy, (xError *)(void *)packet->bytes,
                                                    &extension->codes, &result))
                return result;
    _XError(dpy, (xError *)(void *)packet->bytes);
    return 0;
}

static void asynchronous_reply(Display *dpy, WmbPacket *packet)
{
    _XAsyncHandler *handler, *next;
    X_DPY_SET_LAST_REQUEST_READ(dpy, packet->serial);
    for (handler = dpy->async_handlers; handler; handler = next) {
        next = handler->next;
        if (handler->handler(dpy, (xReply *)(void *)packet->bytes,
            (char *)packet->bytes, (int)packet->size, handler->data)) return;
    }
    io_failure(dpy, "unclaimed asynchronous reply");
}

static void pending_errors(Display *dpy)
{
    WmbDisplay *w = state(dpy);
    WmbPacket *packet;
    while ((packet = list_get_head(&w->connection->responses)) != NULL) {
        if (w->awaiting && packet->serial == w->awaiting_sequence) break;
        packet = wmb_packet_take(&w->connection->responses);
        if (packet->bytes[0] == X_Error) error(dpy, packet, false);
        else asynchronous_reply(dpy, packet);
        wmb_packet_free(packet);
    }
}

static void send_requests(Display *dpy)
{
    WmbDisplay *w = state(dpy);
    size_t size = (size_t)(dpy->bufptr - dpy->buffer);
    if (size) {
        if (w->connection->trace)
            w->connection->trace(w->connection->trace_data, true, dpy->buffer, size);
        if (wmb_connection_submit(w->connection, (unsigned char *)dpy->buffer, size) != 0)
            io_failure(dpy, "request dispatch failed");
        dpy->bufptr = dpy->buffer;
        dpy->last_req = (char *)&empty_request;
    }
}

void _XFlush(Display *dpy)
{
    send_requests(dpy);
    pending_errors(dpy);
    events(dpy);
}

int wmb_xlib_sync(Display *dpy) { _XFlush(dpy); return 0; }

void _XSend(Display *dpy, const char *data, long size)
{
    size_t padded;
    if (size < 0 || (unsigned long)size > WMB_WIRE_LIMIT)
        io_failure(dpy, "invalid send length");
    padded = ((size_t)size + 3u) & ~(size_t)3u;
    if (_XBufferGrow(dpy, padded) != 0) io_failure(dpy, "send buffer allocation failed");
    if (size) memcpy(dpy->bufptr, data, (size_t)size);
    if (padded > (size_t)size) memset(dpy->bufptr + size, 0, padded - (size_t)size);
    dpy->bufptr += padded;
    send_requests(dpy);
}

static bool_t response_available(void *argument)
{
    WmbConnection *c = argument;
    return c->responses.count || c->failed || c->closed;
}

Status _XReply(Display *dpy, xReply *reply, int extra, Bool discard)
{
    WmbDisplay *w = state(dpy);
    WmbPacket *packet;
    if (w->reply && w->read_pos != w->reply->size)
        io_failure(dpy, "previous reply payload was not consumed");
    wmb_packet_free(w->reply);
    w->reply = NULL;
    w->awaiting = true;
    w->awaiting_sequence = dpy->request;
    _XFlush(dpy);
    for (;;) {
        if (!fiber_wait_for(response_available, w->connection, WMB_REPLY_TIMEOUT_MS))
            io_failure(dpy, "reply timeout");
        packet = wmb_packet_take(&w->connection->responses);
        if (!packet) io_failure(dpy, "connection closed while awaiting reply");
        if (packet->serial == w->awaiting_sequence) break;
        if (packet->bytes[0] == X_Error) error(dpy, packet, true);
        else asynchronous_reply(dpy, packet);
        wmb_packet_free(packet);
    }
    w->awaiting = false;
    if (packet->bytes[0] == X_Error) {
        xError *wire = (xError *)(void *)packet->bytes;
        int result = 0;
        bool expected = wire->errorCode == BadAlloc || wire->errorCode == BadAccess ||
            (wire->errorCode == BadFont && wire->majorCode == X_QueryFont) ||
            (wire->errorCode == BadName &&
             (wire->majorCode == X_LookupColor || wire->majorCode == X_AllocNamedColor));
        memcpy(reply, wire, 32);
        X_DPY_SET_LAST_REQUEST_READ(dpy, packet->serial);
        if (!expected) result = error(dpy, packet, true);
        wmb_packet_free(packet);
        return result;
    }
    if (extra < 0 || (size_t)extra > (packet->size - 32) / 4)
        io_failure(dpy, "invalid fixed reply length");
    memcpy(reply, packet->bytes, 32u + (size_t)extra * 4u);
    X_DPY_SET_LAST_REQUEST_READ(dpy, packet->serial);
    w->reply = packet;
    w->read_pos = discard ? packet->size : 32u + (size_t)extra * 4u;
    return 1;
}

int _XRead(Display *dpy, char *bytes, long size)
{
    WmbDisplay *w = state(dpy);
    if (size == 0) return 0;
    if (size < 0 || !w->reply || (size_t)size > w->reply->size - w->read_pos)
        io_failure(dpy, "reply payload underflow");
    if (size) memcpy(bytes, w->reply->bytes + w->read_pos, (size_t)size);
    w->read_pos += (size_t)size;
    return 0;
}

void _XReadPad(Display *dpy, char *bytes, long size)
{
    _XRead(dpy, bytes, size);
    _XEatData(dpy, (4 - ((unsigned long)size & 3)) & 3);
}

void _XEatData(Display *dpy, unsigned long size)
{
    WmbDisplay *w = state(dpy);
    if (size == 0) return;
    if (!w->reply || size > w->reply->size - w->read_pos)
        io_failure(dpy, "reply discard underflow");
    w->read_pos += size;
}

void _XEatDataWords(Display *dpy, unsigned long words)
{
    if (words > WMB_WIRE_LIMIT / 4u) io_failure(dpy, "reply word count overflow");
    _XEatData(dpy, words * 4u);
}

int _XEventsQueued(Display *dpy, int mode)
{
    if (mode == QueuedAfterFlush) _XFlush(dpy);
    pending_errors(dpy);
    events(dpy);
    return dpy->qlen;
}

static bool_t event_available(void *argument)
{
    WmbConnection *c = argument;
    return c->events.count || c->responses.count || c->failed || c->closed;
}

int wmb_xlib_wait(Display *dpy, uint32_t milliseconds)
{
    if (!dpy || !dpy->xcb) {
        fputs("WMB Xlib: invalid display for event wait\n", stderr);
        abort();
    }
    if (wmb_bridge_is_server()) io_failure(dpy, "server cannot wait on client events");
    WmbConnection *c = state(dpy)->connection;
    if (XPending(dpy)) return 1;
    (void)fiber_wait_for(event_available, c, milliseconds);
    if (c->failed || c->closed) io_failure(dpy, "event connection closed");
    pending_errors(dpy);
    events(dpy);
    return dpy->qlen != 0;
}

void _XReadEvents(Display *dpy)
{
    WmbConnection *c = state(dpy)->connection;
    _XFlush(dpy);
    while (!dpy->qlen) {
        if (c->failed || c->closed) io_failure(dpy, "event connection closed");
        (void)fiber_wait_for(event_available, c, FIBER_WAIT_FOREVER);
        pending_errors(dpy);
        events(dpy);
    }
}

XID _XAllocID(Display *dpy)
{
    XID id;
    if (dpy->bufptr == dpy->buffer) wmb_xlib_prepare_ids(dpy);
    if (dpy->resource_id > (dpy->resource_mask >> dpy->resource_shift))
        io_failure(dpy, "resource ID range exhausted");
    id = dpy->resource_base | (dpy->resource_id << dpy->resource_shift);
    dpy->resource_id++;
    return id;
}
void _XAllocIDs(Display *dpy, XID *ids, int count)
{
    int i;
    for (i = 0; i < count; i++) ids[i] = _XAllocID(dpy);
}
void wmb_xlib_prepare_ids(Display *dpy)
{
    WmbDisplay *w = state(dpy);
    xXCMiscGetXIDRangeReq *request;
    xXCMiscGetXIDRangeReply reply;
    int opcode, event, error_code;
    if (!w || w->refilling_ids || !dpy->resource_mask ||
        dpy->resource_id <= dpy->resource_max) return;
    w->refilling_ids = true;
    _XFlush(dpy);
    if (!XQueryExtension(dpy, XCMiscExtensionName, &opcode, &event, &error_code))
        io_failure(dpy, "XC-MISC resource refill is unavailable");
    request = _XGetRequest(dpy, (CARD8)opcode, sizeof(*request));
    if (!request) io_failure(dpy, "resource refill request allocation failed");
    request->miscReqType = X_XCMiscGetXIDRange;
    if (!_XReply(dpy, (xReply *)&reply, 0, True) || reply.count <= 5)
        io_failure(dpy, "server resource ID range exhausted");
    dpy->resource_id = (reply.start_id & dpy->resource_mask) >> dpy->resource_shift;
    dpy->resource_max = dpy->resource_id + reply.count - 5;
    w->refilling_ids = false;
}
void _XIDHandler(Display *dpy)
{
    if (dpy->bufptr == dpy->buffer) wmb_xlib_prepare_ids(dpy);
}
unsigned long _XNextRequest(Display *dpy) { return NextRequest(dpy); }
