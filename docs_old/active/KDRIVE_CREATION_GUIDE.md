# Creating a Kdrive Backend — Comprehensive Guide

**Last updated**: 2026-07-13
**Scope**: How to create a new Kdrive backend for the TinyX server in this
workspace. Uses the existing `tinyx/kdrive/tima/` backend as the reference
implementation, documents its current state, and lists what is still missing.

---

## Table of Contents

1. [What Kdrive Is](#1-what-kdrive-is)
2. [Architecture in This Repository](#2-architecture-in-this-repository)
3. [The Four Kdrive Interfaces](#3-the-four-kdrive-interfaces)
4. [Directory Layout of a Backend](#4-directory-layout-of-a-backend)
5. [Step‑by‑Step: Build a New Backend](#5-stepbystep-build-a-new-backend)
   - 5.1 [Header: private data + prototypes](#51-header-private-data--prototypes)
   - 5.2 [Framebuffer / card driver](#52-framebuffer--card-driver)
   - 5.3 [Keyboard driver](#53-keyboard-driver)
   - 5.4 [Mouse / pointer driver](#54-mouse--pointer-driver)
   - 5.5 [OS layer + DDX entry points](#55-os-layer--ddx-entry-points)
6. [HAL Contract (`hal_drivers.h`)](#6-hal-contract-hal_driversh)
7. [Build Integration](#7-build-integration)
8. [Reference Backend Status — `tinyx/kdrive/tima/`](#8-reference-backend-status--tinyxkdrivetima)
9. [Known Gaps and TODOs](#9-known-gaps-and-todos)
10. [Porting Checklist for a New Platform](#10-porting-checklist-for-a-new-platform)
11. [Testing & Validation](#11-testing--validation)

---

## 1. What Kdrive Is

Kdrive is TinyX's built‑in Hardware Abstraction Layer. Instead of writing a
separate porting layer, you implement Kdrive's four well‑defined vtables:

| Vtable            | Purpose                                    |
|-------------------|--------------------------------------------|
| `KdCardFuncs`     | Graphics card / framebuffer lifecycle      |
| `KdKeyboardFuncs` | Keyboard init / LEDs / bell                |
| `KdMouseFuncs`    | Pointer init / cleanup                     |
| `KdOsFuncs`       | OS‑level init, enable/disable, event poll  |

TinyX drives the entire X server on top of these four structures. Window
management, region calculations, request dispatch, and rendering (via the `fb`
library) are all handled by TinyX itself. Your backend is a **dumb hardware
plumbing layer** — it provides a framebuffer pointer and pushes raw input
events into Kdrive's queues.

Key files inside TinyX to read while porting:

- [tinyx/kdrive/src/kdrive.h](../../tinyx/kdrive/src/kdrive.h) — vtable
  definitions, `KdCardInfo`, `KdScreenInfo`, `KdEnqueueKeyboardEvent`,
  `KdEnqueueMouseEvent`.
- [tinyx/kdrive/src/kdrive.c](../../tinyx/kdrive/src/kdrive.c) — lifecycle,
  `KdInitOutput`, `KdInitInput`, `KdOsInit`, `KdCardInfoAdd`.
- [tinyx/kdrive/src/kinput.c](../../tinyx/kdrive/src/kinput.c) — input
  plumbing.
- [tinyx/kdrive/fbdev/fbdev.c](../../tinyx/kdrive/fbdev/fbdev.c) — canonical
  reference backend (Linux `/dev/fb0`).

---

## 2. Architecture in This Repository

This repo has a **two‑layer split** on top of Kdrive so that the same Kdrive
backend runs on both hosted SDL builds and the bare‑metal RISC‑V (Tima OS)
target:

```
+----------------------------------------------------------------------+
| TinyX X11 server  (dix / mi / fb / render / randr / ...)             |
+----------------------------------------------------------------------+
| Kdrive core   (tinyx/kdrive/src/)                                    |
+----------------------------------------------------------------------+
| Kdrive backend  --- YOU IMPLEMENT THIS ---                           |
|   tinyx/kdrive/tima/  (platform-independent, calls hal_drivers.h)    |
+----------------------------------------------------------------------+
| HAL (hardware abstraction)                                           |
|   framework/sdl_drivers/     -> hosted, uses SDL2                    |
|   framework/riscv_drivers/   -> target, MMIO/GPIO (NOT YET WRITTEN)  |
+----------------------------------------------------------------------+
| Actual hardware (SDL window OR bare-metal LCD + keypad + mouse)      |
+----------------------------------------------------------------------+
```

**Design rule**: nothing under `tinyx/kdrive/tima/` may include SDL, Linux, or
any platform header. It only sees TinyX headers and
[framework/sdl_drivers/hal_drivers.h](../../framework/sdl_drivers/hal_drivers.h).
Platform details live below the HAL.

---

## 3. The Four Kdrive Interfaces

Full definitions live in [tinyx/kdrive/src/kdrive.h](../../tinyx/kdrive/src/kdrive.h).
Summarised here:

### 3.1 `KdCardFuncs`

```c
typedef struct _KdCardFuncs {
    Bool (*cardinit)(KdCardInfo *);         /* detect / init framebuffer  */
    Bool (*scrinit)(KdScreenInfo *);        /* fill in screen params      */
    Bool (*initScreen)(ScreenPtr);          /* attach to X ScreenRec      */
    Bool (*finishInitScreen)(ScreenPtr);    /* shadow / late setup        */
    Bool (*createRes)(ScreenPtr);           /* create shadow/RANDR bits   */
    void (*preserve)(KdCardInfo *);         /* save HW state              */
    Bool (*enable)(ScreenPtr);              /* take over the display      */
    Bool (*dpms)(ScreenPtr, int);           /* power management           */
    void (*disable)(ScreenPtr);             /* release the display        */
    void (*restore)(KdCardInfo *);          /* restore saved HW state     */
    void (*scrfini)(KdScreenInfo *);        /* free per-screen data       */
    void (*cardfini)(KdCardInfo *);         /* free per-card data         */
    /* cursor + accel hooks — leave NULL for software cursor / no accel */
    void (*getColors)(ScreenPtr, int, xColorItem *);
    void (*putColors)(ScreenPtr, int, xColorItem *);
} KdCardFuncs;
```

Minimum required on bare‑metal: `cardinit`, `scrinit`, `initScreen`,
`finishInitScreen`, `createRes`, `enable`, `disable`, `scrfini`, `cardfini`,
`getColors`, `putColors`. Leave `initCursor`, `initAccel`, etc. as `NULL`.

### 3.2 `KdKeyboardFuncs`

```c
typedef struct _KdKeyboardFuncs {
    void (*Load)(void);                /* populate kdKeymap[]           */
    int  (*Init)(void);                /* init HW; return 1 on success  */
    void (*Leds)(int);                 /* set LED mask                  */
    void (*Bell)(int, int, int);       /* volume, pitch, duration       */
    void (*Fini)(void);
    int  LockLed;                      /* LED number for Caps Lock      */
} KdKeyboardFuncs;
```

All fields must be provided; unused ones can be no‑op stubs. `Load` must fill
the global `kdKeymap[KD_MAX_LENGTH * KD_MAX_WIDTH]` array (see
[tinyx/kdrive/src/kkeymap.h](../../tinyx/kdrive/src/kkeymap.h)).

### 3.3 `KdMouseFuncs`

```c
typedef struct _KdMouseFuncs {
    Bool (*Init)(void);
    void (*Fini)(void);
} KdMouseFuncs;
```

The actual polling loop lives outside this vtable — it runs from
`KdOsFuncs.pollEvents`.

### 3.4 `KdOsFuncs`

```c
typedef struct _KdOsFuncs {
    int  (*Init)(void);
    void (*Enable)(void);
    Bool (*SpecialKey)(KeySym);        /* handle VT switches etc.       */
    void (*Disable)(void);
    void (*Fini)(void);
    void (*pollEvents)(void);          /* drain HW into KdEnqueue*      */
} KdOsFuncs;
```

`pollEvents` is called from the TinyX main loop; it must be non‑blocking.

---

## 4. Directory Layout of a Backend

Follow the layout used by [tinyx/kdrive/tima/](../../tinyx/kdrive/tima/):

```
tinyx/kdrive/<name>/
├── <name>_kdrive.h            <-- private types + prototypes + extern vtables
├── <name>_kdrive_fb.c         <-- KdCardFuncs implementation
├── <name>_kdrive_kbd.c        <-- KdKeyboardFuncs + <name>_KeyboardPoll()
├── <name>_kdrive_mouse.c      <-- KdMouseFuncs + <name>_MousePoll()
└── <name>_kdrive_system.c     <-- KdOsFuncs + InitCard/InitOutput/InitInput/
                                    OsVendorInit + ddx* stubs
```

The five external symbols the X server links against (satisfied by
`_system.c`):

| Symbol           | Where TinyX calls it | Purpose                              |
|------------------|----------------------|--------------------------------------|
| `InitCard`       | `main()`             | Register your `KdCardFuncs`          |
| `InitOutput`     | `main()`             | Delegate to `KdInitOutput`           |
| `InitInput`      | `main()`             | Register keyboard + mouse vtables    |
| `OsVendorInit`   | `os/utils.c`         | Delegate to `KdOsInit`               |
| `ddxUseMsg`, `ddxProcessArgument`, `ddxGiveUp`, `AbortDDX` | various | DDX contract |

---

## 5. Step‑by‑Step: Build a New Backend

The reference implementation is in
[tinyx/kdrive/tima/](../../tinyx/kdrive/tima/). Copy it and rename to
kick‑start a new backend. Below, each subsection points at the exact tima file
that shows the pattern.

### 5.1 Header: private data + prototypes

See [tima_kdrive.h](../../tinyx/kdrive/tima/tima_kdrive.h#L1-L98).

Two private structs must exist:

- **Card‑private** (stored in `KdCardInfo::driver`) — framebuffer base,
  dimensions, bpp, pitch, RGB masks/offsets.
- **Screen‑private** (stored in `KdScreenInfo::driver`) — rotation and shadow
  flag; typically small.

Declare the three `extern const` vtables (`<name>_Funcs`,
`<name>_KeyboardFuncs`, `<name>_MouseFuncs`) plus the two poll functions
(`<name>_KeyboardPoll`, `<name>_MousePoll`).

### 5.2 Framebuffer / card driver

See [tima_kdrive_fb.c](../../tinyx/kdrive/tima/tima_kdrive_fb.c).

Implement each `KdCardFuncs` slot. The critical ones:

**`cardinit`** ([tima_kdrive_fb.c#L31-L104](../../tinyx/kdrive/tima/tima_kdrive_fb.c#L31-L104))
— call the HAL init, allocate your card‑private struct, populate width,
height, bpp, pitch, and RGB masks (32bpp → 0xFF0000/0x00FF00/0x0000FF; 16bpp
→ RGB565 masks), and store it in `card->driver`.

**`scrinit`** ([tima_kdrive_fb.c#L116-L166](../../tinyx/kdrive/tima/tima_kdrive_fb.c#L116-L166))
— fill in `KdScreenInfo`: dimensions, refresh rate, physical mm, and the
`screen->fb.*` fields (`depth`, `bitsPerPixel`, `byteStride`, `pixelStride`,
`frameBuffer`, `visuals`, R/G/B masks). Also set `memory_base`, `memory_size`,
`off_screen_base`.

**`finishInitScreen`** — call `shadowSetup(pScreen)` if you need rotation
support (rotates via the `miext/shadow` package).

**`createRes`** — if the shadow flag is set, register the shadow update
routine (`KdShadowSet(pScreen, randr, shadowUpdatePacked, NULL)`).

**`enable` / `disable` / `dpms`** — platform display power sequencing. On
bare‑metal without a display controller, `enable` may just `memset` the
framebuffer to 0 and `disable` may be a no‑op.

**`getColors` / `putColors`** — for TrueColor visuals these are essentially
no‑ops; keep the tima versions verbatim.

Finish the file with the vtable definition:

```c
const KdCardFuncs tima_Funcs = {
    .cardinit         = tima_CardInit,
    .scrinit          = tima_ScreenInit,
    .initScreen       = tima_InitScreen,
    .finishInitScreen = tima_FinishInitScreen,
    .createRes        = tima_CreateResources,
    .preserve         = tima_Preserve,
    .enable           = tima_Enable,
    .dpms             = tima_DPMS,
    .disable          = tima_Disable,
    .restore          = tima_Restore,
    .scrfini          = tima_ScreenFini,
    .cardfini         = tima_CardFini,
    .initCursor       = NULL,   /* software cursor */
    .enableCursor     = NULL,
    .disableCursor    = NULL,
    .finiCursor       = NULL,
    .recolorCursor    = NULL,
    .initAccel        = NULL,   /* no 2D accel */
    .enableAccel      = NULL,
    .disableAccel     = NULL,
    .finiAccel        = NULL,
    .getColors        = tima_GetColors,
    .putColors        = tima_PutColors,
};
```
See [tima_kdrive_fb.c#L410-L436](../../tinyx/kdrive/tima/tima_kdrive_fb.c#L410-L436).

### 5.3 Keyboard driver

See [tima_kdrive_kbd.c](../../tinyx/kdrive/tima/tima_kdrive_kbd.c).

Two responsibilities:

1. **Provide a `KdKeyboardFuncs` vtable** including a `Load` function that
   fills `kdKeymap`. In tima this uses ASCII values as scancodes so the
   mapping is trivial:
   ```c
   #define SET_KEY(sc, k0, k1) do { \
       if ((sc) < KD_MAX_LENGTH) { \
           KeySym *k = &kdKeymap[(sc) * KD_MAX_WIDTH]; \
           k[0] = (k0); k[1] = (k1); \
           k[2] = NoSymbol; k[3] = NoSymbol; \
       } \
   } while(0)
   ```
   Modifier keys use synthetic scancodes 112–120 (`SC_LSHIFT`, `SC_LCTRL`,
   …). Set `kdMinScanCode = 0; kdMaxScanCode = 127;` at file scope.

2. **Implement `<name>_KeyboardPoll()`** which drains the HAL by calling
   `keypad_driver_has_data()` / `keypad_driver_read_data()` and pushes events
   into TinyX with `KdEnqueueKeyboardEvent(scancode, is_up)`.

The HAL keyboard protocol is a stream of `(command, data)` pairs:

| Cmd    | Meaning              | Follow‑up byte           |
|--------|----------------------|--------------------------|
| `0x01` | `CMD_MODIFIER`       | new modifier mask        |
| `0x02` | `CMD_KEY_DOWN`       | key data (ASCII)         |
| `0x03` | `CMD_KEY_UP`         | key data (ASCII)         |

Modifier changes are diffed against a tracked `current_modifiers` so that
`KdEnqueueKeyboardEvent` gets separate press/release events per changed bit
— see
[tima_kdrive_kbd.c#L201-L235](../../tinyx/kdrive/tima/tima_kdrive_kbd.c#L201-L235).

### 5.4 Mouse / pointer driver

See [tima_kdrive_mouse.c](../../tinyx/kdrive/tima/tima_kdrive_mouse.c).

Provide `Init`/`Fini` and a `<name>_MousePoll()` that decodes the HAL protocol
and calls
`KdEnqueueMouseEvent(kdMouseInfo, flags | KD_MOUSE_DELTA, x, y)`.

HAL mouse commands:

| Cmd    | Meaning              | Follow‑up byte              |
|--------|----------------------|-----------------------------|
| `0x04` | `CMD_MOUSE_X`        | signed 8‑bit dx             |
| `0x05` | `CMD_MOUSE_Y`        | signed 8‑bit dy             |
| `0x06` | `CMD_MOUSE_Z`        | z (absolute / touch)        |
| `0x07` | `CMD_MOUSE_BUTTON`   | button id (`MOUSE_BUTTON_*`) |
| `0x08` | `CMD_MOUSE_WHEEL_X`  | signed 8‑bit                |
| `0x09` | `CMD_MOUSE_WHEEL_Y`  | signed 8‑bit                |

Clamp the accumulated `(x, y)` to the LCD dimensions via
`lcd_driver_get_width()` / `lcd_driver_get_height()`.

⚠️ The current tima mouse code has known bugs — see
[§9](#9-known-gaps-and-todos).

### 5.5 OS layer + DDX entry points

See [tima_kdrive_system.c](../../tinyx/kdrive/tima/tima_kdrive_system.c).

Provide the `KdOsFuncs` vtable whose `pollEvents` simply calls
`<name>_KeyboardPoll()` and `<name>_MousePoll()`, then implement the six DDX
entry points TinyX links against:

```c
void InitCard(char *name)      { KdCardInfoAdd(&tima_Funcs, NULL); }
void InitOutput(ScreenInfo *s, int argc, char **argv)
                               { KdInitOutput(s, argc, argv); }
void InitInput(int argc, char **argv)
                               { KdInitInput(&tima_MouseFuncs,
                                             &tima_KeyboardFuncs); }
void OsVendorInit(void)        { KdOsInit(&tima_OsFuncs); }

void ddxUseMsg(void)                                        { }
int  ddxProcessArgument(int argc, char **argv, int i)       { return 0; }
void ddxGiveUp(void)                                        { }
void AbortDDX(void)                                         { }
```

`SpecialKey` should normally return `FALSE` so all keys pass through to
clients.

---

## 6. HAL Contract (`hal_drivers.h`)

The backend never talks to hardware directly. It calls the flat‑C API in
[framework/sdl_drivers/hal_drivers.h](../../framework/sdl_drivers/hal_drivers.h):

```c
/* LCD / framebuffer */
void            lcd_driver_init(void);
void            lcd_driver_deinit(void);
uint16_t        lcd_driver_get_width(void);
uint16_t        lcd_driver_get_height(void);
uint32_t        lcd_driver_get_bits_per_pixel(void);
PIXEL_TYPE_T *  lcd_driver_get_framebuffer(void);
void            lcd_driver_clear_screen(void);
void            lcd_driver_update_window(void);
void            lcd_driver_do_scroll(uint16_t h);
void            lcd_driver_set_pixel(uint16_t x, uint16_t y, PIXEL_TYPE_T c);
void            lcd_driver_show_bitmap_rgb(...);
void            lcd_driver_fill_colour(...);

/* Keypad */
void    keypad_driver_init(void);
bool_t  keypad_driver_has_data(void);
uint8_t keypad_driver_read_data(void);

/* Mouse */
void    mouse_driver_init(uint8_t index);
bool_t  mouse_driver_has_data(uint8_t index);
uint8_t mouse_driver_read_data(uint8_t index);
void    mouse_driver_process(void);
void    mouse_driver_write_command(uint8_t index, uint8_t *buf, uint32_t max);

/* Protocol constants — CMD_*, KEY_DATA_*, MOUSE_BUTTON_* */
```

A new hardware platform is added by writing a new implementation of this
header (e.g. `framework/riscv_drivers/`) — the Kdrive backend is unchanged.

---

## 7. Build Integration

The hosted (SDL) build wires everything together from
[framework/CMakeLists.txt](../../framework/CMakeLists.txt#L38-L94).
Key points to copy when adding a new backend:

```cmake
set(SOURCE_FILES
    ...
    ../tinyx/kdrive/tima/tima_kdrive_fb.c
    ../tinyx/kdrive/tima/tima_kdrive_kbd.c
    ../tinyx/kdrive/tima/tima_kdrive_mouse.c
    ../tinyx/kdrive/tima/tima_kdrive_system.c
)

include_directories(BEFORE
    ./sdl_drivers                 # hal_drivers.h + PIXEL_TYPE_T
    ../tinyx/kdrive/tima          # backend header
    ../X11                        # X11 core headers
    ../tinyx/kdrive/src           # KDRIVE_INC
    ../tinyx/kdrive/linux         # KDRIVE_OS_INC
    ../tinyx/include              # CORE_INCS
    ../tinyx/mi
    ../tinyx/fb
    ../tinyx/Xext
    ../tinyx/miext/shadow
    ../tinyx/miext/damage
    ../tinyx/render
    ../tinyx/randr
    ../tinyx/xfixes
    ../tinyx/damageext
    ../tinyx/os
    ../tinyx/dix
)

add_definitions(
    -D_USE_SCREEN_WIDTH=800
    -D_USE_SCREEN_HEIGHT=480
    -DTIMA_USE_CONFIG
    -D_USE_LINUX
    -DPIXEL_TYPE_T=uint16_t
)
```

Notes:

- `PIXEL_TYPE_T` is set at build time. Match it to `lcd_driver_get_bits_per_pixel()`
  (16 → `uint16_t`, 32 → `uint32_t`).
- Leave `HAVE_CONFIG_H` **undefined** in the standalone framework build so
  the backend does not try to `#include <kdrive-config.h>` (that header is
  produced by the autotools tree, not the CMake tree).
- For the target build, swap `sdl_drivers` for the target HAL directory and
  keep everything else identical.

---

## 8. Reference Backend Status — `tinyx/kdrive/tima/`

Files that already exist:

| File                                                                                                | Status | Notes                                              |
|-----------------------------------------------------------------------------------------------------|--------|----------------------------------------------------|
| [tima_kdrive.h](../../tinyx/kdrive/tima/tima_kdrive.h)                                              | ✅     | Types + vtable externs, no platform includes       |
| [tima_kdrive_fb.c](../../tinyx/kdrive/tima/tima_kdrive_fb.c)                                        | ✅     | Full `KdCardFuncs`, shadow ready, 16/32 bpp        |
| [tima_kdrive_kbd.c](../../tinyx/kdrive/tima/tima_kdrive_kbd.c)                                      | ✅     | ASCII scancodes, full modifier tracking            |
| [tima_kdrive_mouse.c](../../tinyx/kdrive/tima/tima_kdrive_mouse.c)                                  | ⚠️     | See gaps below                                     |
| [tima_kdrive_system.c](../../tinyx/kdrive/tima/tima_kdrive_system.c)                                | ✅     | `KdOsFuncs` + all DDX externs                      |

HAL implementation:
- Hosted SDL: [framework/sdl_drivers/](../../framework/sdl_drivers/) — present.
- Bare‑metal RISC‑V: `framework/riscv_drivers/` — **not yet written**.

Design is platform‑independent: no `#include <SDL...>` or `<linux/...>` in
any `tinyx/kdrive/tima/*` source. All platform work is delegated to
`hal_drivers.h`.

---

## 9. Known Gaps and TODOs

These issues in the reference backend must be addressed before final
deployment.

### 9.1 Mouse polling — control flow bug

In [tima_kdrive_mouse.c](../../tinyx/kdrive/tima/tima_kdrive_mouse.c#L64-L110):

- `dx`, `dy`, `flags` are **not initialised** before use.
- The `CMD_MOUSE_X` and `CMD_MOUSE_Y` branches use `continue`, which jumps
  back to the top of the `while` — so the position update / enqueue at the
  bottom is only reached for `CMD_MOUSE_BUTTON` / `CMD_MOUSE_Z` /
  `CMD_MOUSE_WHEEL_Y`. In that path `flags` is used without ever being
  cleared, leaking stale button state across events.
- The `flags |= KD_BUTTON_*` cascade never distinguishes press from release.
- `mouse_driver_init(0)` is **never called** — `tima_MouseInit()` only
  toggles the local `mouse_state.initialized` flag.

Recommended shape (drop‑in replacement for the polling loop):

```c
void tima_MousePoll(void)
{
    extern KdMouseInfo *kdMouseInfo;
    int dx = 0, dy = 0;
    unsigned long flags = 0;
    uint8_t id = mouse_state.mouse_driver_id;

    if (!mouse_state.initialized) return;

    while (mouse_driver_has_data(id)) {
        uint8_t cmd  = mouse_driver_read_data(id);
        uint8_t data;
        switch (cmd) {
        case CMD_MOUSE_X:
            dx = (int8_t)mouse_driver_read_data(id); break;
        case CMD_MOUSE_Y:
            dy = (int8_t)mouse_driver_read_data(id); break;
        case CMD_MOUSE_BUTTON:
            data = mouse_driver_read_data(id);
            if (data == MOUSE_BUTTON_LEFT)   flags |= KD_BUTTON_1;
            if (data == MOUSE_BUTTON_MIDDLE) flags |= KD_BUTTON_2;
            if (data == MOUSE_BUTTON_RIGHT)  flags |= KD_BUTTON_3;
            if (data == MOUSE_BUTTON_X1)     flags |= KD_BUTTON_4;
            if (data == MOUSE_BUTTON_X2)     flags |= KD_BUTTON_5;
            break;
        default:
            (void)mouse_driver_read_data(id); break;
        }

        /* When we have at least one axis or a button, dispatch */
        mouse_state.x += dx;
        mouse_state.y += dy;
        if (mouse_state.x < 0) mouse_state.x = 0;
        if (mouse_state.y < 0) mouse_state.y = 0;
        if (mouse_state.x >= lcd_driver_get_width())
            mouse_state.x = lcd_driver_get_width() - 1;
        if (mouse_state.y >= lcd_driver_get_height())
            mouse_state.y = lcd_driver_get_height() - 1;

        KdEnqueueMouseEvent(kdMouseInfo, flags | KD_MOUSE_DELTA,
                            mouse_state.x, mouse_state.y);
        dx = dy = 0; flags = 0;
    }
}
```

And add the missing init:

```c
static Bool tima_MouseInit(void)
{
    mouse_driver_init(0);            /* <-- missing today */
    mouse_state.initialized = TRUE;
    mouse_state.x = lcd_driver_get_width()  / 2;
    mouse_state.y = lcd_driver_get_height() / 2;
    mouse_state.buttons = 0;
    mouse_state.mouse_driver_id = 0;
    return TRUE;
}
```

### 9.2 Missing button‑release tracking

The current code ORs button bits into `flags` but never clears them on
release. A real implementation must remember previous button state and pass
release deltas to `KdEnqueueMouseEvent`.

### 9.3 Wheel events are dropped

`CMD_MOUSE_WHEEL_X` / `CMD_MOUSE_WHEEL_Y` are consumed but not translated to
X button 4/5 events.

### 9.4 No target HAL

`framework/riscv_drivers/` does not exist yet. To run on Tima OS hardware,
create it with the same `hal_drivers.h` symbols implemented against MMIO
framebuffer + GPIO/SPI input.

### 9.5 DPMS / cursor / accel are stubs

`tima_DPMS` is empty. Hardware cursor and 2D accel hooks are `NULL`. This is
acceptable for a first bring‑up but should be revisited if the target has HW
support.

### 9.6 Keyboard LEDs & bell are no‑ops

`tima_KeyboardLeds` and `tima_KeyboardBell` ignore their arguments. Wire them
to hardware when available.

### 9.7 `ddxProcessArgument` accepts nothing

The DDX layer currently ignores `-fb` / `-fbsize` even though `ddxUseMsg`
advertises them. Either implement the parsing or remove the help text.

---

## 10. Porting Checklist for a New Platform

To add a **new HAL backend** (target hardware) without touching the Kdrive
backend:

- [ ] Create `framework/<plat>_drivers/`.
- [ ] Implement every symbol in
      [hal_drivers.h](../../framework/sdl_drivers/hal_drivers.h).
- [ ] Provide a `types.h` compatible with the one in
      [framework/types.h](../../framework/types.h) (bool_t, uint8_t, PIXEL_TYPE_T…).
- [ ] Add a CMake option / toolchain file that selects
      `<plat>_drivers/*.c` instead of `sdl_drivers/*.c` in the sources list.
- [ ] Rebuild — Kdrive sources are unchanged.

To add a **new Kdrive backend** (e.g. a different rendering strategy):

- [ ] `cp -r tinyx/kdrive/tima tinyx/kdrive/<name>` and rename files/symbols.
- [ ] Update the `SOURCE_FILES` and `include_directories` in
      [framework/CMakeLists.txt](../../framework/CMakeLists.txt) (or create a
      new CMakeLists for it).
- [ ] Ensure exactly one backend defines `InitCard`, `InitOutput`,
      `InitInput`, `OsVendorInit`, `ddx*` in the final link.

---

## 11. Testing & Validation

Minimum smoke test after wiring a new backend:

1. **Link**: `cmake --build build` — every DDX external must be resolved by
   the backend, not a stub in `os/utils.c`.
2. **Init sequence**: enable `#define DEBUG 1` at the top of each backend
   file and confirm the log shows, in order:
   `tima_OsInit → tima_CardInit → tima_ScreenInit → tima_KeyboardInit →
   tima_MouseInit → tima_Enable`.
3. **Framebuffer**: on `tima_Enable`, the screen should clear to black.
4. **Keyboard**: `xev` should print `KeyPress` / `KeyRelease` events with
   correct `keysym` for the pressed key, plus modifier tracking for Shift /
   Ctrl / Alt / Super / Caps.
5. **Mouse**: `xev` should report motion inside the LCD bounds, button 1/2/3
   press *and release*, and no phantom buttons remaining pressed after
   release.
6. **Long‑run**: run for several minutes — no memory growth, no leaked
   `malloc`s from `cardinit` / `scrinit` (both are freed by `cardfini` /
   `scrfini`).

Once these pass, the backend is ready to host JWM and X clients.

---

## Appendix — File Cross‑Reference

Reference backend:

- Header:        [tinyx/kdrive/tima/tima_kdrive.h](../../tinyx/kdrive/tima/tima_kdrive.h)
- Framebuffer:   [tinyx/kdrive/tima/tima_kdrive_fb.c](../../tinyx/kdrive/tima/tima_kdrive_fb.c)
- Keyboard:      [tinyx/kdrive/tima/tima_kdrive_kbd.c](../../tinyx/kdrive/tima/tima_kdrive_kbd.c)
- Mouse:         [tinyx/kdrive/tima/tima_kdrive_mouse.c](../../tinyx/kdrive/tima/tima_kdrive_mouse.c)
- OS + DDX:      [tinyx/kdrive/tima/tima_kdrive_system.c](../../tinyx/kdrive/tima/tima_kdrive_system.c)

TinyX/Kdrive core:

- Vtables:       [tinyx/kdrive/src/kdrive.h](../../tinyx/kdrive/src/kdrive.h)
- Core impl:     [tinyx/kdrive/src/kdrive.c](../../tinyx/kdrive/src/kdrive.c)
- Input:         [tinyx/kdrive/src/kinput.c](../../tinyx/kdrive/src/kinput.c)
- Keymap:        [tinyx/kdrive/src/kkeymap.h](../../tinyx/kdrive/src/kkeymap.h)
- Canonical example backend: [tinyx/kdrive/fbdev/fbdev.c](../../tinyx/kdrive/fbdev/fbdev.c)

HAL:

- API header:    [framework/sdl_drivers/hal_drivers.h](../../framework/sdl_drivers/hal_drivers.h)
- SDL LCD:       [framework/sdl_drivers/lcd_driver.c](../../framework/sdl_drivers/lcd_driver.c)
- SDL keypad:    [framework/sdl_drivers/keyraw_driver.c](../../framework/sdl_drivers/keyraw_driver.c)
- SDL mouse:     [framework/sdl_drivers/mouse_driver.c](../../framework/sdl_drivers/mouse_driver.c)

Build:

- Hosted CMake:  [framework/CMakeLists.txt](../../framework/CMakeLists.txt)

## Final note

To complete the porting, implementing every symbol in hal_drivers.h is what bridges TinyX to your hardware, **but only if all of the following are also in place**:

1. **The tima backend is compiled and linked in.** That's the code that actually calls your HAL functions and translates them into `KdEnqueueKeyboardEvent` / `KdEnqueueMouseEvent` / `screen->fb.frameBuffer = ...`. Without it, TinyX has no idea your HAL exists.

2. **Your HAL implements the full set, not just the "core" ones.** The backend today calls:
   - `lcd_driver_init`, `lcd_driver_get_framebuffer`, `lcd_driver_get_width`, `lcd_driver_get_height`, `lcd_driver_get_bits_per_pixel`
   - `keypad_driver_init`, `keypad_driver_has_data`, `keypad_driver_read_data`
   - `mouse_driver_has_data`, `mouse_driver_read_data` (and `mouse_driver_init` once the bug in §9.1 is fixed)

   The other symbols (`lcd_driver_set_pixel`, `lcd_driver_fill_colour`, `lcd_driver_update_window`, …) are **not** used by the Kdrive backend — TinyX renders directly into the framebuffer pointer returned by `lcd_driver_get_framebuffer()`. They only need to exist if some other code in the framework calls them.

3. **Framebuffer semantics match.** `lcd_driver_get_framebuffer()` must return a pointer to linear memory that stays valid for the lifetime of the server, with the layout TinyX was told about (`width × bpp/8` bytes per row, no tiling, correct RGB order). If your hardware uses a swap-chain or tiled memory, you need either a shadow buffer (already supported: set `scrpriv->shadow = TRUE` and enable `shadowUpdatePacked`) or a `lcd_driver_update_window()` call driven from a WakeupHandler — neither of which is wired today.

4. **`PIXEL_TYPE_T` and bpp agree.** The CMake `-DPIXEL_TYPE_T=uint16_t` must match what `lcd_driver_get_bits_per_pixel()` returns. Mismatch → wrong pitch → torn/garbage rendering.

5. **The 5 DDX externs (`InitCard`, `InitOutput`, `InitInput`, `OsVendorInit`, `ddx*`) come from tima_kdrive_system.c**, not from anywhere else in your build. If a stub in utils.c wins the link, none of your HAL is called.

6. **Your `pollEvents` gets called.** `tima_OsPollEvents` calls `tima_KeyboardPoll` + `tima_MousePoll`, which drain your HAL. This only works because `OsVendorInit → KdOsInit(&tima_OsFuncs)` registers it. That path is already correct in the reference backend — just don't break it in your fork.

In short: **implement hal_drivers.h + link `tinyx/kdrive/tima/*.c` + fix the mouse bugs in §9 = TinyX runs on your hardware.** No other bridging layer is needed.
