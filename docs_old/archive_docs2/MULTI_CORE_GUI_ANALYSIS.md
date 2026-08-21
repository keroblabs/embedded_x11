# Multi-Core GUI Architecture Analysis

**Date**: January 25, 2026
**Purpose**: Analyze the feasibility of running WMB GUI on a separate thread/RISC-V core with halt capability

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Platform Architecture](#2-current-platform-architecture)
3. [Layer Separation Analysis](#3-layer-separation-analysis)
4. [Shared Resources and Dependencies](#4-shared-resources-and-dependencies)
5. [Single-Threaded Cooperative Model (Immediate Solution)](#5-single-threaded-cooperative-model-immediate-solution)
   - [5.9 Critical Limitation: Single Application Only](#59-critical-limitation-single-application-only)
   - [5A. Multi-Application Execution Model (Required)](#5a-multi-application-execution-model-required)
   - [5B. Hosted Build: Context Switching Alternatives](#5b-hosted-build-context-switching-alternatives)
6. [Threading Model Feasibility (Future)](#6-threading-model-feasibility-future)
7. [Multi-Core RISC-V Model (Recommended Long-Term)](#7-multi-core-risc-v-model-recommended-long-term)
8. [Halt/Sleep Mechanism Design](#8-haltsleep-mechanism-design)
9. [Inter-Core Communication](#9-inter-core-communication)
10. [Implementation Recommendations](#10-implementation-recommendations)
11. [Conclusions](#11-conclusions)

---

## 1. Executive Summary

### Question
Can the WMB GUI subsystem run on a separate thread (or dedicated RISC-V core) that can be halted when no user events or hardware events are pending?

### Answer: **Yes, with careful design**

The current architecture has **good separation** between:
- **Application Layer** (business logic, I/O via Tima OS)
- **GUI Layer** (WMB: TinyX + JWM + libX11-wmb)
- **HAL Layer** (KDrive tima drivers interfacing with OS-provided hardware access)

However, achieving clean halt capability requires:
1. **Event-driven wake mechanism** from Tima OS
2. **Defined communication interface** between Application Core and GUI Core
3. **Careful handling of shared resources** (framebuffer, event queues)

---

## 2. Current Platform Architecture

Based on analysis of the workspace, the architecture is:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        APPLICATION LAYER                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────┐        │
│  │                    Application Code                         │        │
│  │  ┌───────────────────────┐  ┌───────────────────────────┐   │        │
│  │  │       GUI Access      │  │         I/O Access        │   │        │
│  │  │   (via libX11-app)    │  │     (via Tima OS API)     │   │        │
│  │  └───────────┬───────────┘  └─────────────┬─────────────┘   │        │
│  └──────────────┼────────────────────────────┼─────────────────┘        │
│                 │                            │                          │
└─────────────────┼────────────────────────────┼──────────────────────────┘
                  │                            │
                  │ Entry Point Table          │ Tima OS Syscalls
                  │ @ Fixed Address            │
                  ▼                            ▼
┌─────────────────────────────────┐  ┌────────────────────────────────────┐
│         WMB (GUI Layer)         │  │           TIMA OS                  │
├─────────────────────────────────┤  ├────────────────────────────────────┤
│                                 │  │                                    │
│  ┌──────────────────────────┐   │  │  ┌────────────────────────────┐    │
│  │   libX11-wmb (Entry Pt)  │   │  │  │  Filesystem (fopen, etc.)  │    │
│  └────────────┬─────────────┘   │  │  ├────────────────────────────┤    │
│               │                 │  │  │  Serial Port / UART        │    │
│  ┌────────────▼─────────────┐   │  │  ├────────────────────────────┤    │
│  │         JWM              │   │  │  │  Network Stack             │    │
│  │   (Window Manager)       │   │  │  ├────────────────────────────┤    │
│  └────────────┬─────────────┘   │  │  │  Printer Interface         │    │
│               │                 │  │  ├────────────────────────────┤    │
│  ┌────────────▼─────────────┐   │  │  │  Memory Management         │    │
│  │       TinyX (X Server)   │   │  │  ├────────────────────────────┤    │
│  │  ┌───────────────────┐   │   │  │  │  Timer Services            │    │
│  │  │   DIX / MI Core   │   │   │  │  └────────────────────────────┘    │
│  │  ├───────────────────┤   │   │  │                                    │
│  │  │  KDrive Interface │   │   │  │  Provides: libc, syscalls,         │
│  │  └─────────┬─────────┘   │   │  │  HAL access to WMB                 │
│  └────────────┼─────────────┘   │  │                                    │
│               │                 │  └────────────────────────────────────┘
│  ┌────────────▼─────────────┐   │                    │
│  │    KDrive Tima Driver    │   │                    │
│  │  tima_kdrive_system.c    │   │                    │
│  │  tima_kdrive_fb.c        │◄──┼────────────────────┘
│  │  tima_kdrive_kbd.c       │   │    HAL API Calls
│  │  tima_kdrive_mouse.c     │   │
│  └────────────┬─────────────┘   │
│               │                 │
└───────────────┼─────────────────┘
                │
                │ HAL Driver API (hal_drivers.h)
                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    TIMA OS - HAL IMPLEMENTATION                         │
├─────────────────────────────────────────────────────────────────────────┤
│  lcd_driver_*()     - Framebuffer access                                │
│  keypad_driver_*()  - Keyboard input                                    │
│  mouse_driver_*()   - Mouse/pointer input                               │
└─────────────────────────────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         HARDWARE LAYER                                  │
│  Framebuffer @ MMIO, Keyboard Controller, Mouse Controller, Timers      │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Layer Separation Analysis

### 3.1 Application Layer Dependencies

| Dependency | Provider | Notes |
|------------|----------|-------|
| GUI API (`XCreateWindow`, etc.) | WMB via libX11-app | Via entry point table |
| File I/O (`fopen`, `fread`) | Tima OS | Standard libc |
| Memory (`malloc`, `free`) | Tima OS | Standard libc |
| Serial/Network/Printer | Tima OS | OS syscalls |
| Time (`gettimeofday`) | Tima OS | Standard libc |

**Finding**: Application has **clean separation** - GUI access via entry points, I/O via OS.

### 3.2 WMB Layer Dependencies

| Dependency | Provider | Interface |
|------------|----------|-----------|
| Framebuffer | Tima OS HAL | `lcd_driver_get_framebuffer()` |
| Keyboard Input | Tima OS HAL | `keypad_driver_has_data()`, `keypad_driver_read_data()` |
| Mouse Input | Tima OS HAL | `mouse_driver_has_data()`, `mouse_driver_read_data()` |
| Memory | Tima OS | `malloc()`, `free()` |
| File I/O (fonts, config) | Tima OS | `fopen()`, `fread()` |
| Timer | Tima OS | `gettimeofday()` |

**Finding**: WMB depends on Tima OS for **everything** except internal X11 logic.

### 3.3 Current HAL Interface (from `hal_drivers.h`)

```c
// Framebuffer
PIXEL_TYPE_T * lcd_driver_get_framebuffer(void);
uint32_t lcd_driver_get_bits_per_pixel(void);
uint16_t lcd_driver_get_width(void);
uint16_t lcd_driver_get_height(void);
void lcd_driver_update_window(void);

// Keyboard
void keypad_driver_init(void);
bool_t keypad_driver_has_data(void);
uint8_t keypad_driver_read_data(void);

// Mouse
void mouse_driver_init(uint8_t index);
bool_t mouse_driver_has_data(uint8_t index);
uint8_t mouse_driver_read_data(uint8_t index);
```

**Finding**: HAL interface is **polling-based**, not interrupt-driven. This is important for halt design.

---

## 4. Shared Resources and Dependencies

### 4.1 Resources That Must Be Shared Between Cores

| Resource | Access Pattern | Contention Risk |
|----------|---------------|-----------------|
| **Framebuffer** | WMB writes, hardware reads | Low - single writer |
| **Keyboard Buffer** | OS writes, WMB reads | Medium - producer/consumer |
| **Mouse Buffer** | OS writes, WMB reads | Medium - producer/consumer |
| **Heap Memory** | Both cores allocate | High - needs synchronization |
| **Config Files** | Read at startup only | None - read-only after init |

### 4.2 Resource Access by Layer

```
┌──────────────────────────────────────────────────────────────────────┐
│                      SHARED MEMORY MAP                               │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  FRAMEBUFFER (Write: WMB, Read: Display Hardware)           │     │
│  │  @ Fixed MMIO Address                                       │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  INPUT EVENT QUEUE (Write: OS/IRQ Handler, Read: WMB)       │     │
│  │  @ Shared Memory Region                                     │     │
│  │  ┌─────────────────────────────────────────────────────┐    │     │
│  │  │  Keyboard Ring Buffer  │  Mouse Ring Buffer         │    │     │
│  │  └─────────────────────────────────────────────────────┘    │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  GUI COMMAND QUEUE (Write: App, Read: WMB)                  │     │
│  │  @ Shared Memory Region - For App→WMB requests              │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  HEAP (Shared or Partitioned)                               │     │
│  │  Option A: Single heap with mutex                           │     │
│  │  Option B: Separate heaps per core                          │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 5. Single-Threaded Cooperative Model (Immediate Solution)

### 5.1 The Problem: Blocking in a Single-Threaded System

The proposed `tima_WaitForEvents()` would block the entire system:

```c
// PROBLEM: This blocks EVERYTHING in single-threaded environment
void tima_WaitForEvents(void)
{
    while (!keypad_driver_has_data() && !mouse_driver_has_data(0)) {
        __asm__ volatile ("wfi");  // System frozen!
    }
}
```

In a single-threaded system, we can't block - the OS needs to keep running to:
- Handle timers and scheduled tasks
- Process network packets
- Service serial port / UART
- Run background tasks
- Respond to other interrupts

### 5.2 Solution: OS-Owned Main Loop (Recommended)

**Key Insight**: The OS should own the main loop, not WMB.

```
┌─────────────────────────────────────────────────────────────────────────┐
│              OS-OWNED MAIN LOOP ARCHITECTURE                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │                    TIMA OS MAIN LOOP                           │     │
│  │                                                                │     │
│  │   while (system_running) {                                     │     │
│  │       // 1. Poll/handle all system events                      │     │
│  │       os_process_timers();                                     │     │
│  │       os_process_network();                                    │     │
│  │       os_process_serial();                                     │     │
│  │       os_process_background_tasks();                           │     │
│  │                                                                │     │
│  │       // 2. Poll GUI input and let WMB process if available    │     │
│  │       os_poll_gui_input();     // Fills input buffers          │     │
│  │       wmb_process_iteration(); // WMB does ONE iteration       │     │
│  │                                                                │     │
│  │       // 3. If nothing to do, THEN we can sleep                │     │
│  │       if (os_all_idle()) {                                     │     │
│  │           __asm__ volatile ("wfi");  // Safe to sleep now      │     │
│  │       }                                                        │     │
│  │   }                                                            │     │
│  │                                                                │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.3 WMB Non-Blocking API Design

Instead of WMB blocking in `XNextEvent()`, we redesign the execution model:

#### Option A: Polling Mode (Simplest)

WMB provides a non-blocking `wmb_process_iteration()` that the OS calls repeatedly:

```c
// tima_os_main.c - OS owns the main loop

void os_main_loop(void)
{
    while (system_running) {
        // OS does its work
        os_tick();

        // Give WMB a chance to process (non-blocking)
        wmb_process_iteration();

        // Sleep only if ALL subsystems are idle
        if (os_is_idle() && wmb_is_idle()) {
            wfi_with_timer_wakeup();
        }
    }
}
```

```c
// tinyx/kdrive/tima/tima_wmb_api.c

// Non-blocking: process available events, return immediately
void wmb_process_iteration(void)
{
    // 1. Poll input (non-blocking - just check buffers)
    tima_OsPollEvents();

    // 2. If events available, process them
    while (has_pending_x_events()) {
        process_one_x_event();
    }

    // 3. Do any pending rendering
    if (screen_needs_update()) {
        render_damaged_regions();
    }

    // Returns immediately - does NOT block
}

bool wmb_is_idle(void)
{
    return !keypad_driver_has_data() &&
           !mouse_driver_has_data(0) &&
           !has_pending_x_events() &&
           !screen_needs_update();
}
```

#### Option B: Callback Mode

OS notifies WMB when input is available:

```c
// OS calls this when keyboard data arrives (from ISR or polling)
void os_keyboard_data_ready_callback(void)
{
    // Queue the raw data
    enqueue_keyboard_data(read_keyboard_hardware());

    // Signal WMB to process
    wmb_input_available = TRUE;
}

// OS main loop
void os_main_loop(void)
{
    while (system_running) {
        os_tick();

        // Only call WMB if it has work to do
        if (wmb_input_available || wmb_has_pending_work()) {
            wmb_process_iteration();
            wmb_input_available = FALSE;
        }

        if (os_all_idle()) {
            wfi_with_timer_wakeup();
        }
    }
}
```

#### Option C: Cooperative Yield (Most Compatible with Current Design)

`tima_WaitForEvents()` yields to OS instead of blocking:

```c
// tinyx/kdrive/tima/tima_kdrive_system.c

// This is called by XNextEvent() when no events are pending
void tima_WaitForEvents(void)
{
    // DON'T BLOCK! Just yield to OS
    os_yield();  // Give OS a chance to run, then return

    // OS will call us again via wmb_process_iteration()
    // or the app will call XNextEvent() again
}

// OS provides this yield function
void os_yield(void)
{
    // 1. Process any pending OS work
    os_process_timers();
    os_process_network();
    os_process_serial();

    // 2. Poll for new input (fills buffers)
    os_poll_all_input();

    // 3. If truly nothing to do, brief sleep
    if (os_all_idle()) {
        os_sleep_ms(1);  // Very short sleep, not WFI
    }

    // Return to caller (WMB)
}
```

### 5.4 Modified JWM Event Loop

JWM's event loop becomes cooperative:

```c
// jwm/src/event.c - Modified for cooperative execution

void EventLoop(void)
{
    XEvent event;

    for(;;) {
        // Non-blocking check for events
        if (JXPending(display) > 0) {
            JXNextEvent(display, &event);
            ProcessEvent(&event);
        } else {
            // No events - yield to OS instead of blocking
            os_yield();

            // Check if we should exit
            if (!wmb_should_continue()) {
                break;
            }
        }
    }
}
```

Or even simpler - JWM doesn't run a loop, it gets called:

```c
// Alternative: Event-driven JWM (called by OS)

// OS calls this when events are available
void jwm_handle_events(void)
{
    XEvent event;

    // Process all pending events (non-blocking)
    while (JXPending(display) > 0) {
        JXNextEvent(display, &event);
        ProcessEvent(&event);
    }

    // Do any pending window updates
    jwm_update_windows();

    // Return to OS
}
```

### 5.5 Comparison of Approaches

| Approach | Complexity | JWM Changes | OS Changes | Best For |
|----------|------------|-------------|------------|----------|
| **A: Polling Mode** | Low | Minimal | Add `wmb_process_iteration()` call | Simple systems |
| **B: Callback Mode** | Medium | Minimal | Implement callbacks | Interrupt-driven |
| **C: Cooperative Yield** | Low | Minimal | Implement `os_yield()` | Current architecture |

### 5.6 Recommended Implementation: Hybrid Approach

Combine the best of all approaches:

```c
// hal_drivers.h - Extended HAL interface

// Check if system should yield (called by WMB when idle)
void hal_yield_to_os(void);

// Check if WMB should keep running
bool_t hal_wmb_continue(void);

// Timeout yield - return after max_ms or when events available
// Returns: 1 if events available, 0 if timeout
int hal_yield_timeout(uint32_t max_ms);
```

```c
// tinyx/kdrive/tima/tima_kdrive_system.c

void tima_WaitForEvents(void)
{
    if (!os_enabled) return;

    // If events already available, return immediately
    if (keypad_driver_has_data() || mouse_driver_has_data(0)) {
        return;
    }

    // No events - yield to OS with timeout
    // This allows OS to run while waiting for input
    hal_yield_timeout(10);  // Max 10ms, returns early if input arrives

    // Poll again after yield (OS may have filled buffers)
    tima_KeyboardPoll();
    tima_MousePoll();
}
```

```c
// Tima OS implementation

int hal_yield_timeout(uint32_t max_ms)
{
    uint32_t start = os_get_tick_ms();

    while ((os_get_tick_ms() - start) < max_ms) {
        // 1. Let OS do its work
        os_process_timers();
        os_process_network();
        os_process_serial();
        os_run_background_tasks();

        // 2. Poll hardware input
        os_poll_keyboard();  // Fills keypad_driver buffer
        os_poll_mouse();     // Fills mouse_driver buffer

        // 3. Check if GUI input is now available
        if (keypad_driver_has_data() || mouse_driver_has_data(0)) {
            return 1;  // Events available
        }

        // 4. Brief sleep if truly idle (power saving)
        if (os_all_idle()) {
            os_sleep_ms(1);
        }
    }

    return 0;  // Timeout
}
```

### 5.7 Flow Diagram: Cooperative Execution

```
┌──────────────────────────────────────────────────────────────────────────┐
│                 COOPERATIVE EXECUTION FLOW                               │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   Application                    WMB                       Tima OS       │
│       │                          │                            │          │
│       │   XNextEvent()           │                            │          │
│       │─────────────────────────►│                            │          │
│       │                          │                            │          │
│       │                          │  Poll: events pending?     │          │
│       │                          │◄─────────────────────────► │          │
│       │                          │        No                  │          │
│       │                          │                            │          │
│       │                          │  tima_WaitForEvents()      │          │
│       │                          │        │                   │          │
│       │                          │        │  hal_yield_timeout()         │
│       │                          │        │──────────────────►│          │
│       │                          │        │                   │          │
│       │                          │        │      OS does:     │          │
│       │                          │        │      - timers     │          │
│       │                          │        │      - network    │          │
│       │                          │        │      - poll input │          │
│       │                          │        │                   │          │
│       │                          │        │  Input available! │          │
│       │                          │        │◄──────────────────│          │
│       │                          │        │                   │          │
│       │                          │  Poll: events pending?     │          │
│       │                          │◄─────────────────────────► │          │
│       │                          │        Yes                 │          │
│       │                          │                            │          │
│       │   XEvent returned        │                            │          │
│       │◄─────────────────────────│                            │          │
│       │                          │                            │          │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

### 5.8 Key Benefits of Cooperative Model

1. **No blocking** - System never freezes waiting for GUI input
2. **OS keeps running** - Timers, network, serial all continue working
3. **Power efficient** - Can still use brief sleeps when truly idle
4. **Minimal changes** - Works with current single-threaded architecture
5. **Upgrade path** - Same design works when moving to multi-core later

### 5.9 Critical Limitation: Single Application Only

**⚠️ IMPORTANT**: The cooperative yield model described above works for **ONE application only**.

The moment you have multiple applications, the model breaks down:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                 THE MULTI-APPLICATION PROBLEM                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   App1 (Calculator)              App2 (Text Editor)                     │
│   ┌──────────────────┐           ┌──────────────────┐                   │
│   │  int main() {    │           │  int main() {    │                   │
│   │    ...           │           │    ...           │                   │
│   │    while(1) {    │           │    while(1) {    │                   │
│   │      XNextEvent()│           │      XNextEvent()│                   │
│   │      // STUCK!   │           │      // NEVER    │                   │
│   │    }             │           │      // REACHED! │                   │
│   │  }               │           │    }             │                   │
│   └──────────────────┘           └──────────────────┘                   │
│                                                                         │
│   Problem: App1's while loop never exits to let App2 run!               │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

Each application:
- Has its own `main()` function
- Has its own `while(running)` event loop
- Calls `XNextEvent()` which waits for events
- **Never voluntarily exits its loop** to let another app run

**Cooperative yield only helps WITHIN one application** - it lets the OS do background work while that ONE app waits. It does NOT allow switching to another application.

---

## 5A. Multi-Application Execution Model (Required)

### 5A.1 The Fundamental Challenge

Multiple GUI applications cannot coexist without **context switching**:

```
┌─────────────────────────────────────────────────────────────────────────┐
│         WITHOUT CONTEXT SWITCHING - IMPOSSIBLE                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Time ──────────────────────────────────────────────────────────►       │
│                                                                         │
│  App1: ████████████████████████████████████████████████████████████     │
│        main()   while(1) { XNextEvent()... }   RUNS FOREVER             │
│                                                                         │
│  App2: ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░     │
│        NEVER GETS CPU TIME - App1 never yields execution                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│         WITH CONTEXT SWITCHING - REQUIRED                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Time ──────────────────────────────────────────────────────────►       │
│                                                                         │
│  App1: ████░░░░████░░░░████░░░░████░░░░████░░░░████░░░░████░░░░████     │
│        run  wait run  wait run  wait ...  (context saved/restored)      │
│                                                                         │
│  App2: ░░░░████░░░░████░░░░████░░░░████░░░░████░░░░████░░░░████░░░░     │
│        wait run  wait run  wait run  ...  (context saved/restored)      │
│                                                                         │
│  OS:   Scheduler decides which app runs, saves/restores CPU state       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5A.2 What is an Execution Context?

Each application needs its own **execution context** (thread/process state):

```c
// Tima OS - Application Context Structure

typedef struct {
    // CPU Register State (saved on context switch)
    uint32_t pc;           // Program Counter - where to resume
    uint32_t sp;           // Stack Pointer
    uint32_t ra;           // Return Address
    uint32_t s0_s11[12];   // Saved registers (callee-saved)
    uint32_t a0_a7[8];     // Argument registers
    uint32_t t0_t6[7];     // Temporary registers

    // Application State
    uint32_t app_id;       // Unique application ID
    void *stack_base;      // Application's stack memory
    uint32_t stack_size;   // Stack size

    // X11/GUI State
    Display *display;      // Application's X11 display connection
    Window *windows;       // Windows owned by this app
    uint32_t window_count;

    // Scheduling State
    enum {
        APP_STATE_READY,       // Can run
        APP_STATE_RUNNING,     // Currently running
        APP_STATE_WAITING,     // Waiting for event (in XNextEvent)
        APP_STATE_BLOCKED,     // Blocked on I/O
        APP_STATE_TERMINATED   // Finished
    } state;

    // Event waiting info
    uint32_t event_mask;   // What events this app is waiting for

} AppContext;
```

### 5A.3 Context Switch Trigger Points

The OS must perform context switches at strategic points:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    CONTEXT SWITCH TRIGGER POINTS                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. XNextEvent() with no events pending                                 │
│     ────────────────────────────────────────                            │
│     App calls XNextEvent() → no events → OS marks app as WAITING        │
│     → OS switches to another READY app (or WMB/JWM if needed)           │
│                                                                         │
│  2. Timer interrupt (preemptive scheduling)                             │
│     ────────────────────────────────────────                            │
│     Timer fires → OS saves current context → selects next app           │
│     → OS restores next app's context → continues execution              │
│                                                                         │
│  3. I/O completion                                                      │
│     ────────────────────────────────────────                            │
│     Blocked app's I/O completes → app becomes READY                     │
│     → Scheduler may switch to it if higher priority                     │
│                                                                         │
│  4. Explicit yield (cooperative)                                        │
│     ────────────────────────────────────────                            │
│     App calls os_yield() or sleep() → OS switches to another app        │
│                                                                         │
│  5. User interaction (window focus change)                              │
│     ────────────────────────────────────────                            │
│     User clicks different window → JWM changes focus                    │
│     → Events routed to different app → may trigger scheduling           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5A.4 XNextEvent() with Context Switching

This is where the magic happens - `XNextEvent()` becomes a scheduling point:

```c
// libX11-wmb/src/NextEvent.c - WITH CONTEXT SWITCHING

int XNextEvent(Display *dpy, XEvent *event_return)
{
    AppContext *current_app = os_get_current_context();

    while (1) {
        // 1. Poll for pending events (non-blocking)
        tima_OsPollEvents();

        // 2. Check if events available for THIS app
        if (GetEventsQueued(current_app->wmb_client) > 0) {
            // Events available - dequeue and return
            xEvent *serverEvent = DequeueEvent(current_app->wmb_client);
            WireToEvent(serverEvent, event_return);
            return 0;
        }

        // 3. No events - THIS IS THE KEY SCHEDULING POINT
        //    Mark ourselves as waiting and let OS switch to another app
        current_app->state = APP_STATE_WAITING;
        current_app->event_mask = EVENT_MASK_ANY;  // Wake on any event

        os_schedule();  // ◄── CONTEXT SWITCH HAPPENS HERE

        // 4. We resume here when OS switches back to us
        //    (because an event arrived for us, or it's our turn)
        current_app->state = APP_STATE_RUNNING;

        // Loop back to check for events
    }
}
```

```c
// Tima OS - Scheduler

void os_schedule(void)
{
    AppContext *current = current_running_app;
    AppContext *next = NULL;

    // 1. Save current app's CPU state
    save_context(current);

    // 2. Find next app to run
    next = scheduler_select_next();

    // 3. If no apps ready, run OS idle loop (can WFI here)
    if (next == NULL) {
        run_idle_loop();  // Can safely WFI - nothing to do
        return;           // Will be woken by interrupt
    }

    // 4. Restore next app's CPU state and switch
    current_running_app = next;
    restore_context(next);

    // Execution continues in 'next' app - we don't return here
    // until OS switches back to 'current'
}

AppContext* scheduler_select_next(void)
{
    // Check if any WAITING apps now have events
    for (each app in app_list) {
        if (app->state == APP_STATE_WAITING) {
            if (app_has_pending_events(app)) {
                app->state = APP_STATE_READY;
            }
        }
    }

    // Select highest priority READY app
    for (each app in app_list) {
        if (app->state == APP_STATE_READY) {
            return app;
        }
    }

    return NULL;  // No apps ready
}
```

### 5A.5 Application Lifecycle with Context Switching

```
┌──────────────────────────────────────────────────────────────────────────┐
│                    APPLICATION LIFECYCLE                                 │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  USER ACTION              TIMA OS                    APPLICATION         │
│       │                      │                            │              │
│       │  "Launch App2"       │                            │              │
│       │─────────────────────►│                            │              │
│       │                      │                            │              │
│       │                      │  1. Create new context     │              │
│       │                      │     ─────────────────      │              │
│       │                      │     AppContext *ctx =      │              │
│       │                      │       create_context();    │              │
│       │                      │     ctx->stack = alloc();  │              │
│       │                      │     ctx->pc = app2_main;   │              │
│       │                      │     ctx->state = READY;    │              │
│       │                      │                            │              │
│       │                      │  2. Add to scheduler       │              │
│       │                      │     ─────────────────      │              │
│       │                      │     add_to_app_list(ctx);  │              │
│       │                      │                            │              │
│       │                      │  3. Eventually scheduled   │              │
│       │                      │     ─────────────────      │              │
│       │                      │     os_schedule();         │              │
│       │                      │         │                  │              │
│       │                      │         └─────────────────►│              │
│       │                      │                            │              │
│       │                      │                    int main() {           │
│       │                      │                      // App2 starts       │
│       │                      │                      XOpenDisplay();      │
│       │                      │                      XCreateWindow();     │
│       │                      │                      while(1) {           │
│       │                      │                        XNextEvent();◄──┐  │
│       │                      │                           │            │  │
│       │                      │◄──────────────────────────┘            │  │
│       │                      │  (no events - context switch)          │  │
│       │                      │                                        │  │
│       │                      │  ... other apps run ...                │  │
│       │                      │                                        │  │
│       │  Mouse click on      │                                        │  │
│       │  App2's window       │                                        │  │
│       │─────────────────────►│                                        │  │
│       │                      │  Event queued for App2                 │  │
│       │                      │  App2 state → READY                    │  │
│       │                      │  os_schedule() → switch to App2        │  │
│       │                      │────────────────────────────────────────┘  │
│       │                      │                            │              │
│       │                      │                        ProcessEvent();    │
│       │                      │                      }                    │
│       │                      │                    }                      │
│       │                      │                            │              │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

### 5A.6 JWM's Special Role: Internal Client Architecture Options

**⚠️ IMPORTANT TRADE-OFF**: The JWM architecture choice directly impacts the BASE_REQUIREMENTS goal of "minimum to zero code change" in JWM, TinyX, and libX11.

```
┌─────────────────────────────────────────────────────────────────────────┐
│         JWM ARCHITECTURE OPTIONS - TRADE-OFF ANALYSIS                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  BASE REQUIREMENT: Minimum to zero code change in JWM, TinyX, libX11    │
│                                                                         │
│  ═══════════════════════════════════════════════════════════════════    │
│                                                                         │
│  OPTION A: JWM Keeps Event Loop (HONORS BASE REQUIREMENTS)              │
│  ─────────────────────────────────────────────────────────              │
│                                                                         │
│  JWM runs as internal client WITH its own event loop:                   │
│                                                                         │
│    int main() {                    // ← UNCHANGED                       │
│      display = XOpenDisplay();     // ← Uses libX11-wmb                 │
│      // ... initialization ...                                          │
│      while (running) {             // ← UNCHANGED                       │
│        XNextEvent(display, &ev);   // ← Context switch point            │
│        ProcessEvent(&ev);          // ← UNCHANGED                       │
│      }                                                                  │
│    }                                                                    │
│                                                                         │
│  Impact on Base Requirements:                                           │
│    • JWM code changes:   ✅ NONE (or minimal startup changes)           │
│    • TinyX code changes: ✅ Minimal (client registration)               │
│    • libX11 changes:     ✅ NONE (uses libX11-wmb as-is)                │
│                                                                         │
│  Trade-off:                                                             │
│    • JWM needs its own execution context                                │
│    • Context switching required: OS ↔ TinyX ↔ JWM ↔ Apps                │
│    • Slightly more complex runtime                                      │
│                                                                         │
│  ═══════════════════════════════════════════════════════════════════    │
│                                                                         │
│  OPTION B: JWM as Callback-Driven Module (VIOLATES BASE REQUIREMENTS)   │
│  ────────────────────────────────────────────────────────────────────   │
│                                                                         │
│  JWM restructured to callbacks:                                         │
│                                                                         │
│    // NO main()                    // ← MAJOR CHANGE                    │
│    // NO event loop                // ← MAJOR CHANGE                    │
│    void JWM_Init() { ... }         // ← NEW API                         │
│    void JWM_HandleEvent() { ... }  // ← RESTRUCTURED                    │
│    void JWM_Shutdown() { ... }     // ← NEW API                         │
│                                                                         │
│  Impact on Base Requirements:                                           │
│    • JWM code changes:   ❌ SIGNIFICANT (restructure entire app)        │
│    • TinyX code changes: ⚠️ Moderate (call JWM functions)               │
│    • libX11 changes:     ✅ NONE                                        │
│                                                                         │
│  Trade-off:                                                             │
│    • No separate context for JWM                                        │
│    • Simpler runtime (direct function calls)                            │
│    • But VIOLATES base requirement of minimal JWM changes               │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5A.6.1 Recommendation: Option A (Preserve JWM Code)

**To honor the base requirements, JWM should keep its event loop structure.**

This means:
1. JWM is compiled as an internal client (linked with TinyX, not separate process)
2. JWM keeps its `main()` function (renamed to `jwm_main()` to avoid conflict)
3. JWM keeps its `while(running) { XNextEvent(); ... }` loop
4. JWM uses `libX11-wmb` for X11 calls (direct function calls, no IPC)
5. JWM runs in its own execution context managed by Tima OS

```c
// Minimal changes to JWM - only rename main() to avoid linker conflict

// jwm/src/main.c - BEFORE
int main(int argc, char *argv[]) {
    // ... JWM initialization ...
    while (running) {
        XNextEvent(display, &event);
        ProcessEvent(&event);
    }
    return 0;
}

// jwm/src/main.c - AFTER (minimal change)
int jwm_main(int argc, char *argv[]) {  // ← Only rename
    // ... JWM initialization - UNCHANGED ...
    while (running) {
        XNextEvent(display, &event);     // ← Uses libX11-wmb
        ProcessEvent(&event);            // ← UNCHANGED
    }
    return 0;
}
```

### 5A.6.2 Execution Model with JWM Context

```
┌─────────────────────────────────────────────────────────────────────────┐
│         JWM AS INTERNAL CLIENT (CORRECTED MODEL)                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │          TIMA OS SCHEDULER (includes TinyX server work)           │  │
│  │                                                                   │  │
│  │  scheduler_loop() {                                               │  │
│  │    while(1) {                                                     │  │
│  │      // TinyX server work (NOT a separate context)                │  │
│  │      hal_poll_input();           // Read keyboard/mouse           │  │
│  │      tinyx_process_input();      // Convert to xEvents            │  │
│  │      tinyx_route_to_clients();   // Queue events per-client       │  │
│  │                                                                   │  │
│  │      // Scheduler work                                            │  │
│  │      next = select_ready_client();                                │  │
│  │      context_switch(next);       // Switch to JWM or App          │  │
│  │    }                                                              │  │
│  │  }                                                                │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                          │
│           ┌──────────────────┴──────────────────┬────────────────┐      │
│           │                                     │                │      │
│           ▼                                     ▼                ▼      │
│  ┌─────────────────┐                   ┌───────────┐ ┌───────────┐      │
│  │ JWM Context     │                   │App1 Ctx   │ │App2 Ctx   │      │
│  │                 │                   │           │ │           │      │
│  │ jwm_main() {    │                   │ main() {  │ │ main() {  │      │
│  │   while(1) {    │                   │  while(1){│ │  while(1){│      │
│  │     XNextEvent()│◄──── gets ────────│   XNext.. │ │   XNext.. │      │
│  │     Process()   │      events       │  }        │ │  }        │      │
│  │   }             │      from         │ }         │ │ }         │      │
│  │ }               │      TinyX        │           │ │           │      │
│  └────────┬────────┘      queues       └─────┬─────┘ └─────┬─────┘      │
│           │                                  │             │            │
│           └──────────────────────────────────┴─────────────┘            │
│                              │                                          │
│                    ┌─────────▼─────────┐                                │
│                    │  libX11-wmb API   │                                │
│                    │  (Shared by all   │                                │
│                    │   client contexts)│                                │
│                    └─────────┬─────────┘                                │
│                              │                                          │
│                    ┌─────────▼─────────┐                                │
│                    │  TinyX Server     │                                │
│                    │  (runs in sched-  │                                │
│                    │   uler, not as    │                                │
│                    │   own context)    │                                │
│                    └───────────────────┘                                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5A.6.3 Context Switching Flow (Corrected)

```
Time ─────────────────────────────────────────────────────────────────────►

Scheduler   ████░░░░░░░░████░░░░░░░░████░░░░░░░░████░░░░░░░░████░░░░░░░░████
(+TinyX):   poll         poll         poll         poll         poll
            route        route        route        route        route
            select       select       select       select       select

JWM:        ░░░░████░░░░░░░░░░░░░░░░░░░░████░░░░░░░░░░░░████░░░░░░░░░░░░░░░░
            wait proc wait              proc            proc
                 (titlebar click)       (menu)          (focus)

App1:       ░░░░░░░░████░░░░░░░░░░░░████░░░░░░░░████░░░░░░░░░░░░████░░░░░░░░
            wait    proc            proc        proc            proc
                    (button)        (key)       (key)           (expose)

App2:       ░░░░░░░░░░░░████░░░░████░░░░░░░░░░░░░░░░████░░░░░░░░░░░░████░░░░
            wait        proc    proc                proc            proc


Legend:
  ████ = Running (has CPU)
  ░░░░ = Waiting (yielded, no events)

  Scheduler runs when ALL clients have yielded (no events pending)
  Scheduler does TinyX work: poll input, route events to client queues
  Then scheduler picks next READY client and switches to it
```

### 5A.6.4 Revised Context Count (Corrected)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    EXECUTION CONTEXTS (CORRECTED)                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  SCHEDULER / KERNEL (not a switchable context)                          │
│  ─────────────────────────────────────────────                          │
│     - The "main" execution that runs when no client is active           │
│     - Performs TinyX server work: poll input, route events, render      │
│     - Manages context switching                                         │
│     - TinyX is NOT a separate context - server work runs here           │
│                                                                         │
│  CLIENT CONTEXTS (switchable, each with own stack):                     │
│  ──────────────────────────────────────────────                         │
│                                                                         │
│  1. JWM Context                                                         │
│     ─────────────────                                                   │
│     - Window manager with its own event loop                            │
│     - Uses libX11-wmb (internal client)                                 │
│     - JWM code mostly UNCHANGED (only main→jwm_main rename)             │
│                                                                         │
│  2. Application Contexts (one per running app)                          │
│     ────────────────────────────────────────                            │
│     - App1 context: has main(), while(1) { XNextEvent() }               │
│     - App2 context: has main(), while(1) { XNextEvent() }               │
│     - Uses libX11-wmb (internal client)                                 │
│                                                                         │
│  Total: 1 + N contexts (JWM + N applications)                           │
│                                                                         │
│  TinyX does NOT need a context because:                                 │
│    - TinyX is the SERVER, not a client                                  │
│    - Servers don't have event loops that call XNextEvent()              │
│    - Server work (poll, route, render) runs in scheduler                │
│                                                                         │
│  This honors the base requirement: JWM code changes are MINIMAL         │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5A.6.5 Summary: JWM Architecture Decision

| Criteria | Option A (Keep Loop) | Option B (Callbacks) |
|----------|---------------------|---------------------|
| **JWM code changes** | ✅ Minimal (rename main) | ❌ Major restructure |
| **Base requirements** | ✅ Honored | ❌ Violated |
| **Execution contexts** | 1+N (JWM + Apps) | N (Apps only) |
| **Runtime complexity** | Moderate | Simpler |
| **Maintenance** | JWM updates easy | JWM updates hard |

**Note**: TinyX server work runs in the scheduler, not as a separate context.

**Recommendation: Option A** - Keep JWM's event loop to honor base requirements. JWM runs as an internal client with its own context, just like external applications.
```

### 5A.7 Complete System Architecture with Context Switching

```
┌─────────────────────────────────────────────────────────────────────────┐
│           COMPLETE MULTI-APPLICATION ARCHITECTURE                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                     APPLICATION CONTEXTS                        │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐     │    │
│  │  │   JWM     │  │   App1    │  │   App2    │  │   App3    │     │    │
│  │  │(priority 1)│  │(priority 2)│ │(priority 2)│ │(priority 2)│   │    │
│  │  │           │  │           │  │           │  │           │     │    │
│  │  │ main() {  │  │ main() {  │  │ main() {  │  │ main() {  │     │    │
│  │  │  while(1){│  │  while(1){│  │  while(1){│  │  while(1){│     │    │
│  │  │   XNext.. │  │   XNext.. │  │   XNext.. │  │   XNext.. │     │    │
│  │  │  }        │  │  }        │  │  }        │  │  }        │     │    │
│  │  │ }         │  │ }         │  │ }         │  │ }         │     │    │
│  │  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘     │    │
│  │        │              │              │              │           │    │
│  │        └──────────────┴──────────────┴──────────────┘           │    │
│  │                              │                                  │    │
│  │                    ┌─────────▼─────────┐                        │    │
│  │                    │  libX11-wmb API   │                        │    │
│  │                    │  (Shared by all)  │                        │    │
│  │                    └─────────┬─────────┘                        │    │
│  └──────────────────────────────┼──────────────────────────────────┘    │
│                                 │                                       │
│  ┌──────────────────────────────▼──────────────────────────────────┐    │
│  │                         WMB (TinyX)                             │    │
│  │  ┌────────────────────────────────────────────────────────┐     │    │
│  │  │  Event Queue (per-client)                              │     │    │
│  │  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐    │     │    │
│  │  │  │JWM queue│  │App1 que │  │App2 que │  │App3 que │    │     │    │
│  │  │  └─────────┘  └─────────┘  └─────────┘  └─────────┘    │     │    │
│  │  └────────────────────────────────────────────────────────┘     │    │
│  │                                                                 │    │
│  │  Window tree, focus management, event routing                   │    │
│  └────────────────────────────────┬────────────────────────────────┘    │
│                                   │                                     │
│  ┌────────────────────────────────▼────────────────────────────────┐    │
│  │                    TIMA OS KERNEL                               │    │
│  │                                                                 │    │
│  │  ┌────────────────────────────────────────────────────────┐     │    │
│  │  │  Scheduler                                             │     │    │
│  │  │  - Maintains list of AppContexts                       │     │    │
│  │  │  - Context switch on XNextEvent (no events)            │     │    │
│  │  │  - Context switch on timer interrupt (preemptive)      │     │    │
│  │  │  - Wakes apps when their events arrive                 │     │    │
│  │  └────────────────────────────────────────────────────────┘     │    │
│  │                                                                 │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │    │
│  │  │   Timers    │  │   Network   │  │   Serial    │              │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘              │    │
│  │                                                                 │    │
│  │  ┌────────────────────────────────────────────────────────┐     │    │
│  │  │  HAL (keyboard, mouse, framebuffer)                    │     │    │
│  │  └────────────────────────────────────────────────────────┘     │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5A.8 Summary: Context Switching Requirements

| Requirement | Description | Provided By |
|-------------|-------------|-------------|
| **AppContext struct** | Stores CPU state, stack, app info | Tima OS |
| **Context save/restore** | Save registers on switch, restore on resume | Tima OS (assembly) |
| **Scheduler** | Decides which app runs next | Tima OS |
| **App state tracking** | READY, RUNNING, WAITING, BLOCKED | Tima OS |
| **Timer interrupt** | Preemptive scheduling trigger | Tima OS + Hardware |
| **XNextEvent integration** | Triggers context switch when no events | libX11-wmb |
| **Event-to-app routing** | Wakes correct app when event arrives | TinyX + Tima OS |
| **App launch** | Creates new context, starts at main() | Tima OS |
| **TinyX-OS linkage** | ClientRec ↔ AppContext pointer | TinyX (see 5B.5.3) |

**Note**: The TinyX-OS linkage (last row) is critical. When TinyX routes an event to a client, it must wake the correct OS context. See Section 5B.5.3 for the detailed linkage mechanism.

### 5A.9 Minimum Viable Implementation

For initial multi-application support, Tima OS needs:

```c
// Minimum Tima OS API for multi-application

// Context management
AppContext* os_create_context(void (*entry_point)(void), size_t stack_size);
void os_destroy_context(AppContext *ctx);

// Scheduling
void os_schedule(void);              // Trigger context switch
void os_yield(void);                 // Cooperative yield
void os_mark_waiting(uint32_t mask); // Mark current app as waiting
void os_mark_ready(AppContext *ctx); // Wake an app

// App launch (called by JWM)
int os_launch_application(const char *path);

// Current context
AppContext* os_get_current_context(void);
```

---

## 5B. Hosted Build: Context Switching Alternatives

### 5B.1 The Hosted Build Requirement

Per BASE_REQUIREMENTS.md, the hosted build (Linux/macOS with SDL) must:
- Run on development machines for testing and debugging
- Mimic Tima OS context switch infrastructure
- **NOT use pthread** (for simplicity and closer match to embedded behavior)
- Support the same API that Tima OS will provide

```
┌─────────────────────────────────────────────────────────────────────────┐
│                 HOSTED BUILD CONTEXT SWITCHING                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  TARGET (Tima OS)                    HOSTED (Linux + SDL)               │
│  ────────────────                    ────────────────────               │
│                                                                         │
│  ┌─────────────────────┐             ┌─────────────────────┐            │
│  │  Tima OS Scheduler  │             │  Hosted Shim Layer  │            │
│  │  - HW context switch│             │  - SW context switch│            │
│  │  - RISC-V registers │             │  - x86/ARM registers│            │
│  │  - Real interrupts  │             │  - Cooperative yield│            │
│  └─────────────────────┘             └─────────────────────┘            │
│            │                                   │                        │
│            └───────────── SAME API ────────────┘                        │
│                           │                                             │
│            ┌──────────────▼──────────────────┐                          │
│            │    os_create_context()          │                          │
│            │    os_destroy_context()         │                          │
│            │    os_schedule()                │                          │
│            │    os_yield()                   │                          │
│            │    os_get_current_context()     │                          │
│            │    os_launch_application()      │                          │
│            └─────────────────────────────────┘                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5B.2 Context Switching Library Options (Non-pthread)

| Library | Type | Pros | Cons |
|---------|------|------|------|
| **ucontext (POSIX)** | User-space contexts | Standard POSIX, simple API | Deprecated on macOS, overhead |
| **libco** | Coroutines | Tiny (~200 LOC), portable | Manual memory management |
| **minicoro** | Single-header coroutines | Zero dependencies, portable | Less mature |
| **setjmp/longjmp** | Primitive | Everywhere available | Tricky stack management |
| **Protothreads** | Stackless | Ultra-lightweight | Requires code restructuring |

### 5B.3 Recommended: libco (by byuu/Near)

**libco** is the best fit because:
1. **Tiny**: ~200 lines of code, easy to audit and embed
2. **Portable**: Works on Linux, macOS, Windows (x86, x86_64, ARM, ARM64)
3. **Simple API**: Only 5 functions, very close to what we need
4. **Proven**: Used in bsnes/higan emulator for years
5. **Public domain**: No license complications

```c
// libco API (complete)
typedef void* cothread_t;

cothread_t co_active(void);                    // Get current context
cothread_t co_create(unsigned int size, void (*entry)(void));  // Create context
void       co_delete(cothread_t cothread);     // Destroy context
void       co_switch(cothread_t cothread);     // Switch to context
```

### 5B.4 Mapping Tima OS API to libco

```c
// hosted/os_shim.h - Hosted build context switch shim using libco

#include "libco/libco.h"

// Our context structure wraps libco
typedef struct {
    cothread_t    co_thread;       // libco coroutine handle
    uint32_t      app_id;
    enum {
        APP_STATE_READY,
        APP_STATE_RUNNING,
        APP_STATE_WAITING,
        APP_STATE_TERMINATED
    } state;
    uint32_t      event_mask;
    const char   *app_name;
} AppContext;

// Context list (simple array for hosted build)
#define MAX_CONTEXTS 16
static AppContext contexts[MAX_CONTEXTS];
static int context_count = 0;
static AppContext *current_context = NULL;
static cothread_t scheduler_thread = NULL;

//=============================================================================
// API Implementation
//=============================================================================

void os_init(void)
{
    // Scheduler runs in the "main" thread
    scheduler_thread = co_active();
    context_count = 0;
    current_context = NULL;
}

AppContext* os_create_context(void (*entry_point)(void), size_t stack_size)
{
    if (context_count >= MAX_CONTEXTS) return NULL;

    AppContext *ctx = &contexts[context_count++];
    ctx->co_thread = co_create((unsigned int)stack_size, entry_point);
    ctx->app_id = context_count;
    ctx->state = APP_STATE_READY;
    ctx->event_mask = 0;
    ctx->app_name = "app";

    return ctx;
}

void os_destroy_context(AppContext *ctx)
{
    if (ctx && ctx->co_thread) {
        co_delete(ctx->co_thread);
        ctx->co_thread = NULL;
        ctx->state = APP_STATE_TERMINATED;
    }
}

AppContext* os_get_current_context(void)
{
    return current_context;
}

void os_schedule(void)
{
    // Return to scheduler to pick next context
    co_switch(scheduler_thread);
}

void os_yield(void)
{
    // Same as schedule for cooperative multitasking
    os_schedule();
}

void os_mark_waiting(uint32_t mask)
{
    if (current_context) {
        current_context->state = APP_STATE_WAITING;
        current_context->event_mask = mask;
    }
    os_schedule();
}

void os_mark_ready(AppContext *ctx)
{
    if (ctx) {
        ctx->state = APP_STATE_READY;
    }
}
```

### 5B.5 Scheduler Implementation (Hosted)

**Important**: The scheduler itself performs TinyX server work (polling hardware, routing events). TinyX is NOT a separate context - it's the **server** that provides events, not a client that consumes them.

```c
// hosted/scheduler.c - Scheduler + TinyX server work

void scheduler_run(void)
{
    os_init();

    // Create JWM context (internal client with event loop)
    AppContext *jwm_ctx = os_create_context(jwm_main, 64*1024);
    jwm_ctx->app_name = "JWM";

    // Note: NO TinyX context! TinyX work is done HERE in scheduler

    // Main scheduler loop
    while (1) {
        //=====================================================================
        // PART 1: TinyX SERVER WORK (done in scheduler, not separate context)
        //=====================================================================

        // 1a. Poll SDL events and convert to X11 input events
        sdl_poll_events();  // Fills HAL input buffers

        // 1b. TinyX processes raw input into X11 events
        tinyx_process_input();  // keyboard/mouse → xEvents

        // 1c. TinyX routes events to appropriate client queues
        tinyx_route_events();   // Determine which client gets each event

        //=====================================================================
        // PART 2: SCHEDULER WORK (context switching)
        //=====================================================================

        // 2a. Check if any WAITING clients now have events
        for (int i = 0; i < context_count; i++) {
            if (contexts[i].state == APP_STATE_WAITING) {
                if (wmb_client_has_events(contexts[i].app_id)) {
                    contexts[i].state = APP_STATE_READY;
                }
            }
        }

        // 2b. Find next READY client (round-robin)
        AppContext *next = scheduler_select_next();

        if (next) {
            // 2c. Switch to selected client context
            current_context = next;
            next->state = APP_STATE_RUNNING;

            co_switch(next->co_thread);  // ◄── CONTEXT SWITCH TO CLIENT

            // 2d. Client yielded back (called os_schedule/os_yield)
            if (current_context->state == APP_STATE_RUNNING) {
                current_context->state = APP_STATE_READY;
            }
        } else {
            // No clients ready - small sleep to avoid CPU spin
            SDL_Delay(1);
        }

        // 2e. Check for SDL quit
        if (sdl_should_quit()) {
            break;
        }
    }
}

static AppContext* scheduler_select_next(void)
{
    static int last_index = -1;

    // Round-robin starting from last+1
    for (int i = 0; i < context_count; i++) {
        int idx = (last_index + 1 + i) % context_count;
        if (contexts[idx].state == APP_STATE_READY) {
            last_index = idx;
            return &contexts[idx];
        }
    }

    return NULL;  // No ready clients
}
```

### 5B.5.1 Why No Separate TinyX Context?

```
┌─────────────────────────────────────────────────────────────────────────┐
│              TinyX IS THE SERVER, NOT A CLIENT                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  X11 CLIENTS (have event loops):       X11 SERVER (provides events):    │
│  ──────────────────────────────        ─────────────────────────────    │
│                                                                         │
│  JWM:                                  TinyX:                           │
│    while(1) {                            - Polls hardware               │
│      XNextEvent(&event);  ◄────────────  - Creates xEvents              │
│      ProcessEvent(&event);               - Routes to client queues      │
│    }                                     - Renders to framebuffer       │
│                                                                         │
│  App1:                                 TinyX does NOT call XNextEvent!  │
│    while(1) {                          TinyX PROVIDES the events that   │
│      XNextEvent(&event);  ◄────────────  XNextEvent returns.            │
│      ProcessEvent(&event);                                              │
│    }                                                                    │
│                                                                         │
│  Therefore:                                                             │
│    - Clients need their own context (to run their event loops)          │
│    - TinyX does NOT need a context (it's not an event loop consumer)    │
│    - TinyX work runs in the scheduler's main loop                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5B.5.2 Revised Context Count

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    EXECUTION CONTEXTS (CORRECTED)                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  SCHEDULER (not a switchable context - it's the "kernel")               │
│  ─────────────────────────────────────────────────────────              │
│    - Runs in main() thread                                              │
│    - Performs TinyX server work (poll, route, render)                   │
│    - Manages context switching via libco                                │
│    - Never yields - it's the thing that RECEIVES yields                 │
│                                                                         │
│  CLIENT CONTEXTS (switchable, each with their own stack):               │
│  ─────────────────────────────────────────────────────────              │
│    1. JWM context - window manager, internal client                     │
│    2. App1 context - user application                                   │
│    3. App2 context - user application                                   │
│    ... etc                                                              │
│                                                                         │
│  Total switchable contexts: 1 + N (JWM + N applications)                │
│                                                                         │
│  NOT 2+N - TinyX is not a separate context!                             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5B.5.3 TinyX-Scheduler Client Linkage

The critical question: **How does TinyX tell the scheduler which context to wake?**

TinyX has its own client model (`ClientRec` structures), and the scheduler has `AppContext` structures. These must be linked so that when TinyX routes an event to a client queue, the scheduler knows which context to mark READY.

```
┌─────────────────────────────────────────────────────────────────────────┐
│              TINYX CLIENT ←→ SCHEDULER CONTEXT LINKAGE                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  TinyX World                           OS Scheduler World               │
│  ────────────                          ───────────────────              │
│                                                                         │
│  ┌──────────────────┐                  ┌──────────────────┐             │
│  │  ClientRec       │                  │  AppContext      │             │
│  │  (X11 client)    │                  │  (OS context)    │             │
│  ├──────────────────┤                  ├──────────────────┤             │
│  │  int client_id   │ ═══════════════► │  uint32_t app_id │             │
│  │  EventQueue *q   │                  │  cothread_t co   │             │
│  │  Window focus    │                  │  state           │             │
│  │  GrabState       │                  │  stack           │             │
│  │  ...             │                  │  ...             │             │
│  │                  │                  │                  │             │
│  │  AppContext *ctx │──────────────────│  (pointer link)  │             │
│  │  (NEW FIELD)     │                  │                  │             │
│  └──────────────────┘                  └──────────────────┘             │
│                                                                         │
│  Linkage Options:                                                       │
│    A. Store AppContext* in ClientRec (direct pointer)                   │
│    B. Lookup table: client_id → AppContext*                             │
│    C. Shared ID: client_id == app_id (same value)                       │
│                                                                         │
│  RECOMMENDED: Option A (direct pointer) - fastest, simplest             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 5B.5.3.1 Reality Check: Current libX11 and TinyX Architecture

**⚠️ IMPORTANT**: The code examples in this section are **proposed WMB modifications**, NOT descriptions of existing code. Let's clarify what actually exists:

**Current libX11 (`libX11/src/OpenDis.c`)**:
```c
// ACTUAL CODE - uses XCB for socket connection to X server
Display* XOpenDisplay(const char *display) {
    // ...
    if(!_XConnectXCB(dpy, display, &iscreen)) {  // ← Socket connection!
        OutOfMemory(dpy);
        return NULL;
    }
    // ... parses setup from xcb_get_setup(dpy->xcb->connection) ...
}
```

**Current TinyX (`tinyx/dix/events.c`)**:
```c
// ACTUAL CODE - writes events to socket via OsCommPtr
void WriteEventsToClient(ClientPtr pClient, int count, xEvent *events) {
    // ... sets sequence numbers ...
    // Eventually calls:
    WriteToClient(pClient, count * sizeof(xEvent), (char *) events);
}

// tinyx/os/io.c - ACTUAL CODE - uses file descriptor
int WriteToClient(ClientPtr who, int count, const char *buf) {
    OsCommPtr oc = (OsCommPtr)who->osPrivate;  // ← Socket!
    // ... writes to oc->fd (file descriptor) ...
}
```

**Current TinyX Client (`tinyx/os/osdep.h`)**:
```c
// ACTUAL CODE - OsCommRec contains socket fd
typedef struct _osComm {
    int fd;                              // ← SOCKET file descriptor!
    ConnectionInputPtr input;
    ConnectionOutputPtr output;
    XID auth_id;
    CARD32 conn_time;
    struct _XtransConnInfo *trans_conn;  // ← Transport connection
} OsCommRec, *OsCommPtr;
```

**The Problem**: Standard X11 is designed for **socket-based IPC** between separate processes. libX11 connects via XCB to the X server. TinyX reads/writes via file descriptors.

**For WMB (internal client model)**: We need to **replace** the socket layer, NOT just "add hooks". This is documented in `X11_CLIENT_SERVER_ANALYSIS.md`.

#### 5B.5.3.2 WMB Linkage Design (Proposed Changes)

For the internal client model, we need to replace the socket-based communication with direct memory queues. Here's the proposed design:

```
┌─────────────────────────────────────────────────────────────────────────┐
│              TINYX CLIENT ←→ SCHEDULER CONTEXT LINKAGE                  │
│                        (WMB PROPOSED DESIGN)                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  TinyX World                           OS Scheduler World               │
│  ────────────                          ───────────────────              │
│                                                                         │
│  ┌──────────────────┐                  ┌──────────────────┐             │
│  │  ClientRec       │                  │  AppContext      │             │
│  │  (X11 client)    │                  │  (OS context)    │             │
│  ├──────────────────┤                  ├──────────────────┤             │
│  │  int index       │                  │  uint32_t app_id │             │
│  │  pointer osPri-  │═══════════════►  │  cothread_t co   │             │
│  │    vate ═══════════════════════════►│  state           │             │
│  │  (WmbClientPtr)  │                  │  stack           │             │
│  └──────────────────┘                  └──────────────────┘             │
│                                                                         │
│  Key Insight: ClientRec.osPrivate already exists!                       │
│  Currently points to OsCommRec (socket).                                │
│  For WMB, we replace it with WmbClientRec (memory queues + context).    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**The `osPrivate` Hook**: TinyX's `ClientRec` already has `pointer osPrivate` - this is the key integration point. Currently it points to `OsCommRec` (sockets). For WMB, we create `WmbClientRec`:

```c
// PROPOSED: wmb/wmb_client.h - Replaces OsCommRec for internal clients

typedef struct _wmbClient {
    // Request buffer (client → server)
    char *requestBuffer;
    int requestBufferSize;
    int requestBytesReady;

    // Reply/Event queue (server → client)
    char *eventBuffer;
    int eventBufferSize;
    int eventHead;
    int eventTail;

    // OS Context linkage (THE KEY!)
    void *os_context;       // Points to AppContext

    // Setup info (cached from initial connection)
    void *setupData;
    int setupDataSize;

} WmbClientRec, *WmbClientPtr;
```

#### Client Registration (Proposed WMB Flow)

```c
// PROPOSED: libX11-wmb/src/OpenDis.c - Internal client version

Display* XOpenDisplay(const char *display_name)
{
    Display *dpy;

    // 1. Allocate Display structure (same as standard libX11)
    if ((dpy = Xcalloc(1, sizeof(Display))) == NULL) {
        return NULL;
    }

    // 2. WMB CHANGE: Create internal client via TinyX, NOT socket
    //    This replaces _XConnectXCB()
    WmbClientPtr wmb_client = wmb_connect_internal(dpy);
    if (!wmb_client) {
        Xfree(dpy);
        return NULL;
    }

    // 3. WMB CHANGE: Link to OS context for scheduling
    AppContext *ctx = os_get_current_context();
    wmb_client->os_context = ctx;

    // 4. Parse setup data (similar to standard, but from memory not socket)
    if (!wmb_parse_setup(dpy, wmb_client)) {
        Xfree(dpy);
        return NULL;
    }

    // ... rest of Display initialization (same as standard) ...

    return dpy;
}
```

#### Event Routing with Context Wake-up (Proposed)

The existing `WriteEventsToClient()` function needs modification:

```c
// PROPOSED: tinyx/dix/events.c - Modified for WMB

void WriteEventsToClient(ClientPtr pClient, int count, xEvent *events)
{
    // ... existing sequence number setup ...

    // WMB CHANGE: Check if internal client vs external (socket) client
    WmbClientPtr wmb = WmbGetClient(pClient);
    if (wmb) {
        // Internal client - write to memory queue
        wmb_queue_events(wmb, events, count);

        // CRITICAL: Wake the client's OS context!
        if (wmb->os_context) {
            os_mark_ready((AppContext*)wmb->os_context);
        }
    } else {
        // External client - use standard socket write (unchanged)
        WriteToClient(pClient, count * sizeof(xEvent), (char *) events);
    }
}
```

#### Why `osPrivate` is the Right Hook

| Approach | Pros | Cons |
|----------|------|------|
| **Use existing `osPrivate`** | ✅ No struct changes, existing pattern | Need to identify WMB vs socket clients |
| Add new field to ClientRec | Clean separation | ❌ Modifies core TinyX struct |
| Lookup table | No TinyX changes | O(n) lookup, extra memory |

**Recommendation**: Use `osPrivate` with a type discriminator:

```c
// At start of both OsCommRec and WmbClientRec
typedef struct _clientPrivateHeader {
    enum { CLIENT_TYPE_SOCKET, CLIENT_TYPE_WMB } type;
} ClientPrivateHeader;

// Then in code:
ClientPrivateHeader *hdr = (ClientPrivateHeader*)pClient->osPrivate;
if (hdr->type == CLIENT_TYPE_WMB) {
    WmbClientPtr wmb = (WmbClientPtr)pClient->osPrivate;
    // ... internal client handling ...
}
```

This approach:
1. **Reuses existing TinyX infrastructure** (`osPrivate` pointer)
2. **Allows mixed mode** (socket clients AND internal clients simultaneously)
3. **Minimal TinyX changes** (just check type before WriteToClient)

#### Scheduler Perspective

The scheduler doesn't need to know about X11 internals. It just sees contexts that are READY or WAITING:

```c
// Scheduler main loop

void scheduler_loop(void)
{
    while (running) {
        // 1. TinyX server work (polls HAL, routes events)
        //    This internally calls os_mark_ready() for clients with new events
        tinyx_poll_and_dispatch();

        // 2. Select next READY context (scheduler doesn't care WHY it's ready)
        AppContext *next = select_ready_context();

        // 3. Context switch to it
        if (next) {
            context_switch(next);
        } else {
            // All clients waiting, nothing to do - can sleep/WFI
            idle_wait();
        }
    }
}
```

#### Complete Event Flow with Linkage

```
┌─────────────────────────────────────────────────────────────────────────┐
│              COMPLETE EVENT FLOW: HAL → CLIENT CONTEXT                  │
│                        (WMB PROPOSED DESIGN)                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. HAL Input                                                           │
│     ───────────                                                         │
│     keyboard_driver_read() → raw keycode                                │
│                                                                         │
│  2. TinyX KDrive converts to X event                                    │
│     ─────────────────────────────────                                   │
│     KdEnqueueKeyboardEvent() → xEvent (KeyPress)                        │
│                                                                         │
│  3. TinyX DIX determines destination client                             │
│     ────────────────────────────────────────                            │
│     - Check keyboard grab                                               │
│     - Check focus window                                                │
│     - Find ClientRec that owns focus window                             │
│                                                                         │
│  4. TinyX queues event AND wakes OS context (WMB CHANGE)                │
│     ────────────────────────────────────────────────────                │
│     WriteEventsToClient(pClient, 1, &event) {                           │
│         WmbClientPtr wmb = WmbGetClient(pClient);                       │
│         wmb_queue_events(wmb, &event, 1);     // Memory queue           │
│         os_mark_ready(wmb->os_context);       // ◄─ SCHEDULER LINKAGE   │
│     }                                                                   │
│                                                                         │
│  5. Scheduler sees READY context, switches to it                        │
│     ──────────────────────────────────────────                          │
│     next = select_ready_context();  // Returns the woken client         │
│     context_switch(next);                                               │
│                                                                         │
│  6. Client resumes in XNextEvent(), gets the event                      │
│     ──────────────────────────────────────────────                      │
│     // Inside XNextEvent(), after context switch back:                  │
│     event = wmb_dequeue_event(wmb);  // From memory queue               │
│     return event;  // App receives the event                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### Summary: Files Requiring Modification for TinyX-Scheduler Linkage

| File | Change | Purpose |
|------|--------|---------|
| `tinyx/os/osdep.h` | Add `WmbClientRec` struct | Internal client state |
| `tinyx/dix/events.c` | Modify `WriteEventsToClient()` | Route to memory queue + wake context |
| `tinyx/os/connection.c` | Add `wmb_create_client()` | Create internal clients |
| `libX11-wmb/src/OpenDis.c` | Replace `_XConnectXCB()` | Use internal connection |
| `libX11-wmb/src/XlibInt.c` | Modify event reading | Read from memory queue |

### 5B.6 Application Launch (Hosted)

```c
// hosted/app_launcher.c - Application launch for hosted build

int os_launch_application(const char *path)
{
    // In hosted build, "applications" are compiled-in functions
    // We use a simple name-to-function mapping

    void (*entry_point)(void) = find_app_entry(path);
    if (!entry_point) {
        printf("Application not found: %s\n", path);
        return -1;
    }

    AppContext *ctx = os_create_context(entry_point, 64*1024);
    if (!ctx) {
        printf("Failed to create context for: %s\n", path);
        return -1;
    }

    ctx->app_name = path;
    printf("Launched application: %s (context %d)\n", path, ctx->app_id);

    return ctx->app_id;
}

// Registry of built-in test applications
typedef struct {
    const char *name;
    void (*entry)(void);
} AppEntry;

static AppEntry app_registry[] = {
    { "xclock",     xclock_main     },
    { "xeyes",      xeyes_main      },
    { "calculator", calculator_main },
    { "notepad",    notepad_main    },
    { NULL, NULL }
};

static void (*find_app_entry(const char *path))(void)
{
    for (int i = 0; app_registry[i].name; i++) {
        if (strcmp(app_registry[i].name, path) == 0) {
            return app_registry[i].entry;
        }
    }
    return NULL;
}
```

### 5B.7 Build Configuration

```cmake
# CMakeLists.txt - Hosted build with libco

option(HOSTED_BUILD "Build for hosted (Linux/macOS) development" ON)

if(HOSTED_BUILD)
    # Add libco source (it's just one .c file)
    add_library(libco STATIC
        hosted/libco/libco.c
    )

    # Add hosted shim layer
    add_library(os_shim STATIC
        hosted/os_shim.c
        hosted/scheduler.c
        hosted/app_launcher.c
    )
    target_link_libraries(os_shim libco)

    # SDL for graphics
    find_package(SDL2 REQUIRED)

    # Main executable
    add_executable(wmb_hosted
        # TinyX sources
        ${TINYX_SOURCES}
        # JWM sources
        ${JWM_SOURCES}
        # libX11-wmb sources
        ${LIBX11_WMB_SOURCES}
        # Test applications
        hosted/apps/xclock.c
        hosted/apps/calculator.c
        # Hosted main
        hosted/main.c
    )
    target_link_libraries(wmb_hosted os_shim SDL2::SDL2)

else()
    # Target build for Tima OS
    # os_* functions provided by Tima OS
endif()
```

### 5B.8 Hosted Main Entry Point

```c
// hosted/main.c - Entry point for hosted build

#include <SDL2/SDL.h>
#include "os_shim.h"
#include "sdl_hal.h"

int main(int argc, char *argv[])
{
    // 1. Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // 2. Create SDL window and renderer (HAL will use these)
    if (sdl_hal_init(800, 600, 32) < 0) {
        fprintf(stderr, "sdl_hal_init failed\n");
        return 1;
    }

    // 3. Run the scheduler (never returns until quit)
    scheduler_run();

    // 4. Cleanup
    sdl_hal_shutdown();
    SDL_Quit();

    return 0;
}
```

### 5B.9 Comparison: Target vs Hosted

| Aspect | Target (Tima OS) | Hosted (libco + SDL) |
|--------|------------------|---------------------|
| **Context switch** | Hardware (RISC-V CSRs) | Software (libco) |
| **Interrupts** | Real hardware IRQs | SDL event polling |
| **Scheduler** | OS kernel scheduler | Simple round-robin |
| **Framebuffer** | Memory-mapped HW | SDL_Texture |
| **Keyboard** | HAL interrupt | SDL_KEYDOWN/UP |
| **Mouse** | HAL interrupt | SDL_MOUSEMOTION/BUTTON |
| **App launch** | Load from filesystem | Lookup in registry |
| **Stack per ctx** | Real memory allocation | libco managed |

### 5B.10 Alternative: ucontext (POSIX)

If libco is not desired, POSIX `ucontext` is an alternative (Linux only, deprecated on macOS):

```c
// Alternative using ucontext (POSIX)
#include <ucontext.h>

typedef struct {
    ucontext_t  uc;
    uint32_t    app_id;
    int         state;
    char        stack[64*1024];
} AppContext;

AppContext* os_create_context(void (*entry)(void), size_t stack_size)
{
    AppContext *ctx = malloc(sizeof(AppContext));

    getcontext(&ctx->uc);
    ctx->uc.uc_stack.ss_sp = ctx->stack;
    ctx->uc.uc_stack.ss_size = sizeof(ctx->stack);
    ctx->uc.uc_link = &scheduler_context;  // Return to scheduler when done

    makecontext(&ctx->uc, entry, 0);

    return ctx;
}

void os_schedule(void)
{
    swapcontext(&current_context->uc, &scheduler_context);
}
```

**Note**: ucontext is deprecated on macOS and may be removed. libco is more portable.

---

## 6. Threading Model Feasibility (Future)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     SINGLE CORE - TWO THREADS                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌────────────────────────┐    ┌────────────────────────┐               │
│  │    Application Thread  │    │      GUI Thread        │               │
│  │                        │    │                        │               │
│  │  - Business logic      │    │  - WMB (JWM + TinyX)   │               │
│  │  - File I/O            │    │  - Event processing    │               │
│  │  - Network             │    │  - Rendering           │               │
│  │  - Serial              │    │                        │               │
│  └───────────┬────────────┘    └────────────┬───────────┘               │
│              │                              │                           │
│              │   Shared Memory / Mutex      │                           │
│              └──────────────┬───────────────┘                           │
│                             │                                           │
│                    ┌────────▼────────┐                                  │
│                    │  Tima OS Kernel │                                  │
│                    │  (Scheduler)    │                                  │
│                    └─────────────────┘                                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**Pros**:
- Simpler memory model (single address space)
- Standard threading primitives (mutex, condition variable)

**Cons**:
- GUI thread sleeping doesn't save power on single core
- Context switching overhead
- Requires thread-safe heap

**Halt Mechanism**: GUI thread calls `pthread_cond_wait()` or equivalent, woken by:
- Input event interrupt handler signals condition
- Application thread signals condition (GUI request)

### 5.2 Current Code Threading Safety

Analyzing the current codebase for thread safety:

| Component | Thread-Safe? | Notes |
|-----------|--------------|-------|
| TinyX DIX | No | Global state, no mutex |
| JWM | No | Global `display`, window lists |
| libX11-wmb | No | Uses global `wmbClient` |
| KDrive tima | Partially | HAL polling is stateless |
| HAL drivers | Depends on OS | Ring buffers can be lock-free |

**Finding**: **Significant work needed** to make WMB thread-safe for true multi-threading.

---

## 6. Multi-Core RISC-V Model

### 6.1 Dual-Core Architecture (Preferred)

Running GUI on a dedicated RISC-V core:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    DUAL-CORE RISC-V ARCHITECTURE                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌────────────────────────────┐    ┌────────────────────────────┐       │
│  │    CORE 0 (Application)    │    │    CORE 1 (GUI - WMB)      │       │
│  │                            │    │                            │       │
│  │  ┌──────────────────────┐  │    │  ┌──────────────────────┐  │       │
│  │  │  Application Code    │  │    │  │  WMB Binary          │  │       │
│  │  │  - Business logic    │  │    │  │  - JWM               │  │       │
│  │  │  - I/O operations    │  │    │  │  - TinyX             │  │       │
│  │  └──────────────────────┘  │    │  │  - libX11-wmb        │  │       │
│  │                            │    │  └──────────────────────┘  │       │
│  │  ┌──────────────────────┐  │    │                            │       │
│  │  │  Tima OS (Full)      │  │    │  ┌──────────────────────┐  │       │
│  │  │  - Scheduler         │  │    │  │  Tima OS (Minimal)   │  │       │
│  │  │  - Filesystem        │  │    │  │  - HAL drivers       │  │       │
│  │  │  - Network           │  │    │  │  - Timer             │  │       │
│  │  │  - HAL               │  │    │  │  - IPC recv          │  │       │
│  │  └──────────────────────┘  │    │  └──────────────────────┘  │       │
│  │                            │    │                            │       │
│  │  Can run independently     │    │  ◄─── CAN BE HALTED ───►   │       │
│  │                            │    │  when no events pending    │       │
│  └────────────┬───────────────┘    └────────────────┬───────────┘       │
│               │                                     │                   │
│               │        SHARED MEMORY REGION         │                   │
│               └──────────────┬──────────────────────┘                   │
│                              │                                          │
│               ┌──────────────▼──────────────┐                           │
│               │   Framebuffer               │                           │
│               │   Input Event Queue         │                           │
│               │   GUI Command Queue         │                           │
│               │   GUI Response Queue        │                           │
│               └─────────────────────────────┘                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**Pros**:
- **True parallel execution** - App continues while GUI is idle
- **Clean halt** - Core 1 can execute WFI without affecting Core 0
- **Memory isolation** - Separate L1 caches, shared L2/memory
- **No thread-safety needed in WMB** - Single-threaded on its core

**Cons**:
- More complex boot sequence
- IPC overhead for GUI requests
- Cache coherency considerations

### 6.2 Core Responsibilities

| Core 0 (Application) | Core 1 (GUI) |
|---------------------|--------------|
| Application business logic | WMB (JWM + TinyX + libX11-wmb) |
| File I/O, Network, Serial | Graphics rendering |
| libX11-app (thin wrapper) | Event processing |
| IPC send to GUI Core | Input polling/handling |
| Interrupt handling (most) | Display refresh |
| Full Tima OS | Minimal OS (HAL + IPC) |

---

## 7. Halt/Sleep Mechanism Design

### 7.1 Wake Sources for GUI Core

The GUI Core should wake from halt (WFI) on:

| Wake Source | Mechanism | Priority |
|-------------|-----------|----------|
| **Keyboard Input** | Hardware interrupt from keyboard controller | High |
| **Mouse Input** | Hardware interrupt from mouse controller | High |
| **Timer Tick** | Timer interrupt for animations, cursor blink | Medium |
| **GUI Request** | IPC interrupt from Application Core | High |
| **Display VSync** | Optional - for tearing-free rendering | Low |

### 7.2 Halt Decision Logic

```c
// In GUI Core main loop (simplified)

void gui_core_main_loop(void)
{
    while (1) {
        // 1. Process any pending input events
        tima_OsPollEvents();  // Non-blocking poll

        // 2. Process any pending GUI requests from App Core
        process_ipc_gui_requests();

        // 3. Process X11 event queue
        if (events_pending()) {
            dispatch_events();
        }

        // 4. Render any damaged regions
        if (screen_needs_update()) {
            render_screen();
        }

        // 5. Check if we can halt
        if (can_halt()) {
            // No pending: input, IPC requests, timers, or render work
            enter_low_power_wait();  // WFI instruction
            // Woken by interrupt: keyboard, mouse, timer, or IPC
        }
    }
}

bool can_halt(void)
{
    return !keypad_driver_has_data() &&
           !mouse_driver_has_data(0) &&
           !ipc_request_pending() &&
           !animation_timer_active() &&
           !screen_damage_pending();
}

void enter_low_power_wait(void)
{
    // Enable interrupts that should wake us
    enable_interrupt(IRQ_KEYBOARD);
    enable_interrupt(IRQ_MOUSE);
    enable_interrupt(IRQ_TIMER);
    enable_interrupt(IRQ_IPC);

    // RISC-V Wait For Interrupt
    __asm__ volatile ("wfi");

    // Execution resumes here after interrupt
}
```

### 7.3 Required HAL Extensions

Current HAL (`hal_drivers.h`) is polling-based. For efficient halt, we need:

```c
// hal_drivers.h - Extensions for halt support

// Blocking wait for any input event (keyboard, mouse, or IPC)
// Returns when any event is available
void hal_wait_for_event(void);

// Blocking wait with timeout (for timers/animations)
// Returns: 1 if event occurred, 0 if timeout
int hal_wait_for_event_timeout(uint32_t timeout_ms);

// Check if any wake source has pending data
bool_t hal_has_pending_event(void);

// For IPC from Application Core
bool_t hal_ipc_has_request(void);
void hal_ipc_read_request(gui_request_t *req);
void hal_ipc_send_response(gui_response_t *resp);
```

### 7.4 KDrive Tima Integration

The `tima_WaitForEvents()` function (proposed in X11_CLIENT_SERVER_ANALYSIS.md) fits perfectly:

```c
// tinyx/kdrive/tima/tima_kdrive_system.c

void tima_WaitForEvents(void)
{
    if (!os_enabled) return;

    // Check if we already have pending events
    if (keypad_driver_has_data() || mouse_driver_has_data(0)) {
        return;  // Don't wait, events already pending
    }

    // No events pending - safe to halt
    hal_wait_for_event();  // Blocks until interrupt
}
```

---

## 8. Inter-Core Communication

### 8.1 Communication Requirements

| Direction | Purpose | Latency Requirement |
|-----------|---------|---------------------|
| App → GUI | X11 API calls (`XCreateWindow`, `XDrawLine`, etc.) | Medium (~1ms) |
| GUI → App | Event delivery (`XNextEvent` return) | Low (~100µs) |
| OS → GUI | Input events (keyboard, mouse) | Low (~100µs) |

### 8.2 IPC Mechanism Options

#### Option A: Shared Memory Queues (Recommended)

```
┌────────────────────────────────────────────────────────────────┐
│                   SHARED MEMORY IPC                            │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  GUI Request Queue (App → GUI)                          │   │
│  │  ┌─────┬─────┬─────┬─────┬─────┬─────┐                  │   │
│  │  │ Req │ Req │ Req │     │     │     │  Lock-free ring  │   │
│  │  └─────┴─────┴─────┴─────┴─────┴─────┘                  │   │
│  │  Write: App Core    Read: GUI Core                      │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  GUI Response Queue (GUI → App)                         │   │
│  │  ┌─────┬─────┬─────┬─────┬─────┬─────┐                  │   │
│  │  │ Resp│ Resp│     │     │     │     │  Lock-free ring  │   │
│  │  └─────┴─────┴─────┴─────┴─────┴─────┘                  │   │
│  │  Write: GUI Core    Read: App Core                      │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Event Queue (GUI → App)                                │   │
│  │  ┌─────┬─────┬─────┬─────┬─────┬─────┐                  │   │
│  │  │KeyEv│MouEv│ Exp │     │     │     │  XEvent structs  │   │
│  │  └─────┴─────┴─────┴─────┴─────┴─────┘                  │   │
│  │  Write: GUI Core    Read: App Core (XNextEvent)         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

**Pros**: Low latency, no kernel involvement, lock-free possible
**Cons**: Requires careful memory barrier usage

#### Option B: Mailbox Registers + Interrupt

```c
// Hardware mailbox (common in multi-core SoCs)

// App Core sends request
void app_send_gui_request(gui_request_t *req)
{
    // Copy request to shared memory
    memcpy(&shared_mem->request, req, sizeof(*req));

    // Signal GUI Core via mailbox interrupt
    MAILBOX->SEND = MAILBOX_MSG_GUI_REQUEST;

    // Wait for response (optional - can be async)
    while (MAILBOX->STATUS & MAILBOX_BUSY);
}

// GUI Core receives (in interrupt handler or polling)
void gui_handle_mailbox_interrupt(void)
{
    uint32_t msg = MAILBOX->RECV;

    if (msg == MAILBOX_MSG_GUI_REQUEST) {
        // Process request from shared memory
        process_gui_request(&shared_mem->request);
    }
}
```

### 8.3 Entry Point Table for Multi-Core

For multi-core, `libX11-app` becomes an IPC client:

```c
// libX11-app/src/CreateWindow.c (Multi-Core Version)

Window XCreateSimpleWindow(Display *dpy, Window parent,
                           int x, int y, unsigned int width, unsigned int height,
                           unsigned int borderWidth,
                           unsigned long border, unsigned long background)
{
    gui_request_t req;
    gui_response_t resp;

    // Build request
    req.type = GUI_REQ_CREATE_SIMPLE_WINDOW;
    req.create_window.parent = parent;
    req.create_window.x = x;
    req.create_window.y = y;
    req.create_window.width = width;
    req.create_window.height = height;
    req.create_window.border_width = borderWidth;
    req.create_window.border = border;
    req.create_window.background = background;

    // Send to GUI Core and wait for response
    hal_ipc_send_request(&req);
    hal_ipc_wait_response(&resp);

    return resp.window_id;
}
```

---

## 9. Implementation Recommendations

### 9.1 Phased Approach

#### Phase 1: Single-Core with Efficient Polling (Current)
- Complete current WMB implementation
- Implement `tima_WaitForEvents()` with polling + sleep
- Validate architecture works end-to-end

#### Phase 2: Single-Core with Interrupt-Driven Wake
- Add `hal_wait_for_event()` to HAL interface
- Implement interrupt-based wake for keyboard/mouse
- Measure power savings from WFI during idle

#### Phase 3: Multi-Core Preparation
- Define IPC protocol and shared memory layout
- Create `libX11-app` IPC version (parallel to direct-call version)
- Test IPC performance

#### Phase 4: Multi-Core Implementation
- Port WMB to run on dedicated GUI Core
- Implement mailbox/interrupt wake mechanism
- Validate Application Core continues during GUI halt

### 9.2 HAL Changes Required

| Current HAL | Required Extension | Purpose |
|-------------|-------------------|---------|
| `keypad_driver_has_data()` | Keep | Polling check |
| `keypad_driver_read_data()` | Keep | Read event |
| (none) | `hal_wait_for_event()` | Blocking wait with WFI |
| (none) | `hal_wait_for_event_timeout()` | Wait with timeout |
| (none) | `hal_enable_input_interrupt()` | Enable keyboard/mouse IRQ |
| (none) | `hal_ipc_*()` | Inter-core communication |

### 9.3 Tima OS Responsibilities

Tima OS must provide:

| Service | Single-Core | Multi-Core |
|---------|-------------|------------|
| Input interrupt handlers | Optional | Required |
| WFI support | Yes | Yes |
| Timer interrupt | Yes | Yes |
| IPC mechanism | No | Yes (mailbox/shared mem) |
| Core 1 boot | No | Yes |
| Shared memory management | No | Yes |

### 9.4 Code Changes Summary

| Component | Changes Needed |
|-----------|---------------|
| **HAL (hal_drivers.h)** | Add `hal_wait_for_event()`, interrupt enable/disable |
| **KDrive Tima** | Implement `tima_WaitForEvents()` using HAL |
| **libX11-wmb** | XNextEvent calls `tima_WaitForEvents()` |
| **libX11-app** | Phase 4: IPC version for multi-core |
| **Tima OS** | Interrupt handlers, IPC, multi-core boot |

---

## 10. Conclusions

### 10.1 Feasibility Assessment

| Architecture | Feasibility | Effort | Power Savings |
|--------------|-------------|--------|---------------|
| Single-core polling | ✅ Current | Done | None |
| Single-core with WFI | ✅ Possible | Low | Moderate |
| Single-core multi-thread | ⚠️ Possible | High | Low |
| **Dual-core dedicated GUI** | ✅ Recommended | Medium | **High** |

### 10.2 Key Findings

1. **Good Separation Exists**: The current architecture has clean boundaries:
   - Application ↔ WMB via entry points
   - WMB ↔ Hardware via HAL
   - All I/O via Tima OS

2. **Polling-Based HAL is Limiting**: Current `keypad_driver_has_data()` polling prevents efficient halt. Need interrupt-driven wake.

3. **Multi-Core is Cleanest**: Running WMB on dedicated core avoids thread-safety issues and allows true halt without affecting application.

4. **Tima OS is Critical**: The OS must provide interrupt handlers, WFI support, and (for multi-core) IPC mechanism.

5. **Incremental Path Exists**: Can start with single-core WFI-based sleep, then migrate to multi-core.

### 10.3 Recommended Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                    RECOMMENDED: DUAL-CORE MODEL                      │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   CORE 0 (Always Running)          CORE 1 (Haltable)                 │
│   ┌─────────────────────┐          ┌─────────────────────┐           │
│   │  Application        │          │  WMB                │           │
│   │  Tima OS (Full)     │   IPC    │  - JWM              │           │
│   │  - Filesystem       │ ◄──────► │  - TinyX            │           │
│   │  - Network          │          │  - libX11-wmb       │           │
│   │  - Serial           │          │                     │           │
│   │  libX11-app (IPC)   │          │  Can execute WFI    │           │
│   └─────────────────────┘          │  when idle          │           │
│                                    └─────────────────────┘           │
│                                                                      │
│   Wake Sources for Core 1:                                           │
│   - Keyboard interrupt                                               │
│   - Mouse interrupt                                                  │
│   - Timer interrupt (animations)                                     │
│   - IPC interrupt (GUI API call)                                     │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### 10.4 Action Items

1. **Immediate**: Add `tima_WaitForEvents()` to KDrive tima (already planned)
2. **Short-term**: Define `hal_wait_for_event()` API with Tima OS team
3. **Medium-term**: Implement interrupt-driven input in Tima OS HAL
4. **Long-term**: Design IPC protocol for multi-core deployment

---

## Appendix A: Relevant Code References

| File | Purpose | Key Functions |
|------|---------|---------------|
| `tinyx/kdrive/tima/tima_kdrive_system.c` | KDrive OS interface | `tima_OsPollEvents()` |
| `framework/sdl_drivers/hal_drivers.h` | HAL interface | `keypad_driver_*`, `mouse_driver_*` |
| `docs/X11_CLIENT_SERVER_ANALYSIS.md` | Event architecture | Section 7.8 |
| `docs/OS_LIBC_REQUIREMENTS.md` | OS dependencies | Timer, libc requirements |

## Appendix B: Related Documents

- [X11_CLIENT_SERVER_ANALYSIS.md](X11_CLIENT_SERVER_ANALYSIS.md) - Event handling architecture
- [KDRIVE_HAL_GUIDE.md](KDRIVE_HAL_GUIDE.md) - KDrive driver implementation
- [BASE_REQUIREMENTS.md](BASE_REQUIREMENTS.md) - System requirements
- [OS_LIBC_REQUIREMENTS.md](OS_LIBC_REQUIREMENTS.md) - OS interface requirements
