#ifndef WMB_SERVER_CONNECTION_H
#define WMB_SERVER_CONNECTION_H
#include "wmb_bridge.h"
#include "kdrive.h"
#include "dixstruct.h"
#include "osdep.h"

typedef struct {
    OsCommRec os;
    WmbConnection *connection;
} WmbOsConnection;

static inline WmbConnection *wmb_server_connection(ClientPtr client)
{
    return client->osPrivate ? ((WmbOsConnection *)client->osPrivate)->connection : NULL;
}

void WmbServerConnections(void);
int WmbServerReadyClients(int *ready);
#endif
