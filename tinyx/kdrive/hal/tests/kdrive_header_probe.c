#include "kdrive.h"

_Static_assert(KD_MAX_BUTTON == (WMB_TEST_EXTENDED_BUTTONS ? 9 : 7),
               "KDrive button capacity must match the selected project variant");
_Static_assert(sizeof(((KdMouseInfo *)0)->buttonState) ==
               (WMB_TEST_EXTENDED_BUTTONS ? sizeof(unsigned long) : sizeof(unsigned char)),
               "KDrive button-state layout must match the selected project variant");

#if WMB_TEST_EXTENDED_BUTTONS
_Static_assert(KD_BUTTON_9 == 0x100, "The project variant must support button nine");
#else
#ifdef KD_BUTTON_9
#error "Project button definitions must not appear in the reference variant"
#endif
#endif
