# OS C Library Requirements for WMB

**Date**: January 22, 2026  
**Purpose**: Documents the libc functions and syscalls that the underlying OS must provide for WMB to function correctly.

## Overview

Per BASE_REQUIREMENTS #8, the WMB (Window Manager Binary) assumes a hypothetical OS provides a POSIX-compatible C library. This document specifies the **minimum required functions** the OS must implement.

WMB does **not** implement syscalls or libc—it links against them.

---

## Required libc Functions

### Memory Management (Critical)

| Function | Header | Notes |
|----------|--------|-------|
| `malloc(size_t)` | stdlib.h | Must support allocations up to 16MB |
| `free(void*)` | stdlib.h | |
| `realloc(void*, size_t)` | stdlib.h | |
| `calloc(size_t, size_t)` | stdlib.h | |

**Heap Requirements**:
- Minimum 32MB heap available
- Thread-safety not required (WMB is single-threaded)

### String Operations (Critical)

| Function | Header |
|----------|--------|
| `memcpy` | string.h |
| `memmove` | string.h |
| `memset` | string.h |
| `memcmp` | string.h |
| `strlen` | string.h |
| `strcpy` | string.h |
| `strncpy` | string.h |
| `strcmp` | string.h |
| `strncmp` | string.h |
| `strcat` | string.h |
| `strchr` | string.h |
| `strrchr` | string.h |
| `strstr` | string.h |
| `strdup` | string.h |

### Standard I/O (Required for configuration files)

| Function | Header | Notes |
|----------|--------|-------|
| `fopen` | stdio.h | Read-only support sufficient |
| `fclose` | stdio.h | |
| `fread` | stdio.h | |
| `fwrite` | stdio.h | Optional (for logging) |
| `fseek` | stdio.h | |
| `ftell` | stdio.h | |
| `feof` | stdio.h | |
| `ferror` | stdio.h | |
| `fgets` | stdio.h | For config parsing |
| `fprintf` | stdio.h | For debug output |
| `sprintf` | stdio.h | |
| `snprintf` | stdio.h | |
| `vsnprintf` | stdio.h | |

#### Why Filesystem Access is Needed

TinyX and JWM read several configuration files at startup:

**TinyX/X Server Configuration Files:**
| File | Purpose | Fallback if Missing |
|------|---------|---------------------|
| `/etc/X11/rgb.txt` | Color name database (e.g., "red" → 0xFF0000) | Built-in color table |
| `/etc/X11/XErrorDB` | Error message strings | Generic error messages |
| `/usr/share/fonts/**` | Font files (BDF, PCF, TTF) | Built-in bitmap fonts |
| `~/.Xauthority` | Authentication cookies | No auth (local only) |
| Keyboard mappings | XKB or Xmodmap files | US QWERTY default |

**JWM Configuration Files:**
| File | Purpose | Fallback if Missing |
|------|---------|---------------------|
| `~/.jwmrc` or `/etc/jwm/jwmrc` | Window manager config (menus, keys, styles) | Hard-coded defaults |
| Theme/style files | Window decoration colors, fonts | Built-in theme |
| Icon files (XPM, PNG) | Application icons | No icons or placeholders |

**Note**: The OS is expected to provide filesystem access. The "built-in defaults" are fallback values compiled into WMB that allow it to function minimally if specific config files are missing—not a replacement for filesystem support.

### Time Functions (Required)

| Function | Header | Notes |
|----------|--------|-------|
| `time(time_t*)` | time.h | Can return seconds since boot |
| `gettimeofday(struct timeval*, void*)` | sys/time.h | Microsecond precision preferred |
| `usleep(useconds_t)` | unistd.h | Or equivalent delay |
| `localtime(const time_t*)` | time.h | Can return static data |

**Timer Requirements**:
- Monotonic time source
- Millisecond resolution minimum
- Backed by HAL timer if available

### General Utilities (Required)

| Function | Header |
|----------|--------|
| `abs` | stdlib.h |
| `atoi` | stdlib.h |
| `atol` | stdlib.h |
| `strtol` | stdlib.h |
| `strtoul` | stdlib.h |
| `qsort` | stdlib.h |
| `bsearch` | stdlib.h |
| `exit` | stdlib.h |
| `abort` | stdlib.h |

### Character Classification (Required)

| Function | Header |
|----------|--------|
| `isalpha` | ctype.h |
| `isdigit` | ctype.h |
| `isspace` | ctype.h |
| `isprint` | ctype.h |
| `toupper` | ctype.h |
| `tolower` | ctype.h |

### Math Functions (Optional)

Only needed if applications use floating-point graphics:

| Function | Header |
|----------|--------|
| `sin`, `cos`, `tan` | math.h |
| `sqrt` | math.h |
| `floor`, `ceil` | math.h |

---

## Optional Functions

### Signals (Optional)

WMB can function without signal support. If unavailable, stub these:

| Function | Header | Stub Behavior |
|----------|--------|---------------|
| `signal` | signal.h | Return SIG_DFL |
| `raise` | signal.h | Return 0 |

### Environment (Optional)

| Function | Header | Stub Behavior |
|----------|--------|---------------|
| `getenv` | stdlib.h | Return NULL |
| `setenv` | stdlib.h | Return -1 |

### Directory Operations (Required for font/config discovery)

Used to scan font directories and locate configuration files:

| Function | Header | Notes |
|----------|--------|-------|
| `opendir` | dirent.h | For scanning font directories |
| `readdir` | dirent.h | |
| `closedir` | dirent.h | |
| `stat` | sys/stat.h | File existence and type checking |
| `access` | unistd.h | Permission checking |

---

## Syscalls (OS Responsibility)

The libc functions above typically require these underlying syscalls:

### File I/O Syscalls
```c
int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int fstat(int fd, struct stat *buf);
```

### Memory Syscalls
```c
void *sbrk(intptr_t increment);  // For heap growth
// Or mmap-based allocation
```

### Time Syscalls
```c
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int nanosleep(const struct timespec *req, struct timespec *rem);
```

---

## Implementation Notes for OS Developers

### Minimal Implementation Strategy

For a minimal bare-metal OS:

1. **Heap**: Implement `sbrk()` with a static heap region
2. **Time**: Use RISC-V `mtime` CSR for `gettimeofday()`
3. **Files**: Stub all file operations to return ENOENT
4. **String/Memory**: Use newlib's implementations

### Using Newlib

Newlib requires these syscall stubs:
```c
// Minimal newlib syscalls for WMB
void *_sbrk(intptr_t incr);      // Heap management
int _write(int fd, char *buf, int len);  // Debug output
int _read(int fd, char *buf, int len);   // Return 0 (EOF)
int _close(int fd);              // Return 0
int _fstat(int fd, struct stat *st);     // Return -1
int _isatty(int fd);             // Return 1 for stdout/stderr
int _lseek(int fd, int offset, int whence); // Return 0
int _kill(int pid, int sig);     // Return -1
int _getpid(void);               // Return 1
```

### Testing Without Full OS

For development, WMB can be tested on Linux using the host's libc:
```bash
# Build WMB linking against host libc
gcc -o wmb_test wmb_sources.c -lX11 -lm

# Or cross-compile with newlib
riscv64-unknown-elf-gcc -specs=nosys.specs -o wmb.elf wmb_sources.c
```

---

## Verification Checklist

Before running WMB, verify the OS provides:

- [ ] `malloc()` works with 1MB+ allocations
- [ ] `memcpy()` handles overlapping regions correctly (or `memmove()` available)
- [ ] `gettimeofday()` returns incrementing values
- [ ] `sprintf()` / `snprintf()` work correctly
- [ ] Stack is at least 1MB

### Test Code

```c
// Simple verification test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

int verify_libc(void) {
    // Test malloc
    void *p = malloc(1024 * 1024);  // 1MB
    if (!p) return 1;
    free(p);
    
    // Test string ops
    char buf[100];
    snprintf(buf, sizeof(buf), "Test %d", 42);
    if (strcmp(buf, "Test 42") != 0) return 2;
    
    // Test time
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    for (volatile int i = 0; i < 100000; i++);
    gettimeofday(&tv2, NULL);
    if (tv2.tv_sec < tv1.tv_sec) return 3;
    
    return 0;  // Success
}
```

---

## See Also

- [BUILD_PLAN.md](BUILD_PLAN.md) - Build dependencies and strategy
- [HAL_SPECIFICATION.md](HAL_SPECIFICATION.md) - Hardware abstraction layer
- [WMB_API_DESIGN.md](WMB_API_DESIGN.md) - WMB architecture
