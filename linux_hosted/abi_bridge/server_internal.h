#ifndef WMB_RPC_SERVER_INTERNAL_H
#define WMB_RPC_SERVER_INTERNAL_H
#include "rpc.h"
#include "rpc_metadata.h"
#include "api_calls.h"
#include "wmb_gateway.h"

typedef enum {RPC_REF_DISPLAY = 1, RPC_REF_SCREEN = 2, RPC_REF_GC = 3} RpcReferenceKind;
typedef struct {
    uint64_t token;
    RpcReferenceKind kind;
    void *pointer;
    bool owned;
} RpcReference;
typedef struct {
    Display *display;
    RpcReference references[128];
    uint64_t generation;
    Window root, windows[128];
} RpcServerConnection;
typedef union {
    XGCValues gc;
    XWindowChanges changes;
    XSetWindowAttributes attributes;
    XWindowAttributes window;
    XSizeHints hints;
    XColor color;
    XEvent event;
    Visual visual;
    int integer;
    unsigned unsigned_integer;
    unsigned long word;
    Window window_id;
    Atom atom;
    Window *children;
    unsigned char *bytes;
} RpcValue;
void *rpc_server_reference(RpcServerConnection *, RpcReferenceKind, uint64_t);
uint64_t rpc_server_add(RpcServerConnection *, RpcReferenceKind, void *, bool);
void rpc_server_forget(RpcServerConnection *, RpcReferenceKind, uint64_t);
int rpc_server_gc_owned(RpcServerConnection *, GC);
int rpc_server_free_gcs(RpcServerConnection *);
void rpc_server_closed(RpcServerConnection *);
int rpc_server_native_free(void *);
int rpc_server_shape(const RpcFrame *, unsigned);
int rpc_server_presence(const RpcFrame *, unsigned, bool);
int rpc_server_string(const RpcFrame *, unsigned);
size_t rpc_server_count(uint64_t);
int rpc_server_track_window(RpcServerConnection *, Window, Window);
void rpc_server_destroyed_window(RpcServerConnection *, Window);
int rpc_server_prepare_close(RpcServerConnection *);
#endif
