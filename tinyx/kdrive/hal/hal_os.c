#include "kdrive_hal.h"
#include "opaque.h"

static Bool os_initialized, os_enabled;

static int halOsInit(void)
{
    os_initialized = TRUE;
    return 1;
}

static void halOsEnable(void) { os_enabled = os_initialized; }
static void halOsDisable(void) { os_enabled = FALSE; }
static Bool halOsSpecialKey(KeySym key) { (void)key; return FALSE; }

static void halOsFini(void)
{
    KdHalInputFini();
    hal_display_shutdown();
    os_enabled = os_initialized = FALSE;
}

static void halOsPollEvents(void)
{
    hal_display_status_t status;
    if (!os_enabled) return;
    hal_input_pump();
    KdHalKeyboardPoll();
    KdHalMousePoll();
    status = hal_display_pump();
    if (status != HAL_DISPLAY_ACTIVE)
    {
        if (status == HAL_DISPLAY_ERROR) ErrorF("KDrive HAL: display pump failed\n");
        dispatchException |= DE_TERMINATE;
        isItTimeToYield = TRUE;
    }
}

const KdOsFuncs KdHalOsFuncs =
{
    .Init = halOsInit, .Enable = halOsEnable, .SpecialKey = halOsSpecialKey,
    .Disable = halOsDisable, .Fini = halOsFini, .pollEvents = halOsPollEvents
};

void InitCard(char *name)
{
    (void)name;
    if (kdCardInfo != NULL) FatalError("KDrive HAL supports one fixed display\n");
    if (KdCardInfoAdd(&KdHalCardFuncs, NULL) == NULL)
        FatalError("KDrive HAL card registration failed\n");
}

void InitOutput(ScreenInfo *info, int argc, char **argv)
{
    KdInitOutput(info, argc, argv);
}

void InitInput(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!KdHalInputInit()) FatalError("KDrive HAL input initialization failed\n");
    KdInitInput(&KdHalMouseFuncs, &KdHalKeyboardFuncs);
}

void OsVendorInit(void) { KdOsInit(&KdHalOsFuncs); }
void ddxUseMsg(void) { KdUseMsg(); }
int ddxProcessArgument(int argc, char **argv, int i) { return KdProcessArgument(argc, argv, i); }
