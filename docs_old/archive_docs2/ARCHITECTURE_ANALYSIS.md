# X11 Bare-Metal RISC-V Architecture Analysis

## Executive Summary


You have an excellent starting point for building a standalone X11 server + window manager for RISC-V bare metal. Key OS-specific adaptation areas:

- **X11/Xos.h**: Central header for platform abstraction. Redirect POSIX APIs to HAL/syscalls.
- **TinyX/src/os/**: Platform-dependent code for event loop, timers, signals, file I/O, networking. Stub/replace unsupported features, use HAL for input/timer.
- **libc/syscalls.c**: Minimal POSIX layer for bare-metal. Implement required functions for Xos.h and os/.
- **JWM/src/timing.c**: Uses time, gettimeofday, localtime, etc. Ensure APIs are available via syscalls.c/Xos.h.
- **Makefile/configure scripts**: Adjust macros/build rules for platform.
- **X11 headers**: Ensure compatibility with platform/HAL.

**Effort Summary:**
- Xos.h: Moderate
- os/: High
- syscalls.c: Moderate
- JWM/timing.c: Low to moderate
- Makefile/configure: Low
- X11 headers: Low

### Current Components

1. **TinyX (Kdrive)**: A lightweight X11 server designed for embedded systems
   - Full X11 protocol implementation
   - Framebuffer-optimized rendering
   - Clean Kdrive HAL interface (KdCardFuncs, KdKeyboardFuncs, etc.)
   - No OS windowing dependencies
   - **Key insight**: Designed specifically for framebuffer/embedded use

2. **JWM (Joe's Window Manager)**: Lightweight X11 window manager
   - Window manager policy (decorations, menus, etc.)
   - Requires libX11 client library
   - Uses Xlib to communicate with X server

3. **libX11**: Standard X11 client library
4. **X11 headers**: Protocol definitions

## Critical Architecture Understanding

### NEW REQUIREMENTS - WMB Architecture

**Key Design Decisions:**
1. ✅ **No Client/Server** - Single monolithic binary, no IPC/sockets
2. ✅ **WMB Entry Points** - Applications call into WM via fixed API addresses
3. ✅ **Shared WM** - JWM not embedded in apps, accessed as system service
4. ✅ **Fixed Framebuffer** - Predefined display configuration
5. ✅ **Minimal Patching** - Clean interfaces for easy X11/JWM updates

### Updated Component Relationships (WMB Model)

```
WMB (Window Manager Binary) @ Fixed Address (platform-dependent)
┌────────────────────────────────────────────────────────────┐
│                    WMB API Entry Points                    │
│  ┌──────────────────────────────────────────────────┐      │
│  │  gui_init(), gui_create_window(), gui_draw(),... │      │
│  │  (Fixed addresses - like BIOS interrupt table)   │      │
│  └───────────────────────┬──────────────────────────┘      │
│                          │                                 │
│  ┌───────────────────────▼──────────────────────────┐      │
│  │         Window Manager (JWM Core)                │      │
│  │  - Window decorations, menus, task management    │      │
│  │  - Calls X11 server directly (no IPC)            │      │
│  └───────────────────────┬──────────────────────────┘      │
│                          │                                 │
│  ┌───────────────────────▼──────────────────────────┐      │
│  │         X11 Server Core (TinyX/Kdrive)           │      │
│  │  ┌────────────────────────────────────────┐      │      │
│  │  │  Resource Management (no networking)   │      │      │
│  │  │  - Window tree, GCs, pixmaps, etc.     │      │      │
│  │  └────────────────┬───────────────────────┘      │      │
│  │  ┌───────────────┼───────────────────────┐       │      │
│  │  │   Graphics    │   Input    │  Events  │       │      │
│  │  │   (fb/)       │ (kdrive/)  │ (dix/)   │       │      │
│  │  └───────┬───────┴────────┬───┴──────────┘       │      │
│  └──────────┼────────────────┼──────────────────────┘      │
└─────────────┼────────────────┼─────────────────────────────┘
              │                │
┌─────────────▼────────────────▼──────────────────────────┐
│           Hardware Abstraction Layer (HAL)              │
│  ┌──────────────────────────────────────────────┐       │
│  │  hal_fb_*()    - Framebuffer (fixed config)  │       │
│  │  hal_input_*() - Keyboard, Mouse, Touch      │       │
│  │  hal_timer_*() - Timing services             │       │
│  │  hal_irq_*()   - Interrupt management        │       │
│  └─────────────────────┬────────────────────────┘       │
└────────────────────────┼────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│              Hardware Layer (RISC-V Platform)            │
│  Framebuffer @ 0xB0000000, Input Controllers, Timers     │
└──────────────────────────────────────────────────────────┘

Application Space (Separate binaries or RAM)
┌──────────────────────────────────────────────────────────┐
│                    Application Code                      │
│  ┌────────────────────────────────────────────────┐      │
│  │  app_main() {                                  │      │
│  │    win = gui_create_window(x, y, w, h);        │      │
│  │    gui_draw_rect(win, ...);                    │      │
│  │    event = gui_wait_event();                   │      │
│  │  }                                             │      │
│  └────────────────────┬───────────────────────────┘      │
└───────────────────────┼──────────────────────────────────┘
                        │ Direct function calls to WMB API
                        ▼
              WMB Entry Point Table @ <PLATFORM_ADDR>
```

## Simplified Architecture for WMB System

### Key Simplifications

1. **No Network Stack** - Remove all socket code
2. **No IPC** - Direct function calls only
3. **No X11 Protocol Encoding** - Internal calls use C structs, not wire protocol
4. **Shared Memory Model** - All in same address space
5. **Fixed Configuration** - No runtime display detection

### API Entry Point Design

```c
/* WMB API Table @ 0x80000000 */
typedef struct {
    void (*init)(void);
    int (*create_window)(int x, int y, int w, int h, const char *title);
    void (*destroy_window)(int win_id);
    void (*show_window)(int win_id);
    void (*draw_rect)(int win_id, int x, int y, int w, int h, uint32_t color);
    void (*draw_text)(int win_id, int x, int y, const char *text);
    int (*wait_event)(gui_event_t *event);
    void (*redraw)(int win_id);
    /* ... more API functions ... */
} gui_api_t;

extern const gui_api_t *GUI_API;  /* At fixed WMB address */
```

Applications call:
```c
GUI_API->create_window(100, 100, 640, 480, "My App");
``` (Simplified for Fixed Config)

All graphics/input operations go through the Kdrive interface:

1. **Framebuffer Access (FIXED CONFIGURATION)**
   - Implement KdCardFuncs structure
   - Direct memory-mapped framebuffer @ fixed address
   - Fixed pixel format (no runtime detection needed)
   - Fixed resolution (configured at compile time)

2. **Input Devices (SIMPLIFIED)**
   - Direct hardware access to keyboard controller
   - Direct hardware access to mouse/touch controller
   - Simple polling or interrupt-driven
   - No complex input method support

3. **Timers (HAL ABSTRACTION)**
   - Platform-specific timer implementation (outside scope)
   - HAL provides: hal_timer_get_ms(), hal_timer_get_us()
   - Simple millisecond/microsecond counter interface
   - X server and JWM use HAL timer API only

4. **Interrupts (MINIMAL)**
   - Basic PLIC setup
   - Input device interrupts only (WMB Architecture)

### ELIMINATED Dependencies (Due to WMB Design)

**Removed from TinyX:**
- ❌ **Socket library** - No networking in WMB design
- ❌ **X11 Protocol encoding/decoding** - Direct C function calls
- ❌ **Client connection handling** - No separate clients
- ❌ **INET domain sockets** - Not needed
- ❌ **Select/poll** - Simplified event handling

**Removed from JWM:**
- ❌ **libX11 (Xlib)** - Not needed, direct calls to server
- ❌ **X11 Protocol** - No wire protocol, just function calls
- ❌ **All external libraries** - Cairo, Pango, Xrender, etc.
- ❌ **Complex rendering** - Use simple drawing only

### Remaining Minimal Dependencies

#### TinyX (Simplified):
- **Kdrive interfaces** → Implement ~10 HAL functions (KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs)
- **Standard C library** → newlib (already in RISC-V toolchain)

### WMB Single Binary

#### Phase 1: Core Graphics + HAL
Focus on getting graphics working with fixed configuration:

```
Components needed:
├── Hardware Abstraction Layer (HAL)
│   ├── hal_fb_init() - Fixed framebuffer config
│   ├── hal_fb_fill_rect() - Basic drawing
│   ├── hal_fb_copy_rect() - Blitting
│   ├── hal_input_poll() - Simple input
│   └── hal_timer_get_ms() - Timing
│
├── X Server Core (SIMPLIFIED - No Networking)
│   ├── Window tree management (window.c)
│   ├── Graphics primitives (grph.c) → uses HAL
│   ├── Resource management (GC, pixmap, etc.)
│   ├── Event queue (simplified, no client dispatch)
│   └── REMOVE: clnt.c, server.c, network code
│
└── Minimal C library (newlib from toolchain)
    ├── malloc/free (with custom _sbrk)
    ├── string functions
    └── printf → serial or framebuffer debug
```

#### Phase 2: Add Window Manager API
Add JWM directly integrated (not as separate client):

```
Additional components:
├── Window Manager (JWM Core - NO libX11)
│   ├── Direct calls to X server functions
│   ├── Window decorations
│   ├── Menu system (simplified)
│   ├── Event handling
│   └── REMOVE: All Xlib calls, use internal API
│
├── WMB API Layer
│   ├── gui_api_t structure at fixed address
│   ├── Wrapper functions for applications
│   ├── Event marshalling
│   └── Window management calls
│
└── Built-in fonts
    ├── 6x13 bitmap font (compiled in)
    └── 8x16 bitmap font (compiled in)
```

#### Phase 3: Application Interface
Define clean API for applications:

```
Application Support:
├── WMB API Entry Points @ 0x80000000
│   ├── gui_init()
│   ├── gui_create_window()
│   ├── gui_draw_*() functions
│   ├── gui_wait_event()
│   └── ~20 core functions
│
└── Header file for applications
    └── gui_api.h (defines WMB API interface)
- libpng/libjpeg (image loading - optional)
- libcairo/librsvg (SVG support - optional)
- Pango (text rendering - optional)

### Strategy for Single Binary

#### Phase 1: Minimal X Server Only
Focus on getting just the X server working:

```
Components needed:
├── X11 Protocol Implementation (from TinyX/Kdrive)
│   ├── Request handlers (dix/ layer)
│   ├── Window management (window.c, wind_*.c)
│   ├── Graphics context (gcontext.c)
│   ├── Drawing primitives (grph.c, draw_img.c)
│   ├── Events (event.c)
│   ├── Resources (Atom.c, Property.c, etc.)
│   └── Client connections (clnt.c, server.c)
├── Custom HAL for RISC-V
│   ├── Framebuffer driver
│   ├── Input drivers (keyboard, mouse)
│   ├── Timer driver
│   └── Interrupt handling
├── Minimal C library (newlib-nano or custom)
│   ├── malloc/free
│   ├── string functions
│   ├── printf (for debugging)
│   └── Basic I/O
└── Network stack (optional for Unix sockets)
    └── TCP/IP for remote X clients (can be deferred)
```

#### Phase 2: Add Window Manager
Once server works, add JWM as built-in client:

```
Additional components:
├── libX11 subset (compiled in)
│   ├── Core Xlib functions
│   ├── Protocol encoding/decoding
│   └── Direct server calls (no sockets)
└── JWM window manager
    ├── Window decorations
    ├── Menu system
    ├── Task switching
    └── Root window management
```

## Hardware Abstraction Layer Design

### Proposed HAL Interface

Create a clean abstraction layer in `hal/riscv/`:

```c
// hal_framebuffer.h
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;       // bits per pixel
    uint32_t pitch;     // bytes per line
    void *base_addr;    // framebuffer base
    uint32_t format;    // pixel format (RGB565, RGB888, etc.)
} fb_info_t;

int hal_fb_init(fb_info_t *info);
void hal_fb_set_pixel(int x, int y, uint32_t color);
void hal_fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void hal_fb_copy_rect(int sx, int sy, int dx, int dy, int w, int h);
void hal_fb_blit(void *src, int sx, int sy, int dx, int dy, int w, int h);

// hal_input.h
typedef enum {
    INPUT_KEYBOARD,
    INPUT_MOUSE,
    INPUT_TOUCH
} input_type_t;

typedef struct {
    input_type_t type;
    union {
        struct { int key; int pressed; } keyboard;
        struct { int x, y, buttons; } mouse;
        struct { int x, y, pressed; } touch;
    } data;
} input_event_t;

int hal_input_init(void);
int hal_input_poll(input_event_t *event);
void hal_input_set_interrupt(void (*callback)(input_event_t *));

// hal_timer.h
uint64_t hal_timer_get_ticks(void);  // microseconds
void hal_timer_delay(uint32_t us);
void hal_timer_set_callback(void (*callback)(void), uint32_t interval_us);

// hal_irq.h
void hal_irq_enable(int irq);
void hal_irq_disable(int irq);
void hal_irq_register(int irq, void (*handler)(void));
```

## Porting Strategy

### Step 1: Extract Core X Server
1. Use TinyX/Kdrive source (already embedded-focused)
2. No OS-specific code to remove
3. Kdrive interface already clean and minimal
4. Use existing interface structures

### Step 2: Implement HAL
1. Write RISC-V-specific drivers
2. Implement HAL interface
3. Test each component independently

### Step 3: Replace Dependencies
1. Remove socket code (or implement minimal TCP/IP)
2. Replace standard library calls with newlib
3. Remove font server dependency
4. Use built-in bitmap fonts only

### Step 4: Integrate Window Manager
1. Compile libX11 subset
2. Modify JWM to use internal server connection
3. Remove external dependencies (Cairo, Pango, etc.)
4. Use simple built-in rendering

### Step 5: Build System
1. Create RISC-V cross-compilation toolchain
2. Write custom linker script for bare metal
3. Create single binary with all components
4. Include boot code and initialization

## Build Dependencies to Bundle

### Must Have (statically linked):
- **newlib** or **musl** (C standard library)
- **X11 protocol headers** (already have)
- **TinyX/Kdrive core** (already have)
- **Basic font files** (BDF or PCF format)

### Can Eliminate:
- Socket library (use direct HAL or minimal TCP/IP)
- GDOS/font server (use built-in fonts)
- Image loading libraries (use XBM/simple formats)
- Complex rendering (Cairo, Pango)

### Should Consider:
- **lwIP** - Lightweight TCP/IP stack (if network needed)
- **FatFs** - Simple FAT filesystem (for config/fonts)
- **FreeType** (minimal build for font rendering)

## File Organization Proposal

```
x11-riscv/
├── hal/
│   ├── riscv/
│   │   ├── framebuffer.c
│   │   ├── keyboard.c
│   │   ├── mouse.c
│   │   ├── timer.c
│   │   └── interrupts.c
│   └── hal.h
├── xserver/
│   ├── kdrive/         (from TinyX/Kdrive)
│   ├── dix/            (Device-Independent X)
│   ├── graphics/
│   ├── resources/
│   └── events/
├── xlib/               (minimal libX11)
│   ├── display.c
│   ├── window.c
│   ├── events.c
│   └── atoms.c
├── wm/                 (from JWM, simplified)
│   ├── client.c
│   ├── border.c
│   ├── menu.c
│   └── root.c
├── libc/               (newlib or subset)
├── fonts/              (built-in bitmap fonts)
├── boot/
│   ├── startup.S       (RISC-V boot code)
│   └── init.c
└── linker.ld           (memory layout)
```

## Next Steps Recommendations

### Immediate Actions:

1. **Create HAL specification** - Define exact Kdrive interface
2. **Study TinyX/Kdrive** - Review interface structures
3. **Implement Kdrive layer** - Implement ~10 interface functions
4. **Set up build environment** - RISC-V toolchain
5. **Implement minimal HAL** - Start with framebuffer
6. **Port X server core** - Get basic protocol working
7. **Add input handling** - Keyboard and mouse
8. **Test with simple client** - xterm or xclock
9. **Integrate window manager** - Add JWM
10. **Optimize and package** - Create final binary

### Questions to Answer:

1. **What is the exact RISC-V platform?**
   - CPU model (RV32/RV64, extensions)
   - Memory layout
   - Peripheral addresses
   - Boot mechanism

2. **What graphics hardware exists?**
   - Simple framebuffer?
   - GPU with acceleration?
   - Resolution and color depth?

3. **What input devices?**
   - PS/2 keyboard/mouse?
   - USB?
   - Custom controllers?

4. **Network requirements?**
   - Local clients only?
   - Remote X11 support needed?
   - Shared memory extension?

5. **Performance requirements?**
   - Acceptable frame rate?
   - Maximum memory usage?
   - Boot time constraints?

## Estimated Effort

- **HAL Implementation**: 2-4 weeks
- **X Server Port**: 4-6 weeks
- **Window Manager Integration**: 2-3 weeks
- **Testing & Optimization**: 2-4 weeks
- **Total**: 10-17 weeks (2.5-4 months)

This assumes one experienced developer working full-time.
