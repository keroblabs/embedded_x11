#include "tenant.h"
#include "allocation.h"
#include <stdio.h>
#include <string.h>

static void run(void *argument)
{
    WmbTestTenant *tenant = argument;
    void *blocks[128] = {0};
    unsigned count = 0;
    while (!tenant->stop) {
        unsigned required = *tenant->progress / 100;
        if (required > sizeof(blocks) / sizeof(blocks[0])) {
            fputs("P8 tenant: workload exceeds independent allocation capacity\n", stderr);
            tenant->result = -1;
            break;
        }
        while (count < required) {
            blocks[count] = allocation_malloc(2, 1024);
            if (!blocks[count]) {
                fputs("P8 tenant: independent allocation failed\n", stderr);
                tenant->result = -1;
                break;
            }
            memset(blocks[count], 0x7b, 1024);
            count++;
        }
        if (tenant->result) break;
        (void)hal_yield_timeout(1);
    }
    for (unsigned index = 0; index < count; index++) allocation_free(blocks[index]);
    tenant->done = true;
    for (;;) hal_task_wait();
}

int wmb_tenant_start(WmbTestTenant *tenant, const uint32_t *progress)
{
    if (!tenant || !progress) {
        fputs("P8 tenant: invalid configuration\n", stderr);
        return -1;
    }
    tenant->progress = progress;
    tenant->task = hal_task_create("tenant-owner2", run, tenant, 64 * 1024);
    if (!tenant->task) {
        fputs("P8 tenant: task allocation failed\n", stderr);
        return -1;
    }
    hal_task_wake(tenant->task);
    return 0;
}

int wmb_tenant_stop(WmbTestTenant *tenant)
{
    if (!tenant || !tenant->task || !tenant->done) {
        fputs("P8 tenant: stop requires the independently parked task\n", stderr);
        return -1;
    }
    hal_task_destroy(tenant->task);
    tenant->task = NULL;
    return tenant->result;
}
