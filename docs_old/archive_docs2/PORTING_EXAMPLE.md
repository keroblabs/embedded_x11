# Example: Porting grph.c from VDI to HAL

This document shows a concrete example of porting one key file from GEM-XServer.

## Original Code Analysis

[grph.c](GEM-XServer/src/grph.c) is the core graphics module in GEM-XServer. Let's examine and port it.

### Key VDI Functions Used in grph.c

From our analysis, grph.c uses:
- `graf_handle()` - Get graphics handle (line 87)
- `v_opnvwk()` - Open virtual workstation (line 88)
- `vq_extnd()` - Query extended info (line 95)
- `vst_alignment()` - Set text alignment (line 98)
- `vsf_perimeter()` - Set fill perimeter (line 99)
- `vst_unload_fonts()` - Unload fonts (line 110)
- `v_clsvwk()` - Close workstation (line 113)
- `vq_scrninfo()` - Query screen info (line 143)

## Step-by-Step Port

### Step 1: Add HAL Includes

**Original (lines 1-30):**
```c
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mint/cookie.h>

#include "main.h"
#include "tools.h"
#include "clnt.h"
#include "grph.h"
// ... more includes
```

**Modified:**
```c
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* HAL includes - replace mint/cookie.h */
#include "hal.h"
#include "hal_framebuffer.h"
#include "hal_graphics.h"

#include "main.h"
#include "tools.h"
#include "clnt.h"
#include "grph.h"
// ... more includes (remove VDI-specific ones)
```

### Step 2: Replace Global VDI Variables

**Original (lines 36-46):**
```c
short GRPH_Handle = -1;
short GRPH_Vdi = 0;
short GRPH_ScreenW;
short GRPH_ScreenH;
short GRPH_Depth;
short GRPH_muWidth;
short GRPH_muHeight;
int GRPH_DepthNum = 1;
short GRPH_Format = SCRN_Interleaved;
short GRPH_Fonts = 0x8000;
```

**Modified:**
```c
/* HAL-based globals - no VDI handle needed */
static hal_fb_info_t fb_info;
short GRPH_ScreenW;
short GRPH_ScreenH;
short GRPH_Depth;
int GRPH_DepthNum = 1;

/* Remove: GRPH_Handle, GRPH_Vdi (VDI-specific) */
/* Remove: GRPH_muWidth, GRPH_muHeight (millimeters - not critical) */
/* Remove: GRPH_Fonts (GDOS-specific) */
```

### Step 3: Port GrphInit() Function

**Original (lines 71-102):**
```c
BOOL GrphInit(void)
{
    if (GRPH_Vdi <= 0)
    {
        short work_in[16] = { 1, SOLID, G_BLACK, MRKR_DOT, G_BLACK, 1, G_BLACK,
            FIS_SOLID, 0, 0, 2, 0, 0, 0, 0, 0
        };
        short w_out[57];
        short dmy;

        GRPH_Vdi = GRPH_Handle = graf_handle(&dmy, &dmy, &dmy, &dmy);
        v_opnvwk(work_in, &GRPH_Vdi, w_out);
        if (GRPH_Vdi <= 0)
        {
            x_printf("\033pFATAL\033q can't initialize VDI for #%i: %i!\n", 
                     GRPH_Handle, GRPH_Vdi);
            return xFalse;
        }
        GRPH_ScreenW = w_out[0];
        GRPH_ScreenH = w_out[1];
        GRPH_muWidth = w_out[3];
        GRPH_muHeight = w_out[4];
        GRPH_Fonts |= w_out[10];
        vq_extnd(GRPH_Vdi, 1, w_out);
        GRPH_Depth = w_out[4];

        vst_alignment(GRPH_Vdi, TA_LEFT, TA_BASE, w_out, w_out);
        vsf_perimeter(GRPH_Vdi, PERIMETER_OFF);
    }
    return xTrue;
}
```

**Modified (HAL version):**
```c
BOOL GrphInit(void)
{
    /* Initialize HAL framebuffer */
    if (hal_fb_init(&fb_info) != HAL_OK)
    {
        x_printf("FATAL: Can't initialize framebuffer!\n");
        return xFalse;
    }

    /* Store screen parameters for X server */
    GRPH_ScreenW = fb_info.width;
    GRPH_ScreenH = fb_info.height;
    GRPH_Depth = fb_info.bpp;
    
    /* Determine depth number (visual count) */
    if (GRPH_Depth == 1) {
        GRPH_DepthNum = 1;  /* Monochrome */
    } else {
        GRPH_DepthNum = 2;  /* Color + monochrome */
    }

    /* Note: Text alignment and fill perimeter are now handled */
    /* in software by the X server, not by hardware */

    x_printf("Graphics initialized: %dx%d @ %d bpp\n",
             GRPH_ScreenW, GRPH_ScreenH, GRPH_Depth);

    return xTrue;
}
```

### Step 4: Port GrphExit() Function

**Original (lines 104-115):**
```c
void GrphExit(void)
{
    if (GRPH_Vdi > 0)
    {
        if (GRPH_Fonts > 0)
        {
            if (vq_gdos())
                vst_unload_fonts(GRPH_Vdi, 0);
        }
        v_clsvwk(GRPH_Vdi);
    }
}
```

**Modified (HAL version):**
```c
void GrphExit(void)
{
    /* HAL cleanup - currently nothing needed */
    /* Fonts are built-in, no GDOS to unload */
    /* Framebuffer doesn't need closing */
    
    /* Could add hal_fb_shutdown() if platform needs it */
    x_printf("Graphics shutdown\n");
}
```

### Step 5: Port Screen Info Query

**Original (lines 125-165) - Simplified excerpt:**
```c
int GrphSetup(void *format_arr)
{
    // ...
    short w_out[273];
    
    vq_scrninfo(GRPH_Vdi, w_out);
    if ((w_out[14] & 2) && GRPH_Depth == 16)
    {
        GRPH_Format = w_out[14];
        /* Detect pixel format from hardware */
    }
    // ... more format detection
}
```

**Modified (HAL version):**
```c
int GrphSetup(void *format_arr)
{
    xPixmapFormat *pfrm = format_arr;
    
    /* Get framebuffer info */
    if (hal_fb_get_info(&fb_info) != HAL_OK) {
        return -1;
    }
    
    /* Set up X11 pixmap formats based on HAL format */
    switch (fb_info.format) {
        case HAL_FMT_RGB565:
            GRPH_Depth = 16;
            pfrm->depth = 16;
            pfrm->bits_per_pixel = 16;
            pfrm->scanline_pad = 32;
            break;
            
        case HAL_FMT_RGB888:
            GRPH_Depth = 24;
            pfrm->depth = 24;
            pfrm->bits_per_pixel = 24;
            pfrm->scanline_pad = 32;
            break;
            
        case HAL_FMT_ARGB8888:
            GRPH_Depth = 32;
            pfrm->depth = 24;  /* X11 depth */
            pfrm->bits_per_pixel = 32;
            pfrm->scanline_pad = 32;
            break;
            
        default:
            x_printf("Unsupported pixel format!\n");
            return -1;
    }
    
    /* Set up visual information */
    // ... rest of function
    
    return 1;  /* Number of formats */
}
```

### Step 6: Replace Drawing Functions

The grph.c file has many drawing helper functions. Here's one example:

**Drawing rectangle in another file that uses grph.c:**
```c
/* Example from wind_draw.c */

/* BEFORE (VDI): */
void wind_draw_rect(int x, int y, int w, int h, int color)
{
    short pxy[4];
    pxy[0] = x;
    pxy[1] = y;
    pxy[2] = x + w - 1;
    pxy[3] = y + h - 1;
    
    vsf_color(GRPH_Vdi, color);
    v_bar(GRPH_Vdi, pxy);
}

/* AFTER (HAL): */
void wind_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    hal_fb_fill_rect(x, y, w, h, color);
}
```

## Complete Modified grph.c Header Section

Here's how the beginning of your modified grph.c would look:

```c
/*
 *==============================================================================
 * grph.c - Graphics operations (HAL-based port from VDI)
 * 
 * Original: Copyright (C) 2000,2001 Ralph Lowinski <AltF4@freemint.de>
 * HAL Port: Copyright (C) 2025 [Your Name]
 *==============================================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* HAL includes replace VDI/GEM */
#include "hal.h"
#include "hal_framebuffer.h"
#include "hal_graphics.h"
#include "hal_color.h"

#include "main.h"
#include "tools.h"
#include "clnt.h"
#include "grph.h"
#include "colormap.h"
#include "font.h"
#include "window.h"
#include "event.h"
#include "gcontext.h"
#include "x_printf.h"

#include <X11/Xprotostr.h>

#define _CONST_DPI 72

#define DFLT_IMSK    (NoEventMask)

/* HAL-based globals */
static hal_fb_info_t fb_info;

/* X server screen parameters */
short GRPH_ScreenW;
short GRPH_ScreenH;
short GRPH_Depth;
int GRPH_DepthNum = 1;

/* Depth and visual structures (keep from original) */
static struct xDepthAndVisual __depth_msb_1 = { 
    {1, 00, 1}, 
    {DFLT_VISUAL + 0, StaticGray, 1, 2, 0, 0, 0} 
};
static struct xDepthAndVisual __depth_msb_n = { 
    {8, 00, 1}, 
    {DFLT_VISUAL + 1, StaticColor, 8, 256, 0, 0, 0} 
};

struct xDepthAndVisual *GRPH_DepthMSB[2] = { &__depth_msb_1, &__depth_msb_n };
/* ... continue with rest of file ... */
```

## Color Conversion Functions

Add helper functions for color management:

```c
/*
 * Convert X11 RGB color to HAL pixel format
 */
static uint32_t grph_rgb_to_pixel(uint16_t r, uint16_t g, uint16_t b)
{
    /* X11 uses 16-bit color components, convert to 8-bit */
    uint8_t r8 = r >> 8;
    uint8_t g8 = g >> 8;
    uint8_t b8 = b >> 8;
    
    return hal_color_rgb(r8, g8, b8);
}

/*
 * Convert pixel value back to RGB
 */
static void grph_pixel_to_rgb(uint32_t pixel, uint16_t *r, uint16_t *g, uint16_t *b)
{
    uint8_t r8, g8, b8;
    hal_color_pixel_to_rgb(pixel, &r8, &g8, &b8);
    
    /* Convert 8-bit to 16-bit for X11 */
    *r = r8 << 8;
    *g = g8 << 8;
    *b = b8 << 8;
}
```

## Testing the Port

After modifying grph.c, create a test:

```c
/* test/test_grph.c */
#include "grph.h"
#include "hal_framebuffer.h"

int main(void)
{
    /* Initialize HAL */
    hal_init(NULL);
    
    /* Initialize graphics */
    if (!GrphInit()) {
        printf("Failed to initialize graphics!\n");
        return 1;
    }
    
    printf("Graphics initialized successfully:\n");
    printf("  Screen: %dx%d\n", GRPH_ScreenW, GRPH_ScreenH);
    printf("  Depth: %d bpp\n", GRPH_Depth);
    
    /* Draw test pattern */
    hal_fb_fill_rect(0, 0, GRPH_ScreenW, GRPH_ScreenH,
                     hal_color_rgb(0, 0, 100));  /* Dark blue background */
    
    hal_fb_fill_rect(100, 100, 200, 200,
                     hal_color_rgb(255, 0, 0));  /* Red square */
    
    hal_fb_fill_rect(350, 100, 200, 200,
                     hal_color_rgb(0, 255, 0));  /* Green square */
    
    /* Cleanup */
    GrphExit();
    
    printf("Graphics test complete.\n");
    return 0;
}
```

## Build and Test

Update your Makefile:

```makefile
# Add grph.c to X server sources
XSERVER_SRCS = xserver/grph_ported.c \
               xserver/main_stub.c

# Test target
test_grph: test/test_grph.o xserver/grph_ported.o hal/*.o
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: test_grph
```

Build and run:
```bash
make test_grph
qemu-system-riscv64 -machine virt -kernel test_grph.elf
```

## Common Issues When Porting

### Issue 1: Missing Constants

**Error:**
```
error: 'SOLID' undeclared
error: 'G_BLACK' undeclared
```

**Solution:**
These are VDI constants that are no longer needed. Remove them or define as:
```c
/* VDI constants no longer used - defined for compatibility */
#define SOLID     1
#define G_BLACK   0
#define G_WHITE   1
```

### Issue 2: Coordinate System Differences

**Problem:** VDI uses inclusive coordinates, HAL uses width/height

**Example:**
```c
/* VDI: pxy[2,3] is bottom-right inclusive */
pxy[0] = 10; pxy[1] = 10;
pxy[2] = 109; pxy[3] = 109;  /* 100x100 rect */

/* HAL: width and height */
hal_fb_fill_rect(10, 10, 100, 100);  /* 100x100 rect */
```

**Solution:**
```c
/* Conversion helper */
#define VDI_WIDTH(pxy)  ((pxy)[2] - (pxy)[0] + 1)
#define VDI_HEIGHT(pxy) ((pxy)[3] - (pxy)[1] + 1)
```

### Issue 3: Handle Management

**Problem:** VDI uses handles, HAL is global

**Solution:**
```c
/* BEFORE: */
v_bar(GRPH_Vdi, pxy);  /* Handle passed */

/* AFTER: */
hal_fb_fill_rect(x, y, w, h, color);  /* No handle needed */

/* Old code checking handle: */
if (GRPH_Vdi > 0) {
    /* ... VDI call ... */
}

/* Can be replaced with: */
/* HAL is always initialized if GrphInit() succeeded */
```

## Next Files to Port

After grph.c works, port these in order:

1. **draw_img.c** - Image drawing (uses grph.c)
2. **wind_draw.c** - Window drawing (uses grph.c)
3. **pixmap.c** - Pixmap management
4. **Cursor.c** - Cursor handling
5. **event.c** - Event processing
6. **main.c** - Main event loop

Each follows the same pattern:
1. Replace VDI/AES includes with HAL includes
2. Replace function calls
3. Test individually

## Summary

Porting grph.c from VDI to HAL:

✅ **Simple changes:**
- Replace includes
- Replace VDI calls with HAL calls
- Update global variables

✅ **Benefits:**
- Much simpler code (no VDI setup complexity)
- Direct hardware access
- Easier to debug

✅ **Testing:**
- Each function can be tested independently
- Visual verification on framebuffer
- Build up functionality incrementally

The pattern shown here applies to all other GEM-XServer files that use VDI/AES.
