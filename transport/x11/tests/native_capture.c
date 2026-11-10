#define _GNU_SOURCE
#include "wire_script.h"
#include <sys/socket.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int display_fd = -1;
static WmbWireLog *capture;
void wmb_native_capture(int fd, WmbWireLog *log) { display_fd = fd; capture = log; }

static void resolve(void *function, size_t size, const char *name)
{
    void *symbol = dlsym(RTLD_NEXT, name);
    if (!symbol || size != sizeof(symbol)) abort();
    memcpy(function, &symbol, size);
}

static void vectors(bool output, const struct iovec *vector, size_t count, ssize_t bytes)
{
    size_t i;
    for (i = 0; i < count && bytes > 0; i++) {
        size_t size = vector[i].iov_len < (size_t)bytes ? vector[i].iov_len : (size_t)bytes;
        wmb_wire_record(capture, output, vector[i].iov_base, size);
        bytes -= (ssize_t)size;
    }
}

ssize_t writev(int fd, const struct iovec *vector, int count)
{
    static ssize_t (*real)(int, const struct iovec *, int);
    ssize_t result;
    if (!real) resolve(&real, sizeof(real), "writev");
    result = real(fd, vector, count);
    if (fd == display_fd && capture && result > 0)
        vectors(true, vector, (size_t)count, result);
    return result;
}
ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
    static ssize_t (*real)(int, const struct msghdr *, int);
    ssize_t result;
    if (!real) resolve(&real, sizeof(real), "sendmsg");
    result = real(fd, message, flags);
    if (fd == display_fd && capture && result > 0)
        vectors(true, message->msg_iov, message->msg_iovlen, result);
    return result;
}
ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    static ssize_t (*real)(int, struct msghdr *, int);
    ssize_t result;
    if (!real) resolve(&real, sizeof(real), "recvmsg");
    result = real(fd, message, flags);
    if (fd == display_fd && capture && result > 0 && !(flags & MSG_PEEK))
        vectors(false, message->msg_iov, message->msg_iovlen, result);
    return result;
}
ssize_t recv(int fd, void *bytes, size_t size, int flags)
{
    static ssize_t (*real)(int, void *, size_t, int);
    ssize_t result;
    if (!real) resolve(&real, sizeof(real), "recv");
    result = real(fd, bytes, size, flags);
    if (fd == display_fd && capture && result > 0 && !(flags & MSG_PEEK))
        wmb_wire_record(capture, false, bytes, (size_t)result);
    return result;
}
