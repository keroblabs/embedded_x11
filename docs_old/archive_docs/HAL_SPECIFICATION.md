# Hardware Abstraction Layer (HAL) Specification for WMB

## Overview

This document defines the complete HAL interface for implementing TinyX/Kdrive. The Kdrive architecture provides a clean separation between the X server core and hardware-specific code through well-defined interface structures:

- **KdCardFuncs** - Graphics card/framebuffer operations
- **KdKeyboardFuncs** - Keyboard driver interface
- **KdPointerFuncs** - Mouse/pointer driver interface  
- **KdOsFuncs** - OS-level functions (timing, bells, etc.)

## Platform Backends

The HAL supports multiple platform backends through the same interface:

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

Benefits:
- Fast iteration (no hardware deployment)
- Full debugging with GDB/IDE
- Visual output in SDL window
- Keyboard/mouse via SDL events

### Target Deployment (RISC-V Backend)

For final deployment on RISC-V hardware:

```bash
# Build for RISC-V
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake
cmake --build build
# Deploy build/wmb.elf to hardware
```

---

All platform-specific code will be isolated behind these Kdrive interfaces. In addition, OS-level services (timing, file I/O, environment, etc.) are adapted as follows:

- **X11/Xos.h**: Redirects POSIX APIs (gettimeofday, open, read, etc.) to HAL/syscalls.c.
- **libc/syscalls.c**: Implements minimal POSIX layer for bare-metal, mapping to HAL functions.
- **TinyX/src/os/**: Event loop, timers, signals, file I/O, networking—stubbed or replaced with HAL implementations.
- **JWM/src/timing.c**: Uses time, gettimeofday, localtime, etc.—ensure these are available via syscalls/HAL.

**Effort:**
- Moderate for Xos.h and syscalls.c
- High for os/ folder (major porting/stubbing)
- Low to moderate for JWM/timing.c

See WMB_API_DESIGN.md for overall strategy.

## Kdrive Interface Overview

### Kdrive HAL Architecture

TinyX uses a clean HAL model through Kdrive. Instead of replacing scattered function calls throughout the codebase, you implement well-defined interface structures:

```c
/* From tinyx/kdrive/src/kdrive.h */

typedef struct _KdCardFuncs {
    Bool (*cardinit) (KdCardInfo *);     /* Initialize card/framebuffer */
    Bool (*screeninit) (KdScreenInfo *); /* Initialize screen */  
    void (*preserve) (KdCardInfo *);     /* Save state */
    void (*enable) (ScreenPtr);          /* Enable screen */
    Bool (*dpms) (ScreenPtr, int);       /* Power management */
    void (*disable) (ScreenPtr);         /* Disable screen */
    void (*restore) (KdCardInfo *);      /* Restore state */
    void (*scrfini) (KdScreenInfo *);    /* Cleanup screen */
    void (*cardfini) (KdCardInfo *);     /* Cleanup card */
} KdCardFuncs;

typedef struct _KdKeyboardFuncs {
    void (*Load) (void);                 /* Load keymap */
    int  (*Init) (void);                 /* Initialize keyboard */
    void (*Leds) (int);                  /* Set LED state */
    void (*Bell) (int, int, int);        /* System bell */
    void (*Fini) (void);                 /* Cleanup */
} KdKeyboardFuncs;

typedef struct _KdPointerFuncs {
    int  (*Init) (void);                 /* Initialize pointer */
    void (*Fini) (void);                 /* Cleanup */
} KdPointerFuncs;
```

**Your task**: Implement these structures for each platform backend (SDL for hosted, hardware for target).

### Comparison: Kdrive vs VDI/AES (Why This is Better)

**Old approach (GEM-XServer):**
- Scattered VDI/AES calls throughout codebase (~30+ functions)
- Each function called individually from different places
- Hard to track all dependencies

**New approach (TinyX/Kdrive):**
- Clean interface structures (~10 functions)
- All hardware access through well-defined callbacks
- Easy to see exactly what needs implementing

## VDI/AES to Kdrive Mapping

For reference, here's how old VDI/AES functions map to Kdrive:

### Graphics Operations (VDI → KdCardFuncs)
| VDI Call | Purpose | Replacement |
|----------|---------|-------------|
| `v_opnvwk()` | Open virtual workstation | `hal_gfx_init()` |
| `v_clsvwk()` | Close workstation | `hal_gfx_shutdown()` |
| `v_opnbm()` | Open bitmap | `hal_bitmap_create()` |
| `v_clsbm()` | Close bitmap | `hal_bitmap_destroy()` |
| `v_bar()` | Draw filled rectangle | `hal_gfx_fill_rect()` |
| `v_pline()` | Draw polyline | `hal_gfx_draw_lines()` |
| `v_hide_c()` | Hide cursor | `hal_cursor_hide()` |
| `v_show_c()` | Show cursor | `hal_cursor_show()` |
| `vst_alignment()` | Set text alignment | (handled in software) |
| `vsf_perimeter()` | Set fill perimeter | (handled in software) |
| `vq_extnd()` | Query capabilities | `hal_gfx_get_info()` |
| `vq_scrninfo()` | Screen information | `hal_fb_get_info()` |
| `graf_handle()` | Get graphics handle | (not needed) |
| `graf_mouse()` | Set mouse cursor | `hal_cursor_set_shape()` |

#### Window/Event Operations (AES)
| AES Call | Purpose | Why Not Needed / Replacement |
|----------|---------|-------------------------------|
| `wind_get()` | Get window rectangle from OS | **Not needed** - X server owns window tree, no external OS |
| `wind_set()` | Set window attributes in OS | **Not needed** - X server manages windows directly |
| `wind_get_first()` | Get first clipping rectangle from OS | **Not needed** - X server calculates own clip regions |
| `wind_get_next()` | Get next clipping rectangle from OS | **Not needed** - X server has complete window hierarchy |
| `evnt_multi()` | Wait for events | `hal_event_wait()` - poll hardware input |
| `appl_init()` | Initialize GEM application | `hal_init()` - initialize hardware |

**Key Architectural Difference:**

On **Atari MiNT/GEM** (original environment):
- GEM AES provided an OS-level window manager
- X server ran as an application *under* GEM
- `wind_get()`/`wind_set()` were needed to communicate with the OS window manager
- `wind_get_first()`/`wind_get_next()` queried OS for window overlap/clipping info

On **bare-metal RISC-V** (WMB):
- **No underlying OS window system exists**
- X server **IS** the window manager (owns the window tree)
- All window state in X server's internal data structures (`WindowRec`, `ScreenRec`)
- X server calculates its own clipping regions from window hierarchy
- Direct access to window positions, sizes, stacking order - no need to query external entity

**Where Window Management Happens in WMB:**

```
Window operations handled internally by X server:
├── Window creation/destruction
├── Window positioning (x, y, width, height)
├── Window stacking order (raise/lower)
├── Clipping region calculation
├── Window visibility (map/unmap)
└── Window attributes (border, background, etc.)

These are NOT delegated to HAL because they're pure software operations
on X server's internal window tree data structure.
```

**What HAL Actually Provides:**

Only the hardware-level operations that X server needs to *render* windows:
- `hal_gfx_fill_rect()` - to draw window backgrounds
- `hal_gfx_copy_rect()` - to scroll window contents  
- `hal_fb_flush()` - to update display
- `hal_input_wait()` - to receive user input events

## Window Management: X Server vs HAL Responsibilities

### What X Server Manages (No HAL Needed)

The X server maintains complete window state in memory:

```c
/* X server internal structures (from GEM-XServer) */
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

HAL only provides **rendering primitives** that X server uses to **draw** windows:

```c
/* Example: X server draws a window using HAL */
void draw_window_background(WindowPtr win) {
    /* X server knows window position from its internal tree */
    int x = win->winSize.x;
    int y = win->winSize.y;
    int w = win->winSize.width;
    int h = win->winSize.height;
    uint32_t bg_color = win->background_pixel;
    
    /* Call HAL to render to framebuffer */
    hal_gfx_fill_rect(x, y, w, h, bg_color);
}
```

**HAL is dumb rendering layer:**
- Doesn't know about windows
- Doesn't manage screen regions
- Just draws pixels where told

### Why GEM-XServer Originally Needed wind_get/wind_set

On Atari MiNT, there were **two window managers**:

```
Atari MiNT Architecture:
┌─────────────────────────────────────┐
│ GEM Desktop (OS window manager)     │
│ - Manages OS windows                │
│ - Handles window decorations        │
│ - Controls window positions         │
└─────────────┬───────────────────────┘
              │ wind_get(), wind_set()
              ▼
┌─────────────────────────────────────┐
│ GEM-XServer (X11 server)            │
│ - Had to query OS for window info   │
│ - Cooperated with GEM window manager│
└─────────────────────────────────────┘
```

**GEM-XServer had to ask the OS:**
- "Where is my window positioned?" → `wind_get(WF_CURRXYWH)`
- "What regions are visible?" → `wind_get_first()`/`wind_get_next()`
- "Make my window larger" → `wind_set(WF_CURRXYWH)`

### WMB Bare-Metal Architecture

On bare-metal, there is **one window manager** (X server):

```
WMB Bare-Metal Architecture:
┌─────────────────────────────────────┐
│ WMB X11 Server (owns everything)    │
│ - Manages all windows directly      │
│ - Owns window tree in memory        │
│ - Calculates all positions/clipping │
└─────────────┬───────────────────────┘
              │ hal_gfx_*() only
              ▼
┌─────────────────────────────────────┐
│ HAL (framebuffer hardware)          │
│ - Provides drawing primitives       │
│ - No window knowledge               │
└─────────────────────────────────────┘
```

**X server never needs to query external entity** because it IS the authority on all window information.

### Practical Example: Window Overlap/Clipping

**On Atari MiNT (why wind_get_first/next were needed):**
```c
/* GEM-XServer had to ask OS for clipping rectangles */
void update_window_region(WindowPtr win) {
    GRECT clip_rect;
    
    /* Ask OS: what parts of my window are visible? */
    wind_get_first(win->gem_handle, &clip_rect);
    while (clip_rect.valid) {
        /* Process this clipping rectangle */
        add_to_clip_region(&clip_rect);
        
        /* Ask OS: any more visible rectangles? */
        wind_get_next(win->gem_handle, &clip_rect);
    }
}
```

**On bare-metal WMB (internal calculation):**
```c
/* X server calculates clipping from its own window tree */
void compute_clip_list(WindowPtr win) {
    RegionPtr clip = &win->clipList;
    
    /* Start with window's own rectangle */
    RegionInit(clip, &win->winSize, 1);
    
    /* Subtract overlapping sibling windows from tree */
    for (WindowPtr sib = win->nextSib; sib; sib = sib->nextSib) {
        RegionSubtract(clip, clip, &sib->winSize);
    }
    
    /* No HAL calls needed - pure geometry calculation */
}
```

**Result:** X server has complete window information in memory, so no need for external queries.

## HAL Interface Definition

### 1. Initialization and Shutdown

```c
/* hal_init.h */

typedef struct {
    const char *device_name;
    void *platform_data;    // Platform-specific configuration
} hal_config_t;

/**
 * Initialize the entire HAL subsystem
 * Called once at X server startup
 */
int hal_init(hal_config_t *config);

/**
 * Shutdown HAL and release resources
 */
void hal_shutdown(void);

/**
 * Get HAL version information
 */
void hal_get_version(int *major, int *minor, int *patch);
```

### 2. Framebuffer and Graphics

```c
/* hal_framebuffer.h */

typedef enum {
    HAL_FMT_MONO,           // 1 bit per pixel
    HAL_FMT_RGB332,         // 8 bpp: RRRGGGBB
    HAL_FMT_RGB565,         // 16 bpp: RRRRRGGGGGGBBBBB
    HAL_FMT_RGB888,         // 24 bpp: RRRRRRRRGGGGGGGGBBBBBBBB
    HAL_FMT_RGBA8888,       // 32 bpp with alpha
    HAL_FMT_BGR888,         // 24 bpp: BBBBBBBBGGGGGGGGRRRRRRRR
    HAL_FMT_ARGB8888,       // 32 bpp, alpha first
} hal_pixel_format_t;

typedef struct {
    uint32_t width;         // Width in pixels
    uint32_t height;        // Height in pixels
    uint32_t bpp;           // Bits per pixel
    uint32_t pitch;         // Bytes per scanline
    void *framebuffer;      // Physical framebuffer address
    hal_pixel_format_t format;
    uint32_t red_mask;      // Bit mask for red channel
    uint32_t green_mask;    // Bit mask for green channel
    uint32_t blue_mask;     // Bit mask for blue channel
    uint32_t red_shift;     // Shift for red channel
    uint32_t green_shift;   // Shift for green channel
    uint32_t blue_shift;    // Shift for blue channel
} hal_fb_info_t;

/**
 * Initialize framebuffer
 * Returns 0 on success, negative on error
 */
int hal_fb_init(hal_fb_info_t *info);

/**
 * Get current framebuffer information
 */
int hal_fb_get_info(hal_fb_info_t *info);

/**
 * Set video mode (if supported)
 */
int hal_fb_set_mode(uint32_t width, uint32_t height, uint32_t bpp);

/**
 * Get pointer to framebuffer memory
 */
void* hal_fb_get_buffer(void);

/**
 * Flush framebuffer to screen (if buffered)
 */
void hal_fb_flush(void);

/**
 * Flush specific region
 */
void hal_fb_flush_region(int x, int y, int width, int height);
```

### 3. Graphics Primitives

```c
/* hal_graphics.h */

typedef struct {
    uint32_t color;         // Pixel color value
    uint16_t line_width;
    uint8_t line_style;     // Solid, dashed, etc.
    uint8_t fill_style;     // Solid, pattern, etc.
} hal_gfx_context_t;

/**
 * Initialize graphics subsystem
 */
int hal_gfx_init(void);

/**
 * Shutdown graphics
 */
void hal_gfx_shutdown(void);

/**
 * Draw a single pixel
 */
void hal_gfx_set_pixel(int x, int y, uint32_t color);

/**
 * Get pixel value at position
 */
uint32_t hal_gfx_get_pixel(int x, int y);

/**
 * Draw horizontal line (optimized)
 */
void hal_gfx_hline(int x, int y, int width, uint32_t color);

/**
 * Draw vertical line (optimized)
 */
void hal_gfx_vline(int x, int y, int height, uint32_t color);

/**
 * Draw line
 */
void hal_gfx_line(int x1, int y1, int x2, int y2, uint32_t color);

/**
 * Draw polyline
 */
void hal_gfx_polyline(const int *points, int npoints, uint32_t color);

/**
 * Draw rectangle outline
 */
void hal_gfx_rect(int x, int y, int width, int height, uint32_t color);

/**
 * Fill rectangle (replaces v_bar)
 */
void hal_gfx_fill_rect(int x, int y, int width, int height, uint32_t color);

/**
 * Copy rectangle (for scrolling, etc.)
 */
void hal_gfx_copy_rect(int src_x, int src_y, int dst_x, int dst_y, 
                       int width, int height);

/**
 * Blit bitmap to screen
 */
void hal_gfx_blit(const void *src, int src_x, int src_y,
                  int dst_x, int dst_y, int width, int height,
                  int src_pitch, hal_pixel_format_t src_format);

/**
 * Blit with transparency
 */
void hal_gfx_blit_transparent(const void *src, int src_x, int src_y,
                              int dst_x, int dst_y, int width, int height,
                              int src_pitch, hal_pixel_format_t src_format,
                              uint32_t transparent_color);

/**
 * Fill polygon (for complex shapes)
 */
void hal_gfx_fill_polygon(const int *points, int npoints, uint32_t color);
```

### 4. Bitmap/Pixmap Management

```c
/* hal_bitmap.h */

typedef struct hal_bitmap {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    hal_pixel_format_t format;
    void *data;
    void *platform_handle;  // Platform-specific handle
} hal_bitmap_t;

/**
 * Create bitmap in memory (replaces v_opnbm)
 */
hal_bitmap_t* hal_bitmap_create(uint32_t width, uint32_t height, 
                                hal_pixel_format_t format);

/**
 * Destroy bitmap (replaces v_clsbm)
 */
void hal_bitmap_destroy(hal_bitmap_t *bitmap);

/**
 * Copy bitmap to framebuffer
 */
void hal_bitmap_to_fb(hal_bitmap_t *bitmap, int dst_x, int dst_y);

/**
 * Copy framebuffer region to bitmap
 */
void hal_fb_to_bitmap(hal_bitmap_t *bitmap, int src_x, int src_y,
                      int width, int height);

/**
 * Copy bitmap to bitmap
 */
void hal_bitmap_copy(hal_bitmap_t *src, hal_bitmap_t *dst,
                     int src_x, int src_y, int dst_x, int dst_y,
                     int width, int height);
```

### 5. Cursor Management

```c
/* hal_cursor.h */

typedef struct {
    uint16_t width;         // Cursor width (usually 16 or 32)
    uint16_t height;        // Cursor height
    uint16_t hot_x;         // Hotspot X
    uint16_t hot_y;         // Hotspot Y
    const void *pixels;     // Pixel data
    const void *mask;       // Transparency mask
    hal_pixel_format_t format;
} hal_cursor_shape_t;

/**
 * Initialize cursor subsystem
 */
int hal_cursor_init(void);

/**
 * Show hardware cursor (replaces v_show_c)
 */
void hal_cursor_show(void);

/**
 * Hide hardware cursor (replaces v_hide_c)
 */
void hal_cursor_hide(void);

/**
 * Set cursor position
 */
void hal_cursor_set_pos(int x, int y);

/**
 * Get cursor position
 */
void hal_cursor_get_pos(int *x, int *y);

/**
 * Set cursor shape (replaces graf_mouse with custom form)
 */
void hal_cursor_set_shape(const hal_cursor_shape_t *shape);

/**
 * Use built-in cursor shape
 */
void hal_cursor_set_builtin(int cursor_id);

// Built-in cursor IDs
#define HAL_CURSOR_ARROW        0
#define HAL_CURSOR_HAND         1
#define HAL_CURSOR_CROSSHAIR    2
#define HAL_CURSOR_IBEAM        3
#define HAL_CURSOR_WAIT         4
```

### 6. Input Events

```c
/* hal_input.h */

typedef enum {
    HAL_EVENT_NONE = 0,
    HAL_EVENT_KEY_PRESS,
    HAL_EVENT_KEY_RELEASE,
    HAL_EVENT_MOUSE_MOVE,
    HAL_EVENT_MOUSE_BUTTON_PRESS,
    HAL_EVENT_MOUSE_BUTTON_RELEASE,
    HAL_EVENT_MOUSE_WHEEL,
    HAL_EVENT_TIMER,
} hal_event_type_t;

typedef struct {
    hal_event_type_t type;
    uint32_t timestamp;     // Microseconds since boot
    
    union {
        struct {
            uint32_t keycode;
            uint32_t keysym;
            uint32_t modifiers;
        } key;
        
        struct {
            int32_t x;
            int32_t y;
            int32_t dx;     // Delta for relative movement
            int32_t dy;
        } mouse_move;
        
        struct {
            uint8_t button; // 1=left, 2=middle, 3=right
            int32_t x;
            int32_t y;
        } mouse_button;
        
        struct {
            int32_t delta;  // Positive=up, negative=down
            int32_t x;
            int32_t y;
        } mouse_wheel;
        
        struct {
            uint32_t timer_id;
        } timer;
    } data;
} hal_event_t;

// Modifier key flags
#define HAL_MOD_SHIFT   (1 << 0)
#define HAL_MOD_CTRL    (1 << 1)
#define HAL_MOD_ALT     (1 << 2)
#define HAL_MOD_META    (1 << 3)
#define HAL_MOD_CAPS    (1 << 4)
#define HAL_MOD_NUM     (1 << 5)

/**
 * Initialize input subsystem
 */
int hal_input_init(void);

/**
 * Poll for next event (non-blocking)
 * Returns 1 if event available, 0 if none, <0 on error
 */
int hal_input_poll(hal_event_t *event);

/**
 * Wait for event with timeout (replaces evnt_multi)
 * timeout_ms: timeout in milliseconds, 0=no timeout, -1=infinite
 * Returns 1 if event received, 0 if timeout, <0 on error
 */
int hal_input_wait(hal_event_t *event, int timeout_ms);

/**
 * Check if specific event types are pending
 */
int hal_input_pending(uint32_t event_mask);

// Event mask flags for hal_input_pending
#define HAL_MASK_KEY        (1 << 0)
#define HAL_MASK_MOUSE      (1 << 1)
#define HAL_MASK_TIMER      (1 << 2)
```

### 7. Keyboard Management

```c
/* hal_keyboard.h */

/**
 * Initialize keyboard
 */
int hal_keyboard_init(void);

/**
 * Get current keyboard modifier state
 */
uint32_t hal_keyboard_get_modifiers(void);

/**
 * Set keyboard LED state
 */
void hal_keyboard_set_leds(uint8_t led_state);

#define HAL_LED_CAPS_LOCK   (1 << 0)
#define HAL_LED_NUM_LOCK    (1 << 1)
#define HAL_LED_SCROLL_LOCK (1 << 2)

/**
 * Get keyboard layout/mapping
 */
const char* hal_keyboard_get_layout(void);
```

### 8. Timer Services

**IMPORTANT:** Timer implementation is platform-specific and outside the scope of this project.
The HAL provides the interface that TinyX/Kdrive and JWM will use. The actual timer hardware
access (RISC-V CSR, memory-mapped timer, SoC timer, etc.) must be implemented separately
for each target platform.

**Integration with X11/JWM:** TinyX, JWM, and X11 use the standard POSIX `gettimeofday()` function.
For bare-metal systems, implement `gettimeofday()` in your **newlib syscalls layer** (syscalls.c),
which should call `hal_timer_get_us()`. This avoids modifying TinyX/JWM/X11 code.

Example syscalls.c implementation:
```c
#include <sys/time.h>
#include "hal_timer.h"

int _gettimeofday(struct timeval *tv, void *tz) {
    uint64_t us = hal_timer_get_us();
    tv->tv_sec = us / 1000000;
    tv->tv_usec = us % 1000000;
    return 0;
}
```

```c
/* hal_timer.h */

/**
 * Initialize timer subsystem
 * Platform-specific implementation required
 */
int hal_timer_init(void);

/**
 * Get current time in microseconds since boot
 * Platform-specific implementation required
 */
uint64_t hal_timer_get_us(void);

/**
 * Get current time in milliseconds since boot
 */
uint32_t hal_timer_get_ms(void);

/**
 * Busy-wait delay (microseconds)
 */
void hal_timer_delay_us(uint32_t us);

/**
 * Busy-wait delay (milliseconds)
 */
void hal_timer_delay_ms(uint32_t ms);

/**
 * Register periodic timer callback
 * Returns timer ID, or <0 on error
 */
int hal_timer_register(void (*callback)(void), uint32_t interval_us);

/**
 * Unregister timer
 */
void hal_timer_unregister(int timer_id);
```

### 9. Interrupt Management

```c
/* hal_irq.h */

typedef void (*hal_irq_handler_t)(void *context);

/**
 * Initialize interrupt controller
 */
int hal_irq_init(void);

/**
 * Enable interrupt
 */
void hal_irq_enable(int irq_num);

/**
 * Disable interrupt
 */
void hal_irq_disable(int irq_num);

/**
 * Register interrupt handler
 */
int hal_irq_register(int irq_num, hal_irq_handler_t handler, void *context);

/**
 * Unregister interrupt handler
 */
void hal_irq_unregister(int irq_num);

/**
 * Global interrupt enable
 */
void hal_irq_enable_global(void);

/**
 * Global interrupt disable
 */
void hal_irq_disable_global(void);
```

### 10. Color Management

```c
/* hal_color.h */

/**
 * Convert RGB to pixel value for current framebuffer format
 */
uint32_t hal_color_rgb_to_pixel(uint8_t r, uint8_t g, uint8_t b);

/**
 * Convert pixel value to RGB
 */
void hal_color_pixel_to_rgb(uint32_t pixel, uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * Set palette entry (for indexed color modes)
 */
void hal_color_set_palette(int index, uint8_t r, uint8_t g, uint8_t b);

/**
 * Get palette entry
 */
void hal_color_get_palette(int index, uint8_t *r, uint8_t *g, uint8_t *b);
```

### 11. Memory Management (Optional)

```c
/* hal_memory.h */

/**
 * Allocate DMA-capable memory
 */
void* hal_mem_alloc_dma(size_t size);

/**
 * Free DMA memory
 */
void hal_mem_free_dma(void *ptr);

/**
 * Get physical address of memory
 */
uintptr_t hal_mem_get_phys(void *virt);

/**
 * Cache operations
 */
void hal_mem_cache_flush(void *addr, size_t size);
void hal_mem_cache_invalidate(void *addr, size_t size);
```

## Implementation Priority

### Phase 1: Minimal Graphics (Week 1-2)
1. `hal_init()` / `hal_shutdown()`
2. `hal_fb_init()` / `hal_fb_get_info()`
3. `hal_gfx_fill_rect()`
4. `hal_gfx_copy_rect()`
5. `hal_color_rgb_to_pixel()`

### Phase 2: Input (Week 2-3)
1. `hal_input_init()`
2. `hal_input_wait()`
3. `hal_keyboard_init()`
4. `hal_timer_get_ms()`

### Phase 3: Advanced Graphics (Week 3-4)
1. `hal_gfx_line()` / `hal_gfx_polyline()`
2. `hal_gfx_blit()`
3. `hal_bitmap_create()` / `hal_bitmap_destroy()`
4. `hal_cursor_*()` functions

### Phase 4: Optimization (Week 4+)
1. Hardware acceleration (if available)
2. DMA transfers
3. Double buffering
4. Interrupt-driven input

## Platform-Specific Implementations

Each RISC-V platform will have its own implementation in:

```
hal/
├── include/
│   └── hal_*.h         (common interface)
├── common/
│   └── hal_utils.c     (shared utilities)
└── platform/
    ├── generic/        (framebuffer-only reference)
    ├── sifive/         (SiFive platforms)
    ├── kendryte/       (K210, etc.)
    └── custom/         (your custom RISC-V platform)
```

## Testing Strategy

Create test programs for each HAL component:

1. `test_framebuffer` - Draw patterns, verify output
2. `test_graphics` - Test all drawing primitives
3. `test_input` - Capture and display input events
4. `test_cursor` - Move cursor, change shapes
5. `test_performance` - Measure operations/second

## Configuration

Use compile-time configuration for optimization:

```c
/* hal_config.h */

// Hardware capabilities
#define HAL_HW_CURSOR       0   // Hardware cursor support
#define HAL_HW_BLIT         0   // Hardware blitter
#define HAL_HW_FILL         0   // Hardware fill
#define HAL_HW_LINE         0   // Hardware line drawing
#define HAL_DOUBLE_BUFFER   1   // Enable double buffering
#define HAL_DMA_ENABLE      0   // Enable DMA transfers

// Performance tuning
#define HAL_CACHE_LINE_SIZE 64
#define HAL_MAX_CLIP_RECTS  256
```

This HAL design provides clean abstraction while maintaining performance through direct hardware access where available.
