#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "wmb.h"
#include "wmb_gateway.h"
#include "wmb_metrics.h"
#include "wmb_app_table.h"
#include "hal_time.h"
#include "capture.h"
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const wmb_app_t wmb_app_table[] = {{NULL, NULL, 0}};
const size_t wmb_app_table_size = 0;
static pthread_t owner;
static bool is_owner(void *unused) { (void)unused; return pthread_equal(owner, pthread_self()) != 0; }
static uint32_t tick(void *unused) { (void)unused; return hal_tick_ms(); }
static void wait_ms(void *unused, uint32_t milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    (void)unused;
    if (!milliseconds) { sched_yield(); return; }
    while (nanosleep(&delay, &delay) != 0)
        if (errno != EINTR) { perror("RPC runtime wait"); abort(); }
}
int main(int argc, char **argv)
{
    uint32_t version = 2;
    const char *capture = NULL;
    int result = 0;
    uint32_t started;
    wmb_gateway_port_t port = {NULL, is_owner, wait_ms, tick};
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s SOCKET [1|2] [CAPTURE.ppm]\n", argv[0]);
        return 2;
    }
    if (argc >= 3) {
        if (strcmp(argv[2], "1") == 0) version = 1;
        else if (strcmp(argv[2], "2") != 0) return 2;
    }
    if (argc == 4) capture = argv[3];
    owner = pthread_self();
    if (hal_task_runtime_init(0) || wmb_main(NULL) || wmb_gateway_start(&port)) return 1;
    if (wmb_rpc_server_start(argv[1], version)) { (void)wmb_shutdown(); return 1; }
    printf("P8 RPC READY: socket=%s ABI=%u\n", argv[1], version); fflush(stdout);
    started = hal_tick_ms();
    while (!wmb_rpc_server_stop_requested() && wmb_ready()) {
        if (wmb_poll() != 0) { result = 1; break; }
        hal_task_yield();
        if (wmb_rpc_server_take_capture()) {
            if (!capture || wmb_host_capture(capture) != 0) result = 1;
            else { puts("P8 RPC CAPTURE READY"); fflush(stdout); }
        }
        if (hal_tick_ms() - started > 120000) {
            fputs("P8 RPC server: bounded harness deadline exceeded\n", stderr);
            result = 1; break;
        }
        wait_ms(NULL, 1);
    }
    wmb_rpc_server_begin_shutdown();
    if (wmb_shutdown() || wmb_rpc_server_join()) result = 1;
    wmb_metrics_print(stdout);
    wmb_gateway_stats_t stats;
    wmb_gateway_stats(&stats);
    if (stats.posted != stats.completed || stats.clients) result = 1;
    printf("P8 RPC gateway: posted=%u completed=%u rejected=%u live=%zu\n",
           stats.posted, stats.completed, stats.rejected, stats.clients);
    printf("P8 RPC server: ordered shutdown %s\n", result ? "FAIL" : "PASS");
    return result;
}
