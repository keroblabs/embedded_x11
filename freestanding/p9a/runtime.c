#include "runtime.h"
#include "hal_alloc.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

extern unsigned char __heap_start[], __heap_end[];
static uintptr_t heap_break;

void *wmb_os_sbrk(intptr_t increment)
{
    uintptr_t begin = (uintptr_t)__heap_start, end = (uintptr_t)__heap_end;
    uintptr_t current = heap_break ? heap_break : begin;
    if ((increment >= 0 && (uintptr_t)increment > end - current) ||
        (increment < 0 && (uintptr_t)(-(increment + 1)) + 1 > current - begin)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_break = increment < 0 ? current - ((uintptr_t)(-(increment + 1)) + 1) :
                                current + (uintptr_t)increment;
    return (void *)current;
}
void *_sbrk(ptrdiff_t increment) { return wmb_os_sbrk(increment); }
void *wmb_alloc(size_t size) { return malloc(size); }
void wmb_free(void *memory) { free(memory); }
_Noreturn void _exit(int status) { (void)status; wmb_os_abort(); }
_Noreturn void abort(void)
{
    static const char message[] = "WMB: fatal freestanding abort\n";
    (void)wmb_os_console_write(message, sizeof(message) - 1);
    wmb_os_abort();
}
int _write(int descriptor, const void *bytes, size_t count)
{
    if ((descriptor != 1 && descriptor != 2) || count > INT32_MAX || (!bytes && count)) {
        static const char message[] = "WMB: unsupported console descriptor/length\n";
        (void)wmb_os_console_write(message, sizeof(message) - 1);
        errno = EBADF;
        return -1;
    }
    if (!count) return 0;
    int written = wmb_os_console_write(bytes, (int)count);
    if (written <= 0 || (size_t)written > count) {
        errno = EIO;
        return -1;
    }
    return written;
}

static int unsupported(const char *operation, int error)
{
    static const char message[] = "WMB: unsupported newlib operation: ";
    (void)wmb_os_console_write(message, sizeof(message) - 1);
    size_t length = 0;
    while (operation[length]) length++;
    (void)wmb_os_console_write(operation, (int)length);
    (void)wmb_os_console_write("\n", 1);
    errno = error;
    return -1;
}
int _close(int descriptor) { (void)descriptor; return unsupported("close", EBADF); }
int _read(int descriptor, void *memory, size_t count)
{
    (void)descriptor; (void)memory; (void)count;
    return unsupported("read", EBADF);
}
long _lseek(int descriptor, long offset, int direction)
{
    (void)descriptor; (void)offset; (void)direction;
    return unsupported("seek", ESPIPE);
}
int _fstat(int descriptor, struct stat *result)
{
    if ((descriptor != 1 && descriptor != 2) || !result)
        return unsupported("file status", EBADF);
    *result = (struct stat){.st_mode = S_IFCHR};
    return 0;
}
int _isatty(int descriptor)
{
    if (descriptor != 1 && descriptor != 2) return unsupported("terminal query", EBADF);
    return 1;
}
