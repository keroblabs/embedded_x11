#ifndef WMB_HOST_DESKTOP_H
#define WMB_HOST_DESKTOP_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool done, idle, release;
    int result;
    unsigned cancellation;
    uint32_t first_window, first_frame;
} WmbDesktopTest;

int wmb_host_desktop(bool test, bool instrumented, uint32_t idle_ms, unsigned cancellation);
void wmb_wm_test(void *argument);
#endif
