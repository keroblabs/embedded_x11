#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "kdrive.h"
#include "tima_kdrive.h"
#include "hal_drivers.h"

static Bool os_initialized = FALSE;
static Bool os_enabled = FALSE;

static int tima_OsInit(void)
{
    os_initialized = TRUE;
    #ifdef DEBUG
    ErrorF("tima_OsInit: OS layer initialized\n");
    #endif
    return 1;
}

static void tima_OsEnable(void)
{
    if (!os_initialized) return;
    os_enabled = TRUE;
}

static void tima_OsDisable(void)
{
    if (!os_initialized) return;
    os_enabled = FALSE;
}

static Bool tima_OsSpecialKey(KeySym keysym)
{
    /* Return FALSE to pass all keys to clients */
    return FALSE;
}

static void tima_OsFini(void)
{
    os_enabled = FALSE;
    os_initialized = FALSE;
}

static void tima_OsPollEvents(void)
{
    if (!os_enabled) return;

    /* Poll all input devices */
    tima_KeyboardPoll();
    tima_MousePoll();
}

const KdOsFuncs tima_OsFuncs = {
    .Init       = tima_OsInit,
    .Enable     = tima_OsEnable,
    .SpecialKey = tima_OsSpecialKey,
    .Disable    = tima_OsDisable,
    .Fini       = tima_OsFini,
    .pollEvents = tima_OsPollEvents,
};

/*
 * InitCard - Called by TinyX to register graphics card
 * YOU implement this, TinyX calls it from main.c
 */
void InitCard(char *name)
{
    KdCardInfo *card;

    /* KdCardInfoAdd is a KDrive API function */
    card = KdCardInfoAdd(&tima_Funcs, NULL);
    if (!card) {
        FatalError("InitCard: Failed to register card\n");
    }
}

/*
 * InitOutput - Called by TinyX to initialize graphics output
 * YOU implement this, TinyX calls it from main.c
 */
void InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
    /* KdInitOutput is a KDrive API function */
    KdInitOutput(pScreenInfo, argc, argv);
}

/*
 * InitInput - Called by TinyX to initialize input devices
 * YOU implement this, TinyX calls it from main.c
 */
void InitInput(int argc, char **argv)
{
    /* KdInitInput is a KDrive API function that registers your drivers */
    KdInitInput(&tima_MouseFuncs, &tima_KeyboardFuncs);
}

/*
 * OsVendorInit - Called by TinyX to initialize OS-level services
 * YOU implement this, TinyX calls it from os/utils.c
 */
void OsVendorInit(void)
{
    /* KdOsInit is a KDrive API function */
    KdOsInit(&tima_OsFuncs);
}

void ddxUseMsg(void)
{
    #ifdef DEBUG
    ErrorF("\nTima driver options:\n");
    ErrorF("  -fb <addr>     Framebuffer base address (hex)\n");
    ErrorF("  -fbsize WxH    Framebuffer dimensions\n");
    #endif
}

int ddxProcessArgument(int argc, char **argv, int i)
{
    return 0;
}

void ddxGiveUp(void)
{
}

void AbortDDX(void)
{
}
