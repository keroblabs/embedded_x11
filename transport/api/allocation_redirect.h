#ifndef WMB_ALLOCATION_REDIRECT_H
#define WMB_ALLOCATION_REDIRECT_H
#include <stdlib.h>
#include <string.h>
#include "hal_alloc.h"
#include "allocation.h"

#define WMB_ALLOCATION_OWNER 1u
#define malloc(size) allocation_malloc(WMB_ALLOCATION_OWNER, (size))
#define calloc(count, size) allocation_calloc(WMB_ALLOCATION_OWNER, (count), (size))
#define realloc(memory, size) allocation_realloc(WMB_ALLOCATION_OWNER, (memory), (size))
#define free(memory) allocation_free((memory))
#define strdup(text) allocation_strdup(WMB_ALLOCATION_OWNER, (text))
#define wmb_alloc(size) allocation_malloc(WMB_ALLOCATION_OWNER, (size))
#define wmb_free(memory) allocation_free((memory))
#endif
