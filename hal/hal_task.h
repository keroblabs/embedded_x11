/*
 * hal_task.h - Platform contract: execution contexts and cooperative waiting.
 *
 * docs/12-platform-contract.md section 6.  Must compile standalone
 * under -ffreestanding -nostdinc with only <stdint.h> / <stddef.h> /
 * <stdbool.h> (gate PC-V1).
 *
 * Unlike the other four headers this one is NOT implemented by a
 * platform: WMB implements all nine functions itself, once, in
 * framework/fiber/ (90-decisions.md D-31).  A port supplies
 * fiber_port.h's five symbols instead.
 */
#ifndef WMB_HAL_TASK_H
#define WMB_HAL_TASK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_task hal_task_t;
typedef void (*hal_task_entry_t)(void *arg);

/* Bring the cooperative runtime up.  Converts the caller into the
 * root task.  safety_tick_ms is a preemption backstop, not the
 * scheduling mechanism; 0 disables it.
 *
 * NOTE: the tick is currently NOT armed on any port - 90-decisions.md
 * D-47.  The value is accepted and ignored, so no call site changes
 * when it is enabled.  Code as if it were armed. */
int  hal_task_runtime_init(uint32_t safety_tick_ms);

/* Created BLOCKED: the task runs only after hal_task_wake(). */
hal_task_t *hal_task_create(const char *name, hal_task_entry_t entry,
                            void *arg, size_t stack_bytes);
void        hal_task_destroy(hal_task_t *t);
hal_task_t *hal_task_self(void);
const char *hal_task_name(const hal_task_t *t);

/* Round-robin yield.  Returns when the scheduler comes back around. */
void hal_task_yield(void);

/* Block until hal_task_wake() is called on this task. */
void hal_task_wait(void);

/* Mark t runnable.  Safe to call on an already-runnable task.
 * Never switches: it returns to the caller. */
void hal_task_wake(hal_task_t *t);

/* Cooperative wait with a deadline.
 *   returns 1  - woken explicitly by hal_task_wake()
 *   returns 0  - max_ms elapsed
 * max_ms == 0 behaves as hal_task_yield() and returns 0.
 * This is the ONLY blocking primitive in WMB. */
int hal_yield_timeout(uint32_t max_ms);

#ifdef __cplusplus
}
#endif

#endif /* WMB_HAL_TASK_H */
