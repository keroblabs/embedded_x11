#include "kdrive_hal.h"
#include "kkeymap.h"
#include <X11/keysym.h>

enum
{
    SC_FIRST = 128,
    SC_BACKSPACE = SC_FIRST + 95,
    SC_ENTER,
    SC_ESCAPE,
    SC_LEFT,
    SC_RIGHT,
    SC_UP,
    SC_DOWN,
    SC_SHIFT,
    SC_CTRL,
    SC_ALT,
    SC_ALTGR,
    SC_SUPER,
    SC_CAPS
};

_Static_assert(SC_CAPS < 256 && SC_CAPS - SC_FIRST < KD_MAX_LENGTH,
               "HAL scancodes must fit the KDrive keymap");

typedef struct
{
    Bool has_command;
    uint8_t command;
} PendingPacket;

static Bool input_open, keyboard_open, pointer_open;
static uint8_t modifiers;
static PendingPacket keyboard_pending, pointer_pending;
static int dx, dy;
static unsigned long buttons;

/* Keep HAL bytes out of the core's legacy PC/numpad scancode range. */
static uint8_t keyScan(uint8_t key)
{
    if (key >= 0x20 && key <= 0x7E) return (uint8_t)(SC_FIRST + key - 0x20);
    switch (key)
    {
        case HAL_KEY_BACKSPACE: return SC_BACKSPACE;
        case HAL_KEY_ENTER: return SC_ENTER;
        case HAL_KEY_ESCAPE: return SC_ESCAPE;
        case HAL_KEY_LEFT: return SC_LEFT;
        case HAL_KEY_RIGHT: return SC_RIGHT;
        case HAL_KEY_UP: return SC_UP;
        case HAL_KEY_DOWN: return SC_DOWN;
        default: return 0;
    }
}

static Bool readPacket(hal_input_dev_t dev, PendingPacket *pending, uint8_t *command, uint8_t *data)
{
    if (!pending->has_command)
    {
        if (!hal_input_read_byte(dev, &pending->command)) return FALSE;
        pending->has_command = TRUE;
    }
    if (!hal_input_read_byte(dev, data)) return FALSE;
    *command = pending->command;
    pending->has_command = FALSE;
    return TRUE;
}

Bool KdHalInputInit(void)
{
    KdMouseInfo *mouse;
    int i, result;
    if (input_open) return TRUE;
    if (kdMouseInfo != NULL && kdMouseInfo->next != NULL)
    {
        ErrorF("KDrive HAL: only one pointer is supported\n");
        return FALSE;
    }
    if (hal_input_init() != 0)
    {
        ErrorF("KDrive HAL: input initialization failed\n");
        return FALSE;
    }
    result = hal_input_control(HAL_INPUT_MOUSE, HAL_INPUT_CTRL_CURSOR_VISIBLE, 0);
    if (result != HAL_INPUT_CONTROL_OK && result != HAL_INPUT_CONTROL_UNSUPPORTED)
    {
        ErrorF("KDrive HAL: cursor control failed\n");
        hal_input_shutdown();
        return FALSE;
    }
    mouse = kdMouseInfo != NULL ? kdMouseInfo : KdMouseInfoAdd();
    if (mouse == NULL)
    {
        ErrorF("KDrive HAL: pointer allocation failed\n");
        hal_input_shutdown();
        return FALSE;
    }
    mouse->nbutton = KD_MAX_BUTTON;
    mouse->emulateMiddleButton = FALSE;
    mouse->transformCoordinates = TRUE;
    for (i = 0; i < KD_MAX_BUTTON; i++) mouse->map[i] = (char)(i + 1);
    input_open = TRUE;
    return TRUE;
}

void KdHalInputFini(void)
{
    if (!input_open) return;
    input_open = keyboard_open = pointer_open = FALSE;
    keyboard_pending = (PendingPacket){0};
    pointer_pending = (PendingPacket){0};
    modifiers = 0;
    dx = dy = 0;
    buttons = 0;
    hal_input_shutdown();
}

static void setKey(int scan, KeySym normal, KeySym shifted)
{
    KeySym *row = kdKeymap + (scan - kdMinScanCode) * KD_MAX_WIDTH;
    row[0] = normal;
    row[1] = shifted;
}

static void halKeyboardLoad(void)
{
    static const char unshifted[] = "1234567890-=[]\\;',./`";
    static const char shifted[] = "!@#$%^&*()_+{}|:\"<>?~";
    int key;
    size_t i;
    kdMinScanCode = SC_FIRST;
    kdMaxScanCode = SC_CAPS;
    memset(kdKeymap, 0, sizeof(kdKeymap));
    for (key = 0x20; key <= 0x7E; key++) setKey(keyScan((uint8_t)key), (KeySym)key, (KeySym)key);
    for (key = 'a'; key <= 'z'; key++)
        setKey(keyScan((uint8_t)key), (KeySym)key, (KeySym)(key - 'a' + 'A'));
    for (i = 0; i < sizeof(unshifted) - 1; i++)
        setKey(keyScan((uint8_t)unshifted[i]), (KeySym)unshifted[i], (KeySym)shifted[i]);
    setKey(SC_BACKSPACE, XK_BackSpace, XK_BackSpace);
    setKey(SC_ENTER, XK_Return, XK_Return);
    setKey(SC_ESCAPE, XK_Escape, XK_Escape);
    setKey(SC_LEFT, XK_Left, XK_Left);
    setKey(SC_RIGHT, XK_Right, XK_Right);
    setKey(SC_UP, XK_Up, XK_Up);
    setKey(SC_DOWN, XK_Down, XK_Down);
    setKey(SC_SHIFT, XK_Shift_L, XK_Shift_L);
    setKey(SC_CTRL, XK_Control_L, XK_Control_L);
    setKey(SC_ALT, XK_Alt_L, XK_Alt_L);
    setKey(SC_ALTGR, XK_Mode_switch, XK_Mode_switch);
    setKey(SC_SUPER, XK_Super_L, XK_Super_L);
    setKey(SC_CAPS, XK_Caps_Lock, XK_Caps_Lock);
}

static int halKeyboardInit(void)
{
    if (!input_open) return 0;
    keyboard_pending = (PendingPacket){0};
    modifiers = 0;
    keyboard_open = TRUE;
    return 1;
}

static void halKeyboardFini(void)
{
    keyboard_open = FALSE;
    keyboard_pending = (PendingPacket){0};
    modifiers = 0;
}

static void halKeyboardLeds(int value) { (void)value; }
static void halKeyboardBell(int volume, int pitch, int duration)
{
    (void)volume; (void)pitch; (void)duration;
}

void KdHalKeyboardPoll(void)
{
    static const struct { uint8_t mask, scan; } modifier_keys[] =
    {
        {HAL_MOD_SHIFT, SC_SHIFT}, {HAL_MOD_CTRL, SC_CTRL},
        {HAL_MOD_ALT, SC_ALT}, {HAL_MOD_ALTGR, SC_ALTGR},
        {HAL_MOD_SUPER, SC_SUPER}
    };
    uint8_t command, data;
    size_t i;
    if (!keyboard_open) return;
    while (readPacket(HAL_INPUT_KEYBOARD, &keyboard_pending, &command, &data))
    {
        if (command == HAL_CMD_MODIFIER)
        {
            uint8_t changed = data ^ modifiers;
            if (data & ~(HAL_MOD_SHIFT | HAL_MOD_CAPS | HAL_MOD_CTRL |
                         HAL_MOD_ALT | HAL_MOD_ALTGR | HAL_MOD_SUPER))
            {
                ErrorF("KDrive HAL: invalid modifier mask %u\n", data);
                continue;
            }
            for (i = 0; i < sizeof(modifier_keys) / sizeof(modifier_keys[0]); i++)
                if (changed & modifier_keys[i].mask)
                    KdEnqueueKeyboardEvent(modifier_keys[i].scan,
                                           (data & modifier_keys[i].mask) == 0);
            if (changed & HAL_MOD_CAPS)
            {
                KdEnqueueKeyboardEvent(SC_CAPS, FALSE);
                KdEnqueueKeyboardEvent(SC_CAPS, TRUE);
            }
            modifiers = data;
        }
        else if (command == HAL_CMD_KEY_DOWN || command == HAL_CMD_KEY_UP)
        {
            uint8_t scan = keyScan(data);
            if (scan != 0) KdEnqueueKeyboardEvent(scan, command == HAL_CMD_KEY_UP);
            else ErrorF("KDrive HAL: invalid keyboard packet %u/%u\n", command, data);
        }
        else ErrorF("KDrive HAL: invalid keyboard packet %u/%u\n", command, data);
    }
}

static Bool halMouseInit(void)
{
    if (!input_open) return FALSE;
    pointer_pending = (PendingPacket){0};
    buttons = 0;
    dx = dy = 0;
    pointer_open = TRUE;
    return TRUE;
}

static void halMouseFini(void)
{
    pointer_open = FALSE;
    pointer_pending = (PendingPacket){0};
    buttons = 0;
    dx = dy = 0;
}

static void wheel(int ticks, unsigned long button)
{
    int i, count = ticks < 0 ? -ticks : ticks;
    for (i = 0; i < count; i++)
    {
        KdEnqueueMouseEvent(kdMouseInfo, KD_MOUSE_DELTA | buttons | button, 0, 0);
        KdEnqueueMouseEvent(kdMouseInfo, KD_MOUSE_DELTA | buttons, 0, 0);
    }
}

void KdHalMousePoll(void)
{
    static const unsigned long button_map[] =
        {0, KD_BUTTON_1, KD_BUTTON_2, KD_BUTTON_3, KD_BUTTON_8, KD_BUTTON_9};
    uint8_t command, data;
    if (!pointer_open) return;
    while (readPacket(HAL_INPUT_MOUSE, &pointer_pending, &command, &data))
    {
        switch (command)
        {
            case HAL_CMD_MOUSE_X: dx = (int8_t)data; continue;
            case HAL_CMD_MOUSE_Y: dy = (int8_t)data; continue;
            case HAL_CMD_MOUSE_WHEEL_X:
                wheel((int8_t)data, (int8_t)data < 0 ? KD_BUTTON_6 : KD_BUTTON_7);
                continue;
            case HAL_CMD_MOUSE_WHEEL_Y:
                wheel((int8_t)data, (int8_t)data > 0 ? KD_BUTTON_4 : KD_BUTTON_5);
                continue;
            case HAL_CMD_MOUSE_BUTTON:
                if (data >= sizeof(button_map) / sizeof(button_map[0]))
                {
                    ErrorF("KDrive HAL: invalid button %u\n", data);
                    dx = dy = 0;
                    continue;
                }
                buttons = button_map[data];
                break;
            case HAL_CMD_MOUSE_Z:
                buttons = data != 0 ? KD_BUTTON_1 : 0;
                break;
            default:
                ErrorF("KDrive HAL: invalid pointer command %u\n", command);
                dx = dy = 0;
                continue;
        }
        KdEnqueueMouseEvent(kdMouseInfo, KD_MOUSE_DELTA | buttons, dx, dy);
        dx = dy = 0;
    }
}

const KdKeyboardFuncs KdHalKeyboardFuncs =
{
    .Load = halKeyboardLoad,
    .Init = halKeyboardInit,
    .Leds = halKeyboardLeds,
    .Bell = halKeyboardBell,
    .Fini = halKeyboardFini
};

const KdMouseFuncs KdHalMouseFuncs = {.Init = halMouseInit, .Fini = halMouseFini};
