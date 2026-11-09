#include "kdrive.h"
#include "opaque.h"
#include "inputstr.h"
#include "dpmsproc.h"
#define DPMS_SERVER
#include <X11/extensions/dpms.h>

struct _OsTimerRec {
    OsTimerPtr next;
    CARD32 expires, delta;
    OsTimerCallback callback;
    pointer arg;
};
static OsTimerPtr timers, screen_saver_timer;
extern Bool screenSaverSuspended;

void TimerCancel(OsTimerPtr timer)
{
    OsTimerPtr *previous;
    for (previous = &timers; *previous; previous = &(*previous)->next)
        if (*previous == timer) {
            *previous = timer->next;
            timer->next = NULL;
            break;
        }
}

void TimerFree(OsTimerPtr timer)
{
    if (timer == NULL) return;
    TimerCancel(timer);
    free(timer);
}

static void run_timer(OsTimerPtr timer, CARD32 now)
{
    CARD32 next;
    TimerCancel(timer);
    next = timer->callback(timer, now, timer->arg);
    if (next != 0) TimerSet(timer, 0, next, timer->callback, timer->arg);
}

OsTimerPtr TimerSet(OsTimerPtr timer, int flags, CARD32 millis,
                   OsTimerCallback callback, pointer argument)
{
    OsTimerPtr *previous;
    CARD32 now = GetTimeInMillis();
    if (timer == NULL) {
        timer = calloc(1, sizeof(*timer));
        if (timer == NULL) return NULL;
    } else {
        TimerCancel(timer);
        if ((flags & TimerForceOld) && timer->callback)
            timer->callback(timer, now, timer->arg);
    }
    if (millis == 0) return timer;
    timer->delta = flags & TimerAbsolute ? millis - now : millis;
    timer->expires = flags & TimerAbsolute ? millis : now + millis;
    timer->callback = callback;
    timer->arg = argument;
    if ((INT32)(timer->expires - now) <= 0) {
        millis = callback(timer, now, argument);
        if (millis == 0) return timer;
        timer->delta = millis;
        timer->expires = now + millis;
    }
    for (previous = &timers; *previous &&
         (INT32)((*previous)->expires - timer->expires) <= 0;
         previous = &(*previous)->next) {}
    timer->next = *previous;
    *previous = timer;
    return timer;
}

Bool TimerForce(OsTimerPtr timer)
{
    OsTimerPtr current;
    for (current = timers; current; current = current->next)
        if (current == timer) {
            run_timer(timer, GetTimeInMillis());
            return TRUE;
        }
    return FALSE;
}

void TimerInit(void)
{
    while (timers) TimerFree(timers);
    screen_saver_timer = NULL;
}

void WmbRunTimers(void)
{
    CARD32 now = GetTimeInMillis();
    while (timers && (INT32)(timers->expires - now) <= 0)
        run_timer(timers, now);
}

CARD32 WmbNextTimerDelay(void)
{
    INT32 delay = timers ? (INT32)(timers->expires - GetTimeInMillis()) : 20;
    return delay < 0 ? 0 : delay > 20 ? 20 : (CARD32)delay;
}

static CARD32 screen_saver_expired(OsTimerPtr timer, CARD32 now, pointer argument)
{
    CARD32 elapsed = now - lastDeviceEventTime.milliseconds;
    CARD32 next = 0;
    (void)timer; (void)argument;
    if (DPMSEnabled) {
        if (DPMSStandbyTime && elapsed >= DPMSStandbyTime &&
            DPMSPowerLevel < DPMSModeStandby) DPMSSet(DPMSModeStandby);
        if (DPMSSuspendTime && elapsed >= DPMSSuspendTime &&
            DPMSPowerLevel < DPMSModeSuspend) DPMSSet(DPMSModeSuspend);
        if (DPMSOffTime && elapsed >= DPMSOffTime &&
            DPMSPowerLevel < DPMSModeOff) DPMSSet(DPMSModeOff);
        if (DPMSPowerLevel < DPMSModeStandby && DPMSStandbyTime > elapsed)
            next = DPMSStandbyTime - elapsed;
        else if (DPMSPowerLevel < DPMSModeSuspend && DPMSSuspendTime > elapsed)
            next = DPMSSuspendTime - elapsed;
        else if (DPMSPowerLevel < DPMSModeOff && DPMSOffTime > elapsed)
            next = DPMSOffTime - elapsed;
    }
    if (DPMSPowerLevel != DPMSModeOn || ScreenSaverTime == 0) return next;
    if (elapsed < (CARD32)ScreenSaverTime) {
        CARD32 saver = (CARD32)ScreenSaverTime - elapsed;
        return next && next < saver ? next : saver;
    }
    ResetOsBuffers();
    SaveScreens(SCREEN_SAVER_ON, ScreenSaverActive);
    if (ScreenSaverInterval > 0 && (!next || (CARD32)ScreenSaverInterval < next))
        next = ScreenSaverInterval;
    return next;
}

void FreeScreenSaverTimer(void)
{
    TimerFree(screen_saver_timer);
    screen_saver_timer = NULL;
}

void SetScreenSaverTimer(void)
{
    CARD32 delay = 0;
    if (DPMSEnabled)
        delay = DPMSStandbyTime ? DPMSStandbyTime :
                DPMSSuspendTime ? DPMSSuspendTime : DPMSOffTime;
    if (ScreenSaverTime > 0 && (!delay || (CARD32)ScreenSaverTime < delay))
        delay = ScreenSaverTime;
    if (delay && !screenSaverSuspended)
        screen_saver_timer = TimerSet(screen_saver_timer, 0, delay,
                                     screen_saver_expired, NULL);
    else FreeScreenSaverTimer();
}
