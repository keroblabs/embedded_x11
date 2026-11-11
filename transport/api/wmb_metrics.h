#ifndef WMB_METRICS_H
#define WMB_METRICS_H
#include "hal_task.h"
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

/* Windowing allocations above HAL; framebuffer/port stacks are separate domains. */
#define WMB_ALLOCATION_OWNER 1u
int wmb_metrics_record_task(const hal_task_t *task);
/* Some memory tools forbid diagnostic reads of inactive stack frames. */
void wmb_metrics_stack_sampling(bool enabled);
void wmb_metrics_print(FILE *output);
size_t wmb_metrics_max_dispatch_gap(void);
void wmb_metrics_dispatch_tick(void);
#endif
