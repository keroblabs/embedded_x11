#ifndef KDRIVE_TEST_MOCK_HAL_H
#define KDRIVE_TEST_MOCK_HAL_H

#include "hal_display.h"
#include "hal_input.h"

typedef struct
{
    int32_t x, y;
    uint32_t width, height;
} MockFlush;

typedef struct
{
    unsigned int display_inits, display_shutdowns, input_inits, input_shutdowns;
    unsigned int input_pumps, display_pumps, flush_count;
    MockFlush flushes[64];
    bool fail_display_init, fail_input_init;
    int power_result, cursor_result, last_power;
    hal_display_status_t status;
} MockHalState;

extern MockHalState mock_hal;
void MockHalReset(hal_pixfmt_t format);
bool MockHalPush(hal_input_dev_t dev, const uint8_t *bytes, size_t size);
hal_framebuffer_t *MockHalFramebuffer(void);

#endif
