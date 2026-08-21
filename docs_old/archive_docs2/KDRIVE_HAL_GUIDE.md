# Kdrive HAL Implementation Guide for WMB

**Date**: January 23, 2026
**Purpose**: Complete HAL implementation guide for TinyX/Kdrive on RISC-V bare-metal and hosted (SDL) builds

---

## Table of Contents

1. [Overview](#overview)
2. [Platform Backends](#platform-backends)
3. [Kdrive Interface Architecture](#kdrive-interface-architecture)
4. [Window Management Responsibilities](#window-management-responsibilities)
5. [Framebuffer Implementation](#framebuffer-implementation)
6. [Keyboard Implementation](#keyboard-implementation)
   - [Standard HAL Keyboard Protocol](#standard-hal-keyboard-protocol)
   - [Alternative: Direct Hardware Keyboard Driver](#alternative-direct-hardware-keyboard-driver)
7. [Mouse/Pointer Implementation](#mouse-pointer-implementation)
8. [OS Functions Implementation](#os-functions-implementation)
9. [Timer & Syscalls Integration](#timer--syscalls-integration)
10. [Event Queue Integration](#event-queue-integration)
11. [Implementation Template](#implementation-template)
12. [Build Integration](#build-integration)
13. [Testing & Validation](#testing--validation)

---

## Overview

This document provides the complete guide for implementing the Hardware Abstraction Layer (HAL) for TinyX/Kdrive in the WMB project.

### Key Insight: Kdrive IS the HAL

TinyX was designed with a clean HAL model through Kdrive. Instead of creating a separate abstraction layer, we implement the well-defined Kdrive interface structures directly:

- **KdCardFuncs** - Graphics card/framebuffer operations
- **KdKeyboardFuncs** - Keyboard driver interface
- **KdPointerFuncs** - Mouse/pointer driver interface
- **KdOsFuncs** - OS-level functions (timing, bells, etc.)

This approach:
- Aligns with BASE_REQUIREMENTS #4 (HAL compatible with X11 server)
- Minimizes patching (BASE_REQUIREMENTS #5)
- Uses proven, embedded-focused design from X.org

### What You're Implementing

```
┌─────────────────────────────────────────────────────────────────┐
│                     TinyX X11 Server                            │
├─────────────────────────────────────────────────────────────────┤
│  DIX (Device Independent X)  │  MI (Machine Independent)        │
├──────────────────────────────┴──────────────────────────────────┤
│                        Kdrive Core                              │
│  ┌──────────────┐  ┌───────────────┐  ┌──────────────┐          │
│  │ KdCardFuncs  │  │KdKeyboardFuncs│  │KdPointerFuncs│          │
│  └──────┬───────┘  └──────┬────────┘  └─────┬────────┘          │
├─────────┼─────────────────┼─────────────────┼───────────────────┤
│         │    YOUR IMPLEMENTATION LAYER      │                   │
│         ▼                 ▼                 ▼                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │Platform fb.c │  │Platform kbd.c│  │Platform mouse│           │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘           │
├─────────┼─────────────────┼─────────────────┼───────────────────┤
│         ▼                 ▼                 ▼                   │
│                    HAL / Hardware                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │ Framebuffer  │  │   Keyboard   │  │    Mouse     │           │
│  │  Hardware    │  │   Hardware   │  │   Hardware   │           │
│  └──────────────┘  └──────────────┘  └──────────────┘           │
└─────────────────────────────────────────────────────────────────┘
```

### Files to Create

```
tinyx/kdrive/
├── sdl/                  # Hosted development backend
│   ├── sdl.h
│   ├── sdl_fb.c
│   ├── sdl_kbd.c
│   ├── sdl_mouse.c
│   ├── sdl_os.c
│   ├── sdl_init.c
│   └── CMakeLists.txt
│
└── riscv/                # Target deployment backend
    ├── riscv.h
    ├── riscv_fb.c
    ├── riscv_kbd.c
    ├── riscv_mouse.c
    ├── riscv_os.c
    ├── riscv_init.c
    └── CMakeLists.txt
```

---

## Platform Backends

The HAL supports multiple platform backends through the same Kdrive interfaces:

| Backend | Platform | Use Case | Display | Input |
|---------|----------|----------|---------|-------|
| **SDL** | Linux/Windows/Mac | Hosted development | SDL_Window | SDL_Event |
| **RISC-V** | Bare-metal | Target deployment | MMIO framebuffer | GPIO/UART |

### Hosted Development (SDL Backend)

For development and testing, WMB can run natively on a PC using SDL2:

```bash
# Build hosted version
cmake -B build -DWMB_HOSTED_BUILD=ON
cmake --build build
./build/wmb   # See X11 in SDL window!
```

**Benefits:**
- Fast iteration (no hardware deployment)
- Full debugging with GDB/IDE
- Visual output in SDL window
- Keyboard/mouse via SDL events
- Same Kdrive interface as target

### Target Deployment (RISC-V Backend)

For final deployment on RISC-V hardware:

```bash
# Build for RISC-V
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake
cmake --build build
# Deploy build/wmb.elf to hardware
```

### How Backends Share Code

Both backends implement the same Kdrive interfaces:

```c
/* SDL Backend */
const KdCardFuncs SdlFuncs = {
    .cardinit = SdlCardInit,
    .scrinit  = SdlScreenInit,
    .enable   = SdlEnable,
    /* ... */
};

/* RISC-V Backend */
const KdCardFuncs RiscvFuncs = {
    .cardinit = RiscvCardInit,
    .scrinit  = RiscvScreenInit,
    .enable   = RiscvEnable,
    /* ... */
};
```

CMake selects the appropriate backend at build time:
```cmake
if(WMB_HOSTED_BUILD)
    add_subdirectory(kdrive/tima)   # Platform-independent, uses HAL
else()
    add_subdirectory(kdrive/tima)   # Same code, different HAL backend
endif()
```

---

## Kdrive Interface Architecture

### Kdrive Data Flow

```
Hardware Event → Platform Poll → Kdrive Input → X Server Event Queue → Client

┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ Hardware │──▶│ Platform │──▶│ Kdrive   │──▶│ X Server │
│ (IRQ/    │    │ Poll/    │    │ Enqueue  │    │ Event    │
│  Poll)   │    │ Read     │    │ Function │    │ Dispatch │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
```

### Graphics Data Flow

```
X Client Request → DIX → MI → fb library → Framebuffer Memory

┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ X Client │──▶│ DIX/MI   │──▶│ fb lib   │──▶│ Frame-   │
│ DrawRect │    │ Request  │    │ render   │    │ buffer   │
│          │    │ Process  │    │          │    │ Memory   │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
```

### Interface Structures

#### KdCardFuncs - Graphics Card Interface

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

#### KdKeyboardFuncs - Keyboard Interface

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

#### KdMouseFuncs - Mouse Interface

```c
typedef struct _KdMouseFuncs {
    Bool (*Init)(void);         /* Initialize mouse hardware */
    void (*Fini)(void);         /* Cleanup mouse */
} KdMouseFuncs;
```

**All functions required.**

#### KdOsFuncs - OS-Level Interface

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

## Window Management Responsibilities

### What X Server Manages (No HAL Needed)

The X server maintains complete window state in memory:

```c
/* X server internal structures */
typedef struct _WindowRec {
    DrawableRec drawable;           // Drawable properties
    struct _WindowRec *parent;      // Parent in window tree
    struct _WindowRec *nextSib;     // Next sibling
    struct _WindowRec *firstChild;  // First child

    BoxRec winSize;                 // Window position/size
    RegionRec clipList;             // Clipping region
    RegionRec borderClip;           // Border clipping

    unsigned int mapped:1;          // Visibility state
    unsigned int realized:1;        // Created on screen
    /* ... many more fields ... */
} WindowRec;
```

**X Server handles these operations internally:**
- Creating/destroying windows in the tree
- Calculating window positions relative to root
- Computing clipping regions from window overlap
- Managing window stacking order (which window is on top)
- Tracking window visibility (mapped/unmapped)
- Handling window reparenting
- Processing ConfigureWindow requests (resize/move)

**Why no HAL calls needed:** These are pure data structure operations on the window tree. The X server already has all information.

### What HAL Provides (Hardware Operations)

The Kdrive interface only provides **rendering primitives** that X server uses to **draw** windows:

```c
/* Example: X server draws a window using framebuffer */
void draw_window_background(WindowPtr win) {
    /* X server knows window position from its internal tree */
    int x = win->winSize.x;
    int y = win->winSize.y;
    int w = win->winSize.width;
    int h = win->winSize.height;
    uint32_t bg_color = win->background_pixel;

    /* fb library renders to framebuffer memory */
    /* (Kdrive provides framebuffer pointer, fb lib does rendering) */
}
```

**HAL is a dumb rendering layer:**
- Doesn't know about windows
- Doesn't manage screen regions
- Just provides framebuffer access and input events

### WMB Bare-Metal Architecture

On bare-metal, there is **one window manager** (X server + JWM):

```
WMB Bare-Metal Architecture:
┌─────────────────────────────────────────────────────────────┐
│ WMB (X11 Server + JWM - owns everything)                    │
│ - Manages all windows directly                              │
│ - Owns window tree in memory                                │
│ - Calculates all positions/clipping                         │
└───────────────────────────┬─────────────────────────────────┘
                            │ Kdrive interface only
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Kdrive Driver (Platform Backend)                            │
│ - Provides framebuffer pointer                              │
│ - Polls input events                                        │
│ - No window knowledge                                       │
└─────────────────────────────────────────────────────────────┘
```

**X server never needs to query an external entity** because it IS the authority on all window information.

---

## Framebuffer Implementation

### Data Structures

```c
/* riscv.h (or sdl.h for hosted) */

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

### Standard HAL Keyboard Protocol

The WMB project uses a **standardized keyboard HAL API** that is consistent across all platforms (SDL hosted, RISC-V bare-metal, or any future platform). This ensures portability and simplifies porting to new hardware.

#### HAL API Functions

```c
/* Initialize keyboard driver - call once at startup */
void keypad_driver_init(void);

/* Check if keyboard data is available (non-blocking) */
bool_t keypad_driver_has_data(void);

/* Read one byte from keyboard buffer */
uint8_t keypad_driver_read_data(void);
```

#### Protocol Format

The keyboard protocol uses a simple command+data byte format:

| Command | Name | Description | Data Byte |
|---------|------|-------------|-----------|
| `0x01` | Modifier | Modifier key state changed | Modifier mask |
| `0x02` | Key Down | Key was pressed | Key data value |
| `0x03` | Key Up | Key was released | Key data value |

**Reading Protocol:**
```c
cmd = keypad_driver_read_data();   // First byte: command
data = keypad_driver_read_data();  // Second byte: data
```

#### Key Data Values

For printable ASCII characters (0x20-0x7E), the key data value equals the ASCII code:
- `'a'` (0x61) = key data 0x61
- `'A'` (0x41) = key data 0x41
- `' '` (0x20) = key data 0x20

For special keys, fixed values are used:

| Key | Data Value | Notes |
|-----|------------|-------|
| Backspace | `127` (0x7F) | DEL character |
| Enter | `13` (0x0D) | CR character |
| Escape | `27` (0x1B) | ESC character |
| Up Arrow | `11` (0x0B) | VT character |
| Down Arrow | `10` (0x0A) | LF character |
| Right Arrow | `21` (0x15) | NAK character |
| Left Arrow | `8` (0x08) | BS character |

#### Implementation Example (Consumer Side)

```c
/* sdl_keyboard.c - KDrive keyboard driver */

void sdl_KeyboardPoll(void)
{
    uint8_t cmd, data;

    while (keypad_driver_has_data()) {
        cmd = keypad_driver_read_data();

        if (!keypad_driver_has_data())
            break;  /* Incomplete packet */

        data = keypad_driver_read_data();

        switch (cmd) {
            case 0x01:  /* Modifier state */
                sdl_HandleModifier(data);
                break;

            case 0x02:  /* Key down */
                KdEnqueueKeyboardEvent(data, 0);
                break;

            case 0x03:  /* Key up */
                KdEnqueueKeyboardEvent(data, 1);
                break;
        }
    }
}
```

#### Implementation Example (Producer Side)

```c
/* keyraw_driver.c - HAL keyboard driver (SDL platform) */

void keypad_driver_event_key(uint32_t window_id, uint8_t key_code,
                              uint16_t control_key, uint8_t state)
{
    if (key_code == 0) {
        /* Modifier key event */
        buffer_write(&keypad_buffer_data, 0x01);
        buffer_write(&keypad_buffer_data, (uint8_t)control_key);
    }
    else if (key_code == 1) {
        /* Special key event */
        buffer_write(&keypad_buffer_data, (state == KEY_DOWN) ? 0x02 : 0x03);

        switch (control_key) {
            case EVENT_KEY_BACKSPACE: buffer_write(&keypad_buffer_data, 127); break;
            case EVENT_KEY_ENTER:     buffer_write(&keypad_buffer_data, 13);  break;
            case EVENT_KEY_ESC:       buffer_write(&keypad_buffer_data, 27);  break;
            case EVENT_KEY_UP:        buffer_write(&keypad_buffer_data, 11);  break;
            case EVENT_KEY_DOWN:      buffer_write(&keypad_buffer_data, 10);  break;
            case EVENT_KEY_RIGHT:     buffer_write(&keypad_buffer_data, 21);  break;
            case EVENT_KEY_LEFT:      buffer_write(&keypad_buffer_data, 8);   break;
        }
    }
    else if ((key_code >= 0x20) && (key_code <= 125)) {
        /* Printable ASCII */
        buffer_write(&keypad_buffer_data, (state == KEY_DOWN) ? 0x02 : 0x03);
        buffer_write(&keypad_buffer_data, key_code);
    }
}
```

#### Data Flow

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│ Platform Input  │──▶│ keypad_driver_*  │──▶│ KDrive Driver   │
│ (SDL/GPIO/UART) │    │ (Ring Buffer)    │    │ (Poll + Enqueue)│
└─────────────────┘    └──────────────────┘    └─────────────────┘
       │                        │                       │
       │                        │                       ▼
  SDL_Event or            0x01/0x02/0x03         KdEnqueueKeyboardEvent()
  GPIO interrupt          protocol bytes               │
                                                       ▼
                                                X11 Event Queue
```

#### Porting to New Platforms

To port the keyboard HAL to a new platform:

1. **Implement the producer side** (`keypad_driver_event_key` equivalent):
   - Capture hardware input events (GPIO, UART, SPI, etc.)
   - Convert to 0x01/0x02/0x03 protocol
   - Write to ring buffer

2. **The consumer side is platform-independent**:
   - KDrive driver calls `keypad_driver_has_data()` / `keypad_driver_read_data()`
   - Same code works on all platforms

---

### Alternative: Direct Hardware Keyboard Driver

For platforms with PS/2 or memory-mapped keyboard controllers, a direct hardware implementation can be used instead of the HAL protocol:

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
 */
static void RiscvLoadKeymap(void)
{
    extern KeySym kdKeymap[];
    extern int kdMinScanCode;
    extern int kdMaxScanCode;

    /* Set scancode range */
    kdMinScanCode = 1;
    kdMaxScanCode = 127;

    /* Clear keymap */
    memset(kdKeymap, 0, sizeof(KeySym) * KD_MAX_LENGTH * KD_MAX_WIDTH);

    /*
     * Load standard US keymap - Row 0 = normal, Row 1 = shifted
     * (Abbreviated - full implementation needed)
     */

    /* Escape */
    kdKeymap[(1 - 1) * KD_MAX_WIDTH + 0] = XK_Escape;

    /* Number row */
    kdKeymap[(2 - 1) * KD_MAX_WIDTH + 0] = XK_1;
    kdKeymap[(2 - 1) * KD_MAX_WIDTH + 1] = XK_exclam;
    /* ... continue for all keys ... */

    /* QWERTY row */
    kdKeymap[(16 - 1) * KD_MAX_WIDTH + 0] = XK_q;
    kdKeymap[(16 - 1) * KD_MAX_WIDTH + 1] = XK_Q;
    /* ... continue for all keys ... */

    /* Space */
    kdKeymap[(57 - 1) * KD_MAX_WIDTH + 0] = XK_space;

    /* Note: Complete implementation should include all keys */
}

/*
 * RiscvKeyboardInit - Initialize keyboard hardware
 */
static int RiscvKeyboardInit(void)
{
    /* PLATFORM-SPECIFIC: Initialize keyboard controller */
    kbd_write_cmd(KBD_CMD_RESET);

    /* Wait for acknowledgment */
    while (!(kbd_read_status() & KBD_STATUS_DATA_READY))
        ;
    kbd_read_data();  /* Read ACK */

    kbd_write_cmd(KBD_CMD_ENABLE);

    ErrorF("RiscvKeyboardInit: Keyboard initialized\n");
    return 1;
}

/*
 * RiscvKeyboardLeds - Set keyboard LED state
 */
static void RiscvKeyboardLeds(int leds)
{
    uint8_t led_byte = 0;

    if (leds & (1 << 0)) led_byte |= KBD_LED_CAPS;
    if (leds & (1 << 1)) led_byte |= KBD_LED_NUM;
    if (leds & (1 << 2)) led_byte |= KBD_LED_SCROLL;

    kbd_write_cmd(KBD_CMD_SET_LEDS);
    while (!(kbd_read_status() & KBD_STATUS_DATA_READY))
        ;
    kbd_read_data();
    kbd_write_data(led_byte);
}

/*
 * RiscvKeyboardBell - Ring system bell
 */
static void RiscvKeyboardBell(int volume, int pitch, int duration)
{
    /* PLATFORM-SPECIFIC: Generate bell tone or stub */
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
    .LockLed = 1,
};

/*
 * RiscvKeyboardRead - Read and process keyboard events
 *
 * Called from event polling loop.
 */
void RiscvKeyboardRead(void)
{
    uint8_t scancode;
    unsigned char is_up;

    while (kbd_read_status() & KBD_STATUS_DATA_READY) {
        scancode = kbd_read_data();

        if (scancode & 0x80) {
            is_up = 1;
            scancode &= 0x7F;
        } else {
            is_up = 0;
        }

        if (scancode == 0 || scancode > 127)
            continue;

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
#define RISCV_MOUSE_BASE    0x10001000
#endif

#define RISCV_MOUSE_DATA    (RISCV_MOUSE_BASE + 0x00)
#define RISCV_MOUSE_STATUS  (RISCV_MOUSE_BASE + 0x04)

#define MOUSE_STATUS_DATA_READY  0x01
#define MOUSE_BTN_LEFT      0x01
#define MOUSE_BTN_RIGHT     0x02
#define MOUSE_BTN_MIDDLE    0x04
#define MOUSE_SIGN_X        0x10
#define MOUSE_SIGN_Y        0x20

/* Mouse state */
static struct {
    Bool initialized;
    int  packet_byte;
    uint8_t packet[4];
    int  x, y;
    int  buttons;
} mouse_state;

static int screen_width = 1024;
static int screen_height = 768;

static inline uint8_t mouse_read_status(void)
{
    return *(volatile uint8_t *)RISCV_MOUSE_STATUS;
}

static inline uint8_t mouse_read_data(void)
{
    return *(volatile uint8_t *)RISCV_MOUSE_DATA;
}

/*
 * RiscvMouseInit - Initialize mouse hardware
 */
static Bool RiscvMouseInit(void)
{
    mouse_state.initialized = TRUE;
    mouse_state.packet_byte = 0;
    mouse_state.x = screen_width / 2;
    mouse_state.y = screen_height / 2;
    mouse_state.buttons = 0;

    ErrorF("RiscvMouseInit: Mouse initialized\n");
    return TRUE;
}

static void RiscvMouseFini(void)
{
    mouse_state.initialized = FALSE;
}

const KdMouseFuncs RiscvMouseFuncs = {
    .Init = RiscvMouseInit,
    .Fini = RiscvMouseFini,
};

/*
 * RiscvMouseRead - Read and process mouse events
 *
 * Called from event polling loop.
 */
void RiscvMouseRead(void)
{
    extern KdMouseInfo *kdMouseInfo;
    uint8_t byte;
    int dx, dy;
    unsigned long flags;

    if (!mouse_state.initialized)
        return;

    while (mouse_read_status() & MOUSE_STATUS_DATA_READY) {
        byte = mouse_read_data();
        mouse_state.packet[mouse_state.packet_byte] = byte;

        /* First byte should have bit 3 set */
        if (mouse_state.packet_byte == 0 && !(byte & 0x08))
            continue;

        mouse_state.packet_byte++;

        if (mouse_state.packet_byte >= 3) {
            mouse_state.packet_byte = 0;

            uint8_t status = mouse_state.packet[0];

            /* Extract delta */
            dx = mouse_state.packet[1];
            if (status & MOUSE_SIGN_X) dx -= 256;

            dy = mouse_state.packet[2];
            if (status & MOUSE_SIGN_Y) dy -= 256;
            dy = -dy;  /* Invert Y axis */

            /* Update position with bounds */
            mouse_state.x += dx;
            mouse_state.y += dy;

            if (mouse_state.x < 0) mouse_state.x = 0;
            if (mouse_state.y < 0) mouse_state.y = 0;
            if (mouse_state.x >= screen_width) mouse_state.x = screen_width - 1;
            if (mouse_state.y >= screen_height) mouse_state.y = screen_height - 1;

            /* Button flags */
            flags = 0;
            if (status & MOUSE_BTN_LEFT)   flags |= KD_BUTTON_1;
            if (status & MOUSE_BTN_MIDDLE) flags |= KD_BUTTON_2;
            if (status & MOUSE_BTN_RIGHT)  flags |= KD_BUTTON_3;

            if (kdMouseInfo) {
                KdEnqueueMouseEvent(kdMouseInfo, flags,
                                   mouse_state.x, mouse_state.y);
            }
        }
    }
}

void RiscvSetMouseBounds(int width, int height)
{
    screen_width = width;
    screen_height = height;
    if (mouse_state.x >= width) mouse_state.x = width - 1;
    if (mouse_state.y >= height) mouse_state.y = height - 1;
}
```

### Integration Points: How TinyX Calls Mouse Functions

Understanding where to connect your mouse driver to TinyX is critical. Here's the complete call flow:

#### 1. InitInput - External Function You Must Implement

`InitInput()` is an **external function declared by TinyX** that **you must implement** in your platform backend. TinyX calls it from `main.c` during server startup:

```c
/* tinyx/dix/main.c (TinyX code - you don't modify this) */
InitInput(argc, argv);  /* TinyX calls YOUR implementation */
```

**Declaration** (in `tinyx/include/input.h`):
```c
void InitInput(int argc, char **argv);
```

**Your implementation** (in `riscv_init.c` or `sdl_init.c`):
```c
void InitInput(int argc, char **argv)
{
    /* Call KDrive API to register your input drivers */
    KdInitInput(&RiscvMouseFuncs, &RiscvKeyboardFuncs);
}
```

#### 2. KdInitInput - TinyX/KDrive API for Registration

`KdInitInput()` is a **TinyX/KDrive API function** that you call from your `InitInput()` to register your mouse and keyboard drivers:

```c
/* KDrive API - defined in tinyx/kdrive/src/kinput.c */
void KdInitInput(const KdMouseFuncs *mouse, const KdKeyboardFuncs *keyboard);
```

This registers your `KdMouseFuncs` and `KdKeyboardFuncs` structures with KDrive. After registration, KDrive will call:
- `RiscvMouseInit()` / `RiscvKeyboardInit()` - During X server startup
- `RiscvMouseFini()` / `RiscvKeyboardFini()` - During X server shutdown

#### 3. Polling via KdOsFuncs.pollEvents

The `RiscvMouseRead()` function is **NOT** called directly by KDrive. You must call it from your OS polling function:

```c
/* riscv_os.c */

static void RiscvOsPollEvents(void)
{
    if (!os_enabled) return;

    /* YOU call your input read functions here */
    RiscvKeyboardRead();
    RiscvMouseRead();
}

const KdOsFuncs RiscvOsFuncs = {
    /* ... */
    .pollEvents = RiscvOsPollEvents,  /* KDrive calls this */
};
```

**Call flow:**
```
TinyX Main Loop (Dispatch.c)
    ↓
WaitForSomething() - waits for events or timeout
    ↓
KdOsFuncs.pollEvents() - called periodically
    ↓
RiscvOsPollEvents() - YOUR function
    ↓
RiscvMouseRead() + RiscvKeyboardRead() - YOUR input readers
    ↓
KdEnqueueMouseEvent() / KdEnqueueKeyboardEvent() - queue to X server
```

#### 3. Setting Mouse Bounds (Screen Dimensions)

`RiscvSetMouseBounds()` must be called when the screen is initialized so the mouse cursor stays within screen boundaries. Call it from your framebuffer enable function:

```c
/* riscv_fb.c */

extern void RiscvSetMouseBounds(int width, int height);

Bool RiscvEnable(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    RiscvPriv *priv = pScreenPriv->card->driver;

    /* Set mouse bounds to match screen dimensions */
    RiscvSetMouseBounds(priv->width, priv->height);

    /* Clear framebuffer */
    memset(priv->fb, 0, priv->fb_size);

    return TRUE;
}
```

**Alternative location** - in screen init:

```c
/* riscv_fb.c */

Bool RiscvScreenInit(KdScreenInfo *screen)
{
    RiscvPriv *priv = screen->card->driver;

    /* ... other initialization ... */

    /* Set mouse bounds */
    RiscvSetMouseBounds(priv->width, priv->height);

    return TRUE;
}
```

#### Summary: Function Call Origins

| Function | Called By | When |
|----------|-----------|------|
| `RiscvMouseInit()` | KDrive via `KdMouseFuncs.Init` | X server startup |
| `RiscvMouseFini()` | KDrive via `KdMouseFuncs.Fini` | X server shutdown |
| `RiscvMouseRead()` | **You** from `RiscvOsPollEvents()` | Every main loop iteration |
| `RiscvSetMouseBounds()` | **You** from `RiscvEnable()` or `RiscvScreenInit()` | Screen initialization |

---

## OS Functions Implementation

### OS-Level Driver

```c
/* riscv_os.c */

#include "riscv.h"

extern void RiscvKeyboardRead(void);
extern void RiscvMouseRead(void);

static Bool os_initialized = FALSE;
static Bool os_enabled = FALSE;

static int RiscvOsInit(void)
{
    os_initialized = TRUE;
    ErrorF("RiscvOsInit: OS layer initialized\n");
    return 1;
}

static void RiscvOsEnable(void)
{
    if (!os_initialized) return;
    os_enabled = TRUE;
}

static Bool RiscvOsSpecialKey(KeySym keysym)
{
    /* Return FALSE to pass all keys to clients */
    return FALSE;
}

static void RiscvOsDisable(void)
{
    os_enabled = FALSE;
}

static void RiscvOsFini(void)
{
    os_enabled = FALSE;
    os_initialized = FALSE;
}

static void RiscvOsPollEvents(void)
{
    if (!os_enabled) return;

    /* Poll all input devices */
    RiscvKeyboardRead();
    RiscvMouseRead();
}

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

## Timer & Syscalls Integration

### How Timer Works with Kdrive

TinyX, JWM, and X11 use standard POSIX `gettimeofday()`. For bare-metal, implement this in your **newlib syscalls layer**:

```c
/* syscalls.c - in your OS layer */

#include <sys/time.h>

/* Platform-specific timer function */
extern uint64_t platform_timer_get_us(void);

int _gettimeofday(struct timeval *tv, void *tz)
{
    uint64_t us = platform_timer_get_us();
    tv->tv_sec = us / 1000000;
    tv->tv_usec = us % 1000000;
    return 0;
}
```

### Connection Flow

```
TinyX/JWM calls: gettimeofday()
       ↓
newlib calls: _gettimeofday()
       ↓
OS syscall: calls platform_timer_get_us()
       ↓
Platform: reads RISC-V mtime CSR (or SDL_GetTicks for hosted)
```

### RISC-V Timer Example

```c
/* Platform-specific timer for RISC-V */

#define MTIME_BASE  0x0200BFF8  /* Standard CLINT mtime address */
#define MTIME_FREQ  10000000    /* 10 MHz typical */

uint64_t platform_timer_get_us(void)
{
    uint64_t mtime = *(volatile uint64_t *)MTIME_BASE;
    return (mtime * 1000000) / MTIME_FREQ;
}
```

### SDL Timer Example

```c
/* Platform-specific timer for SDL hosted build */

#include <SDL2/SDL.h>

uint64_t platform_timer_get_us(void)
{
    return (uint64_t)SDL_GetTicks() * 1000;
}
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
 * @param mi: Mouse info structure (from kdMouseInfo)
 * @param flags: Button state flags (KD_BUTTON_1|2|3|4|5)
 * @param x: Absolute X position
 * @param y: Absolute Y position
 */
void KdEnqueueMouseEvent(KdMouseInfo *mi, unsigned long flags, int x, int y);
```

### Button Flags

```c
#define KD_BUTTON_1     0x01    /* Left button */
#define KD_BUTTON_2     0x02    /* Middle button */
#define KD_BUTTON_3     0x04    /* Right button */
#define KD_BUTTON_4     0x08    /* Scroll up */
#define KD_BUTTON_5     0x10    /* Scroll down */
#define KD_MOUSE_DELTA  0x80000000  /* Relative movement flag */
```

---

## Implementation Template

### External Functions Required by TinyX

TinyX expects your platform backend to implement these **external functions**. TinyX calls them from `main.c` during server startup:

| Function | Called From | Purpose | You Call (KDrive API) |
|----------|-------------|---------|----------------------|
| `InitCard()` | `main.c` | Register graphics card | `KdCardInfoAdd()` |
| `InitOutput()` | `main.c` | Initialize graphics | `KdInitOutput()` |
| `InitInput()` | `main.c` | Register input drivers | `KdInitInput()` |
| `OsVendorInit()` | `os/utils.c` | Register OS functions | `KdOsInit()` |
| `ddxUseMsg()` | `os/utils.c` | Print usage message | N/A |
| `ddxProcessArgument()` | `os/utils.c` | Handle CLI args | N/A |
| `ddxGiveUp()` | Various | Cleanup on exit | N/A |
| `AbortDDX()` | Various | Cleanup on abort | N/A |

### Main Initialization File

```c
/* riscv_init.c */

#include "riscv.h"

extern const KdCardFuncs RiscvFuncs;
extern const KdKeyboardFuncs RiscvKeyboardFuncs;
extern const KdMouseFuncs RiscvMouseFuncs;
extern const KdOsFuncs RiscvOsFuncs;

/*
 * InitCard - Called by TinyX to register graphics card
 * YOU implement this, TinyX calls it from main.c
 */
void InitCard(char *name)
{
    KdCardInfo *card;

    /* KdCardInfoAdd is a KDrive API function */
    card = KdCardInfoAdd(&RiscvFuncs, NULL);
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
    KdInitInput(&RiscvMouseFuncs, &RiscvKeyboardFuncs);
}

/*
 * OsVendorInit - Called by TinyX to initialize OS-level services
 * YOU implement this, TinyX calls it from os/utils.c
 */
void OsVendorInit(void)
{
    /* KdOsInit is a KDrive API function */
    KdOsInit(&RiscvOsFuncs);
}

void ddxUseMsg(void)
{
    ErrorF("\nRISC-V bare-metal driver options:\n");
    ErrorF("  -fb <addr>     Framebuffer base address (hex)\n");
    ErrorF("  -fbsize WxH    Framebuffer dimensions\n");
}

int ddxProcessArgument(int argc, char **argv, int i)
{
    return 0;
}

void ddxGiveUp(void) { }
void AbortDDX(void) { }
```

---

## Build Integration

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

target_compile_definitions(kdrive_riscv PRIVATE
    RISCV_FB_BASE=0x80000000
    RISCV_FB_WIDTH=1024
    RISCV_FB_HEIGHT=768
    RISCV_FB_BPP=32
    RISCV_KBD_BASE=0x10000000
    RISCV_MOUSE_BASE=0x10001000
)

target_link_libraries(kdrive_riscv
    kdrive_core
    fb
    mi
    dix
)
```

### Tima Backend CMake

```cmake
# tinyx/kdrive/tima/CMakeLists.txt

add_library(kdrive_tima STATIC
    tima_kdrive.h
    tima_kdrive_fb.c
    tima_kdrive_kbd.c
    tima_kdrive_mouse.c
    tima_kdrive_system.c
)

target_include_directories(kdrive_tima PRIVATE
    ${CMAKE_SOURCE_DIR}/tinyx/include
    ${CMAKE_SOURCE_DIR}/tinyx/kdrive/src
    ${CMAKE_SOURCE_DIR}/framework/sdl_drivers  # HAL headers
)

target_link_libraries(kdrive_tima
    kdrive_core
    # Links to HAL implementation (sdl_drivers or riscv_drivers)
)
```

---

## Testing & Validation

### Test Checklist

#### Framebuffer Tests
- [ ] Card initializes without errors
- [ ] Screen dimensions correct
- [ ] Pixel format matches expected
- [ ] Colors display accurately
- [ ] DPMS state changes work

#### Keyboard Tests
- [ ] Keyboard initializes
- [ ] Keys generate correct keysyms
- [ ] Shift modifiers work
- [ ] Caps Lock LED toggles
- [ ] Special keys (F1-F12, arrows) work

#### Mouse Tests
- [ ] Mouse initializes
- [ ] Movement tracks correctly
- [ ] Buttons generate correct events
- [ ] Cursor stays within screen bounds

### Test Application

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

- [TINYX_MIGRATION.md](TINYX_MIGRATION.md) - TinyX overview and migration from GEM-XServer
- [WMB_API_DESIGN.md](WMB_API_DESIGN.md) - Overall WMB architecture
- [OS_LIBC_REQUIREMENTS.md](OS_LIBC_REQUIREMENTS.md) - Syscall stub requirements
- `tinyx/kdrive/fbdev/` - Reference Linux framebuffer implementation
- `tinyx/kdrive/src/kdrive.h` - Kdrive interface definitions
- `tinyx/kdrive/src/kinput.c` - Input event handling

---

## Document History

**January 23, 2026**: Created by merging HAL_SPECIFICATION.md and KDRIVE_IMPLEMENTATION_REFERENCE.md
- Eliminated redundant custom `hal_*.h` API (Kdrive IS the HAL)
- Kept platform backends section from HAL_SPECIFICATION
- Kept window management explanation from HAL_SPECIFICATION
- Kept full implementation templates from KDRIVE_IMPLEMENTATION_REFERENCE
- Added timer/syscalls integration section
- Streamlined structure for clearer guidance

**Archived documents**: See `docs/archive_docs/` for original files
