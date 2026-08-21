# Kdrive Implementation Reference for RISC-V Bare-Metal

**Date**: January 22, 2026  
**Purpose**: Complete implementation guide for TinyX/Kdrive drivers (framebuffer, keyboard, mouse)

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Interface Structures](#interface-structures)
4. [Framebuffer Implementation](#framebuffer-implementation)
5. [Keyboard Implementation](#keyboard-implementation)
6. [Mouse/Pointer Implementation](#mouse-pointer-implementation)
7. [OS Functions Implementation](#os-functions-implementation)
8. [Event Queue Integration](#event-queue-integration)
9. [Implementation Template](#implementation-template)
10. [Build Integration](#build-integration)
11. [Testing & Validation](#testing--validation)

---

## Overview

This document provides a complete reference for implementing Kdrive drivers to satisfy all input/output and framebuffer operations required by TinyX on bare-metal RISC-V.

### What You're Implementing

```
┌─────────────────────────────────────────────────────────────────┐
│                     TinyX X11 Server                            │
├─────────────────────────────────────────────────────────────────┤
│  DIX (Device Independent X)  │  MI (Machine Independent)       │
├──────────────────────────────┴──────────────────────────────────┤
│                        Kdrive Core                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ KdCardFuncs  │  │KdKeyboardFuncs│  │KdPointerFuncs│          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
├─────────┼─────────────────┼─────────────────┼───────────────────┤
│         │    YOUR IMPLEMENTATION LAYER      │                   │
│         ▼                 ▼                 ▼                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │riscv_fb.c    │  │riscv_kbd.c   │  │riscv_mouse.c │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
├─────────┼─────────────────┼─────────────────┼───────────────────┤
│         ▼                 ▼                 ▼                   │
│                    HAL / Hardware                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Framebuffer  │  │   PS/2 or    │  │   PS/2 or    │          │
│  │  Hardware    │  │   USB HID    │  │   USB HID    │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
└─────────────────────────────────────────────────────────────────┘
```

### Files to Create

```
tinyx/kdrive/riscv/
├── riscv.h           # Header with structures and declarations
├── riscv_fb.c        # Framebuffer/card functions (KdCardFuncs)
├── riscv_kbd.c       # Keyboard functions (KdKeyboardFuncs)
├── riscv_mouse.c     # Mouse functions (KdPointerFuncs/KdMouseFuncs)
├── riscv_os.c        # OS-level functions (KdOsFuncs)
├── riscv_init.c      # Initialization and driver registration
└── Makefile.am       # Build configuration
```

---

## Architecture

### Kdrive Data Flow

```
Hardware Event → HAL Poll → Kdrive Input → X Server Event Queue → Client

┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ Hardware │───▶│ HAL      │───▶│ Kdrive   │───▶│ X Server │
│ (IRQ/    │    │ Poll/    │    │ Enqueue  │    │ Event    │
│  Poll)   │    │ Read     │    │ Function │    │ Dispatch │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
```

### Graphics Data Flow

```
X Client Request → DIX → MI → Kdrive → Framebuffer Memory

┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ X Client │───▶│ DIX/MI   │───▶│ Kdrive   │───▶│ Frame-   │
│ DrawRect │    │ Request  │    │ fb lib   │    │ buffer   │
│          │    │ Process  │    │ render   │    │ Memory   │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
```

---

## Interface Structures

### KdCardFuncs - Graphics Card Interface

**Location**: `tinyx/kdrive/src/kdrive.h`

```c
typedef struct _KdCardFuncs {
    /* Card lifecycle */
    Bool (*cardinit)(KdCardInfo *);      /* Detect and initialize card */
    Bool (*scrinit)(KdScreenInfo *);     /* Initialize screen info */
    Bool (*initScreen)(ScreenPtr);       /* Initialize ScreenRec */
    Bool (*finishInitScreen)(ScreenPtr); /* Complete screen init */
    Bool (*createRes)(ScreenPtr);        /* Create screen resources */
    
    /* State management */
    void (*preserve)(KdCardInfo *);      /* Save hardware state */
    Bool (*enable)(ScreenPtr);           /* Enable rendering */
    Bool (*dpms)(ScreenPtr, int);        /* Power management */
    void (*disable)(ScreenPtr);          /* Disable rendering */
    void (*restore)(KdCardInfo *);       /* Restore hardware state */
    
    /* Cleanup */
    void (*scrfini)(KdScreenInfo *);     /* Cleanup screen */
    void (*cardfini)(KdCardInfo *);      /* Cleanup card */
    
    /* Cursor (optional - can use software cursor) */
    Bool (*initCursor)(ScreenPtr);       /* Init hardware cursor */
    void (*enableCursor)(ScreenPtr);     /* Enable cursor */
    void (*disableCursor)(ScreenPtr);    /* Disable cursor */
    void (*finiCursor)(ScreenPtr);       /* Cleanup cursor */
    void (*recolorCursor)(ScreenPtr, int, xColorItem *);
    
    /* Acceleration (optional) */
    Bool (*initAccel)(ScreenPtr);        /* Init 2D acceleration */
    void (*enableAccel)(ScreenPtr);      /* Enable acceleration */
    void (*disableAccel)(ScreenPtr);     /* Disable acceleration */
    void (*finiAccel)(ScreenPtr);        /* Cleanup acceleration */
    
    /* Colormap */
    void (*getColors)(ScreenPtr, int, xColorItem *);
    void (*putColors)(ScreenPtr, int, xColorItem *);
} KdCardFuncs;
```

**Required Functions** (minimum for bare-metal):
- `cardinit` - Initialize framebuffer hardware
- `scrinit` - Setup screen parameters
- `initScreen` - Connect to X screen infrastructure
- `enable` - Enable output
- `disable` - Disable output
- `cardfini` - Cleanup

### KdKeyboardFuncs - Keyboard Interface

**Location**: `tinyx/kdrive/src/kdrive.h`

```c
typedef struct _KdKeyboardFuncs {
    void (*Load)(void);         /* Load keymap from hardware/config */
    int  (*Init)(void);         /* Initialize keyboard hardware */
    void (*Leds)(int);          /* Set keyboard LED state */
    void (*Bell)(int, int, int);/* Ring system bell (volume, pitch, duration) */
    void (*Fini)(void);         /* Cleanup keyboard */
    int  LockLed;               /* LED number for Caps Lock indicator */
} KdKeyboardFuncs;
```

**All functions required.**

### KdMouseFuncs - Mouse Interface

**Location**: `tinyx/kdrive/src/kdrive.h`

```c
typedef struct _KdMouseFuncs {
    Bool (*Init)(void);         /* Initialize mouse hardware */
    void (*Fini)(void);         /* Cleanup mouse */
} KdMouseFuncs;
```

**All functions required.**

### KdOsFuncs - OS-Level Interface

**Location**: `tinyx/kdrive/src/kdrive.h`

```c
typedef struct _KdOsFuncs {
    int  (*Init)(void);           /* Initialize OS layer */
    void (*Enable)(void);         /* Enable OS services */
    Bool (*SpecialKey)(KeySym);   /* Handle special keys (VT switch, etc.) */
    void (*Disable)(void);        /* Disable OS services */
    void (*Fini)(void);           /* Cleanup OS layer */
    void (*pollEvents)(void);     /* Poll for input events */
} KdOsFuncs;
```

**All functions required for bare-metal.**

---

## Framebuffer Implementation

### Data Structures

```c
/* riscv.h */

#ifndef _RISCV_H_
#define _RISCV_H_

#include "kdrive.h"

/* Framebuffer private data - stored in KdCardInfo->driver */
typedef struct _RiscvPriv {
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
} RiscvPriv;

/* Screen private data - stored in KdScreenInfo->driver */
typedef struct _RiscvScrPriv {
    Rotation randr;             /* Current rotation */
    Bool     shadow;            /* Using shadow framebuffer? */
} RiscvScrPriv;

/* Function declarations */
Bool RiscvCardInit(KdCardInfo *card);
Bool RiscvScreenInit(KdScreenInfo *screen);
Bool RiscvInitScreen(ScreenPtr pScreen);
Bool RiscvFinishInitScreen(ScreenPtr pScreen);
Bool RiscvCreateResources(ScreenPtr pScreen);
void RiscvPreserve(KdCardInfo *card);
Bool RiscvEnable(ScreenPtr pScreen);
Bool RiscvDPMS(ScreenPtr pScreen, int mode);
void RiscvDisable(ScreenPtr pScreen);
void RiscvRestore(KdCardInfo *card);
void RiscvScreenFini(KdScreenInfo *screen);
void RiscvCardFini(KdCardInfo *card);
void RiscvGetColors(ScreenPtr pScreen, int n, xColorItem *pdefs);
void RiscvPutColors(ScreenPtr pScreen, int n, xColorItem *pdefs);

#endif /* _RISCV_H_ */
```

### Card Initialization

```c
/* riscv_fb.c */

#include "riscv.h"
#include <string.h>

/*
 * PLATFORM-SPECIFIC: Define your framebuffer base address
 * This will vary based on your SoC/FPGA memory map
 */
#ifndef RISCV_FB_BASE
#define RISCV_FB_BASE       0x80000000  /* Example: 2GB mark */
#endif

#ifndef RISCV_FB_WIDTH
#define RISCV_FB_WIDTH      1024
#endif

#ifndef RISCV_FB_HEIGHT
#define RISCV_FB_HEIGHT     768
#endif

#ifndef RISCV_FB_BPP
#define RISCV_FB_BPP        32
#endif

/*
 * RiscvCardInit - Initialize the graphics card/framebuffer
 * 
 * Called once per card at server startup.
 * Detects hardware and sets up basic card info.
 *
 * @param card: Kdrive card info structure
 * @return: TRUE on success, FALSE on failure
 */
Bool RiscvCardInit(KdCardInfo *card)
{
    RiscvPriv *priv;
    
    /* Allocate private data */
    priv = (RiscvPriv *)malloc(sizeof(RiscvPriv));
    if (!priv) {
        ErrorF("RiscvCardInit: Failed to allocate private data\n");
        return FALSE;
    }
    memset(priv, 0, sizeof(RiscvPriv));
    
    /*
     * PLATFORM-SPECIFIC: Initialize framebuffer
     * 
     * On bare-metal, framebuffer is typically at a fixed address
     * defined by hardware or bootloader configuration.
     */
    priv->fb_base = (void *)RISCV_FB_BASE;
    priv->fb = priv->fb_base;  /* May need offset for alignment */
    
    /* Set display parameters */
    priv->width = RISCV_FB_WIDTH;
    priv->height = RISCV_FB_HEIGHT;
    priv->bpp = RISCV_FB_BPP;
    priv->pitch = priv->width * (priv->bpp / 8);
    priv->fb_size = priv->pitch * priv->height;
    
    /* RGB888 or RGBA8888 format (typical for 32bpp) */
    if (priv->bpp == 32) {
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
    else if (priv->bpp == 16) {
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
    
    ErrorF("RiscvCardInit: Framebuffer at %p, %dx%d, %d bpp\n",
           priv->fb, priv->width, priv->height, priv->bpp);
    
    return TRUE;
}

/*
 * RiscvScreenInit - Initialize screen information
 *
 * Sets up screen dimensions, depth, and framebuffer parameters.
 * Called after card init for each screen on the card.
 *
 * @param screen: Kdrive screen info structure
 * @return: TRUE on success
 */
Bool RiscvScreenInit(KdScreenInfo *screen)
{
    RiscvPriv *priv = screen->card->driver;
    RiscvScrPriv *scrpriv;
    
    /* Allocate screen private data */
    scrpriv = (RiscvScrPriv *)calloc(1, sizeof(RiscvScrPriv));
    if (!scrpriv)
        return FALSE;
    
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
 * RiscvInitScreen - Initialize X ScreenRec
 *
 * Connects Kdrive to the X server screen infrastructure.
 * Called during screen initialization.
 *
 * @param pScreen: X server screen pointer
 * @return: TRUE on success
 */
Bool RiscvInitScreen(ScreenPtr pScreen)
{
    /* 
     * The fb library handles most initialization.
     * Add any screen-specific setup here.
     */
    return TRUE;
}

/*
 * RiscvFinishInitScreen - Complete screen initialization
 *
 * Called after all screen components are initialized.
 * Set up shadow framebuffer if needed.
 *
 * @param pScreen: X server screen pointer
 * @return: TRUE on success
 */
Bool RiscvFinishInitScreen(ScreenPtr pScreen)
{
    /* Initialize shadow layer (for rotation support) */
    if (!shadowSetup(pScreen))
        return FALSE;
    
    /* Initialize RandR extension */
    /* Note: May want to skip for simple bare-metal */
    
    return TRUE;
}

/*
 * RiscvCreateResources - Create screen resources
 *
 * Called to create any additional screen resources.
 * Set up shadow if using rotation.
 *
 * @param pScreen: X server screen pointer  
 * @return: TRUE on success
 */
Bool RiscvCreateResources(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    RiscvScrPriv *scrpriv = screen->driver;
    
    if (scrpriv->shadow) {
        /* Set up shadow update function */
        return KdShadowSet(pScreen, scrpriv->randr,
                          shadowUpdatePacked, NULL);
    }
    
    return TRUE;
}

/*
 * RiscvPreserve - Save hardware state
 *
 * Save any hardware state that needs to be restored later.
 * On bare-metal with no other graphics users, this may be empty.
 *
 * @param card: Kdrive card info
 */
void RiscvPreserve(KdCardInfo *card)
{
    /* No state to preserve on bare-metal */
}

/*
 * RiscvEnable - Enable rendering
 *
 * Called when X server takes control of display.
 * Enable framebuffer output.
 *
 * @param pScreen: X server screen pointer
 * @return: TRUE on success
 */
Bool RiscvEnable(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    RiscvPriv *priv = pScreenPriv->card->driver;
    
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
 * RiscvDPMS - Display Power Management
 *
 * Handle DPMS (power saving) modes.
 *
 * @param pScreen: X server screen pointer
 * @param mode: DPMS mode (KD_DPMS_NORMAL, STANDBY, SUSPEND, POWERDOWN)
 * @return: TRUE if mode was set
 */
Bool RiscvDPMS(ScreenPtr pScreen, int mode)
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
 * RiscvDisable - Disable rendering
 *
 * Called when X server releases display control.
 *
 * @param pScreen: X server screen pointer
 */
void RiscvDisable(ScreenPtr pScreen)
{
    /* 
     * On bare-metal, typically no action needed.
     * Could blank screen if desired.
     */
}

/*
 * RiscvRestore - Restore hardware state
 *
 * Restore hardware state saved by Preserve.
 *
 * @param card: Kdrive card info
 */
void RiscvRestore(KdCardInfo *card)
{
    /* No state to restore on bare-metal */
}

/*
 * RiscvScreenFini - Cleanup screen
 *
 * Free screen-specific resources.
 *
 * @param screen: Kdrive screen info
 */
void RiscvScreenFini(KdScreenInfo *screen)
{
    RiscvScrPriv *scrpriv = screen->driver;
    
    if (scrpriv) {
        free(scrpriv);
        screen->driver = NULL;
    }
}

/*
 * RiscvCardFini - Cleanup card
 *
 * Free card resources and cleanup hardware.
 *
 * @param card: Kdrive card info
 */
void RiscvCardFini(KdCardInfo *card)
{
    RiscvPriv *priv = card->driver;
    
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
 * RiscvGetColors - Get colormap entries
 *
 * Read current colormap entries (for PseudoColor).
 * For TrueColor displays, this is typically not used.
 *
 * @param pScreen: X server screen pointer
 * @param n: Number of colors to get
 * @param pdefs: Array to fill with color values
 */
void RiscvGetColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
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
 * RiscvPutColors - Set colormap entries
 *
 * Update colormap entries (for PseudoColor).
 * For TrueColor displays, this is typically not used.
 *
 * @param pScreen: X server screen pointer
 * @param n: Number of colors to set
 * @param pdefs: Color values to set
 */
void RiscvPutColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
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
const KdCardFuncs RiscvFuncs = {
    .cardinit         = RiscvCardInit,
    .scrinit          = RiscvScreenInit,
    .initScreen       = RiscvInitScreen,
    .finishInitScreen = RiscvFinishInitScreen,
    .createRes        = RiscvCreateResources,
    .preserve         = RiscvPreserve,
    .enable           = RiscvEnable,
    .dpms             = RiscvDPMS,
    .disable          = RiscvDisable,
    .restore          = RiscvRestore,
    .scrfini          = RiscvScreenFini,
    .cardfini         = RiscvCardFini,
    .initCursor       = NULL,  /* Use software cursor */
    .enableCursor     = NULL,
    .disableCursor    = NULL,
    .finiCursor       = NULL,
    .recolorCursor    = NULL,
    .initAccel        = NULL,  /* No 2D acceleration */
    .enableAccel      = NULL,
    .disableAccel     = NULL,
    .finiAccel        = NULL,
    .getColors        = RiscvGetColors,
    .putColors        = RiscvPutColors,
};
```

---

## Keyboard Implementation

### Keyboard Driver

```c
/* riscv_kbd.c */

#include "riscv.h"
#include "kkeymap.h"
#include <X11/keysym.h>

/*
 * PLATFORM-SPECIFIC: Define keyboard hardware interface
 */
#ifndef RISCV_KBD_BASE
#define RISCV_KBD_BASE      0x10000000  /* Example keyboard controller address */
#endif

#define RISCV_KBD_DATA      (RISCV_KBD_BASE + 0x00)
#define RISCV_KBD_STATUS    (RISCV_KBD_BASE + 0x04)
#define RISCV_KBD_CMD       (RISCV_KBD_BASE + 0x08)

/* Status register bits */
#define KBD_STATUS_DATA_READY   0x01
#define KBD_STATUS_INPUT_FULL   0x02

/* Commands */
#define KBD_CMD_SET_LEDS        0xED
#define KBD_CMD_ENABLE          0xF4
#define KBD_CMD_RESET           0xFF

/* LED bits */
#define KBD_LED_SCROLL          0x01
#define KBD_LED_NUM             0x02
#define KBD_LED_CAPS            0x04

/*
 * Hardware access functions
 * PLATFORM-SPECIFIC: Implement for your hardware
 */
static inline uint8_t kbd_read_status(void)
{
    return *(volatile uint8_t *)RISCV_KBD_STATUS;
}

static inline uint8_t kbd_read_data(void)
{
    return *(volatile uint8_t *)RISCV_KBD_DATA;
}

static inline void kbd_write_cmd(uint8_t cmd)
{
    *(volatile uint8_t *)RISCV_KBD_CMD = cmd;
}

static inline void kbd_write_data(uint8_t data)
{
    *(volatile uint8_t *)RISCV_KBD_DATA = data;
}

/*
 * PS/2 scancode to X keycode mapping
 * 
 * X keycodes = PS/2 scancode + 8 (traditionally)
 * This allows PS/2 scancodes 1-127 to map to X keycodes 9-135
 */
#define PS2_TO_KEYCODE(scancode) ((scancode) + 8)

/*
 * Default keymap - standard US QWERTY layout
 * 
 * This maps X keycodes to keysyms.
 * Loaded into Kdrive's keymap array.
 * 
 * Format: Each keycode has KD_MAX_WIDTH keysyms
 *         [0] = unshifted, [1] = shifted, [2] = mode switch, etc.
 */
static void RiscvLoadKeymap(void)
{
    extern KeySym kdKeymap[];
    extern int kdMinScanCode;
    extern int kdMaxScanCode;
    
    /* Set scancode range */
    kdMinScanCode = 1;    /* Minimum PS/2 scancode */
    kdMaxScanCode = 127;  /* Maximum PS/2 scancode */
    
    /* Clear keymap */
    memset(kdKeymap, 0, sizeof(KeySym) * KD_MAX_LENGTH * KD_MAX_WIDTH);
    
    /* 
     * Load standard US keymap
     * Row 0 = normal, Row 1 = shifted
     * 
     * Example entries (actual implementation needs full map):
     */
    
    /* Escape */
    kdKeymap[(1 - 1) * KD_MAX_WIDTH + 0] = XK_Escape;
    
    /* 1-0 row */
    kdKeymap[(2 - 1) * KD_MAX_WIDTH + 0] = XK_1;
    kdKeymap[(2 - 1) * KD_MAX_WIDTH + 1] = XK_exclam;
    kdKeymap[(3 - 1) * KD_MAX_WIDTH + 0] = XK_2;
    kdKeymap[(3 - 1) * KD_MAX_WIDTH + 1] = XK_at;
    kdKeymap[(4 - 1) * KD_MAX_WIDTH + 0] = XK_3;
    kdKeymap[(4 - 1) * KD_MAX_WIDTH + 1] = XK_numbersign;
    kdKeymap[(5 - 1) * KD_MAX_WIDTH + 0] = XK_4;
    kdKeymap[(5 - 1) * KD_MAX_WIDTH + 1] = XK_dollar;
    kdKeymap[(6 - 1) * KD_MAX_WIDTH + 0] = XK_5;
    kdKeymap[(6 - 1) * KD_MAX_WIDTH + 1] = XK_percent;
    kdKeymap[(7 - 1) * KD_MAX_WIDTH + 0] = XK_6;
    kdKeymap[(7 - 1) * KD_MAX_WIDTH + 1] = XK_asciicircum;
    kdKeymap[(8 - 1) * KD_MAX_WIDTH + 0] = XK_7;
    kdKeymap[(8 - 1) * KD_MAX_WIDTH + 1] = XK_ampersand;
    kdKeymap[(9 - 1) * KD_MAX_WIDTH + 0] = XK_8;
    kdKeymap[(9 - 1) * KD_MAX_WIDTH + 1] = XK_asterisk;
    kdKeymap[(10 - 1) * KD_MAX_WIDTH + 0] = XK_9;
    kdKeymap[(10 - 1) * KD_MAX_WIDTH + 1] = XK_parenleft;
    kdKeymap[(11 - 1) * KD_MAX_WIDTH + 0] = XK_0;
    kdKeymap[(11 - 1) * KD_MAX_WIDTH + 1] = XK_parenright;
    kdKeymap[(12 - 1) * KD_MAX_WIDTH + 0] = XK_minus;
    kdKeymap[(12 - 1) * KD_MAX_WIDTH + 1] = XK_underscore;
    kdKeymap[(13 - 1) * KD_MAX_WIDTH + 0] = XK_equal;
    kdKeymap[(13 - 1) * KD_MAX_WIDTH + 1] = XK_plus;
    kdKeymap[(14 - 1) * KD_MAX_WIDTH + 0] = XK_BackSpace;
    
    /* Tab and QWERTY row */
    kdKeymap[(15 - 1) * KD_MAX_WIDTH + 0] = XK_Tab;
    kdKeymap[(16 - 1) * KD_MAX_WIDTH + 0] = XK_q;
    kdKeymap[(16 - 1) * KD_MAX_WIDTH + 1] = XK_Q;
    kdKeymap[(17 - 1) * KD_MAX_WIDTH + 0] = XK_w;
    kdKeymap[(17 - 1) * KD_MAX_WIDTH + 1] = XK_W;
    kdKeymap[(18 - 1) * KD_MAX_WIDTH + 0] = XK_e;
    kdKeymap[(18 - 1) * KD_MAX_WIDTH + 1] = XK_E;
    kdKeymap[(19 - 1) * KD_MAX_WIDTH + 0] = XK_r;
    kdKeymap[(19 - 1) * KD_MAX_WIDTH + 1] = XK_R;
    kdKeymap[(20 - 1) * KD_MAX_WIDTH + 0] = XK_t;
    kdKeymap[(20 - 1) * KD_MAX_WIDTH + 1] = XK_T;
    kdKeymap[(21 - 1) * KD_MAX_WIDTH + 0] = XK_y;
    kdKeymap[(21 - 1) * KD_MAX_WIDTH + 1] = XK_Y;
    kdKeymap[(22 - 1) * KD_MAX_WIDTH + 0] = XK_u;
    kdKeymap[(22 - 1) * KD_MAX_WIDTH + 1] = XK_U;
    kdKeymap[(23 - 1) * KD_MAX_WIDTH + 0] = XK_i;
    kdKeymap[(23 - 1) * KD_MAX_WIDTH + 1] = XK_I;
    kdKeymap[(24 - 1) * KD_MAX_WIDTH + 0] = XK_o;
    kdKeymap[(24 - 1) * KD_MAX_WIDTH + 1] = XK_O;
    kdKeymap[(25 - 1) * KD_MAX_WIDTH + 0] = XK_p;
    kdKeymap[(25 - 1) * KD_MAX_WIDTH + 1] = XK_P;
    
    /* Enter */
    kdKeymap[(28 - 1) * KD_MAX_WIDTH + 0] = XK_Return;
    
    /* Control */
    kdKeymap[(29 - 1) * KD_MAX_WIDTH + 0] = XK_Control_L;
    
    /* ASDF row */
    kdKeymap[(30 - 1) * KD_MAX_WIDTH + 0] = XK_a;
    kdKeymap[(30 - 1) * KD_MAX_WIDTH + 1] = XK_A;
    kdKeymap[(31 - 1) * KD_MAX_WIDTH + 0] = XK_s;
    kdKeymap[(31 - 1) * KD_MAX_WIDTH + 1] = XK_S;
    kdKeymap[(32 - 1) * KD_MAX_WIDTH + 0] = XK_d;
    kdKeymap[(32 - 1) * KD_MAX_WIDTH + 1] = XK_D;
    kdKeymap[(33 - 1) * KD_MAX_WIDTH + 0] = XK_f;
    kdKeymap[(33 - 1) * KD_MAX_WIDTH + 1] = XK_F;
    kdKeymap[(34 - 1) * KD_MAX_WIDTH + 0] = XK_g;
    kdKeymap[(34 - 1) * KD_MAX_WIDTH + 1] = XK_G;
    kdKeymap[(35 - 1) * KD_MAX_WIDTH + 0] = XK_h;
    kdKeymap[(35 - 1) * KD_MAX_WIDTH + 1] = XK_H;
    kdKeymap[(36 - 1) * KD_MAX_WIDTH + 0] = XK_j;
    kdKeymap[(36 - 1) * KD_MAX_WIDTH + 1] = XK_J;
    kdKeymap[(37 - 1) * KD_MAX_WIDTH + 0] = XK_k;
    kdKeymap[(37 - 1) * KD_MAX_WIDTH + 1] = XK_K;
    kdKeymap[(38 - 1) * KD_MAX_WIDTH + 0] = XK_l;
    kdKeymap[(38 - 1) * KD_MAX_WIDTH + 1] = XK_L;
    
    /* Shift */
    kdKeymap[(42 - 1) * KD_MAX_WIDTH + 0] = XK_Shift_L;
    
    /* ZXCV row */
    kdKeymap[(44 - 1) * KD_MAX_WIDTH + 0] = XK_z;
    kdKeymap[(44 - 1) * KD_MAX_WIDTH + 1] = XK_Z;
    kdKeymap[(45 - 1) * KD_MAX_WIDTH + 0] = XK_x;
    kdKeymap[(45 - 1) * KD_MAX_WIDTH + 1] = XK_X;
    kdKeymap[(46 - 1) * KD_MAX_WIDTH + 0] = XK_c;
    kdKeymap[(46 - 1) * KD_MAX_WIDTH + 1] = XK_C;
    kdKeymap[(47 - 1) * KD_MAX_WIDTH + 0] = XK_v;
    kdKeymap[(47 - 1) * KD_MAX_WIDTH + 1] = XK_V;
    kdKeymap[(48 - 1) * KD_MAX_WIDTH + 0] = XK_b;
    kdKeymap[(48 - 1) * KD_MAX_WIDTH + 1] = XK_B;
    kdKeymap[(49 - 1) * KD_MAX_WIDTH + 0] = XK_n;
    kdKeymap[(49 - 1) * KD_MAX_WIDTH + 1] = XK_N;
    kdKeymap[(50 - 1) * KD_MAX_WIDTH + 0] = XK_m;
    kdKeymap[(50 - 1) * KD_MAX_WIDTH + 1] = XK_M;
    
    /* Right shift */
    kdKeymap[(54 - 1) * KD_MAX_WIDTH + 0] = XK_Shift_R;
    
    /* Alt */
    kdKeymap[(56 - 1) * KD_MAX_WIDTH + 0] = XK_Alt_L;
    
    /* Space */
    kdKeymap[(57 - 1) * KD_MAX_WIDTH + 0] = XK_space;
    
    /* Caps Lock */
    kdKeymap[(58 - 1) * KD_MAX_WIDTH + 0] = XK_Caps_Lock;
    
    /* Function keys F1-F10 */
    kdKeymap[(59 - 1) * KD_MAX_WIDTH + 0] = XK_F1;
    kdKeymap[(60 - 1) * KD_MAX_WIDTH + 0] = XK_F2;
    kdKeymap[(61 - 1) * KD_MAX_WIDTH + 0] = XK_F3;
    kdKeymap[(62 - 1) * KD_MAX_WIDTH + 0] = XK_F4;
    kdKeymap[(63 - 1) * KD_MAX_WIDTH + 0] = XK_F5;
    kdKeymap[(64 - 1) * KD_MAX_WIDTH + 0] = XK_F6;
    kdKeymap[(65 - 1) * KD_MAX_WIDTH + 0] = XK_F7;
    kdKeymap[(66 - 1) * KD_MAX_WIDTH + 0] = XK_F8;
    kdKeymap[(67 - 1) * KD_MAX_WIDTH + 0] = XK_F9;
    kdKeymap[(68 - 1) * KD_MAX_WIDTH + 0] = XK_F10;
    
    /* Arrow keys (extended scancodes - simplified) */
    kdKeymap[(72 - 1) * KD_MAX_WIDTH + 0] = XK_Up;
    kdKeymap[(75 - 1) * KD_MAX_WIDTH + 0] = XK_Left;
    kdKeymap[(77 - 1) * KD_MAX_WIDTH + 0] = XK_Right;
    kdKeymap[(80 - 1) * KD_MAX_WIDTH + 0] = XK_Down;
    
    /* Note: Complete implementation should include all keys */
}

/*
 * RiscvKeyboardInit - Initialize keyboard hardware
 *
 * Called when keyboard device is enabled.
 *
 * @return: 1 on success, 0 on failure
 */
static int RiscvKeyboardInit(void)
{
    /*
     * PLATFORM-SPECIFIC: Initialize keyboard controller
     * 
     * Example PS/2 initialization sequence:
     * 1. Reset controller
     * 2. Perform self-test
     * 3. Enable keyboard interface
     * 4. Set keyboard to scan code set 2
     */
    
    /* Send reset command */
    kbd_write_cmd(KBD_CMD_RESET);
    
    /* Wait for acknowledgment (simplified - should add timeout) */
    while (!(kbd_read_status() & KBD_STATUS_DATA_READY))
        ;
    kbd_read_data();  /* Read ACK */
    
    /* Enable keyboard */
    kbd_write_cmd(KBD_CMD_ENABLE);
    
    ErrorF("RiscvKeyboardInit: Keyboard initialized\n");
    
    return 1;  /* Success */
}

/*
 * RiscvKeyboardLeds - Set keyboard LED state
 *
 * @param leds: LED state bitmap (KBD_LED_SCROLL|NUM|CAPS)
 */
static void RiscvKeyboardLeds(int leds)
{
    uint8_t led_byte = 0;
    
    if (leds & (1 << 0))  /* Caps Lock */
        led_byte |= KBD_LED_CAPS;
    if (leds & (1 << 1))  /* Num Lock */
        led_byte |= KBD_LED_NUM;
    if (leds & (1 << 2))  /* Scroll Lock */
        led_byte |= KBD_LED_SCROLL;
    
    /*
     * PLATFORM-SPECIFIC: Send LED command to keyboard
     */
    kbd_write_cmd(KBD_CMD_SET_LEDS);
    /* Wait for ACK */
    while (!(kbd_read_status() & KBD_STATUS_DATA_READY))
        ;
    kbd_read_data();
    
    /* Send LED byte */
    kbd_write_data(led_byte);
}

/*
 * RiscvKeyboardBell - Ring system bell
 *
 * @param volume: Bell volume (0-100)
 * @param pitch: Bell frequency in Hz
 * @param duration: Duration in ms
 */
static void RiscvKeyboardBell(int volume, int pitch, int duration)
{
    /*
     * PLATFORM-SPECIFIC: Generate bell tone
     * 
     * Options:
     * - Use PC speaker (if available)
     * - Use audio codec
     * - Use GPIO-connected buzzer
     * - Implement software beep via audio output
     * 
     * For bare-metal, may be stubbed initially.
     */
    
    /* Simple implementation: just log it */
    if (volume > 0) {
        ErrorF("Bell: volume=%d pitch=%d duration=%d\n", 
               volume, pitch, duration);
    }
}

/*
 * RiscvKeyboardFini - Cleanup keyboard
 */
static void RiscvKeyboardFini(void)
{
    /* Nothing to cleanup on bare-metal */
}

/*
 * KdKeyboardFuncs structure
 */
const KdKeyboardFuncs RiscvKeyboardFuncs = {
    .Load    = RiscvLoadKeymap,
    .Init    = RiscvKeyboardInit,
    .Leds    = RiscvKeyboardLeds,
    .Bell    = RiscvKeyboardBell,
    .Fini    = RiscvKeyboardFini,
    .LockLed = 1,  /* LED number for Caps Lock */
};

/*
 * RiscvKeyboardRead - Read and process keyboard events
 *
 * Called from event polling loop.
 * Reads scancodes and enqueues X keyboard events.
 */
void RiscvKeyboardRead(void)
{
    uint8_t scancode;
    unsigned char is_up;
    
    /* Check if data is available */
    while (kbd_read_status() & KBD_STATUS_DATA_READY) {
        scancode = kbd_read_data();
        
        /* Check for key release (bit 7 set) */
        if (scancode & 0x80) {
            is_up = 1;
            scancode &= 0x7F;
        } else {
            is_up = 0;
        }
        
        /* Skip invalid scancodes */
        if (scancode == 0 || scancode > 127)
            continue;
        
        /*
         * Enqueue the keyboard event
         * This function is provided by Kdrive (kinput.c)
         */
        KdEnqueueKeyboardEvent(scancode, is_up);
    }
}
```

---

## Mouse/Pointer Implementation

### Mouse Driver

```c
/* riscv_mouse.c */

#include "riscv.h"

/*
 * PLATFORM-SPECIFIC: Define mouse hardware interface
 */
#ifndef RISCV_MOUSE_BASE
#define RISCV_MOUSE_BASE    0x10001000  /* Example mouse controller address */
#endif

#define RISCV_MOUSE_DATA    (RISCV_MOUSE_BASE + 0x00)
#define RISCV_MOUSE_STATUS  (RISCV_MOUSE_BASE + 0x04)
#define RISCV_MOUSE_CMD     (RISCV_MOUSE_BASE + 0x08)

/* Status bits */
#define MOUSE_STATUS_DATA_READY  0x01

/* PS/2 mouse packet bytes */
#define MOUSE_BTN_LEFT      0x01
#define MOUSE_BTN_RIGHT     0x02
#define MOUSE_BTN_MIDDLE    0x04
#define MOUSE_SIGN_X        0x10
#define MOUSE_SIGN_Y        0x20
#define MOUSE_OVERFLOW_X    0x40
#define MOUSE_OVERFLOW_Y    0x80

/* Mouse state */
static struct {
    Bool initialized;
    int  packet_byte;       /* Current byte in packet (0-2 or 0-3 for wheel) */
    uint8_t packet[4];      /* Accumulated packet bytes */
    int  x, y;              /* Current position */
    int  buttons;           /* Current button state */
} mouse_state;

/* Screen dimensions (set during init) */
static int screen_width = 1024;
static int screen_height = 768;

/*
 * Hardware access functions
 */
static inline uint8_t mouse_read_status(void)
{
    return *(volatile uint8_t *)RISCV_MOUSE_STATUS;
}

static inline uint8_t mouse_read_data(void)
{
    return *(volatile uint8_t *)RISCV_MOUSE_DATA;
}

static inline void mouse_write_cmd(uint8_t cmd)
{
    *(volatile uint8_t *)RISCV_MOUSE_CMD = cmd;
}

/*
 * RiscvMouseInit - Initialize mouse hardware
 *
 * @return: TRUE on success
 */
static Bool RiscvMouseInit(void)
{
    /*
     * PLATFORM-SPECIFIC: Initialize PS/2 mouse
     * 
     * Typical sequence:
     * 1. Reset mouse
     * 2. Enable mouse data reporting
     * 3. Set sample rate (optional)
     * 4. Enable wheel (optional)
     */
    
    mouse_state.initialized = TRUE;
    mouse_state.packet_byte = 0;
    mouse_state.x = screen_width / 2;
    mouse_state.y = screen_height / 2;
    mouse_state.buttons = 0;
    
    ErrorF("RiscvMouseInit: Mouse initialized\n");
    
    return TRUE;
}

/*
 * RiscvMouseFini - Cleanup mouse
 */
static void RiscvMouseFini(void)
{
    mouse_state.initialized = FALSE;
}

/*
 * KdMouseFuncs structure
 */
const KdMouseFuncs RiscvMouseFuncs = {
    .Init = RiscvMouseInit,
    .Fini = RiscvMouseFini,
};

/*
 * RiscvMouseRead - Read and process mouse events
 *
 * Called from event polling loop.
 * Reads mouse packets and enqueues X pointer events.
 */
void RiscvMouseRead(void)
{
    extern KdMouseInfo *kdMouseInfo;
    uint8_t byte;
    int dx, dy;
    unsigned long flags;
    
    if (!mouse_state.initialized)
        return;
    
    /* Read available bytes */
    while (mouse_read_status() & MOUSE_STATUS_DATA_READY) {
        byte = mouse_read_data();
        
        /* Store byte in packet */
        mouse_state.packet[mouse_state.packet_byte] = byte;
        
        /* First byte should have bit 3 set (always 1 in PS/2 protocol) */
        if (mouse_state.packet_byte == 0) {
            if (!(byte & 0x08)) {
                /* Sync error - skip this byte */
                continue;
            }
        }
        
        mouse_state.packet_byte++;
        
        /* Standard PS/2 mouse: 3 bytes per packet */
        if (mouse_state.packet_byte >= 3) {
            mouse_state.packet_byte = 0;
            
            /* Parse packet */
            uint8_t status = mouse_state.packet[0];
            
            /* Check for overflow */
            if (status & (MOUSE_OVERFLOW_X | MOUSE_OVERFLOW_Y)) {
                /* Discard packet with overflow */
                continue;
            }
            
            /* Extract delta X */
            dx = mouse_state.packet[1];
            if (status & MOUSE_SIGN_X)
                dx -= 256;  /* Sign extend */
            
            /* Extract delta Y (inverted for X11 coordinate system) */
            dy = mouse_state.packet[2];
            if (status & MOUSE_SIGN_Y)
                dy -= 256;  /* Sign extend */
            dy = -dy;  /* Invert Y axis */
            
            /* Update position with bounds checking */
            mouse_state.x += dx;
            mouse_state.y += dy;
            
            if (mouse_state.x < 0) mouse_state.x = 0;
            if (mouse_state.y < 0) mouse_state.y = 0;
            if (mouse_state.x >= screen_width) mouse_state.x = screen_width - 1;
            if (mouse_state.y >= screen_height) mouse_state.y = screen_height - 1;
            
            /* Build button flags */
            flags = 0;
            if (status & MOUSE_BTN_LEFT)
                flags |= KD_BUTTON_1;
            if (status & MOUSE_BTN_MIDDLE)
                flags |= KD_BUTTON_2;
            if (status & MOUSE_BTN_RIGHT)
                flags |= KD_BUTTON_3;
            
            /*
             * Enqueue mouse event
             * 
             * KdEnqueueMouseEvent handles:
             * - Button state changes (press/release)
             * - Motion events
             * - Middle button emulation (if configured)
             */
            if (kdMouseInfo) {
                KdEnqueueMouseEvent(kdMouseInfo, flags, 
                                   mouse_state.x, mouse_state.y);
            }
        }
    }
}

/*
 * RiscvSetMouseBounds - Set mouse position bounds
 *
 * Call this when screen resolution is known.
 */
void RiscvSetMouseBounds(int width, int height)
{
    screen_width = width;
    screen_height = height;
    
    /* Keep mouse within new bounds */
    if (mouse_state.x >= width) mouse_state.x = width - 1;
    if (mouse_state.y >= height) mouse_state.y = height - 1;
}
```

---

## OS Functions Implementation

### OS-Level Driver

```c
/* riscv_os.c */

#include "riscv.h"

/*
 * Forward declarations for input polling
 */
extern void RiscvKeyboardRead(void);
extern void RiscvMouseRead(void);

static Bool os_initialized = FALSE;
static Bool os_enabled = FALSE;

/*
 * RiscvOsInit - Initialize OS-level services
 *
 * Called once at server startup.
 *
 * @return: 1 on success
 */
static int RiscvOsInit(void)
{
    /*
     * PLATFORM-SPECIFIC: Initialize system services
     * 
     * May include:
     * - Timer setup
     * - Interrupt controller configuration
     * - Memory management initialization
     * - Console/debug output setup
     */
    
    os_initialized = TRUE;
    
    ErrorF("RiscvOsInit: OS layer initialized\n");
    
    return 1;  /* Success */
}

/*
 * RiscvOsEnable - Enable OS services
 *
 * Called when X server takes control.
 */
static void RiscvOsEnable(void)
{
    if (!os_initialized)
        return;
    
    /*
     * PLATFORM-SPECIFIC: Enable services
     * 
     * May include:
     * - Enabling interrupts
     * - Starting timer
     * - Setting up I/O
     */
    
    os_enabled = TRUE;
}

/*
 * RiscvOsSpecialKey - Handle special key combinations
 *
 * Check for special keys like VT switching (Ctrl+Alt+Fn).
 * On bare-metal without VTs, this can be used for debugging.
 *
 * @param keysym: The keysym that was pressed
 * @return: TRUE if key was handled, FALSE to pass to clients
 */
static Bool RiscvOsSpecialKey(KeySym keysym)
{
    /*
     * Example special key handling:
     * - Ctrl+Alt+Backspace: Terminate X server
     * - Ctrl+Alt+F1-F12: VT switching (not applicable on bare-metal)
     */
    
    /* For bare-metal, typically return FALSE to pass all keys */
    return FALSE;
}

/*
 * RiscvOsDisable - Disable OS services
 *
 * Called when X server releases control.
 */
static void RiscvOsDisable(void)
{
    os_enabled = FALSE;
}

/*
 * RiscvOsFini - Cleanup OS services
 *
 * Called at X server shutdown.
 */
static void RiscvOsFini(void)
{
    os_enabled = FALSE;
    os_initialized = FALSE;
}

/*
 * RiscvOsPollEvents - Poll for input events
 *
 * Called from X server main loop.
 * This is the main input polling function.
 */
static void RiscvOsPollEvents(void)
{
    if (!os_enabled)
        return;
    
    /*
     * Poll all input devices
     * 
     * Each read function checks its hardware for new events
     * and calls the appropriate KdEnqueue* function.
     */
    RiscvKeyboardRead();
    RiscvMouseRead();
    
    /*
     * PLATFORM-SPECIFIC: Additional event sources
     * 
     * Could include:
     * - Touchscreen
     * - Gamepad/joystick
     * - Hardware buttons
     * - Timers
     */
}

/*
 * KdOsFuncs structure
 */
const KdOsFuncs RiscvOsFuncs = {
    .Init       = RiscvOsInit,
    .Enable     = RiscvOsEnable,
    .SpecialKey = RiscvOsSpecialKey,
    .Disable    = RiscvOsDisable,
    .Fini       = RiscvOsFini,
    .pollEvents = RiscvOsPollEvents,
};
```

---

## Event Queue Integration

### How Events Flow to X Server

Kdrive provides functions to enqueue input events:

```c
/* From tinyx/kdrive/src/kinput.c */

/*
 * KdEnqueueKeyboardEvent - Queue a keyboard event
 *
 * @param scan_code: PS/2 scancode (0-127)
 * @param is_up: 1 if key release, 0 if key press
 */
void KdEnqueueKeyboardEvent(unsigned char scan_code, unsigned char is_up);

/*
 * KdEnqueueMouseEvent - Queue a mouse event
 *
 * Handles both button state and position.
 * Kdrive tracks button state and generates press/release events.
 *
 * @param mi: Mouse info structure (from kdMouseInfo)
 * @param flags: Button state flags (KD_BUTTON_1|2|3|4|5)
 * @param x: Absolute X position
 * @param y: Absolute Y position
 *
 * Use KD_MOUSE_DELTA flag for relative movement:
 *   flags |= KD_MOUSE_DELTA;  // dx/dy instead of x/y
 */
void KdEnqueueMouseEvent(KdMouseInfo *mi, unsigned long flags, int x, int y);

/*
 * KdEnqueueMotionEvent - Queue a motion-only event
 *
 * @param mi: Mouse info structure
 * @param x: Absolute X position
 * @param y: Absolute Y position
 */
void KdEnqueueMotionEvent(KdMouseInfo *mi, int x, int y);
```

### Button Flags

```c
/* Button flag definitions */
#define KD_BUTTON_1     0x01    /* Left button */
#define KD_BUTTON_2     0x02    /* Middle button */
#define KD_BUTTON_3     0x04    /* Right button */
#define KD_BUTTON_4     0x08    /* Scroll up */
#define KD_BUTTON_5     0x10    /* Scroll down */
#define KD_MOUSE_DELTA  0x80000000  /* Relative movement flag */
```

---

## Implementation Template

### Main Initialization File

```c
/* riscv_init.c */

#include "riscv.h"

/*
 * External declarations for our driver functions
 */
extern const KdCardFuncs RiscvFuncs;
extern const KdKeyboardFuncs RiscvKeyboardFuncs;
extern const KdMouseFuncs RiscvMouseFuncs;
extern const KdOsFuncs RiscvOsFuncs;

/*
 * InitCard - Register graphics card
 *
 * Called by Kdrive at startup to discover cards.
 * This is the entry point for graphics driver registration.
 *
 * @param name: Driver name (may be NULL)
 */
void InitCard(char *name)
{
    KdCardInfo *card;
    
    /*
     * Register our card with Kdrive
     * 
     * KdCardInfoAdd allocates a KdCardInfo structure and
     * links it into Kdrive's card list.
     * 
     * The closure parameter can be used to pass platform-specific
     * data; NULL for simple implementations.
     */
    card = KdCardInfoAdd(&RiscvFuncs, NULL);
    if (!card) {
        FatalError("InitCard: Failed to register card\n");
    }
}

/*
 * InitOutput - Initialize graphics output
 *
 * Called by Kdrive to initialize display output.
 * Can be used for additional output setup.
 *
 * @param pScreenInfo: X server screen info
 * @param argc: Command line argument count
 * @param argv: Command line arguments
 */
void InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
    KdInitOutput(pScreenInfo, argc, argv);
}

/*
 * InitInput - Initialize input devices
 *
 * Called by Kdrive to initialize input.
 * Registers keyboard and mouse drivers.
 *
 * @param argc: Command line argument count
 * @param argv: Command line arguments
 */
void InitInput(int argc, char **argv)
{
    /*
     * Register mouse and keyboard drivers
     * 
     * KdInitInput sets up the input subsystem and
     * creates X input devices for the drivers.
     */
    KdInitInput(&RiscvMouseFuncs, &RiscvKeyboardFuncs);
}

/*
 * OsVendorInit - Initialize OS-level services
 *
 * Called early in X server startup.
 * Registers OS functions with Kdrive.
 */
void OsVendorInit(void)
{
    /*
     * Register OS functions
     * 
     * KdOsInit stores the function pointers and calls
     * the Init function.
     */
    KdOsInit(&RiscvOsFuncs);
}

/*
 * ddxUseMsg - Print driver-specific usage
 */
void ddxUseMsg(void)
{
    ErrorF("\nRISC-V bare-metal driver options:\n");
    ErrorF("  -fb <addr>     Framebuffer base address (hex)\n");
    ErrorF("  -fbsize WxH    Framebuffer dimensions\n");
    ErrorF("  -fbdepth N     Framebuffer depth (16, 24, 32)\n");
}

/*
 * ddxProcessArgument - Process driver-specific arguments
 *
 * @return: Number of arguments consumed
 */
int ddxProcessArgument(int argc, char **argv, int i)
{
    /* 
     * Process any driver-specific command line arguments
     * Return number of args consumed (0 if not recognized)
     */
    return 0;
}

/*
 * ddxGiveUp - Called on server exit
 */
void ddxGiveUp(void)
{
    /* Cleanup as needed */
}

/*
 * AbortDDX - Called on fatal error
 */
void AbortDDX(void)
{
    /* Emergency cleanup */
}
```

---

## Build Integration

### Makefile.am

```makefile
# tinyx/kdrive/riscv/Makefile.am

noinst_LTLIBRARIES = libriscv.la

libriscv_la_SOURCES = \
    riscv.h \
    riscv_fb.c \
    riscv_kbd.c \
    riscv_mouse.c \
    riscv_os.c \
    riscv_init.c

libriscv_la_CFLAGS = \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/kdrive/src \
    -I$(top_srcdir)/fb \
    -I$(top_srcdir)/mi \
    -I$(top_srcdir)/dix \
    $(DIX_CFLAGS)

# Platform-specific definitions
libriscv_la_CFLAGS += \
    -DRISCV_FB_BASE=0x80000000 \
    -DRISCV_FB_WIDTH=1024 \
    -DRISCV_FB_HEIGHT=768 \
    -DRISCV_FB_BPP=32
```

### CMake Integration

```cmake
# tinyx/kdrive/riscv/CMakeLists.txt

add_library(kdrive_riscv STATIC
    riscv.h
    riscv_fb.c
    riscv_kbd.c
    riscv_mouse.c
    riscv_os.c
    riscv_init.c
)

target_include_directories(kdrive_riscv PRIVATE
    ${CMAKE_SOURCE_DIR}/tinyx/include
    ${CMAKE_SOURCE_DIR}/tinyx/kdrive/src
    ${CMAKE_SOURCE_DIR}/tinyx/fb
    ${CMAKE_SOURCE_DIR}/tinyx/mi
    ${CMAKE_SOURCE_DIR}/tinyx/dix
    ${CMAKE_SOURCE_DIR}/X11
)

# Platform configuration
target_compile_definitions(kdrive_riscv PRIVATE
    RISCV_FB_BASE=0x80000000
    RISCV_FB_WIDTH=1024
    RISCV_FB_HEIGHT=768
    RISCV_FB_BPP=32
    RISCV_KBD_BASE=0x10000000
    RISCV_MOUSE_BASE=0x10001000
)

# Link with main Kdrive library
target_link_libraries(kdrive_riscv
    kdrive_core
    fb
    mi
    dix
)
```

---

## Testing & Validation

### Test Checklist

#### Framebuffer Tests
- [ ] Card initializes without errors
- [ ] Screen dimensions correct
- [ ] Pixel format matches expected
- [ ] Fill rectangle renders correctly
- [ ] Colors display accurately
- [ ] DPMS state changes work

#### Keyboard Tests
- [ ] Keyboard initializes
- [ ] Keys generate correct keysyms
- [ ] Shift modifiers work
- [ ] Caps Lock LED toggles
- [ ] Special keys (F1-F12, arrows) work
- [ ] Key repeat functions

#### Mouse Tests
- [ ] Mouse initializes
- [ ] Movement tracks correctly
- [ ] Buttons generate correct events
- [ ] Cursor stays within screen bounds
- [ ] No lag or jitter

### Debug Output

Enable debug logging in your implementation:

```c
#define RISCV_DEBUG 1

#if RISCV_DEBUG
#define RISCV_LOG(fmt, ...) ErrorF("RISCV: " fmt "\n", ##__VA_ARGS__)
#else
#define RISCV_LOG(fmt, ...)
#endif
```

### Test Application

Simple X client to verify functionality:

```c
/* test_input.c - Simple input test */
#include <X11/Xlib.h>
#include <stdio.h>

int main() {
    Display *dpy = XOpenDisplay(NULL);
    Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                                     0, 0, 400, 300, 1, 0, 0xFFFFFF);
    XSelectInput(dpy, win, KeyPressMask | ButtonPressMask | PointerMotionMask);
    XMapWindow(dpy, win);
    
    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        
        switch (ev.type) {
        case KeyPress:
            printf("Key: %d\n", ev.xkey.keycode);
            break;
        case ButtonPress:
            printf("Button: %d at %d,%d\n", 
                   ev.xbutton.button, ev.xbutton.x, ev.xbutton.y);
            break;
        case MotionNotify:
            printf("Motion: %d,%d\n", ev.xmotion.x, ev.xmotion.y);
            break;
        }
    }
}
```

---

## Quick Reference

### Minimum Implementation Checklist

1. **Framebuffer** (KdCardFuncs):
   - [ ] `cardinit` - Map framebuffer memory
   - [ ] `scrinit` - Set screen dimensions and format
   - [ ] `enable` - Enable display
   - [ ] `disable` - Disable display
   - [ ] `cardfini` - Cleanup

2. **Keyboard** (KdKeyboardFuncs):
   - [ ] `Load` - Load keymap
   - [ ] `Init` - Initialize hardware
   - [ ] `Leds` - Set LEDs
   - [ ] `Bell` - Ring bell (can be stub)
   - [ ] `Fini` - Cleanup
   - [ ] Polling function that calls `KdEnqueueKeyboardEvent`

3. **Mouse** (KdMouseFuncs):
   - [ ] `Init` - Initialize hardware
   - [ ] `Fini` - Cleanup
   - [ ] Polling function that calls `KdEnqueueMouseEvent`

4. **OS** (KdOsFuncs):
   - [ ] `Init` - Initialize
   - [ ] `Enable` - Enable services
   - [ ] `pollEvents` - Poll all input devices
   - [ ] `Disable` - Disable services
   - [ ] `Fini` - Cleanup

5. **Registration**:
   - [ ] `InitCard` - Register card
   - [ ] `InitInput` - Register input drivers
   - [ ] `OsVendorInit` - Register OS functions

---

## See Also

- [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) - HAL interface details
- [TINYX_MIGRATION.md](TINYX_MIGRATION.md) - TinyX overview
- [WMB_API_DESIGN.md](WMB_API_DESIGN.md) - Overall WMB architecture
- `tinyx/kdrive/fbdev/` - Reference Linux framebuffer implementation
- `tinyx/kdrive/src/kdrive.h` - Kdrive interface definitions
- `tinyx/kdrive/src/kinput.c` - Input event handling
