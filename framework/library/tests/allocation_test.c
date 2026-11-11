#include "allocation.h"
#include "hal_alloc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail_after = -1;
void *__real_wmb_alloc(size_t size);
void *__wrap_wmb_alloc(size_t size)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) fail_after--;
    return __real_wmb_alloc(size);
}

int main(void)
{
    allocation_stats_t initial, current, other;
    char *text, *resized;
    assert(allocation_get_stats(1, &initial) == 0);
    assert(initial.current_bytes == 0 && initial.live_blocks == 0);
    text = allocation_strdup(1, "tracked");
    assert(text && strcmp(text, "tracked") == 0);
    resized = allocation_realloc(2, text, 1024);
    assert(resized && strcmp(resized, "tracked") == 0);
    assert(allocation_get_stats(1, &current) == 0 && current.payload_bytes == 1024);
    assert(allocation_get_stats(2, &other) == 0 && other.current_bytes == 0);
    assert(current.current_bytes > current.payload_bytes && current.live_blocks == 1);
    fail_after = 0;
    assert(allocation_realloc(1, resized, 2048) == NULL);
    assert(strcmp(resized, "tracked") == 0);
    fail_after = -1;
    allocation_free(resized);
    fail_after = 1;
    assert(allocation_malloc(1, 200) == NULL);
    fail_after = -1;
    assert(allocation_calloc(1, SIZE_MAX, 2) == NULL);
    assert(allocation_get_stats(1, &current) == 0 && current.current_bytes == 0);
    assert(current.allocations == 2 && current.frees == 2);
    allocation_free(wmb_alloc(12));
    assert(allocation_get_stats(ALLOCATION_OWNER_LIMIT, &current) < 0);
    puts("P8 allocation: payload, metadata, ownership, realloc/OOM and raw free PASS");
    return 0;
}
