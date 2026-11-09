#include "kdrive.h"
#include "dixstruct.h"
#include "osdep.h"

int ReadRequestFromClient(ClientPtr client)
{
    (void)client;
    ErrorF("WMB TinyX: request dispatch requires the P5 client transport\n");
    return -1;
}

int WriteToClient(ClientPtr client, int count, const char *buffer)
{
    (void)client; (void)count; (void)buffer;
    ErrorF("WMB TinyX: client output requires the P5 client transport\n");
    return -1;
}

void ResetOsBuffers(void) {}
void FlushAllOutput(void) {}
void FlushIfCriticalOutputPending(void) {}
void SetCriticalOutputPending(void) { CriticalOutputPending = TRUE; }
void ResetCurrentRequest(ClientPtr client)
{
    (void)client;
    FatalError("WMB TinyX: request suspension requires the P5 client transport\n");
}

int InsertFakeRequest(ClientPtr client, char *data, int count)
{
    (void)client; (void)data; (void)count;
    ErrorF("WMB TinyX: request insertion requires the P5 client transport\n");
    return FALSE;
}
