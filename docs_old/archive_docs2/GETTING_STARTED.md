# Getting Started: First Steps Implementation Guide

This guide provides concrete first steps to start porting the X11 server to RISC-V bare metal.

## Step 1: Create Project Structure

```bash
# Create directory structure
mkdir -p x11-riscv/{hal/{include,common,platform/generic},xserver,config,boot,test}
cd x11-riscv

## OS-Specific Adaptation Checklist

1. Customize **X11/Xos.h** to redirect POSIX APIs (gettimeofday, open, read, etc.) to HAL/syscalls.
2. Implement required functions in **libc/syscalls.c** for bare-metal (gettimeofday, open, read, write, sbrk, etc.).
3. Stub/replace unsupported features in **TinyX/src/os/** (event loop, timers, signals, file I/O, networking).
4. Ensure **JWM/src/timing.c** and similar modules have required time/env APIs.
5. Adjust **Makefile/configure** scripts for platform macros and build rules.
6. Verify **X11 headers** are compatible with platform/HAL.
```

## Step 2: Implement Basic HAL Headers

### hal/include/hal.h

```c
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>

/* Boolean type */
typedef int hal_bool_t;
#define HAL_TRUE  1
#define HAL_FALSE 0

/* Error codes */
#define HAL_OK              0
#define HAL_ERR_GENERIC    -1
#define HAL_ERR_INVALID    -2
#define HAL_ERR_NOMEM      -3
#define HAL_ERR_NOTFOUND   -4
#define HAL_ERR_TIMEOUT    -5

/* Platform initialization */
int hal_init(void);
void hal_shutdown(void);

#endif /* HAL_H */
```

### hal/include/hal_framebuffer.h

```c
#ifndef HAL_FRAMEBUFFER_H
#define HAL_FRAMEBUFFER_H

#include "hal.h"

typedef enum {
    HAL_FMT_RGB565,     /* 16 bpp */
    HAL_FMT_RGB888,     /* 24 bpp */
    HAL_FMT_ARGB8888,   /* 32 bpp */
} hal_pixel_format_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    void *framebuffer;
    hal_pixel_format_t format;
} hal_fb_info_t;

/* Initialize framebuffer */
int hal_fb_init(hal_fb_info_t *info);

/* Get framebuffer information */
int hal_fb_get_info(hal_fb_info_t *info);

/* Fill rectangle - most critical primitive */
void hal_fb_fill_rect(int x, int y, int width, int height, uint32_t color);

/* Copy rectangle for scrolling/moving windows */
void hal_fb_copy_rect(int src_x, int src_y, int dst_x, int dst_y, 
                      int width, int height);

/* Convert RGB to pixel format */
uint32_t hal_color_rgb(uint8_t r, uint8_t g, uint8_t b);

#endif /* HAL_FRAMEBUFFER_H */
```

### hal/include/hal_input.h

```c
#ifndef HAL_INPUT_H
#define HAL_INPUT_H

#include "hal.h"

typedef enum {
    HAL_EVENT_NONE = 0,
    HAL_EVENT_KEY_PRESS,
    HAL_EVENT_KEY_RELEASE,
    HAL_EVENT_MOUSE_MOVE,
    HAL_EVENT_MOUSE_BUTTON,
} hal_event_type_t;

typedef struct {
    hal_event_type_t type;
    uint32_t timestamp;
    
    union {
        struct {
            uint32_t keycode;
            uint32_t modifiers;
        } key;
        
        struct {
            int32_t x;
            int32_t y;
            uint8_t buttons;
        } mouse;
    } data;
} hal_event_t;

/* Initialize input subsystem */
int hal_input_init(void);

/* Wait for event with timeout (milliseconds) */
int hal_input_wait(hal_event_t *event, int timeout_ms);

#endif /* HAL_INPUT_H */
```

### hal/include/hal_timer.h

```c
#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include "hal.h"

/* Initialize timer */
int hal_timer_init(void);

/* Get milliseconds since boot */
uint32_t hal_timer_get_ms(void);

/* Get microseconds since boot */
uint64_t hal_timer_get_us(void);

/* Delay (busy wait) */
void hal_timer_delay_ms(uint32_t ms);

#endif /* HAL_TIMER_H */
```

## Step 3: Implement Generic Framebuffer HAL

### hal/platform/generic/fb_generic.c

```c
#include "hal_framebuffer.h"
#include <string.h>

/* Platform-specific configuration - adjust for your platform */
#define FB_BASE_ADDR    0xB0000000  /* Platform-dependent: check your memory map */
#define FB_WIDTH        1024
#define FB_HEIGHT       768
#define FB_BPP          32
#define FB_FORMAT       HAL_FMT_ARGB8888

static hal_fb_info_t fb_info;
static uint32_t *framebuffer;

int hal_fb_init(hal_fb_info_t *info)
{
    fb_info.width = FB_WIDTH;
    fb_info.height = FB_HEIGHT;
    fb_info.bpp = FB_BPP;
    fb_info.pitch = FB_WIDTH * (FB_BPP / 8);
    fb_info.framebuffer = (void *)FB_BASE_ADDR;
    fb_info.format = FB_FORMAT;
    
    framebuffer = (uint32_t *)fb_info.framebuffer;
    
    /* Clear screen to black */
    memset(framebuffer, 0, fb_info.pitch * fb_info.height);
    
    if (info) {
        *info = fb_info;
    }
    
    return HAL_OK;
}

int hal_fb_get_info(hal_fb_info_t *info)
{
    if (!info) return HAL_ERR_INVALID;
    *info = fb_info;
    return HAL_OK;
}

void hal_fb_fill_rect(int x, int y, int width, int height, uint32_t color)
{
    /* Clip to screen bounds */
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > fb_info.width) width = fb_info.width - x;
    if (y + height > fb_info.height) height = fb_info.height - y;
    
    if (width <= 0 || height <= 0) return;
    
    /* Fast path for 32bpp */
    uint32_t *line = framebuffer + (y * fb_info.width + x);
    int pitch_pixels = fb_info.width;
    
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            line[col] = color;
        }
        line += pitch_pixels;
    }
}

void hal_fb_copy_rect(int src_x, int src_y, int dst_x, int dst_y,
                      int width, int height)
{
    /* Clip rectangles */
    if (src_x < 0 || src_y < 0 || dst_x < 0 || dst_y < 0) return;
    if (src_x + width > fb_info.width || dst_x + width > fb_info.width) return;
    if (src_y + height > fb_info.height || dst_y + height > fb_info.height) return;
    
    uint32_t *src = framebuffer + (src_y * fb_info.width + src_x);
    uint32_t *dst = framebuffer + (dst_y * fb_info.width + dst_x);
    int pitch = fb_info.width;
    
    /* Handle overlapping regions */
    if (dst_y > src_y || (dst_y == src_y && dst_x > src_x)) {
        /* Copy bottom-to-top or right-to-left */
        src += (height - 1) * pitch;
        dst += (height - 1) * pitch;
        for (int row = height - 1; row >= 0; row--) {
            memmove(dst, src, width * sizeof(uint32_t));
            src -= pitch;
            dst -= pitch;
        }
    } else {
        /* Copy top-to-bottom, left-to-right */
        for (int row = 0; row < height; row++) {
            memmove(dst, src, width * sizeof(uint32_t));
            src += pitch;
            dst += pitch;
        }
    }
}

uint32_t hal_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* ARGB8888 format */
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}
```

## Step 4: Timer HAL (Platform-Specific - Outside Scope)

**Note:** The timer implementation is platform-specific and outside the scope of this project.
The HAL defines the interface that TinyX and JWM will use.

### hal/platform/generic/timer_platform.c (Stub Example)

```c
#include "hal_timer.h"

/* Platform-specific timer implementation goes here */
/* Examples: RISC-V CSR, memory-mapped timer, SoC-specific timer */
/* This is a STUB - replace with actual platform implementation */

/* Assume 1MHz time register for simplicity */
/* Adjust TIMER_FREQ for your platform */
#define TIMER_FREQ 1000000UL  /* 1 MHz */

static uint64_t boot_time_us = 0;

int hal_timer_init(void)
{
    boot_time_us = read_csr(time);
    return HAL_OK;
}

uint64_t hal_timer_get_us(void)
{
    uint64_t now = read_csr(time);
    return now - boot_time_us;
}

uint32_t hal_timer_get_ms(void)
{
    return (uint32_t)(hal_timer_get_us() / 1000);
}

void hal_timer_delay_ms(uint32_t ms)
{
    uint64_t start = hal_timer_get_us();
    uint64_t delay = ms * 1000ULL;
    
    while ((hal_timer_get_us() - start) < delay) {
        /* Busy wait */
        asm volatile ("nop");
    }
}
```

---

## Step 5: Implement Newlib Syscalls (Timer Integration)

TinyX, JWM, and X11 use standard POSIX `gettimeofday()`. Implement this in the newlib syscalls layer to connect to your HAL timer.

### libc/syscalls.c

```c
#include <sys/time.h>
#include <sys/times.h>
#include <sys/stat.h>
#include <errno.h>
#include "hal_timer.h"

extern char _end[];  /* Defined by linker */
static char *heap_ptr = NULL;

/* Memory allocation support */
void *_sbrk(int incr)
{
    char *prev_heap;
    
    if (heap_ptr == NULL) {
        heap_ptr = _end;
    }
    
    prev_heap = heap_ptr;
    heap_ptr += incr;
    
    return prev_heap;
}

/* Timer support - CRITICAL for TinyX/JWM/X11 */
int _gettimeofday(struct timeval *tv, void *tz)
{
    if (tv == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    uint64_t us = hal_timer_get_us();
    tv->tv_sec = us / 1000000;
    tv->tv_usec = us % 1000000;
    return 0;
}

/* Stub implementations for other syscalls */
int _close(int file) { return -1; }
int _fstat(int file, struct stat *st) { st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { return 1; }
int _lseek(int file, int offset, int whence) { return 0; }
int _read(int file, char *ptr, int len) { return 0; }
int _write(int file, char *ptr, int len) { return len; }
void _exit(int status) { while(1); }
int _kill(int pid, int sig) { return -1; }
int _getpid(void) { return 1; }
```

**Why this matters:**
- TinyX, JWM, and X11 call `gettimeofday()` for timing
- `_gettimeofday()` calls `hal_timer_get_us()` → connects to your platform timer
- No modification needed to TinyX/JWM/X11 source code
- Standard POSIX API maintained

---

## Step 6: Implement Input Stub

### hal/platform/generic/input_stub.c

```c
#include "hal_input.h"

/* Stub implementation - returns no events */
/* Replace with actual keyboard/mouse drivers */

int hal_input_init(void)
{
    return HAL_OK;
}

int hal_input_wait(hal_event_t *event, int timeout_ms)
{
    /* For now, just timeout immediately */
    /* Real implementation would poll hardware */
    (void)event;
    (void)timeout_ms;
    
    return HAL_ERR_TIMEOUT;
}
```

## Step 7: Create Boot Code

### boot/start.S

```assembly
# RISC-V startup code
.section .text.init
.global _start
.type _start, @function

_start:
    # Disable interrupts
    csrw mie, zero
    csrw mip, zero
    
    # Setup stack pointer
    la sp, __stack_top
    
    # Clear BSS
    la t0, __bss_start
    la t1, __bss_end
1:
    bge t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    
    # Call C main function
    call main
    
    # Halt on return
3:
    wfi
    j 3b

.size _start, . - _start
```

### boot/init.c

```c
#include "hal.h"
#include "hal_framebuffer.h"
#include "hal_timer.h"
#include "hal_input.h"

extern void xserver_main(void);

int main(void)
{
    hal_fb_info_t fb;
    
    /* Initialize HAL subsystems */
    hal_fb_init(&fb);
    hal_timer_init();
    hal_input_init();
    
    /* Draw test pattern */
    hal_fb_fill_rect(0, 0, fb.width, fb.height, hal_color_rgb(0, 0, 100));
    hal_fb_fill_rect(100, 100, 200, 200, hal_color_rgb(255, 0, 0));
    hal_fb_fill_rect(350, 100, 200, 200, hal_color_rgb(0, 255, 0));
    hal_fb_fill_rect(100, 350, 200, 200, hal_color_rgb(0, 0, 255));
    hal_fb_fill_rect(350, 350, 200, 200, hal_color_rgb(255, 255, 0));
    
    /* Start X server */
    xserver_main();
    
    return 0;
}
```

## Step 8: Create Minimal Build System

### Makefile

```makefile
# RISC-V bare-metal X11 server Makefile

# Toolchain
CROSS_COMPILE ?= riscv64-unknown-elf-
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# Architecture flags
ARCH_FLAGS = -march=rv64imac -mabi=lp64

# Compiler flags
CFLAGS = -Os -Wall -Wextra
CFLAGS += $(ARCH_FLAGS)
CFLAGS += -ffreestanding -nostdlib
CFLAGS += -Ihal/include -Iconfig

# Linker flags
LDFLAGS = $(ARCH_FLAGS)
LDFLAGS += -T linker.ld
LDFLAGS += -nostdlib

# Source files
LIBC_SRCS = libc/syscalls.c

HAL_SRCS = hal/platform/generic/fb_generic.c \
           hal/platform/generic/timer_platform.c \
           hal/platform/generic/input_stub.c

BOOT_SRCS = boot/start.S \
            boot/init.c

XSERVER_SRCS = xserver/main_stub.c

SRCS = $(LIBC_SRCS) $(HAL_SRCS) $(BOOT_SRCS) $(XSERVER_SRCS)
OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)

# Targets
TARGET = x11-riscv

all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).lst: $(TARGET).elf
	$(OBJDUMP) -d $< > $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).lst

.PHONY: all clean
```

### linker.ld

```ld
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    /* ORIGIN address is platform-dependent - adjust for your memory map */
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128M
}

SECTIONS
{
    /* Start address matches RAM ORIGIN above */
    . = 0x80000000;
    
    .text : {
        *(.text.init)
        *(.text*)
    } > RAM
    
    .rodata : {
        *(.rodata*)
    } > RAM
    
    .data : {
        *(.data*)
    } > RAM
    
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > RAM
    
    . = ALIGN(16);
    .stack : {
        . = . + 0x100000;  /* 1MB stack */
        __stack_top = .;
    } > RAM
}
```

## Step 8: Create X Server Stub

### xserver/main_stub.c

```c
#include <stdint.h>
#include "hal_framebuffer.h"
#include "hal_timer.h"
#include "hal_input.h"

void xserver_main(void)
{
    hal_fb_info_t fb;
    hal_event_t event;
    uint32_t color_index = 0;
    uint32_t colors[] = {
        hal_color_rgb(255, 0, 0),    /* Red */
        hal_color_rgb(0, 255, 0),    /* Green */
        hal_color_rgb(0, 0, 255),    /* Blue */
        hal_color_rgb(255, 255, 0),  /* Yellow */
        hal_color_rgb(255, 0, 255),  /* Magenta */
        hal_color_rgb(0, 255, 255),  /* Cyan */
    };
    
    hal_fb_get_info(&fb);
    
    /* Simple animation loop */
    while (1) {
        /* Fill screen with rotating color */
        hal_fb_fill_rect(0, 0, fb.width, fb.height, 
                        colors[color_index % 6]);
        
        /* Draw a moving box */
        int box_x = (hal_timer_get_ms() / 10) % (fb.width - 100);
        hal_fb_fill_rect(box_x, fb.height / 2 - 50, 100, 100,
                        hal_color_rgb(255, 255, 255));
        
        /* Check for input events */
        if (hal_input_wait(&event, 100) == HAL_OK) {
            /* Event received - change color */
            color_index++;
        }
        
        /* Delay */
        hal_timer_delay_ms(1000);
        color_index++;
    }
}
```

## Step 10: Build and Test

```bash
# Build the project
make clean
make

# Outputs:
#   x11-riscv.elf  - ELF executable
#   x11-riscv.bin  - Binary image for direct loading
#   x11-riscv.lst  - Disassembly listing

# Test in QEMU (if available)
qemu-system-riscv64 \
    -machine virt \
    -m 256M \
    -kernel x11-riscv.elf \
    -nographic

# Or with graphics
qemu-system-riscv64 \
    -machine virt \
    -m 256M \
    -kernel x11-riscv.elf \
    -device VGA
```

## Step 11: Implement Kdrive Interface

Now that you have a working HAL, you can start implementing the Kdrive interface:

### Implement KdCardFuncs

```c
/* xserver/kdrive/riscv_screen.c */
#include "kdrive.h"
#include "hal_framebuffer.h"

/* Kdrive screen info */
typedef struct {
    short x, y;
} PXY;

/* Replace VDI v_bar with HAL */
void v_bar(short handle, short *pxy)
{
    (void)handle;  /* Ignore VDI handle */
    
    int x1 = pxy[0];
    int y1 = pxy[1];
    int x2 = pxy[2];
    int y2 = pxy[3];
    
    int width = x2 - x1 + 1;
    int height = y2 - y1 + 1;
    
    /* Use current GC color - needs integration with GC management */
    extern uint32_t current_color;  /* To be implemented */
    
    hal_fb_fill_rect(x1, y1, width, height, current_color);
}

/* Add more VDI function wrappers... */
```

## Next Steps

1. **Verify HAL works**: Build and test the basic framebuffer and timer
2. **Add keyboard driver**: Implement actual keyboard hardware access
3. **Add mouse driver**: Implement mouse hardware access
4. **Implement Kdrive interface**: Implement KdCardFuncs, KdKeyboardFuncs, KdPointerFuncs for TinyX
5. **Add protocol handling**: Enable X11 client connections
6. **Integrate window manager**: Add JWM once server works

## Development Tips

### Debugging Without Serial Console

```c
/* Use framebuffer for debug output */
void fb_printf(int x, int y, const char *fmt, ...)
{
    /* Draw text directly to framebuffer */
    /* Implement simple 8x8 bitmap font */
}
```

### Memory Layout Verification

```c
/* Check memory layout at runtime */
extern char __bss_start, __bss_end, __stack_top;

void verify_memory_layout(void)
{
    fb_printf(0, 0, "BSS: %p - %p", &__bss_start, &__bss_end);
    fb_printf(0, 16, "Stack: %p", &__stack_top);
    fb_printf(0, 32, "Framebuffer: %p", (void*)0x80000000);
}
```

### Testing Individual Components

Create simple test programs:

```c
/* test/test_fb.c */
int main(void)
{
    hal_fb_init(NULL);
    
    /* Draw test pattern */
    for (int y = 0; y < 768; y++) {
        for (int x = 0; x < 1024; x++) {
            uint32_t color = hal_color_rgb(x % 256, y % 256, (x+y) % 256);
            hal_fb_fill_rect(x, y, 1, 1, color);
        }
    }
    
    return 0;
}
```

This provides a solid foundation to build upon. Each component can be tested independently before integrating the full X server.
