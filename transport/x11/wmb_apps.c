#include "wmb_app_table.h"
#include "wmb.h"
#include "wmb_apps.h"
#include "hal_alloc.h"
#include "fiber.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Launch {
    struct Launch *next;
    hal_task_t *task;
    const wmb_app_t *app;
    int argc, result;
    char **argv;
    bool started, done;
} Launch;

static Launch *applications;
static int application_result;

const wmb_app_t *wmb_app_lookup(const char *name)
{
    size_t index;
    if (!name) return NULL;
    for (index = 0; index < wmb_app_table_size; index++)
        if (strcmp(name, wmb_app_table[index].name) == 0) return &wmb_app_table[index];
    return NULL;
}

static bool_t released(void *unused)
{
    (void)unused;
    return wmb_ready() || wmb_stop_requested();
}

static void run_application(void *argument)
{
    Launch *launch = argument;
    launch->started = true;
    (void)fiber_wait_for(released, NULL, FIBER_WAIT_FOREVER);
    if (!wmb_stop_requested())
        launch->result = launch->app->entry(launch->argc, launch->argv);
    launch->done = true;
    fiber_notify();
    /* Retain the FCB until its owner reaps the task handle. */
    for (;;) hal_task_wait();
}

hal_task_t *wmb_app_launch(const wmb_app_t *app, int argc, char **argv)
{
    Launch *launch;
    size_t size, offset;
    int index;
    if (!app || !app->entry || argc < 0 || argc > 32 ||
        (argc && !argv) || wmb_stop_requested()) {
        fputs("WMB launch: invalid app/arguments or component is stopping\n", stderr);
        return NULL;
    }
    size = sizeof(*launch) + ((size_t)argc + 1) * sizeof(char *);
    for (index = 0; index < argc; index++) {
        size_t length;
        if (!argv[index] || (length = strlen(argv[index])) > 4096 ||
            size > SIZE_MAX - length - 1) {
            fputs("WMB launch: invalid or oversized argument\n", stderr);
            return NULL;
        }
        size += length + 1;
    }
    launch = wmb_alloc(size);
    if (!launch) {
        fputs("WMB launch: argument allocation failed\n", stderr);
        return NULL;
    }
    memset(launch, 0, sizeof(*launch));
    launch->app = app;
    launch->argc = argc;
    launch->argv = (char **)(void *)(launch + 1);
    offset = sizeof(*launch) + ((size_t)argc + 1) * sizeof(char *);
    for (index = 0; index < argc; index++) {
        size_t length = strlen(argv[index]) + 1;
        launch->argv[index] = (char *)launch + offset;
        memcpy(launch->argv[index], argv[index], length);
        offset += length;
    }
    launch->argv[argc] = NULL;
    launch->task = hal_task_create(app->name, run_application, launch, app->stack_bytes);
    if (!launch->task) {
        fputs("WMB launch: task allocation failed\n", stderr);
        wmb_free(launch);
        return NULL;
    }
    launch->next = applications;
    applications = launch;
    return launch->task;
}

void wmb_apps_stop(void)
{
    Launch *launch;
    for (launch = applications; launch; launch = launch->next)
        if (!launch->started) hal_task_wake(launch->task);
    fiber_notify();
}

void wmb_apps_reap(void)
{
    Launch **position = &applications;
    while (*position) {
        Launch *launch = *position;
        if (!launch->done) position = &launch->next;
        else {
            if (launch->result) application_result = launch->result;
            *position = launch->next;
            hal_task_destroy(launch->task);
            wmb_free(launch);
        }
    }
}

size_t wmb_apps_count(void)
{
    Launch *launch;
    size_t count = 0;
    for (launch = applications; launch; launch = launch->next) count++;
    return count;
}

int wmb_apps_result(void) { return application_result; }
