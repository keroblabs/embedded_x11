#define _POSIX_C_SOURCE 200809L
#include "api_calls.h"
#include "wmb_gateway.h"
#include "wmb.h"
#include "wmb_app_table.h"
#include "hal_time.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

const wmb_app_t wmb_app_table[] = {{NULL, NULL, 0}};
const size_t wmb_app_table_size = 0;
static pthread_t owner;
static atomic_bool waiter_done, reject_done;
static atomic_bool long_yield_started, long_yield_done;
static _Thread_local bool long_yielder;
static bool native_yield_started, native_yield_done;
static _Atomic(Display *) shared;
static int waiter_result = -1, reject_result = -1;
static int long_yield_result = -1, native_yield_result = -1;
static bool native_misuse;
static bool is_owner(void *unused) { (void)unused; return pthread_equal(owner, pthread_self()) != 0; }
static uint32_t tick(void *unused) { (void)unused; return hal_tick_ms(); }
static void wait_ms(void *unused, uint32_t milliseconds)
{
    struct timespec value = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    (void)unused;
    if (long_yielder) atomic_store_explicit(&long_yield_started, true, memory_order_release);
    while (nanosleep(&value, &value) != 0)
        if (errno != EINTR) { perror("gateway test wait"); abort(); }
}
static void *long_yield(void *unused)
{
    (void)unused;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) abort();
    long_yielder = true;
    long_yield_result = wmb_gateway_table()->wmbYield(UINT32_MAX) == -1 ? 0 : -1;
    atomic_store_explicit(&long_yield_done, true, memory_order_release);
    return NULL;
}
static void native_yield(void *unused)
{
    (void)unused;
    native_yield_started = true;
    native_yield_result = wmb_api_native_table()->wmbYield(UINT32_MAX) == -1 ? 0 : -1;
    native_yield_done = true;
    for (;;) hal_task_wait();
}
static void *waiter(void *unused)
{
    const wmb_api_t *api = native_misuse ? wmb_api_native_table() : wmb_gateway_table();
    Display *display;
    XEvent event;
    (void)unused;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) abort();
    display = api->XOpenDisplay(NULL);
    if (display) {
        atomic_store_explicit(&shared, display, memory_order_release);
        waiter_result = api->XNextEvent(display, &event) == -1 ? 0 : -1;
        api->XCloseDisplay(display);
    }
    atomic_store_explicit(&waiter_done, true, memory_order_release);
    return NULL;
}
static void *rejecter(void *unused)
{
    WmbApiCall call = {.slot = WMB_SLOT_XSync};
    WmbApiCall null_screen = {.slot = WMB_SLOT_XDisplayOfScreen};
    (void)unused;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) abort();
    if (wmb_gateway_submit(&call) >= 0 || wmb_gateway_submit(&null_screen) >= 0) {
        fputs("P8 gateway test: NULL Display/Screen was not rejected\n", stderr);
        atomic_store_explicit(&reject_done, true, memory_order_release);
        return NULL;
    }
    call.args.XSync.p0 = atomic_load_explicit(&shared, memory_order_acquire);
    call.args.XSync.p1 = False;
    reject_result = wmb_gateway_submit(&call) < 0 ? 0 : -1;
    atomic_store_explicit(&reject_done, true, memory_order_release);
    return NULL;
}
int main(int argc, char **argv)
{
    pthread_t first, second, third;
    hal_task_t *yield_task;
    bool second_started = false;
    uint32_t started;
    int result = 0;
    wmb_gateway_port_t port = {NULL, is_owner, wait_ms, tick};
    wmb_gateway_stats_t stats;
    native_misuse = argc == 2 && strcmp(argv[1], "--wrong-native-thread") == 0;
    if (argc != 1 && !native_misuse) return 2;
    owner = pthread_self();
    if (hal_task_runtime_init(0) || wmb_main(NULL) || wmb_gateway_start(&port)) return 1;
    yield_task = hal_task_create("native-long-yield", native_yield, NULL, 64 * 1024);
    if (!yield_task) return 1;
    hal_task_wake(yield_task);
    if (pthread_create(&first, NULL, waiter, NULL) != 0) return 1;
    if (pthread_create(&third, NULL, long_yield, NULL) != 0) return 1;
    started = hal_tick_ms();
    while (!atomic_load_explicit(&waiter_done, memory_order_acquire) ||
           !atomic_load_explicit(&long_yield_done, memory_order_acquire) || !native_yield_done) {
        if (wmb_poll() != 0) { result = 1; break; }
        hal_task_yield();
        wmb_gateway_stats(&stats);
        if (!second_started && stats.posted >= 2 && stats.completed == 1) {
            if (pthread_create(&second, NULL, rejecter, NULL) != 0) return 1;
            second_started = true;
        }
        if (atomic_load_explicit(&reject_done, memory_order_acquire) &&
            atomic_load_explicit(&long_yield_started, memory_order_acquire) && native_yield_started)
            wmb_gateway_stop();
        if (hal_tick_ms() - started > 3000) {
            fputs("P8 gateway test: cancellation deadline exceeded\n", stderr);
            abort();
        }
        wait_ms(NULL, 1);
    }
    if (pthread_join(first, NULL) != 0 ||
        !second_started || pthread_join(second, NULL) != 0 ||
        pthread_join(third, NULL) != 0) result = 1;
    hal_task_destroy(yield_task);
    if (wmb_shutdown() != 0) result = 1;
    wmb_gateway_stats(&stats);
    if (waiter_result || reject_result || long_yield_result || native_yield_result ||
        stats.rejected != 3 ||
        stats.posted != stats.completed || stats.clients) result = 1;
    printf("P8 gateway: NULL/busy objects diagnosed, event/long-yield waits cancelled, all borrowed calls drained %s\n",
           result ? "FAIL" : "PASS");
    return result;
}
