#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "kdrive_hal.h"
#include "hal_task.h"
#include "fiber.h"
#include "hal_time.h"
#include "hal_display.h"
#include "wmb_test_client.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned int flushes;
extern int wmb_host_test_key(void);
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

static uint32_t pixel(const hal_framebuffer_t *fb, uint32_t x, uint32_t y)
{
    uint32_t value = 0;
    memcpy(&value, (const unsigned char *)fb->pixels +
           (size_t)y * fb->stride + x * (fb->bpp / 8), fb->bpp / 8);
    return value & (fb->bpp == 16 ? 0xffffu : 0xffffffu);
}

static bool client_pixels_valid(void)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    uint32_t x, y, black = 0;
    if (!fb || fb->width < 260 || fb->height < 130) return false;
    if (pixel(fb, 25, 25) != (fb->bpp == 16 ? 0xffffu : 0xffffffu) ||
        pixel(fb, 80, 40) != 0) return false;
    for (y = 82; y < 96; y++)
        for (x = 30; x < 97; x++)
            if (pixel(fb, x, y) == 0) black++;
    return black >= 10;
}

int main(int argc, char **argv)
{
    bool self_test = argc == 2 && strcmp(argv[1], "--self-test") == 0;
    bool raw_test = argc == 2 && strcmp(argv[1], "--raw-test") == 0;
    bool client_soak = argc == 2 && strcmp(argv[1], "--client-soak") == 0;
    bool client_test = (argc == 2 && strcmp(argv[1], "--client-test") == 0) || client_soak;
    bool flow_test = argc == 2 && strcmp(argv[1], "--flow-test") == 0;
    bool timeout_test = argc == 2 && strcmp(argv[1], "--timeout-test") == 0;
    bool wire_test = argc == 3 && strcmp(argv[1], "--wire-test") == 0;
    bool key_injected = false;
    uint32_t visible_at = 0;
    double client_cpu = 0;
    unsigned long client_polls = 0;
    bool client_idle_checked = false, client_idle_pass = false;
    char *server_args[] = {"wmb_host", "-dpi", "96", "-noreset", NULL};
    wmb_tinyx_server_t server = {4, server_args, false, false, 0};
    hal_task_t *task;
    hal_task_t *client_task = NULL;
    WmbTestResult client_result = {0};
    client_result.hold_ms = client_soak ? 60000u : 10000u;
    if (wire_test) client_result.capture_path = argv[2];
    uint32_t started, ready_at = 0;
    unsigned long ready_polls = 0;
    double ready_cpu = 0;
    bool validated = false, ready_observed = false, failed = false;
    if (argc != 1 && !self_test && !raw_test && !client_test && !flow_test && !timeout_test && !wire_test) {
        fprintf(stderr, "usage: %s [--self-test | --raw-test | --client-test | --client-soak | --flow-test | --timeout-test | --wire-test FILE]\n", argv[0]);
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
        if ((raw_test || client_test || flow_test || timeout_test || wire_test) && server.ready && client_task == NULL) {
            client_task = hal_task_create("client",
                raw_test ? wmb_raw_test_client :
                flow_test ? wmb_flow_test_client :
                timeout_test ? wmb_timeout_test_client :
                wire_test ? wmb_wire_test_client : wmb_xlib_test_client,
                &client_result, 256 * 1024);
            if (!client_task) {
                fputs("WMB: client allocation failed\n", stderr);
                failed = true;
                WmbTinyXServerStop();
            } else hal_task_wake(client_task);
        }
        if (client_test && client_result.awaiting_key && !key_injected) {
            if (wmb_host_test_key() != 0) {
                fputs("WMB: backend key injection failed\n", stderr);
                failed = true;
                WmbTinyXServerStop();
            }
            key_injected = true;
        }
        if (client_test && client_result.visible) {
            if (!visible_at) {
                visible_at = hal_tick_ms();
                client_cpu = cpu_seconds();
                client_polls = KdHalPollCount();
            }
            if (!client_idle_checked && hal_tick_ms() - visible_at >= client_result.hold_ms) {
                double elapsed = hal_tick_ms() - visible_at;
                double cpu = (cpu_seconds() - client_cpu) * 100000 / elapsed;
                double polls = (KdHalPollCount() - client_polls) * 1000 / elapsed;
                bool pixels = client_pixels_valid();
                client_idle_pass = pixels && cpu >= 0 && (client_soak || cpu < 5) && polls >= 30;
                printf("P5 idle: pixels=%s CPU=%.2f%% poll-Hz=%.1f %s\n",
                    pixels ? "valid" : "invalid", cpu, polls, client_idle_pass ? "PASS" : "FAIL");
                client_idle_checked = true;
                client_result.release = true;
                fiber_notify();
            }
        }
        if ((raw_test || client_test || flow_test || wire_test) && client_result.done) {
            validated = client_result.result == 0 &&
                (!client_test || (client_result.keyboard_received && client_result.mouse_received &&
                                  client_idle_pass));
            WmbTinyXServerStop();
            raw_test = client_test = flow_test = wire_test = false;
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
    if (client_task) hal_task_destroy(client_task);
    hal_task_yield();
    if (failed || (argc > 1 && !validated)) return 1;
    return server.result;
}
