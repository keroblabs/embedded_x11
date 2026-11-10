#include "kdrive.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

extern int wmb_xlib_profile_test(void);
extern int wmb_font_profile_test(void);

#define CHECK(value) do { \
    if (!(value)) { fprintf(stderr, "P6 portability FAIL %d: %s\n", __LINE__, #value); return 1; } \
} while (0)

int main(void)
{
    CARD32 delay = 20;

    CHECK(GetTimeInMillis() == 1234);
    AdjustWaitForDelay(&delay, 50);
    CHECK(delay == 20);
    AdjustWaitForDelay(&delay, 7);
    CHECK(delay == 7);
    AdjustWaitForDelay(&delay, ULONG_MAX);
    CHECK(delay == 7);
    AdjustWaitForDelay(&delay, 0);
    CHECK(delay == 0);
    CHECK(wmb_font_profile_test() == 0);
    CHECK(wmb_xlib_profile_test() == 0);
    puts("PASS P6 detachability: whole windowing archives, no backend, portable waits and client profile");
    return 0;
}
