/*
 * wmb_app_table.h - The launch table contract.
 *
 * docs/45-jwm-module-plan.md section 3.1.4 (I5).  Consumed by JWM's
 * command.c (guard #13, docs/11-portability-plan.md section 8.3) and
 * by any harness or boot module that populates the launch table.
 * The implementation lives with the transport
 * (docs/46-transport-module-plan.md).
 *
 * An in-tree app looks like a C main() at the source level (D-I5-1);
 * wmb_app_launch bridges to hal_task_create's void(*)(void *) form.
 */
#ifndef WMB_TRANSPORT_API_APP_TABLE_H
#define WMB_TRANSPORT_API_APP_TABLE_H

#include <stddef.h>
#include "hal_task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*wmb_app_main_t)(int argc, char **argv);

typedef struct {
    const char     *name;         /* lookup key, unique within the table */
    wmb_app_main_t  entry;        /* app body */
    size_t          stack_bytes;  /* fiber stack for this app */
} wmb_app_t;

/* Populated by the boot module; iteration bound is wmb_app_table_size. */
extern const wmb_app_t wmb_app_table[];
extern const size_t    wmb_app_table_size;

/* Return the entry matching name, or NULL (D-I5-3, D-I5-4). */
const wmb_app_t *wmb_app_lookup(const char *name);

/* Spawn app as a blocked fiber.  Packs argv into a heap slab, creates
 * the fiber via hal_task_create with a trampoline that unpacks and
 * calls app->entry.  Returns the created task, or NULL on failure.
 * Caller retains ownership of argv until wmb_app_launch returns. */
hal_task_t *wmb_app_launch(const wmb_app_t *app, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* WMB_TRANSPORT_API_APP_TABLE_H */
