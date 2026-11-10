#ifndef WMB_KDRIVE_HAL_H
#define WMB_KDRIVE_HAL_H

#if !defined(WMB_DEVEL)
#error "The WMB TinyX HAL port requires WMB_DEVEL to be defined"
#endif

#include "kdrive.h"
#include "hal_display.h"
#include "hal_input.h"

extern const KdCardFuncs KdHalCardFuncs;
extern const KdKeyboardFuncs KdHalKeyboardFuncs;
extern const KdMouseFuncs KdHalMouseFuncs;
extern const KdOsFuncs KdHalOsFuncs;

Bool KdHalInputInit(void);
void KdHalInputFini(void);
void KdHalKeyboardPoll(void);
void KdHalMousePoll(void);
hal_display_status_t KdHalDisplayStatus(void);
unsigned long KdHalPollCount(void);
void KdHalSetStopHandler(void (*handler)(hal_display_status_t, void *), void *argument);

#endif
