#include "kdrive.h"
#include "inputstr.h"
#include "opaque.h"
#include "window.h"
#include <stdio.h>
#define DPMS_SERVER
#include <X11/extensions/dpms.h>

#define CHECK(value) do { \
    if (!(value)) { fprintf(stderr, "Timer FAIL line %d: %s\n", __LINE__, #value); return 1; } \
} while (0)

extern void WmbRunTimers(void);
extern CARD32 WmbNextTimerDelay(void);

static CARD32 now;
static int calls, saver_calls, power_calls;
CARD32 ScreenSaverTime, ScreenSaverInterval;
Bool screenSaverSuspended, DPMSEnabled;
CARD32 DPMSStandbyTime, DPMSSuspendTime, DPMSOffTime;
CARD16 DPMSPowerLevel;
TimeStamp lastDeviceEventTime;

CARD32 GetTimeInMillis(void) { return now; }
void ResetOsBuffers(void) {}
void SaveScreens(int on, int mode) { (void)on; (void)mode; saver_calls++; }
void DPMSSet(int level) { DPMSPowerLevel = level; power_calls++; }

static CARD32 callback(OsTimerPtr timer, CARD32 time, pointer argument)
{
    int *count = argument;
    (void)timer; (void)time;
    (*count)++;
    return 0;
}

static CARD32 periodic(OsTimerPtr timer, CARD32 time, pointer argument)
{
    callback(timer, time, argument);
    return calls < 3 ? 5 : 0;
}

int main(void)
{
    OsTimerPtr timer, later;
    int other = 0;
    now = 100;
    timer = TimerSet(NULL, 0, 5, callback, &calls);
    later = TimerSet(NULL, 0, 10, callback, &other);
    CHECK(timer && later && WmbNextTimerDelay() == 5);
    now += 5;
    WmbRunTimers();
    CHECK(calls == 1 && other == 0 && WmbNextTimerDelay() == 5);
    CHECK(TimerForce(later) && other == 1);
    CHECK(!TimerForce(later));
    TimerFree(timer);
    TimerFree(later);

    calls = 0;
    now = UINT32_MAX - 2;
    timer = TimerSet(NULL, 0, 5, periodic, &calls);
    CHECK(timer && WmbNextTimerDelay() == 5);
    now += 5;
    WmbRunTimers();
    CHECK(calls == 1 && WmbNextTimerDelay() == 5);
    now += 5;
    WmbRunTimers();
    now += 5;
    WmbRunTimers();
    CHECK(calls == 3 && WmbNextTimerDelay() == 20);
    TimerFree(timer);

    calls = 0;
    timer = TimerSet(NULL, TimerAbsolute, now + 7, callback, &calls);
    CHECK(WmbNextTimerDelay() == 7);
    timer = TimerSet(timer, TimerForceOld, 2, callback, &calls);
    CHECK(calls == 1 && WmbNextTimerDelay() == 2);
    TimerCancel(timer);
    now += 10;
    WmbRunTimers();
    CHECK(calls == 1);
    TimerFree(timer);

    now = 0;
    ScreenSaverTime = 10;
    ScreenSaverInterval = 3;
    SetScreenSaverTimer();
    now = 10;
    WmbRunTimers();
    CHECK(saver_calls == 1 && WmbNextTimerDelay() == 3);
    now = 13;
    WmbRunTimers();
    CHECK(saver_calls == 2);
    FreeScreenSaverTimer();

    now = 0;
    ScreenSaverTime = 0;
    DPMSEnabled = TRUE;
    DPMSStandbyTime = 5;
    DPMSSuspendTime = 10;
    DPMSOffTime = 15;
    SetScreenSaverTimer();
    now = 5;
    WmbRunTimers();
    CHECK(DPMSPowerLevel == DPMSModeStandby && WmbNextTimerDelay() == 5);
    now = 10;
    WmbRunTimers();
    CHECK(DPMSPowerLevel == DPMSModeSuspend && WmbNextTimerDelay() == 5);
    now = 15;
    WmbRunTimers();
    CHECK(DPMSPowerLevel == DPMSModeOff && power_calls == 3);
    FreeScreenSaverTimer();
    TimerInit();
    puts("PASS TinyX timers: deadlines, wrap, repeat, force, cancellation, saver, DPMS");
    return 0;
}
