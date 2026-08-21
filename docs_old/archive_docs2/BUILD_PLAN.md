# VSCode and CMake Integration

## Overview

To streamline development and ensure reproducible builds, the project will use CMake as the build system and Visual Studio Code (VSCode) as the primary IDE. This section outlines the steps to set up, configure, and use CMake and VSCode for building the WMB binary and managing dependencies.

## Steps

1. **Install Prerequisites**
    - [ ] Install [CMake](https://cmake.org/download/)
    - [ ] Install [Ninja](https://ninja-build.org/) (optional, for faster builds)
    - [ ] Install [VSCode](https://code.visualstudio.com/)
    - [ ] Install the VSCode extensions: "CMake Tools", "C/C++", and optionally "CMake Language Support"
    - [ ] Ensure RISC-V GCC toolchain is in your PATH

2. **Project Structure**
    - Place a top-level `CMakeLists.txt` in the project root
    - Organize source, HAL, and config files into logical subdirectories

3. **CMake Configuration**
    - Define targets for WMB, TinyX, JWM, HAL, and any libraries
    - Set the cross-compilation toolchain file for RISC-V
    - Use `add_subdirectory()` for modular builds
    - Specify include directories and link dependencies

4. **VSCode Setup**
    - Open the project folder in VSCode
    - Use the CMake Tools extension to configure the project (select the RISC-V toolchain)
    - Use the "Build" and "Clean" commands from the VSCode command palette
    - Set up launch.json/tasks.json for custom build or debug tasks if needed

5. **Dependency Management**
    - Use CMake's `target_link_libraries()` to manage dependencies between modules
    - Optionally, use `FetchContent` or `ExternalProject` for third-party libraries

6. **Building**
    - Use the CMake Tools extension or run `cmake --build build` from the terminal
    - The output should be a single WMB binary (e.g., `wmb.elf`)

7. **Troubleshooting**
    - Check CMake output for missing dependencies or misconfigurations
    - Use VSCode's Problems panel for quick navigation to errors

## Example CMakeLists.txt (Skeleton)

```cmake
cmake_minimum_required(VERSION 3.16)
project(wmb_x11 C CXX ASM)

# Set toolchain for RISC-V
# set(CMAKE_TOOLCHAIN_FILE path/to/riscv-toolchain.cmake)

add_subdirectory(hal)
add_subdirectory(tinyx)
add_subdirectory(jwm)

add_executable(wmb.elf main.c)
target_link_libraries(wmb.elf PRIVATE hal tinyx jwm)
```

## Notes
- Update this section as the project structure and dependencies evolve.
- See GETTING_STARTED.md for initial project setup and HAL integration.

---

# Dependency Analysis and Build Plan

## OS/WMB Boundary

Per BASE_REQUIREMENTS #8, there is a clear separation between the **hypothetical OS** and **WMB**:

### What the OS Provides (Out of WMB Scope)

| Service | Description |
|---------|-------------|
| **C Library (libc)** | newlib or equivalent with POSIX functions |
| **Syscalls** | open, read, write, close, malloc, free, etc. |
| **Timer/Clock** | time(), gettimeofday(), usleep() |
| **Memory Management** | Heap allocation, stack setup |
| **Hardware Init** | CPU, memory controller, basic peripherals |
| **WMB Loading** | Load WMB binary to specified address |

### What WMB Provides

| Component | Description |
|-----------|-------------|
| **X11 Server** | TinyX/Kdrive with HAL interface |
| **Window Manager** | JWM (simplified) |
| **libX11-wmb** | Full Xlib implementation (inside WMB) |
| **Entry Point Table** | Fixed-address API table for applications |
| **HAL Interface** | Framebuffer, keyboard, mouse abstraction |

### What Applications Link Against

| Component | Description |
|-----------|-------------|
| **libX11-app.a** | Thin wrapper library (~15 KB) |
| | Calls WMB entry points at fixed address |
| | 100% X11 API compatible |

### Two-Block Boundary Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         APPLICATION                             │
│                    (Uses X11/Xlib API)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ libX11-app.a (wrappers only, ~15 KB per app)              │  │
│  └─────────────────────────────┬─────────────────────────────┘  │
├────────────────────────────────┼────────────────────────────────┤
│                                │ Function calls to fixed addr   │
│                            WMB │ SPACE (Fixed Address)          │
│  ┌─────────────────────────────▼─────────────────────────────┐  │
│  │               WMB Entry Point Table                       │  │
│  └─────────────────────────────┬─────────────────────────────┘  │
│  ┌─────────────────────────────▼─────────────────────────────┐  │
│  │ libX11-wmb (full Xlib)  │  JWM  │  TinyX/Kdrive           │  │
│  └─────────────────────────┴───────┴────────────┬────────────┘  │
│                                                 │               │
│                              ┌──────────────────▼────────────┐  │
│                              │  HAL Interface                │  │
│                              │  (fb, kbd, mouse)             │  │
│                              └──────────────────┬────────────┘  │
├─────────────────────────────────────────────────┼───────────────┤
│                      OS BOUNDARY                │               │
├──────────────────────────────────────────────────┼──────────────┤
│                     HYPOTHETICAL OS              │              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┴───────────┐  │
│  │    libc     │  │   Syscalls  │  │   Hardware Drivers      │  │
│  │  (newlib)   │  │ (POSIX)     │  │   (actual hw access)    │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                         HARDWARE                                │
└─────────────────────────────────────────────────────────────────┘
```

**Key Point**: The OS provides the libc and syscalls; WMB provides the X11 windowing system.

---

## Hosted Development Environment (SDL Backend)

A critical development strategy is the ability to build and run WMB **natively on a development PC** (Linux, Windows, macOS) without requiring RISC-V hardware. This is achieved by providing an **SDL-based HAL backend**.

### Two Build Targets

| Build Target | Platform | HAL Backend | libc | Use Case |
|--------------|----------|-------------|------|----------|
| **Hosted** | Linux/Windows/Mac | SDL2 | Host libc (glibc, MSVC, etc.) | Development, testing, debugging |
| **Target** | RISC-V bare-metal | Hardware drivers | newlib (from OS) | Final deployment |

### Architecture: HAL Abstraction Enables Both

```
                    ┌─────────────────────────────────────┐
                    │              WMB                    │
                    │  (X Server + JWM + libX11-wmb)      │
                    └─────────────────┬───────────────────┘
                                      │
                    ┌─────────────────▼───────────────────┐
                    │           HAL Interface             │
                    │  hal_fb_*, hal_input_*, hal_timer_* │
                    └─────────────────┬───────────────────┘
                                      │
              ┌───────────────────────┼───────────────────────┐
              │                       │                       │
┌─────────────▼─────────────┐  ┌──────▼──────┐  ┌─────────────▼─────────────┐
│   HAL SDL Backend         │  │    ...      │  │   HAL RISC-V Backend      │
│   (hal/platform/sdl/)     │  │             │  │   (hal/platform/riscv/)   │
├───────────────────────────┤  │             │  ├───────────────────────────┤
│ hal_fb_init() → SDL_Window│  │             │  │ hal_fb_init() → MMIO FB   │
│ hal_fb_flush() → SDL_Render│ │             │  │ hal_fb_flush() → DMA/nop  │
│ hal_input_poll() → SDL_Poll│ │             │  │ hal_input_poll() → GPIO   │
│ hal_timer_get() → SDL_Ticks│ │             │  │ hal_timer_get() → MTIME   │
└───────────────────────────┘  └─────────────┘  └───────────────────────────┘
        │                                                │
        ▼                                                ▼
   Host OS (Linux/Win/Mac)                      RISC-V Hardware
```

### Benefits of Hosted Development

| Benefit | Description |
|---------|-------------|
| **Fast iteration** | No hardware deployment, rebuild and run instantly |
| **Standard debugging** | Use GDB, Visual Studio, lldb on native code |
| **Easy testing** | Automated tests, CI/CD pipelines |
| **Team access** | Developers don't need RISC-V hardware |
| **Visual debugging** | See X11 output in SDL window on PC |
| **Input testing** | Use PC keyboard/mouse as input |

### HAL SDL Backend Implementation

```c
/* hal/platform/sdl/hal_sdl.c */
#include <SDL2/SDL.h>
#include "hal.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *framebuffer_texture;
static uint32_t *framebuffer;

int hal_init(void)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
    
    window = SDL_CreateWindow("WMB Development",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              HAL_SCREEN_WIDTH, HAL_SCREEN_HEIGHT,
                              SDL_WINDOW_SHOWN);
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    
    framebuffer_texture = SDL_CreateTexture(renderer,
                                            SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            HAL_SCREEN_WIDTH, HAL_SCREEN_HEIGHT);
    
    framebuffer = malloc(HAL_SCREEN_WIDTH * HAL_SCREEN_HEIGHT * 4);
    
    return 0;
}

uint32_t *hal_fb_get_buffer(void)
{
    return framebuffer;
}

void hal_fb_flush(int x, int y, int w, int h)
{
    SDL_UpdateTexture(framebuffer_texture, NULL, framebuffer,
                      HAL_SCREEN_WIDTH * sizeof(uint32_t));
    SDL_RenderCopy(renderer, framebuffer_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

int hal_input_poll(hal_input_event_t *event)
{
    SDL_Event sdl_event;
    
    if (SDL_PollEvent(&sdl_event)) {
        switch (sdl_event.type) {
            case SDL_KEYDOWN:
                event->type = HAL_EVENT_KEY_PRESS;
                event->key.keycode = sdl_to_x11_keycode(sdl_event.key.keysym.sym);
                return 1;
            case SDL_KEYUP:
                event->type = HAL_EVENT_KEY_RELEASE;
                event->key.keycode = sdl_to_x11_keycode(sdl_event.key.keysym.sym);
                return 1;
            case SDL_MOUSEMOTION:
                event->type = HAL_EVENT_MOUSE_MOVE;
                event->mouse.x = sdl_event.motion.x;
                event->mouse.y = sdl_event.motion.y;
                return 1;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                event->type = (sdl_event.type == SDL_MOUSEBUTTONDOWN) 
                              ? HAL_EVENT_MOUSE_PRESS : HAL_EVENT_MOUSE_RELEASE;
                event->mouse.button = sdl_event.button.button;
                event->mouse.x = sdl_event.button.x;
                event->mouse.y = sdl_event.button.y;
                return 1;
            case SDL_QUIT:
                event->type = HAL_EVENT_QUIT;
                return 1;
        }
    }
    return 0;  /* No event */
}

uint64_t hal_timer_get_ms(void)
{
    return SDL_GetTicks64();
}

void hal_timer_delay_ms(uint32_t ms)
{
    SDL_Delay(ms);
}
```

### Build Configuration

```cmake
# CMakeLists.txt

option(WMB_HOSTED_BUILD "Build for host development with SDL" OFF)

if(WMB_HOSTED_BUILD)
    # Hosted build - use host compiler and SDL
    find_package(SDL2 REQUIRED)
    
    set(HAL_SOURCES
        hal/platform/sdl/hal_sdl.c
        hal/platform/sdl/hal_sdl_keymap.c
    )
    
    target_link_libraries(wmb PRIVATE SDL2::SDL2)
    target_compile_definitions(wmb PRIVATE WMB_HOSTED_BUILD)
    
else()
    # Target build - cross-compile for RISC-V
    set(CMAKE_C_COMPILER riscv64-unknown-elf-gcc)
    
    set(HAL_SOURCES
        hal/platform/riscv/hal_riscv_fb.c
        hal/platform/riscv/hal_riscv_input.c
        hal/platform/riscv/hal_riscv_timer.c
    )
endif()
```

### Development Workflow

```bash
# ============================================
# HOSTED DEVELOPMENT (on your PC)
# ============================================

# Configure for hosted build
mkdir build-hosted && cd build-hosted
cmake .. -DWMB_HOSTED_BUILD=ON

# Build
cmake --build .

# Run directly on PC - see X11 in SDL window!
./wmb

# Debug with GDB
gdb ./wmb

# Run tests
ctest

# ============================================
# TARGET BUILD (for RISC-V deployment)
# ============================================

# Configure for target build
mkdir build-riscv && cd build-riscv
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/riscv-toolchain.cmake

# Build
cmake --build .

# Deploy to hardware
# (platform-specific deployment)
```

### Directory Structure Update

```
hal/
├── include/
│   └── hal.h              # Common HAL interface
├── common/
│   ├── hal_utils.c        # Shared utilities
│   └── hal_bitmap.c       # Software bitmap ops
└── platform/
    ├── sdl/               # HOSTED BUILD
    │   ├── hal_sdl.c      # SDL2 implementation
    │   ├── hal_sdl_keymap.c
    │   └── CMakeLists.txt
    └── riscv/             # TARGET BUILD
        ├── hal_riscv_fb.c
        ├── hal_riscv_input.c
        ├── hal_riscv_timer.c
        └── CMakeLists.txt
```

### What Works in Hosted Mode

| Feature | Hosted (SDL) | Notes |
|---------|--------------|-------|
| Window creation/management | ✅ | Full X11 window ops |
| Graphics primitives | ✅ | Lines, rectangles, etc. |
| Event handling | ✅ | Keyboard, mouse |
| Window manager (JWM) | ✅ | Decorations, move, resize |
| Fonts | ✅ | Built-in or FreeType |
| Multiple windows | ✅ | Full multi-window support |
| Framebuffer | ✅ | SDL texture as FB |
| Timer/delays | ✅ | SDL_GetTicks |
| File I/O (configs) | ✅ | Standard fopen/fread |

### Limitations of Hosted Mode

| Limitation | Reason |
|------------|--------|
| Hardware-specific drivers | Not applicable on PC |
| RISC-V assembly | Compiled as x86/ARM instead |
| Memory-mapped I/O | Uses SDL abstractions |
| Exact timing | SDL timing differs from MTIME |

---

## Client-Server Bypass: Two-Block Architecture

Per BASE_REQUIREMENTS #1, WMB bypasses the traditional X11 client-server model using a **two-block design** for code size efficiency.

### Traditional X11 Architecture (What We're Replacing)

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ Application │     │   libX11    │     │  X Server   │
│             │────▶│ (Xlib)      │────▶│ (separate   │
│   +400 KB   │     │  +400 KB    │     │  process)   │
└─────────────┘     └──────┬──────┘     └─────────────┘
                          │
                    Socket/IPC
                    X11 Protocol
```

- Application links full Xlib (+400 KB per app)
- libX11 encodes request into X11 wire protocol
- Protocol sent over Unix socket or TCP to X server
- **Problem**: Protocol overhead, IPC latency, Xlib duplicated in every app

### WMB Two-Block Architecture (Direct Calls + Shared Xlib)

```
APPLICATION SPACE                           WMB SPACE (Fixed Address)
┌─────────────────────────┐                ┌─────────────────────────┐
│      Application        │                │   WMB Entry Table       │
│                         │                │   (Fixed Address)       │
│  ┌───────────────────┐  │                └───────────┬─────────────┘
│  │  libX11-app.a     │  │                            │
│  │  (wrappers only)  │──┼──────────────────────────▶│
│  │  ~15 KB           │  │  Function call to         │
│  └───────────────────┘  │  fixed address            │
└─────────────────────────┘                ┌───────────▼─────────────┐
                                           │   Full Xlib (libX11-wmb)│
                                           │   ~400 KB               │
                                           ├─────────────────────────┤
                                           │   X Server (TinyX)      │
                                           ├─────────────────────────┤
                                           │   JWM                   │
                                           ├─────────────────────────┤
                                           │   HAL                   │
                                           └─────────────────────────┘
```

### How It Works

1. **Application** calls `XCreateWindow()` (same API!)
2. **libX11-app wrapper** forwards call to WMB entry table at fixed address
3. **WMB entry table** routes to full Xlib implementation inside WMB
4. **libX11-wmb** calls X server's internal `CreateWindow()` function directly
5. **No protocol encoding**, no sockets, no IPC

### Size Benefits

| Scenario | Traditional (Xlib per app) | Two-Block (Wrappers only) |
|----------|---------------------------|---------------------------|
| WMB binary | ~800 KB | ~1.2 MB (includes full Xlib) |
| Per-app addition | +400 KB | +15 KB |
| 5 applications | 800 + 5×400 = **2.8 MB** | 1200 + 5×15 = **1.275 MB** |
| 10 applications | 800 + 10×400 = **4.8 MB** | 1200 + 10×15 = **1.35 MB** |

### Two Blocks Explained

#### Block 1: Application Wrappers (libX11-app.a)
- **Purpose**: Thin wrapper library linked into applications
- **Size**: ~10-20 KB per application
- **Content**: ~200 wrapper functions that call WMB entry points
- **Location**: Linked into each application binary

```c
/* libX11-app/wrappers.c - Application side */
#define WMB_API ((wmb_entry_t*)0x80100000)  /* Fixed address */

Display* XOpenDisplay(const char* name) {
    return WMB_API->XOpenDisplay(name);  /* Forward to WMB */
}

Window XCreateSimpleWindow(Display* dpy, ...) {
    return WMB_API->XCreateSimpleWindow(dpy, ...);  /* Forward to WMB */
}
```

#### Block 2: Full Xlib in WMB (libX11-wmb + Entry Table)
- **Purpose**: Complete Xlib implementation + entry point table
- **Size**: ~400 KB (part of WMB binary)
- **Content**: Full libX11 with transport layer replaced for direct calls
- **Location**: Inside WMB binary at fixed memory address

```c
/* wmb_entry.c - Inside WMB */
__attribute__((section(".wmb_api")))
wmb_entry_t wmb_entry_table = {
    .XOpenDisplay = XOpenDisplay,
    .XCreateSimpleWindow = XCreateSimpleWindow,
    /* ... all Xlib functions ... */
};
```

### What This Means for Developers

| Aspect | Standard X11 | WMB Two-Block |
|--------|-------------|---------------|
| API | `XCreateWindow()`, `XMapWindow()`, etc. | **Same functions** |
| Headers | `#include <X11/Xlib.h>` | **Same headers** |
| Compile | `gcc app.c -lX11` | `riscv-gcc app.c -lX11-app` |
| Link | Shared library (libX11.so) | Thin wrappers (libX11-app.a) |
| App size | +400 KB | **+15 KB** |
| Display | `XOpenDisplay(":0")` | `XOpenDisplay(NULL)` |

**Source code is 100% compatible** - same X11 code works on both platforms.

### Detailed Implementation Guide

For complete implementation details including code templates, see:
- **[CUSTOM_XLIB.md](CUSTOM_XLIB.md)** - Two-block architecture details
- **[WMB_API_DESIGN.md](WMB_API_DESIGN.md)** - Full API specification

---

## Complete Dependency Tree


### TinyX/Kdrive Dependencies

#### System Libraries and OS Boundary

**Important**: Per BASE_REQUIREMENTS #8, the WMB assumes a hypothetical OS provides:
- A POSIX-compatible C library (newlib or similar)
- Standard syscall implementations (open, read, write, malloc, etc.)
- Basic timer/clock services

The WMB **links against** these services but does **not implement** them.
Syscall implementation is the responsibility of the underlying OS, not WMB.

```
TinyX/Kdrive
├── Standard C Library (PROVIDED BY OS)
│   ├── fopen, fread, fwrite, fclose → OS libc
│   ├── malloc/free → OS libc heap
│   ├── time(), gettimeofday() → OS libc/HAL
│   └── Signal handling → OS libc
│
├── X11/Xos.h → Compatibility header (may need minor adaptation)
│
├── Network → NOT NEEDED (per BASE_REQUIREMENTS #1: no client/server)
│   └── All socket/network code removed from WMB
│
├── Standard C Library (PROVIDED BY OS - not WMB scope)
│   ├── malloc/free → OS libc
│   ├── string.h, stdio.h, stdlib.h → OS libc
│   ├── time.h → OS libc (backed by HAL timer)
│   └── math.h → OS libc (if needed)
│
├── Graphics (Kdrive Interface) → Implement structures
│   ├── KdCardFuncs → ~5 functions for display
│   ├── KdKeyboardFuncs → ~3 functions for keyboard
│   ├── KdPointerFuncs → ~3 functions for mouse
│   └── Input polling → KdEnqueueKeyboardEvent/PointerEvent
│
└── Fonts (GDOS) → BUNDLE
    ├── Font loading → built-in or FreeType
    ├── Font rendering → software or FreeType
    └── Font metrics → BDF/PCF parsers
```

#### Platform-Specific Build/Configure Changes
- Adjust Makefile/configure scripts for platform macros and build rules
- Ensure syscalls.c and Xos.h are customized for HAL
- Stub/replace unsupported features in TinyX/src/os/

#### Source File Dependencies

```
main.c
├── stdlib.h, stdio.h, string.h → OS libc
├── time.h → OS libc (backed by HAL timer)
├── dirent.h → OS libc (for config files)
├── fcntl.h, unistd.h → OS libc
├── signal.h → OS libc (or stub)
├── mint/ssystem.h → REMOVE
└── Local headers (keep)

server.c → REMOVED (no client/server model)
└── X11 protocol handling moved to direct function calls

clnt.c → REMOVED (no client/server model)
└── Direct API calls replace client connection management

kdrive/
├── Kdrive interface → implement KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs
├── Screen setup → framebuffer config
└── Input handling → event queue

window.c
├── GEM window calls → remove (X does its own windows)
└── Window management → keep (X11 protocol)

font.c
├── GDOS font calls → replace with built-in
├── FreeType → static link (required)
└── BDF/PCF parsers → include source

event.c
├── evnt_multi → hal_input_wait
└── Event queue → keep

Cursor.c, Pointer.c
├── graf_mouse → hal_cursor_*
└── Cursor bitmaps → keep

keyboard.c
├── Keyboard handling → hal_keyboard_*
└── Keysym mapping → keep

draw_img.c, pixmap.c
├── VDI bitmap ops → hal_bitmap_*
└── Image manipulation → keep

wmgr.c (window manager module)
├── Built-in WM → could keep or replace with JWM
└── Window decorations → keep or replace
```

### JWM Dependencies

#### External Libraries
```
JWM
├── libX11 (Xlib) → BUILD SUBSET
│   ├── XOpenDisplay → custom (direct server)
│   ├── XCreateWindow → protocol encoding
│   ├── XMapWindow → protocol encoding
│   ├── XNextEvent → custom event loop
│   ├── XDrawString → protocol encoding
│   └── ~200 more functions → minimize to ~50
│
├── libXext → OPTIONAL/REMOVE
│   ├── Shape extension → remove
│   └── DPMS → remove
│
├── libXrender → REMOVE
│   └── Compositing → too complex for bare metal
│
├── libXmu → REMOVE
│   └── Rounded corners → software implementation
│
├── libXinerama → REMOVE
│   └── Multi-head → not needed initially
│
├── libXpm → OPTIONAL/SIMPLIFY
│   └── XPM image format → simple parser or remove
│
├── libpng, libjpeg → REMOVE
│   └── Use simpler formats (XBM, XPM)
│
└── libcairo, librsvg, Pango → REMOVE
    └── Complex rendering → basic X11 drawing only
```

#### JWM Source File Dependencies

```
main.c
├── X11/Xlib.h → subset
├── Event loop → adapt to direct server
└── Configuration → simplify

client.c
├── Window management → keep
├── ICCCM/EWMH → subset (minimize)
└── Client properties → basic only

border.c, tray.c, menu.c
├── XDrawRectangle, XFillRectangle → keep
├── XDrawString → basic text
└── Complex rendering → simplify

parse.c
├── XML parsing → optional (use simple config)
└── Configuration → hard-coded defaults

icon.c, image.c
├── libpng, libjpeg → remove
└── XPM or XBM only → simple parser

font.c
├── Pango → remove
├── Xft → remove
└── Core X11 fonts → keep
```

### libX11 Subset Needed

Instead of full libX11, create minimal version:

```
libX11-minimal/
├── Display management
│   ├── XOpenDisplay → connect to local server
│   ├── XCloseDisplay
│   └── Internal connection handling
│
├── Window operations
│   ├── XCreateWindow
│   ├── XDestroyWindow
│   ├── XMapWindow / XUnmapWindow
│   ├── XMoveWindow / XResizeWindow
│   ├── XRaiseWindow / XLowerWindow
│   └── XConfigureWindow
│
├── Graphics operations
│   ├── XCreateGC / XFreeGC
│   ├── XChangeGC
│   ├── XDrawPoint / XDrawPoints
│   ├── XDrawLine / XDrawLines
│   ├── XDrawRectangle / XFillRectangle
│   ├── XDrawArc / XFillArc
│   ├── XDrawString / XDrawImageString
│   └── XCopyArea
│
├── Event handling
│   ├── XNextEvent
│   ├── XPending
│   ├── XSelectInput
│   └── XSendEvent
│
├── Property/Atom operations
│   ├── XInternAtom
│   ├── XGetAtomName
│   ├── XChangeProperty
│   ├── XGetWindowProperty
│   └── XDeleteProperty
│
├── Utility functions
│   ├── XSync / XFlush
│   ├── XSetErrorHandler
│   └── XGetWindowAttributes
│
└── Total: ~80 functions instead of 600+
```

## Build Strategy

### Development Approach: Hosted-First

**Recommendation**: Develop primarily on hosted (SDL) build, then validate on RISC-V target.

```
Development Workflow:
┌─────────────────────────────────────────────────────────────────┐
│  1. HOSTED BUILD (SDL)                                          │
│     - Fast iteration (compile + run in seconds)                 │
│     - Full debugging with GDB/IDE                               │
│     - All features testable                                     │
│     - Automated testing possible                                │
└──────────────────────────┬──────────────────────────────────────┘
                           │ When feature complete
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│  2. TARGET BUILD (RISC-V)                                       │
│     - Cross-compile                                             │
│     - Deploy to hardware/QEMU                                   │
│     - Validate HAL integration                                  │
│     - Performance testing                                       │
└─────────────────────────────────────────────────────────────────┘
```

### Stage 1: Core WMB (X Server + HAL)

**Goal**: Get basic X server running on framebuffer with keyboard/mouse

**Start with Hosted Build (SDL)**:
```bash
cmake -B build-hosted -DWMB_HOSTED_BUILD=ON
cmake --build build-hosted
./build-hosted/wmb    # See X server in SDL window!
```

```
Components:
├── HAL implementation
│   ├── SDL backend (for hosted development)
│   └── RISC-V backend (for target deployment)
├── TinyX/Kdrive core
│   ├── Implement Kdrive interface (~10 functions)
│   └── Use built-in fonts only
├── C library
│   ├── Host libc (glibc/MSVC) for hosted build
│   └── OS-provided newlib for target build
└── Simple test client (built-in)
    └── Draw window, respond to events

Dependencies:
├── Hosted: SDL2
└── Target: None (fully self-contained)

Binary size estimate: 200-300 KB (target)
```

### Stage 2: Add Basic Window Manager

**Goal**: Add window decorations and management

```
Additional components:
├── libX11-minimal (~80 functions)
├── JWM core (simplified)
│   ├── Remove all external libs
│   ├── Hard-coded config
│   └── Basic rendering only
└── Connect JWM to X server via internal API

New dependencies:
└── Still none (all static)

Binary size estimate: 400-500 KB
```

### Stage 3: Full Feature Set

**Goal**: Complete WMB with scalable fonts and configuration

```
Additional components:
├── FreeType (minimal build) → INCLUDED (integral to WMB)
├── Simple image loaders (XPM, XBM)
└── Configuration file support

Dependencies:
├── FreeType source (~200 KB compiled)
└── Minimal file I/O (from OS libc)

Binary size estimate: 700 KB - 1 MB
```

---

## Font System Architecture

WMB provides a comprehensive font system with **two font backends** that work together:

### Overview: Two Font Types

| Font Type | Format | Size | Quality | Use Case |
|-----------|--------|------|---------|----------|
| **Built-in Bitmap** | Compiled C arrays | Fixed sizes | Pixel-perfect | System UI, fallback |
| **FreeType Scalable** | TTF/OTF files | Any size | Anti-aliased | Application text |

**Both are included in WMB** - built-in for guaranteed availability, FreeType for quality.

### Built-in Bitmap Fonts

Built-in fonts are **compiled directly into the WMB binary** as C arrays. No file I/O required.

#### What Are BDF/PCF Fonts?

- **BDF** (Bitmap Distribution Format): Human-readable bitmap font format
- **PCF** (Portable Compiled Format): Binary compiled version of BDF

These are traditional X11 bitmap fonts - each glyph is a small bitmap image.

```
Example: Letter 'A' in 6x13 font (actual bitmap data)
......    <- Row 0
..##..    <- Row 1  
.#..#.    <- Row 2
.#..#.    <- Row 3
.####.    <- Row 4
.#..#.    <- Row 5
.#..#.    <- Row 6
......    <- Row 7
```

#### Built-in Font Format (Compiled C)

BDF fonts are converted to C arrays at build time:

```c
/* fonts/builtin/fixed_6x13.c - Auto-generated from fixed BDF font */

#include "builtin_font.h"

/* Font metadata */
static const FontInfo fixed_6x13_info = {
    .name = "fixed",
    .width = 6,
    .height = 13,
    .ascent = 11,
    .descent = 2,
    .first_char = 0x20,   /* Space */
    .last_char = 0x7E,    /* Tilde */
    .num_glyphs = 95,
};

/* Glyph bitmaps - 2 bytes per row × 13 rows × 95 glyphs */
static const uint8_t fixed_6x13_glyphs[] = {
    /* ' ' (0x20) - Space */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    
    /* '!' (0x21) - Exclamation */
    0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x00, 0x10, 0x10, 0x00, 0x00,
    
    /* 'A' (0x41) */
    0x00, 0x00, 0x18, 0x24, 0x24, 0x42, 0x42, 0x7E,
    0x42, 0x42, 0x42, 0x00, 0x00,
    
    /* ... remaining 92 glyphs ... */
};

/* Width table (for proportional fonts, all same for fixed) */
static const uint8_t fixed_6x13_widths[] = {
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, /* ... 95 entries ... */
};

/* Export font structure */
const BuiltinFont builtin_font_fixed_6x13 = {
    .info = &fixed_6x13_info,
    .glyphs = fixed_6x13_glyphs,
    .widths = fixed_6x13_widths,
};
```

#### Standard Built-in Fonts Included

| Font Name | Size | Characters | Purpose | Size in Binary |
|-----------|------|------------|---------|----------------|
| `fixed-6x13` | 6×13 | ASCII (95) | Default system font | ~2 KB |
| `fixed-8x16` | 8×16 | ASCII (95) | Larger UI text | ~3 KB |
| `fixed-10x20` | 10×20 | ASCII (95) | Headings | ~5 KB |
| `cursor` | 16×16 | 77 shapes | Mouse cursors | ~3 KB |
| `misc-fixed` | Various | Extended Latin | International | ~10 KB |
| **Total** | | | | **~23 KB** |

#### Built-in Font API

```c
/* fonts/builtin/builtin_font.h */

typedef struct {
    const char *name;
    int width, height;
    int ascent, descent;
    int first_char, last_char;
    int num_glyphs;
} FontInfo;

typedef struct {
    const FontInfo *info;
    const uint8_t *glyphs;
    const uint8_t *widths;
} BuiltinFont;

/* Available built-in fonts */
extern const BuiltinFont builtin_font_fixed_6x13;
extern const BuiltinFont builtin_font_fixed_8x16;
extern const BuiltinFont builtin_font_fixed_10x20;
extern const BuiltinFont builtin_font_cursor;

/* Font rendering functions */
const BuiltinFont* builtin_font_find(const char *name, int size);
void builtin_font_render_char(const BuiltinFont *font, uint32_t *fb, 
                               int fb_stride, int x, int y, 
                               char c, uint32_t color);
void builtin_font_render_string(const BuiltinFont *font, uint32_t *fb,
                                 int fb_stride, int x, int y,
                                 const char *str, uint32_t color);
int builtin_font_text_width(const BuiltinFont *font, const char *str);
```

#### Font Conversion Tool

```bash
# Convert BDF font to C array at build time
tools/font_converter/bdf2c fixed.bdf > fonts/builtin/fixed_6x13.c

# The tool generates:
# - Glyph bitmap arrays
# - Width tables
# - Font metadata structure
```

### FreeType 2.14.1 Scalable Fonts (Integral to WMB)

FreeType 2.14.1 is **included in the workspace** (`fonts/freetype/`) as an integral part of WMB for high-quality text rendering.

> ✅ **Status**: FreeType source is present in workspace at `fonts/freetype/`

#### Why FreeType is Essential (Not Optional)

| Feature | Built-in Bitmap | FreeType |
|---------|-----------------|----------|
| **Scalability** | Fixed sizes only | Any point size |
| **Anti-aliasing** | ❌ Jagged edges | ✅ Smooth edges |
| **Unicode** | Limited (ASCII/Latin) | Full Unicode support |
| **Font formats** | BDF/PCF only | TTF, OTF, WOFF |
| **Hinting** | ❌ None | ✅ Subpixel precision |
| **Modern apps** | Basic UI only | Required for real apps |

**Conclusion**: FreeType is required for any serious application. Built-in fonts are fallback only.

#### FreeType Integration in WMB

```
┌─────────────────────────────────────────────────────────────┐
│                      X11 Font Request                       │
│              XLoadQueryFont("DejaVuSans-12")                │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    WMB Font Router                          │
│  1. Check built-in fonts first (fixed, cursor, etc.)        │
│  2. Fall back to FreeType for scalable fonts                │
└───────────┬─────────────────────────────────┬───────────────┘
            │                                 │
            ▼                                 ▼
┌───────────────────────┐       ┌─────────────────────────────┐
│   Built-in Backend    │       │     FreeType Backend        │
│   (fixed-6x13, etc.)  │       │     (TTF/OTF files)         │
│   ~23 KB in binary    │       │     ~200 KB code            │
└───────────────────────┘       └─────────────────────────────┘
```

#### FreeType Minimal Build Configuration

FreeType is built with minimal modules to reduce size:

```cmake
# cmake/freetype.cmake - Minimal FreeType build

set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)

# Enable only essential modules
set(FT_WITH_ZLIB OFF)       # No compressed fonts
set(FT_WITH_PNG OFF)        # No PNG glyphs
set(FT_WITH_BZIP2 OFF)      # No BZ2 compression

# Core modules only
# - TrueType (TTF)
# - OpenType/CFF (OTF)
# - Rasterizer
# - Glyph cache
```

**Minimal FreeType size**: ~150-200 KB compiled

#### FreeType Wrapper API

```c
/* fonts/freetype/ft_wrapper.h */

#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct {
    FT_Library library;
    FT_Face face;
    int size;
    int ascent;
    int descent;
} FTFont;

/* Initialize FreeType subsystem */
int ft_init(void);
void ft_shutdown(void);

/* Load font from file or memory */
FTFont* ft_font_load(const char *path, int size);
FTFont* ft_font_load_memory(const uint8_t *data, size_t len, int size);
void ft_font_free(FTFont *font);

/* Set font size */
int ft_font_set_size(FTFont *font, int size);

/* Render glyph to bitmap */
int ft_render_char(FTFont *font, uint32_t codepoint, 
                   uint8_t *bitmap, int *width, int *height,
                   int *bearing_x, int *bearing_y, int *advance);

/* Render string directly to framebuffer */
void ft_render_string(FTFont *font, uint32_t *fb, int fb_stride,
                      int x, int y, const char *str, uint32_t color);

/* Text metrics */
int ft_text_width(FTFont *font, const char *str);
int ft_font_height(FTFont *font);
```

#### Bundled Fonts

WMB includes essential fonts in the binary (or loads from filesystem):

| Font | License | Purpose | File Size |
|------|---------|---------|-----------|
| **DejaVu Sans** | Public Domain | UI, general text | ~700 KB |
| **DejaVu Sans Mono** | Public Domain | Code, terminal | ~350 KB |
| **Liberation Sans** | SIL OFL | Arial replacement | ~400 KB |
| **Noto Sans** (subset) | SIL OFL | Unicode coverage | ~500 KB |

**Option 1**: Bundle fonts as C arrays (like built-in, but larger)
**Option 2**: Load from filesystem at runtime (reduces binary, needs file I/O)

```c
/* Option 1: Bundled as C array */
extern const uint8_t font_dejavu_sans_ttf[];
extern const size_t font_dejavu_sans_ttf_size;

FTFont *font = ft_font_load_memory(font_dejavu_sans_ttf, 
                                    font_dejavu_sans_ttf_size, 12);

/* Option 2: Load from filesystem */
FTFont *font = ft_font_load("/usr/share/fonts/dejavu/DejaVuSans.ttf", 12);
```

### X11 Font Integration

The X server presents a unified font interface to applications:

```c
/* xserver/resources/font.c - Unified font handling */

FontPtr FontOpen(const char *name, int size)
{
    FontPtr font;
    
    /* Try built-in fonts first */
    if (strncmp(name, "fixed", 5) == 0 || 
        strncmp(name, "cursor", 6) == 0) {
        font = BuiltinFontOpen(name, size);
        if (font) return font;
    }
    
    /* Fall back to FreeType */
    font = FreeTypeFontOpen(name, size);
    if (font) return font;
    
    /* Ultimate fallback: fixed-6x13 */
    return BuiltinFontOpen("fixed", 6);
}
```

### Font Directory Structure

```
fonts/                          # IN WORKSPACE
├── freetype/                   # FreeType 2.14.1 ✅ PRESENT
│   ├── CMakeLists.txt         # FreeType CMake build
│   ├── Makefile               # FreeType Make build
│   ├── meson.build            # FreeType Meson build
│   ├── include/               # Public headers (ft2build.h, freetype/*.h)
│   ├── src/                   # Source code (~250 files)
│   ├── builds/                # Platform-specific configs
│   ├── docs/                  # Documentation
│   └── LICENSE.TXT            # FreeType License (FTL)
│
├── builtin/                    # TO BE CREATED - Built-in bitmap fonts
│   ├── builtin_font.h         # API header
│   ├── fixed_6x13.c           # Generated from BDF
│   ├── fixed_8x16.c
│   ├── cursor.c               # Cursor glyphs
│   └── fonts_init.c           # Registration
│
├── wrapper/                    # TO BE CREATED - WMB integration
│   ├── ft_wrapper.h           # FreeType wrapper API
│   ├── ft_wrapper.c           # Implementation
│   └── ft_cache.c             # Glyph caching
│
├── bundled/                    # TO BE CREATED - Optional TTF embedding
│   ├── DejaVuSans.ttf
│   └── fonts.c                # Generated C arrays
│
├── bdf/                        # TO BE CREATED - BDF source files
│   ├── fixed-6x13.bdf
│   └── cursor.bdf
│
└── tools/                      # TO BE CREATED - Build tools
    └── bdf2c.py               # BDF to C converter
```

### Font Size Estimates

| Component | Binary Size |
|-----------|-------------|
| Built-in fonts (ASCII) | ~25 KB |
| FreeType library (minimal) | ~180 KB |
| Bundled TTF fonts (optional) | ~500 KB - 2 MB |
| **Total (without bundled TTF)** | **~205 KB** |
| **Total (with DejaVu Sans)** | **~900 KB** |

---

## Source Code Organization

```
x11-wmb/
│
├── cmake/                         # CMake configuration
│   ├── riscv-toolchain.cmake     # RISC-V cross-compilation toolchain
│   └── FindSDL2.cmake            # SDL2 find module (if needed)
│
├── hal/                           # Hardware Abstraction Layer
│   ├── include/
│   │   ├── hal.h                 # Main HAL header (platform-independent)
│   │   ├── hal_framebuffer.h
│   │   ├── hal_graphics.h
│   │   ├── hal_input.h
│   │   ├── hal_timer.h
│   │   └── hal_types.h
│   ├── common/
│   │   ├── hal_init.c            # Common initialization
│   │   ├── hal_utils.c           # Utilities (color conversion, etc.)
│   │   └── hal_bitmap.c          # Software bitmap operations
│   └── platform/
│       ├── sdl/                  # HOSTED BUILD (SDL backend)
│       │   ├── hal_sdl.c         # SDL2 implementation
│       │   ├── hal_sdl_keymap.c  # SDL to X11 keycode mapping
│       │   └── CMakeLists.txt
│       ├── riscv/                # TARGET BUILD (RISC-V bare-metal)
│       │   ├── hal_riscv_fb.c    # Framebuffer via MMIO
│       │   ├── hal_riscv_input.c # Input via GPIO/UART
│       │   ├── hal_riscv_timer.c # Timer via MTIME
│       │   └── CMakeLists.txt
│       └── [other-platform]/     # Other platform implementations
│           └── ...
│
├── xserver/                       # X11 Server (TinyX/Kdrive)
│   ├── kdrive/                    # Kdrive interface
│   │   ├── wmb_screen.c          # KdCardFuncs implementation
│   │   ├── server.c              # Connection handling
│   │   ├── clnt.c                # Client management
│   │   └── event.c               # Event distribution
│   ├── protocol/
│   │   ├── Request.c             # Protocol request handling
│   │   ├── Atom.c
│   │   ├── Property.c
│   │   └── selection.c
│   ├── graphics/
│   │   ├── grph.c                # Graphics primitives (HAL-adapted)
│   │   ├── drawable.c
│   │   ├── gcontext.c            # Graphics contexts
│   │   ├── pixmap.c              # Pixmaps
│   │   └── draw_img.c            # Image drawing
│   ├── resources/
│   │   ├── window.c              # Window management
│   │   ├── Cursor.c              # Cursor handling
│   │   ├── font.c                # Font management
│   │   ├── colormap.c            # Colormap management
│   │   └── Pointer.c             # Pointer handling
│   └── include/
│       └── *.h                   # Server headers
│
├── xlib/                          # Minimal libX11 implementation
│   ├── Display.c                 # Display connection
│   ├── Window.c                  # Window operations
│   ├── Graphics.c                # Drawing operations
│   ├── Event.c                   # Event handling
│   ├── Atom.c                    # Atom operations
│   ├── Property.c                # Property operations
│   └── internal/
│       ├── protocol.c            # Protocol encoding/decoding
│       └── transport.c           # Local server connection
│
├── wm/                            # Window Manager (JWM simplified)
│   ├── main.c                    # WM main loop
│   ├── client.c                  # Client window management
│   ├── border.c                  # Window decorations
│   ├── menu.c                    # Menus (simplified)
│   ├── tray.c                    # Task bar (optional)
│   ├── event.c                   # WM event handling
│   ├── config.c                  # Configuration (hard-coded)
│   └── render.c                  # Basic rendering
│
├── fonts/                         # Font support (INTEGRAL)
│   ├── builtin/                  # TO BE CREATED
│   │   ├── builtin_font.h        # Built-in font API
│   │   ├── fixed_6x13.c          # Built-in bitmap font (generated)
│   │   ├── fixed_8x16.c          # Larger bitmap font
│   │   ├── cursor.c              # Cursor font
│   │   └── fonts_init.c          # Font registration
│   ├── freetype/                 # FreeType 2.14.1 ✅ IN WORKSPACE
│   │   ├── CMakeLists.txt        # FreeType build system
│   │   ├── include/              # FreeType public headers
│   │   ├── src/                  # FreeType source code
│   │   ├── builds/               # Platform build configs
│   │   └── docs/                 # FreeType documentation
│   ├── bdf/
│   │   ├── fixed-6x13.bdf        # Source BDF files
│   │   └── cursor.bdf
│   └── tools/
│       └── bdf2c.py              # BDF to C converter
│
├── libc/                          # C Library interface (NOT syscall implementation)
│   ├── libc_stubs.c              # Stubs for any missing libc functions
│   └── README.md                 # Documents OS libc requirements
│
├── common/                        # Shared utilities
│   ├── list.c                    # Linked list
│   ├── hashtable.c               # Hash table
│   ├── bitmap.c                  # Bitmap utilities
│   └── debug.c                   # Debug output
│
├── boot/                          # WMB initialization (NOT OS boot)
│   ├── wmb_entry.c               # WMB entry point (called by OS)
│   └── wmb_init.c                # WMB subsystem initialization
│
├── config/                        # Configuration
│   ├── config.h                  # Build-time configuration
│   ├── platform.h                # Platform-specific settings
│   └── defaults.h                # Default values
│
├── tools/                         # Build tools
│   ├── font_converter/           # Convert fonts to built-in format
│   └── config_gen/               # Generate configuration
│
├── test/                          # Test programs
│   ├── test_hal.c                # HAL tests
│   ├── test_xserver.c            # X server tests
│   └── xclock.c                  # Simple X client for testing
│
└── Build files
    ├── Makefile                  # Top-level Makefile
    ├── Makefile.config           # Configuration options
    ├── linker.ld                 # Linker script
    └── toolchain.mk              # Toolchain definitions
```

## Build Process

### 1. Prerequisites

#### For Hosted Development (Recommended First)
```bash
# Linux (Ubuntu/Debian)
sudo apt-get install cmake ninja-build libsdl2-dev

# Windows (with vcpkg)
vcpkg install sdl2

# macOS
brew install cmake ninja sdl2
```

#### For Target Build (RISC-V)
```bash
# Install RISC-V GCC toolchain
sudo apt-get install gcc-riscv64-unknown-elf

# Or build from source
git clone https://github.com/riscv/riscv-gnu-toolchain
cd riscv-gnu-toolchain
./configure --prefix=/opt/riscv --with-arch=rv64imac --with-abi=lp64
make

# Set environment
export CROSS_COMPILE=riscv64-unknown-elf-
export PATH=/opt/riscv/bin:$PATH
```

### 2. Build Commands

#### Hosted Build (SDL) - For Development
```bash
# Configure
cmake -B build-hosted -G Ninja -DWMB_HOSTED_BUILD=ON

# Build
cmake --build build-hosted

# Run - see WMB in SDL window!
./build-hosted/wmb

# Run with debug output
WMB_DEBUG=1 ./build-hosted/wmb

# Debug with GDB
gdb ./build-hosted/wmb
```

#### Target Build (RISC-V) - For Deployment
```bash
# Configure with toolchain file
cmake -B build-riscv -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake

# Build
cmake --build build-riscv

# Output: build-riscv/wmb.elf (for RISC-V target)

# Generate binary image
riscv64-unknown-elf-objcopy -O binary build-riscv/wmb.elf wmb.bin

# Test with QEMU
qemu-system-riscv64 -machine virt -m 256M -kernel build-riscv/wmb.elf -serial stdio
```

### 3. CMake Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `WMB_HOSTED_BUILD` | OFF | Build for host with SDL backend |
| `WMB_ENABLE_JWM` | ON | Include JWM window manager |
| `WMB_BUNDLE_FONTS` | OFF | Embed TTF fonts in binary (vs filesystem) |
| `WMB_DEBUG` | OFF | Enable debug output |
| `WMB_SCREEN_WIDTH` | 1024 | Screen width in pixels |
| `WMB_SCREEN_HEIGHT` | 768 | Screen height in pixels |
| `WMB_SCREEN_BPP` | 32 | Bits per pixel |

> **Note:** FreeType is always built as an integral part of WMB (not optional).
> Built-in bitmap fonts provide base functionality; FreeType adds scalable font support.

### 4. Platform Configuration

```bash
# Edit config/config.h for target-specific settings
#define PLATFORM "generic"
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define SCREEN_BPP 32
/* FB_BASE_ADDR is platform-dependent - check your memory map */
#define FB_BASE_ADDR 0xB0000000  /* Example address */

# Platform-specific config
cd hal/platform/riscv
# Edit platform_config.h for your hardware
```

### 5. Build Stages

```makefile
# Makefile.config
ARCH = riscv64
PLATFORM = generic
USE_FREETYPE = 0
USE_BUILTIN_WM = 1

# Build stages (no network - per BASE_REQUIREMENTS #1)
all: stage1

stage1: hal xserver test_client
stage2: stage1 xlib wm
stage3: stage2 freetype
```

### 6. Legacy Make Commands (Alternative to CMake)

```bash
# Build HAL
make -C hal PLATFORM=generic

# Build X server
make -C xserver

# Build minimal Xlib
make -C xlib

# Build window manager
make -C wm

# Link final binary
make x11-riscv.elf

# Generate binary image
riscv64-unknown-elf-objcopy -O binary x11-riscv.elf x11-riscv.bin

# Disassembly for debugging
riscv64-unknown-elf-objdump -d x11-riscv.elf > x11-riscv.lst
```

### 5. Linker Script

The linker script defines WMB memory layout. **Note**: The OS determines where WMB is loaded;
the WMB linker script defines internal section organization, not absolute addresses.

```ld
/* wmb_linker.ld - WMB internal layout */
OUTPUT_ARCH(riscv)
/* Entry point called by OS after loading WMB */
ENTRY(wmb_init)

/* 
 * Memory regions are placeholders - actual addresses set by OS loader.
 * WMB is linked as relocatable or the OS specifies the load address.
 */
MEMORY
{
    /* These are example addresses - OS determines actual placement */
    WMB_TEXT (rx)  : ORIGIN = 0x40000000, LENGTH = 2M
    WMB_DATA (rwx) : ORIGIN = 0x40200000, LENGTH = 64M
}

SECTIONS
{
    /* WMB API entry table - fixed offset for OS to call */
    .wmb_api : {
        KEEP(*(.wmb_api))
    } > WMB_TEXT

    .text : {
        *(.text.init)
        *(.text*)
    } > WMB_TEXT

    .rodata : {
        *(.rodata*)
        *(.srodata*)
    } > WMB_TEXT

    .data : {
        *(.data*)
        *(.sdata*)
    } > WMB_DATA

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        __bss_end = .;
    } > WMB_DATA

    /* WMB internal heap - for X server resources */
    .wmb_heap : {
        __wmb_heap_start = .;
        . = . + 32M;  /* 32MB WMB heap */
        __wmb_heap_end = .;
    } > WMB_DATA

    /* 
     * Framebuffer is mapped by OS/HAL, not allocated here.
     * The HAL provides framebuffer address at runtime.
     */
}
```

**Note**: The OS is responsible for:
- Loading WMB at the appropriate address
- Setting up the main stack before calling `wmb_init`
- Providing heap memory (WMB may use its own internal heap or OS malloc)

## Dependency Resolution Strategy

### External Dependencies

#### Provided by OS (NOT bundled in WMB)

1. **C Standard Library** (newlib or equivalent)
   - The OS must provide a POSIX-compatible libc
   - WMB expects: stdio, stdlib, string, time, math functions
   - Heap allocation (malloc/free) must be functional
   - **See [OS_LIBC_REQUIREMENTS.md](OS_LIBC_REQUIREMENTS.md) for detailed requirements**

#### Bundled in WMB (Required Dependencies)

1. **FreeType 2.14.1** (required, integral to WMB) ✅ **IN WORKSPACE**
   - Location: `fonts/freetype/` (already added to workspace)
   - Source: https://www.freetype.org/
   - Minimal build configuration:
     - Disable: PNG, BZ2, HarfBuzz, LZW compression
     - Enable: TrueType, OpenType, BDF/PCF support
     - Size: ~150-200 KB when optimized
   - Provides: Scalable fonts, anti-aliased rendering, metrics
   - **Required for modern applications** (see Font System Architecture section)

### Eliminating Dependencies

| Original Dependency | Replacement Strategy |
|---------------------|---------------------|
| MiNT system calls | N/A - TinyX uses POSIX |
| Sockets (network) | REMOVED - per BASE_REQUIREMENTS #1 (no client/server) |
| Kdrive interface | Implement ~10 functions |
| NVDI | Not needed (software rendering) |
| GDOS fonts | Built-in bitmap fonts |
| libX11 | Minimal custom implementation |
| libXext | Remove or stub out |
| libXrender | Remove |
| libpng/libjpeg | Remove, use XBM/XPM |
| Cairo/Pango | Remove |

## Testing Strategy

### Unit Tests
```bash
# Test HAL components independently
make test_hal
./test/test_framebuffer
./test/test_input
./test/test_timer

# Test X server without WM
make test_xserver
./test/test_protocol
./test/test_window
```

### Integration Tests
```bash
# Run simple X client
make test_client
# Should show window on framebuffer

# Run with window manager
make run_wm
# Should show decorated windows
```

### Debugging
```bash
# QEMU RISC-V for testing
qemu-system-riscv64 \
    -machine virt \
    -m 256M \
    -kernel x11-riscv.elf \
    -serial stdio \
    -display gtk

# GDB debugging
riscv64-unknown-elf-gdb x11-riscv.elf
(gdb) target remote :1234
```

## Build Time Estimates

- **HAL implementation**: 40-80 hours
- **X server port**: 80-120 hours
- **Xlib minimal**: 40-60 hours
- **WM integration**: 40-60 hours
- **Font system (built-in + FreeType)**: 20-40 hours
- **Testing & debugging**: 80-120 hours
- **Total**: 300-480 hours (8-12 weeks)

## Binary Size Estimates (Two-Block Architecture)

### WMB Binary (Contains Full Xlib + FreeType)

| Component | Optimized (-Os) | Debug (-O0 -g) |
|-----------|----------------|----------------|
| HAL | 10-20 KB | 50-80 KB |
| X Server Core | 100-150 KB | 400-600 KB |
| libX11-wmb (Full Xlib) | 300-400 KB | 800-1000 KB |
| Window Manager | 40-60 KB | 200-300 KB |
| Entry Point Table | 2-5 KB | 2-5 KB |
| Built-in Fonts | 20-30 KB | 20-30 KB |
| **FreeType (minimal)** | **150-200 KB** | **400-500 KB** |
| **Total WMB Binary** | **622-865 KB** | **1.9-2.5 MB** |

### Optional: Bundled TTF Fonts

| Font | Size | Notes |
|------|------|-------|
| DejaVu Sans | ~700 KB | General purpose |
| DejaVu Sans Mono | ~350 KB | Monospace/code |
| Liberation Sans | ~400 KB | Arial replacement |
| **If bundled** | **+1-1.5 MB** | Can load from FS instead |

### Application Wrapper Library (Per-App Addition)

| Component | Optimized (-Os) | Debug (-O0 -g) |
|-----------|----------------|----------------|
| libX11-app.a (wrappers) | 10-20 KB | 30-50 KB |

### Total System Size Comparison

| Scenario | Traditional (Xlib per app) | Two-Block |
|----------|---------------------------|-----------|
| WMB + 1 app | 400 + 400 = 800 KB | 750 + 15 = **765 KB** |
| WMB + 5 apps | 400 + 5×400 = 2.4 MB | 750 + 5×15 = **825 KB** |
| WMB + 10 apps | 400 + 10×400 = 4.4 MB | 750 + 10×15 = **900 KB** |

*Note: libc (newlib) is provided by OS and not counted in WMB binary size.*
*FreeType is now included in base WMB binary.*

## Memory Requirements

- **WMB Code**: 700KB - 1.5MB (with FreeType)
- **WMB Data/BSS**: 100-200KB
- **WMB Heap**: 16-32MB (for X server resources + font cache)
- **Font Cache**: 1-4MB (glyph bitmaps)
- **Per-App Addition**: 15-50KB code
- **Stack**: 1-2MB
- **Framebuffer**: Depends on resolution (e.g., 1024x768x32 = 3MB)
- **Total RAM needed**: 32-64MB minimum, 128MB comfortable

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [OS_LIBC_REQUIREMENTS.md](OS_LIBC_REQUIREMENTS.md) | Required libc functions from OS |
| [KDRIVE_IMPLEMENTATION_REFERENCE.md](KDRIVE_IMPLEMENTATION_REFERENCE.md) | Kdrive driver implementation guide |
| [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) | Hardware abstraction layer API |
| [WMB_API_DESIGN.md](WMB_API_DESIGN.md) | WMB architecture and API design |
| [GETTING_STARTED.md](GETTING_STARTED.md) | Quick start guide |
