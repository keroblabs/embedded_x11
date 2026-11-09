#ifndef WMB_TINYX_SERVER_H
#define WMB_TINYX_SERVER_H
#if !defined(WMB_DEVEL)
#error "The WMB server requires WMB_DEVEL to be defined"
#endif
#include <stdbool.h>

typedef struct {
    int argc;
    char **argv;
    bool ready;
    bool finished;
    int result;
} wmb_tinyx_server_t;

void TinyX_ServerFiber(void *argument);
void WmbTinyXServerReady(void);
void WmbTinyXExtensionAdded(const char *name, int opcode);
void WmbTinyXServerStop(void);
bool WmbTinyXServerRootValid(void);

#endif
