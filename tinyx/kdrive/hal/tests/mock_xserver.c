#include "mock_xserver.h"
#include "opaque.h"
#include "window.h"
#include "colormap.h"
#include <stdarg.h>
#include <stdlib.h>

MockXServerState mock_xserver;
long mock_calloc_failure_after = -1;
ScreenInfo screenInfo;
InputInfo inputInfo;
WindowPtr WindowTable[MAXSCREENS];
BoxRec miEmptyBox;
RegDataRec miEmptyData, miBrokenData;
unsigned long serverGeneration = 1;
volatile char dispatchException, isItTimeToYield;
KeybdCtrl defaultKeyboardControl = {.autoRepeat = TRUE};
PtrCtrl defaultPointerControl = {.num = 1, .den = 1, .threshold = 4};

static int device_count;
int rrPrivIndex = 1;
static rrScrPrivRec mock_rr;
static RRScreenSize mock_rr_size;
#ifdef SMART_SCHEDULE
Bool SmartScheduleDisable = TRUE;
long SmartScheduleTime;
#endif

#if defined(WMB_ALLOCATION_TRACKED)
void *__real_allocation_calloc(unsigned owner, size_t count, size_t size);
void *__wrap_allocation_calloc(unsigned owner, size_t count, size_t size)
{
    if (mock_calloc_failure_after == 0) return NULL;
    if (mock_calloc_failure_after > 0) mock_calloc_failure_after--;
    return __real_allocation_calloc(owner, count, size);
}
#endif
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size)
{
    if (mock_calloc_failure_after == 0) return NULL;
    if (mock_calloc_failure_after > 0) mock_calloc_failure_after--;
    return __real_calloc(count, size);
}

static PixmapPtr getScreenPixmap(ScreenPtr screen) { (void)screen; return &mock_xserver.pixmap; }
static Bool closeScreen(int index, ScreenPtr screen)
{
    (void)index; (void)screen;
    if (mock_xserver.damage != NULL)
        FatalError("HAL Damage must be detached before the framebuffer closes\n");
    return TRUE;
}

void MockXServerReset(KdCardInfo *card, KdScreenInfo *screen)
{
    mock_xserver = (MockXServerState){0};
    mock_xserver.time = 100;
    mock_xserver.screen.width = screen->width;
    mock_xserver.screen.height = screen->height;
    mock_xserver.screen.GetScreenPixmap = getScreenPixmap;
    mock_xserver.screen.CloseScreen = closeScreen;
    mock_xserver.screen.devPrivates = mock_xserver.privates;
    mock_xserver.privates[0].ptr = &mock_xserver.screen_private;
    mock_rr = (rrScrPrivRec){0};
    mock_xserver.privates[1].ptr = &mock_rr;
    mock_xserver.screen_private.card = card;
    mock_xserver.screen_private.screen = screen;
    mock_xserver.screen_private.enabled = TRUE;
    mock_xserver.pixmap.drawable.type = DRAWABLE_PIXMAP;
    mock_xserver.pixmap.drawable.pScreen = &mock_xserver.screen;
    mock_xserver.pixmap.drawable.width = screen->width;
    mock_xserver.pixmap.drawable.height = screen->height;
    mock_xserver.pixmap.devPrivate.ptr = screen->fb.frameBuffer;
    mock_xserver.pixmap.devKind = screen->fb.byteStride;
    screen->pScreen = &mock_xserver.screen;
    kdScreenPrivateIndex = 0;
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &mock_xserver.screen;
    inputInfo = (InputInfo){0};
    device_count = 0;
    dispatchException = isItTimeToYield = 0;
}

Bool RRScreenInit(ScreenPtr screen) { (void)screen; return TRUE; }

RRScreenSizePtr RRRegisterSize(ScreenPtr screen, short width, short height,
                              short mm_width, short mm_height)
{
    (void)screen;
    mock_rr_size = (RRScreenSize){.width = width, .height = height,
                                  .mmWidth = mm_width, .mmHeight = mm_height};
    return &mock_rr_size;
}

void RRSetCurrentConfig(ScreenPtr screen, Rotation rotation, int rate,
                       RRScreenSizePtr size)
{
    (void)screen; (void)size;
    mock_rr.rotation = rotation;
    mock_rr.rate = rate;
}

void ErrorF(const char *format, ...)
{
    va_list args;
    mock_xserver.errors++;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void FatalError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    abort();
}

CARD32 GetTimeInMillis(void) { return mock_xserver.time; }
void NoticeEventTime(xEventPtr event) { (void)event; }
void AdjustWaitForDelay(pointer timeout, unsigned long delay) { (void)timeout; mock_xserver.adjusted_wait = delay; }
void NoopDDA(void) {}
void UseMsg(void) { ErrorF("Unexpected usage request in the KDrive harness\n"); }

DeviceIntPtr AddInputDevice(DeviceProc proc, Bool auto_start)
{
    DeviceIntPtr dev;
    (void)auto_start;
    if (device_count >= 2) FatalError("Too many mock devices\n");
    dev = device_count++ == 0 ? &mock_xserver.pointer : &mock_xserver.keyboard;
    dev->deviceProc = proc;
    return dev;
}

void RegisterPointerDevice(DeviceIntPtr dev) { inputInfo.pointer = dev; }
void RegisterKeyboardDevice(DeviceIntPtr dev) { inputInfo.keyboard = dev; }
DevicePtr LookupKeyboardDevice(void) { return inputInfo.keyboard ? &inputInfo.keyboard->public : NULL; }
DevicePtr LookupPointerDevice(void) { return inputInfo.pointer ? &inputInfo.pointer->public : NULL; }

Bool InitPointerDeviceStruct(DevicePtr dev, CARD8 *map, int count,
                             ValuatorMotionProcPtr motion, PtrCtrlProcPtr control, int events)
{
    int i;
    (void)motion; (void)control; (void)events;
    if (mock_xserver.fail_pointer_init) return FALSE;
    if (dev != &mock_xserver.pointer.public || count > KD_MAX_BUTTON) FatalError("Invalid mock pointer init\n");
    mock_xserver.pointer.ptrfeed = &mock_xserver.ptrfeed;
    mock_xserver.pointer.button = &mock_xserver.button;
    mock_xserver.ptrfeed.ctrl = defaultPointerControl;
    mock_xserver.button.numButtons = count;
    for (i = 1; i <= count; i++) mock_xserver.button.map[i] = map[i];
    return TRUE;
}

Bool InitKeyboardDeviceStruct(DevicePtr dev, KeySymsPtr syms, CARD8 *map,
                              BellProcPtr bell, KbdCtrlProcPtr control)
{
    (void)bell; (void)control;
    if (mock_xserver.fail_keyboard_init) return FALSE;
    if (dev != &mock_xserver.keyboard.public) FatalError("Invalid mock keyboard init\n");
    mock_xserver.keyboard.key = &mock_xserver.key;
    mock_xserver.keyboard.kbdfeed = &mock_xserver.kbdfeed;
    mock_xserver.key.curKeySyms = *syms;
    memcpy(mock_xserver.key.modifierMap, map, sizeof(mock_xserver.key.modifierMap));
    mock_xserver.kbdfeed.ctrl = defaultKeyboardControl;
    return TRUE;
}

void MockXServerStartDevices(void)
{
    if (inputInfo.pointer->deviceProc(inputInfo.pointer, DEVICE_INIT) != Success ||
        inputInfo.keyboard->deviceProc(inputInfo.keyboard, DEVICE_INIT) != Success ||
        inputInfo.pointer->deviceProc(inputInfo.pointer, DEVICE_ON) != Success ||
        inputInfo.keyboard->deviceProc(inputInfo.keyboard, DEVICE_ON) != Success)
        FatalError("Mock device startup failed\n");
}

void MockXServerStopDevices(void)
{
    if (inputInfo.pointer) inputInfo.pointer->deviceProc(inputInfo.pointer, DEVICE_OFF);
    if (inputInfo.keyboard) inputInfo.keyboard->deviceProc(inputInfo.keyboard, DEVICE_OFF);
}

void NoteLedState(DeviceIntPtr dev, int led, Bool on)
{
    if (on) dev->kbdfeed->ctrl.leds |= 1UL << (led - 1);
    else dev->kbdfeed->ctrl.leds &= ~(1UL << (led - 1));
}

int miPointerGetMotionBufferSize(void) { return 0; }
int miPointerGetMotionEvents(DeviceIntPtr dev, xTimecoord *coords, unsigned long start,
                             unsigned long stop, ScreenPtr screen)
{
    (void)dev; (void)coords; (void)start; (void)stop; (void)screen;
    return 0;
}
void miRegisterPointerDevice(ScreenPtr screen, DeviceIntPtr dev) { (void)screen; (void)dev; }
Bool mieqInit(DevicePtr keyboard, DevicePtr pointer) { (void)keyboard; (void)pointer; return TRUE; }

void mieqEnqueue(xEventPtr event)
{
    unsigned int modifier;
    if (mock_xserver.event_count >= 2048) FatalError("Mock event queue overflow\n");
    mock_xserver.events[mock_xserver.event_count++] = *event;
    if (event->u.u.type == KeyPress || event->u.u.type == KeyRelease)
    {
        modifier = mock_xserver.key.modifierMap[event->u.u.detail] & ~(LockMask | Mod2Mask);
        if (event->u.u.type == KeyPress) mock_xserver.key.state |= modifier;
        else mock_xserver.key.state &= ~modifier;
    }
}

void miPointerAbsoluteCursor(int x, int y, unsigned long time)
{
    xEvent event = {0};
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= mock_xserver.screen.width) x = mock_xserver.screen.width - 1;
    if (y >= mock_xserver.screen.height) y = mock_xserver.screen.height - 1;
    if (x == mock_xserver.pointer_x && y == mock_xserver.pointer_y) return;
    mock_xserver.pointer_x = x;
    mock_xserver.pointer_y = y;
    event.u.u.type = MotionNotify;
    event.u.keyButtonPointer.time = time;
    event.u.keyButtonPointer.rootX = x;
    event.u.keyButtonPointer.rootY = y;
    mieqEnqueue(&event);
}

void miPointerDeltaCursor(int dx, int dy, unsigned long time)
{
    mock_xserver.last_dx = dx;
    mock_xserver.last_dy = dy;
    miPointerAbsoluteCursor(mock_xserver.pointer_x + dx, mock_xserver.pointer_y + dy, time);
}

void miPointerPosition(int *x, int *y) { *x = mock_xserver.pointer_x; *y = mock_xserver.pointer_y; }
void miPointerWarpCursor(ScreenPtr screen, int x, int y)
{
    (void)screen;
    miPointerAbsoluteCursor(x, y, mock_xserver.time);
}
void mieqProcessInputEvents(void) {}
void miPointerUpdate(void) {}

/* These rendering services are link dependencies of unexercised server paths. */
RegionPtr miRegionCreate(BoxPtr box, int size)
{
    (void)box; (void)size;
    FatalError("Full X-server region rendering is outside this harness\n");
}

Bool miSubtract(RegionPtr dst, RegionPtr lhs, RegionPtr rhs)
{
    (void)dst; (void)lhs; (void)rhs;
    FatalError("Full X-server region rendering is outside this harness\n");
}

Bool miRegionBreak(RegionPtr region)
{
    (void)region;
    FatalError("Full X-server region rendering is outside this harness\n");
}

void ResizeChildrenWinSize(WindowPtr window, int dx, int dy, int dw, int dh)
{
    (void)window; (void)dx; (void)dy; (void)dw; (void)dh;
    FatalError("Full X-server window rendering is outside this harness\n");
}

void WindowsRestructured(void)
{
    FatalError("Full X-server window rendering is outside this harness\n");
}

int QueryColors(ColormapPtr map, int count, Pixel *pixels, xrgb *colours)
{
    (void)map; (void)count; (void)pixels; (void)colours;
    FatalError("Full X-server colormap rendering is outside this harness\n");
}

Bool DamageSetup(ScreenPtr screen) { (void)screen; return !mock_xserver.fail_damage_setup; }
DamagePtr DamageCreate(DamageReportFunc report, DamageDestroyFunc destroy,
                       DamageReportLevel level, Bool internal, ScreenPtr screen, void *closure)
{
    DamagePtr damage;
    (void)level; (void)internal; (void)screen;
    if (mock_xserver.fail_damage_create) return NULL;
    damage = calloc(1, sizeof(*damage));
    if (damage == NULL) FatalError("Mock damage allocation failed\n");
    damage->damageReport = report;
    damage->damageDestroy = destroy;
    damage->damageLevel = level;
    damage->isInternal = internal;
    damage->closure = closure;
    mock_xserver.damage = damage;
    return damage;
}

void DamageRegister(DrawablePtr drawable, DamagePtr damage)
{
    damage->pDrawable = drawable;
    mock_xserver.damage_registers++;
}
void DamageUnregister(DrawablePtr drawable, DamagePtr damage)
{
    if (damage->pDrawable != drawable) FatalError("Invalid mock damage unregister\n");
    damage->pDrawable = NULL;
}
void DamageEmpty(DamagePtr damage) { damage->damage = (RegionRec){0}; }
void DamageDestroy(DamagePtr damage)
{
    damage->damageDestroy(damage, damage->closure);
    mock_xserver.damage_destroys++;
    mock_xserver.damage = NULL;
    free(damage);
}
void MockXServerReportDamage(RegionPtr region)
{
    if (mock_xserver.damage == NULL) FatalError("No registered mock damage callback\n");
    mock_xserver.damage->damageReport(mock_xserver.damage, region, mock_xserver.damage->closure);
}
