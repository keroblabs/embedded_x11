#include "hal_display.h"
#include "hal_input.h"
#include "sdl_backend.h"
#include "sdl_input.h"
#include "sdl_mouse.h"
#include "sdl_main.h"
#include "sdl_window.h"
#include "sdl_input_queue.h"
#include "sdl_internal.h"
#include "../../apps/common/framebuffer_draw.h"

#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static unsigned int log_errors, log_warnings, fail_point, texture_updates;

void *__real_SDL_calloc(size_t count, size_t size);
SDL_Window *__real_SDL_CreateWindow(const char *name, int x, int y, int width, int height, Uint32 flags);
SDL_Renderer *__real_SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
SDL_Surface *__real_SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format);
SDL_Texture *__real_SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int width, int height);
int __real_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);

void *__wrap_SDL_calloc(size_t count, size_t size)
{
    if (fail_point == 1) { SDL_SetError("Injected allocation failure"); return NULL; }
    return __real_SDL_calloc(count, size);
}

SDL_Window *__wrap_SDL_CreateWindow(const char *name, int x, int y, int width, int height, Uint32 flags)
{
    if (fail_point == 2) { SDL_SetError("Injected window failure"); return NULL; }
    return __real_SDL_CreateWindow(name, x, y, width, height, flags);
}

SDL_Renderer *__wrap_SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    if (fail_point == 3) { SDL_SetError("Injected renderer failure"); return NULL; }
    return __real_SDL_CreateRenderer(window, index, flags);
}

SDL_Surface *__wrap_SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format)
{
    if (fail_point == 4) { SDL_SetError("Injected surface failure"); return NULL; }
    return __real_SDL_CreateRGBSurfaceWithFormat(flags, width, height, depth, format);
}

SDL_Texture *__wrap_SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int width, int height)
{
    if (fail_point == 5) { SDL_SetError("Injected texture failure"); return NULL; }
    return __real_SDL_CreateTexture(renderer, format, access, width, height);
}

int __wrap_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    texture_updates++;
    if (fail_point == 6) return SDL_SetError("Injected texture upload failure");
    return __real_SDL_UpdateTexture(texture, rect, pixels, pitch);
}

static void capture_log(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    (void)category;
    if (priority >= SDL_LOG_PRIORITY_ERROR) log_errors++;
    else if (priority >= SDL_LOG_PRIORITY_WARN) log_warnings++;
    fprintf(stderr, "SDL: %s\n", message);
}

static int open_display(void)
{
    CHECK(hal_display_init() == 0);
    CHECK(hal_display_get_fb() != NULL);
    return 0;
}

static PIXEL_TYPE_T *pixel_at(uint32_t x, uint32_t y)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    return (PIXEL_TYPE_T *)((uint8_t *)fb->pixels + (size_t)y * fb->stride) + x;
}

static int expect_byte(hal_input_dev_t dev, uint8_t expected)
{
    uint8_t value;
    CHECK(hal_input_read_byte(dev, &value));
    CHECK(value == expected);
    return 0;
}

static int expect_bytes(hal_input_dev_t dev, const uint8_t *expected, size_t size)
{
    size_t i;
    uint8_t value = 0xAA;
    for (i = 0; i < size; i++) CHECK(expect_byte(dev, expected[i]) == 0);
    CHECK(!hal_input_has_data(dev));
    CHECK(!hal_input_read_byte(dev, &value));
    CHECK(value == 0xAA);
    return 0;
}

static int push_key(uint32_t id, SDL_Keycode sym, SDL_Scancode scancode, uint16_t mods, bool down, uint8_t repeat)
{
    SDL_Event event = {0};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.windowID = id;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = repeat;
    event.key.keysym.sym = sym;
    event.key.keysym.scancode = scancode;
    event.key.keysym.mod = mods;
    CHECK(SDL_PushEvent(&event) == 1);
    return 0;
}

static int push_button(uint32_t id, uint8_t button, bool down)
{
    SDL_Event event = {0};
    event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.windowID = id;
    event.button.button = button;
    event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.button.x = event.button.y = 1;
    CHECK(SDL_PushEvent(&event) == 1);
    return 0;
}

static int push_motion(uint32_t id, int32_t x, int32_t y, int32_t dx, int32_t dy, uint32_t buttons)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEMOTION;
    event.motion.windowID = id;
    event.motion.x = x;
    event.motion.y = y;
    event.motion.xrel = dx;
    event.motion.yrel = dy;
    event.motion.state = buttons;
    CHECK(SDL_PushEvent(&event) == 1);
    return 0;
}

static int test_display(void)
{
    const hal_framebuffer_t *fb;
    gui_instance_t *instance;
    uint32_t id, colour;
    CHECK(hal_display_get_fb() == NULL);
    CHECK(hal_display_pump() == HAL_DISPLAY_ERROR);
    CHECK(open_display() == 0);
    CHECK(hal_display_init() < 0);
    fb = hal_display_get_fb();
    id = sdl_backend_display_window();
    instance = sdl_find_window(id);
    CHECK(instance != NULL && sdl_window_count() == 1);
    CHECK(fb->width == TEST_NATIVE_WIDTH && fb->height == TEST_NATIVE_HEIGHT);
    CHECK(fb->bpp == WMB_SDL_PIXEL_BITS && fb->stride == (uint32_t)instance->surface->pitch);
    CHECK(fb->stride >= fb->width * sizeof(PIXEL_TYPE_T) && fb->stride % 4u == 0);
    CHECK((uintptr_t)fb->pixels % 4u == 0);
    CHECK(fb->format == (WMB_SDL_PIXEL_BITS == 16 ? HAL_PIXFMT_RGB565 : HAL_PIXFMT_XRGB8888));
    CHECK(hal_display_set_power(0) == -1 && hal_display_set_power(1) == -1);
    CHECK(hal_display_pump() == HAL_DISPLAY_ACTIVE);
    {
        unsigned int updates = texture_updates;
        SDL_Event expose = {0};
        SDL_Delay(VSYNC_PERIOD_MS + 1);
        CHECK(hal_display_pump() == HAL_DISPLAY_ACTIVE);
        CHECK(texture_updates == updates);
        expose.type = SDL_WINDOWEVENT;
        expose.window.windowID = id;
        expose.window.event = SDL_WINDOWEVENT_EXPOSED;
        CHECK(SDL_PushEvent(&expose) == 1);
        CHECK(hal_display_pump() == HAL_DISPLAY_ACTIVE);
        CHECK(texture_updates == updates + 1);
        sdl_update_window(id);
        CHECK(texture_updates == updates + 2);
        SDL_Delay(VSYNC_PERIOD_MS + 1);
        sdl_window_process();
        CHECK(texture_updates == updates + 3);
    }

    CHECK(framebuffer_fill_rect(fb, fb->width - 1u, fb->height - 1u,
                                UINT32_MAX, UINT32_MAX, FRAMEBUFFER_RGB(255, 128, 64)) == 0);
    CHECK(*pixel_at(fb->width - 1u, fb->height - 1u) == WMB_SDL_RGB(255, 128, 64));
    CHECK(framebuffer_fill_rect(fb, UINT32_MAX, UINT32_MAX, 1, 1, 0) == 0);
    CHECK(framebuffer_fill_rect(fb, 0, 0, fb->width, fb->height, 0) == 0);
    sdl_update_window(id);
    CHECK(!instance->has_damage);

    *pixel_at(0, 0) = WMB_SDL_RGB(255, 0, 0);
    if (fb->width > 1) *pixel_at(1, 0) = WMB_SDL_RGB(0, 0, 255);
    hal_display_flush(-1, -1, 2, 2);
    CHECK(instance->has_damage && instance->dirty.x == 0 && instance->dirty.y == 0);
    CHECK(instance->dirty.w == 1 && instance->dirty.h == 1);
    hal_display_flush(INT32_MAX, INT32_MAX, UINT32_MAX, UINT32_MAX);
    CHECK(instance->dirty.w == 1 && instance->dirty.h == 1);
    instance->last_present_ms = SDL_GetTicks() - VSYNC_PERIOD_MS;
    CHECK(hal_display_pump() == HAL_DISPLAY_ACTIVE);
    CHECK(!instance->has_damage);
    {
        SDL_Rect rect = {0, 0, 1, 1};
        CHECK(SDL_RenderReadPixels(instance->renderer, &rect, SDL_PIXELFORMAT_RGB888, &colour, 4) == 0);
        CHECK((colour & 0xFFFFFFu) == 0xFF0000u);
        if (fb->width > 1)
        {
            rect.x = 1;
            CHECK(SDL_RenderReadPixels(instance->renderer, &rect, SDL_PIXELFORMAT_RGB888, &colour, 4) == 0);
            CHECK((colour & 0xFFFFFFu) == 0);
        }
    }
    hal_display_flush(-5, -5, 2, 2);
    hal_display_flush(0, 0, 0, 0);
    CHECK(!instance->has_damage);
    hal_display_flush(INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
    CHECK(instance->dirty.w == (int)fb->width && instance->dirty.h == (int)fb->height);
    sdl_update_window(id);
    hal_display_flush(0, 0, 1, 1);
    hal_display_flush((int32_t)fb->width - 1, (int32_t)fb->height - 1, 1, 1);
    CHECK(instance->dirty.w == (int)fb->width && instance->dirty.h == (int)fb->height);

    fail_point = 6;
    instance->last_present_ms = SDL_GetTicks() - VSYNC_PERIOD_MS;
    CHECK(hal_display_pump() == HAL_DISPLAY_ERROR);
    CHECK(hal_display_get_fb()->pixels == fb->pixels);
    fail_point = 0;
    CHECK(hal_display_pump() == HAL_DISPLAY_ERROR);
    hal_display_shutdown();
    CHECK(open_display() == 0);
    fb = hal_display_get_fb();
    {
        SDL_Event event = {0};
        void *pixels = fb->pixels;
        event.type = SDL_QUIT;
        CHECK(SDL_PushEvent(&event) == 1);
        CHECK(hal_display_pump() == HAL_DISPLAY_STOP_REQUESTED);
        CHECK(hal_display_get_fb()->pixels == pixels);
        *pixel_at(0, 0) = 19;
        CHECK(*pixel_at(0, 0) == 19);
    }
    hal_display_shutdown();
    hal_display_shutdown();
    CHECK(hal_display_get_fb() == NULL && sdl_window_count() == 0);
    CHECK(open_display() == 0);
    {
        SDL_Event event = {0};
        event.type = SDL_WINDOWEVENT;
        event.window.windowID = sdl_backend_display_window();
        event.window.event = SDL_WINDOWEVENT_CLOSE;
        CHECK(SDL_PushEvent(&event) == 1);
        CHECK(hal_display_pump() == HAL_DISPLAY_STOP_REQUESTED);
        CHECK(hal_display_get_fb() != NULL);
    }
    return 0;
}

static int test_keyboard(void)
{
    uint32_t id;
    size_t i;
    uint8_t state;
    unsigned int warnings;
    static const SDL_Keycode special[] = {SDLK_BACKSPACE, SDLK_RETURN, SDLK_ESCAPE, SDLK_UP, SDLK_DOWN, SDLK_RIGHT, SDLK_LEFT};
    static const uint8_t codes[] = {HAL_KEY_BACKSPACE, HAL_KEY_ENTER, HAL_KEY_ESCAPE, HAL_KEY_UP, HAL_KEY_DOWN, HAL_KEY_RIGHT, HAL_KEY_LEFT};
    const uint8_t chords[] = {
        HAL_CMD_MODIFIER, HAL_MOD_SHIFT, HAL_CMD_MODIFIER, HAL_MOD_SHIFT | HAL_MOD_CTRL,
        HAL_CMD_KEY_DOWN, 'a', HAL_CMD_KEY_DOWN, 'b', HAL_CMD_KEY_UP, 'a',
        HAL_CMD_MODIFIER, HAL_MOD_CTRL, HAL_CMD_KEY_UP, 'b', HAL_CMD_MODIFIER, 0
    };
    const uint8_t modifiers[] = {
        HAL_CMD_MODIFIER, HAL_MOD_ALT, HAL_CMD_MODIFIER, HAL_MOD_ALT | HAL_MOD_ALTGR,
        HAL_CMD_MODIFIER, HAL_MOD_ALT | HAL_MOD_ALTGR | HAL_MOD_SUPER,
        HAL_CMD_MODIFIER, HAL_MOD_ALT | HAL_MOD_SUPER, HAL_CMD_MODIFIER, HAL_MOD_SUPER, HAL_CMD_MODIFIER, 0
    };
    CHECK(open_display() == 0 && hal_input_init() == 0);
    CHECK(hal_input_init() < 0);
    id = sdl_backend_display_window();
    hal_input_pump();
    CHECK(push_key(id, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, KMOD_LSHIFT, true, 0) == 0);
    CHECK(push_key(id, SDLK_LCTRL, SDL_SCANCODE_LCTRL, KMOD_LSHIFT | KMOD_LCTRL, true, 0) == 0);
    CHECK(push_key(id, SDLK_a, SDL_SCANCODE_A, KMOD_LSHIFT | KMOD_LCTRL, true, 0) == 0);
    CHECK(push_key(id, SDLK_b, SDL_SCANCODE_B, KMOD_LSHIFT | KMOD_LCTRL, true, 0) == 0);
    CHECK(push_key(id, SDLK_a, SDL_SCANCODE_A, KMOD_LSHIFT | KMOD_LCTRL, false, 0) == 0);
    CHECK(push_key(id, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, KMOD_LCTRL, false, 0) == 0);
    CHECK(push_key(id, SDLK_b, SDL_SCANCODE_B, KMOD_LCTRL, false, 0) == 0);
    CHECK(push_key(id, SDLK_LCTRL, SDL_SCANCODE_LCTRL, 0, false, 0) == 0);
    hal_input_pump();
    CHECK(expect_bytes(HAL_INPUT_KEYBOARD, chords, sizeof(chords)) == 0);

    CHECK(push_key(id, SDLK_LALT, SDL_SCANCODE_LALT, 0, true, 0) == 0);
    CHECK(push_key(id, SDLK_RALT, SDL_SCANCODE_RALT, KMOD_LALT, true, 0) == 0);
    CHECK(push_key(id, SDLK_LGUI, SDL_SCANCODE_LGUI, KMOD_LALT | KMOD_RALT, true, 0) == 0);
    CHECK(push_key(id, SDLK_RALT, SDL_SCANCODE_RALT, KMOD_LALT | KMOD_LGUI, false, 0) == 0);
    CHECK(push_key(id, SDLK_LALT, SDL_SCANCODE_LALT, KMOD_LGUI, false, 0) == 0);
    CHECK(push_key(id, SDLK_LGUI, SDL_SCANCODE_LGUI, 0, false, 0) == 0);
    hal_input_pump();
    CHECK(expect_bytes(HAL_INPUT_KEYBOARD, modifiers, sizeof(modifiers)) == 0);
    CHECK(push_key(id, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, KMOD_LSHIFT, true, 0) == 0);
    CHECK(push_key(id, SDLK_RSHIFT, SDL_SCANCODE_RSHIFT, KMOD_SHIFT, true, 0) == 0);
    CHECK(push_key(id, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, KMOD_RSHIFT, false, 0) == 0);
    CHECK(push_key(id, SDLK_RSHIFT, SDL_SCANCODE_RSHIFT, 0, false, 0) == 0);
    hal_input_pump();
    {
        const uint8_t expected[] = {HAL_CMD_MODIFIER, HAL_MOD_SHIFT, HAL_CMD_MODIFIER, 0};
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, expected, sizeof(expected)) == 0);
    }
    CHECK(push_key(id, SDLK_CAPSLOCK, SDL_SCANCODE_CAPSLOCK, KMOD_CAPS, true, 0) == 0);
    CHECK(push_key(id, SDLK_CAPSLOCK, SDL_SCANCODE_CAPSLOCK, KMOD_CAPS, false, 0) == 0);
    CHECK(push_key(id, SDLK_CAPSLOCK, SDL_SCANCODE_CAPSLOCK, 0, true, 0) == 0);
    CHECK(push_key(id, SDLK_CAPSLOCK, SDL_SCANCODE_CAPSLOCK, 0, false, 0) == 0);
    hal_input_pump();
    {
        const uint8_t expected[] = {HAL_CMD_MODIFIER, HAL_MOD_CAPS, HAL_CMD_MODIFIER, 0};
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, expected, sizeof(expected)) == 0);
    }
    for (i = 0; i < sizeof(special) / sizeof(special[0]); i++)
    {
        uint8_t expected[] = {HAL_CMD_KEY_DOWN, codes[i], HAL_CMD_KEY_UP, codes[i]};
        SDL_Scancode scancode = SDL_GetScancodeFromKey(special[i]);
        CHECK(push_key(id, special[i], scancode, 0, true, 0) == 0);
        CHECK(push_key(id, special[i], scancode, 0, false, 0) == 0);
        hal_input_pump();
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, expected, sizeof(expected)) == 0);
    }
    warnings = log_warnings;
    sdl_send_key_event(id, 1, EVENT_KEY_TAB, KEY_DOWN);
    CHECK(!hal_input_has_data(HAL_INPUT_KEYBOARD) && log_warnings == warnings + 1u);
    {
        unsigned int errors = log_errors;
        sdl_send_key_event(id, 0, UINT16_MAX, KEY_DOWN);
        CHECK(!hal_input_has_data(HAL_INPUT_KEYBOARD) && log_errors == errors + 1u);
    }
    CHECK(push_key(id, SDLK_c, SDL_SCANCODE_C, 0, true, 0) == 0);
    CHECK(push_key(id, SDLK_c, SDL_SCANCODE_C, 0, true, 1) == 0);
    {
        SDL_Event event = {0};
        event.type = SDL_TEXTINPUT;
        event.text.windowID = id;
        strcpy(event.text.text, "c");
        CHECK(SDL_PushEvent(&event) == 1);
    }
    CHECK(push_key(id, SDLK_c, SDL_SCANCODE_C, 0, false, 0) == 0);
    hal_input_pump();
    {
        const uint8_t expected[] = {HAL_CMD_KEY_DOWN, 'c', HAL_CMD_KEY_UP, 'c'};
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, expected, sizeof(expected)) == 0);
    }
    sdl_input_init();
    {
        SDL_TextInputEvent event = {0};
        const uint8_t expected[] = {HAL_CMD_KEY_DOWN, '~', HAL_CMD_KEY_UP, '~'};
        event.windowID = id;
        sdl_text_input_event(&event, '~');
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, expected, sizeof(expected)) == 0);
        CHECK(sdl_input_check_key() && sdl_input_read_key(&state) == '~' && state == KEY_UP);
        CHECK(!sdl_input_check_key() && sdl_input_read_key(NULL) == 0);
    }
    CHECK(push_key(id, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, KMOD_LSHIFT, true, 0) == 0);
    CHECK(push_key(id, SDLK_a, SDL_SCANCODE_A, KMOD_LSHIFT, true, 0) == 0);
    CHECK(push_key(id, SDLK_b, SDL_SCANCODE_B, KMOD_LSHIFT, true, 0) == 0);
    hal_input_pump();
    {
        const uint8_t down[] = {HAL_CMD_MODIFIER, HAL_MOD_SHIFT, HAL_CMD_KEY_DOWN, 'a', HAL_CMD_KEY_DOWN, 'b'};
        const uint8_t up[] = {HAL_CMD_KEY_UP, 'a', HAL_CMD_KEY_UP, 'b', HAL_CMD_MODIFIER, 0};
        SDL_Event event = {0};
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, down, sizeof(down)) == 0);
        event.type = SDL_WINDOWEVENT;
        event.window.windowID = id;
        event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        CHECK(SDL_PushEvent(&event) == 1);
        hal_input_pump();
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, up, sizeof(up)) == 0);
    }
    for (i = 0; i < SDL_INPUT_QUEUE_SIZE / 2u; i++) sdl_send_key_event(id, 'A', 0, KEY_DOWN);
    CHECK(expect_byte(HAL_INPUT_KEYBOARD, HAL_CMD_KEY_DOWN) == 0);
    warnings = log_warnings;
    sdl_send_key_event(id, 'B', 0, KEY_DOWN);
    CHECK(log_warnings == warnings + 1u);
    CHECK(expect_byte(HAL_INPUT_KEYBOARD, 'A') == 0);
    for (i = 1; i < SDL_INPUT_QUEUE_SIZE / 2u; i++)
    {
        CHECK(expect_byte(HAL_INPUT_KEYBOARD, HAL_CMD_KEY_DOWN) == 0);
        CHECK(expect_byte(HAL_INPUT_KEYBOARD, 'A') == 0);
    }
    CHECK(!hal_input_has_data(HAL_INPUT_KEYBOARD));
    sdl_send_key_event(id, 'Z', 0, KEY_UP);
    {
        const uint8_t expected[] = {HAL_CMD_KEY_UP, 'Z'};
        CHECK(expect_bytes(HAL_INPUT_KEYBOARD, expected, sizeof(expected)) == 0);
    }
    CHECK(!hal_input_read_byte(HAL_INPUT_KEYBOARD, NULL));
    CHECK(!hal_input_has_data((hal_input_dev_t)99));
    hal_input_shutdown();
    sdl_send_key_event(id, 'Q', 0, KEY_DOWN);
    CHECK(!hal_input_has_data(HAL_INPUT_KEYBOARD));
    CHECK(hal_input_init() == 0 && !hal_input_has_data(HAL_INPUT_KEYBOARD));
    return 0;
}

static int test_mouse(void)
{
    uint32_t id;
    uint16_t x, y, z;
    size_t i;
    unsigned int warnings, errors;
    CHECK(open_display() == 0 && hal_input_init() == 0);
    id = sdl_backend_display_window();
    hal_input_pump();
    CHECK(push_button(id, SDL_BUTTON_LEFT, true) == 0);
    hal_input_pump();
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, HAL_MOUSE_BUTTON_LEFT};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(sdl_mouse_get_pos(&x, &y, &z));
    CHECK(x == (hal_display_get_fb()->width > 1 ? 1 : 0));
    CHECK(y == (hal_display_get_fb()->height > 1 ? 1 : 0) && z == 100);
    CHECK(!sdl_mouse_get_pos(&x, &y, &z) && !sdl_mouse_get_pos(NULL, &y, &z));
    CHECK(push_motion(id, -10, INT32_MAX, 5, -7, SDL_BUTTON_LMASK) == 0);
    CHECK(push_button(id, SDL_BUTTON_RIGHT, true) == 0);
    CHECK(push_button(id, SDL_BUTTON_RIGHT, false) == 0);
    CHECK(push_button(id, SDL_BUTTON_LEFT, false) == 0);
    hal_input_pump();
    {
        const uint8_t expected[] = {
            HAL_CMD_MOUSE_X, 5, HAL_CMD_MOUSE_Y, (uint8_t)-7, HAL_CMD_MOUSE_BUTTON, HAL_MOUSE_BUTTON_LEFT,
            HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, HAL_MOUSE_BUTTON_RIGHT,
            HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, HAL_MOUSE_BUTTON_LEFT,
            HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, 0
        };
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    for (i = SDL_BUTTON_MIDDLE; i <= SDL_BUTTON_X2; i++)
    {
        uint8_t expected[] = {HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, (uint8_t)i,
                              HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, 0};
        CHECK(push_button(id, (uint8_t)i, true) == 0);
        CHECK(push_button(id, (uint8_t)i, false) == 0);
        hal_input_pump();
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    {
        SDL_Event event = {0};
        const uint8_t expected[] = {
            HAL_CMD_MOUSE_WHEEL_X, 2, HAL_CMD_MOUSE_WHEEL_Y, (uint8_t)-3,
            HAL_CMD_MOUSE_WHEEL_X, (uint8_t)-2, HAL_CMD_MOUSE_WHEEL_Y, 3
        };
        event.type = SDL_MOUSEWHEEL;
        event.wheel.windowID = id;
        event.wheel.x = 2;
        event.wheel.y = -3;
        CHECK(SDL_PushEvent(&event) == 1);
        event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        CHECK(SDL_PushEvent(&event) == 1);
        hal_input_pump();
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, HAL_INPUT_SCALE_ONE * 2) == HAL_INPUT_CONTROL_OK);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_POINTER_MODE, HAL_POINTER_PRESSURE) == HAL_INPUT_CONTROL_OK);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_CURSOR_VISIBLE, 0) == HAL_INPUT_CONTROL_OK);
    CHECK(SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE);
    CHECK(push_motion(id, 0, 0, 3, -4, SDL_BUTTON_LMASK) == 0);
    hal_input_pump();
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_X, 6, HAL_CMD_MOUSE_Y, (uint8_t)-8, HAL_CMD_MOUSE_Z, 100};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, INT32_MAX) == HAL_INPUT_CONTROL_OK);
    warnings = log_warnings;
    sdl_backend_pointer_event(id, INT32_MAX, INT32_MIN, 500, 0);
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_X, 127, HAL_CMD_MOUSE_Y, 128, HAL_CMD_MOUSE_Z, 255};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(log_warnings == warnings + 3u);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, -HAL_INPUT_SCALE_ONE) == HAL_INPUT_CONTROL_OK);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_POINTER_MODE, HAL_POINTER_BUTTONS) == HAL_INPUT_CONTROL_OK);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_CURSOR_VISIBLE, 1) == HAL_INPUT_CONTROL_OK);
    sdl_backend_pointer_event(id, 10, -20, 0, 0);
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_X, (uint8_t)-10, HAL_CMD_MOUSE_Y, 20, HAL_CMD_MOUSE_BUTTON, 0};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE);
    errors = log_errors;
    CHECK(hal_input_control(HAL_INPUT_MOUSE, (hal_input_control_t)99, 0) == HAL_INPUT_CONTROL_INVALID);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_CURSOR_VISIBLE, 2) == HAL_INPUT_CONTROL_INVALID);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_POINTER_MODE, 2) == HAL_INPUT_CONTROL_INVALID);
    CHECK(hal_input_control((hal_input_dev_t)99, HAL_INPUT_CTRL_SCALE_Q12, 0) == HAL_INPUT_CONTROL_INVALID);
    CHECK(log_errors == errors + 4u);
    CHECK(hal_input_control(HAL_INPUT_KEYBOARD, HAL_INPUT_CTRL_SCALE_Q12, HAL_INPUT_SCALE_ONE) == HAL_INPUT_CONTROL_UNSUPPORTED);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, HAL_INPUT_SCALE_ONE) == HAL_INPUT_CONTROL_OK);
    warnings = log_warnings;
    sdl_backend_pointer_event(id, INT8_MAX, INT8_MIN, 0, 0);
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_X, 127, HAL_CMD_MOUSE_Y, 128, HAL_CMD_MOUSE_BUTTON, 0};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(log_warnings == warnings);
    sdl_backend_pointer_event(id, INT8_MAX + 1, INT8_MIN - 1, 0, 0);
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_X, 127, HAL_CMD_MOUSE_Y, 128, HAL_CMD_MOUSE_BUTTON, 0};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(log_warnings == warnings + 2u);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, 0) == HAL_INPUT_CONTROL_OK);
    sdl_backend_pointer_event(id, INT32_MAX, INT32_MIN, 0, 0);
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, 0};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, HAL_INPUT_SCALE_ONE) == HAL_INPUT_CONTROL_OK);
    for (i = 0; i < SDL_INPUT_QUEUE_SIZE / 6u; i++) sdl_backend_pointer_event(id, 1, 2, 0, 1);
    CHECK(expect_byte(HAL_INPUT_MOUSE, HAL_CMD_MOUSE_X) == 0);
    warnings = log_warnings;
    sdl_backend_wheel_event(id, 3, 4);
    CHECK(log_warnings == warnings + 1u);
    {
        const uint8_t remaining[] = {1, HAL_CMD_MOUSE_Y, 2, HAL_CMD_MOUSE_BUTTON, 1};
        for (i = 0; i < sizeof(remaining); i++) CHECK(expect_byte(HAL_INPUT_MOUSE, remaining[i]) == 0);
    }
    for (i = 1; i < SDL_INPUT_QUEUE_SIZE / 6u; i++)
    {
        const uint8_t event[] = {HAL_CMD_MOUSE_X, 1, HAL_CMD_MOUSE_Y, 2, HAL_CMD_MOUSE_BUTTON, 1};
        size_t j;
        for (j = 0; j < sizeof(event); j++) CHECK(expect_byte(HAL_INPUT_MOUSE, event[j]) == 0);
    }
    CHECK(!hal_input_has_data(HAL_INPUT_MOUSE));
    CHECK(push_button(id, SDL_BUTTON_LEFT, true) == 0);
    {
        SDL_Event event = {0};
        const uint8_t expected[] = {
            HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, HAL_MOUSE_BUTTON_LEFT,
            HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_BUTTON, 0
        };
        event.type = SDL_WINDOWEVENT;
        event.window.windowID = id;
        event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        CHECK(SDL_PushEvent(&event) == 1);
        hal_input_pump();
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    sdl_backend_wheel_event(id, -2, 3);
    {
        const uint8_t expected[] = {HAL_CMD_MOUSE_WHEEL_X, (uint8_t)-2, HAL_CMD_MOUSE_WHEEL_Y, 3};
        CHECK(expect_bytes(HAL_INPUT_MOUSE, expected, sizeof(expected)) == 0);
    }
    hal_input_shutdown();
    sdl_backend_pointer_event(id, 1, 1, 0, 1);
    CHECK(!hal_input_has_data(HAL_INPUT_MOUSE));
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, HAL_INPUT_SCALE_ONE) == HAL_INPUT_CONTROL_FAILED);
    return 0;
}

static uint32_t callback_id;
static unsigned int key_calls, mouse_calls, delta_calls, wheel_calls;

static void key_callback(uint32_t id, uint8_t code, uint16_t control, uint8_t state)
{
    (void)code; (void)control; (void)state;
    callback_id = id;
    key_calls++;
}

static void mouse_callback(uint32_t id, uint16_t x, uint16_t y, uint16_t z, uint8_t button)
{
    (void)x; (void)y; (void)z; (void)button;
    callback_id = id;
    mouse_calls++;
}

static void delta_callback(uint32_t id, int32_t x, int32_t y, uint16_t z, uint8_t button)
{
    (void)x; (void)y; (void)z; (void)button;
    callback_id = id;
    delta_calls++;
}

static void wheel_callback(uint32_t id, int32_t x, int32_t y)
{
    (void)x; (void)y;
    callback_id = id;
    wheel_calls++;
}

static bool destroy_on_key_up;

static void destroying_key_callback(uint32_t id, uint8_t code, uint16_t control, uint8_t state)
{
    (void)code; (void)control;
    if (!destroy_on_key_up || state == KEY_UP) sdl_destroy_window(id);
}

static void destroying_mouse_callback(uint32_t id, uint16_t x, uint16_t y, uint16_t z, uint8_t button)
{
    (void)x; (void)y; (void)z; (void)button;
    sdl_destroy_window(id);
}

static int test_lifecycle(void)
{
    sdl_api_t *api;
    uint32_t a, b, c, d, e, f, stride;
    CHECK(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
    api = library_entry_point();
    CHECK(api != NULL && sdl_is_running());
    a = api->create_window("a", 3, 2);
    b = api->create_window("b", 3, 2);
    c = api->create_window("c", 3, 2);
    CHECK(a != UINT32_MAX && b != UINT32_MAX && c != UINT32_MAX && sdl_window_count() == 3);
    CHECK(api->get_width(a) == 3 && api->get_height(a) == 2 && api->get_buffer(a) != NULL);
    stride = sdl_window_get_stride(a);
    CHECK(stride >= 3u * sizeof(PIXEL_TYPE_T) && stride % 4u == 0);
    api->set_key_handler(a, key_callback);
    api->set_mouse_handler(a, mouse_callback);
    api->set_mouse_wheel_handler(a, wheel_callback);
    sdl_window_set_mouse_delta_event(a, delta_callback);
    sdl_send_key_event(a, 'a', 0, KEY_DOWN);
    sdl_send_mouse_event(a, 1, 1, 0, 0);
    sdl_send_mouse_delta_event(a, -2, 3, 0, 0);
    sdl_send_mouse_wheel_event(a, -1, 1);
    CHECK(callback_id == a && key_calls == 1 && mouse_calls == 1 && delta_calls == 1 && wheel_calls == 1);
    {
        uint16_t x, y, z;
        CHECK(sdl_mouse_get_pos(&x, &y, &z) && x == 1 && y == 1 && z == 0);
        sdl_send_mouse_event(b, 2, 1, 7, 0);
        CHECK(sdl_mouse_get_pos(&x, &y, &z) && x == 2 && y == 1 && z == 7);
        CHECK(!sdl_mouse_get_pos(&x, &y, &z));
    }
    api->set_key_handler(b, key_callback);
    CHECK(push_key(a, SDLK_a, SDL_SCANCODE_A, 0, true, 0) == 0);
    CHECK(push_key(b, SDLK_b, SDL_SCANCODE_B, 0, true, 0) == 0);
    CHECK(push_key(a, SDLK_a, SDL_SCANCODE_A, 0, false, 0) == 0);
    CHECK(push_key(b, SDLK_b, SDL_SCANCODE_B, 0, false, 0) == 0);
    CHECK(api->process() == 1 && key_calls == 5 && callback_id == b);
    for (fail_point = 1; fail_point <= 6; fail_point++)
    {
        CHECK(api->create_window("failure", 3, 2) == UINT32_MAX);
        CHECK(sdl_window_count() == 3 && api->get_buffer(a) != NULL);
    }
    fail_point = 0;
    CHECK(api->create_window(NULL, 3, 2) == UINT32_MAX);
    CHECK(api->create_window("invalid", 0, 2) == UINT32_MAX);
    api->destroy(c);
    d = api->create_window("d", 3, 2);
    CHECK(d != UINT32_MAX && sdl_window_count() == 3);
    api->destroy(a);
    e = api->create_window("e", 3, 2);
    CHECK(e != UINT32_MAX && sdl_window_count() == 3);
    api->destroy(d);
    f = api->create_window("f", 3, 2);
    CHECK(f != UINT32_MAX && sdl_window_count() == 3);
    {
        uint32_t g = api->create_window("callback-modifier", 3, 2);
        CHECK(g != UINT32_MAX);
        api->set_key_handler(g, destroying_key_callback);
        CHECK(push_key(g, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, KMOD_LSHIFT, true, 0) == 0);
        CHECK(api->process() == 1 && sdl_get_buffer(g) == NULL && sdl_window_count() == 3);
        g = api->create_window("callback-key-up", 3, 2);
        CHECK(g != UINT32_MAX);
        destroy_on_key_up = true;
        api->set_key_handler(g, destroying_key_callback);
        CHECK(push_key(g, SDLK_a, SDL_SCANCODE_A, 0, true, 0) == 0);
        CHECK(api->process() == 1 && sdl_get_buffer(g) != NULL);
        sdl_input_release_window(g);
        CHECK(sdl_get_buffer(g) == NULL && sdl_window_count() == 3);
        g = api->create_window("callback-mouse", 3, 2);
        CHECK(g != UINT32_MAX);
        api->set_mouse_handler(g, destroying_mouse_callback);
        sdl_window_set_mouse_delta_event(g, delta_callback);
        CHECK(push_motion(g, 1, 1, 1, 1, 0) == 0);
        CHECK(api->process() == 1 && sdl_get_buffer(g) == NULL);
        CHECK(delta_calls == 1 && sdl_window_count() == 3);
    }
    api->destroy(d);
    CHECK(sdl_get_buffer(d) == NULL && sdl_window_get_width(d) == 0 &&
          sdl_window_get_height(d) == 0 && sdl_window_get_stride(d) == 0);
    sdl_update_window(UINT32_MAX);
    api->terminate();
    CHECK(sdl_window_count() == 0 && !sdl_is_running() && sdl_event_loop() == 0);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO);
    api->terminate();
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    return 0;
}

static int test_ownership(void)
{
    uint32_t external;
    CHECK(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
    for (fail_point = 1; fail_point <= 6; fail_point++)
    {
        CHECK(hal_display_init() < 0 && hal_display_get_fb() == NULL);
        CHECK(sdl_window_count() == 0 && SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO);
    }
    fail_point = 0;
    CHECK(hal_input_init() == 0);
    hal_input_pump();
    CHECK(open_display() == 0);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_CURSOR_VISIBLE, 0) == HAL_INPUT_CONTROL_OK);
    hal_input_shutdown();
    CHECK(hal_display_get_fb() != NULL && hal_display_pump() == HAL_DISPLAY_ACTIVE);
    CHECK(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE);
    CHECK(hal_input_init() == 0);
    hal_display_shutdown();
    hal_input_pump();
    CHECK(hal_display_get_fb() == NULL);
    CHECK(hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_SCALE_Q12, 8192) == HAL_INPUT_CONTROL_OK);
    CHECK(open_display() == 0);
    hal_input_shutdown();
    hal_input_shutdown();
    hal_display_shutdown();
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO);

    external = sdl_create_window("independent", 3, 2);
    CHECK(external != UINT32_MAX);
    CHECK(open_display() == 0);
    hal_display_shutdown();
    CHECK(sdl_get_buffer(external) != NULL && sdl_window_count() == 1);
    sdl_destroy_window(external);
    sdl_terminate();
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    return 0;
}

static int test_init_failure(void)
{
    CHECK(hal_display_init() < 0);
    CHECK(hal_display_get_fb() == NULL && sdl_window_count() == 0);
    CHECK(hal_input_init() < 0 && !hal_input_has_data(HAL_INPUT_KEYBOARD));
    CHECK(hal_display_pump() == HAL_DISPLAY_ERROR);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s display|keyboard|mouse|lifecycle|ownership|init-failure\n", argv[0]);
        return 2;
    }
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_WARN);
    SDL_LogSetOutputFunction(capture_log, NULL);
    if (strcmp(argv[1], "display") == 0) result = test_display();
    else if (strcmp(argv[1], "keyboard") == 0) result = test_keyboard();
    else if (strcmp(argv[1], "mouse") == 0) result = test_mouse();
    else if (strcmp(argv[1], "lifecycle") == 0) result = test_lifecycle();
    else if (strcmp(argv[1], "ownership") == 0) result = test_ownership();
    else if (strcmp(argv[1], "init-failure") == 0) result = test_init_failure();
    else
    {
        fprintf(stderr, "Unknown backend test: %s\n", argv[1]);
        result = 2;
    }
    fail_point = 0;
    hal_input_shutdown();
    hal_display_shutdown();
    sdl_terminate();
    if (result == 0) printf("PASS %s (%u bpp)\n", argv[1], WMB_SDL_PIXEL_BITS);
    return result;
}
