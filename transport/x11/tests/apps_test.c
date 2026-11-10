#include "wmb_app_table.h"
#include "wmb_apps.h"
#include "hal_task.h"
#include "fiber.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool ready = true, stopping, fail;
static unsigned calls;
static int bad_arguments;
bool wmb_ready(void) { return ready; }
bool wmb_stop_requested(void) { return stopping; }
void *__real_wmb_alloc(size_t size);
void *__wrap_wmb_alloc(size_t size)
{
    if (fail) { fail = false; return NULL; }
    return __real_wmb_alloc(size);
}

static int application(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[0], "probe") || strcmp(argv[1], "copied") || argv[2])
        bad_arguments = 1;
    calls++;
    return 0;
}

const wmb_app_t wmb_app_table[] = {{"probe", application, 64 * 1024}};
const size_t wmb_app_table_size = 1;

#define CHECK(value) do { \
    if (!(value)) { fprintf(stderr, "P7 launch FAIL %d: %s\n", __LINE__, #value); return 1; } \
} while (0)

int main(void)
{
    const wmb_app_t *app;
    CHECK(hal_task_runtime_init(0) == 0);
    app = wmb_app_lookup("probe");
    CHECK(app && !wmb_app_lookup("missing") && !wmb_app_lookup(NULL));
    for (unsigned index = 0; index < 100; index++) {
        char name[] = "probe", value[] = "copied";
        char *arguments[] = {name, value};
        hal_task_t *task = wmb_app_launch(app, 2, arguments);
        unsigned before = calls;
        CHECK(task != NULL);
        value[0] = 'x';
        hal_task_yield();
        CHECK(calls == before);
        hal_task_wake(task);
        for (unsigned attempt = 0; attempt < 20 && wmb_apps_count(); attempt++) {
            hal_task_yield();
            wmb_apps_reap();
        }
        CHECK(!wmb_apps_count() && calls == before + 1 && !bad_arguments);
    }
    CHECK(!wmb_app_launch(NULL, 0, NULL));
    CHECK(!wmb_app_launch(app, -1, NULL));
    fail = true;
    CHECK(!wmb_app_launch(app, 0, NULL));
    ready = false;
    CHECK(wmb_app_launch(app, 0, NULL) != NULL);
    stopping = true;
    wmb_apps_stop();
    for (unsigned attempt = 0; attempt < 20 && wmb_apps_count(); attempt++) {
        hal_task_yield();
        wmb_apps_reap();
    }
    CHECK(!wmb_apps_count() && calls == 100 && !wmb_apps_result());
    puts("PASS P7 launch: copied argv, blocked start, OOM, reaping/reuse and cancellation");
    return 0;
}
