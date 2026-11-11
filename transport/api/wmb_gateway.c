#include "api_internal.h"
#include "wmb_gateway.h"
#include "wmb_metrics.h"
#include "wmb_xlib.h"
#include "wmb.h"
#include "hal_task.h"
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>

#define CLIENT_LIMIT 4u
#define ADMISSION_LIMIT 64u
#define ADMISSION_CLOSED (1u << 31)
typedef struct {
    Display *display;
    hal_task_t *task;
    WmbApiCall *request;
    bool busy, finished;
} Client;
static Client clients[CLIENT_LIMIT];
static const char *const client_names[] = {"api0", "api1", "api2", "api3"};
static wmb_gateway_port_t platform;
static atomic_bool initialized, accepting;
static _Atomic(WmbApiCall *) mailbox;
static atomic_uint admission, posted, completed, rejected;
static bool failed;

bool wmb_gateway_is_owner(void)
{
    return atomic_load_explicit(&initialized, memory_order_acquire) &&
           platform.is_runtime_thread(platform.context);
}
bool wmb_gateway_stopping(void)
{
    return atomic_load_explicit(&initialized, memory_order_acquire) &&
           !atomic_load_explicit(&accepting, memory_order_acquire);
}

static void finish(WmbApiCall *call)
{
    if (call->status) atomic_fetch_add_explicit(&rejected, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&completed, 1, memory_order_relaxed);
    /* No reader/worker may access the descriptor after publishing completion. */
    atomic_store_explicit(&call->done, true, memory_order_release);
}

static int capture_error(Display *display, XErrorEvent *event)
{
    unsigned index;
    fprintf(stderr, "WMB gateway: X error=%u request=%u serial=%lu\n",
            event->error_code, event->request_code, event->serial);
    for (index = 0; index < CLIENT_LIMIT; index++)
        if (clients[index].display == display && clients[index].request) {
            clients[index].request->status = event->error_code;
            return 0;
        }
    wmb_api_fatal("unattributed gateway X error");
}

static void run_client(void *argument)
{
    Client *client = argument;
    for (;;) {
        WmbApiCall *call;
        while (!client->request) hal_task_wait();
        call = client->request;
        atomic_store_explicit(&call->entered, true, memory_order_release);
        wmb_api_execute(call);
        if (call->slot == WMB_SLOT_XOpenDisplay) {
            client->display = call->result.XOpenDisplay;
            if (client->display)
                wmb_xlib_set_error_handler(client->display, capture_error);
            else client->finished = true;
        } else if (call->slot == WMB_SLOT_XCloseDisplay) {
            client->display = NULL;
            client->finished = true;
        }
        client->request = NULL;
        client->busy = false;
        finish(call);
        if (client->finished) for (;;) hal_task_wait();
    }
}

int wmb_gateway_submit(WmbApiCall *call)
{
    WmbApiCall *head;
    unsigned count;
    uint32_t started;
    if (!call || call->slot >= WMB_API_SLOT_COUNT ||
        !atomic_load_explicit(&initialized, memory_order_acquire) ||
        platform.is_runtime_thread(platform.context)) {
        fputs("WMB gateway: invalid request, unconfigured port or runtime-thread producer\n", stderr);
        atomic_fetch_add_explicit(&rejected, 1, memory_order_relaxed);
        return -1;
    }
    if (!atomic_load_explicit(&accepting, memory_order_acquire) &&
        call->slot == WMB_SLOT_XOpenDisplay) {
        fputs("WMB gateway: display admission is stopping\n", stderr);
        atomic_fetch_add_explicit(&rejected, 1, memory_order_relaxed);
        return -1;
    }
    count = atomic_load_explicit(&admission, memory_order_relaxed);
    do {
        if (count >= ADMISSION_LIMIT) {
            atomic_fetch_add_explicit(&rejected, 1, memory_order_relaxed);
            fputs("WMB gateway: admission closed or capacity exhausted\n", stderr);
            return -1;
        }
    } while (!atomic_compare_exchange_weak_explicit(
        &admission, &count, count + 1, memory_order_acq_rel, memory_order_relaxed));
    call->status = 0;
    atomic_init(&call->done, false);
    atomic_init(&call->entered, false);
    started = platform.tick_ms(platform.context);
    head = atomic_load_explicit(&mailbox, memory_order_relaxed);
    do {
        call->next = head;
    } while (!atomic_compare_exchange_weak_explicit(
        &mailbox, &head, call, memory_order_release, memory_order_relaxed));
    atomic_fetch_add_explicit(&posted, 1, memory_order_relaxed);
    /* A timeout must not abandon stack descriptors or borrowed caller buffers. */
    while (!atomic_load_explicit(&call->done, memory_order_acquire)) {
        if (platform.tick_ms(platform.context) - started >= 6000 &&
            (call->slot != WMB_SLOT_XNextEvent ||
             !atomic_load_explicit(&call->entered, memory_order_acquire)))
            wmb_api_fatal("gateway service deadline; borrowed calls cannot be abandoned safely");
        platform.wait_ms(platform.context, 1);
    }
    atomic_fetch_sub_explicit(&admission, 1, memory_order_release);
    return call->status;
}

static int wmb_gateway_foreign_yield(uint32_t milliseconds)
{
    uint32_t started;
    if (!atomic_load_explicit(&initialized, memory_order_acquire) ||
        platform.is_runtime_thread(platform.context)) {
        fputs("WMB gateway: foreign yield requires a configured foreign caller\n", stderr);
        return -1;
    }
    if (!atomic_load_explicit(&accepting, memory_order_acquire)) return -1;
    started = platform.tick_ms(platform.context);
    if (!milliseconds) platform.wait_ms(platform.context, 0);
    while (milliseconds) {
        uint32_t elapsed;
        platform.wait_ms(platform.context, milliseconds > 10 ? 10 : milliseconds);
        if (!atomic_load_explicit(&accepting, memory_order_acquire)) return -1;
        elapsed = platform.tick_ms(platform.context) - started;
        if (elapsed >= milliseconds) break;
        milliseconds -= elapsed;
        started += elapsed;
    }
    return atomic_load_explicit(&accepting, memory_order_acquire) ? 0 : -1;
}

#include "gateway_generated.inc"

const wmb_api_t *wmb_gateway_table(void) { return &foreign_api; }

int wmb_gateway_start(const wmb_gateway_port_t *port)
{
    if (!port || !port->is_runtime_thread || !port->wait_ms || !port->tick_ms ||
        !port->is_runtime_thread(port->context) || !wmb_ready() ||
        atomic_load_explicit(&initialized, memory_order_acquire)) {
        fputs("WMB gateway: invalid port, lifecycle state or repeated initialization\n", stderr);
        return -1;
    }
    platform = *port;
    atomic_store_explicit(&accepting, true, memory_order_relaxed);
    atomic_store_explicit(&initialized, true, memory_order_release);
    return 0;
}

void wmb_gateway_poll(void)
{
    WmbApiCall *chain, *fifo = NULL;
    unsigned index;
    if (!atomic_load_explicit(&initialized, memory_order_acquire)) return;
    if (!wmb_gateway_is_owner()) wmb_api_fatal("gateway poll requires the runtime thread");
    for (index = 0; index < CLIENT_LIMIT; index++)
        if (clients[index].task && clients[index].finished) {
            if (wmb_metrics_record_task(clients[index].task) != 0) failed = true;
            hal_task_destroy(clients[index].task);
            clients[index] = (Client){0};
        }
    chain = atomic_exchange_explicit(&mailbox, NULL, memory_order_acquire);
    while (chain) {
        WmbApiCall *next = chain->next;
        chain->next = fifo;
        fifo = chain;
        chain = next;
    }
    while (fifo) {
        WmbApiCall *call = fifo;
        Client *client = NULL;
        Display *display;
        fifo = fifo->next;
        if (call->slot == WMB_SLOT_XOpenDisplay) {
            if (!atomic_load_explicit(&accepting, memory_order_acquire)) {
                fputs("WMB gateway: display admission stopped\n", stderr);
                call->status = -1;
                finish(call);
                continue;
            }
            for (index = 0; index < CLIENT_LIMIT; index++)
                if (!clients[index].task) { client = &clients[index]; break; }
            if (client)
                client->task = hal_task_create(client_names[index], run_client, client, 256 * 1024);
            if (!client || !client->task) {
                fputs("WMB gateway: display worker capacity/allocation exhausted\n", stderr);
                call->status = -1;
                finish(call);
                continue;
            }
        } else if (call_display(call, &display)) {
            for (index = 0; index < CLIENT_LIMIT; index++)
                if (display && clients[index].task &&
                    clients[index].display == display && !clients[index].finished) {
                    client = &clients[index];
                    break;
                }
            if (!client || client->busy) {
                fputs("WMB gateway: unknown or concurrently used Display\n", stderr);
                call->status = -1;
                finish(call);
                continue;
            }
        } else {
            atomic_store_explicit(&call->entered, true, memory_order_release);
            wmb_api_execute(call);
            finish(call);
            continue;
        }
        client->busy = true;
        client->request = call;
        hal_task_wake(client->task);
    }
}

void wmb_gateway_stop(void)
{
    if (atomic_load_explicit(&initialized, memory_order_acquire))
        atomic_store_explicit(&accepting, false, memory_order_release);
}

size_t wmb_gateway_count(void)
{
    size_t count = 0;
    unsigned index;
    for (index = 0; index < CLIENT_LIMIT; index++) if (clients[index].task) count++;
    return count + (atomic_load_explicit(&admission, memory_order_acquire) & ~ADMISSION_CLOSED);
}

bool wmb_gateway_try_finish(void)
{
    unsigned expected = 0;
    if (!atomic_load_explicit(&initialized, memory_order_acquire)) return true;
    if (!wmb_gateway_is_owner()) wmb_api_fatal("gateway finalization requires runtime ownership");
    if (wmb_gateway_count()) return false;
    return atomic_compare_exchange_strong_explicit(
        &admission, &expected, ADMISSION_CLOSED, memory_order_acq_rel, memory_order_acquire) ||
        expected == ADMISSION_CLOSED;
}

int wmb_gateway_result(void) { return failed ? -1 : 0; }

void wmb_gateway_stats(wmb_gateway_stats_t *result)
{
    if (!result || !wmb_gateway_is_owner()) wmb_api_fatal("invalid gateway statistics request");
    *result = (wmb_gateway_stats_t){
        .posted = atomic_load_explicit(&posted, memory_order_relaxed),
        .completed = atomic_load_explicit(&completed, memory_order_relaxed),
        .rejected = atomic_load_explicit(&rejected, memory_order_relaxed),
        .clients = wmb_gateway_count()
    };
    if (failed) fputs("WMB gateway: a task stack measurement failed\n", stderr);
}
