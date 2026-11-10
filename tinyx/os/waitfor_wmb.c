#include "wmb_connection.h"
#include "opaque.h"
#include "hal_task.h"

extern void WmbRunTimers(void);
extern CARD32 WmbNextTimerDelay(void);

int WaitForSomething(int *clients_ready)
{
    CARD32 milliseconds;
    WmbServerConnections();
    if (workQueue != NULL) ProcessWorkQueue();
    WmbRunTimers();
    milliseconds = WmbNextTimerDelay();
    BlockHandler(&milliseconds, NULL);
    if (dispatchException) return 0;
    (void)fiber_wait_for(wmb_bridge_has_work, NULL, milliseconds);
    WakeupHandler(0, NULL);
    WmbRunTimers();
    WmbServerConnections();
    return WmbServerReadyClients(clients_ready);
}
