# Project Roadmap: Bare-Metal X11 for RISC-V

## Executive Summary

**Goal**: Create a single, standalone binary combining X11 server + window manager for bare-metal RISC-V platform

**Approach**: 
1. Use TinyX/Kdrive as the X11 protocol implementation base
2. Implement Kdrive interface (KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs) - ~10 functions
3. Integrate JWM (Joe's Window Manager) as built-in client
4. Eliminate all external dependencies through static linking

**Timeline**: 10-17 weeks (2.5-4 months) for full implementation (potentially shorter with cleaner Kdrive)

**Memory Requirements**: 32-128MB RAM, 400KB-1.5MB code size

---

## Phase 1: Foundation (Weeks 1-3)

### Goals
- ✓ Create project structure
- ✓ Implement basic HAL interface
- ✓ Get framebuffer working
- ✓ Implement input polling
- ✓ Test on QEMU RISC-V

### Deliverables
1. **HAL Framework**
   - Framebuffer driver (fill rect, copy rect)
   - Timer service (platform-specific HAL interface - implementation outside scope)
   - Input stub (keyboard/mouse framework)
   
2. **Boot Code**
   - RISC-V startup assembly
   - BSS clearing
   - Stack setup
   - C runtime initialization
   
3. **Build System**
   - Makefile for cross-compilation
   - Linker script for bare metal
   - QEMU testing setup

4. **Test Program**
   - Draw colored rectangles
   - Simple animation
   - Verify timer accuracy

### Success Criteria
- [ ] Boots on QEMU RISC-V
- [ ] Draws to framebuffer
- [ ] Timer functions work
- [ ] Can compile with `make`

### Key Files Created
```
x11-riscv/
├── hal/include/{hal.h, hal_framebuffer.h, hal_input.h, hal_timer.h}
├── hal/platform/generic/{fb_generic.c, timer_platform.c, input_stub.c}
├── boot/{start.S, init.c}
├── Makefile
└── linker.ld
```

**Note:** `timer_platform.c` is a stub. Actual timer implementation is platform-specific and outside project scope.

---

## Phase 2: X Server Core (Weeks 4-8)

### Goals
- Implement Kdrive interface (KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs)
- Integrate TinyX/Kdrive with HAL
- Implement X11 protocol handling
- Get basic window management working

### Tasks

#### Week 4: Analysis & Setup
- [ ] Study TinyX/Kdrive interface structures
- [ ] Review tinyx/kdrive/fbdev/ reference implementation
- [ ] Create kdrive/riscv/ directory for platform-specific code
- [ ] Set up Kdrive build configuration

#### Week 5-6: Core Porting
- [ ] Implement KdCardFuncs structure
  - `cardinit()` → Initialize framebuffer
  - `screeninit()` → Setup screen info
  - `enable()` / `disable()` → Display control
  
- [ ] Replace event handling (event.c)
  - `evnt_multi()` → `hal_input_wait()`
  - Event queue integration
  
- [ ] Port window management (window.c)
  - Remove GEM window dependencies
  - Pure X11 window tree
  
- [ ] Port drawing primitives (draw_img.c, pixmap.c)
  - Bitmap operations via HAL
  - Pixmap creation/destruction

#### Week 7: Protocol & Resources
- [ ] Client connection handling (clnt.c, server.c)
  - Remove socket code (local only for now)
  - Shared memory communication
  
- [ ] Resource management
  - Atoms (Atom.c)
  - Properties (Property.c)
  - Graphics contexts (gcontext.c)
  - Colormaps (colormap.c)
  
- [ ] Request processing (Request.c)
  - X11 protocol dispatcher
  - Request validation

#### Week 8: Testing & Integration
- [ ] Create test X client
  - Simple window creation
  - Basic drawing (lines, rectangles)
  - Event handling
  
- [ ] Debug protocol issues
- [ ] Performance baseline

### Deliverables
1. **Ported X Server**
   - Full X11 protocol support
   - Local client connections
   - Window hierarchy management
   - Graphics primitives working
   
2. **Test Client**
   - Connects to server
   - Creates windows
   - Draws graphics
   - Receives events

### Success Criteria
- [ ] Test client displays window
- [ ] Can draw basic shapes
- [ ] Mouse/keyboard events work
- [ ] Multiple windows supported
- [ ] No crashes during operation

### Key Metrics
- **Code Size**: ~150KB X server core
- **Memory Usage**: ~2MB heap for resources
- **Performance**: 60 FPS for simple operations

---

## Phase 3: Window Manager Integration (Weeks 9-11)

### Goals
- Create minimal libX11 implementation
- Port JWM window manager
- Integrate WM as built-in server client
- Add window decorations and menus

### Tasks

#### Week 9: Minimal libX11
- [ ] Display connection
  - `XOpenDisplay()` - connect to local server
  - Direct function calls (no sockets)
  
- [ ] Window operations (~30 functions)
  - Create/destroy/map/unmap windows
  - Window configuration
  - Window properties
  
- [ ] Drawing operations (~20 functions)
  - GC creation and management
  - Basic drawing primitives
  - Text rendering
  
- [ ] Event handling (~10 functions)
  - Event retrieval
  - Event masks
  - Event dispatch

#### Week 10: JWM Port
- [ ] Remove external dependencies
  - Strip out Cairo, Pango, Xrender
  - Remove PNG/JPEG support
  - Use XBM for icons only
  
- [ ] Core WM functionality
  - Window decorations (border.c)
  - Title bars
  - Resize/move operations
  - Window stacking
  
- [ ] UI Components
  - Root menu (menu.c)
  - Task bar (optional)
  - Window list

#### Week 11: Integration & Polish
- [ ] Connect JWM to X server
  - Internal client connection
  - Event routing
  - Drawing coordination
  
- [ ] Configuration
  - Hard-coded defaults
  - Remove XML parsing
  
- [ ] Testing
  - Multiple windows
  - Window manipulation
  - Menu interaction

### Deliverables
1. **libX11 Subset** (~80 functions)
2. **Integrated Window Manager**
   - Window decorations
   - Basic menus
   - Task switching
3. **Sample Applications**
   - xclock equivalent
   - xterm equivalent (if time permits)

### Success Criteria
- [ ] Windows have decorations
- [ ] Can move/resize windows
- [ ] Menus work
- [ ] Multiple apps run simultaneously
- [ ] Stable operation

### Code Size Impact
- **libX11**: +50KB
- **JWM**: +60KB
- **Total**: ~260KB

---

## Phase 4: Hardware Drivers (Weeks 12-14)

### Goals
- Implement real keyboard driver
- Implement real mouse driver
- Optimize graphics performance
- Add hardware cursor if available

### Tasks

#### Week 12: Keyboard Driver
- [ ] Identify keyboard controller
  - PS/2 controller
  - USB keyboard
  - Custom interface
  
- [ ] Implement driver
  - Interrupt handler
  - Scancode to keycode mapping
  - Modifier key tracking
  
- [ ] Keyboard mapping
  - US layout (default)
  - Configurable layouts (optional)

#### Week 13: Mouse Driver
- [ ] Identify mouse interface
  - PS/2 mouse
  - USB mouse
  - Touchscreen
  
- [ ] Implement driver
  - Interrupt handler
  - Movement tracking
  - Button state
  - Scroll wheel (if applicable)

#### Week 14: Graphics Optimization
- [ ] Profile performance
  - Identify bottlenecks
  - Measure frame rates
  
- [ ] Optimize critical paths
  - Rectangle fill (DMA if available)
  - Bitblt operations
  - Scrolling
  
- [ ] Hardware acceleration (if available)
  - GPU initialization
  - 2D acceleration
  - Hardware cursor

### Deliverables
1. **Complete Input System**
   - Fully functional keyboard
   - Fully functional mouse
   - Proper event generation
   
2. **Optimized Graphics**
   - Acceptable performance (30+ FPS)
   - Hardware acceleration (if available)

### Success Criteria
- [ ] Keyboard fully functional
- [ ] Mouse tracking smooth
- [ ] Graphics fast enough for UI
- [ ] No input lag

---

## Phase 5: Polish & Features (Weeks 15-17)

### Goals
- Add font support
- Configuration system
- Stability improvements
- Documentation

### Tasks

#### Week 15: Fonts
- [ ] Built-in bitmap fonts
  - Fixed 8x13
  - Variable width (optional)
  
- [ ] Font rendering
  - Simple glyph blitting
  - Anti-aliasing (optional)
  
- [ ] FreeType integration (optional)
  - Minimal build
  - TrueType support

#### Week 16: Configuration & Stability
- [ ] Configuration system
  - Simple text file format
  - Or hard-coded with defines
  
- [ ] Error handling
  - Graceful degradation
  - Error reporting
  
- [ ] Memory management
  - Leak detection
  - Resource limits

#### Week 17: Testing & Documentation
- [ ] Comprehensive testing
  - Stress tests
  - Long-running stability
  - Memory usage monitoring
  
- [ ] Documentation
  - Build instructions
  - Platform porting guide
  - API documentation
  
- [ ] Example applications
  - Demo programs
  - Sample X clients

### Deliverables
1. **Production-Ready System**
   - Stable and tested
   - Well-documented
   - Example applications
   
2. **Porting Guide**
   - How to adapt to new platform
   - HAL implementation guide

### Success Criteria
- [ ] 24-hour stability test passes
- [ ] All features documented
- [ ] Builds cleanly
- [ ] Demo applications work

---

## Optional Enhancements (Future)

### Network Support
- **Effort**: 1-2 weeks
- **Dependencies**: lwIP
- **Benefits**: Remote X clients
- **Size**: +100KB

### Advanced Fonts
- **Effort**: 1 week
- **Dependencies**: FreeType
- **Benefits**: Better text rendering
- **Size**: +200KB

### Compositing
- **Effort**: 2-3 weeks
- **Benefits**: Transparency, effects
- **Size**: +100KB
- **Note**: Requires significant performance

### Multi-Monitor
- **Effort**: 1 week
- **Benefits**: Multiple displays
- **Requires**: Hardware support

---

## Risk Mitigation

### Technical Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Platform-specific hardware varies | High | Clean HAL abstraction |
| Performance insufficient | High | Early profiling, optimization |
| Memory constraints | Medium | Configurable limits, testing |
| Compatibility issues | Medium | Minimize X11 extensions |
| Input driver complexity | Medium | Start with polling, add interrupts later |

### Schedule Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Underestimated effort | Medium | Focus on MVP, defer optional features |
| Debugging takes longer | Medium | Good logging, incremental testing |
| Scope creep | Low | Strict feature priorities |

---

## Success Metrics

### Functional Requirements
- [x] X11 protocol compliance (core protocol)
- [ ] Window management (create, map, move, resize)
- [ ] Input handling (keyboard, mouse)
- [ ] Graphics primitives (draw, fill, copy)
- [ ] Multi-window support
- [ ] Basic window decorations

### Non-Functional Requirements
- **Performance**: 30+ FPS for typical operations
- **Memory**: <64MB RAM total usage
- **Code Size**: <1MB binary
- **Reliability**: No crashes in 24-hour test
- **Portability**: HAL supports new platform in <1 week

### Quality Metrics
- No memory leaks (Valgrind clean, if applicable)
- Clean compilation (zero warnings with -Wall -Wextra)
- Test coverage (unit tests for HAL components)
- Documentation complete

---

## Resource Requirements

### Development Environment
- RISC-V cross-compiler toolchain
- QEMU for testing
- Hardware platform (or emulation)
- Debugging tools (GDB, OpenOCD)

### Team
- **Recommended**: 1-2 experienced developers
- **Skills needed**:
  - C programming
  - RISC-V assembly
  - X11 protocol knowledge
  - Bare-metal/embedded development
  - Hardware driver development

### Hardware
- Development machine (Linux workstation)
- RISC-V target board (or QEMU)
- Debug probe (JTAG, if applicable)

---

## Checkpoint Reviews

### End of Phase 1 (Week 3)
- **Review**: Does HAL work on target platform?
- **Decision**: Proceed to X server or refine HAL?

### End of Phase 2 (Week 8)
- **Review**: Is X server functional?
- **Decision**: Continue to WM or revisit server?

### End of Phase 3 (Week 11)
- **Review**: Is WM integrated and working?
- **Decision**: Focus on drivers or additional features?

### End of Phase 4 (Week 14)
- **Review**: Are drivers complete?
- **Decision**: Ship or add polish phase?

### End of Phase 5 (Week 17)
- **Review**: Is system production-ready?
- **Decision**: Release or continue refinement?

---

## Deliverable Summary

### Week 3 Milestone
- Working HAL on target platform
- Framebuffer test program

### Week 8 Milestone
- X server runs test client
- Basic window operations work

### Week 11 Milestone
- Window manager fully integrated
- Multiple windows with decorations

### Week 14 Milestone
- All hardware drivers functional
- Performance acceptable

### Week 17 Milestone
- Production-ready system
- Complete documentation
- Example applications

---

## Next Immediate Actions

1. **Set up development environment**
   - Install RISC-V toolchain
   - Set up QEMU or target hardware
   - Test cross-compilation

2. **Create project repository**
   - Initialize directory structure (see GETTING_STARTED.md)
   - Copy relevant source files
   - Set up version control

3. **Implement Phase 1**
   - Follow GETTING_STARTED.md
   - Build basic HAL
   - Test on QEMU

4. **Document platform specifics**
   - Framebuffer address and format
   - Input device interfaces  
   - Timer hardware (platform-specific, outside scope - just define HAL interface)
   - Interrupt controller details

5. **Study TinyX/Kdrive architecture**
   - Read through kdrive source code
   - Study fbdev reference implementation
   - Plan Kdrive interface implementation strategy

---

## Documentation Index

For detailed information, refer to:

1. **[ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md)**
   - System architecture
   - Component relationships
   - Dependency analysis

2. **[HAL_SPECIFICATION.md](HAL_SPECIFICATION.md)**
   - Complete HAL API reference
   - Kdrive interface mapping
   - Implementation guidelines

3. **[BUILD_PLAN.md](BUILD_PLAN.md)**
   - Detailed dependency tree
   - Build strategy
   - Source organization

4. **[GETTING_STARTED.md](GETTING_STARTED.md)**
   - Step-by-step implementation guide
   - Code examples
   - Initial testing procedures

This roadmap provides a structured approach to achieving your goal of a standalone X11 system for bare-metal RISC-V.
