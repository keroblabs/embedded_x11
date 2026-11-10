#ifndef WMB_JWM_PORT_H
#define WMB_JWM_PORT_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *configuration;
    bool ready, finished;
    int result;
    uint64_t started_us, ready_us, loop_count, last_loop_us, max_loop_gap_us;
    uint64_t callbacks, last_callback_us, max_callback_gap_us;
    uint64_t clock_updates, last_clock, last_clock_us, max_clock_gap_us, min_clock_gap_us;
    unsigned protocol_errors;
} WmbJwmState;

void WmbJwm_Run(void *argument);
void WmbJwm_RequestStop(void);
void WmbJwm_Ready(void);
void WmbJwm_Pulse(void);
void WmbJwm_Wait(uint32_t milliseconds);
void WmbJwm_InstallErrorHandler(void);
const char *WmbJwm_Configuration(void);
_Noreturn void WmbJwm_Terminate(int result);
extern const char WmbJwm_DefaultConfiguration[];
const WmbJwmState *WmbJwm_Status(void);
uint64_t WmbJwm_UptimeSeconds(void);
void WmbJwm_ClockFormatted(uint64_t seconds);
void WmbJwm_ClockDrawn(void);
void WmbJwm_TestGrab(bool held);
bool WmbJwm_TestGrabHeld(void);
void WmbJwm_ResetMetrics(void);
#endif
