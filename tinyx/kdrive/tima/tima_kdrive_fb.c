/*
 * WMB KDrive framebuffer DDX.  This is our own driver; upstream KDrive
 * and fb/mi rendering are left untouched.
 */
#include "tima_kdrive.h"
#include "hal_display.h"
#include "hal_alloc.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>

static tima_Priv_t *tima_card_private(KdCardInfo *card)
{
    return card ? (tima_Priv_t *)card->driver : NULL;
}

Bool tima_CardInit(KdCardInfo *card)
{
    const hal_framebuffer_t *fb = hal_display_get_fb();
    tima_Priv_t *priv;
    uint32_t bytes_per_pixel;

    /* wmb_boot owns hal_display_init(): never initialise SDL here. */
    if (!card || !fb || !fb->pixels || !fb->width || !fb->height)
        return FALSE;
    if (fb->format != HAL_PIXFMT_RGB565 &&
        fb->format != HAL_PIXFMT_XRGB8888)
        return FALSE;
    if ((fb->format == HAL_PIXFMT_RGB565 && fb->bpp != 16) ||
        (fb->format == HAL_PIXFMT_XRGB8888 && fb->bpp != 32))
        return FALSE;

    bytes_per_pixel = (uint32_t)fb->bpp / 8u;
    if (fb->stride % bytes_per_pixel ||
        fb->stride < fb->width * bytes_per_pixel ||
        fb->stride / bytes_per_pixel > INT_MAX ||
        fb->width > INT_MAX || fb->height > INT_MAX ||
        fb->height > UINT32_MAX / fb->stride)
        return FALSE;

    priv = (tima_Priv_t *)wmb_alloc(sizeof(*priv));
    if (!priv)
        return FALSE;
    memset(priv, 0, sizeof(*priv));

    priv->fb_base = fb->pixels;
    priv->fb = fb->pixels;
    priv->fb_size = fb->stride * fb->height;
    priv->width = fb->width;
    priv->height = fb->height;
    priv->bpp = fb->bpp;
    priv->pitch = fb->stride;

    if (fb->format == HAL_PIXFMT_XRGB8888) {
        priv->red_mask = 0x00ff0000u;
        priv->green_mask = 0x0000ff00u;
        priv->blue_mask = 0x000000ffu;
        priv->red_offset = 16;
        priv->green_offset = 8;
        priv->blue_offset = 0;
        priv->red_length = priv->green_length = priv->blue_length = 8;
    } else {
        priv->red_mask = 0xf800u;
        priv->green_mask = 0x07e0u;
        priv->blue_mask = 0x001fu;
        priv->red_offset = 11;
        priv->green_offset = 5;
        priv->blue_offset = 0;
        priv->red_length = 5;
        priv->green_length = 6;
        priv->blue_length = 5;
    }

    card->driver = priv;
    return TRUE;
}

Bool tima_ScreenInit(KdScreenInfo *screen)
{
    tima_Priv_t *priv;
    tima_ScrPriv_t *sp;

    if (!screen || !screen->card ||
        !(priv = tima_card_private(screen->card)))
        return FALSE;

    sp = (tima_ScrPriv_t *)wmb_alloc(sizeof(*sp));
    if (!sp)
        return FALSE;
    memset(sp, 0, sizeof(*sp));
    sp->randr = RR_Rotate_0;
    sp->shadow = FALSE;
    screen->driver = sp;

    screen->width = (int)priv->width;
    screen->height = (int)priv->height;
    screen->rate = 60;
    screen->width_mm = (screen->width * 254) / 960;
    screen->height_mm = (screen->height * 254) / 960;

    /* Depth is not storage bpp: XRGB8888 is depth 24 on 32-bit pixels. */
    screen->fb.depth = (priv->bpp == 32) ? 24 : 16;
    screen->fb.bitsPerPixel = (int)priv->bpp;
    screen->fb.byteStride = (int)priv->pitch;
    screen->fb.pixelStride = (int)(priv->pitch / (priv->bpp / 8u));
    screen->fb.frameBuffer = (CARD8 *)priv->fb;
    screen->fb.shadow = FALSE;
    screen->fb.visuals = (1UL << TrueColor);
    screen->fb.redMask = priv->red_mask;
    screen->fb.greenMask = priv->green_mask;
    screen->fb.blueMask = priv->blue_mask;

    screen->memory_base = (CARD8 *)priv->fb;
    screen->memory_size = priv->fb_size;
    screen->off_screen_base = priv->fb_size;
    return TRUE;
}

Bool tima_InitScreen(ScreenPtr screen)
{
    (void)screen;
    return TRUE;
}

Bool tima_FinishInitScreen(ScreenPtr screen)
{
    /* Direct framebuffer: no shadow layer to initialise. */
    (void)screen;
    return TRUE;
}

Bool tima_CreateResources(ScreenPtr screen)
{
    (void)screen;
    return TRUE;
}

void tima_Preserve(KdCardInfo *card) { (void)card; }
void tima_Restore(KdCardInfo *card) { (void)card; }

Bool tima_Enable(ScreenPtr screen)
{
    KdScreenPriv(screen);
    tima_Priv_t *priv = tima_card_private(pScreenPriv->card);
    if (!priv || !priv->fb)
        return FALSE;
    memset(priv->fb, 0, priv->fb_size);
    hal_display_flush(0, 0, priv->width, priv->height);
    return TRUE;
}

Bool tima_DPMS(ScreenPtr screen, int mode)
{
    (void)screen;
    if (mode == KD_DPMS_NORMAL)
        (void)hal_display_set_power(1);
    else
        (void)hal_display_set_power(0);
    return TRUE;  /* Power control is optional; rendering still works. */
}

void tima_Disable(ScreenPtr screen) { (void)screen; }

void tima_ScreenFini(KdScreenInfo *screen)
{
    if (screen && screen->driver) {
        wmb_free(screen->driver);
        screen->driver = NULL;
    }
}

void tima_CardFini(KdCardInfo *card)
{
    if (card && card->driver) {
        /* Framebuffer memory belongs to HAL, not KDrive. */
        wmb_free(card->driver);
        card->driver = NULL;
    }
}

void tima_GetColors(ScreenPtr screen, int n, xColorItem *colors)
{
    (void)screen;
    while (n-- > 0) {
        /* TrueColor uses masks, not programmable colormap hardware. */
        colors->red = colors->green = colors->blue = 0;
        ++colors;
    }
}
void tima_PutColors(ScreenPtr screen, int n, xColorItem *colors)
{
    (void)screen; (void)n; (void)colors;
}

const KdCardFuncs tima_Funcs = {
    .cardinit = tima_CardInit, .scrinit = tima_ScreenInit,
    .initScreen = tima_InitScreen,
    .finishInitScreen = tima_FinishInitScreen,
    .createRes = tima_CreateResources, .preserve = tima_Preserve,
    .enable = tima_Enable, .dpms = tima_DPMS, .disable = tima_Disable,
    .restore = tima_Restore, .scrfini = tima_ScreenFini,
    .cardfini = tima_CardFini, .getColors = tima_GetColors,
    .putColors = tima_PutColors
};
