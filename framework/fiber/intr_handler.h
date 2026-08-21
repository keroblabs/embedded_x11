/*
 * intr_handler.h - Hosted stand-in for the firmware interrupt controller.
 *
 * framework/fiber/fiber.c expects these symbols from the arch layer.
 * The Linux port is cooperative and single threaded, so there is no
 * hardware to mask: the "enabled" flag is bookkeeping that keeps the
 * lock/unlock nesting semantics identical to the bare-metal ports.
 */
#ifndef _intr_handler_h_
#define _intr_handler_h_

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void   intr_global_set_enabled(bool_t enabled);
bool_t intr_global_is_enabled(void);

/* Unrecoverable runtime fault (e.g. a tripped stack canary).  Reports
 * the cause and terminates the process - it must never return.       */
void   intr_handler_call_crash(uint32_t cause);

#ifdef __cplusplus
}
#endif

#endif /* _intr_handler_h_ */
