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

/* Synthetic scancodes for modifiers.  These MUST match the SC_* values
 * in tinyx/kdrive/tima/tima_kdrive_kbd.c - the DDX indexes kdKeymap
 * with them directly, so a mismatch silently remaps modifier keys. */
#define HAL_SC_LSHIFT   112
#define HAL_SC_RSHIFT   113
#define HAL_SC_LCTRL    114
#define HAL_SC_RCTRL    115
#define HAL_SC_LALT     116
#define HAL_SC_RALT     117
#define HAL_SC_LSUPER   118
#define HAL_SC_RSUPER   119
#define HAL_SC_CAPSLOCK 120

typedef enum {
    HAL_INPUT_KEYBOARD = 0,
    HAL_INPUT_MOUSE    = 1,
    HAL_INPUT_DEV_MAX  = 2
} hal_input_dev_t;

int  hal_input_init(void);
void hal_input_shutdown(void);

/* Non-blocking byte-stream read.  Returns true and stores one byte if
 * the device has data; returns false otherwise.  Callers reassemble the
 * command/data pairs above. */
bool hal_input_has_data(hal_input_dev_t dev);
bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out);

/* Backend-specific configuration (cursor visibility, scaling, touch mode).
 * Ignored by backends that do not support it.  Returns 0 or negative. */
int  hal_input_control(hal_input_dev_t dev, const uint8_t *cmd, uint32_t len);

/* Advance the backend: drain hardware / windowing events into the byte
 * buffers.  Called from the `hal` fiber.  Must not block. */
void hal_input_pump(void);

#ifdef __cplusplus
}
#endif

#endif /* WMB_HAL_INPUT_H */
