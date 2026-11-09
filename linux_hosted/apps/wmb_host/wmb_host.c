#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "kdrive_hal.h"
#include "hal_task.h"
#include "hal_time.h"
#include "hal_display.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned int flushes;
void __real_hal_display_flush(int32_t x, int32_t y, uint32_t w, uint32_t h);
void __wrap_hal_display_flush(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    flushes++;
    __real_hal_display_flush(x, y, w, h);
}

static double cpu_seconds(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time) != 0) {
        perror("clock_gettime");
        return -1;
    }
    return time.tv_sec + time.tv_nsec / 1000000000.0;
}

static int idle(void)
{
    struct timespec delay = {0, 1000000};
    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            return -1;
        }
    }
    return 0;
}

static bool root_pixels_valid(void)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    uint32_t x, y, first = 0;
    if (fb == NULL) return false;
    memcpy(&first, fb->pixels, fb->bpp / 8);
    for (y = 0; y < fb->height; y++)
        for (x = 0; x < fb->width; x++) {
            uint32_t pixel = 0;
            memcpy(&pixel, (const unsigned char *)fb->pixels +
                   y * fb->stride + x * (fb->bpp / 8), fb->bpp / 8);
            if (pixel != first) return true;
        }
    return false;
}

int main(int argc, char **argv)
{
    bool self_test = argc == 2 && strcmp(argv[1], "--self-test") == 0;
    char *server_args[] = {"wmb_host", "-dpi", "96", NULL};
    wmb_tinyx_server_t server = {3, server_args, false, false, 0};
    hal_task_t *task;
    uint32_t started, ready_at = 0;
    unsigned long ready_polls = 0;
    double ready_cpu = 0;
    bool validated = false, ready_observed = false, failed = false;
    if (argc != 1 && !self_test) {
        fprintf(stderr, "usage: %s [--self-test]\n", argv[0]);
        return 2;
    }
    if (hal_task_runtime_init(0) != 0) {
        fputs("WMB: task runtime initialization failed\n", stderr);
        return 1;
    }
    task = hal_task_create("xsrv", TinyX_ServerFiber, &server, 512 * 1024);
    if (task == NULL) {
        fputs("WMB: server task allocation failed\n", stderr);
        return 1;
    }
    started = hal_tick_ms();
    hal_task_wake(task);
    while (!server.finished) {
        hal_task_yield();
        if (server.ready && !ready_observed) {
            ready_observed = true;
            ready_at = hal_tick_ms();
            ready_cpu = cpu_seconds();
            ready_polls = KdHalPollCount();
            if (ready_cpu < 0) {
                failed = true;
                WmbTinyXServerStop();
            }
        }
        if (self_test && !failed && ready_observed && hal_tick_ms() - ready_at >= 10000) {
            bool root_valid = WmbTinyXServerRootValid() && root_pixels_valid();
            double percent = (cpu_seconds() - ready_cpu) * 100000 /
                             (hal_tick_ms() - ready_at);
            double poll_hz = (KdHalPollCount() - ready_polls) * 1000.0 /
                             (hal_tick_ms() - ready_at);
            validated = root_valid &&
                        flushes > 1 && percent >= 0 && percent < 5.0 && poll_hz >= 30;
            printf("P4: root=%s Damage/HAL-flushes=%u idle-CPU=%.2f%% poll-Hz=%.1f %s\n",
                root_valid ? "valid" : "invalid", flushes, percent,
                poll_hz,
                validated ? "PASS" : "FAIL");
            WmbTinyXServerStop();
            self_test = false;
        }
        if (hal_tick_ms() - started > 10000 && !server.ready) {
            fputs("WMB: server startup deadline exceeded\n", stderr);
            failed = true;
            WmbTinyXServerStop();
        }
        if (!failed && idle() != 0) {
            failed = true;
            WmbTinyXServerStop();
        }
    }
    hal_task_destroy(task);
    hal_task_yield();
    if (failed || (argc == 2 && !validated)) return 1;
    return server.result;
}
