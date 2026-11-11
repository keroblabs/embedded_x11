#include "runtime.h"
#include "hal_alloc.h"
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

_Alignas(max_align_t) unsigned char wmb_test_heap[256];
static char output[1024];
static size_t position;
static int maximum_write = 1024;
static jmp_buf fatal;
int _write(int descriptor, const void *bytes, size_t count);
int _read(int descriptor, void *memory, size_t count);
int _close(int descriptor);
long _lseek(int descriptor, long offset, int direction);
int _fstat(int descriptor, struct stat *result);
int _isatty(int descriptor);
_Noreturn void _exit(int status);
int wmb_os_console_write(const char *text, int length)
{
    int size = length < maximum_write ? length : maximum_write;
    assert(size >= 0 && position + (size_t)size < sizeof(output));
    memcpy(output + position, text, (size_t)size);
    position += (size_t)size;
    return size;
}
_Noreturn void wmb_os_abort(void) { longjmp(fatal, 1); }
int main(void)
{
    struct stat status;
    assert(wmb_os_sbrk(0) == wmb_test_heap);
    assert(wmb_os_sbrk(16) == wmb_test_heap);
    assert(wmb_os_sbrk(240) == wmb_test_heap + 16);
    assert(wmb_os_sbrk(1) == (void *)-1 && errno == ENOMEM);
    assert(wmb_os_sbrk(-256) == wmb_test_heap + 256);
    assert(wmb_os_sbrk(-1) == (void *)-1 && errno == ENOMEM);
    assert(wmb_os_sbrk(INTPTR_MIN) == (void *)-1 && errno == ENOMEM);
    assert(wmb_os_sbrk(INTPTR_MAX) == (void *)-1 && errno == ENOMEM);
    assert(wmb_os_sbrk(0) == wmb_test_heap);
    assert(_write(1, "abc", 3) == 3 && memcmp(output, "abc", 3) == 0);
    maximum_write = 2;
    assert(_write(2, "xyz", 3) == 2);
    maximum_write = 0;
    assert(_write(1, "z", 1) == -1 && errno == EIO);
    maximum_write = 1024;
    assert(_write(1, NULL, 0) == 0);
    assert(_write(1, NULL, 1) == -1 && errno == EBADF);
    assert(_write(4, "z", 1) == -1 && errno == EBADF);
    assert(_write(1, "z", SIZE_MAX) == -1 && errno == EBADF);
    assert(_read(0, output, 1) == -1 && errno == EBADF);
    assert(_close(1) == -1 && errno == EBADF);
    assert(_lseek(1, 0, 0) == -1 && errno == ESPIPE);
    assert(_fstat(1, &status) == 0 && S_ISCHR(status.st_mode));
    assert(_fstat(3, &status) == -1 && errno == EBADF);
    assert(_isatty(2) == 1 && _isatty(4) == -1 && errno == EBADF);
    void *memory = wmb_alloc(24);
    assert(memory); wmb_free(memory);
    if (!setjmp(fatal)) abort();
    if (!setjmp(fatal)) _exit(1);
    assert(strstr(output, "unsupported") && strstr(output, "abort"));
    puts("P9a runtime: bounded arena, diagnostic console, explicit file rejection and fatal paths PASS");
    return 0;
}
