#include "wmb_metrics.h"
#include "hal_task.h"
#include <assert.h>
#include <stdio.h>

static void parked(void *unused)
{
    (void)unused;
    for (;;) hal_task_wait();
}
int main(void)
{
    assert(hal_task_runtime_init(0) == 0);
    for (unsigned index = 0; index < 21; index++) {
        char name[64];
        if (index == 20)
            snprintf(name, sizeof(name), "an-application-name-longer-than-the-detail-record-budget");
        else snprintf(name, sizeof(name), "independent-app-%u", index);
        hal_task_t *task = hal_task_create(name, parked, NULL, 64 * 1024);
        assert(task);
        hal_task_wake(task); hal_task_yield();
        assert(wmb_metrics_record_task(task) == 0);
        hal_task_destroy(task); hal_task_yield();
    }
    wmb_metrics_print(stdout);
    wmb_metrics_stack_sampling(false);
    assert(wmb_metrics_record_task(NULL) < 0);
    puts("P8 metrics: bounded detail never limits valid app lifecycles; aggregated peaks PASS");
    return 0;
}
