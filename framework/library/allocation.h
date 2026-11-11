#ifndef FRAMEWORK_ALLOCATION_H
#define FRAMEWORK_ALLOCATION_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALLOCATION_OWNER_LIMIT 8u
typedef struct {
    size_t current_bytes, peak_bytes, payload_bytes, live_blocks;
    uint64_t allocations, frees;
} allocation_stats_t;

void *allocation_malloc(unsigned owner, size_t size);
void *allocation_calloc(unsigned owner, size_t count, size_t size);
void *allocation_realloc(unsigned owner, void *memory, size_t size);
char *allocation_strdup(unsigned owner, const char *text);
/* Untracked pointers are released through the same HAL allocator family. */
void allocation_free(void *memory);
int allocation_get_stats(unsigned owner, allocation_stats_t *result);
#ifdef __cplusplus
}
#endif
#endif
