# X Server Comparison for Bare-Metal RISC-V

## ✅ DECISION MADE: Switched to TinyX/Kdrive

**Date**: January 22, 2026  
**Status**: TinyX/Kdrive successfully checked out and verified  
**Location**: `tinyx/kdrive/`

---

## Current Choice: GEM-XServer

### Pros ✅
- **Already checked out** - You have the code
- **Small codebase** (~30K lines core X server code)
- **Full X11 protocol** implementation
- **Analysis already done** - VDI/AES mapping documented
- **Simple architecture** - Designed for single-user embedded system
- **No dependencies** on Linux-specific features

### Cons ❌
- **Designed for GEM OS** - Assumes underlying window manager
- **VDI/AES abstraction** - Need to replace ~30 Atari-specific calls
- **Old codebase** - Not actively maintained since ~2000s
- **Cooperative windowing** - Originally designed to work WITH GEM window manager
- **Limited documentation** - Atari-specific, not much community support

### Architecture Issues for Bare-Metal:
```c
/* GEM-XServer makes assumptions like: */
wind_get(handle, WF_WORKXYWH, ...);  // Expects OS window manager
appl_init();                          // Expects GEM Application Environment
```

---

## Better Alternative: **TinyX (Kdrive)** ⭐ RECOMMENDED

TinyX (also called Kdrive) was specifically designed by Keith Packard for **embedded/framebuffer-only systems**.

### Pros ✅
- **Designed for framebuffer** - No OS window manager assumptions
- **Clean HAL abstraction** - Kdrive layer is the hardware interface
- **Modern codebase** - Part of X.org, still maintained
- **Better documentation** - Used in many embedded Linux systems
- **Smaller than full Xorg** (~100K lines, but cleaner architecture)
- **No network required** - Can build without TCP/IP stack
- **Proven on embedded** - Used in devices, set-top boxes, etc.

### Cons ❌
- **Need to check out** - Don't have it yet (~30MB git clone)
- **Slightly larger** than GEM-XServer
- **More features** to strip out (but clean separation)
- **Initial analysis needed** - Start from scratch

### Architecture for Bare-Metal:
```c
/* TinyX/Kdrive has clean layers: */

Application → Xlib → X Protocol
                        ↓
                   DIX (Device Independent X)
                        ↓
                   Kdrive Layer ← YOUR HAL GOES HERE
                        ↓
                   Framebuffer
```

**Kdrive interface is MUCH cleaner than VDI/AES:**
```c
/* Kdrive functions you need to implement (example): */
typedef struct _KdCardFuncs {
    Bool (*cardinit)(KdCardInfo *);
    Bool (*screeninit)(KdScreenInfo *);
    void (*preserve)(KdCardInfo *);
    void (*enable)(ScreenPtr);
    void (*disable)(ScreenPtr);
    void (*fini)(ScreenPtr);
} KdCardFuncs;

typedef struct _KdKeyboardFuncs {
    void (*Load)(void);
    int  (*Init)(void);
    void (*Fini)(void);
} KdKeyboardFuncs;
```

**Much more aligned with your needs** - it's designed for exactly this use case!

---

## Other Alternatives (Not Recommended)

### Xvfb (Virtual Framebuffer)
- ❌ **Too complex** - Full Xorg-based
- ❌ **Virtual only** - Not designed for real hardware
- ❌ **Overkill** for bare-metal

### nano-X / Microwindows
- ❌ **Not X11 protocol** - Different API entirely
- ❌ Violates requirement #6: "must be compatible with X11 client API"

### Writing from Scratch
- ❌ **Months of work** - X11 protocol is complex
- ❌ **Reinventing wheel** - Existing solutions available

---

## Recommendation: Switch to TinyX/Kdrive

### Why Switch NOW (before implementation):

1. **Save work later** - Kdrive interface is cleaner than VDI/AES mapping
2. **Better architecture** - Designed for framebuffer-only from the start
3. **No OS assumptions** - Built for embedded, not cooperative windowing
4. **Maintained code** - Still part of X.org, bug fixes available
5. **More examples** - Many embedded projects use Kdrive

### Effort Comparison:

**Stick with GEM-XServer:**
- ✅ No checkout needed
- ✅ Analysis done (VDI/AES mapping)
- ❌ Replace ~30 VDI/AES calls
- ❌ Fight OS window manager assumptions
- ❌ Debug Atari-specific quirks
- **Total: ~4-6 weeks porting**

**Switch to TinyX/Kdrive:**
- ❌ Checkout code (30 minutes)
- ❌ Analyze Kdrive interface (1-2 days)
- ✅ Implement ~10 clean HAL functions
- ✅ No OS assumptions to remove
- ✅ Better aligned architecture
- **Total: ~3-4 weeks porting** (less work, cleaner result)

### Switching Cost: ~2 days
- Checkout TinyX source
- Read Kdrive documentation
- Map Kdrive functions to HAL
- Update documentation

### Long-term Benefits:
- ✅ Easier maintenance (upstream bug fixes)
- ✅ Better performance (designed for this)
- ✅ More community support
- ✅ Cleaner codebase

---

## Detailed Comparison: Code Examples

### GEM-XServer Approach (Current)

**Window Drawing:**
```c
/* GEM-XServer (in grph.c) */
void GrphDrawRect(WindowPtr win, int x, int y, int w, int h) {
    /* Setup VDI workstation */
    short pxy[4];
    pxy[0] = x;
    pxy[1] = y;
    pxy[2] = x + w - 1;
    pxy[3] = y + h - 1;
    
    /* VDI call - need to replace */
    v_bar(vdi_handle, pxy);  // ← Atari-specific
}

/* You need to replace with: */
void GrphDrawRect(WindowPtr win, int x, int y, int w, int h) {
    hal_gfx_fill_rect(x, y, w, h, win->background);
}
```

**Event Handling:**
```c
/* GEM-XServer (in event.c) */
void WaitForEvent(void) {
    short msg[8];
    short mx, my, mb, ks;
    
    /* AES call - need to replace */
    evnt_multi(MU_MESAG | MU_BUTTON | MU_KEYBD,
               1, 1, 1,
               0, 0, 0, 0, 0,
               0, 0, 0, 0, 0,
               msg, 0, &mx, &my, &mb, &ks);  // ← Complex Atari call
    
    /* Process events... */
}

/* You need to replace with: */
void WaitForEvent(void) {
    hal_event_t event;
    hal_input_wait(&event, -1);
    /* Process events... */
}
```

**Issues:**
- VDI calls scattered throughout codebase
- GEM assumptions (workstations, handles, etc.)
- Need to map complex AES semantics

---

### TinyX/Kdrive Approach (Recommended)

**Window Drawing:**
```c
/* Kdrive (in kdrive/src/kdrive.c) */
void KdScreenInitialize(ScreenPtr pScreen) {
    /* Your HAL integration point */
    pScreen->CreatePixmap = fbCreatePixmap;
    pScreen->DestroyPixmap = fbDestroyPixmap;
    /* ... */
}

/* Your HAL implementation: */
static Bool myCardInit(KdCardInfo *card) {
    hal_fb_info_t fb_info;
    hal_fb_init(&fb_info);
    
    card->frameBuffer = fb_info.framebuffer;
    card->width = fb_info.width;
    card->height = fb_info.height;
    return TRUE;
}
```

**Event Handling:**
```c
/* Kdrive (in kdrive/linux/keyboard.c) - example */
static void KdEnqueueKeyboardEvent(unsigned char scancode) {
    xEvent event;
    /* Build X event */
    event.u.keyButtonPointer.time = GetTimeInMillis();
    /* Enqueue to X server */
    mieqEnqueue(&event);
}

/* Your HAL integration: */
void myKbdRead(void) {
    hal_event_t hal_event;
    if (hal_input_poll(&hal_event) > 0) {
        if (hal_event.type == HAL_EVENT_KEY_PRESS) {
            KdEnqueueKeyboardEvent(hal_event.data.key.keycode);
        }
    }
}
```

**Advantages:**
- Clear integration points
- No Atari-specific assumptions
- Designed for framebuffer from scratch
- Just implement interface functions

---

## Kdrive Directory Structure

```
xserver/
├── dix/              # Device-Independent X (shared)
├── mi/               # Machine-Independent layer (shared)
├── fb/               # Framebuffer implementation (use as-is)
└── hw/kdrive/        # Kdrive-specific
    ├── src/          # Kdrive core (minimal changes needed)
    │   ├── kdrive.c  # Main driver interface
    │   ├── kinput.c  # Input handling
    │   └── kcmap.c   # Colormap
    ├── fbdev/        # Linux framebuffer example (reference)
    └── YOUR_HAL/     # Your implementation
        ├── hal.c     # Card functions
        ├── kbd.c     # Keyboard
        └── mouse.c   # Mouse
```

**What you implement:** Just the `YOUR_HAL/` directory (~1000 lines)

---

## Migration Path

### Option A: Stick with GEM-XServer
1. Continue with current analysis ✅ Done
2. Implement HAL for VDI/AES replacement (4 weeks)
3. Remove GEM window manager cooperation (1 week)
4. Test and debug (2 weeks)
**Total: 7 weeks**

### Option B: Switch to TinyX/Kdrive ⭐ RECOMMENDED
1. Checkout TinyX source (30 mins)
2. Study Kdrive interface (2 days)
3. Create new HAL_KDRIVE_SPECIFICATION.md (1 day)
4. Implement Kdrive HAL layer (3 weeks)
5. Test and debug (1 week)
**Total: 5 weeks** (2 weeks saved, cleaner result)

---

## Specific Kdrive Resources

### Checkout:
```bash
git clone https://gitlab.freedesktop.org/xorg/xserver.git
cd xserver
git checkout xserver-1.20-branch  # Stable branch
```

**Kdrive is in:** `hw/kdrive/`

### Documentation:
- [Kdrive Overview](https://www.x.org/wiki/Development/Documentation/Kdrive/)
- Many embedded projects use it (Raspberry Pi, etc.)
- Clean examples in `hw/kdrive/fbdev/` (Linux framebuffer driver)

### Size:
- Full X server: ~800K lines
- Kdrive portion: ~20K lines
- Your implementation: ~1K lines

---

## Final Recommendation

**Switch to TinyX/Kdrive** if:
- ✅ Want cleaner architecture
- ✅ Want to save time long-term
- ✅ Haven't started GEM-XServer implementation yet
- ✅ Want better maintained code

**Stick with GEM-XServer** if:
- ✅ Really attached to analyzed code
- ✅ Want smallest possible codebase
- ✅ Comfortable fighting OS assumptions
- ❌ (But honestly, I'd still recommend switching)

---

## Action Items if Switching

1. **Checkout TinyX** (today - 30 mins)
2. **Review Kdrive examples** (tomorrow - 2 hours)
3. **Create HAL_KDRIVE_SPECIFICATION.md** (2 days)
4. **Update ARCHITECTURE_ANALYSIS.md** with Kdrive (1 day)
5. **Begin implementation** (3 weeks)

**Net time saved: ~2 weeks**
**Code quality: Much better**
**Maintenance: Much easier**

---

## My Strong Recommendation

**Switch to TinyX/Kdrive.** 

GEM-XServer was designed for a cooperative windowing environment (running under GEM OS). You're building a standalone WMB where the X server IS the window manager. Kdrive was designed exactly for this case - framebuffer-only, no OS windowing assumptions, embedded systems.

The 2 days spent switching now will save you 2-3 weeks of fighting architectural mismatches later.
