#include "tima_kdrive.h"
#include "hal_drivers.h"
#include <string.h>

#ifndef K_TIMA_FB_WIDTH
#define K_TIMA_FB_WIDTH     lcd_driver_get_width()
#endif

#ifndef K_TIMA_FB_HEIGHT
#define K_TIMA_FB_HEIGHT    lcd_driver_get_height()
#endif

#ifndef K_TIMA_FB_BPP
#define K_TIMA_FB_BPP       (lcd_driver_get_bits_per_pixel())
#endif

#ifndef K_TIMA_FB_BASE
#define K_TIMA_FB_BASE      lcd_driver_get_framebuffer()
#endif


/*
 * tima_CardInit - Initialize the graphics card/framebuffer
 *
 * Called once per card at server startup.
 * Detects hardware and sets up basic card info.
 *
 * @param card: Kdrive card info structure
 * @return: TRUE on success, FALSE on failure
 */
Bool tima_CardInit(KdCardInfo *card)
{
    tima_Priv_t *priv;

    lcd_driver_init();

    /* Allocate private data */
    priv = (tima_Priv_t *)malloc(sizeof(tima_Priv_t));
    if (!priv)
    {
        #ifdef DEBUG
        ErrorF("tima_CardInit: Failed to allocate private data\n");
        #endif
        return FALSE;
    }

    memset(priv, 0, sizeof(tima_Priv_t));

    /*
     * PLATFORM-SPECIFIC: Initialize framebuffer
     *
     * On bare-metal, framebuffer is typically at a fixed address
     * defined by hardware or bootloader configuration.
     */
    priv->fb_base = (void *)K_TIMA_FB_BASE;
    priv->fb = priv->fb_base;  /* May need offset for alignment */

    /* Set display parameters */
    priv->width = K_TIMA_FB_WIDTH;
    priv->height = K_TIMA_FB_HEIGHT;
    priv->bpp = K_TIMA_FB_BPP;
    priv->pitch = priv->width * (priv->bpp / 8);
    priv->fb_size = priv->pitch * priv->height;

    /* RGB888 or RGBA8888 format (typical for 32bpp) */
    if (priv->bpp == 32)
    {
        priv->red_offset = 16;
        priv->red_length = 8;
        priv->red_mask = 0x00FF0000;

        priv->green_offset = 8;
        priv->green_length = 8;
        priv->green_mask = 0x0000FF00;

        priv->blue_offset = 0;
        priv->blue_length = 8;
        priv->blue_mask = 0x000000FF;
    }
    /* RGB565 format (typical for 16bpp) */
    else if (priv->bpp == 16)
    {
        priv->red_offset = 11;
        priv->red_length = 5;
        priv->red_mask = 0xF800;

        priv->green_offset = 5;
        priv->green_length = 6;
        priv->green_mask = 0x07E0;

        priv->blue_offset = 0;
        priv->blue_length = 5;
        priv->blue_mask = 0x001F;
    }

    /* Store private data in card driver field */
    card->driver = priv;

    #ifdef DEBUG
    ErrorF("tima_CardInit: Framebuffer at %p, %dx%d, %d bpp\n",
           priv->fb, priv->width, priv->height, priv->bpp);
    #endif
    return TRUE;
}

/*
 * tima_ScreenInit - Initialize screen information
 *
 * Sets up screen dimensions, depth, and framebuffer parameters.
 * Called after card init for each screen on the card.
 *
 * @param screen: Kdrive screen info structure
 * @return: TRUE on success
 */
Bool tima_ScreenInit(KdScreenInfo *screen)
{
    tima_Priv_t *priv = screen->card->driver;
    tima_ScrPriv_t *scrpriv;

    /* Allocate screen private data */
    scrpriv = (tima_ScrPriv_t *)malloc(sizeof(tima_ScrPriv_t));
    if (!scrpriv)
        return FALSE;

    memset(scrpriv, 0, sizeof(tima_ScrPriv_t));

    screen->driver = scrpriv;
    scrpriv->randr = RR_Rotate_0;
    scrpriv->shadow = FALSE;

    /* Set screen dimensions */
    screen->width = priv->width;
    screen->height = priv->height;
    screen->rate = 60;  /* Assume 60Hz refresh */

    /* Physical size in mm (approximate 96 DPI) */
    screen->width_mm = (screen->width * 254) / 960;
    screen->height_mm = (screen->height * 254) / 960;

    /* Framebuffer configuration */
    screen->fb.depth = priv->bpp;
    screen->fb.bitsPerPixel = priv->bpp;
    screen->fb.byteStride = priv->pitch;
    screen->fb.pixelStride = priv->width;
    screen->fb.frameBuffer = (CARD8 *)priv->fb;

    /* Visual configuration for TrueColor */
    screen->fb.visuals = (1 << TrueColor);
    screen->fb.redMask = priv->red_mask;
    screen->fb.greenMask = priv->green_mask;
    screen->fb.blueMask = priv->blue_mask;

    /* Memory info */
    screen->memory_base = (CARD8 *)priv->fb;
    screen->memory_size = priv->fb_size;
    screen->off_screen_base = priv->fb_size;  /* No offscreen memory */

    return TRUE;
}

/*
 * tima_InitScreen - Initialize X ScreenRec
 *
 * Connects Kdrive to the X server screen infrastructure.
 * Called during screen initialization.
 *
 * @param pScreen: X server screen pointer
 * @return: TRUE on success
 */
Bool tima_InitScreen(ScreenPtr pScreen)
{
    /*
     * The fb library handles most initialization.
     * Add any screen-specific setup here.
     */
    return TRUE;
}

/*
 * tima_FinishInitScreen - Complete screen initialization
 *
 * Called after all screen components are initialized.
 * Set up shadow framebuffer if needed.
 *
 * @param pScreen: X server screen pointer
 * @return: TRUE on success
 */
Bool tima_FinishInitScreen(ScreenPtr pScreen)
{
    /* Initialize shadow layer (for rotation support) */
    if (!shadowSetup(pScreen))
        return FALSE;

    return TRUE;
}

/*
 * tima_CreateResources - Create screen resources
 *
 * Called to create any additional screen resources.
 * Set up shadow if using rotation.
 *
 * @param pScreen: X server screen pointer
 * @return: TRUE on success
 */
Bool tima_CreateResources(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    tima_ScrPriv_t *scrpriv = screen->driver;

    if (scrpriv->shadow)
    {
        /* Set up shadow update function */
        return KdShadowSet(pScreen, scrpriv->randr,
                          shadowUpdatePacked, NULL);
    }

    return TRUE;
}

/*
 * tima_Preserve - Save hardware state
 *
 * Save any hardware state that needs to be restored later.
 * On bare-metal with no other graphics users, this may be empty.
 *
 * @param card: Kdrive card info
 */
void tima_Preserve(KdCardInfo *card)
{
    /* No state to preserve on bare-metal */
}

/*
 * tima_Enable - Enable rendering
 *
 * Called when X server takes control of display.
 * Enable framebuffer output.
 *
 * @param pScreen: X server screen pointer
 * @return: TRUE on success
 */
Bool tima_Enable(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    tima_Priv_t *priv = pScreenPriv->card->driver;

    /*
     * PLATFORM-SPECIFIC: Enable display output
     *
     * May involve:
     * - Enabling display controller
     * - Setting video timing registers
     * - Enabling pixel clock
     * - Turning on backlight
     */

    /* Example: Clear framebuffer to black */
    memset(priv->fb, 0, priv->fb_size);

    return TRUE;
}

/*
 * tima_DPMS - Display Power Management
 *
 * Handle DPMS (power saving) modes.
 *
 * @param pScreen: X server screen pointer
 * @param mode: DPMS mode (KD_DPMS_NORMAL, STANDBY, SUSPEND, POWERDOWN)
 * @return: TRUE if mode was set
 */
Bool tima_DPMS(ScreenPtr pScreen, int mode)
{
    /*
     * PLATFORM-SPECIFIC: Implement power management
     *
     * KD_DPMS_NORMAL    - Full power
     * KD_DPMS_STANDBY   - Reduced power, quick recovery
     * KD_DPMS_SUSPEND   - Lower power, slower recovery
     * KD_DPMS_POWERDOWN - Minimum power
     */

    switch (mode) {
    case KD_DPMS_NORMAL:
        /* Enable display */
        break;
    case KD_DPMS_STANDBY:
    case KD_DPMS_SUSPEND:
    case KD_DPMS_POWERDOWN:
        /* Disable display / reduce power */
        break;
    }

    return TRUE;
}

/*
 * tima_Disable - Disable rendering
 *
 * Called when X server releases display control.
 *
 * @param pScreen: X server screen pointer
 */
void tima_Disable(ScreenPtr pScreen)
{
    /*
     * On bare-metal, typically no action needed.
     * Could blank screen if desired.
     */
}

/*
 * tima_Restore - Restore hardware state
 *
 * Restore hardware state saved by Preserve.
 *
 * @param card: Kdrive card info
 */
void tima_Restore(KdCardInfo *card)
{
    /* No state to restore on bare-metal */
}

/*
 * tima_ScreenFini - Cleanup screen
 *
 * Free screen-specific resources.
 *
 * @param screen: Kdrive screen info
 */
void tima_ScreenFini(KdScreenInfo *screen)
{
    tima_ScrPriv_t *scrpriv = screen->driver;

    if (scrpriv) {
        free(scrpriv);
        screen->driver = NULL;
    }
}

/*
 * tima_CardFini - Cleanup card
 *
 * Free card resources and cleanup hardware.
 *
 * @param card: Kdrive card info
 */
void tima_CardFini(KdCardInfo *card)
{
    tima_Priv_t *priv = card->driver;

    if (priv) {
        /*
         * PLATFORM-SPECIFIC: Cleanup
         * If framebuffer was mmap'd, unmap it here
         */
        free(priv);
        card->driver = NULL;
    }
}

/*
 * tima_GetColors - Get colormap entries
 *
 * Read current colormap entries (for PseudoColor).
 * For TrueColor displays, this is typically not used.
 *
 * @param pScreen: X server screen pointer
 * @param n: Number of colors to get
 * @param pdefs: Array to fill with color values
 */
void tima_GetColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    /*
     * For TrueColor (direct color), colormap is identity.
     * Hardware doesn't have a palette to query.
     */
    while (n--) {
        pdefs->red = pdefs->pixel >> 8;
        pdefs->green = pdefs->pixel >> 8;
        pdefs->blue = pdefs->pixel >> 8;
        pdefs++;
    }
}

/*
 * tima_PutColors - Set colormap entries
 *
 * Update colormap entries (for PseudoColor).
 * For TrueColor displays, this is typically not used.
 *
 * @param pScreen: X server screen pointer
 * @param n: Number of colors to set
 * @param pdefs: Color values to set
 */
void tima_PutColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    /*
     * PLATFORM-SPECIFIC: Update hardware palette
     *
     * For indexed color modes, write to palette registers.
     * For TrueColor, typically no action needed.
     */
}

/*
 * KdCardFuncs structure - Wire up all functions
 */
const KdCardFuncs tima_Funcs = {
    .cardinit         = tima_CardInit,
    .scrinit          = tima_ScreenInit,
    .initScreen       = tima_InitScreen,
    .finishInitScreen = tima_FinishInitScreen,
    .createRes        = tima_CreateResources,
    .preserve         = tima_Preserve,
    .enable           = tima_Enable,
    .dpms             = tima_DPMS,
    .disable          = tima_Disable,
    .restore          = tima_Restore,
    .scrfini          = tima_ScreenFini,
    .cardfini         = tima_CardFini,
    .initCursor       = NULL,  /* Use software cursor */
    .enableCursor     = NULL,
    .disableCursor    = NULL,
    .finiCursor       = NULL,
    .recolorCursor    = NULL,
    .initAccel        = NULL,  /* No 2D acceleration */
    .enableAccel      = NULL,
    .disableAccel     = NULL,
    .finiAccel        = NULL,
    .getColors        = tima_GetColors,
    .putColors        = tima_PutColors,
};
