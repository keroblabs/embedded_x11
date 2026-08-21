#ifndef _fiber_config_h_
#define _fiber_config_h_

/* Tunables.  Override from a build flag (-DFIBER_MAX=…) if needed. */

#ifndef FIBER_MAX
#define FIBER_MAX                  8
#endif

#ifndef FIBER_DEFAULT_STACK
#define FIBER_DEFAULT_STACK        2048u
#endif

#ifndef FIBER_DEFAULT_PREEMPTION_MS
/* Default preemption period in milliseconds (number of 1 ms ticks
 * between forced context switches).  Override with a build flag
 * (-DFIBER_DEFAULT_PREEMPTION_MS=…) if needed.                      */
#define FIBER_DEFAULT_PREEMPTION_MS  1u
#endif

#ifndef FIBER_CANARY
#define FIBER_CANARY               0xDEADBEEFu
#endif

#ifndef FIBER_MUTEX_MAX
/* Maximum number of fiber_mutex_t objects allocatable via
 * fiber_mutex_create().  Statically allocated, no heap.            */
#define FIBER_MUTEX_MAX            8
#endif

#ifndef FIBER_COND_MAX
/* Maximum number of fiber_cond_t objects allocatable via
 * fiber_cond_create().  Statically allocated, no heap.             */
#define FIBER_COND_MAX             8
#endif

#endif /* _fiber_config_h_ */
