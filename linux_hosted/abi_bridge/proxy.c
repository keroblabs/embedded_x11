#define _POSIX_C_SOURCE 200809L
#include "proxy_internal.h"
#include "client.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>

static char endpoint[sizeof(((struct sockaddr_un *)0)->sun_path)];
static RpcProxyDisplay control = {.descriptor = -1};
static size_t open_displays;
static uint32_t provider_version, provider_count;

_Noreturn void rpc_proxy_fatal(const char *reason)
{
    rpc_error(reason);
    abort();
}
size_t rpc_proxy_count(int64_t value)
{
    if (value < 0 || value > RPC_LIMIT) rpc_proxy_fatal("negative or oversized argument count");
    return (size_t)value;
}
RpcProxyDisplay *rpc_proxy_display(Display *display)
{
    if (!display || display->descriptor < 0 || !display->token)
        rpc_proxy_fatal("invalid proxy Display");
    return display;
}
RpcProxyDisplay *rpc_proxy_control(void)
{
    if (control.descriptor < 0) rpc_proxy_fatal("bridge is not bound");
    return &control;
}

int rpc_proxy_exchange(RpcProxyDisplay *connection, RpcFrame *request, RpcFrame *reply)
{
    struct timeval deadline = {6, 0};
    if (connection->sequence == UINT32_MAX) return rpc_error("connection sequence exhausted");
    request->sequence = ++connection->sequence;
    if (request->operation == RPC_SLOT_XNextEvent) deadline.tv_sec = 0;
    else if (request->operation == RPC_SLOT_wmbYield)
        deadline.tv_sec += (time_t)(request->values[0] / 1000);
    if (setsockopt(connection->descriptor, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline)) != 0) {
        perror("WMB proxy timeout"); return -1;
    }
    if (rpc_write(connection->descriptor, request) || rpc_read(connection->descriptor, reply))
        return rpc_error("provider disconnected or did not return a complete response");
    if (reply->operation != request->operation || reply->sequence != request->sequence)
        return rpc_error("response operation/sequence mismatch");
    if (reply->status) {
        fprintf(stderr, "WMB proxy: provider rejected operation %u with status %d\n",
                request->operation, reply->status);
        return -1;
    }
    return 0;
}

static int connect_provider(uint32_t *version, uint32_t *count)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    RpcProxyDisplay connection = {.descriptor = -1};
    RpcFrame request = {.operation = RPC_HELLO}, reply = {0};
    int descriptor = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) { perror("WMB proxy socket"); return -1; }
    strcpy(address.sun_path, endpoint);
    if (connect(descriptor, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("WMB proxy connect"); close(descriptor); return -1;
    }
    connection.descriptor = descriptor;
    request.values[0] = WMB_API_MAGIC; request.values[1] = WMB_API_VERSION;
    request.values[2] = WMB_API_SLOT_COUNT; request.values[3] = WMB_API_LAYOUT;
    request.values[4] = rpc_byte_order();
    if (rpc_proxy_exchange(&connection, &request, &reply) ||
        reply.values[0] != WMB_API_MAGIC || reply.values[3] != WMB_API_LAYOUT ||
        reply.values[4] != rpc_byte_order() ||
        reply.values[1] > UINT32_MAX || reply.values[2] > UINT32_MAX) {
        rpc_error("incompatible host protocol, ABI data model or byte order");
        rpc_clear(&request); rpc_clear(&reply); close(descriptor); return -1;
    }
    *version = (uint32_t)reply.values[1]; *count = (uint32_t)reply.values[2];
    rpc_clear(&request); rpc_clear(&reply);
    return descriptor;
}

static RpcProxyObject *object(RpcProxyDisplay *connection, RpcReferenceKind kind, uint64_t token)
{
    for (RpcProxyObject *entry = connection->objects; entry; entry = entry->next)
        if (entry->kind == kind && entry->token == token) return entry;
    return NULL;
}
static void remember(RpcProxyDisplay *connection, RpcReferenceKind kind, uint64_t token,
                      void *pointer, bool owned)
{
    RpcProxyObject *entry = malloc(sizeof(*entry));
    if (!entry) rpc_proxy_fatal("proxy reference allocation failed");
    *entry = (RpcProxyObject){connection->objects, kind, token, pointer, owned};
    connection->objects = entry;
}
uint64_t rpc_proxy_token(RpcProxyDisplay *connection, const void *pointer, RpcReferenceKind kind)
{
    if (kind == RPC_REF_DISPLAY && pointer == connection) return connection->token;
    for (RpcProxyObject *entry = connection->objects; entry; entry = entry->next)
        if (entry->kind == kind && entry->pointer == pointer) return entry->token;
    rpc_proxy_fatal("unregistered, expired or cross-Display proxy object");
}
Screen *rpc_proxy_screen(RpcProxyDisplay *connection, uint64_t token)
{
    RpcProxyObject *entry;
    Screen *screen;
    if (!token) return NULL;
    entry = object(connection, RPC_REF_SCREEN, token);
    if (entry) return entry->pointer;
    screen = malloc(sizeof(*screen));
    if (!screen) rpc_proxy_fatal("Screen proxy allocation failed");
    *screen = (Screen){connection, token};
    remember(connection, RPC_REF_SCREEN, token, screen, false);
    return screen;
}
GC rpc_proxy_gc(RpcProxyDisplay *connection, uint64_t token, bool owned)
{
    RpcProxyObject *entry;
    GC gc;
    if (!token) return NULL;
    entry = object(connection, RPC_REF_GC, token);
    if (entry) return entry->pointer;
    gc = malloc(sizeof(*gc));
    if (!gc) rpc_proxy_fatal("GC proxy allocation failed");
    *gc = (struct _XGC){connection, token, owned};
    remember(connection, RPC_REF_GC, token, gc, owned);
    return gc;
}
void rpc_proxy_forget_gc(RpcProxyDisplay *connection, GC gc)
{
    RpcProxyObject **position = &connection->objects, *entry;
    while (*position && (*position)->pointer != gc) position = &(*position)->next;
    entry = *position;
    if (!entry || !entry->owned) rpc_proxy_fatal("attempt to release a borrowed/unregistered GC");
    *position = entry->next; free(entry->pointer); free(entry);
}
Display *rpc_proxy_screen_owner(Screen *screen)
{
    if (!screen || !screen->owner) rpc_proxy_fatal("invalid Screen proxy");
    return screen->owner;
}
static Visual *cache_visual(RpcProxyDisplay *connection, const Visual *value)
{
    RpcProxyObject *entry = object(connection, 0, value->visualid);
    Visual *visual;
    if (entry) return entry->pointer;
    visual = malloc(sizeof(*visual));
    if (!visual) rpc_proxy_fatal("Visual snapshot allocation failed");
    *visual = *value; visual->ext_data = NULL;
    remember(connection, 0, value->visualid, visual, false);
    return visual;
}
Visual *rpc_proxy_visual(RpcProxyDisplay *connection, const RpcFrame *frame, unsigned index)
{
    Visual visual;
    if (!frame->result) return NULL;
    if (rpc_unpack_visual(frame, index, &visual)) rpc_proxy_fatal("invalid Visual snapshot");
    return cache_visual(connection, &visual);
}
int rpc_proxy_window(RpcProxyDisplay *connection, const RpcFrame *frame, unsigned index,
                      XWindowAttributes *attributes)
{
    Visual visual;
    uint64_t screen;
    if (rpc_unpack_window(frame, index, attributes, &visual, &screen)) return -1;
    if (attributes->visual) attributes->visual = cache_visual(connection, &visual);
    attributes->screen = rpc_proxy_screen(connection, screen);
    return 0;
}
char *rpc_proxy_string(RpcProxyDisplay *connection, const RpcFrame *frame, bool borrowed,
                       unsigned operation)
{
    char *result;
    char **cache = operation == RPC_SLOT_XServerVendor ? &connection->vendor : &connection->name;
    if (!frame->result) return NULL;
    if (!frame->sizes[RPC_RESULT_BLOB] ||
        memchr(frame->data[RPC_RESULT_BLOB], 0, frame->sizes[RPC_RESULT_BLOB]) !=
            frame->data[RPC_RESULT_BLOB] + frame->sizes[RPC_RESULT_BLOB] - 1)
        rpc_proxy_fatal("invalid returned string");
    if (borrowed && *cache) return *cache;
    result = malloc(frame->sizes[RPC_RESULT_BLOB]);
    if (!result) rpc_proxy_fatal("returned string allocation failed");
    memcpy(result, frame->data[RPC_RESULT_BLOB], frame->sizes[RPC_RESULT_BLOB]);
    if (borrowed) *cache = result;
    return result;
}

int rpc_proxy_put_event(Display *display, const XEvent *event)
{
    RpcProxyDisplay *connection = rpc_proxy_display(display);
    RpcProxyEvent *entry;
    if (!event || event->type < KeyPress || event->type > MappingNotify)
        rpc_proxy_fatal("only core events are part of ABI v1");
    if (connection->event_count >= 4096) rpc_proxy_fatal("local event queue limit exceeded");
    entry = malloc(sizeof(*entry));
    if (!entry) rpc_proxy_fatal("local event allocation failed");
    entry->event = *event; entry->next = connection->events;
    connection->events = entry; connection->event_count++;
    return 0;
}
int rpc_proxy_take_event(RpcProxyDisplay *connection, long mask, Window window, int type,
                         XEvent *event)
{
    RpcProxyEvent **position = &connection->events, *entry;
    if (!event) rpc_proxy_fatal("NULL event return buffer");
    while ((entry = *position) != NULL) {
        bool matches = type == -1 ? true : type == -2 ?
            (rpc_event_mask(&entry->event) & mask) != 0 :
            entry->event.type == type && entry->event.xany.window == window;
        if (matches) {
            *position = entry->next; *event = entry->event;
            free(entry); connection->event_count--; return 1;
        }
        position = &entry->next;
    }
    return 0;
}

void rpc_proxy_discard_events(RpcProxyDisplay *connection)
{
    while (connection->events) {
        RpcProxyEvent *entry = connection->events;
        connection->events = entry->next;
        free(entry);
    }
    connection->event_count = 0;
}

Display *rpc_proxy_open(const char *name)
{
    RpcProxyDisplay *connection;
    RpcFrame request = {.operation = RPC_SLOT_XOpenDisplay}, reply = {0};
    uint32_t version, count;
    if (control.descriptor < 0) rpc_proxy_fatal("open before bridge binding");
    int descriptor = connect_provider(&version, &count);
    if (descriptor < 0) return NULL;
    if (version != provider_version || count != provider_count) {
        rpc_error("provider changed after binding"); close(descriptor); return NULL;
    }
    connection = calloc(1, sizeof(*connection));
    if (!connection) { rpc_error("Display proxy allocation failed"); close(descriptor); return NULL; }
    connection->descriptor = descriptor; connection->sequence = 1;
    request.values[0] = name != NULL;
    if ((name && rpc_blob(&request, 0, name, strlen(name) + 1)) ||
        rpc_proxy_exchange(connection, &request, &reply) || !reply.result) {
        rpc_error("remote display open failed"); close(descriptor); free(connection);
        rpc_clear(&request); rpc_clear(&reply); return NULL;
    }
    connection->token = reply.result; open_displays++;
    rpc_clear(&request); rpc_clear(&reply);
    return connection;
}
int rpc_proxy_close(Display *display)
{
    RpcProxyDisplay *connection = rpc_proxy_display(display);
    RpcFrame request = {.operation = RPC_SLOT_XCloseDisplay}, reply = {0};
    int result;
    request.values[0] = connection->token;
    if (rpc_proxy_exchange(connection, &request, &reply)) rpc_proxy_fatal("remote Display close failed");
    result = (int)(int64_t)reply.result;
    rpc_clear(&request); rpc_clear(&reply);
    if (close(connection->descriptor) != 0) perror("WMB proxy close");
    while (connection->objects) {
        RpcProxyObject *entry = connection->objects;
        connection->objects = entry->next; free(entry->pointer); free(entry);
    }
    rpc_proxy_discard_events(connection);
    free(connection->vendor); free(connection->name); free(connection); open_displays--;
    return result;
}

#include "proxy_generated.inc"

int wmb_bridge_bind(const char *path)
{
    uint32_t version, count;
    if (!path || !*path || strlen(path) >= sizeof(endpoint) || control.descriptor >= 0)
        return rpc_error("invalid endpoint or repeated live bridge binding");
    strcpy(endpoint, path);
    control.descriptor = connect_provider(&version, &count);
    if (control.descriptor < 0) return -1;
    provider_version = version; provider_count = count;
    proxy_api.version = version < WMB_API_VERSION ? version : WMB_API_VERSION;
    proxy_api.count = count < WMB_API_SLOT_COUNT ? count : WMB_API_SLOT_COUNT;
    control.sequence = 1;
    if (wmb_bind(&proxy_api) != 0) {
        close(control.descriptor); control.descriptor = -1; return -1;
    }
    return 0;
}
int wmb_bridge_disconnect(void)
{
    if (open_displays) return rpc_error("close all Displays before bridge disconnect");
    if (control.descriptor < 0) return rpc_error("bridge is not connected");
    if (close(control.descriptor) != 0) { perror("WMB bridge disconnect"); return -1; }
    control = (RpcProxyDisplay){.descriptor = -1};
    return 0;
}
