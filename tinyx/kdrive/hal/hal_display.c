#include <limits.h>
#include "kdrive_hal.h"
#include "damage.h"

typedef struct
{
    const hal_framebuffer_t *fb;
} HalCardPrivate;

typedef struct
{
    DamagePtr damage;
    DrawablePtr drawable;
} HalScreenPrivate;

static Bool halCardInit(KdCardInfo *card)
{
    HalCardPrivate *priv;
    const hal_framebuffer_t *fb;
    if (hal_display_init() != 0)
    {
        ErrorF("KDrive HAL: display initialization failed\n");
        return FALSE;
    }
    fb = hal_display_get_fb();
    if (fb == NULL || fb->pixels == NULL || fb->width == 0 || fb->height == 0 ||
        fb->width > SHRT_MAX || fb->height > SHRT_MAX || fb->stride > INT_MAX ||
        (fb->format != HAL_PIXFMT_RGB565 && fb->format != HAL_PIXFMT_XRGB8888) ||
        fb->bpp != (fb->format == HAL_PIXFMT_RGB565 ? 16 : 32) ||
        (uint64_t)fb->width * (fb->bpp / 8u) > fb->stride ||
        fb->stride % 4u != 0 || (uintptr_t)fb->pixels % 4u != 0 ||
        (uint64_t)fb->stride * fb->height > SIZE_MAX)
    {
        ErrorF("KDrive HAL: invalid/unsupported framebuffer descriptor\n");
        hal_display_shutdown();
        return FALSE;
    }
    priv = calloc(1, sizeof(*priv));
    if (priv == NULL)
    {
        ErrorF("KDrive HAL: card allocation failed\n");
        hal_display_shutdown();
        return FALSE;
    }
    priv->fb = fb;
    card->driver = priv;
    return TRUE;
}

static Bool halScreenInit(KdScreenInfo *screen)
{
    HalCardPrivate *card = screen->card->driver;
    const hal_framebuffer_t *fb;
    HalScreenPrivate *priv;
    if (card == NULL)
    {
        ErrorF("KDrive HAL: screen has no initialized card\n");
        return FALSE;
    }
    fb = card->fb;
    if (screen->driver != NULL || screen->mynum != 0 ||
        (screen->width != 0 && screen->width != (int)fb->width) ||
        (screen->height != 0 && screen->height != (int)fb->height) ||
        (screen->randr != 0 && screen->randr != RR_Rotate_0) ||
        (screen->fb.depth != 0 && screen->fb.depth != (fb->format == HAL_PIXFMT_RGB565 ? 16 : 24)) ||
        (screen->fb.bitsPerPixel != 0 && screen->fb.bitsPerPixel != fb->bpp))
    {
        ErrorF("KDrive HAL: requested geometry/format/rotation differs from the fixed display\n");
        return FALSE;
    }
    priv = calloc(1, sizeof(*priv));
    if (priv == NULL)
    {
        ErrorF("KDrive HAL: screen allocation failed\n");
        return FALSE;
    }
    screen->driver = priv;
    screen->width = (int)fb->width;
    screen->height = (int)fb->height;
    screen->width_mm = screen->width * 254 / 960;
    screen->height_mm = screen->height * 254 / 960;
    screen->randr = RR_Rotate_0;
    screen->softCursor = TRUE;
    screen->fb.depth = fb->format == HAL_PIXFMT_RGB565 ? 16 : 24;
    screen->fb.bitsPerPixel = fb->bpp;
    screen->fb.byteStride = (int)fb->stride;
    screen->fb.pixelStride = (int)(fb->stride / (fb->bpp / 8u));
    screen->fb.frameBuffer = fb->pixels;
    screen->fb.visuals = 1UL << TrueColor;
    screen->fb.redMask = fb->format == HAL_PIXFMT_RGB565 ? 0xF800 : 0xFF0000;
    screen->fb.greenMask = fb->format == HAL_PIXFMT_RGB565 ? 0x07E0 : 0x00FF00;
    screen->fb.blueMask = fb->format == HAL_PIXFMT_RGB565 ? 0x001F : 0x0000FF;
    screen->memory_base = fb->pixels;
    screen->memory_size = (unsigned long)fb->stride * fb->height;
    screen->off_screen_base = screen->memory_size;
    return TRUE;
}

static Bool halInitScreen(ScreenPtr screen)
{
    if (!DamageSetup(screen))
    {
        ErrorF("KDrive HAL: damage setup failed\n");
        return FALSE;
    }
    return TRUE;
}

static Bool halFinishInitScreen(ScreenPtr screen)
{
    (void)screen;
    return TRUE;
}

static void halDamageReport(DamagePtr damage, RegionPtr region, void *closure)
{
    long i, count = REGION_NUM_RECTS(region);
    BoxPtr boxes = REGION_RECTS(region);
    (void)damage; (void)closure;
    for (i = 0; i < count; i++)
    {
        BoxRec box = boxes[i];
        if (box.x2 > box.x1 && box.y2 > box.y1)
            hal_display_flush(box.x1, box.y1, (uint32_t)(box.x2 - box.x1),
                              (uint32_t)(box.y2 - box.y1));
    }
}

static void halDamageDestroyed(DamagePtr damage, void *closure)
{
    HalScreenPrivate *priv = closure;
    (void)damage;
    priv->damage = NULL;
    priv->drawable = NULL;
}

static Bool halCreateResources(ScreenPtr screen)
{
    KdScreenPriv(screen);
    HalScreenPrivate *priv = pScreenPriv->screen->driver;
    PixmapPtr pixmap = screen->GetScreenPixmap(screen);
    if (pixmap == NULL || priv == NULL)
    {
        ErrorF("KDrive HAL: screen pixmap/private state is unavailable\n");
        return FALSE;
    }
    if (priv->damage != NULL) return TRUE;
    priv->damage = DamageCreate(halDamageReport, halDamageDestroyed,
                                DamageReportRawRegion, TRUE, screen, priv);
    if (priv->damage == NULL)
    {
        ErrorF("KDrive HAL: damage allocation failed\n");
        return FALSE;
    }
    priv->drawable = &pixmap->drawable;
    DamageRegister(priv->drawable, priv->damage);
    return TRUE;
}

static Bool halEnable(ScreenPtr screen)
{
    KdScreenPriv(screen);
    KdScreenInfo *info = pScreenPriv->screen;
    hal_display_flush(0, 0, (uint32_t)info->width, (uint32_t)info->height);
    return TRUE;
}

static Bool halDPMS(ScreenPtr screen, int mode)
{
    (void)screen;
    if (mode < KD_DPMS_NORMAL || mode > KD_DPMS_POWERDOWN)
    {
        ErrorF("KDrive HAL: invalid DPMS mode\n");
        return FALSE;
    }
    return hal_display_set_power(mode == KD_DPMS_NORMAL) == 0;
}

static void halPreserve(KdCardInfo *card) { (void)card; }
static void halRestore(KdCardInfo *card) { (void)card; }
static void halDisable(ScreenPtr screen) { (void)screen; }

static void halScreenFini(KdScreenInfo *screen)
{
    HalScreenPrivate *priv = screen->driver;
    if (priv == NULL) return;
    if (priv->damage != NULL)
    {
        DamageUnregister(priv->drawable, priv->damage);
        DamageDestroy(priv->damage);
    }
    free(priv);
    screen->driver = NULL;
}

static void halCardFini(KdCardInfo *card)
{
    if (card->driver == NULL) return;
    hal_display_shutdown();
    free(card->driver);
    card->driver = NULL;
}

const KdCardFuncs KdHalCardFuncs =
{
    .cardinit = halCardInit,
    .scrinit = halScreenInit,
    .initScreen = halInitScreen,
    .finishInitScreen = halFinishInitScreen,
    .createRes = halCreateResources,
    .preserve = halPreserve,
    .enable = halEnable,
    .dpms = halDPMS,
    .disable = halDisable,
    .restore = halRestore,
    .scrfini = halScreenFini,
    .cardfini = halCardFini
};
