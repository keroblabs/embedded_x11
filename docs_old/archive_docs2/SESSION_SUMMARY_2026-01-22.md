# Executive Session Summary
## WMB X11 Window System Project
### Session Date: January 22, 2026

---

## Project Introduction

The user introduced this as a **challenging project** that immediately captured attention due to its ambitious scope: creating a complete X11 windowing system (WMB - Window Manager Binary) for a custom RISC-V bare-metal environment.

### Initial Fascination Points
- Porting X11 to bare metal without traditional OS dependencies
- Creating a two-block architecture to eliminate client-server overhead
- Building a complete graphical stack from HAL to window manager
- The elegance of the "shared Xlib" approach for code size efficiency

---

## Project Overview

### What is WMB?
**WMB (Window Manager Binary)** is a single monolithic binary containing:
- X11 Server (based on TinyX/Kdrive)
- Window Manager (simplified JWM)
- Full Xlib implementation (libX11-wmb)
- Hardware Abstraction Layer (HAL)
- FreeType font rendering
- Entry Point Table for application access

### Target Environment
- **Primary**: RISC-V bare-metal with custom OS
- **Development**: Hosted build on PC via SDL backend
- **OS Dependency**: Only libc (newlib) provided by underlying OS

---

## Key Architectural Decisions

### 1. Two-Block Architecture (BASE_REQUIREMENTS #1)

**Decision**: Bypass traditional X11 client-server model for code efficiency.

```
Traditional X11:
  App (400KB) → libX11 (400KB) → [Socket/IPC] → X Server
  Problem: 400KB duplicated per application

WMB Two-Block:
  App (15KB wrapper) → [Direct call] → WMB (contains full Xlib)
  Benefit: Only 15KB per app, Xlib shared in WMB
```

**Rationale**: 
- 10 apps traditional = 4.8 MB
- 10 apps WMB = 900 KB
- Massive savings for embedded systems

### 2. HAL Abstraction for Dual Build Targets

**Decision**: Create HAL interface enabling both hosted (SDL) and target (RISC-V) builds.

| Build | HAL Backend | Use Case |
|-------|-------------|----------|
| Hosted | SDL2 | PC development, debugging, testing |
| Target | RISC-V MMIO | Final deployment on hardware |

**Rationale**: 
- Enables rapid development without hardware
- Same codebase, different HAL implementations
- Debug with standard tools (GDB, IDE) on PC

### 3. TinyX/Kdrive as X11 Server Foundation

**Decision**: Use TinyX/Kdrive instead of GEM-XServer as the X11 server base.

| Criteria | GEM-XServer | TinyX/Kdrive |
|----------|-------------|--------------|
| Architecture | Under GEM window manager | Direct framebuffer |
| HAL Interface | ~30 scattered VDI/AES calls | ~10 clean Kdrive functions |
| Maintenance | Unmaintained since 2000s | X.org ecosystem |
| Effort | 6-7 weeks | 4-5 weeks |

**Rationale**:
- TinyX was designed for embedded framebuffer systems
- Clean Kdrive interface aligns perfectly with HAL design
- Reference implementation (`kdrive/fbdev/`) provides clear path
- Lower risk, faster development

### 4. Font System: Built-in + FreeType (Both Required)

**Decision**: Include BOTH font systems as integral parts of WMB.

| Component | Purpose | Size |
|-----------|---------|------|
| Built-in Bitmap | Fallback, system UI, cursors | ~25 KB |
| FreeType 2.14.1 | Scalable fonts, anti-aliasing | ~180 KB |

**Rationale**:
- Built-in fonts: Guaranteed availability, no file I/O
- FreeType: Required for any modern application (smooth text)
- FreeType is NOT optional - essential for real apps

### 5. OS/WMB Boundary (BASE_REQUIREMENTS #8)

**Decision**: Clear separation of responsibilities.

**OS Provides**:
- C library (newlib or equivalent)
- Syscalls (open, read, write, malloc, etc.)
- Timer/clock services
- Hardware initialization
- WMB loading

**WMB Provides**:
- X11 server functionality
- Window management
- Font rendering
- Entry point table for apps
- HAL abstraction

### 6. Newlib/Libc Requirements (OS Implementation Guide)

**Decision**: WMB links against libc but does NOT implement syscalls - that's the OS's job.

#### Why Newlib?

Newlib is the standard C library for bare-metal/embedded RISC-V development:
- Included in RISC-V GCC toolchain (`riscv64-unknown-elf-gcc`)
- Designed for embedded systems with minimal OS support
- Requires only ~15 syscall stubs to function

#### Syscall Stubs the OS Must Implement

Newlib calls these functions; the OS must provide implementations:

```c
/* CRITICAL - Required for WMB to function */
void *_sbrk(intptr_t incr);           /* Heap management - MUST work */
int _write(int fd, char *buf, int len); /* Debug output (stdout/stderr) */
int _read(int fd, char *buf, int len);  /* File/config reading */
int _open(const char *path, int flags); /* File access */
int _close(int fd);                     /* File cleanup */
int _fstat(int fd, struct stat *st);    /* File info */
int _lseek(int fd, int offset, int whence); /* File positioning */
int _isatty(int fd);                    /* Terminal check */

/* Timer - connect to HAL */
int _gettimeofday(struct timeval *tv, void *tz);  /* CRITICAL for X11/JWM */

/* Optional - can stub */
int _kill(int pid, int sig);            /* Return -1 */
int _getpid(void);                      /* Return 1 */
```

#### Minimal Syscall Implementation Example

```c
/* os/syscalls.c - Minimal newlib syscalls for WMB */

#include <sys/stat.h>
#include <errno.h>
#include "hal.h"

/* Heap management - CRITICAL */
extern char _heap_start;  /* From linker script */
extern char _heap_end;
static char *heap_ptr = &_heap_start;

void *_sbrk(intptr_t incr) {
    char *prev = heap_ptr;
    if (heap_ptr + incr > &_heap_end) {
        errno = ENOMEM;
        return (void*)-1;
    }
    heap_ptr += incr;
    return prev;
}

/* Timer - connect to HAL */
int _gettimeofday(struct timeval *tv, void *tz) {
    uint64_t ms = hal_timer_get_ms();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
    return 0;
}

/* Debug output */
int _write(int fd, char *buf, int len) {
    if (fd == 1 || fd == 2) {  /* stdout/stderr */
        hal_debug_write(buf, len);
        return len;
    }
    return -1;
}

/* File I/O - minimal stubs or full implementation */
int _read(int fd, char *buf, int len)  { return 0; }  /* EOF */
int _open(const char *path, int flags) { return -1; } /* No file */
int _close(int fd)                     { return 0; }
int _fstat(int fd, struct stat *st)    { return -1; }
int _lseek(int fd, int off, int w)     { return 0; }
int _isatty(int fd)                    { return (fd < 3); }
int _kill(int pid, int sig)            { return -1; }
int _getpid(void)                      { return 1; }
```

#### Required libc Functions by Category

| Category | Functions | Priority |
|----------|-----------|----------|
| **Memory** | malloc, free, realloc, calloc | CRITICAL |
| **String** | memcpy, memset, strlen, strcmp, strcpy, strdup | CRITICAL |
| **Stdio** | fopen, fread, fclose, sprintf, snprintf | Required |
| **Time** | time, gettimeofday, usleep | CRITICAL |
| **Directory** | opendir, readdir, closedir, stat | Required for fonts |
| **Math** | sin, cos, sqrt | Optional |
| **Signal** | signal, raise | Can stub |

#### Heap Requirements

| Requirement | Value | Notes |
|-------------|-------|-------|
| Minimum heap | 32 MB | For X server resources + font cache |
| Comfortable | 64 MB | Better performance |
| Max single allocation | 16 MB | Font cache, pixmaps |
| Thread safety | Not required | WMB is single-threaded |

#### Timer Requirements

| Requirement | Value | Notes |
|-------------|-------|-------|
| Resolution | Millisecond minimum | Microsecond preferred |
| Source | Monotonic | RISC-V `mtime` CSR ideal |
| Function | `gettimeofday()` | TinyX/JWM use this extensively |

**Connection to HAL**:
```
Application calls: gettimeofday()
       ↓
newlib calls: _gettimeofday()
       ↓
OS syscall: calls hal_timer_get_ms()
       ↓
HAL: reads RISC-V mtime CSR (or SDL_GetTicks for hosted)
```

#### File I/O Requirements

WMB needs file access for:
1. **Font files** - `/usr/share/fonts/` (TTF, BDF, PCF)
2. **JWM config** - `~/.jwmrc` or `/etc/jwm/jwmrc`
3. **X11 configs** - `/etc/X11/rgb.txt`, keyboard mappings
4. **Debug logs** - stdout/stderr output

**Fallback behavior if files unavailable**:
- Fonts: Use built-in bitmap fonts
- JWM config: Use hard-coded defaults
- X11 configs: Use built-in color table, US keyboard

#### Verification Test

```c
/* Test before running WMB */
int verify_os_libc(void) {
    /* Test heap */
    void *p = malloc(1024 * 1024);  /* 1 MB */
    if (!p) return 1;
    free(p);
    
    /* Test time */
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    usleep(1000);  /* 1ms */
    gettimeofday(&tv2, NULL);
    if (tv2.tv_sec < tv1.tv_sec && tv2.tv_usec <= tv1.tv_usec)
        return 2;  /* Time not advancing */
    
    /* Test sprintf */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", 42);
    if (strcmp(buf, "42") != 0) return 3;
    
    return 0;  /* Success */
}
```

#### Documentation Reference

See [OS_LIBC_REQUIREMENTS.md](OS_LIBC_REQUIREMENTS.md) for complete function list and detailed requirements.

### 5. GEM-XServer → TinyX/Kdrive Replacement (Prior Session Decision)

**Decision**: Replace GEM-XServer with TinyX/Kdrive as the X11 server foundation.

#### Analysis: Why Not GEM-XServer?

The workspace originally contained **GEM-XServer** (in `GEM-XServer/` directory), an X11 server designed to run on Atari systems under the GEM operating system.

| Issue | GEM-XServer | Impact |
|-------|-------------|--------|
| **Architecture** | Designed to run UNDER GEM window manager | Wrong direction - we want X11 to BE the window system |
| **OS Dependencies** | VDI graphics calls, AES window calls | ~30 scattered functions to replace |
| **Maintenance** | Unmaintained since ~2000s, Atari-specific | No community, outdated practices |
| **HAL Integration** | No clean abstraction layer | Difficult to port to new hardware |
| **Effort** | 6-7 weeks fighting OS assumptions | High risk, unclear path |

#### Analysis: Why TinyX/Kdrive?

**TinyX** (also known as Kdrive) was designed from the start for embedded/framebuffer systems:

| Advantage | TinyX/Kdrive | Impact |
|-----------|--------------|--------|
| **Architecture** | Designed for direct framebuffer access | Perfect match for bare-metal |
| **Clean Interface** | KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs | Only ~10 functions to implement |
| **Lineage** | Part of X.org ecosystem | Well-documented, proven code |
| **Reference** | `kdrive/fbdev/` example driver included | Clear implementation path |
| **Effort** | 4-5 weeks with clean architecture | Lower risk, clear path |

#### What TinyX/Kdrive Provides

```
tinyx/
├── kdrive/
│   ├── src/              # Kdrive core (HAL interface)
│   │   ├── kdrive.c      # Main Kdrive implementation
│   │   └── kdrive.h      # KdCardFuncs, KdKeyboardFuncs, etc.
│   ├── fbdev/            # Reference framebuffer driver
│   └── [riscv/]          # Our implementation (to create)
├── dix/                  # Device-Independent X (protocol handling)
├── fb/                   # Framebuffer rendering library
├── mi/                   # Machine-Independent layer
└── os/                   # OS abstraction (to adapt)
```

#### Kdrive Interface (What We Implement)

```c
/* Only ~10 functions needed for complete HAL integration */

typedef struct _KdCardFuncs {
    Bool (*cardinit)(KdCardInfo *);    /* Initialize display */
    Bool (*scrinit)(KdScreenInfo *);   /* Initialize screen */
    Bool (*initScreen)(ScreenPtr);     /* X screen setup */
    Bool (*finishInitScreen)(ScreenPtr);
    void (*preserve)(KdCardInfo *);
    void (*restore)(KdCardInfo *);
    void (*enable)(ScreenPtr);         /* Enable display */
    void (*disable)(ScreenPtr);        /* Disable display */
    void (*getColors)(/* ... */);
    void (*putColors)(/* ... */);
} KdCardFuncs;

typedef struct _KdKeyboardFuncs {
    void (*Init)(KdKeyboardInfo *);
    void (*Leds)(KdKeyboardInfo *, int);
    void (*Bell)(/* ... */);
    void (*Fini)(KdKeyboardInfo *);
} KdKeyboardFuncs;

typedef struct _KdPointerFuncs {
    Status (*Init)(KdPointerInfo *);
    Status (*Enable)(KdPointerInfo *);
    void (*Disable)(KdPointerInfo *);
    void (*Fini)(KdPointerInfo *);
} KdPointerFuncs;
```

#### Migration Actions Completed

1. **TinyX source added to workspace** at `tinyx/`
   - Verified: Kdrive directory present
   - Verified: KdCardFuncs interface structures present
   - Verified: fbdev reference implementation available
   - Source: "Necromancy" fork - resurrected TinyX from X.org history

2. **Documentation updated** (all files now reference TinyX):
   - README.md - Architecture diagrams
   - ARCHITECTURE_ANALYSIS.md - Component descriptions
   - HAL_SPECIFICATION.md - Kdrive interface mapping
   - CUSTOM_XLIB.md - X server references
   - BUILD_PLAN.md - Build strategy

3. **Key terminology changes**:
   - "GEM-XServer" → "TinyX/Kdrive"
   - "VDI/AES calls" → "Kdrive interface structures"
   - "~30 functions to replace" → "~10 interface functions to implement"

#### Migration Document Reference

See [TINYX_MIGRATION.md](TINYX_MIGRATION.md) for complete migration details.

---

## Session Work Log

### Phase 0: GEM-XServer → TinyX Migration
*(Completed in prior session)*

- Analyzed GEM-XServer limitations for bare-metal target
- Identified TinyX/Kdrive as better fit for embedded framebuffer systems
- Added TinyX source to workspace at `tinyx/`
- Updated all documentation to reference TinyX/Kdrive
- Created TINYX_MIGRATION.md documenting the decision

### Phase 1: ROM → WMB Terminology Update
*(From prior session, referenced in context)*

- Changed terminology from "ROM" to "WMB" throughout documentation
- WMB better reflects that it's a loadable binary, not necessarily in ROM
- Updated all references in BUILD_PLAN.md and related documents

### Phase 2: Hosted Development Environment Documentation

**User Request**: Clarify how hosted development with SDL works - "this is game-changing"

**Changes Made to BUILD_PLAN.md**:

1. **Added "Hosted Development Environment (SDL Backend)" section** (~150 lines)
   - Two Build Targets table (Hosted vs Target)
   - Architecture diagram showing HAL abstraction
   - Benefits of hosted development
   - Complete HAL SDL backend implementation code
   - CMake build configuration
   - Development workflow examples
   - What works in hosted mode table
   - Limitations table

2. **Updated HAL directory structure**
   ```
   hal/platform/
   ├── sdl/           # HOSTED BUILD
   │   ├── hal_sdl.c
   │   └── hal_sdl_keymap.c
   └── riscv/         # TARGET BUILD
       ├── hal_riscv_fb.c
       ├── hal_riscv_input.c
       └── hal_riscv_timer.c
   ```

3. **Added CMake option**: `WMB_HOSTED_BUILD` (OFF by default)

### Phase 3: Font System Architecture Documentation

**User Request**: Document font system - built-in fonts and FreeType integration

**Changes Made to BUILD_PLAN.md**:

1. **Added "Font System Architecture" section** (~300 lines)
   - Overview: Two Font Types table
   - Built-in Bitmap Fonts explanation
     - What are BDF/PCF fonts
     - Built-in font format (compiled C arrays)
     - Code example: `fixed_6x13.c` structure
     - Standard built-in fonts table
     - Built-in font API header
     - Font conversion tool usage
   - FreeType Scalable Fonts section
     - Why FreeType is essential (comparison table)
     - Integration diagram
     - Minimal build configuration (CMake)
     - FreeType wrapper API
     - Bundled fonts options
   - X11 Font Integration code
   - Font Directory Structure

2. **Updated Binary Size Estimates**
   - Added FreeType: 150-200 KB (optimized)
   - Updated total: 622-865 KB (was ~500 KB before)
   - Added optional bundled TTF fonts section

3. **Updated Source Code Organization**
   - Added fonts/ directory with subdirectories
   - Marked FreeType as integral (not optional)

### Phase 4: FreeType as Required Dependency

**User Request**: Make FreeType integral to WMB, not optional

**Changes Made**:

1. **Changed "Optionally Bundled" to "Bundled in WMB (Required Dependencies)"**

2. **Removed `WMB_ENABLE_FREETYPE` CMake option** (was optional, now always ON)

3. **Added note**: 
   > FreeType is always built as an integral part of WMB (not optional).

4. **Updated all references** from "optional" to "required/integral"

### Phase 5: FreeType Repository Added to Workspace

**User Action**: Added FreeType source repository to workspace

**Verification Performed**:
- Confirmed: `fonts/freetype/` exists
- Version: **FreeType 2.14.1**
- License: FreeType License (FTL) or GPLv2+ (dual-licensed)
- Build systems: CMake, Make, Meson (all included)

**Documentation Updates**:

1. **Updated Bundled Dependencies section**:
   ```markdown
   1. **FreeType 2.14.1** (required, integral to WMB) ✅ **IN WORKSPACE**
      - Location: `fonts/freetype/` (already added to workspace)
   ```

2. **Updated FreeType section header**:
   ```markdown
   ### FreeType 2.14.1 Scalable Fonts (Integral to WMB)
   > ✅ **Status**: FreeType source is present in workspace at `fonts/freetype/`
   ```

3. **Updated Font Directory Structure** to reflect actual repo contents:
   ```
   fonts/                          # IN WORKSPACE
   ├── freetype/                   # FreeType 2.14.1 ✅ PRESENT
   │   ├── CMakeLists.txt         # FreeType CMake build
   │   ├── Makefile               # FreeType Make build
   │   ├── meson.build            # FreeType Meson build
   │   ├── include/               # Public headers
   │   ├── src/                   # Source code (~250 files)
   │   ├── builds/                # Platform-specific configs
   │   ├── docs/                  # Documentation
   │   └── LICENSE.TXT            # FreeType License (FTL)
   ```

4. **Updated Source Code Organization** to show FreeType as present

---

## Current Workspace State

### Directory Structure
```
c:\inbox\x11_window\
├── docs/                    # Documentation
│   ├── archive_docs/       # Archived/merged documents
│   │   ├── HAL_SPECIFICATION.md         # Merged into KDRIVE_HAL_GUIDE.md
│   │   ├── KDRIVE_IMPLEMENTATION_REFERENCE.md  # Merged into KDRIVE_HAL_GUIDE.md
│   │   └── PORTING_EXAMPLE.md           # GEM-XServer specific (obsolete)
│   ├── BUILD_PLAN.md       # Main build plan (extensively updated)
│   ├── BASE_REQUIREMENTS.md
│   ├── KDRIVE_HAL_GUIDE.md # NEW: Merged HAL + Kdrive guide
│   ├── WMB_API_DESIGN.md
│   ├── CUSTOM_XLIB.md
│   ├── OS_LIBC_REQUIREMENTS.md
│   ├── GETTING_STARTED.md
│   └── SESSION_SUMMARY_2026-01-22.md  # This file
├── fonts/
│   └── freetype/            # FreeType 2.14.1 ✅ PRESENT
├── jwm/                     # JWM window manager source
├── libX11/                  # Reference libX11 source
├── tinyx/                   # TinyX/Kdrive X server source
├── X11/                     # X11 headers and utilities
└── .vscode/                 # VS Code configuration
```

### Components Status

| Component | Status | Location |
|-----------|--------|----------|
| TinyX/Kdrive | ✅ In workspace | `tinyx/` |
| JWM | ✅ In workspace | `jwm/` |
| libX11 (reference) | ✅ In workspace | `libX11/` |
| X11 headers | ✅ In workspace | `X11/` |
| FreeType 2.14.1 | ✅ In workspace | `fonts/freetype/` |
| HAL | ⬜ To be created | `hal/` |
| libX11-wmb | ⬜ To be created | `xlib/` |
| libX11-app wrappers | ⬜ To be created | `libX11-app/` |
| Built-in fonts | ⬜ To be created | `fonts/builtin/` |
| FreeType wrapper | ⬜ To be created | `fonts/wrapper/` |
| BDF source files | ⬜ To be created | `fonts/bdf/` |
| CMake build system | ⬜ To be created | `CMakeLists.txt` |

---

## Key Documents Modified This Session

### BUILD_PLAN.md
The primary document, extensively updated with:

1. **New Sections Added**:
   - Hosted Development Environment (SDL Backend)
   - Font System Architecture
   - Two-Block Architecture explanation
   - Development Workflow

2. **Updated Sections**:
   - Dependency Resolution Strategy (FreeType now required)
   - Binary Size Estimates (includes FreeType)
   - Source Code Organization (fonts/, HAL structure)
   - CMake Configuration Options

3. **Line Count**: ~1700 lines (comprehensive)
   - *Note: Actual count may vary due to ongoing edits*

### HAL_SPECIFICATION.md
- Added platform backends section with SDL context

---

## Session: January 23, 2026 - Documentation Consolidation

### Document Merge: HAL_SPECIFICATION + KDRIVE_IMPLEMENTATION_REFERENCE

**Analysis Findings:**

Both documents covered overlapping topics with architectural ambiguity:
- `HAL_SPECIFICATION.md` (970 lines) proposed a custom `hal_*.h` API layer
- `KDRIVE_IMPLEMENTATION_REFERENCE.md` (1845 lines) implemented Kdrive interfaces directly

**Key Insight:** Kdrive IS the HAL. TinyX was designed with a clean HAL model through `KdCardFuncs`, `KdKeyboardFuncs`, `KdPointerFuncs`. Adding another `hal_*` layer would:
- Double implementation work
- Provide no benefit
- Violate BASE_REQUIREMENTS #5 (minimal patching)

**Decision:** Merge into single document

### Actions Taken

1. **Created `docs/archive_docs/` folder** for historical reference

2. **Moved to archive:**
   - `HAL_SPECIFICATION.md`
   - `KDRIVE_IMPLEMENTATION_REFERENCE.md`

3. **Created new merged document: `KDRIVE_HAL_GUIDE.md`** (~1100 lines)

### Additional Archive: PORTING_EXAMPLE.md

**Reason:** This document was entirely GEM-XServer specific:
- Showed porting `grph.c` from GEM-XServer VDI calls to HAL
- Referenced custom `hal_*.h` API that's now obsolete
- Used VDI/AES function mapping not applicable to TinyX/Kdrive
- File path references: `GEM-XServer/src/grph.c`

**Action:** Moved to `docs/archive_docs/PORTING_EXAMPLE.md`

**Note:** With TinyX/Kdrive approach, porting examples are different - implement Kdrive interface structures (`KdCardFuncs`, etc.) rather than replacing VDI calls. See KDRIVE_HAL_GUIDE.md for implementation templates.

### New Document Structure: KDRIVE_HAL_GUIDE.md

| Section | Content |
|---------|----------|
| Overview | Key insight: Kdrive IS the HAL |
| Platform Backends | SDL hosted vs RISC-V target builds |
| Kdrive Interface Architecture | Data flow and interface structures |
| Window Management Responsibilities | What X server handles vs HAL |
| Framebuffer Implementation | Complete code template |
| Keyboard Implementation | Complete code template |
| Mouse Implementation | Complete code template |
| OS Functions | Polling and lifecycle |
| Timer & Syscalls Integration | newlib connection |
| Event Queue Integration | KdEnqueue* functions |
| Implementation Template | Init file example |
| Build Integration | CMake for both backends |
| Testing & Validation | Checklist and test app |

### What Was Kept from Each Document

**From HAL_SPECIFICATION.md:**
- Platform backends section (SDL vs RISC-V)
- Window management responsibilities explanation
- Timer/syscalls integration guidance

**From KDRIVE_IMPLEMENTATION_REFERENCE.md:**
- Full implementation code templates
- Interface structure definitions
- Event queue integration
- Build integration (CMake)
- Testing checklist

**Removed (redundant):**
- Custom `hal_*.h` API definitions (Kdrive provides this)
- VDI/AES mapping tables (GEM-XServer legacy)
- Duplicate interface definitions

---

## Pending Work / Next Steps

### Immediate (High Priority)
1. **Create HAL implementation**
   - `hal/include/hal.h` - Common interface
   - `hal/platform/sdl/` - SDL backend for hosted build
   - Start with framebuffer and input

2. **Create top-level CMakeLists.txt**
   - Configure hosted vs target builds
   - Integrate FreeType build
   - Set up cross-compilation for RISC-V

3. **Create built-in font infrastructure**
   - `fonts/builtin/builtin_font.h`
   - BDF to C converter tool
   - Fixed fonts (6x13, 8x16)

### Medium Priority
4. **Create FreeType wrapper**
   - `fonts/wrapper/ft_wrapper.h`
   - `fonts/wrapper/ft_wrapper.c`
   - Minimal FreeType configuration

5. **Begin TinyX integration**
   - Identify Kdrive interface points
   - Create `wmb_screen.c` driver

6. **Create libX11-app wrappers**
   - Entry point table structure
   - Wrapper generation

### Documentation Needed
- [ ] CMake build guide (when created)
- [ ] HAL porting guide (for new platforms)
- [ ] Font system API reference

---

## Technical Reference

### Binary Size Targets

| Component | Optimized | Notes |
|-----------|-----------|-------|
| HAL | 10-20 KB | Platform-specific |
| X Server Core | 100-150 KB | TinyX/Kdrive |
| libX11-wmb | 300-400 KB | Full Xlib in WMB |
| Window Manager | 40-60 KB | Simplified JWM |
| Entry Point Table | 2-5 KB | API dispatch |
| Built-in Fonts | 20-30 KB | Bitmap fonts |
| FreeType | 150-200 KB | Minimal build |
| **Total WMB** | **622-865 KB** | |
| **Per-app addition** | **~15 KB** | Wrappers only |

### Memory Requirements
- WMB Code: 700 KB - 1.5 MB
- WMB Heap: 16-32 MB (X resources + font cache)
- Framebuffer: ~3 MB (1024×768×32)
- **Minimum RAM**: 32-64 MB
- **Comfortable**: 128 MB

### Build Commands (When Ready)

```bash
# Hosted development (SDL)
cmake -B build-hosted -DWMB_HOSTED_BUILD=ON
cmake --build build-hosted
./build-hosted/wmb

# Target build (RISC-V)
cmake -B build-riscv -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake
cmake --build build-riscv
```

---

## Key Insights from Session

1. **TinyX/Kdrive was the right choice** - GEM-XServer was designed to run under an existing window manager; TinyX was designed for exactly our use case (framebuffer-only embedded). The clean Kdrive interface (~10 functions) vs scattered VDI/AES calls (~30 functions) dramatically reduces integration effort.

2. **Hosted development is transformative** - Being able to build and test on a PC with SDL makes the project much more approachable. Full debugging, fast iteration, no hardware needed.

3. **FreeType is essential** - Initially considered optional, but analysis showed modern applications require scalable, anti-aliased fonts. Built-in bitmap fonts are fallback only.

4. **Two-block architecture scales well** - The more applications, the more savings. At 10 apps, traditional approach uses 4.4 MB vs WMB's 900 KB.

5. **Clear OS/WMB boundary** - WMB doesn't implement syscalls; it consumes them. This keeps scope manageable.

6. **Newlib syscall stubs are well-defined** - Only ~15 syscall stubs needed for newlib. Critical ones: `_sbrk` (heap), `_gettimeofday` (timer), `_write` (debug). See OS_LIBC_REQUIREMENTS.md for complete list.

7. **Workspace is taking shape** - With TinyX and FreeType now in workspace, major source dependencies are present. HAL implementation and build system are next priorities.

---

## Session Update: January 23, 2026 - CMakeLists Include Path Setup

### Include Path Analysis for KDrive SDL Module

Scanned `tinyx/configure.ac` to extract all required include paths for VSCode IntelliSense and compilation. The following paths were added to `framework/CMakeLists.txt`:

#### Include Paths Added (mapped from configure.ac variables)

| Include Path | configure.ac Variable | Purpose |
|--------------|----------------------|---------|
| `../X11` | - | X11 protocol headers (X.h, Xproto.h, etc.) |
| `../tinyx/kdrive/src` | `KDRIVE_INC` | KDrive core headers (kdrive.h) |
| `../tinyx/kdrive/linux` | `KDRIVE_OS_INC` | Linux-specific KDrive headers |
| `../tinyx/include` | `CORE_INCS` | TinyX core headers (scrnintstr.h, windowstr.h, etc.) |
| `../tinyx/mi` | `MI_INC` | Machine-independent layer (mi.h, mipointer.h) |
| `../tinyx/fb` | `FB_INC` | Framebuffer layer (fb.h, fboverlay.h) |
| `../tinyx/Xext` | `XEXT_INC` | X extensions |
| `../tinyx/miext/shadow` | `MIEXT_SHADOW_INC` | Shadow framebuffer (shadow.h) |
| `../tinyx/miext/damage` | `MIEXT_DAMAGE_INC` | Damage tracking |
| `../tinyx/render` | `RENDER_INC` | Render extension |
| `../tinyx/randr` | `RANDR_INC` | RandR extension (randrstr.h) |
| `../tinyx/xfixes` | `FIXES_INC` | XFixes extension |
| `../tinyx/damageext` | `DAMAGE_INC` | Damage extension |
| `../tinyx/os` | - | OS abstraction layer |
| `../tinyx/dix` | - | Device-independent X (dix.h) |

#### configure.ac Reference (lines 553-556)

```makefile
KDRIVE_INC='-I$(top_srcdir)/kdrive/src'
KDRIVE_PURE_INCS="$KDRIVE_INC $MIEXT_DAMAGE_INC $MIEXT_SHADOW_INC $XEXT_INC $FB_INC $MI_INC"
KDRIVE_OS_INC='-I$(top_srcdir)/kdrive/linux'
KDRIVE_INCS="$KDRIVE_PURE_INCS $KDRIVE_OS_INC"
```

#### Regenerate compile_commands.json

After updating CMakeLists.txt, regenerate for VSCode IntelliSense:
```bash
cd build && cmake ..
```

---

### Preprocessor Definition Scan

Scanned TinyX source for all preprocessor definitions needed for compilation. Created comprehensive reference document: [TINYX_PREPROCESSOR_DEFINITIONS.md](TINYX_PREPROCESSOR_DEFINITIONS.md)

#### Sources Scanned
- `tinyx/configure.ac` - 45 `AC_DEFINE` macros
- `tinyx/include/dix-config.h.in` - DIX configuration template (252 lines)
- `tinyx/include/kdrive-config.h.in` - KDrive configuration template
- All `Makefile.am` files for `AM_CFLAGS` and `AM_CPPFLAGS`
- Source files for `#ifdef` usage patterns

#### Critical Definitions for WMB Build

| Category | Definition | Value | Purpose |
|----------|------------|-------|---------|
| **Required** | `HAVE_DIX_CONFIG_H` | 1 | Enables dix-config.h inclusion |
| **Required** | `HAVE_KDRIVE_CONFIG_H` | 1 | Enables kdrive-config.h inclusion |
| **Required** | `KDRIVESERVER` | 1 | Building KDrive server |
| **Required** | `KDRIVEDDXACTIONS` | (empty) | KDrive DDX actions |
| **Required** | `X_BYTE_ORDER` | `X_LITTLE_ENDIAN` | Endianness |
| Extension | `DBE` | 1 | Double buffer (needed for JWM) |
| Extension | `DPMSExtension` | 1 | Power management |
| Extension | `SCREENSAVER` | 1 | Screen saver support |
| Scheduler | `SMART_SCHEDULE` | 1 | Time-based scheduler |
| **Disabled** | `TCPCONN`, `UNIXCONN` | - | No network in WMB |
| **Disabled** | `XDMCP` | - | No remote display |

#### Key Findings
1. **`HAVE_DIX_CONFIG_H`** is used by virtually ALL TinyX source files
2. **KDrive** adds `HAVE_KDRIVE_CONFIG_H` which includes dix-config.h
3. **Network disabled** - WMB uses direct calls, no socket connections needed
4. **Extensions selectable** - Most extensions can be disabled for smaller build
5. **RISC-V build** may need `NEED_STRCASECMP` if newlib lacks it

See full document: [TINYX_PREPROCESSOR_DEFINITIONS.md](TINYX_PREPROCESSOR_DEFINITIONS.md)

---

### VSCode Include Path Conflict Resolution

**Problem**: Linux development machine has `/usr/include/X11` which VSCode was finding instead of workspace headers.

**Solution**: Modified `framework/CMakeLists.txt`:
1. Added `BEFORE` keyword to `include_directories()` - puts workspace paths before system paths
2. Changed `../X11` to `..` - so `#include <X11/X.h>` resolves to workspace `../X11/X.h`

```cmake
include_directories(BEFORE
                    ./
                    ..    # Parent dir so #include <X11/X.h> works
                    ../tinyx/kdrive/src
                    # ... rest of paths
                    )
```

---

### KDrive Keyboard Driver Implementation

#### Understanding kdKeymap

The `kdKeymap` array maps hardware scancodes to X11 KeySyms:

```c
KeySym kdKeymap[KD_MAX_LENGTH * KD_MAX_WIDTH];
//              247 scancodes  × 4 columns
```

For each scancode, 4 KeySyms (columns):
| Index | Meaning |
|-------|---------|
| 0 | Unshifted (normal) |
| 1 | Shifted |
| 2 | Mode_switch (AltGr) |
| 3 | Mode_switch + Shift |

**Key insight**: For Latin-1/ASCII characters (0x20-0x7E), **KeySym value equals ASCII value**!
```c
#define XK_space    0x0020  // Same as ASCII ' '
#define XK_a        0x0061  // Same as ASCII 'a'
```

#### Design Discussion: Scancode vs ASCII

**Initial approach considered**: Use SDL scancodes directly (SDL_SCANCODE_A = 4, etc.)
- Pros: Physical key positions, standard approach
- Cons: Would require changing the HAL API

**Final approach adopted**: Use existing HAL API protocol
- The `keypad_driver` already provides a working protocol
- Same API will work on final RISC-V target
- No changes needed to existing SDL event handling

#### HAL Keyboard Protocol

The existing `keypad_driver` uses this protocol (2 bytes per event):

| Command | Data | Meaning |
|---------|------|---------|
| `0x01` | modifier_mask | Modifier key change (Shift, Ctrl, Alt) |
| `0x02` | key_data | Key press |
| `0x03` | key_data | Key release |

Key data values from `keyraw_driver.c`:
| Value | Key |
|-------|-----|
| 8 | Left arrow |
| 10 | Down arrow |
| 11 | Up arrow |
| 13 | Enter |
| 21 | Right arrow |
| 27 (0x1B) | Escape |
| 32-125 | Printable ASCII |
| 127 | Backspace |

#### Implementation: tima_kdrive_kbd.c

Created `tinyx/kdrive/tima/tima_kdrive_kbd.c` that:

1. **Consumes from HAL API**:
```c
extern void    keypad_driver_init(void);
extern int     keypad_driver_has_data(void);
extern uint8_t keypad_driver_read_data(void);
```

2. **Uses key_data as scancode index** - simplifies mapping since we control both ends

3. **Builds kdKeymap** mapping key_data values to X11 KeySyms:
```c
SET_KEY(KEY_DATA_LEFT,      XK_Left,      XK_Left);      // 8
SET_KEY(KEY_DATA_ENTER,     XK_Return,    XK_Return);    // 13
SET_KEY('a',                XK_a,         XK_A);         // 97
// etc.
```

4. **Handles modifier keys** via 0x01 packets - tracks state changes and generates synthetic press/release events

5. **Provides poll function** called from main loop:
```c
void tima_KeyboardPoll(void)
{
    while (keypad_driver_has_data()) {
        cmd = keypad_driver_read_data();
        switch (cmd) {
            case 0x01: /* modifier */ ...
            case 0x02: /* key down */ KdEnqueueKeyboardEvent(data, 0);
            case 0x03: /* key up */   KdEnqueueKeyboardEvent(data, 1);
        }
    }
}
```

#### Data Flow

```
SDL Event → keypad_driver_event_key() → buffer [cmd, data]
                                              ↓
                        tima_KeyboardPoll() reads buffer
                                              ↓
                        KdEnqueueKeyboardEvent(scancode, is_up)
                                              ↓
                        kdKeymap[scancode * 4 + shift_column]
                                              ↓
                        KeySym delivered to X client (JWM)
```

#### Files Created/Modified

| File | Purpose |
|------|---------|
| `tinyx/kdrive/tima/tima_kdrive_kbd.c` | KDrive keyboard driver using HAL API |
| `tinyx/kdrive/tima/tima_kdrive.h` | Updated with keyboard function declarations |

#### KdKeyboardFuncs Structure

```c
const KdKeyboardFuncs tima_KeyboardFuncs = {
    .Load    = tima_KeyboardLoad,    /* Build kdKeymap */
    .Init    = tima_KeyboardInit,    /* Call keypad_driver_init() */
    .Leds    = tima_KeyboardLeds,    /* No-op (HAL doesn't support) */
    .Bell    = tima_KeyboardBell,    /* No-op */
    .Fini    = tima_KeyboardFini,    /* Cleanup */
    .LockLed = 1                     /* CapsLock LED number */
};
```

#### Key Design Decision

**Why use ASCII-based key_data as scancode?**

The alternative was to use physical scancodes (like SDL_SCANCODE_*), but:
1. The existing `keypad_driver` protocol already works
2. Same code will run on RISC-V target with identical HAL API
3. ASCII-based mapping is simpler (KeySym == ASCII for printables)
4. No need to modify the SDL event handling layer

This approach trades "standard" scancodes for **portability** - the KDrive driver is now independent of SDL and will work with any HAL that implements the same protocol.

---

## Session Update: January 24, 2026 - Tima KDrive Code Review

### Complete Tima KDrive Implementation

**Note**: The KDrive module is named "Tima" after **Tima OS**, the custom RISC-V bare-metal operating system this project targets (as defined in BASE_REQUIREMENTS).

The user implemented the full Tima KDrive module across 5 files:

| File | Purpose | Platform Dependencies |
|------|---------|----------------------|
| `tima_kdrive.h` | Header with types and declarations | ✅ None |
| `tima_kdrive_fb.c` | Framebuffer (KdCardFuncs) | ✅ None |
| `tima_kdrive_kbd.c` | Keyboard (KdKeyboardFuncs) | ✅ None |
| `tima_kdrive_mouse.c` | Mouse (KdMouseFuncs) | ✅ None |
| `tima_kdrive_system.c` | OS + TinyX externals | ✅ None |

### Code Review Summary

**Overall Assessment**: ✅ PASS - Platform Independence Achieved

The KDrive module in `tinyx/kdrive/tima/` is **completely platform-independent**. All SDL dependencies are correctly isolated in `framework/sdl_drivers/`.

#### Architecture Validation

```
tinyx/kdrive/tima/         →  Uses only hal_drivers.h API
                              (Platform-free, portable to any HAL backend)
         ↓
framework/sdl_drivers/     →  SDL HAL implementation (hosted builds)
framework/riscv_drivers/   →  RISC-V HAL implementation (Tima OS target, future)
```

#### HAL API Used by KDrive Module

**LCD/Framebuffer**:
```c
lcd_driver_init()
lcd_driver_get_framebuffer()
lcd_driver_get_width() / lcd_driver_get_height()
lcd_driver_get_bits_per_pixel()
```

**Keyboard**:
```c
keypad_driver_init()
keypad_driver_has_data()
keypad_driver_read_data()
```

**Mouse**:
```c
mouse_driver_init(index)
mouse_driver_has_data(index)
mouse_driver_read_data(index)
```

#### Minor Issues Found

| Issue | File | Severity | Description |
|-------|------|----------|-------------|
| #1 | `tima_kdrive_mouse.c` | ⚠️ Medium | `flags` variable not initialized |
| #2 | `tima_kdrive_mouse.c` | ⚠️ Medium | `continue` in switch breaks packet parsing |
| #3 | `tima_kdrive_mouse.c` | ⚠️ Low | Missing `mouse_driver_init()` call |

#### Recommended Fixes

**Fix #1 & #3** - In `tima_kdrive_mouse.c`:
```c
static Bool tima_MouseInit(void)
{
    mouse_driver_init(0);  // Add this line
    mouse_state.initialized = TRUE;
    // ...
}

void tima_MousePoll(void)
{
    int dx = 0, dy = 0;           // Initialize
    unsigned long flags = 0;       // Initialize
    // ...
}
```

**Fix #2** - Restructure mouse packet parsing to accumulate X/Y before processing.

### Portability Assessment

**To port to RISC-V bare-metal (Tima OS target)**:
1. KDrive module (`tinyx/kdrive/tima/`): **No changes needed**
2. Create `framework/riscv_drivers/` implementing same HAL API:
   - `lcd_driver.c` → MMIO framebuffer
   - `keyraw_driver.c` → GPIO/UART keyboard
   - `mouse_driver.c` → GPIO/SPI mouse

### Files Created/Updated This Session

| File | Purpose |
|------|---------|
| `docs/TIMA_KDRIVE_CODE_REVIEW.md` | Detailed code review document |

### Documentation Updates

- Updated `KDRIVE_HAL_GUIDE.md` with clarification on:
  - `InitInput()` is external function YOU implement, TinyX calls it
  - `KdInitInput()` is KDrive API function you call to register drivers
  - Added table of all TinyX external functions vs KDrive API functions
- Renamed module from `sdl/` to `tima/` to reflect Tima OS branding

---

## How to Continue

### ✅ Completed Milestone: Tima KDrive Module
The KDrive HAL module (`tinyx/kdrive/tima/`) has been **fully implemented and code reviewed**. It is platform-independent and uses the `hal_drivers.h` API.

### ✅ Completed: X11 Client-Server Analysis Document
Created comprehensive analysis document: **[X11_CLIENT_SERVER_ANALYSIS.md](X11_CLIENT_SERVER_ANALYSIS.md)**

This document analyzes the current X11 client-server communication architecture between JWM and TinyX, providing the foundation for implementing the bypass. Key sections:

| Section | Content |
|---------|---------|
| **JWM Client-Side** | JX* wrappers, event loop, `ConnectionNumber(display)` usage |
| **libX11 Request Building** | `GetReq()`, `Data()`, buffer management (`dpy->buffer/bufptr/bufmax`) |
| **TinyX Server-Side** | `Dispatch()` loop, `ReadRequestFromClient()`, `requestVector[]` dispatch |
| **Wire Protocol** | Request/Reply/Event formats, `xCreateWindowReq` structure |
| **Complete Data Flow** | Step-by-step trace: JWM → libX11 → socket → TinyX → handler |
| **Bypass Architecture** | Entry Point Table design, libX11-wmb modifications, event bypass |

### ✅ Completed: Simplified Bypass Architecture Design
Updated the analysis document with a **cleaner bypass implementation strategy**:

**Key Insight**: The bypass is simpler than originally thought:
1. **JWM side**: No code changes - just link against our libX11-wmb instead of standard libX11
2. **libX11-wmb**: Each X function (e.g., `XCreateSimpleWindow`, `XDrawArcs`) builds the request struct and calls entry point
3. **Entry Point Table**: Maps to TinyX handlers (e.g., `wmb.CreateWindow` → `ProcCreateWindow`)

Added comprehensive **Xlib → TinyX Proc mapping table**:

| Xlib Function | Request Struct | TinyX Proc |
|---------------|----------------|------------|
| `XCreateWindow` | `xCreateWindowReq` | `ProcCreateWindow` |
| `XDrawArcs` | `xPolyArcReq` | `ProcPolyArc` |
| `XDrawString16` | `xPolyTextReq` | `ProcPolyText` (opcode 75) |
| `XDrawImageString16` | `xImageTextReq` | `ProcImageText16` |
| ... | (60+ functions mapped) | ... |

**Note**: `XDrawString16` maps to `ProcPolyText` with opcode X_PolyText16 (75), while `XDrawImageString16` maps to `ProcImageText16`.

### ✅ Completed: Build Architecture Clarification
Important clarifications added to analysis document:

1. **No Preprocessor Switching**: libX11-wmb is a **separate implementation**, not a modified libX11 with `#ifdef`. JWM source unchanged, only build instructions differ.

2. **Proc* Header Already Exists**: `tinyx/dix/dispatch.h` already declares all `Proc*` functions using the `DISPATCH_PROC` macro (defined in `tinyx/include/dixstruct.h`). No new header needed.

3. **Two Build Modes for libX11-wmb**:
   | Mode | Define | wmb Access |
   |------|--------|------------|
   | **Hosted (SDL)** | `-DWMB_HOSTED_BUILD` | `extern WmbEntryPointTable wmb;` (link-time) |
   | **Target (RISC-V)** | `-DWMB_TARGET_BUILD` | `#define wmb (*(WmbEntryPointTable*)0x80100000)` (fixed address) |

4. **Linker Script**: For target build, WMB linker script places `wmb` table at known fixed address using `__attribute__((section(".wmb_entry_table")))`.

### 🔜 Next Major Milestone: Implement libX11-wmb
Per the simplified architecture, implementation is now clearer:

1. **Phase 1: Core Infrastructure**
   - Create `wmb_entry_points.h` with table definition
   - Create `wmb_link.h` with hosted/target build modes
   - Create `wmb_entries.c` with embedded client setup
   - Include existing `tinyx/dix/dispatch.h` for Proc* declarations

2. **Phase 2: Window Operations** (JWM startup)
   - `XOpenDisplay` / `XCloseDisplay`
   - `XCreateWindow` / `XCreateSimpleWindow`
   - `XMapWindow` / `XUnmapWindow` / `XDestroyWindow`

3. **Phase 3: Event Handling** (JWM event loop)
   - `XNextEvent` / `XPending` / `XSelectInput`

4. **Phase 4: Drawing Operations** (JWM rendering)
   - `XDrawRectangle` / `XFillRectangle`
   - `XDrawLine` / `XDrawString` / `XDrawArc`

5. **Phase 5: Remaining Operations**
   - GC, Pixmap, Color, Property/Atom operations

### Remaining Tasks
- Fix minor issues in `tima_kdrive_mouse.c` (initialization, packet parsing)
- Create CMakeLists.txt for KDrive Tima module build integration
- Test hosted build with SDL backend

---

*Summary created: January 22, 2026*
*Updated: January 23, 2026 - Documentation consolidation (HAL + Kdrive merge)*
*Updated: January 23, 2026 - CMakeLists include path setup for KDrive module*
*Updated: January 23, 2026 - KDrive keyboard driver implementation*
*Updated: January 24, 2026 - Full Tima KDrive module code review*
*Updated: January 24, 2026 - Renamed module from sdl/ to tima/ (Tima OS branding)*
*Updated: January 24, 2026 - X11 Client-Server Analysis document created*
*Updated: January 24, 2026 - Simplified bypass architecture with Xlib→Proc mapping table*
*Updated: January 24, 2026 - Build architecture clarification (no preprocessor, two build modes)*
*Next session: Implement libX11-wmb Phase 1 (core infrastructure)*
