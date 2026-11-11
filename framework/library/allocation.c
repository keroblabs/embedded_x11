#include "allocation.h"
#include "hal_alloc.h"
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct Allocation {
    struct Allocation *next;
    void *memory;
    size_t size;
    unsigned owner;
} Allocation;

static Allocation *allocations[256];
static allocation_stats_t statistics[ALLOCATION_OWNER_LIMIT];
static atomic_flag guard = ATOMIC_FLAG_INIT;
static unsigned bucket(const void *memory) { return ((uintptr_t)memory >> 4) % 256u; }

static void lock(void)
{
    while (atomic_flag_test_and_set_explicit(&guard, memory_order_acquire)) {}
}
static void unlock(void) { atomic_flag_clear_explicit(&guard, memory_order_release); }

void *allocation_malloc(unsigned owner, size_t size)
{
    Allocation *entry;
    void *memory;
    size_t bytes;
    if (owner >= ALLOCATION_OWNER_LIMIT || size > SIZE_MAX - sizeof(*entry)) {
        fputs("allocation: invalid owner or overflowing size\n", stderr);
        errno = EINVAL;
        return NULL;
    }
    if (!size) size = 1;
    bytes = size + sizeof(*entry);
    memory = wmb_alloc(size);
    entry = memory ? wmb_alloc(sizeof(*entry)) : NULL;
    if (!entry) {
        wmb_free(memory);
        fputs("allocation: backing storage or tracking allocation failed\n", stderr);
        errno = ENOMEM;
        return NULL;
    }
    *entry = (Allocation){.memory = memory, .size = size, .owner = owner};
    lock();
    if (statistics[owner].current_bytes > SIZE_MAX - bytes) {
        unlock();
        wmb_free(entry);
        wmb_free(memory);
        fputs("allocation: accounting overflow\n", stderr);
        errno = ENOMEM;
        return NULL;
    }
    entry->next = allocations[bucket(memory)];
    allocations[bucket(memory)] = entry;
    statistics[owner].current_bytes += bytes;
    statistics[owner].payload_bytes += size;
    statistics[owner].live_blocks++;
    statistics[owner].allocations++;
    if (statistics[owner].current_bytes > statistics[owner].peak_bytes)
        statistics[owner].peak_bytes = statistics[owner].current_bytes;
    unlock();
    return memory;
}

void *allocation_calloc(unsigned owner, size_t count, size_t size)
{
    void *memory;
    if (size && count > SIZE_MAX / size) {
        fputs("allocation: calloc size overflow\n", stderr);
        errno = ENOMEM;
        return NULL;
    }
    size *= count;
    memory = allocation_malloc(owner, size);
    if (memory) memset(memory, 0, size ? size : 1);
    return memory;
}

void allocation_free(void *memory)
{
    Allocation **position, *entry;
    if (!memory) return;
    lock();
    for (position = &allocations[bucket(memory)]; *position && (*position)->memory != memory;
         position = &(*position)->next) {}
    entry = *position;
    if (entry) {
        *position = entry->next;
        statistics[entry->owner].current_bytes -= entry->size + sizeof(*entry);
        statistics[entry->owner].payload_bytes -= entry->size;
        statistics[entry->owner].live_blocks--;
        statistics[entry->owner].frees++;
    }
    unlock();
    wmb_free(entry);
    wmb_free(memory);
}

void *allocation_realloc(unsigned owner, void *memory, size_t size)
{
    Allocation *entry;
    size_t previous;
    void *replacement;
    if (!memory) return allocation_malloc(owner, size);
    if (!size) { allocation_free(memory); return NULL; }
    lock();
    for (entry = allocations[bucket(memory)]; entry && entry->memory != memory; entry = entry->next) {}
    if (!entry) {
        unlock();
        fputs("allocation: realloc requires a tracked allocation\n", stderr);
        errno = EINVAL;
        return NULL;
    }
    previous = entry->size;
    owner = entry->owner;
    unlock();
    replacement = allocation_malloc(owner, size);
    if (!replacement) return NULL;
    memcpy(replacement, memory, previous < size ? previous : size);
    allocation_free(memory);
    return replacement;
}

char *allocation_strdup(unsigned owner, const char *text)
{
    size_t size;
    char *result;
    if (!text) {
        fputs("allocation: NULL string\n", stderr);
        errno = EINVAL;
        return NULL;
    }
    size = strlen(text) + 1;
    result = allocation_malloc(owner, size);
    if (result) memcpy(result, text, size);
    return result;
}

int allocation_get_stats(unsigned owner, allocation_stats_t *result)
{
    if (owner >= ALLOCATION_OWNER_LIMIT || !result) {
        fputs("allocation: invalid statistics request\n", stderr);
        return -1;
    }
    lock();
    *result = statistics[owner];
    unlock();
    return 0;
}
