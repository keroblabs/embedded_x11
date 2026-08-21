# Quick Start Summary: X11 on Bare-Metal RISC-V

## What You Have

You've checked out excellent source code to work with:

1. **TinyX (Kdrive)** - Lightweight X11 server designed for embedded/framebuffer systems
   - Full X11 protocol handling
   - Clean Kdrive HAL interface
   - Framebuffer-optimized rendering
   - No OS windowing dependencies
   - Designed by Keith Packard specifically for embedded use

2. **JWM** - Lightweight X11 window manager
   - Window decorations and management
   - Menus and taskbar
   - Currently depends on libX11, libXext, etc.

3. **libX11** - Standard X11 client library
4. **X11 Headers** - Protocol definitions

## The Challenge

Create a **single standalone binary** that runs on bare-metal RISC-V with:
- No operating system
- Direct hardware access
- All dependencies statically linked
- Window manager built-in

## The Solution

### Three-Layer Architecture

```
┌──────────────────────────────────────┐
│   Window Manager (JWM simplified)    │  ← User interface layer
├──────────────────────────────────────┤
│   X11 Server (TinyX/Kdrive)          │  ← Protocol & management
├──────────────────────────────────────┤
│   HAL (Kdrive interface)             │  ← Clean HAL (KdCardFuncs)
├──────────────────────────────────────┤
│   Hardware (RISC-V platform)         │  ← Framebuffer, input, etc.
└──────────────────────────────────────┘
```

## Key Insights

### 1. TinyX/Kdrive is Already Platform-Abstracted

TinyX isolates hardware access through clean Kdrive interfaces:
- **KdCardFuncs** - Graphics/framebuffer operations
- **KdKeyboardFuncs** - Keyboard driver interface
- **KdPointerFuncs** - Mouse/pointer driver interface
- **Clean interface** - Only ~10 functions to implement

**Your task**: Implement Kdrive interface structures for RISC-V hardware

### 2. Critical VDI/AES Calls to Replace

| Current | Replacement | Purpose |
|---------|-------------|---------|
| `v_bar()` | `hal_fb_fill_rect()` | Fill rectangle |
| `v_pline()` | `hal_gfx_line()` | Draw line |
| `v_opnvwk()` | `hal_fb_init()` | Initialize graphics |
| `graf_mouse()` | `hal_cursor_set()` | Set cursor |
| `evnt_multi()` | `hal_input_wait()` | Wait for events |
| `wind_get()` | (remove - X handles windows) | Window info |

Only **~30 functions** need hardware replacements!

### 3. Dependencies Can Be Eliminated

**TinyX/Kdrive** current dependencies:
- Standard C library → Use newlib
- Network sockets → Optional (lwIP) or remove  
- Kdrive interface → Your HAL implementation
- Font rendering → Built-in bitmap fonts or FreeType (optional)

**JWM** current dependencies:
- libX11 → Create minimal version (~80 functions)
- libXext, libXrender → Remove
- Cairo, Pango → Remove (too heavy)
- PNG, JPEG → Remove (use XBM only)

Result: **Zero external dependencies**

## Your Strategy

### Phase 1: Minimal X Server (3 weeks)
**Goal**: Get X server displaying on framebuffer

**Tasks**:
1. Create HAL interface (framebuffer, input, timer)
2. Implement HAL for your RISC-V platform
3. Implement Kdrive interface (KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs)
4. Test with simple built-in client

**Success**: Display window, handle mouse/keyboard

### Phase 2: Add Window Manager (3 weeks)
**Goal**: Window decorations and management

**Tasks**:
1. Create minimal libX11 (~80 functions)
2. Port JWM (remove heavy dependencies)
3. Integrate WM as built-in server client
4. Hard-code configuration

**Success**: Multiple windows with decorations

### Phase 3: Hardware Drivers (3 weeks)
**Goal**: Real keyboard/mouse drivers

**Tasks**:
1. Implement keyboard driver for your platform
2. Implement mouse driver
3. Optimize graphics performance
4. Add hardware cursor (if available)

**Success**: Fully functional input

### Phase 4: Polish (2-5 weeks)
**Goal**: Production ready

**Tasks**:
1. Font support (built-in or FreeType)
2. Stability testing
3. Documentation
4. Example applications

**Success**: 24-hour stable operation

## What Makes This Feasible

### 1. TinyX/Kdrive is Small and Clean
- Designed for embedded systems
- Already platform-abstracted (Kdrive interface)
- No kernel dependencies
- Minimal memory footprint

### 2. All Hardware Access is Isolated
- **Graphics**: Just framebuffer (linear memory)
- **Input**: Keyboard + mouse events
- **Timing**: Simple millisecond counter
- **No complex drivers needed**

### 3. X11 Protocol is Well-Defined
- Stable since 1987
- Extensive documentation
- Reference implementation available
- TinyX/Kdrive implements it with clean interfaces

### 4. JWM is Lightweight
- **~10,000 lines** of C code
- Minimal dependencies
- Simple configuration
- Easy to strip down

## Implementation Checklist

### Week 1-3: Foundation
- [ ] Set up RISC-V toolchain
- [ ] Create project structure
- [ ] Implement HAL headers
- [ ] Implement framebuffer HAL for your platform
- [ ] Implement timer HAL (RISC-V CSRs)
- [ ] Create boot code (start.S)
- [ ] Test: Draw colored rectangles

### Week 4-6: X Server Core
- [ ] Copy TinyX source
- [ ] Study Kdrive interface (kdrive.h)
- [ ] Implement KdCardFuncs for RISC-V
- [ ] Implement KdKeyboardFuncs
- [ ] Implement KdPointerFuncs
- [ ] Test: Display simple window

### Week 7-9: Window Manager
- [ ] Create minimal libX11
- [ ] Strip JWM dependencies
- [ ] Port JWM core
- [ ] Integrate with server
- [ ] Test: Multiple decorated windows

### Week 10-12: Drivers
- [ ] Implement keyboard driver
- [ ] Implement mouse driver
- [ ] Optimize performance
- [ ] Test: Full interaction

### Week 13-17: Polish
- [ ] Add fonts
- [ ] Stability testing
- [ ] Documentation
- [ ] Demo applications

## Quick Win Path

For fastest results:

1. **Start with framebuffer only**
   - No keyboard/mouse initially
   - Hard-coded test patterns
   - Verify graphics HAL works

2. **Use QEMU for testing**
   - Faster than real hardware
   - Easy debugging
   - Standard platform

3. **Stub out complex features**
   - No fonts (draw rectangles only)
   - No networking
   - No window manager initially

4. **Incremental testing**
   - Test each component independently
   - Build up functionality gradually
   - Fix issues early

## Code Size Budget

| Component | Size (optimized) |
|-----------|------------------|
| HAL | 20 KB |
| X Server Core | 150 KB |
| Minimal libX11 | 50 KB |
| JWM (stripped) | 60 KB |
| Fonts | 40 KB |
| newlib-nano | 100 KB |
| **Total** | **420 KB** |

Very achievable for embedded systems!

## Memory Budget

| Usage | Size |
|-------|------|
| Code | 500 KB |
| Static data | 100 KB |
| Heap | 16-32 MB |
| Stack | 1 MB |
| Framebuffer | 3 MB (1024x768x32) |
| **Total** | **32-64 MB** |

Reasonable for modern RISC-V SoCs.

## Critical Success Factors

### 1. Clean HAL Abstraction
- All hardware access through HAL
- Easy to port to new platforms
- Well-documented interface

### 2. Minimal Dependencies
- Static link everything
- No dynamic loading
- Self-contained binary

### 3. Incremental Development
- Test each layer independently
- Don't integrate too early
- Fix issues before moving on

### 4. Good Debugging
- Serial console output
- Framebuffer debug text
- Memory usage monitoring

## Common Pitfalls to Avoid

❌ **Don't**: Try to port full Linux X.org server
✅ **Do**: Use TinyX/Kdrive (designed for embedded)

❌ **Don't**: Include all X11 extensions
✅ **Do**: Core protocol only

❌ **Don't**: Try to support all JWM features
✅ **Do**: Strip to essentials

❌ **Don't**: Add networking initially
✅ **Do**: Get local-only working first

❌ **Don't**: Use complex font rendering
✅ **Do**: Start with bitmap fonts

## Getting Help

### Documentation Created for You

1. **[README.md](README.md)** - This quick start (you are here!)
2. **[CUSTOM_XLIB.md](CUSTOM_XLIB.md)** - ⭐ **Start here for implementation!**
   - Practical strategy to patch standard libX11
   - Minimal code changes approach
   - X server internal API definition
   - Maintains 100% X11 API compatibility
3. **[ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md)** - System overview
4. **[ROM_API_DESIGN.md](ROM_API_DESIGN.md)** - Xlib-compatible API design
5. **[HAL_SPECIFICATION.md](HAL_SPECIFICATION.md)** - Complete HAL API
6. **[BUILD_PLAN.md](BUILD_PLAN.md)** - Dependencies and build process
7. **[GETTING_STARTED.md](GETTING_STARTED.md)** - Step-by-step code examples
8. **[ROADMAP.md](ROADMAP.md)** - Detailed timeline and milestones

See **[DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md)** for complete navigation guide.

### Most Important Document

**Read [CUSTOM_XLIB.md](CUSTOM_XLIB.md) first!** It shows the practical, minimal-effort approach to:
- Reuse 99% of standard libX11 code
- Replace only the transport layer (4 files, ~1,050 lines)
- Maintain perfect X11 API compatibility
- Enable zero learning curve for X11 developers

**Note**: Throughout documentation, "WMB" refers to "Window Manager Binary" - the standalone binary containing X11 server + JWM + HAL. The WMB is linked at a fixed address (not PIC) that depends on the target platform's memory architecture (address TBD).

### Key Source Files to Study

**In TinyX**:
- `kdrive/src/kdrive.c` - Kdrive core, main entry point
- `kdrive/src/kinput.c` - Input handling framework
- `kdrive/fbdev/fbdev.c` - Reference framebuffer implementation
- `dix/` - Device-Independent X (core X11 protocol)
- `fb/` - Framebuffer rendering library

**In JWM**:
- `src/main.c` - Window manager entry
- `src/client.c` - Client window handling
- `src/border.c` - Window decorations
- `src/event.c` - Event processing

## First Steps (Today)

1. **Read GETTING_STARTED.md**
   - Contains working code examples
   - Step-by-step instructions
   - Copy-paste ready

2. **Set up toolchain**
   ```bash
   # Install RISC-V GCC
   sudo apt-get install gcc-riscv64-unknown-elf
   ```

3. **Create project directory**
   ```bash
   mkdir x11-riscv
   cd x11-riscv
   # Follow GETTING_STARTED.md structure
   ```

4. **Implement first HAL function**
   - Start with `hal_fb_fill_rect()`
   - Test on QEMU or your platform
   - See colored rectangle on screen!

## Success Looks Like

### After 3 weeks:
- Colored rectangles on screen
- Timer working
- Basic HAL complete

### After 6 weeks:
- X server running
- Simple client displays window
- Mouse cursor visible

### After 9 weeks:
- Window decorations
- Multiple windows
- Menus working

### After 12 weeks:
- Full keyboard/mouse input
- Smooth graphics
- Stable operation

### After 15-17 weeks:
- Production ready
- Documentation complete
- Demo applications running

## You Can Do This!

This project is **very achievable** because:

✓ You already have working X11 server code (TinyX/Kdrive)
✓ You already have working window manager code (JWM)
✓ The abstraction layer is small (~10 Kdrive functions)
✓ No kernel complexity - direct hardware access
✓ Well-defined interfaces (X11 protocol, HAL)
✓ Incremental approach - test each piece

The hard work (X11 protocol, window management) is **already done**.
You just need to connect it to your hardware!

## Questions to Answer Before Starting

1. **What RISC-V platform?**
   - CPU model (RV32/RV64)?
   - Clock speed?
   - RAM size?

2. **What display hardware?**
   - Framebuffer address?
   - Resolution?
   - Pixel format?

3. **What input devices?**
   - Keyboard interface (PS/2, USB, custom)?
   - Mouse interface?

4. **What tools available?**
   - JTAG debugger?
   - Serial console?
   - QEMU emulation?

Once you know these, you can start implementing the HAL!

---

**Start with [GETTING_STARTED.md](GETTING_STARTED.md)** - it has complete, working code examples to get you up and running quickly.

Good luck! This is an exciting project with a clear path to success.
