#define _POSIX_C_SOURCE 200809L
#include "server_internal.h"
#include "hal_time.h"
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

void *rpc_server_reference(RpcServerConnection *connection, RpcReferenceKind kind, uint64_t token)
{
    unsigned position = (unsigned)(token & 255u);
    if (position && position <= 128) {
        RpcReference *reference = &connection->references[position - 1];
        if (reference->token == token && reference->kind == kind && reference->pointer)
            return reference->pointer;
    }
    rpc_error("unknown, expired or incorrectly typed remote handle");
    return NULL;
}

uint64_t rpc_server_add(RpcServerConnection *connection, RpcReferenceKind kind,
                        void *pointer, bool owned)
{
    unsigned index;
    if (!pointer) return 0;
    for (index = 0; index < 128; index++)
        if (connection->references[index].pointer == pointer &&
            connection->references[index].kind == kind)
            return connection->references[index].token;
    for (index = 0; index < 128; index++)
        if (!connection->references[index].pointer) break;
    if (index == 128 || connection->generation >= (UINT64_MAX >> 8)) {
        rpc_error("remote handle capacity/generation exhausted");
        if (kind == RPC_REF_GC && owned) {
            WmbApiCall call = {.slot = WMB_SLOT_XFreeGC};
            call.args.XFreeGC.p0 = connection->display;
            call.args.XFreeGC.p1 = pointer;
            if (wmb_gateway_submit(&call)) rpc_error("failed to release an unregistered GC");
        }
        return 0;
    }
    uint64_t token = (++connection->generation << 8) | (index + 1);
    connection->references[index] = (RpcReference){token, kind, pointer, owned};
    return token;
}

void rpc_server_forget(RpcServerConnection *connection, RpcReferenceKind kind, uint64_t token)
{
    unsigned index = (unsigned)(token & 255u);
    if (!index || index > 128 || connection->references[index - 1].token != token ||
        connection->references[index - 1].kind != kind) {
        rpc_error("invalid handle removal");
        return;
    }
    connection->references[index - 1] = (RpcReference){0};
}

int rpc_server_gc_owned(RpcServerConnection *connection, GC gc)
{
    for (unsigned index = 0; index < 128; index++)
        if (connection->references[index].kind == RPC_REF_GC &&
            connection->references[index].pointer == gc)
            return connection->references[index].owned ? 0 : rpc_error("default GC is borrowed");
    return rpc_error("unregistered GC");
}

int rpc_server_free_gcs(RpcServerConnection *connection)
{
    for (unsigned index = 0; index < 128; index++) {
        RpcReference *reference = &connection->references[index];
        if (reference->kind == RPC_REF_GC && reference->owned && reference->pointer) {
            WmbApiCall call = {.slot = WMB_SLOT_XFreeGC};
            call.args.XFreeGC.p0 = connection->display;
            call.args.XFreeGC.p1 = reference->pointer;
            if (wmb_gateway_submit(&call)) return rpc_error("GC cleanup failed");
            *reference = (RpcReference){0};
        }
    }
    return 0;
}

void rpc_server_closed(RpcServerConnection *connection)
{
    connection->display = NULL;
    connection->root = None;
    memset(connection->windows, 0, sizeof(connection->windows));
    memset(connection->references, 0, sizeof(connection->references));
}

int rpc_server_native_free(void *memory)
{
    WmbApiCall call = {.slot = WMB_SLOT_XFree};
    call.args.XFree.p0 = memory;
    return wmb_gateway_submit(&call);
}

int rpc_server_shape(const RpcFrame *frame, unsigned count)
{
    if (count > RPC_ARGUMENTS || frame->status || frame->result ||
        frame->sizes[RPC_RESULT_BLOB]) return rpc_error("invalid request prefix");
    for (unsigned index = count; index < RPC_ARGUMENTS; index++)
        if (frame->values[index] || frame->sizes[index]) return rpc_error("extra request arguments");
    return 0;
}

int rpc_server_presence(const RpcFrame *frame, unsigned index, bool nullable)
{
    if (index >= RPC_ARGUMENTS || frame->values[index] > 1 ||
        (!nullable && !frame->values[index]) ||
        (!frame->values[index] && frame->sizes[index]))
        return rpc_error("invalid pointer-presence flag");
    return 0;
}
int rpc_server_string(const RpcFrame *frame, unsigned index)
{
    if (!frame->values[index]) return frame->sizes[index] ? rpc_error("unexpected NULL string data") : 0;
    if (!frame->sizes[index] ||
        memchr(frame->data[index], 0, frame->sizes[index]) != frame->data[index] + frame->sizes[index] - 1)
        return rpc_error("noncanonical or unterminated string");
    return 0;
}
size_t rpc_server_count(uint64_t value)
{
    if (value > RPC_LIMIT) { rpc_error("negative or oversized element count"); return SIZE_MAX; }
    return (size_t)value;
}

int rpc_server_track_window(RpcServerConnection *connection, Window window, Window parent)
{
    if (!connection->root) {
        WmbApiCall call = {.slot = WMB_SLOT_XDefaultRootWindow};
        call.args.XDefaultRootWindow.p0 = connection->display;
        if (wmb_gateway_submit(&call)) return rpc_error("root lookup failed");
        connection->root = call.result.XDefaultRootWindow;
    }
    if (parent != connection->root) return 0;
    for (unsigned index = 0; index < 128; index++)
        if (!connection->windows[index]) { connection->windows[index] = window; return 0; }
    return rpc_error("top-level window tracking capacity exhausted");
}

void rpc_server_destroyed_window(RpcServerConnection *connection, Window window)
{
    for (unsigned index = 0; index < 128; index++)
        if (connection->windows[index] == window) connection->windows[index] = None;
}

int rpc_server_prepare_close(RpcServerConnection *connection)
{
    uint32_t started;
    for (unsigned index = 0; index < 128; index++) {
        if (!connection->windows[index]) continue;
        WmbApiCall call = {.slot = WMB_SLOT_XUnmapWindow};
        call.args.XUnmapWindow.p0 = connection->display;
        call.args.XUnmapWindow.p1 = connection->windows[index];
        if (wmb_gateway_submit(&call)) return rpc_error("window withdrawal failed");
    }
    started = hal_tick_ms();
    for (unsigned index = 0; index < 128; index++) {
        Window root, parent, *children = NULL;
        unsigned count;
        if (!connection->windows[index]) continue;
        do {
            WmbApiCall call = {.slot = WMB_SLOT_XQueryTree};
            call.args.XQueryTree.p0 = connection->display;
            call.args.XQueryTree.p1 = connection->windows[index];
            call.args.XQueryTree.p2 = &root; call.args.XQueryTree.p3 = &parent;
            call.args.XQueryTree.p4 = &children; call.args.XQueryTree.p5 = &count;
            if (wmb_gateway_submit(&call) || !call.result.XQueryTree)
                return rpc_error("window withdrawal query failed");
            if (children && rpc_server_native_free(children)) return -1;
            children = NULL;
            if (parent == connection->root) break;
            if (hal_tick_ms() - started >= 2000) return rpc_error("window withdrawal deadline");
            struct timespec delay = {0, 1000000};
            while (nanosleep(&delay, &delay) != 0)
                if (errno != EINTR) return rpc_error("window withdrawal sleep failed");
        } while (true);
    }
    return rpc_server_free_gcs(connection);
}
