#ifndef WMB_RPC_PROXY_INTERNAL_H
#define WMB_RPC_PROXY_INTERNAL_H
#include "rpc.h"
#include "rpc_metadata.h"
#include <stdlib.h>
#include <string.h>

typedef enum {RPC_REF_DISPLAY = 1, RPC_REF_SCREEN = 2, RPC_REF_GC = 3} RpcReferenceKind;
typedef struct RpcProxyObject {
    struct RpcProxyObject *next;
    RpcReferenceKind kind;
    uint64_t token;
    void *pointer;
    bool owned;
} RpcProxyObject;
typedef struct RpcProxyEvent {
    struct RpcProxyEvent *next;
    XEvent event;
} RpcProxyEvent;
struct _XDisplay {
    int descriptor;
    uint32_t sequence;
    uint64_t token;
    RpcProxyObject *objects;
    RpcProxyEvent *events;
    size_t event_count;
    char *vendor, *name;
};
typedef struct _XDisplay RpcProxyDisplay;
struct _WmbScreen { RpcProxyDisplay *owner; uint64_t token; };
struct _XGC { RpcProxyDisplay *owner; uint64_t token; bool owned; };

_Noreturn void rpc_proxy_fatal(const char *);
size_t rpc_proxy_count(int64_t);
RpcProxyDisplay *rpc_proxy_display(Display *);
RpcProxyDisplay *rpc_proxy_control(void);
Display *rpc_proxy_open(const char *);
int rpc_proxy_close(Display *);
uint64_t rpc_proxy_token(RpcProxyDisplay *, const void *, RpcReferenceKind);
Screen *rpc_proxy_screen(RpcProxyDisplay *, uint64_t);
GC rpc_proxy_gc(RpcProxyDisplay *, uint64_t, bool);
void rpc_proxy_forget_gc(RpcProxyDisplay *, GC);
Display *rpc_proxy_screen_owner(Screen *);
Visual *rpc_proxy_visual(RpcProxyDisplay *, const RpcFrame *, unsigned);
int rpc_proxy_window(RpcProxyDisplay *, const RpcFrame *, unsigned, XWindowAttributes *);
char *rpc_proxy_string(RpcProxyDisplay *, const RpcFrame *, bool, unsigned);
int rpc_proxy_exchange(RpcProxyDisplay *, RpcFrame *, RpcFrame *);
int rpc_proxy_put_event(Display *, const XEvent *);
int rpc_proxy_take_event(RpcProxyDisplay *, long, Window, int, XEvent *);
void rpc_proxy_discard_events(RpcProxyDisplay *);
#endif
