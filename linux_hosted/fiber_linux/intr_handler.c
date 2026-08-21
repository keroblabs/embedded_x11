#include "intr_handler.h"

#include <stdio.h>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////

static volatile bool_t g_irq_enabled = TRUE;

///////////////////////////////////////////////////////////////////////////

void intr_global_set_enabled(bool_t enabled)
{
    g_irq_enabled = enabled;
}

bool_t intr_global_is_enabled(void)
{
    return g_irq_enabled;
}

void intr_handler_call_crash(uint32_t cause)
{
    fprintf(stderr, "fiber runtime crash, cause %d\n", (int)cause);
    fflush(stderr);
    abort();
}
