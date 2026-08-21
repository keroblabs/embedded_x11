/*
 * hal_time.h - Platform contract: monotonic time.
 *
 * docs/12-platform-contract.md section 7.  Must compile standalone
 * under -ffreestanding -nostdinc with only <stdint.h> (gate PC-V1).
 */
#ifndef WMB_HAL_TIME_H
#define WMB_HAL_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Milliseconds since an arbitrary fixed origin.  Monotonic: never
 * decreases, never jumps.  Wraps at 2^32 ms (~49.7 days) - all callers
 * must use unsigned difference arithmetic. */
uint32_t hal_tick_ms(void);

/* Microsecond resolution variant, for profiling only. */
uint64_t hal_tick_us(void);

/* Wall clock.  Optional, and off in every current profile (D-30).
 * Seconds since 1970-01-01 UTC; 0 if the platform has no RTC or it
 * has never been set.  Never used for elapsed-time arithmetic. */
#ifdef WMB_HAS_WALLCLOCK
uint32_t hal_wallclock_s(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* WMB_HAL_TIME_H */
