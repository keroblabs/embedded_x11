#include "wmb.h"
#include "wmb_apps.h"
#include "jwm_port.h"
#include "server.h"
#include "kdrive_hal.h"
#include "hal_task.h"
#include "hal_time.h"
#include "fiber.h"
#include "wmb_gateway.h"
#include "wmb_metrics.h"
#include <stdio.h>

static char *server_arguments[] = {"WMB", "-dpi", "96", "-noreset", NULL};
static wmb_tinyx_server_t server = {4, server_arguments, false, false, 0};
static WmbJwmState manager;
static hal_task_t *server_task, *manager_task;
static bool initialized, stopping, applications_stopping, finished, failed;

static void stop_requested(hal_display_status_t status, void *unused)
{
    (void)unused;
    if (status == HAL_DISPLAY_ERROR) failed = true;
    stopping = true;
    fiber_notify();
}

static void server_entry(void *argument)
{
    TinyX_ServerFiber(argument);
    for (;;) hal_task_wait();
}

bool wmb_ready(void)
{
    return manager.ready && !manager.finished && !server.finished && !stopping && !finished;
}
bool wmb_stop_requested(void) { return applications_stopping; }

int wmb_main(const wmb_config_t *configuration)
{
    uint32_t started;
    if (initialized || !hal_task_self()) {
        fputs("WMB: initialize the runtime and call wmb_main only once\n", stderr);
        return -1;
    }
    initialized = true;
    manager.configuration = configuration ? configuration->jwm_configuration : NULL;
    server_task = hal_task_create("xsrv", server_entry, &server, 512 * 1024);
    manager_task = hal_task_create("jwm", WmbJwm_Run, &manager, 512 * 1024);
    if (!server_task || !manager_task) {
        fputs("WMB: component task allocation failed\n", stderr);
        if (server_task) hal_task_destroy(server_task);
        if (manager_task) hal_task_destroy(manager_task);
        finished = failed = true;
        return -1;
    }
    KdHalSetStopHandler(stop_requested, NULL);
    started = hal_tick_ms();
    hal_task_wake(server_task);
    while (!server.ready && !server.finished && hal_tick_ms() - started < 2000)
        hal_task_yield();
    if (server.ready && !server.finished) {
        hal_task_wake(manager_task);
        while (!manager.ready && !manager.finished && !server.finished &&
               hal_tick_ms() - started < 2000)
            hal_task_yield();
    }
    if (!manager.ready || manager.finished || server.finished) {
        fputs("WMB: server/window-manager startup failed or timed out\n", stderr);
        failed = stopping = true;
        if (!manager.ready && !manager.finished) {
            hal_task_destroy(manager_task);
            manager_task = NULL;
            manager.finished = true;
        }
        (void)wmb_shutdown();
        return -1;
    }
    fiber_notify();
    return 0;
}

int wmb_poll(void)
{
    if (!initialized) {
        fputs("WMB: poll before initialization\n", stderr);
        return -1;
    }
    if (finished) return failed ? -1 : 1;
    wmb_metrics_dispatch_tick();
    wmb_gateway_poll();
    wmb_apps_reap();
    if (manager.finished || server.finished) stopping = true;
    if (stopping) {
        if (!manager.finished) WmbJwm_RequestStop();
        else {
            applications_stopping = true;
            wmb_gateway_stop();
            wmb_apps_stop();
            if (!wmb_apps_count() && !wmb_gateway_count() && !server.finished) WmbTinyXServerStop();
        }
    }
    if (server.finished && manager.finished && !wmb_apps_count() && wmb_gateway_try_finish()) {
        failed = failed || server.result != 0 || manager.result != 0 ||
            wmb_apps_result() != 0 || wmb_gateway_result() != 0;
        if (manager_task) {
            if (wmb_metrics_record_task(manager_task) != 0) failed = true;
            hal_task_destroy(manager_task);
        }
        if (wmb_metrics_record_task(server_task) != 0) failed = true;
        hal_task_destroy(server_task);
        KdHalSetStopHandler(NULL, NULL);
        finished = true;
        return failed ? -1 : 1;
    }
    return 0;
}

int wmb_shutdown(void)
{
    uint32_t started = hal_tick_ms();
    int result;
    if (!initialized) {
        fputs("WMB: shutdown before initialization\n", stderr);
        return -1;
    }
    stopping = true;
    fiber_notify();
    while ((result = wmb_poll()) == 0) {
        if (hal_tick_ms() - started >= 5000) {
            fputs("WMB: cooperative application shutdown timed out\n", stderr);
            return -1;
        }
        hal_task_yield();
    }
    return result < 0 ? -1 : 0;
}
