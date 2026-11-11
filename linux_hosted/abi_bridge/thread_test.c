#define _POSIX_C_SOURCE 200809L
#include "wmb_api.h"
#include "probe.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const wmb_api_t *api;
    pthread_t thread;
    unsigned number;
    atomic_bool done;
    int result;
} Caller;
static Caller callers[2];
static unsigned started;

static void *run(void *argument)
{
    Caller *caller = argument;
    const wmb_api_t *api = caller->api;
    Display *display = NULL;
    int screen;
    Window window = 0;
    Atom atom;
    char name[32];
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) goto failed;
    display = api->XOpenDisplay(NULL);
    if (!display) goto failed;
    screen = api->XDefaultScreen(display);
    window = api->XCreateSimpleWindow(display, api->XRootWindow(display, screen),
                                      8, 8, 64, 40, 0, 0, 0);
    snprintf(name, sizeof(name), "WMB_P8_THREAD_%u", caller->number);
    atom = api->XInternAtom(display, name, False);
    if (!window || !atom || api->wmbApiRevision() != 2) goto failed;
    for (unsigned iteration = 0; iteration < 300; iteration++) {
        unsigned long values[] = {caller->number, iteration, 0xfedcba98, 0xffffffff};
        Atom actual;
        int format;
        unsigned long count, after;
        unsigned char *data = NULL;
        api->XChangeProperty(display, window, atom, atom, 32, PropModeReplace,
                              (const unsigned char *)values, 4);
        if (api->XGetWindowProperty(display, window, atom, 0, 4, False, atom,
                                    &actual, &format, &count, &after, &data) != Success ||
            actual != atom || format != 32 || count != 4 || after ||
            !data) {
            if (data) api->XFree(data);
            goto failed;
        }
        for (size_t index = 0; index < count; index++)
            if ((((unsigned long *)(void *)data)[index] & 0xffffffffUL) != values[index]) {
                api->XFree(data);
                goto failed;
            }
        api->XFree(data);
        if (api->XDisplayOfScreen(api->XDefaultScreenOfDisplay(display)) != display)
            goto failed;
        sched_yield();
    }
    caller->result = 0;
    goto cleanup;
failed:
    fprintf(stderr, "P8 E4: caller %u FAILED\n", caller->number);
    caller->result = -1;
cleanup:
    if (window) api->XDestroyWindow(display, window);
    if (display) api->XCloseDisplay(display);
    atomic_store_explicit(&caller->done, true, memory_order_release);
    return NULL;
}

int wmb_threads_start(const void *table)
{
    for (unsigned index = 0; index < 2; index++) {
        callers[index].api = table;
        callers[index].number = index + 1;
        if (pthread_create(&callers[index].thread, NULL, run, &callers[index]) != 0) {
            fputs("P8 E4: pthread_create failed\n", stderr);
            return -1;
        }
        started++;
    }
    return 0;
}
int wmb_threads_done(void)
{
    for (unsigned index = 0; index < started; index++)
        if (!atomic_load_explicit(&callers[index].done, memory_order_acquire)) return 0;
    return 1;
}
int wmb_threads_join(void)
{
    int result = started == 2 ? 0 : -1;
    for (unsigned index = 0; index < started; index++) {
        if (pthread_join(callers[index].thread, NULL) != 0 || callers[index].result)
            result = -1;
    }
    printf("P8 E4: two preempting callers, 600 exact format-32 property round trips %s\n",
           result ? "FAIL" : "PASS");
    return result;
}
