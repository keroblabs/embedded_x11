#include <SDL.h>
#include "hal_input.h"
#include "sdl_backend.h"
#include "sdl_input.h"
#include "sdl_input_queue.h"
#include "sdl_internal.h"

static sdl_input_queue_t keyboard_queue;
static sdl_input_queue_t pointer_queue;
static bool is_open;
static int32_t pointer_scale;
static hal_pointer_mode_t pointer_mode;
static int saved_cursor_state;

static sdl_input_queue_t *device_queue(hal_input_dev_t dev)
{
    switch (dev)
    {
        case HAL_INPUT_KEYBOARD: return &keyboard_queue;
        case HAL_INPUT_MOUSE: return &pointer_queue;
        default:
            SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Invalid HAL input device %d", dev);
            return NULL;
    }
}

static void enqueue(sdl_input_queue_t *queue, const uint8_t *bytes, size_t size)
{
    if (sdl_input_queue_write(queue, bytes, size) < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "HAL input queue full; dropping a complete new event");
    }
}

void sdl_backend_key_event(uint32_t window, uint8_t key, uint16_t control, uint8_t state)
{
    uint8_t packet[2];
    (void)window;
    if (!is_open) return;
    if (state != KEY_DOWN && state != KEY_UP)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Invalid keyboard state %u", state);
        return;
    }
    if (key == 0)
    {
        if ((control & ~(HAL_MOD_SHIFT | HAL_MOD_CAPS | HAL_MOD_CTRL |
                         HAL_MOD_ALT | HAL_MOD_ALTGR | HAL_MOD_SUPER)) != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Invalid keyboard modifier mask %u", control);
            return;
        }
        packet[0] = HAL_CMD_MODIFIER;
        packet[1] = (uint8_t)control;
    }
    else
    {
        packet[0] = state == KEY_DOWN ? HAL_CMD_KEY_DOWN : HAL_CMD_KEY_UP;
        if (key == 1)
        {
            switch (control)
            {
                case EVENT_KEY_BACKSPACE: packet[1] = HAL_KEY_BACKSPACE; break;
                case EVENT_KEY_ENTER: packet[1] = HAL_KEY_ENTER; break;
                case EVENT_KEY_ESC: packet[1] = HAL_KEY_ESCAPE; break;
                case EVENT_KEY_UP: packet[1] = HAL_KEY_UP; break;
                case EVENT_KEY_DOWN: packet[1] = HAL_KEY_DOWN; break;
                case EVENT_KEY_RIGHT: packet[1] = HAL_KEY_RIGHT; break;
                case EVENT_KEY_LEFT: packet[1] = HAL_KEY_LEFT; break;
                default:
                    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "No HAL key encoding for control %u", control);
                    return;
            }
        }
        else if (key >= 0x20 && key <= 0x7E) packet[1] = key;
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Unsupported keyboard byte %u", key);
            return;
        }
    }
    enqueue(&keyboard_queue, packet, sizeof(packet));
}

static uint8_t scaled_delta(int32_t delta)
{
    int64_t scaled = ((int64_t)delta * pointer_scale) / HAL_INPUT_SCALE_ONE;
    if (scaled < INT8_MIN || scaled > INT8_MAX)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Scaled pointer delta exceeds the int8 protocol; clipping");
        scaled = scaled < INT8_MIN ? INT8_MIN : INT8_MAX;
    }
    return (uint8_t)(int8_t)scaled;
}

void sdl_backend_pointer_event(uint32_t window, int32_t dx, int32_t dy, uint16_t pressure, uint8_t button)
{
    uint8_t packet[6];
    (void)window;
    if (!is_open) return;
    if (button > HAL_MOUSE_BUTTON_X2)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Invalid pointer button id %u", button);
        return;
    }
    if (pointer_mode == HAL_POINTER_PRESSURE && pressure > UINT8_MAX)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Pointer pressure exceeds the byte protocol; clipping");
        pressure = UINT8_MAX;
    }
    packet[0] = HAL_CMD_MOUSE_X;
    packet[1] = scaled_delta(dx);
    packet[2] = HAL_CMD_MOUSE_Y;
    packet[3] = scaled_delta(dy);
    packet[4] = pointer_mode == HAL_POINTER_PRESSURE ? HAL_CMD_MOUSE_Z : HAL_CMD_MOUSE_BUTTON;
    packet[5] = pointer_mode == HAL_POINTER_PRESSURE ? (uint8_t)pressure : button;
    enqueue(&pointer_queue, packet, sizeof(packet));
}

void sdl_backend_wheel_event(uint32_t window, int32_t x, int32_t y)
{
    uint8_t packet[4];
    (void)window;
    if (!is_open) return;
    packet[0] = HAL_CMD_MOUSE_WHEEL_X;
    packet[1] = scaled_delta(x);
    packet[2] = HAL_CMD_MOUSE_WHEEL_Y;
    packet[3] = scaled_delta(y);
    enqueue(&pointer_queue, packet, sizeof(packet));
}

int hal_input_init(void)
{
    if (is_open)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "HAL input is already initialized");
        return -1;
    }
    if (sdl_backend_acquire() < 0) return -1;
    saved_cursor_state = SDL_ShowCursor(SDL_QUERY);
    if (saved_cursor_state < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Cannot query pointer cursor state: %s", SDL_GetError());
        sdl_backend_release();
        return -1;
    }
    sdl_input_queue_reset(&keyboard_queue);
    sdl_input_queue_reset(&pointer_queue);
    pointer_scale = HAL_INPUT_SCALE_ONE;
    pointer_mode = HAL_POINTER_BUTTONS;
    is_open = true;
    return 0;
}

void hal_input_shutdown(void)
{
    uint32_t window;
    if (!is_open) return;
    is_open = false;
    sdl_input_queue_reset(&keyboard_queue);
    sdl_input_queue_reset(&pointer_queue);
    window = sdl_backend_display_window();
    sdl_input_release_window(window);
    sdl_mouse_release_window(window);
    if (SDL_ShowCursor(saved_cursor_state) < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Cannot restore pointer cursor state: %s", SDL_GetError());
    }
    sdl_backend_release();
}

bool hal_input_has_data(hal_input_dev_t dev)
{
    sdl_input_queue_t *queue = device_queue(dev);
    return is_open && queue != NULL && queue->size != 0;
}

bool hal_input_read_byte(hal_input_dev_t dev, uint8_t *out)
{
    sdl_input_queue_t *queue = device_queue(dev);
    if (out == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "HAL input output pointer is NULL");
        return false;
    }
    if (!is_open || queue == NULL || queue->size == 0) return false;
    *out = sdl_input_queue_read(queue);
    return true;
}

int hal_input_control(hal_input_dev_t dev, hal_input_control_t control, int32_t value)
{
    if (device_queue(dev) == NULL) return HAL_INPUT_CONTROL_INVALID;
    if (!is_open)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "HAL input control requires an initialized device");
        return HAL_INPUT_CONTROL_FAILED;
    }
    if (control != HAL_INPUT_CTRL_SCALE_Q12 && control != HAL_INPUT_CTRL_CURSOR_VISIBLE &&
        control != HAL_INPUT_CTRL_POINTER_MODE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Invalid HAL input control %d", control);
        return HAL_INPUT_CONTROL_INVALID;
    }
    if ((control == HAL_INPUT_CTRL_CURSOR_VISIBLE && value != 0 && value != 1) ||
        (control == HAL_INPUT_CTRL_POINTER_MODE && value != HAL_POINTER_BUTTONS && value != HAL_POINTER_PRESSURE))
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Invalid value %d for HAL input control %d", value, control);
        return HAL_INPUT_CONTROL_INVALID;
    }
    if (dev != HAL_INPUT_MOUSE) return HAL_INPUT_CONTROL_UNSUPPORTED;

    switch (control)
    {
        case HAL_INPUT_CTRL_SCALE_Q12:
            pointer_scale = value;
            return HAL_INPUT_CONTROL_OK;
        case HAL_INPUT_CTRL_POINTER_MODE:
            pointer_mode = (hal_pointer_mode_t)value;
            return HAL_INPUT_CONTROL_OK;
        case HAL_INPUT_CTRL_CURSOR_VISIBLE:
            if (SDL_ShowCursor(value ? SDL_ENABLE : SDL_DISABLE) < 0)
            {
                SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Pointer cursor control failed: %s", SDL_GetError());
                return HAL_INPUT_CONTROL_FAILED;
            }
            return HAL_INPUT_CONTROL_OK;
        default:
            return HAL_INPUT_CONTROL_INVALID;
    }
}

void hal_input_pump(void)
{
    if (is_open) (void)sdl_poll_events();
}
