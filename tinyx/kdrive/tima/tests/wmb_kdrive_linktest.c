/*
 * Compile and link the actual tima KDrive DDX against the real upstream
 * kdrive.h but no TinyX server and no SDL.  Server-entry symbols below
 * are intentionally minimal test doubles; production uses TinyX.
 */
#include "tima_kdrive.h"
#include "wmb_mock_hal.h"
#include "hal_display.h"
#include "hal_input.h"
#include "kkeymap.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* Core DDX entry points not declared by this vintage kdrive.h. */
void InitInput(int argc, char **argv);
void OsVendorInit(void);

KeySym kdKeymap[KD_MAX_LENGTH * KD_MAX_WIDTH];
int kdScreenPrivateIndex;
static KdCardInfo registered_card;
static KdMouseInfo mouse_info;
KdMouseInfo *kdMouseInfo = &mouse_info;

static unsigned keyboard_count, mouse_count, wheel_count;
static unsigned char last_key, last_up;
static unsigned long last_flags, last_button_flags;
static int last_dx, last_dy;
static const KdOsFuncs *registered_os;
static unsigned initialized_input;

KdCardInfo *KdCardInfoAdd(const KdCardFuncs *funcs, void *closure)
{
    memset(&registered_card, 0, sizeof(registered_card));
    registered_card.cfuncs = funcs;
    registered_card.closure = closure;
    return &registered_card;
}
void KdInitOutput(ScreenInfo *info, int argc, char **argv)
{ (void)info; (void)argc; (void)argv; }
void KdInitInput(const KdMouseFuncs *m, const KdKeyboardFuncs *k)
{ if (m && k) ++initialized_input; }
void KdOsInit(const KdOsFuncs *funcs) { registered_os = funcs; }
void KdEnqueueKeyboardEvent(unsigned char key, unsigned char is_up)
{
    last_key = key; last_up = is_up; ++keyboard_count;
}
void KdEnqueueMouseEvent(KdMouseInfo *mi, unsigned long flags, int x, int y)
{
    if (mi != &mouse_info) abort();
    last_flags = flags;
    last_button_flags = flags & (KD_BUTTON_1 | KD_BUTTON_2 |
                                  KD_BUTTON_3 | KD_BUTTON_4 | KD_BUTTON_5);
    last_dx = x; last_dy = y; ++mouse_count;
    if (flags & (KD_BUTTON_4 | KD_BUTTON_5)) ++wheel_count;
}
void FatalError(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    abort();
}

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
    printf("PASS: %s\n", message); \
} while (0)

int main(void)
{
    KdScreenInfo screen;
    const hal_framebuffer_t *fb;

    CHECK(hal_display_init(96, 64, HAL_PIXFMT_RGB565) == 0,
          "mock display initialises");
    CHECK(hal_input_init() == 0, "mock input initialises");
    InitCard(NULL);
    CHECK(registered_card.cfuncs == &tima_Funcs,
          "DDX registers with upstream KDrive entry point");
    CHECK(tima_CardInit(&registered_card), "DDX card reads HAL framebuffer");

    memset(&screen, 0, sizeof(screen));
    screen.card = &registered_card;
    CHECK(tima_ScreenInit(&screen), "DDX screen initialises");
    fb = hal_display_get_fb();
    CHECK(screen.fb.frameBuffer == (CARD8 *)fb->pixels &&
          screen.fb.bitsPerPixel == 16 && screen.fb.depth == 16 &&
          screen.fb.byteStride == (int)fb->stride &&
          screen.fb.pixelStride == (int)fb->width,
          "DDX uses HAL geometry, stride and depth");

    tima_KeyboardFuncs.Load();
    CHECK(tima_KeyboardFuncs.Init(), "keyboard uses pre-initialised HAL");
    CHECK(tima_MouseFuncs.Init(), "mouse uses pre-initialised HAL");

    wmb_mock_push(HAL_INPUT_KEYBOARD, HAL_CMD_KEY_DOWN, 'a');
    wmb_mock_push(HAL_INPUT_KEYBOARD, HAL_CMD_KEY_UP, 'a');
    tima_KeyboardPoll();
    CHECK(keyboard_count == 2 && last_key == 'a' && last_up,
          "keyboard presses/releases reach KDrive");

    wmb_mock_push(HAL_INPUT_MOUSE, HAL_CMD_MOUSE_X, 4);
    wmb_mock_push(HAL_INPUT_MOUSE, HAL_CMD_MOUSE_Y, (uint8_t)-3);
    tima_MousePoll();
    CHECK(mouse_count > 0 && last_dx == 4 && last_dy == -3 &&
          (last_flags & KD_MOUSE_DELTA),
          "mouse uses signed relative coordinates");

    wmb_mock_push(HAL_INPUT_MOUSE, HAL_CMD_MOUSE_BUTTON,
                  HAL_MOUSE_MASK_LEFT | HAL_MOUSE_MASK_RIGHT);
    tima_MousePoll();
    CHECK((last_button_flags & (KD_BUTTON_1 | KD_BUTTON_3)) ==
            (KD_BUTTON_1 | KD_BUTTON_3),
          "simultaneous button mask reaches KDrive");
    wmb_mock_push(HAL_INPUT_MOUSE, HAL_CMD_MOUSE_BUTTON, 0);
    tima_MousePoll();
    CHECK(last_button_flags == 0, "button release reaches KDrive");

    wmb_mock_push(HAL_INPUT_MOUSE, HAL_CMD_MOUSE_WHEEL_Y, 1);
    tima_MousePoll();
    CHECK(wheel_count > 0 && last_button_flags == 0,
          "vertical wheel is an X11 button pulse and release");

    OsVendorInit();
    CHECK(registered_os != NULL && registered_os->Init() == 1,
          "KDrive OS callback table registers");
    registered_os->Enable();
    wmb_mock_push(HAL_INPUT_KEYBOARD, HAL_CMD_KEY_DOWN, 'b');
    registered_os->pollEvents();
    CHECK(last_key == 'b', "KDrive OS poll drains HAL events");
    registered_os->Disable();
    registered_os->Fini();

    InitInput(0, NULL);
    CHECK(initialized_input == 1, "TinyX input registration hook delegates");
    tima_MouseFuncs.Fini();
    tima_KeyboardFuncs.Fini();
    tima_ScreenFini(&screen);
    tima_CardFini(&registered_card);
    hal_input_shutdown();
    hal_display_shutdown();
    puts("wmb_kdrive_linktest: ALL PASS");
    return 0;
}
