#ifndef TIMA_KDRIVE_H_
#define TIMA_KDRIVE_H_

#include "kdrive.h"
#include "hal_display.h"
#include "hal_input.h"

typedef struct
{
    Bool has_command;
    uint8_t command;
} tima_input_packet_t;

static inline Bool tima_ReadInputPacket(hal_input_dev_t dev, tima_input_packet_t *pending,
                                       uint8_t *command, uint8_t *data)
{
    if (!pending->has_command)
    {
        if (!hal_input_read_byte(dev, &pending->command)) return FALSE;
        pending->has_command = TRUE;
    }
    if (!hal_input_read_byte(dev, data)) return FALSE;
    *command = pending->command;
    pending->has_command = FALSE;
    return TRUE;
}

/* Framebuffer private data - stored in KdCardInfo->driver */
typedef struct _tima_Priv 
{
    /* Framebuffer info */
    void    *fb_base;           /* Mapped framebuffer base address */
    void    *fb;                /* Actual framebuffer start (after alignment) */
    uint32_t fb_size;           /* Framebuffer size in bytes */
    
    /* Display parameters */
    uint32_t width;             /* Width in pixels */
    uint32_t height;            /* Height in pixels */
    uint32_t bpp;               /* Bits per pixel (16, 24, 32) */
    uint32_t pitch;             /* Bytes per scanline */
    
    /* Color masks for TrueColor */
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint8_t  red_offset;
    uint8_t  green_offset;
    uint8_t  blue_offset;
    uint8_t  red_length;
    uint8_t  green_length;
    uint8_t  blue_length;
} tima_Priv_t;

/* Screen private data - stored in KdScreenInfo->driver */
typedef struct _tima_ScrPriv 
{
    Rotation randr;             /* Current rotation */
    Bool     shadow;            /* Using shadow framebuffer? */
} tima_ScrPriv_t;

/* Function declarations */
Bool tima_CardInit(KdCardInfo *card);
Bool tima_ScreenInit(KdScreenInfo *screen);
Bool tima_InitScreen(ScreenPtr pScreen);
Bool tima_FinishInitScreen(ScreenPtr pScreen);
Bool tima_CreateResources(ScreenPtr pScreen);
void tima_Preserve(KdCardInfo *card);
Bool tima_Enable(ScreenPtr pScreen);
Bool tima_DPMS(ScreenPtr pScreen, int mode);
void tima_Disable(ScreenPtr pScreen);
void tima_Restore(KdCardInfo *card);
void tima_ScreenFini(KdScreenInfo *screen);
void tima_CardFini(KdCardInfo *card);
void tima_GetColors(ScreenPtr pScreen, int n, xColorItem *pdefs);
void tima_PutColors(ScreenPtr pScreen, int n, xColorItem *pdefs);

/* Card function table */
extern const KdCardFuncs tima_Funcs;

/*
 * Keyboard driver (tima_keyboard.c)
 * 
 * Reads the HAL_INPUT_KEYBOARD byte stream. The OS adapter owns the
 * shared input lifecycle; keyboard and pointer decoders do not reopen it.
 * 
 * Protocol from HAL:
 *   0x01 + modifier_mask  - Modifier key change
 *   0x02 + key_data       - Key press
 *   0x03 + key_data       - Key release
 */
extern const KdKeyboardFuncs tima_KeyboardFuncs;

/* Call from main loop to dispatch queued keyboard events to X */
void tima_KeyboardPoll(void);

/*
 * Mouse driver (tima_mouse.c)
 * 
 * Reads the HAL_INPUT_MOUSE byte stream.
 */
extern const KdMouseFuncs tima_MouseFuncs;

/* Call from main loop to dispatch queued mouse events to X */
void tima_MousePoll(void);

#endif // TIMA_KDRIVE_H_