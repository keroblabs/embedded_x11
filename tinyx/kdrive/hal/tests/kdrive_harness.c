#include "mock_hal.h"
#include "mock_xserver.h"
#include "kkeymap.h"
#include "opaque.h"
#include <X11/keysym.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static KdCardInfo *card;
static KdScreenInfo *screen;

static int prepare(hal_pixfmt_t format, const KdKeyboardFuncs *keyboard)
{
    MockHalReset(format);
    card = KdCardInfoAdd(&KdHalCardFuncs, NULL);
    CHECK(card != NULL);
    screen = KdScreenInfoAdd(card);
    CHECK(screen != NULL);
    CHECK(card->cfuncs->cardinit(card));
    CHECK(card->cfuncs->scrinit(screen));
    MockXServerReset(card, screen);
    KdOsInit(&KdHalOsFuncs);
    KdHalOsFuncs.Enable();
    CHECK(KdHalInputInit());
    KdInitInput(&KdHalMouseFuncs, keyboard);
    return 0;
}

static int setup(hal_pixfmt_t format)
{
    CHECK(prepare(format, &KdHalKeyboardFuncs) == 0);
    MockXServerStartDevices();
    return 0;
}

static void teardown(void)
{
    KdScreenInfo *next;
    KdMouseInfo *mouse;
    mock_calloc_failure_after = -1;
    MockXServerStopDevices();
    KdHalOsFuncs.Fini();
    if (card != NULL)
    {
        for (screen = card->screenList; screen; screen = next)
        {
            next = screen->next;
            card->cfuncs->scrfini(screen);
            free(screen);
        }
        card->cfuncs->cardfini(card);
        free(card);
    }
    card = NULL;
    kdCardInfo = NULL;
    for (mouse = kdMouseInfo; mouse; )
    {
        KdMouseInfo *next_mouse = mouse->next;
        free(mouse->name);
        free(mouse->prot);
        free(mouse);
        mouse = next_mouse;
    }
    kdMouseInfo = NULL;
}

static int test_framebuffer(void)
{
    hal_framebuffer_t *fb;
    RegionRec region = {.extents = {1, 2, 5, 6}};
    CHECK(setup(HAL_PIXFMT_RGB565) == 0);
    fb = MockHalFramebuffer();
    CHECK(screen->fb.byteStride == (int)fb->stride);
    CHECK(screen->fb.pixelStride == (int)(fb->stride / 2));
    CHECK(screen->fb.depth == 16 && screen->fb.bitsPerPixel == 16);
    CHECK(screen->fb.frameBuffer == fb->pixels && screen->memory_base == fb->pixels);
    CHECK(screen->memory_size == (unsigned long)fb->stride * fb->height);
    CHECK(screen->fb.redMask == 0xF800 && screen->fb.greenMask == 0x07E0 && screen->fb.blueMask == 0x001F);
    {
        KdScreenInfo incompatible = {.card = card, .width = 14, .height = 7};
        KdScreenInfo extra = {.card = card, .mynum = 1};
        CHECK(!card->cfuncs->scrinit(&incompatible) && incompatible.driver == NULL);
        CHECK(!card->cfuncs->scrinit(&extra) && extra.driver == NULL);
        CHECK(!card->cfuncs->scrinit(screen));
    }
    CHECK(card->cfuncs->initScreen(&mock_xserver.screen));
    CHECK(card->cfuncs->createRes(&mock_xserver.screen));
    MockXServerReportDamage(&region);
    CHECK(mock_hal.flush_count == 1 && mock_hal.flushes[0].x == 1 && mock_hal.flushes[0].width == 4);
    teardown();
    CHECK(setup(HAL_PIXFMT_XRGB8888) == 0);
    CHECK(screen->fb.depth == 24 && screen->fb.bitsPerPixel == 32);
    CHECK(screen->fb.pixelStride == (int)(MockHalFramebuffer()->stride / 4));
    CHECK(screen->fb.redMask == 0xFF0000 && screen->fb.greenMask == 0x00FF00 && screen->fb.blueMask == 0x0000FF);
    CHECK(screen->softCursor && !screen->fb.shadow);
    return 0;
}

static int test_damage(void)
{
    struct { RegDataRec header; BoxRec boxes[2]; } storage =
        {.header = {2, 2}, .boxes = {{1, 2, 5, 6}, {-2, -1, 3, 2}}};
    RegionRec region = {.extents = {-2, -1, 5, 6}, .data = &storage.header};
    RegionRec empty = {.extents = {0, 0, 0, 0}};
    unsigned int flushes;
    CHECK(setup(HAL_PIXFMT_XRGB8888) == 0);
    CHECK(card->cfuncs->initScreen(&mock_xserver.screen));
    CHECK(card->cfuncs->finishInitScreen(&mock_xserver.screen));
    CHECK(card->cfuncs->createRes(&mock_xserver.screen));
    CHECK(card->cfuncs->createRes(&mock_xserver.screen));
    CHECK(mock_xserver.damage_registers == 1);
    MockXServerReportDamage(&region);
    CHECK(mock_hal.flush_count == 2);
    CHECK(mock_hal.flushes[0].x == 1 && mock_hal.flushes[0].y == 2 &&
          mock_hal.flushes[0].width == 4 && mock_hal.flushes[0].height == 4);
    CHECK(mock_hal.flushes[1].x == -2 && mock_hal.flushes[1].y == -1 &&
          mock_hal.flushes[1].width == 5 && mock_hal.flushes[1].height == 3);
    flushes = mock_hal.flush_count;
    MockXServerReportDamage(&empty);
    CHECK(mock_hal.flush_count == flushes);
    ((uint32_t *)screen->fb.frameBuffer)[0] = 0x123456;
    CHECK(card->cfuncs->enable(&mock_xserver.screen));
    CHECK(((uint32_t *)screen->fb.frameBuffer)[0] == 0x123456);
    CHECK(mock_hal.flushes[flushes].width == 13 && mock_hal.flushes[flushes].height == 7);
    CHECK(card->cfuncs->dpms(&mock_xserver.screen, KD_DPMS_NORMAL) && mock_hal.last_power == 1);
    CHECK(card->cfuncs->dpms(&mock_xserver.screen, KD_DPMS_POWERDOWN) && mock_hal.last_power == 0);
    mock_hal.power_result = -1;
    CHECK(!card->cfuncs->dpms(&mock_xserver.screen, KD_DPMS_NORMAL));
    CHECK(!card->cfuncs->dpms(&mock_xserver.screen, 99));
    DamageDestroy(mock_xserver.damage);
    CHECK(mock_xserver.damage == NULL && mock_xserver.damage_destroys == 1);
    card->cfuncs->scrfini(screen);
    card->cfuncs->scrfini(screen);
    CHECK(mock_xserver.damage_destroys == 1);
    return 0;
}

static int keycode(KeySym symbol)
{
    int scan;
    for (scan = kdMinScanCode; scan <= kdMaxScanCode; scan++)
        if (kdKeymap[(scan - kdMinScanCode) * KD_MAX_WIDTH] == symbol)
            return scan + KD_MIN_KEYCODE - kdMinScanCode;
    return -1;
}

static int key_event(size_t index, int type, int code)
{
    CHECK(index < mock_xserver.event_count);
    CHECK(mock_xserver.events[index].u.u.type == type);
    CHECK(mock_xserver.events[index].u.u.detail == code);
    return 0;
}

static int test_keyboard(void)
{
    int key;
    size_t events;
    CHECK(setup(HAL_PIXFMT_RGB565) == 0);
    for (key = 0x20; key <= 0x7E; key++)
    {
        uint8_t bytes[] = {HAL_CMD_KEY_DOWN, (uint8_t)key, HAL_CMD_KEY_UP, (uint8_t)key};
        mock_xserver.event_count = 0;
        CHECK(MockHalPush(HAL_INPUT_KEYBOARD, bytes, sizeof(bytes)));
        KdHalKeyboardPoll();
        CHECK(mock_xserver.event_count == 2);
        CHECK(keycode((KeySym)key) >= KD_MIN_KEYCODE);
        CHECK(key_event(0, KeyPress, keycode((KeySym)key)) == 0);
        CHECK(key_event(1, KeyRelease, keycode((KeySym)key)) == 0);
    }
    CHECK(keycode(XK_Shift_L) > keycode('~'));
    CHECK(keycode(XK_Caps_Lock) > keycode('~'));
    {
        static const uint8_t controls[] = {HAL_KEY_BACKSPACE, HAL_KEY_ENTER, HAL_KEY_ESCAPE,
            HAL_KEY_LEFT, HAL_KEY_RIGHT, HAL_KEY_UP, HAL_KEY_DOWN};
        static const KeySym symbols[] = {XK_BackSpace, XK_Return, XK_Escape,
            XK_Left, XK_Right, XK_Up, XK_Down};
        size_t i;
        for (i = 0; i < sizeof(controls); i++)
        {
            uint8_t bytes[] = {HAL_CMD_KEY_DOWN, controls[i], HAL_CMD_KEY_UP, controls[i]};
            mock_xserver.event_count = 0;
            CHECK(MockHalPush(HAL_INPUT_KEYBOARD, bytes, sizeof(bytes)));
            KdHalKeyboardPoll();
            CHECK(mock_xserver.event_count == 2);
            CHECK(keycode(symbols[i]) > keycode('~'));
            CHECK(key_event(0, KeyPress, keycode(symbols[i])) == 0);
            CHECK(key_event(1, KeyRelease, keycode(symbols[i])) == 0);
        }
    }
    mock_xserver.event_count = 0;
    {
        uint8_t bytes[] = {HAL_CMD_MODIFIER, HAL_MOD_SHIFT | HAL_MOD_CTRL | HAL_MOD_ALT |
                            HAL_MOD_ALTGR | HAL_MOD_SUPER, HAL_CMD_KEY_DOWN, 'q',
                            HAL_CMD_KEY_UP, 'q', HAL_CMD_MODIFIER, 0};
        CHECK(MockHalPush(HAL_INPUT_KEYBOARD, bytes, sizeof(bytes)));
        KdHalKeyboardPoll();
        CHECK(mock_xserver.event_count == 12);
        CHECK(key_event(0, KeyPress, keycode(XK_Shift_L)) == 0);
        CHECK(key_event(1, KeyPress, keycode(XK_Control_L)) == 0);
        CHECK(key_event(2, KeyPress, keycode(XK_Alt_L)) == 0);
        CHECK(key_event(3, KeyPress, keycode(XK_Mode_switch)) == 0);
        CHECK(key_event(4, KeyPress, keycode(XK_Super_L)) == 0);
        CHECK(key_event(5, KeyPress, keycode('q')) == 0);
        CHECK(mock_xserver.key.state == 0);
    }
    mock_xserver.event_count = 0;
    {
        uint8_t on[] = {HAL_CMD_MODIFIER, HAL_MOD_CAPS};
        uint8_t off[] = {HAL_CMD_MODIFIER, 0};
        CHECK(MockHalPush(HAL_INPUT_KEYBOARD, on, sizeof(on)));
        KdHalKeyboardPoll();
        CHECK(mock_xserver.key.state & LockMask);
        CHECK(MockHalPush(HAL_INPUT_KEYBOARD, off, sizeof(off)));
        KdHalKeyboardPoll();
        CHECK(!(mock_xserver.key.state & LockMask));
        CHECK(mock_xserver.event_count == 4);
        CHECK(key_event(0, KeyPress, keycode(XK_Caps_Lock)) == 0);
        CHECK(key_event(1, KeyRelease, keycode(XK_Caps_Lock)) == 0);
        CHECK(key_event(2, KeyPress, keycode(XK_Caps_Lock)) == 0);
        CHECK(key_event(3, KeyRelease, keycode(XK_Caps_Lock)) == 0);
    }
    events = mock_xserver.event_count;
    {
        uint8_t command[] = {HAL_CMD_KEY_DOWN};
        uint8_t data[] = {'a', HAL_CMD_KEY_UP, 'a'};
        CHECK(MockHalPush(HAL_INPUT_KEYBOARD, command, sizeof(command)));
        KdHalKeyboardPoll();
        CHECK(mock_xserver.event_count == events);
        CHECK(MockHalPush(HAL_INPUT_KEYBOARD, data, sizeof(data)));
        KdHalKeyboardPoll();
        CHECK(mock_xserver.event_count == events + 2);
    }
    {
        unsigned int errors = mock_xserver.errors;
        uint8_t bytes[] = {0xFE, 0, HAL_CMD_MODIFIER, 0x80, HAL_CMD_KEY_DOWN, 200};
        CHECK(MockHalPush(HAL_INPUT_KEYBOARD, bytes, sizeof(bytes)));
        KdHalKeyboardPoll();
        CHECK(mock_xserver.errors == errors + 3);
    }
    return 0;
}

static int pointer_packet(int x, int y, uint8_t button)
{
    uint8_t bytes[] = {HAL_CMD_MOUSE_X, (uint8_t)x, HAL_CMD_MOUSE_Y, (uint8_t)y,
                       HAL_CMD_MOUSE_BUTTON, button};
    CHECK(MockHalPush(HAL_INPUT_MOUSE, bytes, sizeof(bytes)));
    KdHalMousePoll();
    return 0;
}

static int button_event(size_t index, int type, int button)
{
    CHECK(index < mock_xserver.event_count);
    CHECK(mock_xserver.events[index].u.u.type == type);
    CHECK(mock_xserver.events[index].u.u.detail == button);
    return 0;
}

static int test_pointer(void)
{
    static const int x_buttons[] = {0, 1, 2, 3, 8, 9};
    int button;
    CHECK(setup(HAL_PIXFMT_RGB565) == 0);
    CHECK(mock_xserver.button.numButtons == 9);
    CHECK(mock_xserver.button.map[9] == 9);
    for (button = 1; button <= 5; button++)
    {
        mock_xserver.event_count = 0;
        CHECK(pointer_packet(0, 0, (uint8_t)button) == 0);
        CHECK(pointer_packet(0, 0, 0) == 0);
        CHECK(mock_xserver.event_count == 2);
        CHECK(button_event(0, ButtonPress, x_buttons[button]) == 0);
        CHECK(button_event(1, ButtonRelease, x_buttons[button]) == 0);
    }
    mock_xserver.event_count = 0;
    CHECK(pointer_packet(2, 3, HAL_MOUSE_BUTTON_X2) == 0);
    CHECK(mock_xserver.pointer_x == 2 && mock_xserver.pointer_y == 3);
    CHECK(mock_xserver.last_dx == 2 && mock_xserver.last_dy == 3);
    CHECK(kdMouseInfo->buttonState == KD_BUTTON_9);
    mock_xserver.event_count = 0;
    {
        uint8_t bytes[] = {HAL_CMD_MOUSE_WHEEL_Y, 2, HAL_CMD_MOUSE_WHEEL_Y, (uint8_t)-1,
                           HAL_CMD_MOUSE_WHEEL_X, 1, HAL_CMD_MOUSE_WHEEL_X, (uint8_t)-2};
        int expected[] = {4, 4, 5, 7, 6, 6};
        size_t i;
        CHECK(MockHalPush(HAL_INPUT_MOUSE, bytes, sizeof(bytes)));
        KdHalMousePoll();
        CHECK(mock_xserver.event_count == 12);
        for (i = 0; i < 6; i++)
        {
            CHECK(button_event(i * 2, ButtonPress, expected[i]) == 0);
            CHECK(button_event(i * 2 + 1, ButtonRelease, expected[i]) == 0);
        }
        CHECK(kdMouseInfo->buttonState == KD_BUTTON_9);
    }
    CHECK(pointer_packet(0, 0, 0) == 0);
    {
        uint8_t bytes[] = {HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_Z, 100,
                           HAL_CMD_MOUSE_X, 0, HAL_CMD_MOUSE_Y, 0, HAL_CMD_MOUSE_Z, 0};
        mock_xserver.event_count = 0;
        CHECK(MockHalPush(HAL_INPUT_MOUSE, bytes, sizeof(bytes)));
        KdHalMousePoll();
        CHECK(button_event(0, ButtonPress, 1) == 0 && button_event(1, ButtonRelease, 1) == 0);
    }
    CHECK(pointer_packet(-128, 127, 0) == 0);
    CHECK(mock_xserver.pointer_x == 0 && mock_xserver.pointer_y == 6);
    {
        uint8_t start[] = {HAL_CMD_MOUSE_X, 3, HAL_CMD_MOUSE_Y};
        uint8_t finish[] = {(uint8_t)-2, HAL_CMD_MOUSE_BUTTON, 0};
        CHECK(MockHalPush(HAL_INPUT_MOUSE, start, sizeof(start)));
        KdHalMousePoll();
        CHECK(mock_xserver.pointer_x == 0 && mock_xserver.pointer_y == 6);
        CHECK(MockHalPush(HAL_INPUT_MOUSE, finish, sizeof(finish)));
        KdHalMousePoll();
        CHECK(mock_xserver.pointer_x == 3 && mock_xserver.pointer_y == 4);
    }
    return 0;
}

static int test_lifecycle(void)
{
    unsigned int input_pumps;
    CHECK(setup(HAL_PIXFMT_RGB565) == 0);
    CHECK(KdHalInputInit() && mock_hal.input_inits == 1);
    KdBlockHandler(0, NULL, NULL, NULL);
    CHECK(mock_hal.input_pumps == 1 && mock_hal.display_pumps == 1);
    CHECK(mock_xserver.adjusted_wait == 20);
    kdMouseInfo->timeoutPending = TRUE;
    kdMouseInfo->emulationTimeout = mock_xserver.time + 5;
    KdBlockHandler(0, NULL, NULL, NULL);
    CHECK(mock_xserver.adjusted_wait == 5);
    kdMouseInfo->timeoutPending = FALSE;
    input_pumps = mock_hal.input_pumps;
    KdHalOsFuncs.Disable();
    KdBlockHandler(0, NULL, NULL, NULL);
    CHECK(mock_hal.input_pumps == input_pumps);
    KdHalOsFuncs.Enable();
    mock_hal.status = HAL_DISPLAY_STOP_REQUESTED;
    KdBlockHandler(0, NULL, NULL, NULL);
    CHECK((dispatchException & DE_TERMINATE) != 0 && isItTimeToYield);
    dispatchException = isItTimeToYield = 0;
    mock_hal.status = HAL_DISPLAY_ERROR;
    KdBlockHandler(0, NULL, NULL, NULL);
    CHECK((dispatchException & DE_TERMINATE) != 0);
    MockXServerStopDevices();
    KdHalOsFuncs.Fini();
    KdHalOsFuncs.Fini();
    CHECK(mock_hal.input_shutdowns == 1 && mock_hal.display_shutdowns == 1);
    return 0;
}

static int test_faults(void)
{
    KdScreenInfo *new_screen;
    MockHalReset(HAL_PIXFMT_RGB565);
    mock_hal.fail_display_init = true;
    card = KdCardInfoAdd(&KdHalCardFuncs, NULL);
    CHECK(card && !card->cfuncs->cardinit(card) && card->driver == NULL);
    teardown();
    MockHalReset(HAL_PIXFMT_RGB565);
    MockHalFramebuffer()->stride = 1;
    card = KdCardInfoAdd(&KdHalCardFuncs, NULL);
    CHECK(card && !card->cfuncs->cardinit(card) && mock_hal.display_shutdowns == 1);
    teardown();
    MockHalReset(HAL_PIXFMT_RGB565);
    mock_calloc_failure_after = 1;
    card = KdCardInfoAdd(&KdHalCardFuncs, NULL);
    CHECK(card && !card->cfuncs->cardinit(card) && card->driver == NULL);
    CHECK(mock_hal.display_shutdowns == 1);
    teardown();

    CHECK(setup(HAL_PIXFMT_RGB565) == 0);
    new_screen = KdScreenInfoAdd(card);
    CHECK(new_screen != NULL && !card->cfuncs->scrinit(new_screen));
    mock_xserver.fail_damage_setup = true;
    CHECK(!card->cfuncs->initScreen(&mock_xserver.screen));
    mock_xserver.fail_damage_setup = false;
    mock_xserver.fail_damage_create = true;
    CHECK(!card->cfuncs->createRes(&mock_xserver.screen));
    mock_xserver.fail_damage_create = false;
    CHECK(card->cfuncs->createRes(&mock_xserver.screen));
    teardown();

    MockHalReset(HAL_PIXFMT_RGB565);
    mock_hal.fail_input_init = true;
    CHECK(!KdHalInputInit() && kdMouseInfo == NULL);
    MockHalReset(HAL_PIXFMT_RGB565);
    mock_hal.cursor_result = HAL_INPUT_CONTROL_FAILED;
    CHECK(!KdHalInputInit() && kdMouseInfo == NULL && mock_hal.input_shutdowns == 1);
    MockHalReset(HAL_PIXFMT_RGB565);
    mock_calloc_failure_after = 0;
    CHECK(!KdHalInputInit() && kdMouseInfo == NULL && mock_hal.input_shutdowns == 1);
    mock_calloc_failure_after = -1;

    CHECK(prepare(HAL_PIXFMT_RGB565, &KdHalKeyboardFuncs) == 0);
    mock_xserver.fail_pointer_init = true;
    CHECK(inputInfo.pointer->deviceProc(inputInfo.pointer, DEVICE_INIT) == BadImplementation);
    mock_xserver.fail_pointer_init = false;
    CHECK(inputInfo.pointer->deviceProc(inputInfo.pointer, DEVICE_INIT) == Success);
    mock_xserver.fail_keyboard_init = true;
    CHECK(inputInfo.keyboard->deviceProc(inputInfo.keyboard, DEVICE_INIT) == BadImplementation);
    mock_xserver.fail_keyboard_init = false;
    CHECK(inputInfo.keyboard->deviceProc(inputInfo.keyboard, DEVICE_INIT) == Success);
    KdHalInputFini();
    CHECK(inputInfo.pointer->deviceProc(inputInfo.pointer, DEVICE_ON) == BadImplementation);
    CHECK(inputInfo.keyboard->deviceProc(inputInfo.keyboard, DEVICE_ON) == BadImplementation);
    CHECK(!inputInfo.pointer->public.on && !inputInfo.keyboard->public.on);
    return 0;
}

static void load_boundary_map(void)
{
    kdMinScanCode = 0;
    kdMaxScanCode = KD_MAX_LENGTH - 1;
    memset(kdKeymap, 0, sizeof(kdKeymap));
    kdKeymap[kdMaxScanCode * KD_MAX_WIDTH] = XK_F12;
}

static int test_core(void)
{
    static KdKeyboardFuncs boundary_keyboard;
    KdMouseMatrix matrix;
    boundary_keyboard = KdHalKeyboardFuncs;
    boundary_keyboard.Load = load_boundary_map;
    CHECK(prepare(HAL_PIXFMT_RGB565, &boundary_keyboard) == 0);
    MockXServerStartDevices();
    KdEnqueueKeyboardEvent((uint8_t)kdMaxScanCode, FALSE);
    CHECK(key_event(0, KeyPress, KD_MAX_KEYCODE) == 0);
    KdReleaseAllKeys();
    CHECK(key_event(1, KeyRelease, KD_MAX_KEYCODE) == 0);
    KdEnqueueKeyboardEvent(255, FALSE);
    CHECK(mock_xserver.event_count == 2);
    CHECK(!KdRegisterFd(1, 0, NULL, NULL));
    KdComputeMouseMatrix(&matrix, RR_Rotate_90, 13, 7);
    KdSetMouseMatrix(&matrix);
    KdEnqueueMouseEvent(kdMouseInfo, KD_MOUSE_DELTA, 2, 3);
    CHECK(mock_xserver.last_dx == -3 && mock_xserver.last_dy == 2);
    KdComputeMouseMatrix(&matrix, RR_Rotate_0, 13, 7);
    KdSetMouseMatrix(&matrix);
    return 0;
}

static int test_arguments(void)
{
    char *nozap[] = {"harness", "-nozap"};
    char *native[] = {"harness", "-switchCmd", "unused"};
    char *unknown[] = {"harness", "-unknown"};
    CHECK(KdProcessArgument(2, nozap, 1) == 1 && kdDontZap);
    CHECK(KdProcessArgument(3, native, 1) == 0);
    CHECK(KdProcessArgument(2, unknown, 1) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "framebuffer")) result = test_framebuffer();
    else if (!strcmp(argv[1], "damage")) result = test_damage();
    else if (!strcmp(argv[1], "keyboard")) result = test_keyboard();
    else if (!strcmp(argv[1], "pointer")) result = test_pointer();
    else if (!strcmp(argv[1], "lifecycle")) result = test_lifecycle();
    else if (!strcmp(argv[1], "faults")) result = test_faults();
    else if (!strcmp(argv[1], "core")) result = test_core();
    else if (!strcmp(argv[1], "arguments")) result = test_arguments();
    else return 2;
    teardown();
    if (result == 0) printf("PASS KDrive/HAL %s\n", argv[1]);
    return result;
}
