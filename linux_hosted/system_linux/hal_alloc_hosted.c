/*
 * hal_alloc_hosted.c - Hosted implementation of the hal_alloc contract.
 *
 * The whole point of the contract is that this is the ONLY translation
 * unit in the hosted build that includes <stdlib.h>.  Framework and
 * transport reach the allocator through wmb_alloc / wmb_free.
 */
#include "hal_alloc.h"

#include <stdlib.h>

void *wmb_alloc(size_t size)
{
    return malloc(size);
}

void wmb_free(void *p)
{
    free(p);
}
