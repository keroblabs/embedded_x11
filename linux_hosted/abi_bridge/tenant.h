#ifndef WMB_TEST_TENANT_H
#define WMB_TEST_TENANT_H
#include "hal_task.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    const uint32_t *progress;
    hal_task_t *task;
    bool stop, done;
    int result;
} WmbTestTenant;
int wmb_tenant_start(WmbTestTenant *tenant, const uint32_t *progress);
int wmb_tenant_stop(WmbTestTenant *tenant);
#endif
