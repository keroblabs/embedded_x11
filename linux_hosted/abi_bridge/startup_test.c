#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

bool wmb_ready(void) { return false; }
bool wmb_stop_requested(void) { return false; }
int __wrap_listen(int descriptor, int backlog)
{
    (void)descriptor; (void)backlog;
    errno = EIO;
    return -1;
}
int main(void)
{
    char directory[] = "/tmp/wmb-rpc-start-XXXXXX", endpoint[128];
    int file;
    assert(mkdtemp(directory));
    assert(snprintf(endpoint, sizeof(endpoint), "%s/endpoint.sock", directory) > 0);
    assert(wmb_rpc_server_start(endpoint, 2) < 0);
    assert(access(endpoint, F_OK) < 0 && errno == ENOENT);
    file = open(endpoint, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(file >= 0 && close(file) == 0);
    assert(wmb_rpc_server_start(endpoint, 2) < 0);
    assert(access(endpoint, F_OK) == 0);
    assert(unlink(endpoint) == 0 && rmdir(directory) == 0);
    puts("P8 RPC startup: failed listen cleans only its own socket; existing files preserved PASS");
    return 0;
}
