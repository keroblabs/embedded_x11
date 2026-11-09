#ifndef KDRIVE_TEST_MOCK_XSERVER_H
#define KDRIVE_TEST_MOCK_XSERVER_H

#include "kdrive_hal.h"
#include "inputstr.h"
#include "damage.h"

typedef struct
{
    ScreenRec screen;
    PixmapRec pixmap;
    DevUnion privates[2];
    KdPrivScreenRec screen_private;
    DeviceIntRec keyboard, pointer;
    KeyClassRec key;
    KbdFeedbackClassRec kbdfeed;
    PtrFeedbackClassRec ptrfeed;
    ButtonClassRec button;
    xEvent events[2048];
    size_t event_count;
    int pointer_x, pointer_y, last_dx, last_dy;
    unsigned long adjusted_wait;
    unsigned int errors, damage_registers, damage_destroys;
    CARD32 time;
    bool fail_pointer_init, fail_keyboard_init, fail_damage_setup, fail_damage_create;
    DamagePtr damage;
} MockXServerState;

extern MockXServerState mock_xserver;
extern long mock_calloc_failure_after;
void MockXServerReset(KdCardInfo *card, KdScreenInfo *screen);
void MockXServerStartDevices(void);
void MockXServerStopDevices(void);
void MockXServerReportDamage(RegionPtr region);

#endif
