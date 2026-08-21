#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xlibint.h"
#include "Xlibint_wmb.h"
#include "Xdefs.h"
#include "dixstruct.h"
#include "Xxcbint.h"
#include "wmb_io.h"

/* Configuration */
#define WMB_INITIAL_BUFFER_SIZE     4096        /* Initial dpy->buffer size */
#define WMB_MAX_BUFFER_SIZE         (1 << 20)   /* 1MB hard limit */
#define REQ_SIZE_EXTRA_HEADROOM     256         /* Extra space when resizing */
#define REQ_SIZE_GROWTH_FACTOR      2           /* Double the buffer size on resize */

/* WMB Internal State */
typedef struct {
    ClientPtr       client;          /* TinyX client record */
    CARD8           pending_opcode;  /* Opcode of pending request (0 = none) */
    char           *reply_buffer;    /* Reply from last Proc* call */
    size_t          reply_size;      /* Size of reply data */
    size_t          reply_offset;    /* Read position in reply */
} WmbState;

static WmbState wmb_state = {0};

/*
 * wmb_sync_handler - Called by SyncHandle() after every X11 API call
 *
 * This is the execution trigger. When called:
 * - The request buffer (dpy->buffer) has been completely filled
 * - Data() has appended any variable-length data
 * - UnlockDisplay() has been called
 * - Total request length = dpy->bufptr - dpy->buffer
 */
int wmb_sync_handler(Display *dpy)
{
    if (wmb_state.pending_opcode == 0)
        return 0;  /* No pending request (already executed by _XReply) */
    
    if (wmb_state.client == NULL || dpy->buffer == NULL)
        return 0;  /* Not initialized */
    
    /* Calculate actual request length (fixed header + variable data) */
    size_t request_length = dpy->bufptr - dpy->buffer;
    
    /* Update length field in request header (in 4-byte units) */
    xReq *req = (xReq *)dpy->buffer;
    req->length = (request_length + 3) >> 2;  /* Round up to 4-byte boundary */
    
    /* Point client to dpy->buffer (the complete request) */
    wmb_state.client->requestBuffer = dpy->buffer;
    
    /* Execute the Proc* function directly! */
    CARD8 opcode = wmb_state.pending_opcode;
    int result = (*wmb_state.client->requestVector[opcode])(wmb_state.client);
    
    /* Clear pending - request has been executed */
    wmb_state.pending_opcode = 0;
    
    /* Reset buffer for next request */
    dpy->bufptr = dpy->buffer;
    
    /* TODO: Handle errors - generate X error events if result != Success */
    
    return 0;
}

/*
 * _XGetRequest - Allocate buffer for building a request
 *
 * In standard libX11, this allocates from XCB's buffer.
 * In WMB, we use dpy->buffer directly! This allows Data() macro
 * to append variable-length data naturally.
 *
 * The complete request will be at dpy->buffer when SyncHandle() is called.
 */
void *_XGetRequest(Display *dpy, CARD8 type, size_t len)
{
    /* Ensure buffer is large enough for initial request */
    if (dpy->buffer == NULL || (dpy->bufmax - dpy->buffer) < len) {
        size_t new_size = (len > WMB_INITIAL_BUFFER_SIZE) ? len : WMB_INITIAL_BUFFER_SIZE;
        char *new_buffer = Xrealloc(dpy->buffer, new_size);
        if (new_buffer == NULL)
            return NULL;
        dpy->buffer = new_buffer;
        dpy->bufmax = new_buffer + new_size;
    }
    
    /* Reset buffer pointer to start - new request begins here */
    dpy->bufptr = dpy->buffer;
    
    /* Clear initial request area */
    memset(dpy->buffer, 0, len);
    
    /* Initialize request header */
    xReq *req = (xReq *)dpy->buffer;
    req->reqType = type;
    req->length = len >> 2;  /* Length in 4-byte units (updated by SyncHandle) */
    
    /* Advance bufptr past fixed header - Data() will append here */
    dpy->bufptr = dpy->buffer + len;
    
    /* Record opcode - will be executed by SyncHandle() */
    wmb_state.pending_opcode = type;
    
    /* Update display request counter */
    X_DPY_REQUEST_INCREMENT(dpy);
    
    return dpy->buffer;
}

/*
 * _XReply - Execute request and get reply
 *
 * For requests that need a reply (XGetWindowAttributes, etc.),
 * this executes the request immediately rather than waiting
 * for SyncHandle().
 */
Status _XReply(Display *dpy, xReply *rep, int extra, Bool discard)
{
    /* Execute the pending request */
    int result = wmb_dispatch_request();
    
    if (result != Success) {
        /* TODO: Generate error event */
        return 0;
    }
    
    /* Copy reply from client's reply buffer */
    /* Note: Need to determine where TinyX stores replies */
    if (wmb_state.reply_buffer && rep) {
        memcpy(rep, wmb_state.reply_buffer, sizeof(xReply));
        wmb_state.reply_offset = sizeof(xReply);
    }
    
    /* Reset for next request */
    wmb_state.pending_opcode = 0;
    
    return 1;  /* Success */
}

/*
 * _XRead - Read additional data from reply
 *
 * Some replies have variable-length data after the fixed header.
 */
int _XRead(Display *dpy, char *data, long size)
{
    if (wmb_state.reply_buffer == NULL)
        return -1;
    
    /* Copy from reply buffer starting at current offset */
    size_t available = wmb_state.reply_size - wmb_state.reply_offset;
    if (size > available)
        size = available;
    
    memcpy(data, wmb_state.reply_buffer + wmb_state.reply_offset, size);
    wmb_state.reply_offset += size;
    
    return 0;
}

void _XReadPad(Display *dpy, char *data, long size)
{
    _XRead(dpy, data, size);
    /* Padding is handled internally, no action needed */
}

/*
 * _XBufferOverflow - Resize dpy->buffer when Data() exceeds bufmax
 *
 * This replaces the _XSend() call in the original Data() macro.
 * Instead of sending data over socket, we resize the buffer.
 */
void _XBufferOverflow(Display *dpy, long needed)
{
    /* Calculate current usage and required size */
    size_t current_offset = dpy->bufptr - dpy->buffer;
    size_t current_size = dpy->bufmax - dpy->buffer;
    size_t required = current_offset + needed + REQ_SIZE_EXTRA_HEADROOM;  /* Extra headroom */
    
    /* Double the buffer size (up to max) */
    size_t new_size = current_size * REQ_SIZE_GROWTH_FACTOR;
    if (new_size < required)
        new_size = required;
    
    /* Enforce hard limit to prevent runaway allocations */
    if (new_size > WMB_MAX_BUFFER_SIZE) {
        /* TODO: Set error flag, request will fail */
        fprintf(stderr, "WMB: Buffer overflow limit reached\n");
        return;
    }
    
    /* Reallocate buffer */
    char *new_buffer = Xrealloc(dpy->buffer, new_size);
    if (new_buffer == NULL) {
        /* TODO: Set error flag */
        fprintf(stderr, "WMB: Buffer reallocation failed\n");
        return;
    }
    
    /* Update pointers (bufptr offset preserved) */
    dpy->buffer = new_buffer;
    dpy->bufptr = new_buffer + current_offset;
    dpy->bufmax = new_buffer + new_size;
}

/*
 * _XSend - Send data to server
 *
 * In WMB, this should NOT be called (Data() macro calls _XBufferOverflow).
 * Provided as safety net / debugging hook.
 */
void _XSend(Display *dpy, const char *data, long size)
{
    /* This should not be called in WMB architecture */
    fprintf(stderr, "WMB WARNING: _XSend called - should use Data() buffer\n");
    
    /* Append data to buffer as fallback */
    if (data && size > 0) {
        if (dpy->bufptr + size > dpy->bufmax) {
            _XBufferOverflow(dpy, size);
        }
        memcpy(dpy->bufptr, data, size);
        dpy->bufptr += ((size + 3) & ~3);
    }
}

/*
 * _XFlush - Flush output buffer
 *
 * In WMB, requests are executed by SyncHandle(), so this is mostly a NOP.
 */
void _XFlush(Display *dpy)
{
    /* Execute any pending request (safety net) */
    if (wmb_state.pending_opcode != 0) {
        wmb_sync_handler(dpy);
    }
}

/*
 * _XConnectXCB - Establish connection to X server
 *
 * In standard libX11, this creates an XCB socket connection.
 * In WMB, we register as an internal client with TinyX and
 * SET THE SYNCHANDLER for request execution.
 */
Bool _XConnectXCB(Display *dpy, const char *display, int *screenp)
{
    /* No socket connection needed - we ARE the server */
    
    /* Register as internal WMB client with TinyX */
    wmb_state.client = wmb_create_internal_client();
    if (wmb_state.client == NULL)
        return False;
    
    /* ⭐ KEY: Set the sync handler for request execution */
    dpy->synchandler = wmb_sync_handler;
    
    /* Default screen */
    *screenp = 0;
    
    /* Initialize minimal XCB structure (to prevent NULL crashes) */
    dpy->xcb = Xcalloc(1, sizeof(_X11XCBPrivate));
    if (dpy->xcb == NULL) {
        wmb_destroy_internal_client(wmb_state.client);
        return False;
    }
    
    /* Mark as WMB internal connection */
    dpy->xcb->connection = NULL;  /* No real XCB connection */
    
    return True;
}
