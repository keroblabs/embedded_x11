# VDI/AES to HAL Mapping Reference

Quick reference for porting GEM-XServer to HAL

## Graphics Operations (VDI)

### Window/Screen Initialization
```c
// GEM-XServer (VDI)
v_opnvwk(work_in, &handle, work_out);
v_clsvwk(handle);

// HAL Replacement  
hal_fb_init(&fb_info);
hal_fb_shutdown();
```

### Drawing Primitives
```c
// Rectangle (filled)
// VDI:
v_bar(handle, pxy);  // pxy[0,1]=top-left, pxy[2,3]=bottom-right

// HAL:
hal_fb_fill_rect(x, y, width, height, color);

// Line
// VDI:
v_pline(handle, 2, pxy);

// HAL:
hal_gfx_line(x1, y1, x2, y2, color);

// Polyline
// VDI:
v_pline(handle, count, pxy);

// HAL:
hal_gfx_polyline(points, npoints, color);
```

### Cursor Operations
```c
// Hide/Show cursor
// VDI:
v_hide_c(handle);
v_show_c(handle, reset);

// HAL:
hal_cursor_hide();
hal_cursor_show();

// Set cursor shape
// VDI:
graf_mouse(USER_DEF, &form);
graf_mouse(ARROW, NULL);

// HAL:
hal_cursor_set_shape(&shape);
hal_cursor_set_builtin(HAL_CURSOR_ARROW);
```

### Query Functions
```c
// Get screen info
// VDI:
vq_extnd(handle, 1, work_out);
// work_out[0,1] = width, height
// work_out[4] = bits per pixel

// HAL:
hal_fb_get_info(&fb_info);
// fb_info.width, fb_info.height
// fb_info.bpp
```

### Bitmap Operations
```c
// Open/close bitmap
// VDI:
v_opnbm(work_in, &mfdb, &handle, work_out);
v_clsbm(handle);

// HAL:
bitmap = hal_bitmap_create(width, height, format);
hal_bitmap_destroy(bitmap);
```

## Event Handling (AES)

### Event Waiting
```c
// Multi-event wait
// VDI/AES:
evnt_multi(flags, clicks, mask, state,
           enter_rect, leave_rect,
           timeout, &msg, &mx, &my, &button, &state, &key, &clicks);

// HAL:
hal_input_wait(&event, timeout_ms);

// Then check event.type:
switch (event.type) {
    case HAL_EVENT_KEY_PRESS:
        keycode = event.data.key.keycode;
        break;
    case HAL_EVENT_MOUSE_MOVE:
        x = event.data.mouse.x;
        y = event.data.mouse.y;
        break;
    case HAL_EVENT_MOUSE_BUTTON:
        button = event.data.mouse.buttons;
        break;
}
```

## Window Operations (AES) → Remove

GEM-XServer uses wind_* functions for GEM windows.
These are NOT needed for X11 - X server manages its own windows.

```c
// DON'T port these - remove them:
wind_get(handle, WF_WORKXYWH, &x, &y, &w, &h);
wind_set(handle, WF_CURRXYWH, x, y, w, h);
wind_get_first(handle, &rect);
wind_get_next(handle, &rect);

// X11 server handles window hierarchy internally
```

## Application Initialization (AES)

```c
// VDI/AES:
appl_init();

// HAL:
hal_init(NULL);
```

## Color Conversion

```c
// Convert RGB to pixel value
// VDI uses palette indices for low color depths
// HAL uses direct color

// VDI (indexed color):
color_index = vs_color(handle, index, r, g, b);

// HAL (direct color):
pixel = hal_color_rgb(r, g, b);
```

## Complete Replacement Checklist

### Must Replace (Core Graphics)
- [ ] `v_opnvwk()` → `hal_fb_init()`
- [ ] `v_clsvwk()` → `hal_fb_shutdown()`
- [ ] `v_bar()` → `hal_fb_fill_rect()`
- [ ] `v_pline()` → `hal_gfx_line()` / `hal_gfx_polyline()`
- [ ] `v_hide_c()` / `v_show_c()` → `hal_cursor_hide()` / `hal_cursor_show()`
- [ ] `graf_mouse()` → `hal_cursor_set_shape()`
- [ ] `vq_extnd()` / `vq_scrninfo()` → `hal_fb_get_info()`

### Must Replace (Events)
- [ ] `evnt_multi()` → `hal_input_wait()`
- [ ] `appl_init()` → `hal_init()`

### Must Replace (Bitmaps)
- [ ] `v_opnbm()` → `hal_bitmap_create()`
- [ ] `v_clsbm()` → `hal_bitmap_destroy()`
- [ ] `vro_cpyfm()` → `hal_gfx_blit()`

### Can Remove (Window Management)
- [ ] `wind_get()` - X11 manages windows
- [ ] `wind_set()` - X11 manages windows
- [ ] `wind_get_first()` / `wind_get_next()` - Not needed

### Text Rendering (Handle in Software)
- [ ] `vst_alignment()` - Software text layout
- [ ] `vst_height()` - Software font selection
- [ ] `v_gtext()` - Software text rendering

## File-by-File Replacement Guide

### src/grph.c - Main Graphics Module
**VDI calls to replace:**
```c
Line 87:  v_opnvwk()       → hal_fb_init()
Line 95:  vq_extnd()        → hal_fb_get_info()
Line 114: v_clsvwk()        → hal_fb_shutdown()
Line 143: vq_scrninfo()     → hal_fb_get_info()
```

### src/draw_img.c - Image Drawing
**VDI calls:**
```c
Line 180: wind_get_work()   → Remove (X manages windows)
Various:  vro_cpyfm()       → hal_gfx_blit()
```

### src/pixmap.c - Pixmap Management  
**VDI calls:**
```c
Line 71:  vq_gdos()         → Remove (no GDOS)
Line 75:  v_clsbm()         → hal_bitmap_destroy()
Line 122: v_opnbm()         → hal_bitmap_create()
```

### src/wind_draw.c - Window Drawing
**VDI calls:**
```c
Line 313: wind_get()        → Remove
Line 316: wind_get_first()  → Remove  
Line 344: wind_get_next()   → Remove
Line 461: v_hide_c()        → hal_cursor_hide()
Line 492: v_bar()           → hal_fb_fill_rect()
Line 505: v_show_c()        → hal_cursor_show()
```

### src/Cursor.c - Cursor Management
**VDI calls:**
```c
Line 130: graf_mouse()      → hal_cursor_set_shape()
Line 133: graf_mouse(ARROW) → hal_cursor_set_builtin(HAL_CURSOR_ARROW)
```

### src/main.c - Main Event Loop
**AES calls:**
```c
Line 74:  appl_init()       → hal_init()
Line 250: evnt_multi()      → hal_input_wait()
```

## Priority Order for Porting

### Phase 1: Get something on screen
1. `hal_fb_init()` - Initialize framebuffer
2. `hal_fb_fill_rect()` - Draw rectangles
3. `hal_color_rgb()` - Color conversion

### Phase 2: Basic interaction
4. `hal_input_wait()` - Get events
5. `hal_timer_get_ms()` - Timing

### Phase 3: Graphics operations
6. `hal_gfx_line()` - Lines
7. `hal_gfx_blit()` - Bitmap operations
8. `hal_fb_copy_rect()` - Scrolling

### Phase 4: Polish
9. `hal_cursor_*()` - Cursor support
10. `hal_bitmap_*()` - Offscreen bitmaps

## Code Pattern Examples

### Pattern: Rectangle Drawing

**Before (VDI):**
```c
void draw_rectangle(int x, int y, int w, int h)
{
    short pxy[4];
    pxy[0] = x;
    pxy[1] = y;
    pxy[2] = x + w - 1;
    pxy[3] = y + h - 1;
    
    vsf_color(GRPH_Vdi, current_color);
    v_bar(GRPH_Vdi, pxy);
}
```

**After (HAL):**
```c
void draw_rectangle(int x, int y, int w, int h)
{
    hal_fb_fill_rect(x, y, w, h, current_color);
}
```

### Pattern: Event Loop

**Before (AES):**
```c
while (1) {
    short msg[8], mx, my, button, state, key, clicks;
    
    int which = evnt_multi(
        MU_MESAG | MU_BUTTON | MU_KEYBD,
        2, 1, 1,  // double-click, left button
        0, 0, 0, 0, 0,  // no rect entry/exit
        0, 0, 0, 0, 0,
        msg, 500,  // 500ms timeout
        &mx, &my, &button, &state, &key, &clicks
    );
    
    if (which & MU_KEYBD) {
        handle_key(key);
    }
    if (which & MU_BUTTON) {
        handle_mouse(mx, my, button);
    }
}
```

**After (HAL):**
```c
while (1) {
    hal_event_t event;
    
    if (hal_input_wait(&event, 500) == HAL_OK) {
        switch (event.type) {
            case HAL_EVENT_KEY_PRESS:
                handle_key(event.data.key.keycode);
                break;
            
            case HAL_EVENT_MOUSE_BUTTON:
                handle_mouse(
                    event.data.mouse.x,
                    event.data.mouse.y,
                    event.data.mouse.buttons
                );
                break;
        }
    }
}
```

### Pattern: Screen Info Query

**Before (VDI):**
```c
short work_in[16], work_out[57];
short handle;

// Open workstation
handle = graf_handle(&dummy, &dummy, &dummy, &dummy);
v_opnvwk(work_in, &handle, work_out);

// Get info
int screen_w = work_out[0];
int screen_h = work_out[1];
int bpp;
vq_extnd(handle, 1, work_out);
bpp = work_out[4];
```

**After (HAL):**
```c
hal_fb_info_t fb_info;

// Initialize and get info in one call
hal_fb_init(&fb_info);

int screen_w = fb_info.width;
int screen_h = fb_info.height;
int bpp = fb_info.bpp;
```

## Testing Your Replacements

After replacing each VDI/AES call, test with:

### Test 1: Fill Screen
```c
hal_fb_fill_rect(0, 0, fb_info.width, fb_info.height, 
                 hal_color_rgb(0, 0, 255));  // Blue
```

### Test 2: Draw Pattern
```c
for (int y = 0; y < fb_info.height; y += 10) {
    hal_fb_fill_rect(0, y, fb_info.width, 5,
                     hal_color_rgb(255, 255, 255));
}
```

### Test 3: Test Events
```c
hal_event_t event;
while (1) {
    if (hal_input_wait(&event, -1) == HAL_OK) {
        // Draw white dot where mouse is
        if (event.type == HAL_EVENT_MOUSE_MOVE) {
            hal_fb_fill_rect(event.data.mouse.x,
                           event.data.mouse.y,
                           5, 5,
                           hal_color_rgb(255, 255, 255));
        }
    }
}
```

## Common Issues and Solutions

### Issue: Colors Wrong
**Problem**: Using VDI color indices with HAL
**Solution**: Convert all color values to RGB with `hal_color_rgb()`

### Issue: Coordinates Inverted
**Problem**: VDI uses bottom-right corner, HAL uses width/height
**Solution**: Convert: `width = x2 - x1 + 1; height = y2 - y1 + 1`

### Issue: Events Not Working
**Problem**: Forgot to initialize input HAL
**Solution**: Call `hal_input_init()` in initialization

### Issue: Nothing Displays
**Problem**: Framebuffer address wrong
**Solution**: Check platform documentation for correct FB address

## Summary Checklist

Before starting port:
- [ ] Read HAL_SPECIFICATION.md
- [ ] Understand all HAL functions
- [ ] Know your platform's framebuffer address
- [ ] Have RISC-V toolchain set up

During port:
- [ ] Replace one file at a time
- [ ] Test after each replacement
- [ ] Keep notes on changes
- [ ] Don't remove wind_* calls yet (may cause compile errors)

After port:
- [ ] All VDI calls replaced
- [ ] All AES calls replaced
- [ ] Test program runs
- [ ] Screen updates visible
- [ ] Events work

This mapping guide should help you systematically replace all GEM dependencies with HAL calls.
