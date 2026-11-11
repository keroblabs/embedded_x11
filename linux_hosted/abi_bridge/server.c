#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "server.h"
#include "server_internal.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "server_generated.inc"

typedef struct {
    pthread_t thread;
    int descriptor;
    bool used;
    atomic_bool done;
} Reader;
static Reader readers[16];
static pthread_t acceptor, watcher;
static pthread_mutex_t descriptors = PTHREAD_MUTEX_INITIALIZER;
static int listener = -1;
static atomic_bool requested, draining, accepting, capture;
static atomic_uint bad_frames, calls;
static uint32_t advertised_version, advertised_count;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];

static void sleep_ms(unsigned milliseconds)
{
    struct timespec value = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    while (nanosleep(&value, &value) != 0)
        if (errno != EINTR) { perror("RPC server sleep"); abort(); }
}
static void *read_connection(void *argument)
{
    Reader *reader = argument;
    int descriptor = reader->descriptor;
    RpcServerConnection connection = {0};
    uint32_t expected = 1;
    bool greeted = false;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) abort();
    for (;;) {
        RpcFrame request = {0}, reply = {0};
        int result = rpc_read(descriptor, &request);
        if (result) {
            rpc_clear(&request);
            if (result < 0 && !atomic_load(&draining)) atomic_fetch_add(&bad_frames, 1);
            break;
        }
        reply.operation = request.operation; reply.sequence = request.sequence;
        if (request.sequence != expected++ || request.status || request.result) {
            reply.status = rpc_error("invalid connection sequence or request status");
        } else if (!greeted) {
            if (request.operation != RPC_HELLO || rpc_server_shape(&request, 5) ||
                request.values[0] != WMB_API_MAGIC || request.values[3] != WMB_API_LAYOUT ||
                request.values[4] != rpc_byte_order()) {
                reply.status = rpc_error("incompatible host protocol/data model/byte order");
            } else {
                greeted = true;
                reply.values[0] = WMB_API_MAGIC; reply.values[1] = advertised_version;
                reply.values[2] = advertised_count; reply.values[3] = WMB_API_LAYOUT;
                reply.values[4] = rpc_byte_order();
            }
        } else if (request.operation == RPC_STOP) {
            if (rpc_server_shape(&request, 0)) reply.status = -1;
            else { atomic_store(&requested, true); reply.result = 1; }
        } else if (request.operation == RPC_CAPTURE) {
            if (rpc_server_shape(&request, 0)) reply.status = -1;
            else { atomic_store(&capture, true); reply.result = 1; }
        } else if (request.operation >= advertised_count) {
            reply.status = rpc_error("operation is outside negotiated API capability");
        } else {
            reply.status = dispatch_typed(&connection, &request, &reply);
            atomic_fetch_add(&calls, 1);
        }
        if (reply.status) atomic_fetch_add(&bad_frames, 1);
        result = rpc_write(descriptor, &reply);
        rpc_clear(&request); rpc_clear(&reply);
        if (result || expected == 0) break;
    }
    if (connection.display) {
        WmbApiCall close_call = {.slot = WMB_SLOT_XCloseDisplay};
        if (rpc_server_prepare_close(&connection)) rpc_error("disconnected client cleanup failed");
        close_call.args.XCloseDisplay.p0 = connection.display;
        if (wmb_gateway_submit(&close_call)) rpc_error("disconnected Display close failed");
    }
    pthread_mutex_lock(&descriptors);
    if (close(descriptor) != 0) perror("RPC accepted socket close");
    reader->descriptor = -1;
    pthread_mutex_unlock(&descriptors);
    atomic_store_explicit(&reader->done, true, memory_order_release);
    return NULL;
}
static void *accept_connections(void *unused)
{
    (void)unused;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) abort();
    while (atomic_load(&accepting)) {
        int descriptor = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
        if (descriptor < 0) {
            if (errno == EINTR) continue;
            if (atomic_load(&accepting)) { perror("RPC accept"); atomic_store(&requested, true); }
            break;
        }
        unsigned index;
        pthread_mutex_lock(&descriptors);
        for (index = 0; index < 16; index++) {
            if (readers[index].used && atomic_load_explicit(&readers[index].done, memory_order_acquire)) {
                if (pthread_join(readers[index].thread, NULL) != 0) abort();
                readers[index].used = false;
            }
            if (!readers[index].used) break;
        }
        if (index == 16) { rpc_error("reader capacity exhausted"); close(descriptor); }
        else {
            Reader *reader = &readers[index];
            reader->descriptor = descriptor; reader->used = true;
            atomic_store(&reader->done, false);
            if (pthread_create(&reader->thread, NULL, read_connection, reader) != 0) {
                rpc_error("reader creation failed"); reader->used = false; close(descriptor);
            }
        }
        pthread_mutex_unlock(&descriptors);
    }
    return NULL;
}
static void *watch_shutdown(void *unused)
{
    (void)unused;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) abort();
    while (!atomic_load(&draining)) sleep_ms(1);
    while (!wmb_gateway_stopping()) sleep_ms(1);
    sleep_ms(200);
    pthread_mutex_lock(&descriptors);
    for (unsigned index = 0; index < 16; index++)
        if (readers[index].used && readers[index].descriptor >= 0)
            if (shutdown(readers[index].descriptor, SHUT_RDWR) != 0 && errno != ENOTCONN)
                perror("RPC reader shutdown");
    pthread_mutex_unlock(&descriptors);
    return NULL;
}
int wmb_rpc_server_start(const char *path, uint32_t version)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    mode_t previous;
    if (!path || !*path || strlen(path) >= sizeof(address.sun_path) ||
        (version != 1 && version != WMB_API_VERSION) || listener >= 0)
        return rpc_error("invalid server endpoint/version or repeated start");
    advertised_version = version;
    advertised_count = version == 1 ? RPC_SLOT_wmbApiRevision : WMB_API_SLOT_COUNT;
    strcpy(address.sun_path, path); strcpy(socket_path, path);
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0) { perror("RPC socket"); return -1; }
    previous = umask(0077);
    int result = bind(listener, (const struct sockaddr *)&address, sizeof(address));
    umask(previous);
    if (result != 0 || listen(listener, 8) != 0) {
        perror("RPC bind/listen");
        close(listener); listener = -1;
        if (result == 0 && unlink(path) != 0) perror("RPC failed-start endpoint cleanup");
        return -1;
    }
    atomic_store(&accepting, true);
    if (pthread_create(&acceptor, NULL, accept_connections, NULL) != 0) {
        rpc_error("acceptor creation failed"); close(listener); listener = -1;
        if (unlink(path) != 0) perror("RPC failed-start endpoint cleanup");
        return -1;
    }
    if (pthread_create(&watcher, NULL, watch_shutdown, NULL) != 0) {
        atomic_store(&accepting, false); shutdown(listener, SHUT_RDWR);
        pthread_join(acceptor, NULL); close(listener); listener = -1;
        if (unlink(path) != 0) perror("RPC failed-start endpoint cleanup");
        return rpc_error("shutdown watcher creation failed");
    }
    return 0;
}
bool wmb_rpc_server_stop_requested(void) { return atomic_load(&requested); }
bool wmb_rpc_server_take_capture(void) { return atomic_exchange(&capture, false); }
void wmb_rpc_server_begin_shutdown(void)
{
    atomic_store(&accepting, false); atomic_store(&draining, true);
    if (shutdown(listener, SHUT_RDWR) != 0 && errno != ENOTCONN) perror("RPC listener shutdown");
}
int wmb_rpc_server_join(void)
{
    int result = 0;
    if (pthread_join(acceptor, NULL) != 0 || pthread_join(watcher, NULL) != 0) result = -1;
    for (unsigned index = 0; index < 16; index++)
        if (readers[index].used && pthread_join(readers[index].thread, NULL) != 0) result = -1;
    if (close(listener) != 0 || unlink(socket_path) != 0) { perror("RPC endpoint cleanup"); result = -1; }
    listener = -1;
    printf("P8 RPC: decoded typed calls=%u rejected frames=%u; no process pointers on wire\n",
           atomic_load(&calls), atomic_load(&bad_frames));
    return result;
}
