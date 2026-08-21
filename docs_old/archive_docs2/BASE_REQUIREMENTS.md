# Base requirements - this is a read only file

## Windows Manager Binary, WMB

These are the base requirements for the WMB.

## Target: Tima OS

The name of the OS is Tima. There is no Specification Document for the OS at the moment, but it will be provided in due time.

## No Client/Server socket connection

Bypass the client/server structure, as this will be a standalone binary.
Internal clients to WMB will call the TinyX API directly.
External clients will use entry point function calls to access X11 API with a "one-liner" X11 API wrapper, all code should be implemented in WMB to minimize the code size for external applications.

## Two-parts X11

The X11 library will be divided in two parts: external application side will contain one-liner wrappers. The WMB side will contain all request structure and calls to TinyX Server APIs.
Events are managed by the WMB-side X11 library, external applications will access events via the one-line wrappers.

## WMB in memory

The final WMB will be sitting in a memory space and will have a fixed address entry point to access the API. It will not be PIC (Position-Independent Code).

## Fixed HAL drivers

The hardware layer display will be framebuffer and predefined (pixel width, height and bits per pixel). There will be no hardware acceleration implemented.
HAL driver will be implemented via KDrive TinyX. Additional HAL drivers shall be created to minimize changes in X11 and JWM to minimum (ideally to zero).

## X11-like API

The application API should have a xlib-like, it must be compatible with X11 client API. No learning curve for X11 developer.

## Build system

all the source and scripts will run at once to build the single binary. This will not include RISC-V boot system.
JWM, TinyX and libX11-wmb are linked together for form the single binary.
The choice of building system is CMake integrated to Visual Studio Code IDE.

## Loading in place

At startup, the underlying OS (Called Tima OS) will bring up the platform, initialize relevant hardware modules, load the WMB in the specified memory address and initialize the WMB.
The WMB should not have any concern about the OS (except via HAL or OS related files and folders), or the RISC-V bootloader system.

## Hosted build

We need to have a hosted build option for development and debug, using Linux machine as host, SDL as graphical interface. Working with context switch, we need to use a context switch library (other than pthread for simplicity) to mimic the infrastructure provided by Tima OS.
