/*
 * hal_alloc.h - HAL contract: dynamic memory.
 *
 * WMB's framework never calls malloc/free directly.  It calls the two
 * functions declared here, and each platform supplies an implementation:
 *   linux_hosted/system_linux/hal_alloc_hosted.c  wraps libc malloc/free.
 *   <target>_native/system_<target>/hal_alloc_*.c wraps a static arena.
 *
 * See docs/12-platform-contract.md.  Must compile standalone under
 * -ffreestanding -nostdinc with only <stddef.h>.
 */
#ifndef WMB_HAL_ALLOC_H
#define WMB_HAL_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns NULL on exhaustion. */
void *wmb_alloc(size_t size);

/* wmb_free(NULL) is a no-op, matching libc free(). */
void  wmb_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* WMB_HAL_ALLOC_H */
