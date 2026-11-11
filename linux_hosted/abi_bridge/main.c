#define _POSIX_C_SOURCE 200809L
#include "wmb.h"
#include "wmb_gateway.h"
#include "wmb_metrics.h"
#include "wmb_app_table.h"
#include "wmb_apps.h"
#include "hal_task.h"
#include "hal_time.h"
#include "hal_display.h"
#include "allocation.h"
#include "jwm_port.h"
#include "sdl_backend.h"
#include "probe.h"
#include "tenant.h"
#include <SDL.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const wmb_app_t wmb_app_table[] = {
    {"abi-v1", wmb_probe_v1_entry, 256 * 1024},
    {"abi-v2", wmb_probe_v2_entry, 256 * 1024}
};
const size_t wmb_app_table_size = sizeof(wmb_app_table) / sizeof(wmb_app_table[0]);
static pthread_t runtime_thread;
static bool is_owner(void *unused)
{
    (void)unused;
    return pthread_equal(runtime_thread, pthread_self()) != 0;
}
static void foreign_wait(void *unused, uint32_t milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    (void)unused;
    if (!milliseconds) { sched_yield(); return; }
    while (nanosleep(&delay, &delay) != 0)
        if (errno != EINTR) { perror("P8 foreign wait"); abort(); }
}
static uint32_t foreign_tick(void *unused) { (void)unused; return hal_tick_ms(); }

static int inject(const WmbAbiProbe *probe, unsigned step)
{
    static int previous_x, previous_y;
    static bool positioned;
    SDL_Event event = {0};
    int x = probe->x + (int)(step & 1), y = probe->y + (int)((step >> 1) & 1);
    if (!positioned) {
        previous_x = probe->x; previous_y = probe->y; positioned = true;
    }
    event.type = SDL_MOUSEMOTION;
    event.motion.windowID = sdl_backend_display_window();
    event.motion.x = x; event.motion.y = y;
    event.motion.xrel = x - previous_x; event.motion.yrel = y - previous_y;
    previous_x = x; previous_y = y;
    if (SDL_PushEvent(&event) != 1) goto failed;
    event = (SDL_Event){0};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = sdl_backend_display_window();
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state = SDL_PRESSED;
    event.button.x = x; event.button.y = y;
    if (SDL_PushEvent(&event) != 1) goto failed;
    event.type = SDL_MOUSEBUTTONUP; event.button.state = SDL_RELEASED;
    if (SDL_PushEvent(&event) != 1) goto failed;
    event = (SDL_Event){0};
    event.type = SDL_KEYDOWN;
    event.key.windowID = sdl_backend_display_window();
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = SDLK_a; event.key.keysym.scancode = SDL_SCANCODE_A;
    if (SDL_PushEvent(&event) != 1) goto failed;
    event.type = SDL_KEYUP; event.key.state = SDL_RELEASED;
    if (SDL_PushEvent(&event) != 1) goto failed;
    return 0;
failed:
    fprintf(stderr, "P8 SDL input injection failed: %s\n", SDL_GetError());
    return -1;
}

static bool pixels(const WmbAbiProbe *probe)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    uint32_t pixel = 0, mask;
    int x = probe->x + 96, y = probe->y + 54;
    if (!fb || x < 0 || y < 0 ||
        (uint32_t)x >= fb->width || (uint32_t)y >= fb->height) return false;
    memcpy(&pixel, (const unsigned char *)fb->pixels +
           (size_t)y * fb->stride + (size_t)x * (fb->bpp / 8), fb->bpp / 8);
    mask = fb->bpp == 16 ? 0xffffu : 0xffffffu;
    return (pixel & mask) == (probe->green & mask);
}

int main(int argc, char **argv)
{
    uint32_t seconds = 5;
    bool cycles = false, stable = false, sampling = false, instrumented = false;
    bool threads_started = false, threads_joined = false;
    bool baseline_set = false, failed = false;
    uint32_t cycle_target = 10000;
    WmbAbiProbe v1 = {0}, v2 = {0};
    WmbTestTenant tenant = {0};
    wmb_gateway_port_t port = {NULL, is_owner, foreign_wait, foreign_tick};
    allocation_stats_t baseline = {0}, heap;
    uint64_t started, measuring = 0, last_input = 0, last_sample = 0, last_report = 0;
    uint64_t settling = 0, cycles_finished = 0;
    allocation_stats_t settled = {.current_bytes = SIZE_MAX};
    unsigned injections = 0;
    uint32_t reported_cycles = 0;
    uint32_t previous_v1 = 0, previous_v2 = 0;
    uint64_t previous_callbacks = 0;
    if (argc == 2 && strcmp(argv[1], "--cycles") == 0) cycles = true;
    else if (argc == 2 && strcmp(argv[1], "--cycle-smoke") == 0) {
        cycles = true; cycle_target = 100;
    }
    else if (argc == 2 && strcmp(argv[1], "--sampling-test") == 0) sampling = true;
    else if (argc == 3 && (strcmp(argv[1], "--stability") == 0 ||
                          strcmp(argv[1], "--memory-test") == 0)) {
        char *end;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end || !value || value > 86400) {
            fputs("P8: stability seconds must be 1..86400\n", stderr);
            return 2;
        }
        seconds = (uint32_t)value;
        instrumented = strcmp(argv[1], "--memory-test") == 0;
        stable = !instrumented;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--cycles | --cycle-smoke | --sampling-test | --stability SECONDS | --memory-test SECONDS]\n", argv[0]);
        return 2;
    }
    runtime_thread = pthread_self();
    if (instrumented) wmb_metrics_stack_sampling(false);
    if (hal_task_runtime_init(0) != 0 || wmb_main(NULL) != 0 ||
        wmb_gateway_start(&port) != 0) return 1;
    v1.table = v2.table = wmb_api_native_table();
    wmb_probe_v1_prepare(&v1); wmb_probe_v2_prepare(&v2);
    if (cycles && wmb_tenant_start(&tenant, &v2.cycles_completed) != 0) return 1;
    for (unsigned index = 0; index < 2; index++) {
        hal_task_t *task = wmb_app_launch(&wmb_app_table[index], 0, NULL);
        if (!task) { failed = true; break; }
        hal_task_wake(task);
    }
    started = hal_tick_us();
    while (!failed) {
        uint64_t now = hal_tick_us();
        v1.now_ms = v2.now_ms = (uint32_t)(now / 1000);
        if (wmb_poll() != 0 || (v1.done && v1.result) || (v2.done && v2.result)) {
            failed = true; break;
        }
        hal_task_yield();
        now = hal_tick_us();
        v1.now_ms = v2.now_ms = (uint32_t)(now / 1000);
        if (!v1.ready || !v2.ready) {
            if (now - started > 10000000) {
                fputs("P8: native SDK startup deadline exceeded\n", stderr);
                failed = true;
            }
            continue;
        }
        if (!threads_started) {
            threads_started = true;
            if (wmb_threads_start(wmb_gateway_table()) != 0) failed = true;
        }
        if (threads_started && !threads_joined && wmb_threads_done()) {
            threads_joined = true;
            if (wmb_threads_join() != 0) failed = true;
        }
        if (!measuring && threads_joined && !wmb_gateway_count()) {
            measuring = now;
            v2.arm_input = !cycles;
            v2.cycles_requested = cycles ? 32 : 0;
            printf("P8: native SDKs ready; wall-clock measurement starts, duration=%u s\n", seconds);
            fflush(stdout);
        }
        if (cycles && measuring && !baseline_set && v2.cycles_completed == 32) {
            if (!settling) settling = now;
            if (allocation_get_stats(WMB_ALLOCATION_OWNER, &heap) != 0) failed = true;
            if (heap.current_bytes < settled.current_bytes) settled = heap;
        }
        if (measuring && !baseline_set && (cycles || v2.input_armed) &&
            (!cycles || (settling && now - settling >= 1000000)) && now - measuring > 2000000) {
            baseline_set = true;
            if (allocation_get_stats(WMB_ALLOCATION_OWNER, &baseline) != 0) failed = true;
            if (cycles) {
                baseline.current_bytes = settled.current_bytes;
                baseline.payload_bytes = settled.payload_bytes;
                baseline.live_blocks = settled.live_blocks;
                v2.cycles_requested = 32 + cycle_target;
                settled.current_bytes = SIZE_MAX;
            }
            printf("P8 baseline: current=%zu peak=%zu blocks=%zu\n",
                   baseline.current_bytes, baseline.peak_bytes, baseline.live_blocks);
            fflush(stdout);
            measuring = now;
        }
        if (cycles && baseline_set && v2.cycles_completed == 32 + cycle_target) {
            if (!cycles_finished) cycles_finished = now;
            if (allocation_get_stats(WMB_ALLOCATION_OWNER, &heap) != 0) failed = true;
            if (heap.current_bytes < settled.current_bytes) settled = heap;
        }
        if (measuring && !cycles && v2.input_armed &&
            now - last_input >= (instrumented ? 100000u : 20000u)) {
            if (inject(&v2, injections++) != 0) failed = true;
            last_input = now;
        }
        if (baseline_set && sampling) {
            uint64_t before = hal_tick_us(), updates = WmbJwm_Status()->clock_updates;
            while (WmbJwm_Status()->clock_updates == updates && hal_tick_us() - before < 2000000) {
                hal_task_yield();
                if (wmb_poll() != 0) { failed = true; break; }
                foreign_wait(NULL, 1);
            }
            uint64_t fresh = hal_tick_us(), drawn = WmbJwm_Status()->last_clock_us;
            bool passed = before < drawn && drawn <= fresh && before - drawn > 2000000 &&
                          fresh - drawn < 2000000;
            if (!passed) failed = true;
            printf("P8 sampling: real WM draw occurred after pre-yield sample; fresh sample valid %s\n",
                   passed ? "PASS" : "FAIL");
            break;
        }
        if (baseline_set && now - last_sample >= 1000000) {
            if (allocation_get_stats(WMB_ALLOCATION_OWNER, &heap) != 0) failed = true;
            if (!cycles && !instrumented && heap.peak_bytes > baseline.peak_bytes) {
                fprintf(stderr, "P8: WMB peak grew from %zu to %zu\n", baseline.peak_bytes, heap.peak_bytes);
                failed = true;
            }
            if ((!instrumented && wmb_metrics_max_dispatch_gap() > 250) ||
                WmbJwm_Status()->protocol_errors) {
                fprintf(stderr, "P8: root dispatch gap=%zu ms, WM errors=%u\n",
                        wmb_metrics_max_dispatch_gap(), WmbJwm_Status()->protocol_errors);
                failed = true;
            }
            if (!cycles && !instrumented && WmbJwm_Status()->clock_updates &&
                now - WmbJwm_Status()->last_clock_us > 2000000) {
                fprintf(stderr, "P8: completed WM clock drawing stalled: now=%llu last=%llu\n",
                        (unsigned long long)now,
                        (unsigned long long)WmbJwm_Status()->last_clock_us);
                failed = true;
            }
            last_sample = now;
            if (cycles && v2.cycles_completed >= reported_cycles + 250) {
                printf("P8 cycle progress: completed=%u requested=%u elapsed-ms=%llu\n",
                       v2.cycles_completed, v2.cycles_requested,
                       (unsigned long long)((now - measuring) / 1000));
                fflush(stdout);
                reported_cycles = v2.cycles_completed;
            }
            if (cycles && now - measuring > UINT64_C(3300000000)) {
                fprintf(stderr, "P8 cycle workload deadline: completed=%u requested=%u\n",
                        v2.cycles_completed, v2.cycles_requested);
                failed = true;
            }
            if (!cycles && (v1.polls == previous_v1 || v2.polls == previous_v2 ||
                WmbJwm_Status()->callbacks == previous_callbacks)) {
                fputs("P8: application or WM callback progress stalled\n", stderr);
                failed = true;
            }
            previous_v1 = v1.polls; previous_v2 = v2.polls;
            previous_callbacks = WmbJwm_Status()->callbacks;
            if (stable && now - last_report >= 60000000) {
                printf("P8 endurance: elapsed-s=%llu inputs=%u keys=%u buttons=%u motions=%u "
                       "heap=%zu peak=%zu root-gap-ms=%zu\n",
                       (unsigned long long)((now - measuring) / 1000000), injections,
                       v2.keys, v2.buttons, v2.motions, heap.current_bytes, heap.peak_bytes,
                       wmb_metrics_max_dispatch_gap());
                fflush(stdout);
                last_report = now;
            }
        }
        if (baseline_set && ((cycles && cycles_finished && now - cycles_finished >= 1000000) ||
            (!cycles && now - measuring >= (uint64_t)seconds * 1000000))) {
            unsigned required = injections > 2 ? 2 * (injections - 2) : 0;
            bool input = v2.keys >= required && v2.buttons >= required && v2.motions >= injections / 2;
            if (allocation_get_stats(WMB_ALLOCATION_OWNER, &heap) != 0) failed = true;
            if (cycles) {
                if (settled.current_bytes != baseline.current_bytes ||
                    settled.payload_bytes != baseline.payload_bytes ||
                    settled.live_blocks != baseline.live_blocks ||
                    heap.peak_bytes != baseline.peak_bytes) failed = true;
                heap.current_bytes = settled.current_bytes;
            }
            if (!input || !pixels(&v2)) failed = true;
            printf("P8 %s: elapsed-ms=%llu cycles=%u injections=%u keys=%u buttons=%u motions=%u "
                   "heap=%zu..%zu peak=%zu..%zu input=%s pixels=%s %s\n",
                   cycles ? "cycles" : instrumented ? "instrumented-memory" : stable ? "stability" : "native",
                   (unsigned long long)((now - measuring) / 1000),
                   v2.cycles_completed - (cycles ? 32 : 0), injections, v2.keys, v2.buttons, v2.motions,
                   baseline.current_bytes, heap.current_bytes, baseline.peak_bytes, heap.peak_bytes,
                   input ? "valid" : "invalid", pixels(&v2) ? "valid" : "invalid",
                   failed ? "FAIL" : "PASS");
            if (cycles) {
                allocation_stats_t independent;
                if (allocation_get_stats(2, &independent) != 0 ||
                    independent.payload_bytes < ((cycle_target + 32) / 100) * 1024) failed = true;
                printf("P8 attribution: other-owner2 current=%zu payload=%zu peak=%zu\n",
                       independent.current_bytes, independent.payload_bytes, independent.peak_bytes);
            }
            printf("P8 progress: v1=%u v2=%u WM-callbacks=%llu clock-updates=%llu "
                   "callback-max-us=%llu clock-max-us=%llu\n", v1.polls, v2.polls,
                   (unsigned long long)WmbJwm_Status()->callbacks,
                   (unsigned long long)WmbJwm_Status()->clock_updates,
                   (unsigned long long)WmbJwm_Status()->max_callback_gap_us,
                   (unsigned long long)WmbJwm_Status()->max_clock_gap_us);
            break;
        }
        foreign_wait(NULL, cycles ? 0 : 1);
    }
    if (tenant.task) {
        tenant.stop = true;
        while (!tenant.done) { hal_task_yield(); (void)wmb_poll(); }
        if (wmb_metrics_record_task(tenant.task) != 0 || wmb_tenant_stop(&tenant) != 0) failed = true;
    }
    if (wmb_shutdown() != 0) failed = true;
    if (threads_started && !threads_joined && wmb_threads_done())
        if (wmb_threads_join() != 0) failed = true;
    wmb_metrics_print(stdout);
    {
        wmb_gateway_stats_t gateway;
        wmb_gateway_stats(&gateway);
        if (gateway.posted != gateway.completed || gateway.rejected || gateway.clients) failed = true;
        printf("P8 gateway: posted=%u completed=%u rejected=%u live=%zu\n",
               gateway.posted, gateway.completed, gateway.rejected, gateway.clients);
    }
    if (!v1.done || !v2.done || v1.result || v2.result || wmb_apps_count()) failed = true;
    printf("P8: ordered WM/apps/server shutdown %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
