# TinyX Preprocessor Definitions Reference

This document lists all preprocessor definitions used in TinyX, extracted from `configure.ac`, `dix-config.h.in`, `kdrive-config.h.in`, and source files. Use this as a reference when configuring the CMake build for the WMB project.

---

## Table of Contents

1. [Critical Build Definitions](#critical-build-definitions)
2. [KDrive Specific Definitions](#kdrive-specific-definitions)
3. [X Extensions](#x-extensions)
4. [Network/Connection Definitions](#networkconnection-definitions)
5. [System/Platform Definitions](#systemplatform-definitions)
6. [Feature Detection (HAVE_*)](#feature-detection-have_)
7. [Function Availability (HAS_*)](#function-availability-has_)
8. [Vendor/Build Information](#vendorbuild-information)
9. [Recommended CMake Configuration](#recommended-cmake-configuration)

---

## Critical Build Definitions

These are **essential** for building TinyX/KDrive:

| Definition | Value | Description | Source |
|------------|-------|-------------|--------|
| `HAVE_DIX_CONFIG_H` | 1 | **REQUIRED** - Enables inclusion of dix-config.h | DIX_CFLAGS |
| `HAVE_KDRIVE_CONFIG_H` | 1 | **REQUIRED for KDrive** - Enables kdrive-config.h | KDRIVE_CFLAGS |
| `KDRIVESERVER` | 1 | Building KDrive X server | configure.ac:535 |
| `KDRIVEDDXACTIONS` | (empty) | Build kdrive DDX | configure.ac:536 |
| `X_BYTE_ORDER` | `X_LITTLE_ENDIAN` or `X_BIG_ENDIAN` | Endianness | configure.ac:498 |
| `_XSERVER64` | 1 | Define if `unsigned long` is 64 bits | configure.ac:63 |

### Byte Order Values
```c
#define X_LITTLE_ENDIAN 1234
#define X_BIG_ENDIAN    4321
```

---

## KDrive Specific Definitions

| Definition | Value | Description | Source |
|------------|-------|-------------|--------|
| `KDRIVESERVER` | 1 | Building KDrive X server | configure.ac:535 |
| `KDRIVEDDXACTIONS` | (empty) | KDrive DDX actions enabled | configure.ac:536 |
| `KDRIVEVESA` | 1 | Build VESA-based kdrive server | configure.ac:541 |
| `KDRIVEFBDEV` | 1 | Build fbdev-based kdrive server | configure.ac:548 |

**For WMB SDL backend**: Define `KDRIVESERVER` and `KDRIVEDDXACTIONS`, but NOT `KDRIVEVESA` or `KDRIVEFBDEV`.

---

## X Extensions

Control which X protocol extensions are built:

| Definition | Value | Description | Default | Source |
|------------|-------|-------------|---------|--------|
| `DBE` | 1 | Double Buffer Extension | Enabled | configure.ac:339 |
| `DPMSExtension` | 1 | Display Power Management Signaling | Enabled | configure.ac:350 |
| `SCREENSAVER` | 1 | MIT-SCREEN-SAVER extension | Enabled | configure.ac:328 |
| `RES` | 1 | X Resource extension | Enabled | configure.ac:333 |
| `XResExtension` | 1 | X Resource extension (alternate) | Enabled | configure.ac:584 |
| `XF86BIGFONT` | 1 | XFree86 Big Font extension | Enabled | configure.ac:345 |
| `XDMCP` | 1 | XDM Control Protocol | Optional | configure.ac:391 |
| `HASXDMAUTH` | 1 | XDM-AUTH*-1 support | Optional | configure.ac:398 |

### Recommended for WMB (Minimal Build)
```cmake
# Essential extensions
add_definitions(-DDBE=1)                # Double buffering needed for JWM
add_definitions(-DDPMSExtension=1)      # Power management
add_definitions(-DSCREENSAVER=1)        # Screen saver support

# Can be disabled for minimal build
# add_definitions(-DXDMCP)              # Not needed for embedded
# add_definitions(-DHASXDMAUTH)         # Not needed without XDMCP
# add_definitions(-DXF86BIGFONT=1)      # Optional
```

---

## Network/Connection Definitions

| Definition | Value | Description | Source |
|------------|-------|-------------|--------|
| `TCPCONN` | 1 | Support TCP socket connections | dix-config.h.in |
| `UNIXCONN` | 1 | Support UNIX socket connections | dix-config.h.in |
| `LOCALCONN` | 1 | Support OS-specific local connections | dix-config.h.in |
| `IPv6` | 1 | Support IPv6 for TCP connections | dix-config.h.in |

### Recommended for WMB (No Network)
```cmake
# WMB uses direct function calls, no network needed
# Do NOT define TCPCONN, UNIXCONN, LOCALCONN, IPv6
```

---

## System/Platform Definitions

| Definition | Value | Description | When to Use |
|------------|-------|-------------|-------------|
| `CSRG_BASED` | 1 | System is BSD-like | BSD systems |
| `USE_DEV_IO` | 1 | BSD /dev/io for I/O access | FreeBSD |
| `USE_I386_IOPL` | 1 | BSD i386 iopl | NetBSD/OpenBSD i386 |
| `USE_AMD64_IOPL` | 1 | BSD AMD64 iopl | OpenBSD AMD64 |
| `USE_ALPHA_PIO` | 1 | NetBSD PIO alpha IO | NetBSD Alpha |
| `HAS_MTRR_SUPPORT` | 1 | MTRR support available | Linux with MTRR |
| `HAS_MTRR_BUILTIN` | 1 | NetBSD built-in MTRR | NetBSD |
| `BSD44SOCKETS` | 1 | `struct sockaddr_in` has `sin_len` | BSD systems |

### Recommended for WMB
```cmake
# For hosted Linux/SDL build - none of these needed
# For RISC-V bare metal - none of these needed
```

---

## Feature Detection (HAVE_*)

Standard autoconf-style feature detection:

### Header Files
| Definition | Description |
|------------|-------------|
| `HAVE_ALLOCA_H` | Has `<alloca.h>` |
| `HAVE_ASM_MTRR_H` | Has `<asm/mtrr.h>` |
| `HAVE_DIRENT_H` | Has `<dirent.h>` |
| `HAVE_DLFCN_H` | Has `<dlfcn.h>` |
| `HAVE_FCNTL_H` | Has `<fcntl.h>` |
| `HAVE_INTTYPES_H` | Has `<inttypes.h>` |
| `HAVE_LINUX_APM_BIOS_H` | Has `<linux/apm_bios.h>` |
| `HAVE_LINUX_FB_H` | Has `<linux/fb.h>` |
| `HAVE_LINUX_H3600_TS_H` | Has `<linux/h3600_ts.h>` |
| `HAVE_MEMORY_H` | Has `<memory.h>` |
| `HAVE_STDINT_H` | Has `<stdint.h>` |
| `HAVE_STDLIB_H` | Has `<stdlib.h>` |
| `HAVE_STRING_H` | Has `<string.h>` |
| `HAVE_STRINGS_H` | Has `<strings.h>` |
| `HAVE_SYS_DIR_H` | Has `<sys/dir.h>` |
| `HAVE_SYS_IO_H` | Has `<sys/io.h>` |
| `HAVE_SYS_NDIR_H` | Has `<sys/ndir.h>` |
| `HAVE_SYS_STAT_H` | Has `<sys/stat.h>` |
| `HAVE_SYS_TYPES_H` | Has `<sys/types.h>` |
| `HAVE_SYS_VM86_H` | Has `<sys/vm86.h>` |
| `HAVE_UNISTD_H` | Has `<unistd.h>` |

### Functions
| Definition | Description |
|------------|-------------|
| `HAVE_ALLOCA` | Has `alloca()` function/macro |
| `HAVE_GETEUID` | Has `geteuid()` |
| `HAVE_GETOPT` | Has `getopt()` |
| `HAVE_GETOPT_LONG` | Has `getopt_long()` |
| `HAVE_GETUID` | Has `getuid()` |
| `HAVE_LINK` | Has `link()` |
| `HAVE_MEMMOVE` | Has `memmove()` |
| `HAVE_MEMSET` | Has `memset()` |
| `HAVE_MKSTEMP` | Has `mkstemp()` |
| `HAVE_STRCHR` | Has `strchr()` |
| `HAVE_STRRCHR` | Has `strrchr()` |
| `HAVE_STRTOL` | Has `strtol()` |
| `HAVE_VPRINTF` | Has `vprintf()` |

### Libraries
| Definition | Description |
|------------|-------------|
| `HAVE_LIBM` | Has math library (-lm) |

---

## Function Availability (HAS_*)

TinyX-specific function availability checks:

| Definition | Description | Source |
|------------|-------------|--------|
| `HAS_GETDTABLESIZE` | Has `getdtablesize()` | configure.ac:75 |
| `HAS_GETIFADDRS` | Has `getifaddrs()` | configure.ac:77 |
| `HAS_GETPEEREID` | Has `getpeereid()` | configure.ac:79 |
| `HAS_GETPEERUCRED` | Has `getpeerucred()` | configure.ac:81 |
| `HAS_MMAP` | Has `mmap()` | configure.ac:88 |

---

## Scheduler and Debug

| Definition | Value | Description | Source |
|------------|-------|-------------|--------|
| `SMART_SCHEDULE` | 1 | Include time-based scheduler | configure.ac:434 |
| `MONOTONIC_CLOCK` | 1 | Have `clock_gettime()` with `CLOCK_MONOTONIC` | configure.ac:493 |
| `NDEBUG` | 1 | Disable debugging code (release build) | configure.ac:438 |
| `DEBUG` | 1 | Enable debugging code | dix-config.h.in |
| `NEED_STRCASECMP` | 1 | Need to provide `strcasecmp()` | configure.ac:367 |

---

## Vendor/Build Information

String definitions for server identification:

| Definition | Type | Description | Example |
|------------|------|-------------|---------|
| `OSNAME` | string | Operating System Name | "Linux" |
| `XVENDORNAME` | string | Vendor name | "WMB Project" |
| `XVENDORNAMESHORT` | string | Short vendor name | "WMB" |
| `XORG_RELEASE` | string | Vendor release | "1.0.0" |
| `XORG_DATE` | string | Release date | "2026-01-23" |
| `XORG_MAN_VERSION` | string | Man page version | "1.0" |
| `BUILDERADDR` | string | Builder address | "wmb@example.com" |
| `COMPILEDDEFAULTFONTPATH` | string | Default font path | "/usr/share/fonts" |
| `BASE_FONT_PATH` | string | Base font directory | "/fonts" |
| `PROJECTROOT` | string | Overall prefix | "/usr" |

---

## Recommended CMake Configuration

### Minimal WMB Build (Hosted SDL)

```cmake
# framework/CMakeLists.txt additions for TinyX

# ============================================
# CRITICAL - Required for all TinyX builds
# ============================================
add_definitions(-DHAVE_DIX_CONFIG_H)
add_definitions(-DHAVE_KDRIVE_CONFIG_H)
add_definitions(-DKDRIVESERVER=1)
add_definitions(-DKDRIVEDDXACTIONS)

# Endianness (adjust for target)
add_definitions(-DX_BYTE_ORDER=X_LITTLE_ENDIAN)

# 64-bit system (if applicable)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions(-D_XSERVER64=1)
endif()

# ============================================
# Extensions (enable what's needed)
# ============================================
add_definitions(-DDBE=1)                # Double buffering
add_definitions(-DDPMSExtension=1)      # Power management
add_definitions(-DSCREENSAVER=1)        # Screen saver
add_definitions(-DXResExtension=1)      # Resource extension

# ============================================
# Scheduler
# ============================================
add_definitions(-DSMART_SCHEDULE=1)
add_definitions(-DMONOTONIC_CLOCK=1)    # If using clock_gettime

# ============================================
# Standard library features (Linux hosted)
# ============================================
add_definitions(-DHAVE_ALLOCA_H=1)
add_definitions(-DHAVE_DIRENT_H=1)
add_definitions(-DHAVE_DLFCN_H=1)
add_definitions(-DHAVE_FCNTL_H=1)
add_definitions(-DHAVE_INTTYPES_H=1)
add_definitions(-DHAVE_MEMORY_H=1)
add_definitions(-DHAVE_STDINT_H=1)
add_definitions(-DHAVE_STDLIB_H=1)
add_definitions(-DHAVE_STRING_H=1)
add_definitions(-DHAVE_STRINGS_H=1)
add_definitions(-DHAVE_SYS_STAT_H=1)
add_definitions(-DHAVE_SYS_TYPES_H=1)
add_definitions(-DHAVE_UNISTD_H=1)
add_definitions(-DHAVE_MEMMOVE=1)
add_definitions(-DHAVE_MEMSET=1)
add_definitions(-DHAVE_STRCHR=1)
add_definitions(-DHAVE_STRRCHR=1)
add_definitions(-DHAVE_STRTOL=1)
add_definitions(-DHAVE_VPRINTF=1)
add_definitions(-DHAVE_LIBM=1)

# Function availability
add_definitions(-DHAS_MMAP=1)

# ============================================
# Network - DISABLED for WMB
# ============================================
# NOT defined: TCPCONN, UNIXCONN, LOCALCONN, IPv6, XDMCP

# ============================================
# Vendor Information
# ============================================
add_definitions(-DOSNAME="Linux")
add_definitions(-DXVENDORNAME="WMB\ Project")
add_definitions(-DXVENDORNAMESHORT="WMB")
add_definitions(-DXORG_RELEASE="1.0.0")
add_definitions(-DBUILDERADDR="wmb@localhost")
add_definitions(-DCOMPILEDDEFAULTFONTPATH="/fonts")

# ============================================
# Debug vs Release
# ============================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG=1)
else()
    add_definitions(-DNDEBUG=1)
endif()
```

### RISC-V Bare Metal Build

```cmake
# Additional/different definitions for bare metal

# May not have all POSIX functions
# add_definitions(-DHAS_MMAP=1)         # Only if you implement mmap
# add_definitions(-DMONOTONIC_CLOCK=1)  # Only if HAL provides it

# Likely need these
add_definitions(-DNEED_STRCASECMP=1)    # If newlib doesn't have it

# Definitely NOT these
# TCPCONN, UNIXCONN, LOCALCONN, IPv6
# XDMCP, HASXDMAUTH
# HAVE_DLFCN_H (no dynamic loading)
```

---

## Source Reference

All definitions extracted from:
- `tinyx/configure.ac` - Autoconf configuration
- `tinyx/include/dix-config.h.in` - DIX configuration template
- `tinyx/include/kdrive-config.h.in` - KDrive configuration template
- Various `Makefile.am` files - Build flags

---

*Document created: January 23, 2026*
*Purpose: Reference for configuring TinyX preprocessor definitions in CMake*
