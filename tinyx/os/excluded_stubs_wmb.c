#include "kdrive.h"
#include "dixstruct.h"
#include "osdep.h"

Bool defeatAccessControl = FALSE;

/* MI registers optional SHM hooks even when the extension is not selected. */
void ShmRegisterFbFuncs(ScreenPtr screen) { (void)screen; }

void InitAuthorization(char *file)
{
    if (file != NULL)
        FatalError("WMB TinyX: authorization files are not supported\n");
}
void ResetHosts(const char *name) { (void)name; }
void AddLocalHosts(void) {}
void EnableLocalHost(void) {}
void DisableLocalHost(void) {}
int InvalidHost(sockaddrPtr address, int length, ClientPtr client)
{
    (void)address; (void)length; (void)client;
    return 1;
}
int LocalClient(ClientPtr client) { return client->index == 0; }
int ChangeAccessControl(ClientPtr client, int enabled)
{
    (void)client; (void)enabled;
    ErrorF("WMB TinyX: network access control is not supported\n");
    return BadAccess;
}
int GetHosts(pointer *data, int *count, int *length, BOOL *enabled)
{
    *data = NULL; *count = *length = 0; *enabled = FALSE;
    return Success;
}
int AddHost(ClientPtr client, int family, unsigned length, const void *address)
{
    (void)client; (void)family; (void)length; (void)address;
    ErrorF("WMB TinyX: network hosts are not supported\n");
    return BadAccess;
}
int RemoveHost(ClientPtr client, int family, unsigned length, pointer address)
{
    (void)client; (void)family; (void)length; (void)address;
    ErrorF("WMB TinyX: network hosts are not supported\n");
    return BadAccess;
}
