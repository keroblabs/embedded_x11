/* SDL keyboard -> WMB HAL command/value pairs. */
#include "types.h"
#include "buffer.h"
#include "hal_drivers.h"
#include "sdl_input.h"

#define KEYPAD_BUFFER_SIZE 128

static buffer_data_t keypad_buffer_data;
static uint8_t keypad_buffer[KEYPAD_BUFFER_SIZE];

/* buffer.c overwrites its oldest *byte* when full.  Never enqueue half
 * a packet: reserve both bytes, otherwise drop the complete new packet.
 * With cooperative fibers, the capacity check and append cannot yield. */
static void keypad_emit(uint8_t cmd, uint8_t data)
{
    uint8_t pair[2] = { cmd, data };
    if (buffer_get_size(&keypad_buffer_data) <= KEYPAD_BUFFER_SIZE - 3u)
        buffer_write_buffer(&keypad_buffer_data, pair, 2);
}

void keypad_driver_event_key(uint32_t window_id, uint8_t key_code,
                              uint16_t control_key, uint8_t state)
{
    uint8_t val = 0;
    (void)window_id;
    if (key_code == 0) {
        keypad_emit(HAL_CMD_MODIFIER, (uint8_t)control_key);
        return;
    }
    if (key_code == 1) {
        switch (control_key) {
        case EVENT_KEY_BACKSPACE: val = HAL_KEY_BACKSPACE; break;
        case EVENT_KEY_ENTER:     val = HAL_KEY_ENTER; break;
        case EVENT_KEY_ESC:       val = HAL_KEY_ESCAPE; break;
        case EVENT_KEY_UP:        val = HAL_KEY_UP; break;
        case EVENT_KEY_DOWN:      val = HAL_KEY_DOWN; break;
        case EVENT_KEY_RIGHT:     val = HAL_KEY_RIGHT; break;
        case EVENT_KEY_LEFT:      val = HAL_KEY_LEFT; break;
        default: return;
        }
    } else if (key_code >= 0x20 && key_code <= 0x7d) {
        val = key_code;
    } else {
        return;
    }
    keypad_emit((state == KEY_DOWN) ? HAL_CMD_KEY_DOWN : HAL_CMD_KEY_UP, val);
}

void keypad_driver_init(void)
{
    buffer_init(&keypad_buffer_data, keypad_buffer, KEYPAD_BUFFER_SIZE);
}
bool_t keypad_driver_has_data(void)
{
    return buffer_is_empty(&keypad_buffer_data) ? FALSE : TRUE;
}
uint8_t keypad_driver_read_data(void)
{
    return buffer_read(&keypad_buffer_data);
}
