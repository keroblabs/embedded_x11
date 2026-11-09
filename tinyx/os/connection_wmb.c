#include "kdrive.h"
#include "dixstruct.h"
#include "opaque.h"
#include "osdep.h"

int MaxClients = MAXCLIENTS;
Bool NewOutputPending, AnyClientsWriteBlocked, CriticalOutputPending;
Bool PartialNetwork;
int GrabInProgress;

void InitConnectionLimits(void) { MaxClients = MAXCLIENTS; }
void CreateWellKnownSockets(void) { ResetHosts(NULL); }
void ResetWellKnownSockets(void) { ResetHosts(NULL); }
void CloseWellKnownConnections(void) {}

const char *ClientAuthorized(ClientPtr client, unsigned int proto_n, char *proto,
                            unsigned int string_n, char *string)
{
    (void)client; (void)proto_n; (void)proto; (void)string_n; (void)string;
    return "P4 has no client transport; client attachment is a P5 milestone";
}

Bool EstablishNewConnections(ClientPtr client, pointer closure)
{
    (void)client; (void)closure;
    ErrorF("WMB TinyX: native connection establishment is unsupported\n");
    return FALSE;
}

void CheckConnections(void) {}

void CloseDownConnection(ClientPtr client)
{
    if (client->osPrivate != NULL)
        FatalError("WMB TinyX: unexpected client transport in P4\n");
}

static void require_no_client(ClientPtr client)
{
    if (client != NULL && client->index != 0)
        FatalError("WMB TinyX: client scheduling requires the P5 transport\n");
}

void OnlyListenToOneClient(ClientPtr client) { require_no_client(client); }
void ListenToAllClients(void) {}
void IgnoreClient(ClientPtr client) { require_no_client(client); }
void AttendClient(ClientPtr client) { require_no_client(client); }
void MakeClientGrabImpervious(ClientPtr client) { require_no_client(client); }
void MakeClientGrabPervious(ClientPtr client) { require_no_client(client); }
void AvailableClientInput(ClientPtr client) { require_no_client(client); }

void AddGeneralSocket(int fd)
{
    (void)fd;
    FatalError("WMB TinyX: native sockets are not supported\n");
}
void RemoveGeneralSocket(int fd) { (void)fd; }
void AddEnabledDevice(int fd)
{
    (void)fd;
    FatalError("WMB TinyX: native input descriptors are not supported\n");
}
void RemoveEnabledDevice(int fd) { (void)fd; }
