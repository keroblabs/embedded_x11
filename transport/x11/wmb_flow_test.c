#include "wmb_test_client.h"
#include "wmb_xlib_internal.h"
#include "hal_task.h"
#include "hal_time.h"
#include <X11/extensions/xtestproto.h>
#include <X11/extensions/bigreqsproto.h>
#include <X11/extensions/syncproto.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    Display *display;
    bool started, done, grabbed;
    bool success;
} Worker;

static bool_t started(void *argument) { return ((Worker *)argument)->started; }
static bool_t finished(void *argument) { return ((Worker *)argument)->done; }
static Bool async_focus(Display *display, xReply *reply, char *bytes, int size, XPointer data)
{
    int *received = (int *)(void *)data;
    (void)display; (void)bytes;
    if (reply->generic.type != X_Reply || size != 32) return False;
    (*received)++;
    return True;
}

static bool sync_deadline(Display *display)
{
    xSyncListSystemCountersReq *list;
    xSyncListSystemCountersReply reply;
    xSyncAwaitReq *await;
    xSyncWaitCondition *condition;
    unsigned char *data = NULL;
    CARD32 counter = 0;
    size_t size, offset = 0;
    uint32_t before;
    int opcode, event, error, index;

    if (!XQueryExtension(display, "SYNC", &opcode, &event, &error)) goto failed;
    list = _XGetRequest(display, (CARD8)opcode, sizeof(*list));
    if (!list) goto failed;
    list->syncReqType = X_SyncListSystemCounters;
    if (!_XReply(display, (xReply *)&reply, 0, False)) goto failed;
    size = (size_t)reply.length * 4;
    if (size > WMB_WIRE_LIMIT) goto failed;
    data = malloc(size);
    if (!data || _XRead(display, (char *)data, (long)size) != 0) goto failed;
    for (index = 0; index < reply.nCounters; index++) {
        xSyncSystemCounter item = {0};
        size_t record;
        if (offset > size || size - offset < sz_xSyncSystemCounter) goto failed;
        memcpy(&item, data + offset, sz_xSyncSystemCounter);
        record = (sz_xSyncSystemCounter + (size_t)item.name_length + 3u) & ~(size_t)3u;
        if (record > size - offset) goto failed;
        if (item.name_length == 10 &&
            memcmp(data + offset + sz_xSyncSystemCounter, "SERVERTIME", 10) == 0)
            counter = item.counter;
        offset += record;
    }
    free(data);
    data = NULL;
    if (!counter) goto failed;
    await = _XGetRequest(display, (CARD8)opcode,
                        sizeof(*await) + sizeof(*condition));
    if (!await) goto failed;
    await->syncReqType = X_SyncAwait;
    condition = (xSyncWaitCondition *)(void *)(await + 1);
    *condition = (xSyncWaitCondition){
        .counter = counter,
        .value_type = XSyncRelative,
        .wait_value_lo = 35,
        .test_type = XSyncPositiveComparison
    };
    before = hal_tick_ms();
    _XFlush(display);
    XSync(display, False);
    if (hal_tick_ms() - before < 30 || hal_tick_ms() - before > 500) goto failed;
    return true;
failed:
    free(data);
    fputs("P6 SYNC SERVERTIME deadline failed\n", stderr);
    return false;
}

static void competing_grab(void *argument)
{
    Worker *worker = argument;
    XWindowAttributes attributes;
    worker->started = true;
    fiber_notify();
    XGrabServer(worker->display);
    worker->grabbed = true;
    XUngrabServer(worker->display);
    worker->success = XGetWindowAttributes(worker->display,
                         DefaultRootWindow(worker->display), &attributes) != 0;
    XCloseDisplay(worker->display);
    worker->done = true;
    fiber_notify();
}

static void blocked_query(void *argument)
{
    Worker *worker = argument;
    XWindowAttributes attributes;
    worker->started = true;
    fiber_notify();
    worker->success = XGetWindowAttributes(worker->display,
                         DefaultRootWindow(worker->display), &attributes) != 0;
    XCloseDisplay(worker->display);
    worker->done = true;
    fiber_notify();
}

void wmb_flow_test_client(void *argument)
{
    WmbTestResult *result = argument;
    Display *owner = XOpenDisplay(NULL);
    Worker grab = {.display = XOpenDisplay(NULL)};
    Worker query = {.display = XOpenDisplay(NULL)};
    fiber_t *grab_fiber = NULL, *query_fiber = NULL;
    int opcode, event, error;
    bool held = false;
    uint32_t before;
    xXTestGrabControlReq *control;
    xXTestFakeInputReq *input;
    result->result = 1;
    if (!owner || !grab.display || !query.display) goto close;
    if (!XQueryExtension(grab.display, "XTEST", &opcode, &event, &error)) goto close;
    control = _XGetRequest(grab.display, (CARD8)opcode, sizeof(*control));
    if (!control) goto close;
    control->xtReqType = X_XTestGrabControl;
    control->impervious = True;
    _XFlush(grab.display);
    XGrabServer(owner);
    held = true;
    grab_fiber = fiber_create(competing_grab, &grab, 128 * 1024);
    query_fiber = fiber_create(blocked_query, &query, 128 * 1024);
    if (!grab_fiber || !query_fiber ||
        !fiber_wait_for(started, &grab, 1000) ||
        !fiber_wait_for(started, &query, 1000)) goto close;
    hal_yield_timeout(50);
    if (grab.grabbed || grab.done || query.done) goto close;
    {
        char *buffer = grab.display->buffer;
        if (_XBufferGrow(grab.display, 20000) == 0 || grab.display->buffer != buffer)
            goto close;
    }
    XUngrabServer(owner);
    held = false;
    if (!fiber_wait_for(finished, &grab, 1000) ||
        !fiber_wait_for(finished, &query, 1000) ||
        !grab.success || !query.success || !grab.grabbed) goto close;
    if (!XQueryExtension(owner, "XTEST", &opcode, &event, &error)) goto close;
    before = hal_tick_ms();
    input = _XGetRequest(owner, (CARD8)opcode, sizeof(*input));
    if (!input) goto close;
    input->xtReqType = X_XTestFakeInput;
    input->type = MotionNotify;
    input->detail = True;
    input->time = 35;
    input->root = (CARD32)DefaultRootWindow(owner);
    input->rootX = input->rootY = 0;
    _XFlush(owner);
    if (hal_tick_ms() - before < 30 || hal_tick_ms() - before > 500) goto close;
    XSync(owner, False);
    if (!sync_deadline(owner)) goto close;
    {
        int received = 0;
        _XAsyncHandler handler = {
            .next = owner->async_handlers, .handler = async_focus, .data = (XPointer)&received
        };
        owner->async_handlers = &handler;
        if (!_XGetRequest(owner, X_GetInputFocus, 4)) goto close;
        _XFlush(owner);
        owner->async_handlers = handler.next;
        if (received != 1) goto close;
    }
    {
        unsigned long ids, id = 0;
        unsigned char *property = malloc(280000);
        unsigned char *data = NULL;
        Atom atom, actual;
        int format;
        unsigned long count, remaining;
        if (!property) goto close;
        for (ids = 0; ids < 280000; ids++) property[ids] = (unsigned char)(ids * 13);
        if (owner->bigreq_size <= owner->max_request_size) { free(property); goto close; }
        atom = XInternAtom(owner, "WMB_P5_BIG", False);
        XChangeProperty(owner, DefaultRootWindow(owner), atom, XA_STRING, 8,
                        PropModeReplace, property, 280000);
        if (XGetWindowProperty(owner, DefaultRootWindow(owner), atom, 0, 70000,
            True, XA_STRING, &actual, &format, &count, &remaining, &data) != Success ||
            actual != XA_STRING || format != 8 || count != 280000 ||
            remaining != 0 || !data || memcmp(data, property, 280000)) {
            free(property); XFree(data); goto close;
        }
        free(property);
        XFree(data);
        for (ids = 0; ids < 65540; ids++)
            if (!_XGetRequest(owner, X_NoOperation, 4)) goto close;
        XSync(owner, False);
        if (owner->last_request_read != owner->request) goto close;
        for (ids = 0; ids < (owner->resource_mask >> owner->resource_shift) + 100; ids++)
            id = XAllocID(owner);
        if (!(id & owner->resource_base)) goto close;
        XSync(owner, False);
    }
    result->result = 0;
close:
    if (held) XUngrabServer(owner);
    if (grab_fiber) {
        if (!grab.done) (void)fiber_wait_for(finished, &grab, 1000);
        fiber_destroy(grab_fiber);
    } else if (grab.display) XCloseDisplay(grab.display);
    if (query_fiber) {
        if (!query.done) (void)fiber_wait_for(finished, &query, 1000);
        fiber_destroy(query_fiber);
    } else if (query.display) XCloseDisplay(query.display);
    if (owner) XCloseDisplay(owner);
    printf("P5 flow: clients/grab/replay, delayed input, SYNC deadline, BIG-REQUESTS, sequence wrap, XC-MISC %s\n",
           result->result ? "FAIL" : "PASS");
    result->done = true;
}
