#include "wmb_metrics.h"
#include "allocation.h"
#include "hal_time.h"
#include <stdint.h>
#include <string.h>

typedef struct {
    char name[32];
    size_t used, reserved;
} StackRecord;
static StackRecord stacks[16];
static StackRecord additional = {"additional-roles", 0, 0};
static size_t stack_count, maximum_gap;
static int aggregated;
static bool sample_stacks = true;
static uint32_t previous_dispatch;
static int observed_dispatch;

int wmb_metrics_record_task(const hal_task_t *task)
{
    size_t used, reserved, index;
    StackRecord *record;
    const char *name = hal_task_name(task);
    if (!name) {
        fputs("WMB metrics: invalid task for stack measurement\n", stderr);
        return -1;
    }
    if (!sample_stacks) return 0;
    if (hal_task_stack_usage(task, &used, &reserved) != 0) {
        fputs("WMB metrics: invalid task, stack canary or stack bounds\n", stderr);
        return -1;
    }
    if (strlen(name) >= sizeof(stacks[0].name)) index = stack_count;
    else {
        for (index = 0; index < stack_count; index++)
            if (strcmp(name, stacks[index].name) == 0) break;
    }
    if (index == stack_count && (strlen(name) >= sizeof(stacks[0].name) ||
        stack_count == sizeof(stacks) / sizeof(stacks[0]))) {
        if (!aggregated) {
            fputs("WMB metrics: aggregating additional/long task names into one peak record\n", stderr);
            aggregated = 1;
        }
        record = &additional;
    } else {
        record = &stacks[index];
        if (index == stack_count) {
            strcpy(stacks[index].name, name);
            stack_count++;
        }
    }
    if (used > record->used) record->used = used;
    if (reserved > record->reserved) record->reserved = reserved;
    return 0;
}

void wmb_metrics_stack_sampling(bool enabled)
{
    sample_stacks = enabled;
    if (!enabled)
        fputs("WMB metrics: inactive-stack HWM sampling explicitly disabled; scheduler canaries remain active\n",
              stderr);
}

void wmb_metrics_dispatch_tick(void)
{
    uint32_t now = hal_tick_ms();
    if (observed_dispatch && now - previous_dispatch > maximum_gap)
        maximum_gap = now - previous_dispatch;
    observed_dispatch = 1;
    previous_dispatch = now;
}

size_t wmb_metrics_max_dispatch_gap(void) { return maximum_gap; }

void wmb_metrics_print(FILE *output)
{
    allocation_stats_t heap;
    size_t index;
    if (!output || allocation_get_stats(WMB_ALLOCATION_OWNER, &heap) != 0) {
        fputs("WMB metrics: invalid report destination or allocation owner\n", stderr);
        return;
    }
    fprintf(output, "P8 heap: current=%zu payload=%zu peak=%zu blocks=%zu allocations=%llu frees=%llu\n",
            heap.current_bytes, heap.payload_bytes, heap.peak_bytes, heap.live_blocks,
            (unsigned long long)heap.allocations, (unsigned long long)heap.frees);
    if (!sample_stacks)
        fputs("P8 stack: HWM sampling disabled for memory-tool validation; use a normal run for stack sizing\n",
              output);
    for (index = 0; index < stack_count; index++)
        fprintf(output, "P8 stack: %s used=%zu reserved=%zu (includes port context)\n",
                stacks[index].name, stacks[index].used, stacks[index].reserved);
    if (aggregated)
        fprintf(output, "P8 stack: %s max-used=%zu max-reserved=%zu (aggregated roles)\n",
                additional.name, additional.used, additional.reserved);
    fprintf(output, "P8 stack: main=unknown; root-poll-max-gap=%zu ms\n", maximum_gap);
}
