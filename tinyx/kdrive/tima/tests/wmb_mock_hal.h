#ifndef WMB_KDRIVE_MOCK_HAL_H
#define WMB_KDRIVE_MOCK_HAL_H
#include <stdint.h>
#include "hal_input.h"

/* The link test uses this mock, never SDL. */
void wmb_mock_push(hal_input_dev_t dev, uint8_t command, uint8_t value);
uint32_t wmb_mock_flush_count(void);
#endif
