/*
 * SDL pointer -> command/value stream.  The button value is a held-state
 * bitmask, not an individual SDL button id (see hal_input.h).
 */
#include "types.h"
#include "buffer.h"
#include "hal_drivers.h"

#define MOUSE_BUFFER_SIZE 256
#define MOUSE_CMD_BUFFER_SIZE 128
#define MOUSE_MAX_DEV 2
#define SDL_MOUSE_INDEX 0
#define SCALE_RANGE 4096

typedef struct {
    buffer_data_t output;
    uint8_t output_bytes[MOUSE_BUFFER_SIZE];
    buffer_data_t commands;
    uint8_t command_bytes[MOUSE_CMD_BUFFER_SIZE];
    int32_t scale_mul;
    bool_t touch_mode;
    uint8_t buttons;
} mouse_drv_t;

static mouse_drv_t mice[MOUSE_MAX_DEV];

static bool_t mouse_emit(mouse_drv_t *m, uint8_t cmd, uint8_t value)
{
    uint8_t pair[2] = { cmd, value };
    /* The ring keeps one byte empty; don't overwrite half a pair. */
    if (buffer_get_size(&m->output) > MOUSE_BUFFER_SIZE - 3u)
        return FALSE;
    buffer_write_buffer(&m->output, pair, 2);
    return TRUE;
}

static int8_t clamp_delta(int32_t v)
{
    if (v < -127) return -127;
    if (v > 127) return 127;
    return (int8_t)v;
}

void mouse_driver_event_mouse(uint32_t window_id, int32_t dx, int32_t dy,
                               uint16_t pressure, uint8_t button_mask)
{
    mouse_drv_t *m = &mice[SDL_MOUSE_INDEX];
    int8_t x = clamp_delta((dx * m->scale_mul) / SCALE_RANGE);
    int8_t y = clamp_delta((dy * m->scale_mul) / SCALE_RANGE);
    uint8_t mask = button_mask & HAL_MOUSE_MASK_ALL;
    (void)window_id;
    mouse_emit(m, HAL_CMD_MOUSE_X, (uint8_t)x);
    mouse_emit(m, HAL_CMD_MOUSE_Y, (uint8_t)y);
    if (m->touch_mode) {
        mouse_emit(m, HAL_CMD_MOUSE_Z, (uint8_t)pressure);
    } else if (mask != m->buttons) {
        if (mouse_emit(m, HAL_CMD_MOUSE_BUTTON, mask))
            m->buttons = mask;
    }
}

void mouse_driver_event_wheel(uint32_t window_id, int32_t x, int32_t y)
{
    mouse_drv_t *m = &mice[SDL_MOUSE_INDEX];
    (void)window_id;
    if (x) mouse_emit(m, HAL_CMD_MOUSE_WHEEL_X,
                      (uint8_t)clamp_delta(x));
    if (y) mouse_emit(m, HAL_CMD_MOUSE_WHEEL_Y,
                      (uint8_t)clamp_delta(y));
}

void mouse_driver_init(uint8_t index)
{
    mouse_drv_t *m;
    if (index >= MOUSE_MAX_DEV) return;
    m = &mice[index];
    memset(m, 0, sizeof(*m));
    buffer_init(&m->output, m->output_bytes, MOUSE_BUFFER_SIZE);
    buffer_init(&m->commands, m->command_bytes, MOUSE_CMD_BUFFER_SIZE);
    m->scale_mul = SCALE_RANGE;
}

bool_t mouse_driver_has_data(uint8_t index)
{
    if (index >= MOUSE_MAX_DEV) return FALSE;
    return buffer_is_empty(&mice[index].output) ? FALSE : TRUE;
}

uint8_t mouse_driver_read_data(uint8_t index)
{
    if (index >= MOUSE_MAX_DEV) return 0;
    return buffer_read(&mice[index].output);
}

void mouse_driver_process(void)
{
    uint8_t index;
    for (index = 0; index < MOUSE_MAX_DEV; ++index) {
        mouse_drv_t *m = &mice[index];
        while (buffer_get_size(&m->commands) >= 2) {
            uint8_t cmd = buffer_peek(&m->commands);
            if (cmd == MOUSE_CONFIG_SETTING) {
                uint8_t param;
                if (buffer_get_size(&m->commands) < 6) break;
                (void)buffer_read(&m->commands);
                param = buffer_read(&m->commands);
                if (param == MOUSE_SETTING_SCALE) {
                    (void)buffer_read_buffer(&m->commands,
                          (uint8_t *)&m->scale_mul, sizeof(m->scale_mul));
                } else {
                    uint8_t ignored[4];
                    (void)buffer_read_buffer(&m->commands, ignored, 4);
                }
            } else if (cmd == MOUSE_CONFIG_TOUCH_MODE) {
                (void)buffer_read(&m->commands);
                m->touch_mode = buffer_read(&m->commands) != 0;
            } else if (cmd == MOUSE_CONFIG_CURSOR_SHOW) {
                /* SDL_ShowCursor is backend-internal, configured here. */
                uint8_t visible;
                (void)buffer_read(&m->commands);
                visible = buffer_read(&m->commands);
                (void)visible;
            } else {
                (void)buffer_read(&m->commands);
            }
        }
    }
}

void mouse_driver_write_command(uint8_t index, const uint8_t *data, uint32_t len)
{
    mouse_drv_t *m;
    if (index >= MOUSE_MAX_DEV || !data) return;
    m = &mice[index];
    if (len > MOUSE_CMD_BUFFER_SIZE - 1u - buffer_get_size(&m->commands))
        return;  /* Drop command atomically rather than truncate. */
    buffer_write_buffer(&m->commands, (uint8_t *)data, (uint16_t)len);
}
