/*
 * hal_input.h - Platform contract: keyboard and pointer.
 *
 * docs/12-platform-contract.md section 5.  Must compile standalone
 * under -ffreestanding -nostdinc with only <stdint.h> / <stdbool.h>
 * (gate PC-V1).
 *
 * Per rule PC5 the target's command/data byte protocol IS the contract:
 * this header owns the constants and each platform backend includes
 * them back.
 */
#ifndef WMB_HAL_INPUT_H
#define WMB_HAL_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Command bytes.  Each is followed by exactly one data byte. */
#define HAL_CMD_MODIFIER        0x01   /* + modifier mask       */
#define HAL_CMD_KEY_DOWN        0x02   /* + key data            */
#define HAL_CMD_KEY_UP          0x03   /* + key data            */
#define HAL_CMD_MOUSE_X         0x04   /* + int8 dx             */
#define HAL_CMD_MOUSE_Y         0x05   /* + int8 dy             */
#define HAL_CMD_MOUSE_Z         0x06   /* + z / pressure        */
#define HAL_CMD_MOUSE_BUTTON    0x07   /* + button id           */
#define HAL_CMD_MOUSE_WHEEL_X   0x08   /* + int8                */
#define HAL_CMD_MOUSE_WHEEL_Y   0x09   /* + int8                */

/* Aggregate modifier mask carried by HAL_CMD_MODIFIER. */
#define HAL_MOD_SHIFT          0x01
#define HAL_MOD_CAPS           0x02
#define HAL_MOD_CTRL           0x04
#define HAL_MOD_ALT            0x08
#define HAL_MOD_ALTGR          0x10
#define HAL_MOD_SUPER          0x20

/* Key data values for non-printable keys */
#define HAL_KEY_BACKSPACE       127
#define HAL_KEY_ENTER            13
#define HAL_KEY_ESCAPE           27
#define HAL_KEY_LEFT              8
#define HAL_KEY_DOWN             10
#define HAL_KEY_UP               11
#define HAL_KEY_RIGHT            21

/* Button ids */
#define HAL_MOUSE_BUTTON_LEFT     1
#define HAL_MOUSE_BUTTON_MIDDLE   2
#define HAL_MOUSE_BUTTON_RIGHT    3
#define HAL_MOUSE_BUTTON_X1       4
#define HAL_MOUSE_BUTTON_X2       5

typedef enum {
    HAL_INPUT_KEYBOARD = 0,
    HAL_INPUT_MOUSE    = 1,
    HAL_INPUT_DEV_MAX  = 2
} hal_input_dev_t;

typedef enum {
    HAL_INPUT_CTRL_SCALE_Q12      = 1,
    HAL_INPUT_CTRL_CURSOR_VISIBLE = 2,
    HAL_INPUT_CTRL_POINTER_MODE   = 3
} hal_input_control_t;

typedef enum {
    HAL_POINTER_BUTTONS  = 0,
    HAL_POINTER_PRESSURE = 1
} hal_pointer_mode_t;

typedef enum {
    HAL_INPUT_CONTROL_OK          = 0,
    HAL_INPUT_CONTROL_UNSUPPORTED = -1,
    HAL_INPUT_CONTROL_INVALID     = -2,
    HAL_INPUT_CONTROL_FAILED      = -3
} hal_input_control_result_t;

#define HAL_INPUT_SCALE_ONE 4096

int  hal_input_init(void);
void hal_input_shutdown(void);

/* Non-blocking byte-stream read.  Returns true and stores one byte if
 * the device has data; returns false otherwise.  Callers reassemble the
 * command/data pairs above. */
bool hal_input_has_data(hal_input_dev_t dev);
bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out);

/* Optional controls, applied synchronously without waiting:
 * SCALE_Q12: signed multiplier, HAL_INPUT_SCALE_ONE is 1x.
 * CURSOR_VISIBLE: 0 hides, 1 shows.
 * POINTER_MODE: a hal_pointer_mode_t value selects buttons or pressure.
 * Return one of HAL_INPUT_CONTROL_*. Unsupported controls do not change
 * device state. No backend-specific bytes or native-endian payloads cross
 * this interface. */
int  hal_input_control(hal_input_dev_t dev, hal_input_control_t control,
                       int32_t value);

/* Collect device events into the byte buffers without waiting.
 * Device presentation/status is handled separately by hal_display_pump.
 * Call pumps and reads from serialized task/main-loop context, not an ISR. */
void hal_input_pump(void);

#ifdef __cplusplus
}
#endif

#endif /* WMB_HAL_INPUT_H */
