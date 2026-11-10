#include "wmb_connection.h"
#include "opaque.h"
#include "hal_task.h"

extern void WmbRunTimers(void);
extern CARD32 WmbNextTimerDelay(void);

int WaitForSomething(int *clients_ready)
{
    struct timeval delay;
    struct timeval *timeout = &delay;
    CARD32 milliseconds;
    WmbServerConnections();
    if (workQueue != NULL) ProcessWorkQueue();
    WmbRunTimers();
    milliseconds = WmbNextTimerDelay();
    delay.tv_sec = milliseconds / 1000;
    delay.tv_usec = (milliseconds % 1000) * 1000;
    BlockHandler(&timeout, NULL);
    if (dispatchException) return 0;
    milliseconds = timeout == NULL ? 20 :
        (CARD32)(timeout->tv_sec * 1000 + (timeout->tv_usec + 999) / 1000);
    (void)fiber_wait_for(wmb_bridge_has_work, NULL, milliseconds);
    WakeupHandler(0, NULL);
    WmbRunTimers();
    WmbServerConnections();
    return WmbServerReadyClients(clients_ready);
}
