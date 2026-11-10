#include "wmb_connection.h"
#include "opaque.h"

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
    (void)proto_n; (void)proto; (void)string_n; (void)string;
    return wmb_server_connection(client) ? NULL : "Not an internal WMB client";
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
    WmbConnection *c = wmb_server_connection(client);
    if (c) {
        c->server_client = NULL;
        c->closed = true;
        c->in_flight = false;
    }
    free(client->osPrivate);
    client->osPrivate = NULL;
}

static void update_allowed(void)
{
    unsigned int i;
    for (i = 0; i < WMB_CONNECTION_LIMIT; i++) {
        WmbConnection *c = wmb_connections[i];
        if (c) c->allowed = !c->ignored &&
            (!GrabInProgress || c->index == (unsigned int)GrabInProgress || c->impervious);
    }
    fiber_notify();
}

void OnlyListenToOneClient(ClientPtr client) { GrabInProgress = client->index; update_allowed(); }
void ListenToAllClients(void) { GrabInProgress = 0; update_allowed(); }
void IgnoreClient(ClientPtr client)
{
    WmbConnection *c = wmb_server_connection(client);
    if (c) c->ignored = true;
    isItTimeToYield = TRUE;
    update_allowed();
}
void AttendClient(ClientPtr client)
{
    WmbConnection *c = wmb_server_connection(client);
    if (c) c->ignored = false;
    update_allowed();
}
void MakeClientGrabImpervious(ClientPtr client)
{
    WmbConnection *c = wmb_server_connection(client);
    if (c) c->impervious = true;
    update_allowed();
}
void MakeClientGrabPervious(ClientPtr client)
{
    WmbConnection *c = wmb_server_connection(client);
    if (c) c->impervious = false;
    update_allowed();
}
void AvailableClientInput(ClientPtr client) { (void)client; fiber_notify(); }

void WmbServerConnections(void)
{
    unsigned int i;
    if (!wmb_bridge_is_server()) FatalError("WMB: client control outside server fiber\n");
    for (i = 0; i < WMB_CONNECTION_LIMIT; i++) {
        WmbConnection *c = wmb_connections[i];
        WmbOsConnection *os;
        ClientPtr client;
        xConnClientPrefix prefix = {0};
        const unsigned int endian = 1;
        if (!c) continue;
        wmb_connection_boundary(c);
        if (c->closing) {
            if (c->server_client) CloseDownClient(c->server_client);
            c->closed = true;
            c->closing = c->opening = false;
            continue;
        }
        if (!c->opening) continue;
        c->opening = false;
        os = calloc(1, sizeof(*os));
        if (!os) {
            wmb_connection_fail(c, "server connection allocation failed");
            continue;
        }
        os->os.fd = -1;
        os->connection = c;
        c->in_flight = true;
        client = NextAvailableClient(os);
        if (!client) {
            free(os);
            c->in_flight = false;
            wmb_connection_fail(c, "server client allocation failed");
            continue;
        }
        c->server_client = client;
        c->index = client->index;
        prefix.byteOrder = *(const unsigned char *)&endian ? 'l' : 'B';
        prefix.majorVersion = X_PROTOCOL;
        prefix.minorVersion = X_PROTOCOL_REVISION;
        if (!wmb_bytes_append(&c->pushed, &prefix, sizeof(prefix)))
            wmb_connection_fail(c, "setup prefix allocation failed");
    }
    update_allowed();
    fiber_notify();
}

int WmbServerReadyClients(int *ready)
{
    unsigned int i;
    int count = 0, priority = -2147483647;
    for (i = 0; i < WMB_CONNECTION_LIMIT; i++) {
        WmbConnection *c = wmb_connections[i];
        ClientPtr client;
        if (!c || !c->server_client || c->closed || c->ignored || !c->allowed ||
            (c->pushed_pos == c->pushed.size && c->input_pos == c->input_size))
            continue;
        client = c->server_client;
        if (client->priority > priority) {
            priority = client->priority;
            count = 0;
        }
        if (client->priority == priority) ready[count++] = client->index;
    }
    return count;
}

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
