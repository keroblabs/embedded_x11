#ifndef WMB_FREESTANDING_RUNTIME_H
#define WMB_FREESTANDING_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
int wmb_os_console_write(const char *text, int length);
_Noreturn void wmb_os_abort(void);
void *wmb_os_sbrk(intptr_t increment);
#endif
