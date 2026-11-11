#include "fiber_internal.h"
#include "hal_task.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __wrap_intr_handler_call_crash(uint32_t cause)
{
    assert(cause == (uint32_t)FIBER_ERR_OVERFLOW);
    assert(strcmp(hal_task_name(hal_task_self()), "canary-test") == 0);
    puts("P8 canary: real scheduler rejects the corrupted task stack PASS");
    exit(0);
}
static void corrupt(void *unused)
{
    (void)unused;
    *fiber_self()->canary ^= 1;
    hal_task_yield();
    abort();
}
int main(void)
{
    assert(hal_task_runtime_init(0) == 0);
    hal_task_t *task = hal_task_create("canary-test", corrupt, NULL, 64 * 1024);
    assert(task);
    hal_task_wake(task);
    hal_task_yield();
    fputs("P8 canary: scheduler missed corruption\n", stderr);
    return 1;
}
