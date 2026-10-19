/* WMB's own KDrive DDX hooks; no SDL/backend header is included. */
#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "kdrive.h"
#include "tima_kdrive.h"
#include "hal_input.h"

static Bool os_initialized = FALSE;
static Bool os_enabled = FALSE;

static int tima_OsInit(void)
{
    os_initialized = TRUE;
    return 1;
}

static void tima_OsEnable(void)
{
    if (os_initialized)
        os_enabled = TRUE;
}
static void tima_OsDisable(void) { os_enabled = FALSE; }
static Bool tima_OsSpecialKey(KeySym keysym)
{
    (void)keysym;
    return FALSE;
}
static void tima_OsFini(void)
{
    os_enabled = FALSE;
    os_initialized = FALSE;
}
static void tima_OsPollEvents(void)
{
    if (!os_enabled)
        return;
    /* The HAL fiber pumps SDL/hardware; DDX drains the HAL queue. */
    tima_KeyboardPoll();
    tima_MousePoll();
}
const KdOsFuncs tima_OsFuncs = {
    .Init = tima_OsInit, .Enable = tima_OsEnable,
    .SpecialKey = tima_OsSpecialKey,
    .Disable = tima_OsDisable, .Fini = tima_OsFini,
    .pollEvents = tima_OsPollEvents
};

void InitCard(char *name)
{
    (void)name;
    if (!KdCardInfoAdd(&tima_Funcs, NULL))
        FatalError("WMB KDrive: cannot register framebuffer card\n");
}
void InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
    KdInitOutput(pScreenInfo, argc, argv);
}
void InitInput(int argc, char **argv)
{
    (void)argc; (void)argv;
    KdInitInput(&tima_MouseFuncs, &tima_KeyboardFuncs);
}
void OsVendorInit(void)
{
    KdOsInit(&tima_OsFuncs);
}
void ddxUseMsg(void)
{
    /* Geometry is configured by WMB boot and hal_display_init(). */
}
int ddxProcessArgument(int argc, char **argv, int i)
{
    (void)argc; (void)argv; (void)i;
    return 0;
}
void ddxGiveUp(void) {}
void AbortDDX(void) {}
