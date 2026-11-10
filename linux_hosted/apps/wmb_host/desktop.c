#define _POSIX_C_SOURCE 200809L
#include "desktop.h"
#include "wmb.h"
#include "wmb_app_table.h"
#include "hal_task.h"
#include "hal_time.h"
#include "jwm_port.h"
#include "fiber.h"
#include "hal_display.h"
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    fiber_t *fiber;
    const char *name;
    uint64_t last, maximum;
    unsigned runs;
} Run;
static Run runs[8];
static bool profiling;

void *__real_fiber_sched_swap(void *stack);
void *__wrap_fiber_sched_swap(void *stack)
{
    void *next = __real_fiber_sched_swap(stack);
    if (profiling) {
        const char *name = hal_task_name(hal_task_self());
        if (name && (!strcmp(name, "xsrv") || !strcmp(name, "jwm") || !strcmp(name, "demo"))) {
            unsigned index;
            uint64_t now = hal_tick_us();
            fiber_t *fiber = fiber_self();
            for (index = 0; index < 8 && runs[index].fiber && runs[index].fiber != fiber; index++) {}
            if (index < 8) {
                if (runs[index].last && now - runs[index].last > runs[index].maximum)
                    runs[index].maximum = now - runs[index].last;
                runs[index].fiber = fiber;
                runs[index].name = name;
                runs[index].last = now;
                runs[index].runs++;
            }
        }
    }
    return next;
}

static double cpu(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) {
        perror("desktop clock");
        return -1;
    }
    return value.tv_sec + value.tv_nsec / 1000000000.0;
}

static int capture(void)
{
    const char *path = getenv("WMB_WM_CAPTURE");
    const hal_framebuffer_t *framebuffer = hal_display_get_fb();
    FILE *file;
    if (!path) return 0;
    file = fopen(path, "wb");
    if (!file) { perror("WM capture"); return -1; }
    if (fprintf(file, "P6\n%u %u\n255\n", framebuffer->width, framebuffer->height) < 0)
        goto failed;
    for (uint32_t y = 0; y < framebuffer->height; y++)
        for (uint32_t x = 0; x < framebuffer->width; x++) {
            uint32_t value = 0;
            unsigned char rgb[3];
            memcpy(&value, (const unsigned char *)framebuffer->pixels +
                (size_t)y * framebuffer->stride + x * (framebuffer->bpp / 8),
                framebuffer->bpp / 8);
            rgb[0] = (unsigned char)(framebuffer->bpp == 16 ?
                ((value >> 11) & 31) * 255 / 31 : (value >> 16) & 255);
            rgb[1] = (unsigned char)(framebuffer->bpp == 16 ?
                ((value >> 5) & 63) * 255 / 63 : (value >> 8) & 255);
            rgb[2] = (unsigned char)(framebuffer->bpp == 16 ?
                (value & 31) * 255 / 31 : value & 255);
            if (fwrite(rgb, 1, 3, file) != 3) goto failed;
        }
    if (fclose(file) != 0) { perror("WM capture close"); return -1; }
    return 0;
failed:
    perror("WM capture write");
    (void)fclose(file);
    return -1;
}

int wmb_host_desktop(bool test, bool instrumented, uint32_t idle_ms, unsigned cancellation)
{
    struct timespec delay = {0, 1000000};
    const wmb_app_t *app;
    uint64_t started;
    uint64_t idle_started = 0;
    double idle_cpu = 0;
    hal_task_t *verification = NULL;
    WmbDesktopTest result = {0};
    bool passed = !test;
    int status;
    if (hal_task_runtime_init(0) != 0) return 1;
    result.cancellation = cancellation;
    started = hal_tick_us();
    if (wmb_main(NULL) != 0) return 1;
    printf("P7: JWM ready in %.2f ms\n", (hal_tick_us() - started) / 1000.0);
    if (test && !instrumented && hal_tick_us() - started > 200000) {
        fputs("P7: startup exceeded 200 ms\n", stderr);
        (void)wmb_shutdown();
        return 1;
    }
    app = wmb_app_lookup("demo");
    if (!app) return 1;
    for (unsigned index = 0; index < 2; index++) {
        char *arguments[] = {"demo"};
        hal_task_t *task = wmb_app_launch(app, 1, arguments);
        if (!task) return 1;
        hal_task_wake(task);
    }
    if (test) {
        verification = hal_task_create("wm-verify", wmb_wm_test, &result, 256 * 1024);
        if (!verification) {
            fputs("P7: verification task allocation failed\n", stderr);
            (void)wmb_shutdown();
            return 1;
        }
        hal_task_wake(verification);
    }
    while ((status = wmb_poll()) == 0) {
        hal_task_yield();
        if (test && result.idle && cancellation) {
            passed = true;
            result.release = true;
            fiber_notify();
        }
        if (test && result.idle && !cancellation && !idle_started) {
            idle_started = hal_tick_us();
            idle_cpu = cpu();
            WmbJwm_ResetMetrics();
            memset(runs, 0, sizeof(runs));
            profiling = true;
        }
        if (test && idle_started && !result.release &&
            hal_tick_us() - idle_started >= (uint64_t)idle_ms * 1000) {
            const WmbJwmState *manager = WmbJwm_Status();
            double elapsed = (hal_tick_us() - idle_started) / 1000000.0;
            double percent = (cpu() - idle_cpu) * 100 / elapsed;
            uint64_t maximum = 0;
            unsigned count = 0;
            profiling = false;
            for (unsigned index = 0; index < 8 && runs[index].fiber; index++) {
                printf("P7 schedule: %s runs=%u max-gap=%.2f ms\n", runs[index].name,
                       runs[index].runs, runs[index].maximum / 1000.0);
                if (runs[index].maximum > maximum) maximum = runs[index].maximum;
                count++;
            }
            passed = idle_cpu >= 0 && percent >= 0 && (instrumented || percent < 5) &&
                count >= 4 && (instrumented || maximum <= 50000) &&
                manager->callbacks >= 60 && manager->max_callback_gap_us <= 160000 &&
                manager->clock_updates >= 8 &&
                (instrumented || (manager->max_clock_gap_us <= 1010000 &&
                          manager->min_clock_gap_us >= 990000));
            printf("P7 idle: CPU=%.2f%% callbacks=%llu callback-gap=%.2f ms clock-steps=%llu clock-gap=%.2f..%.2f ms %s\n",
                percent, (unsigned long long)manager->callbacks,
                manager->max_callback_gap_us / 1000.0,
                (unsigned long long)manager->clock_updates,
                manager->min_clock_gap_us / 1000.0,
                manager->max_clock_gap_us / 1000.0, passed ? "PASS" : "FAIL");
            if (capture() != 0) passed = false;
            result.release = true;
            fiber_notify();
        }
        if (test && result.done) {
            passed = passed && result.result == 0;
            break;
        }
        if (test && hal_tick_us() - started > (uint64_t)(idle_ms + 15000) * 1000) {
            fputs("P7: verification deadline exceeded\n", stderr);
            passed = false;
            break;
        }
        while (nanosleep(&delay, NULL) != 0) {
            if (errno != EINTR) {
                perror("desktop idle");
                return 1;
            }
        }
    }
    profiling = false;
    if (status < 0 || wmb_shutdown() != 0) passed = false;
    if (test && WmbJwm_Status()->protocol_errors != 0) passed = false;
    if (verification && result.done) hal_task_destroy(verification);
    printf("P7 desktop: orderly stop %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
