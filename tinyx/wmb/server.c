#include "server.h"
#include "kdrive.h"
#include "kdrive_hal.h"
#include "windowstr.h"
#include "opaque.h"
#include "hal_display.h"
#include "hal_task.h"
#include "wmb_bridge.h"

extern int WmbTinyXRun(int argc, char **argv);
_Static_assert(sizeof(KeySym) == 4, "Server protocol types require consistent configuration");
static wmb_tinyx_server_t *server;
static hal_task_t *server_task;
static const char *const expected_extensions[] = {
    "SHAPE", "XTEST", "BIG-REQUESTS", "MIT-SCREEN-SAVER", "SYNC", "XC-MISC",
    "DPMS", "XFIXES", "RENDER", "RANDR", "X-Resource", "DAMAGE"
};
static unsigned int extensions_seen;

void TinyX_ServerFiber(void *argument)
{
    server = argument;
    server_task = hal_task_self();
    extensions_seen = 0;
    server->result = WmbTinyXRun(server->argc, server->argv);
    if (KdHalDisplayStatus() == HAL_DISPLAY_ERROR) server->result = 1;
    server->finished = true;
    wmb_bridge_server_ready(false);
    server_task = NULL;
}

bool WmbTinyXServerRootValid(void)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    WindowPtr root = WindowTable[0];
    return fb != NULL && screenInfo.numScreens == 1 && root != NULL &&
        root->mapped && root->realized &&
        root->drawable.width == fb->width && root->drawable.height == fb->height;
}

void WmbTinyXServerReady(void)
{
    ScreenPtr screen = screenInfo.screens[0];
    int i;
    if (!WmbTinyXServerRootValid())
        FatalError("WMB TinyX: root window does not match the native display\n");
    if (extensions_seen != (1u << (sizeof(expected_extensions) /
                                  sizeof(expected_extensions[0]))) - 1u)
        FatalError("WMB TinyX: registered extensions do not match the P4 profile\n");
    ErrorF("WMB TinyX: root %dx%d depth=%d visuals=%d rootVisual=0x%lx\n",
        screen->width, screen->height, screen->rootDepth, screen->numVisuals,
        (unsigned long)screen->rootVisual);
    for (i = 0; i < screen->numVisuals; i++)
        ErrorF("WMB TinyX: visual class=%d depth-bits=%d masks=%lx/%lx/%lx\n",
            screen->visuals[i].class, screen->visuals[i].bitsPerRGBValue,
            screen->visuals[i].redMask, screen->visuals[i].greenMask,
            screen->visuals[i].blueMask);
    server->ready = true;
    wmb_bridge_server_ready(true);
}

void WmbTinyXExtensionAdded(const char *name, int opcode)
{
    size_t i;
    for (i = 0; i < sizeof(expected_extensions) / sizeof(expected_extensions[0]); i++)
        if (strcmp(name, expected_extensions[i]) == 0) {
            extensions_seen |= 1u << i;
            ErrorF("WMB TinyX: extension %s opcode=%d\n", name, opcode);
            return;
        }
    FatalError("WMB TinyX: unexpected extension %s\n", name);
}

void WmbTinyXServerStop(void)
{
    dispatchException |= DE_TERMINATE;
    isItTimeToYield = TRUE;
    if (server_task != NULL) hal_task_wake(server_task);
}
