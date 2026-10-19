/*
 * Tima Keyboard Driver for KDrive
 *
 * This driver reads keyboard events from the HAL layer using the
 * keypad_driver API (keypad_driver_has_data / keypad_driver_read_data).
 *
 * Protocol from HAL:
 *   0x01 + modifier_key   - Modifier key event (Alt, Shift, etc.)
 *   0x02 + key_data       - Key press
 *   0x03 + key_data       - Key release
 *
 * Key data values (ASCII-based):
 *   0x20-0x7D  - Printable ASCII characters
 *   127 (0x7F) - Backspace
 *   13         - Enter
 *   27 (0x1B)  - Escape
 *   8          - Left arrow
 *   10         - Down arrow
 *   11         - Up arrow
 *   21         - Right arrow
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "kdrive.h"
#include "kkeymap.h"
#include <X11/keysym.h>
#include "hal_input.h"
#include "tima_kdrive.h"

/* HAL input is initialised by the WMB boot module. */

/*
 * We use the key_data value directly as the "scancode" index into kdKeymap.
 * This simplifies the mapping since we control both ends.
 */
int kdMinScanCode = 0;
int kdMaxScanCode = 127;

/*
 * Modifier key values (from control_key in 0x01 packets)
 * These match the flags in tima_input.h
 */
#define MOD_SHIFT       0x01
#define MOD_CAPS        0x02
#define MOD_CTRL        0x04
#define MOD_ALT         0x08
#define MOD_ALTGR       0x10
#define MOD_WIN         0x20

/*
 * Fake scancodes for modifier keys (we assign them to unused slots)
 */
#define SC_LSHIFT       HAL_SC_LSHIFT
#define SC_RSHIFT       HAL_SC_RSHIFT
#define SC_LCTRL        HAL_SC_LCTRL
#define SC_RCTRL        HAL_SC_RCTRL
#define SC_LALT         HAL_SC_LALT
#define SC_RALT         HAL_SC_RALT
#define SC_LGUI         HAL_SC_LSUPER
#define SC_RGUI         HAL_SC_RSUPER
#define SC_CAPSLOCK     HAL_SC_CAPSLOCK

/* Current modifier state for tracking press/release */
static unsigned char current_modifiers = 0;

/*
 * Build the keymap
 *
 * Since we use ASCII values as scancodes, the mapping is straightforward:
 * - For printable ASCII (0x20-0x7E), KeySym equals ASCII value
 * - For control characters, we map to appropriate X11 KeySyms
 *
 * kdKeymap layout per scancode:
 *   [0] = unshifted
 *   [1] = shifted
 *   [2] = mode_switch (AltGr) - not used
 *   [3] = mode_switch + shift - not used
 */

#define SET_KEY(sc, k0, k1) do { \
    if ((sc) < KD_MAX_LENGTH) { \
        KeySym *k = &kdKeymap[(sc) * KD_MAX_WIDTH]; \
        k[0] = (k0); k[1] = (k1); k[2] = NoSymbol; k[3] = NoSymbol; \
    } \
} while(0)

static void tima_KeyboardLoad(void)
{
    int i;

    /* Clear the entire keymap */
    for (i = 0; i < KD_MAX_LENGTH * KD_MAX_WIDTH; i++)
        kdKeymap[i] = NoSymbol;

    /*
     * Control characters used as key codes
     */
    SET_KEY(HAL_KEY_LEFT,      XK_Left,      XK_Left);
    SET_KEY(HAL_KEY_DOWN,      XK_Down,      XK_Down);
    SET_KEY(HAL_KEY_UP,        XK_Up,        XK_Up);
    SET_KEY(HAL_KEY_ENTER,     XK_Return,    XK_Return);
    SET_KEY(HAL_KEY_RIGHT,     XK_Right,     XK_Right);
    SET_KEY(HAL_KEY_ESCAPE,    XK_Escape,    XK_Escape);
    SET_KEY(HAL_KEY_BACKSPACE, XK_BackSpace, XK_BackSpace);

    /*
     * Printable ASCII characters (0x20 - 0x7E)
     * For these, KeySym == ASCII value!
     */

    /* Space */
    SET_KEY(' ', XK_space, XK_space);

    /* Numbers: unshifted = digit, shifted = symbol */
    SET_KEY('0', XK_0, XK_parenright);
    SET_KEY('1', XK_1, XK_exclam);
    SET_KEY('2', XK_2, XK_at);
    SET_KEY('3', XK_3, XK_numbersign);
    SET_KEY('4', XK_4, XK_dollar);
    SET_KEY('5', XK_5, XK_percent);
    SET_KEY('6', XK_6, XK_asciicircum);
    SET_KEY('7', XK_7, XK_ampersand);
    SET_KEY('8', XK_8, XK_asterisk);
    SET_KEY('9', XK_9, XK_parenleft);

    /* Lowercase letters -> unshifted=lower, shifted=upper */
    for (i = 0; i < 26; i++) {
        SET_KEY('a' + i, XK_a + i, XK_A + i);
    }

    /* Uppercase letters (when caps lock or shift is active) */
    /* Map them to same key as lowercase */
    for (i = 0; i < 26; i++) {
        SET_KEY('A' + i, XK_A + i, XK_a + i);
    }

    /* Punctuation and symbols */
    SET_KEY('!', XK_exclam,      XK_1);
    SET_KEY('@', XK_at,          XK_2);
    SET_KEY('#', XK_numbersign,  XK_3);
    SET_KEY('$', XK_dollar,      XK_4);
    SET_KEY('%', XK_percent,     XK_5);
    SET_KEY('^', XK_asciicircum, XK_6);
    SET_KEY('&', XK_ampersand,   XK_7);
    SET_KEY('*', XK_asterisk,    XK_8);
    SET_KEY('(', XK_parenleft,   XK_9);
    SET_KEY(')', XK_parenright,  XK_0);

    SET_KEY('-', XK_minus,        XK_underscore);
    SET_KEY('_', XK_underscore,   XK_minus);
    SET_KEY('=', XK_equal,        XK_plus);
    SET_KEY('+', XK_plus,         XK_equal);
    SET_KEY('[', XK_bracketleft,  XK_braceleft);
    SET_KEY(']', XK_bracketright, XK_braceright);
    SET_KEY('{', XK_braceleft,    XK_bracketleft);
    SET_KEY('}', XK_braceright,   XK_bracketright);
    SET_KEY('\\', XK_backslash,   XK_bar);
    SET_KEY('|', XK_bar,          XK_backslash);
    SET_KEY(';', XK_semicolon,    XK_colon);
    SET_KEY(':', XK_colon,        XK_semicolon);
    SET_KEY('\'', XK_apostrophe,  XK_quotedbl);
    SET_KEY('"', XK_quotedbl,     XK_apostrophe);
    SET_KEY('`', XK_grave,        XK_asciitilde);
    SET_KEY('~', XK_asciitilde,   XK_grave);
    SET_KEY(',', XK_comma,        XK_less);
    SET_KEY('<', XK_less,         XK_comma);
    SET_KEY('.', XK_period,       XK_greater);
    SET_KEY('>', XK_greater,      XK_period);
    SET_KEY('/', XK_slash,        XK_question);
    SET_KEY('?', XK_question,     XK_slash);

    /*
     * Modifier keys (fake scancodes)
     */
    SET_KEY(SC_LSHIFT,   XK_Shift_L,   XK_Shift_L);
    SET_KEY(SC_RSHIFT,   XK_Shift_R,   XK_Shift_R);
    SET_KEY(SC_LCTRL,    XK_Control_L, XK_Control_L);
    SET_KEY(SC_RCTRL,    XK_Control_R, XK_Control_R);
    SET_KEY(SC_LALT,     XK_Alt_L,     XK_Alt_L);
    SET_KEY(SC_RALT,     XK_Alt_R,     XK_Alt_R);
    SET_KEY(SC_LGUI,     XK_Super_L,   XK_Super_L);
    SET_KEY(SC_RGUI,     XK_Super_R,   XK_Super_R);
    SET_KEY(SC_CAPSLOCK, XK_Caps_Lock, XK_Caps_Lock);
}

/*
 * Handle modifier key packet (0x01 + modifier_mask)
 * We track modifier state changes and generate press/release events
 */
static void tima_HandleModifier(unsigned char new_modifiers)
{
    unsigned char changed = current_modifiers ^ new_modifiers;

    /* Check each modifier bit for changes */
    if (changed & MOD_SHIFT) {
        unsigned char is_up = (new_modifiers & MOD_SHIFT) ? 0 : 1;
        KdEnqueueKeyboardEvent(SC_LSHIFT, is_up);
    }
    if (changed & MOD_CTRL) {
        unsigned char is_up = (new_modifiers & MOD_CTRL) ? 0 : 1;
        KdEnqueueKeyboardEvent(SC_LCTRL, is_up);
    }
    if (changed & MOD_ALT) {
        unsigned char is_up = (new_modifiers & MOD_ALT) ? 0 : 1;
        KdEnqueueKeyboardEvent(SC_LALT, is_up);
    }
    if (changed & MOD_ALTGR) {
        unsigned char is_up = (new_modifiers & MOD_ALTGR) ? 0 : 1;
        KdEnqueueKeyboardEvent(SC_RALT, is_up);
    }
    if (changed & MOD_WIN) {
        unsigned char is_up = (new_modifiers & MOD_WIN) ? 0 : 1;
        KdEnqueueKeyboardEvent(SC_LGUI, is_up);
    }
    if (changed & MOD_CAPS) {
        unsigned char is_up = (new_modifiers & MOD_CAPS) ? 0 : 1;
        KdEnqueueKeyboardEvent(SC_CAPSLOCK, is_up);
    }

    current_modifiers = new_modifiers;
}

/*
 * tima_KeyboardInit - Initialize the keyboard driver
 */
static int tima_KeyboardInit(void)
{
    current_modifiers = 0;
    /* Device initialization happens once in hal_input_init(). */
    return 1;  /* Success */
}

/*
 * tima_KeyboardLeds - Set keyboard LEDs
 */
static void tima_KeyboardLeds(int leds)
{
    /* HAL doesn't support LEDs, ignore */
    (void)leds;
}

/*
 * tima_KeyboardBell - Generate keyboard bell
 */
static void tima_KeyboardBell(int volume, int pitch, int duration)
{
    /* HAL doesn't support bell, ignore */
    (void)volume;
    (void)pitch;
    (void)duration;
}

/*
 * tima_KeyboardFini - Cleanup keyboard driver
 */
static void tima_KeyboardFini(void)
{
    /* Nothing to clean up */
}

/*
 * KdKeyboardFuncs for Tima OS
 */
const KdKeyboardFuncs tima_KeyboardFuncs = {
    .Load    = tima_KeyboardLoad,
    .Init    = tima_KeyboardInit,
    .Leds    = tima_KeyboardLeds,
    .Bell    = tima_KeyboardBell,
    .Fini    = tima_KeyboardFini,
    .LockLed = 1  /* CapsLock LED */
};

/*
 * tima_KeyboardPoll - Process pending keyboard events from HAL
 *
 * Call this from the KdOsFuncs.pollEvents callback.
 * Reads from keypad_driver and dispatches to KDrive.
 */
void tima_KeyboardPoll(void)
{
    unsigned char cmd;
    unsigned char data;
    unsigned char is_up;

    /* Partial pairs may span polling passes; never consume half a pair. */
    static uint8_t pending_cmd = 0;
    while (hal_input_read_byte(HAL_INPUT_KEYBOARD, &data)) {
        if (!pending_cmd) {
            pending_cmd = data;
            continue;
        }
        cmd = pending_cmd;
        pending_cmd = 0;
        switch (cmd) {
        case HAL_CMD_MODIFIER:
            tima_HandleModifier(data);
            break;
        case HAL_CMD_KEY_DOWN:
        case HAL_CMD_KEY_UP:
            is_up = (cmd == HAL_CMD_KEY_UP) ? 1 : 0;
            KdEnqueueKeyboardEvent(data, is_up);
            break;
        default:
            break;
        }
    }
}

