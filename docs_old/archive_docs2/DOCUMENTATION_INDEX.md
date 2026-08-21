# Documentation Index

Complete documentation for porting X11 to bare-metal RISC-V

## Start Here

### [README.md](README.md) - Quick Start Summary
**Read this first!**
- Overview of the project
- What you have and what you need
- High-level strategy
- Quick answers to common questions
- Estimated effort and timeline

**Best for:** Getting oriented and understanding if this project is feasible

---

## Current Session Notes

### [library/PROJECT_CATCHUP_2026-03-09.md](library/PROJECT_CATCHUP_2026-03-09.md) - Catch-Up Snapshot
- Verified repo status vs requirements and prior sessions
- Build reality checks and key blockers discovered
- Risks, gaps, and short-term milestone framing

**Best for:** Quick project-state onboarding before making new code changes

### [library/TINYX_TIMA_INTEGRATION_NEXT_STEPS_2026-03-10.md](library/TINYX_TIMA_INTEGRATION_NEXT_STEPS_2026-03-10.md) - Integration Roadmap
- Comprehensive gap analysis for Tima OS stubs/infrastructure
- HAL completion status (display/keyboard/mouse) and remaining adapter issues
- TinyX module integration plan and expected CMake changes (phased)

**Best for:** Implementing the next integration phase from current linker state to TinyX-aligned build

---

## Planning Documents

### [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md) - System Architecture
- Complete system architecture overview
- Component relationships and data flow
- Current dependencies (what needs to change)
- Hardware abstraction points
- Memory and size estimates
- Next steps recommendations

**Best for:** Understanding how all pieces fit together


### [WMB_API_DESIGN.md](WMB_API_DESIGN.md) - X11 API Design
- WMB-based architecture (RAM, platform-dependent)
- Standard Xlib-compatible API
- No client/server separation
- Direct function calls architecture
- Developer experience and code portability

**Best for:** Understanding the API layer and application development

### [CUSTOM_XLIB.md](CUSTOM_XLIB.md) - Custom Xlib Implementation ⭐
- **Practical patching strategy** for standard libX11
- Minimal code changes (~1,050 lines)
- How to replace XCB transport with direct calls
- X server internal API definition
- Build system integration
- Step-by-step migration from standard libX11

**Best for:** Implementing the custom Xlib with minimal effort while maintaining 100% X11 API compatibility

### [ROADMAP.md](ROADMAP.md) - Project Timeline
- Detailed 17-week implementation plan
- Phase-by-phase breakdown
- Milestones and deliverables
- Success criteria for each phase
- Risk mitigation strategies
- Resource requirements

**Best for:** Project planning and tracking progress

### [BUILD_PLAN.md](BUILD_PLAN.md) - Dependencies & Build Strategy
- Complete dependency tree analysis
- Source code organization
- Build process and toolchain setup
- Linker scripts and memory layout
- Binary size and memory budgets
- Testing strategy

**Best for:** Setting up build system and understanding dependencies

---

## Technical Specifications

### [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) - Hardware Abstraction Layer API
- Complete HAL interface definition
- All function signatures and parameters
- Kdrive interface structures (KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs)
- Implementation priority guide
- Platform-specific notes
- Configuration options

**Best for:** Implementing the HAL or understanding hardware interface

### [VDI_HAL_MAPPING.md](VDI_HAL_MAPPING.md) - **OUTDATED** (GEM-XServer)
⚠️ **Note:** This document is outdated as we switched from GEM-XServer to TinyX/Kdrive.
- Kept for historical reference only
- See [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) for Kdrive interface instead
- See [X_SERVER_COMPARISON.md](X_SERVER_COMPARISON.md) for migration rationale

**Best for:** Quick lookup while porting code

### [X_SERVER_COMPARISON.md](X_SERVER_COMPARISON.md) - X Server Options Analysis
- Comparison between GEM-XServer and TinyX/Kdrive
- **Decision: Switched to TinyX/Kdrive** (Jan 22, 2026)
- Why Kdrive is better for bare-metal RISC-V
- Architecture comparison and effort estimates

**Best for:** Understanding why TinyX/Kdrive was chosen

### [MULTI_CORE_GUI_ANALYSIS.md](MULTI_CORE_GUI_ANALYSIS.md) - Multi-Core Architecture Analysis ⭐ NEW
- Feasibility analysis for running GUI on separate thread/core
- Layer separation analysis (Application, WMB, Tima OS)
- Halt/sleep mechanism design for power savings
- Inter-core communication (IPC) design
- HAL extensions required for interrupt-driven wake
- Recommended dual-core architecture

**Best for:** Understanding how to run WMB on a dedicated RISC-V core with halt capability

### [X11_CLIENT_SERVER_ANALYSIS.md](X11_CLIENT_SERVER_ANALYSIS.md) - Client-Server Layer Analysis ⚠️ UPDATED
- Detailed analysis of libX11 ↔ TinyX communication
- **⚠️ NOW SUPERSEDED** by XCB_STUB_ARCHITECTURE.md for implementation
- Historical reference for understanding X11 protocol internals

**Best for:** Background understanding (see XCB_STUB_ARCHITECTURE.md for implementation)

### [XCB_STUB_ARCHITECTURE.md](XCB_STUB_ARCHITECTURE.md) - Implementation Design ⭐⭐⭐ **START HERE**
- **SyncHandle() discovery**: Built-in hook for request execution
- **ZERO changes to libX11 API files** (172 files completely unchanged)
- Uses `dpy->synchandler` to intercept completed requests
- Detailed stub implementations for _XGetRequest, _XReply, etc.
- Phase-by-phase implementation plan
- **~650 lines of new code total**

**Best for:** THE recommended implementation approach for WMB

### [XOPENDISPLAY_ANALYSIS.md](XOPENDISPLAY_ANALYSIS.md) - XOpenDisplay Deep Dive
- Line-by-line analysis of XOpenDisplay() function
- 5 XCB dependencies identified
- Connection setup specifics

**Best for:** Implementing _XConnectXCB stub

### [XCB_DEPENDENCY_RISK_ASSESSMENT.md](XCB_DEPENDENCY_RISK_ASSESSMENT.md) - Risk Assessment
- Critical finding: ALL libX11 routes through xcb_io.c (885 lines)
- 172 files use GetReq macros → all go through ONE transport layer
- Historical analysis leading to stub architecture discovery

**Best for:** Understanding how we arrived at the stub architecture

**Best for:** Implementing the WMB transport layer with minimal risk

### [TINYX_MIGRATION.md](TINYX_MIGRATION.md) - Migration Complete ⭐
- **Confirmation that TinyX/Kdrive is checked out and verified**
- Summary of all documentation updates
- Quick reference for Kdrive interfaces
- Next steps for implementation
- Code organization overview

**Best for:** Confirmation of current status and what to do next

---

## Implementation Guides

### [GETTING_STARTED.md](GETTING_STARTED.md) - Step-by-Step Implementation
- **Working code examples you can copy**
- Complete HAL implementation examples
- Boot code and build system
- Test programs
- QEMU testing setup
- Debugging tips

**Best for:** Actually implementing the code - most practical document

### [PORTING_EXAMPLE.md](PORTING_EXAMPLE.md) - Detailed Porting Example
- Complete example of porting grph.c
- Before/after code comparisons
- Step-by-step transformation
- Testing the ported code
- Common issues and solutions

**Best for:** Understanding the porting process in detail

---

## How to Use This Documentation

### For Project Planning
1. Read [README.md](README.md) - Understand scope
2. Read [ROADMAP.md](ROADMAP.md) - See timeline
3. Read [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md) - Understand architecture
4. Read [BUILD_PLAN.md](BUILD_PLAN.md) - Plan build system

### For Implementation
1. Read [GETTING_STARTED.md](GETTING_STARTED.md) - Set up environment
2. Implement basic HAL using code examples
3. Keep [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) open for reference
4. Study [TINYX_MIGRATION.md](TINYX_MIGRATION.md) for TinyX-specific guidance
5. Review tinyx/kdrive/fbdev/ for reference implementation

### Quick Reference While Coding
- **Need HAL function signature?** → [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md)
- **Implementing Kdrive interface?** → [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md#kdrive-interface)
- **Need example?** → tinyx/kdrive/fbdev/ directory
- **Build system issue?** → [BUILD_PLAN.md](BUILD_PLAN.md)
- **Stuck on architecture?** → [TINYX_MIGRATION.md](TINYX_MIGRATION.md)

---

## Document Summary Table

| Document | Purpose | Length | When to Read |
|----------|---------|--------|--------------|
| [README.md](README.md) | Quick overview | Short | First |
| [GETTING_STARTED.md](GETTING_STARTED.md) | Practical code | Medium | When starting implementation |
| [ROADMAP.md](ROADMAP.md) | Timeline & milestones | Long | During planning |
| [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md) | System design | Long | When understanding system |
| [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) | API reference | Long | Keep open while coding |
| [BUILD_PLAN.md](BUILD_PLAN.md) | Build & dependencies | Long | Setting up build |
| [VDI_HAL_MAPPING.md](VDI_HAL_MAPPING.md) | Quick reference | Medium | Keep open while porting |
| [PORTING_EXAMPLE.md](PORTING_EXAMPLE.md) | Detailed example | Medium | When porting files |

---

## Key Concepts Explained

### What is Kdrive?
- **Kdrive** - Keith Packard's embedded X server framework
- **Key structures**: KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs
- **Equivalent to**: Like DDX (Device Dependent X) but lighter
- **Your task**: Implement ~10 interface functions for your hardware

### What is HAL?
- **HAL** (Hardware Abstraction Layer) - Your custom API
- **Purpose**: Isolate hardware-specific code
- **Benefit**: Easy to port to new platforms
- **Size**: ~30 functions to implement

### What is TinyX/Kdrive?
- Embedded X11 server implementation
- Originally by Keith Packard for thin clients
- Uses Kdrive interface for hardware abstraction
- **Your base**: Designed for framebuffer, clean HAL interface

### What is JWM?
- Lightweight X11 window manager
- Adds window decorations and menus
- Will be integrated as built-in client
- Needs libX11 (you'll create minimal version)

---

## Code Examples Index

### HAL Implementation Examples
- [GETTING_STARTED.md](GETTING_STARTED.md#step-3-implement-generic-framebuffer-hal) - Complete framebuffer HAL
- [GETTING_STARTED.md](GETTING_STARTED.md#step-4-implement-risc-v-timer) - RISC-V timer HAL
- [GETTING_STARTED.md](GETTING_STARTED.md#step-5-implement-input-stub) - Input HAL stub

### Boot Code Examples
- [GETTING_STARTED.md](GETTING_STARTED.md#step-6-create-boot-code) - RISC-V startup assembly
- [GETTING_STARTED.md](GETTING_STARTED.md#boot-init-c) - C initialization

### Build System Examples
- [GETTING_STARTED.md](GETTING_STARTED.md#step-7-create-minimal-build-system) - Complete Makefile
- [GETTING_STARTED.md](GETTING_STARTED.md#linker-ld) - Linker script
- [BUILD_PLAN.md](BUILD_PLAN.md#linker-script) - Advanced linker script

### Porting Examples
- [PORTING_EXAMPLE.md](PORTING_EXAMPLE.md#step-3-port-grphinit-function) - Port GrphInit()
- [VDI_HAL_MAPPING.md](VDI_HAL_MAPPING.md#code-pattern-examples) - Common patterns

---

## Troubleshooting Guide

### Build Issues
**Problem:** Can't compile
- Check: [BUILD_PLAN.md](BUILD_PLAN.md#toolchain-setup)
- Solution: Verify RISC-V toolchain installed

**Problem:** Linker errors
- Check: [GETTING_STARTED.md](GETTING_STARTED.md#linker-ld)
- Solution: Verify memory addresses in linker script

### Runtime Issues
**Problem:** Nothing displays
- Check: [GETTING_STARTED.md](GETTING_STARTED.md#debugging-without-serial-console)
- Solution: Verify framebuffer address

**Problem:** Crashes on boot
- Check: [PORTING_EXAMPLE.md](PORTING_EXAMPLE.md#common-issues-when-porting)
- Solution: Check BSS clearing, stack setup

### Porting Issues
**Problem:** Don't know how to replace VDI call
- Check: [VDI_HAL_MAPPING.md](VDI_HAL_MAPPING.md#graphics-operations-vdi)
- Solution: Find mapping in reference table

**Problem:** Colors are wrong
- Check: [VDI_HAL_MAPPING.md](VDI_HAL_MAPPING.md#issue-colors-wrong)
- Solution: Use hal_color_rgb() for conversion

---

## FAQ Locations

**Q: How long will this take?**
→ [README.md](README.md#estimated-effort) or [ROADMAP.md](ROADMAP.md#timeline)

**Q: How much memory do I need?**
→ [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md#memory-requirements) or [BUILD_PLAN.md](BUILD_PLAN.md#memory-budget)

**Q: What hardware do I need?**
→ [README.md](README.md#questions-to-answer-before-starting)

**Q: Can I test without real hardware?**
→ [GETTING_STARTED.md](GETTING_STARTED.md#step-9-build-and-test)

**Q: What VDI calls do I need to replace?**
→ [VDI_HAL_MAPPING.md](VDI_HAL_MAPPING.md#complete-replacement-checklist)

**Q: How do I port a specific file?**
→ [PORTING_EXAMPLE.md](PORTING_EXAMPLE.md)

**Q: What's the critical path?**
→ [ROADMAP.md](ROADMAP.md#phase-1-foundation-weeks-1-3)

**Q: Can I skip the window manager?**
→ [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md#strategy-for-single-binary) - Yes, Phase 1 is X server only

---

## Recommended Reading Order

### For Beginners
1. [README.md](README.md) - 15 minutes
2. [GETTING_STARTED.md](GETTING_STARTED.md) - 30 minutes
3. [VDI_HAL_MAPPING.md](VDI_HAL_MAPPING.md) - 20 minutes
4. Start coding!

### For Experienced Developers
1. [README.md](README.md) - Skim
2. [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md) - 30 minutes
3. [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) - Reference
4. Start implementation

### For Project Managers
1. [README.md](README.md) - Overview
2. [ROADMAP.md](ROADMAP.md) - Timeline
3. [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md#estimated-effort) - Effort estimates

---

## File Checklist

Use this to track your progress:

### Documentation (Complete)
- [x] README.md
- [x] ARCHITECTURE_ANALYSIS.md
- [x] HAL_SPECIFICATION.md
- [x] BUILD_PLAN.md
- [x] GETTING_STARTED.md
- [x] VDI_HAL_MAPPING.md
- [x] PORTING_EXAMPLE.md
- [x] ROADMAP.md
- [x] DOCUMENTATION_INDEX.md (this file)

### Implementation (Your tasks)
- [ ] HAL headers (from GETTING_STARTED.md)
- [ ] HAL framebuffer implementation
- [ ] HAL timer implementation
- [ ] HAL input implementation
- [ ] Boot code (start.S, init.c)
- [ ] Build system (Makefile, linker.ld)
- [ ] Port grph.c
- [ ] Port other X server files
- [ ] Minimal libX11
- [ ] JWM integration

---

## Contact and Contributions

This documentation set provides everything needed to implement X11 on bare-metal RISC-V. As you work through the project:

1. **Track your progress** using checklists in [ROADMAP.md](ROADMAP.md)
2. **Refer to specs** when implementing ([HAL_SPECIFICATION.md](HAL_SPECIFICATION.md))
3. **Follow examples** when porting ([PORTING_EXAMPLE.md](PORTING_EXAMPLE.md))
4. **Test incrementally** as described in each document

Good luck with your implementation!

---

## Quick Command Reference

```bash
# Get oriented
cat README.md

# Start implementation
cat GETTING_STARTED.md

# Look up HAL function
grep "hal_fb_fill_rect" HAL_SPECIFICATION.md

# Find VDI mapping
grep "v_bar" VDI_HAL_MAPPING.md

# Check timeline
grep "Week 1" ROADMAP.md

# Understand architecture
grep "Component Relationships" ARCHITECTURE_ANALYSIS.md

# Build system help
grep "Makefile" BUILD_PLAN.md

# See porting example
grep "GrphInit" PORTING_EXAMPLE.md
```

---

## Version Information

Documentation Version: 1.0
Created: 2025
Target Platform: RISC-V (RV32/RV64)
Base Software:
- TinyX/Kdrive (Embedded X11 server)
- JWM (Joe's Window Manager)
- libX11
- X11 protocol headers

---

## Next Steps

1. **Read [README.md](README.md)** if you haven't yet
2. **Follow [GETTING_STARTED.md](GETTING_STARTED.md)** to start coding
3. **Keep this index handy** for quick document lookup

You have everything you need to succeed! 🚀
