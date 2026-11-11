#include "hal_task.h"
#include <assert.h>
#include <stdio.h>

static int started;
static void worker(void *argument)
{
    volatile unsigned char payload[4096];
    (void)argument;
    for (size_t index = 0; index < sizeof(payload); index++) payload[index] = (unsigned char)index;
    started = 1;
    for (;;) {
        assert(payload[7] == 7);
        hal_task_wait();
    }
}
int main(void)
{
    size_t before, after, reserved;
    hal_task_t *task;
    assert(hal_task_runtime_init(0) == 0);
    task = hal_task_create("stack-test", worker, NULL, 64 * 1024);
    assert(task && hal_task_stack_usage(task, &before, &reserved) == 0);
    assert(reserved == 64 * 1024 && before > 0 && before < reserved);
    hal_task_wake(task);
    while (!started) hal_task_yield();
    assert(hal_task_stack_usage(task, &after, &reserved) == 0);
    assert(after >= 4096 && after >= before && after < reserved);
    assert(hal_task_stack_usage(hal_task_self(), &after, &reserved) < 0);
    hal_task_destroy(task);
    hal_task_yield();
    printf("P8 stack: initialized context=%zu, exercised HWM >=4096, main unknown PASS\n", before);
    return 0;
}
