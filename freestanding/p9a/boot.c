#include "wmb.h"
#include "wmb_gateway.h"
#include "wmb_app_table.h"
#include "runtime.h"
#include <stdatomic.h>
#include <newlib.h>
#if defined(_NANO_FORMATTED_IO) || !defined(_WANT_IO_C99_FORMATS) || !defined(_WANT_IO_LONG_LONG)
#error "The reference libc must support the C99/long-long formats used by WMB diagnostics"
#endif

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2,
               "P9a reference admission must not require an external atomic library");
_Static_assert(__STDC_HOSTED__ == 0 && sizeof(void *) == 4 && sizeof(long) == 4,
               "P9a reference must be a real freestanding ILP32 build");

const wmb_app_t wmb_app_table[] = {{NULL, NULL, 0}};
const size_t wmb_app_table_size = 0;
int main(void)
{
    static const char message[] = "P9a is a compile/link-only probe; replace the stub port for P9b\n";
    (void)wmb_os_console_write(message, sizeof(message) - 1);
    if (hal_task_runtime_init(0) || wmb_main(NULL)) wmb_os_abort();
    for (;;) {
        if (wmb_poll() != 0) wmb_os_abort();
        hal_task_yield();
    }
}
