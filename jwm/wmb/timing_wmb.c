#include "jwm.h"
#include "timing.h"
#include "hal_time.h"
#include "error.h"
#include <stdint.h>

void GetCurrentTime(TimeType *time)
{
    uint64_t milliseconds = hal_tick_us() / 1000;
    time->seconds = (unsigned long)(milliseconds / 1000);
    time->ms = (int)(milliseconds % 1000);
}

unsigned long GetTimeDifference(const TimeType *left, const TimeType *right)
{
    uint64_t a = (uint64_t)left->seconds * 1000 + (unsigned)left->ms;
    uint64_t b = (uint64_t)right->seconds * 1000 + (unsigned)right->ms;
    uint64_t difference = a > b ? a - b : b - a;
    return difference > 60000 ? 60000 : (unsigned long)difference;
}

const char *GetTimeString(const char *format, const char *zone)
{
    static char text[64];
    uint64_t seconds = WmbJwm_UptimeSeconds();
    if (zone || !format ||
        (strcmp(format, "%H:%M:%S") != 0 && strcmp(format, "%c") != 0)) {
        Warning("wall-clock formats/timezones are unsupported; use %%H:%%M:%%S uptime");
        return "unsupported time";
    }
    snprintf(text, sizeof(text), "%s%02llu:%02u:%02u",
             strcmp(format, "%c") == 0 ? "Uptime " : "",
             (unsigned long long)(seconds / 3600),
             (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60));
    WmbJwm_ClockFormatted(seconds);
    return text;
}
