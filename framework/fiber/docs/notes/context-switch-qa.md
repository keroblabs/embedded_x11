> ## ℹ NOT DOCUMENTATION — raw research transcript
>
> This is a saved chat about libucontext, kept for background. It is not
> normative, was never reviewed, and nothing should be cited from it.
> It lives under `docs/notes/` for that reason.
>
> Normative: [../fiber-architecture.md](../fiber-architecture.md) ·
> [../hosted-interrupt-simulation.md](../hosted-interrupt-simulation.md)
>
> Relevant decision: [`90-decisions.md`](../../../../docs/90-decisions.md) D‑11
> (vendored libucontext, currently not installed — the hosted port falls back
> to glibc `<ucontext.h>`).

---

User: let's talk about libucontext

libucontext is a lightweight, low-level C library that provides a portable implementation of the System V ucontext functions (getcontext, setcontext, makecontext, and swapcontext). [1, 2, 3, 4]
It is designed to handle user-space context switching by saving and restoring CPU register states, effectively enabling cooperative multitasking, coroutines, and green threads entirely within user space without kernel intervention. [2, 3, 5]
------------------------------
## 🛠️ Why libucontext Exists
Historically, the ucontext.h API was part of the POSIX standard, heavily relied upon for implementing fibers and green threads. However, POSIX deprecated and removed these functions because managing CPU registers and signal masks across varying architectures in a standard way is notoriously difficult. [6, 7, 8, 9]
While glibc (used by most mainstream Linux distributions like Ubuntu and Fedora) kept its own built-in implementation, musl libc (the lightweight C library powering [Alpine Linux](https://pkgs.alpinelinux.org/package/edge/main/x86/libucontext) and many Docker/embedded systems) does not implement swapcontext and its siblings. libucontext was created to plug this exact gap. [10, 11, 12, 13]
------------------------------
## ⚙️ Core Functions Defined
The library implements four architectural building blocks: [3, 8]

* libucontext_getcontext(ucp): Saves the current CPU registers, instruction pointer, and stack pointer into a context structure (ucp).
* libucontext_makecontext(ucp, func, argc, ...): Modifies a context structure to point to a new function and assigns it a dedicated block of memory to use as its stack.
* libucontext_setcontext(ucp): Jumps directly to a saved context, restoring its execution state and discarding the current execution path.
* libucontext_swapcontext(oucp, ucp): Actively pauses the current execution, saves it into oucp, and immediately switches execution to ucp. [2, 3, 8, 14, 15]

------------------------------
## ⚖️ The Critical Trade-offs (Caveats) [8]
Compared to standard glibc implementations, libucontext relies on a highly specialized architecture to maximize speed: [1, 7, 8]

| Feature [2, 3, 7] | Standard glibc / POSIX | libucontext |
|---|---|---|
| Signal Masks | Saved and restored on every swap. | Ignored by default to completely bypass kernel system call overhead. |
| FPU / Vector Registers | Dynamically detects and saves AVX/NEON/FPU states. | Only saves General Purpose Registers (GPR) by default to reduce complexity. |
| Strict POSIX Mode | Default behavior. | Available via -lucontext_posix if you specifically require signal mask preservation. |

------------------------------
## 🌐 Real-World Ecosystem Use Cases
Because it's a foundational component of Alpine Linux, libucontext invisibly powers a massive swath of modern infrastructure: [8, 12]

* [UTM](https://github.com/kaniini/libucontext): The popular macOS and iOS emulation software uses libucontext as the coroutine engine backend for QEMU.
* Lwan: An ultra-high-performance asynchronous web server that utilizes the library to spin up lightweight green threads on non-x86 architectures.
* Programming Languages: Runtime environments like Ruby and Go’s gccgo linker leverage it when compiling programs inside lightweight musl-based Docker containers. [7, 10, 11, 16]

------------------------------
What aspect of libucontext are you hoping to dive deeper into? If you are writing a custom runtime, trying to compile a project on Alpine Linux, or simply looking to understand how context switching functions at the assembly level, let me know and we can zoom in!

[1] [https://github.com](https://github.com/kaniini/libucontext)
[2] [https://man.archlinux.org](https://man.archlinux.org/man/libucontext.3.en)
[3] [https://man.archlinux.org](https://man.archlinux.org/man/libucontext.3.en)
[4] [https://www.cs.utexas.edu](https://www.cs.utexas.edu/~dahlin/Classes/UGOS/labs/labULT/proj-ULT.html)
[5] [https://github.com](https://github.com/NVIDIA/libuthread)
[6] [https://github.com](https://github.com/uxlfoundation/oneTBB/issues/795)
[7] [https://github.com](https://github.com/loongson-community/libucontext)
[8] [https://gitlab.archlinux.org](https://gitlab.archlinux.org/archlinux/packaging/packages/libucontext/-/work_items/2)
[9] [https://github.com](https://github.com/loongson-community/libucontext)
[10] [https://github.com](https://github.com/kaniini/libucontext)
[11] [https://github.com](https://github.com/golang/go/issues/51280)
[12] [https://github.com](https://github.com/crystal-lang/crystal/issues/5863)
[13] [https://www.linuxquestions.org](https://www.linuxquestions.org/questions/linux-software-2/building-libunwind-on-x86-musl-libc-against-libucontext-4175692781/)
[14] [https://man.archlinux.org](https://man.archlinux.org/man/libucontext.3.en)
[15] [https://man.archlinux.org](https://man.archlinux.org/man/libucontext.3.en)
[16] [https://pkgs.alpinelinux.org](https://pkgs.alpinelinux.org/package/edge/main/x86/libucontext)

User: I want to understand the libucontext for creating context, switch between them and terminate them. I want to create a common API that can be used by an application, and then I want to implement a similar context switch for embedded application running RISC-V. I want to start with the hosted (Linux) block first.

To build a clean, architecture-agnostic API for user-space context switching, we must decouple the application logic from the underlying libucontext library structures.
Here is a hosted Linux implementation that wraps libucontext, manages stack allocation, and handles safe execution termination using a central "scheduler" or "main" context.
------------------------------
## 🧱 1. The Common API Definition (fiber.h)
This abstraction shields the application from libucontext specifics. It defines the state of a thread of execution (often called a fiber or coroutine) and the functions to manage it.

#ifndef FIBER_H#define FIBER_H
#include <stddef.h>
// Opaque structure to hide implementation detailstypedef struct fiber fiber_t;
// Function pointer signature for fiber entry pointstypedef void (*fiber_func_t)(void *arg);
// API Functionsfiber_t* fiber_create(fiber_func_t func, void *arg, size_t stack_size);void     fiber_switch(fiber_t *from, fiber_t *to);void     fiber_destroy(fiber_t *f);
#endif // FIBER_H

------------------------------
## ⚙️ 2. The Linux Implementation (fiber_libucontext.c)
This hosted block implements the API using libucontext. It handles a critical edge case: Termination.
If a fiber function returns naturally, it must cleanly transition control back to a safe "main" or "scheduler" context rather than crashing the program. We achieve this by configuring uc_link.

#include "fiber.h"#include <libucontext.h>#include <stdlib.h>
// External reference to the main application/scheduler context.// When a fiber finishes, execution automatically falls back here.extern libucontext_ucontext_t main_ctx;
struct fiber {
    libucontext_ucontext_t ctx;
    void *stack;
};
// Wrapper function to reconcile libucontext's integer-only argument passingstatic void fiber_entry_wrapper(uint32_t arg_high, uint32_t arg_low, uint32_t func_high, uint32_t func_low) {
    // Reconstruct the 64-bit pointers from 32-bit halves
    void *arg = (void *)(((uint64_t)arg_high << 32) | arg_low);
    fiber_func_t func = (fiber_func_t)(((uint64_t)func_high << 32) | func_low);

    // Execute the actual user function
    if (func) {
        func(arg);
    }

    // Once func returns, control automatically jumps to main_ctx (via uc_link)
}
fiber_t* fiber_create(fiber_func_t func, void *arg, size_t stack_size) {
    fiber_t *f = malloc(sizeof(fiber_t));
    if (!f) return NULL;

    f->stack = malloc(stack_size);
    if (!f->stack) {
        free(f);
        return NULL;
    }

    // 1. Initialise the context structure
    libucontext_getcontext(&f->ctx);

    // 2. Set up the dedicated memory stack
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stack_size;
    f->ctx.uc_stack.ss_flags = 0;

    // 3. Termination Link: Define where to jump when this fiber terminates
    f->ctx.uc_link = &main_ctx;

    // Split 64-bit pointers into 32-bit halves for makecontext compliance
    uint64_t a = (uint64_t)arg;
    uint64_t fn = (uint64_t)func;

    // 4. Modify the context to target our entry wrapper
    libucontext_makecontext(&f->ctx, (void (*)(void))fiber_entry_wrapper, 4,
                            (uint32_t)(a >> 32), (uint32_t)a,
                            (uint32_t)(fn >> 32), (uint32_t)fn);

    return f;
}
void fiber_switch(fiber_t *from, fiber_t *to) {
    // Saves 'from' state, restores 'to' state
    libucontext_swapcontext(&from->ctx, &to->ctx);
}
void fiber_destroy(fiber_t *f) {
    if (!f) return;
    free(f->stack);
    free(f);
}

------------------------------
## 💻 3. Hosted Application Example (main.c)
This demonstrates defining a generic application routine, instantiating two parallel fibers, and switching between them sequentially before they self-terminate.

#include <stdio.h>#include <stdlib.h>#include "fiber.h"#include <libucontext.h>
// Global main context storage so fibers know where to return upon completionlibucontext_ucontext_t main_ctx;
// Dummy placeholders to capture the current active fiber statesfiber_t *f1 = NULL;fiber_t *f2 = NULL;
void task_worker(void *arg) {
    char *name = (char *)arg;

    printf("[%s] Starting task execution...\n", name);

    if (name[5] == '1') { // Simple logic to yield from Fiber 1 to Fiber 2
        printf("[%s] Yielding control over to Fiber 2\n", name);
        fiber_switch(f1, f2);
    }

    printf("[%s] Resuming and completing task execution.\n", name);
    // Upon reaching this brace, execution returns back to main_ctx
}
int main(void) {
    printf("[Main] Setting up hosted application fibers...\n");

    // Allocate and configure individual workspaces
    f1 = fiber_create(task_worker, "Fiber 1", 64 * 1024);
    f2 = fiber_create(task_worker, "Fiber 2", 64 * 1024);

    printf("[Main] Passing control to Fiber 1\n");

    // Swap original application context into f1.
    // Once both fibers yield/terminate, libucontext returns control to the line below.
    libucontext_swapcontext(&main_ctx, &f1->ctx);

    printf("[Main] Returned to main safely. Cleaning allocations.\n");

    fiber_destroy(f1);
    fiber_destroy(f2);
    return 0;
}

------------------------------
## 🚀 Key Bridge to the RISC-V Embedded Architecture
By isolating your platform-dependent variables into this common wrapper API, mapping this to RISC-V assembly later is straightforward:

* Creation: In bare-metal RISC-V, fiber_create will manually pre-populate an array inside the fiber struct mimicking saved registers. You will point the stack pointer register (sp) to the top of your allocated block, and the return address register (ra) directly to your fiber_entry_wrapper.
* Switching: Your RISC-V fiber_switch function will be a short, highly performant .s assembly file. It will push the current Callee-Saved registers (s0–s11) onto the current stack, update the structural save pointer, and pull down the target fiber's cached registers.

User: tell me more about the key details about the RISC-V implementation

Moving from a hosted Linux environment to bare-metal RISC-V means stripping away libucontext and writing the register-saving logic directly in assembly.
On RISC-V, context switching is remarkably elegant because of its clean, regular Application Binary Interface (ABI). You only need to save a fraction of the 32 general-purpose registers during a voluntary switch.
------------------------------
## 🗄️ 1. The Core Concept: Callee-Saved Registers
When a fiber calls your C function fiber_switch(from, to), the RISC-V C compiler obeys standard calling conventions. It assumes that the caller has already saved any temporary registers it cares about (t0–t6, a0–a7).
Therefore, your assembly code only needs to save the Callee-Saved registers. These are the registers that a function is required to preserve for its caller:

* ra (x1): The Return Address (where this fiber should resume execution).
* sp (x2): The Stack Pointer (where this fiber's local variables live).
* s0 – s11 (x8, x9, x18–x27): General-purpose saved registers.

This brings our context down to exactly 14 registers (assuming standard 32-bit or 64-bit integer profiles without Floating Point).
------------------------------
## 📐 2. The RISC-V Context Structure
Your structural definition in the embedded firmware changes from holding a libucontext_ucontext_t to a simple array representing the CPU state.

// fiber_riscv.h#include <stdint.h>#include <stddef.h>
// On a 64-bit RISC-V target (RV64), use uint64_t. For RV32, use uint32_t.typedef uint64_t reg_t;
typedef struct fiber {
    reg_t sp;          // The stack pointer must be the first element!
    void *stack_base;  // Kept to allow free/cleanup later
} fiber_t;

Note: Storing sp at byte offset 0 within the structure makes the assembly incredibly clean, as we can dereference the structure pointer directly.
------------------------------
## 🧠 3. The Assembly Switch (fiber_switch.S)
When invoking fiber_switch(from, to), the RISC-V ABI places the pointer to from into register a0, and the pointer to to into register a1.
Here is the entire high-performance context switch in naked RISC-V assembly (written for RV64 using double-word instructions sd/ld. Swap to sw/lw for 32-bit RV32):

.global fiber_switch
.text
.align 2

fiber_switch:
    # 1. Allocate space on the current fiber's stack to save its context
    addi sp, sp, -112      # 14 registers * 8 bytes = 112 bytes

    # 2. Save all callee-saved registers onto the stack
    sd ra,  0(sp)
    sd s0,  8(sp)
    sd s1,  16(sp)
    sd s2,  24(sp)
    sd s3,  32(sp)
    sd s4,  40(sp)
    sd s5,  48(sp)
    sd s6,  56(sp)
    sd s7,  64(sp)
    sd s8,  72(sp)
    sd s9,  80(sp)
    sd s10, 88(sp)
    sd s11, 96(sp)

    # 3. Save the updated Stack Pointer into the 'from' fiber structure
    # 'from' pointer is in a0. Since sp is the first element, we store directly to 0(a0).
    sd sp, 0(a0)

    # ================= THE SWITCH POINT =================

    # 4. Load the target Stack Pointer from the 'to' fiber structure
    # 'to' pointer is in a1.
    ld sp, 0(a1)

    # 5. Restore all callee-saved registers from the new fiber's stack
    ld ra,  0(sp)
    ld s0,  8(sp)
    ld s1,  16(sp)
    ld s2,  24(sp)
    ld s3,  32(sp)
    ld s4,  40(sp)
    ld s5,  48(sp)
    ld s6,  56(sp)
    ld s7,  64(sp)
    ld s8,  72(sp)
    ld s9,  80(sp)
    ld s10, 88(sp)
    ld s11, 96(sp)

    # 6. Deallocate the context frame from the new stack
    addi sp, sp, 112

    # 7. Jump back to the code point where the new fiber previously left off
    ret

------------------------------
## 🛠️ 4. Simulating "Creation" and "Termination"
Because our assembly fiber_switch expects to pop 14 registers off a stack every time it runs, fiber_create must "forge" or prep an empty stack to look exactly like a paused fiber.
When creating a new fiber, your C code must manually construct this 112-byte block at the very top of the newly allocated stack memory:

// A safe terminator stub for execution completionextern fiber_t *main_fiber; void fiber_terminator(void) {
    // If a fiber function returns, immediately abandon it and jump back to safety
    // This replicates the "uc_link" function from libucontext
    fiber_switch(current_fiber, main_fiber);
}
fiber_t* fiber_create(fiber_func_t func, void *arg, size_t stack_size) {
    fiber_t *f = malloc(sizeof(fiber_t));
    f->stack_base = malloc(stack_size);

    // RISC-V stacks grow DOWNWARDS. Find the highest memory address.
    // Ensure 16-byte alignment as strictly required by the RISC-V ABI.
    uintptr_t stack_top = ((uintptr_t)f->stack_base + stack_size) & ~15ULL;

    // Simulate the 112-byte frame that assembly 'fiber_switch' will pop
    stack_top -= 112;
    reg_t *frame = (reg_t *)stack_top;

    // Map structural indices to match assembly offsets
    frame[0]  = (reg_t)func;              // ra (Offset 0): Jump straight to user function
    frame[1]  = 0;                        // s0 (Offset 8)
    // ... frame[2] to frame[12] (s1-s11) can be filled with 0s ...

    // Pass arguments: The RISC-V ABI dictates that functions expect inputs in
    // registers a0 and a1. However, during a switch, our assembly doesn't touch a0/a1.
    // If you want your function to receive arguments, you can modify the assembly
    // switch to restore a0/a1, or pack them carefully into s0/s1 and use a C wrapper stub.

    // Save the forged stack pointer into the fiber control block
    f->sp = (reg_t)stack_top;
    return f;
}

------------------------------
## ⚠️ Critical Hardware Edge Cases to Remember

   1. Floating Point Registers: If your embedded application performs floating-point math (RV32F or RV64D), you must also save the f0–f31 registers and the fcsr register. This expands your stack frame size significantly.
   2. Interrupts: If your context switch is voluntary (cooperative), keeping interrupts enabled is perfectly fine. If you transition this to a preemptive scheduler (driven by a hardware timer interrupt), you must explicitly disable interrupts (csr_clear(mstatus, MSTATUS_MIE)) while shuffling these stack pointers to prevent corrupted memory states.

User: can we have the same concept of context switch for ARM Cortex M0/M0+ (ARMv6T). My mebedded platform can operate with both CPUs

Yes, absolutely! The exact same architectural concept maps perfectly to the ARM Cortex-M0/M0+ (which implements the ARMv6-M architecture) [1].
However, the implementation details change because the Cortex-M0/M0+ is a 32-bit Thumb-only processor with a strictly limited register set [1]. Because it lacks the hardware to manipulate "high" registers (r8–r11) directly with standard stack instructions, we must use a specific sequence to shift them down into "low" registers (r4–r7) before pushing them [2].
Here is how to adapt your common API block for an ARMv6-M platform.
------------------------------
## 🗄️ 1. The ARMv6-M Context Structure
The ARM Cortex-M architecture uses 32-bit registers, meaning our values will use uint32_t [1].
Just like our RISC-V design, your C compiler adheres to the standard AAPCS (ARM Architecture Procedure Call Standard). The callee-saved registers we must manually protect during a voluntary switch are:

* r4 – r11: General-purpose saved registers [2].
* lr (r14): The Link Register (corresponds to RISC-V's ra).

This gives us exactly 9 registers to preserve.

// fiber_armv6m.h#include <stdint.h>
typedef struct fiber {
    uint32_t sp;         // The Stack Pointer MUST be at byte offset 0.
    void *stack_base;
} fiber_t;

------------------------------
## 🧠 2. The Assembly Switch (fiber_switch.S)
On Cortex-M0/M0+, the push and pop instructions can only target the low registers (r0–r7) and lr/pc [2].
When fiber_switch(from, to) is called, the AAPCS places the from pointer in r0 and the to pointer in r1. Here is the context switch written in unified ARM assembly:

.syntax unified
.cpu cortex-m0
.thumb
.global fiber_switch
.text
.align 2

fiber_switch:
    # 1. Save the low callee-saved registers (r4-r7) and return link (lr)
    push {r4-r7, lr}

    # 2. Cortex-M0 restriction: We cannot push r8-r11 directly.
    # We copy high registers into low registers, then push them.
    mov  r4, r8
    mov  r5, r9
    mov  r6, r10
    mov  r7, r11
    push {r4-r7}

    # 3. Save the current stack pointer into the 'from' structure (r0)
    mov  r2, sp
    str  r2, [r0, #0]

    # ================= THE SWITCH POINT =================

    # 4. Load the target stack pointer from the 'to' structure (r1)
    ldr  r2, [r1, #0]
    mov  sp, r2

    # 5. Pop high registers back into temporary low registers, then move them up
    pop  {r4-r7}
    mov  r8, r4
    mov  r9, r5
    mov  r10, r6
    mov  r11, r7

    # 6. Restore original low registers and the execution link
    pop  {r4-r7, r2}

    # 7. Jump back to the code point where this fiber left off
    bx   r2

------------------------------
## 🛠️ 3. Simulating "Creation" on ARMv6-M
Our assembly block expects to pull 9 words (36 bytes) off the stack. Therefore, your embedded C allocator for the ARM architecture must forge this specific layout.
Crucial Architecture Detail: The ARM Cortex-M architecture requires the stack pointer (sp) to be 8-byte aligned at any public interface standard boundaries.

fiber_t* fiber_create(fiber_func_t func, void *arg, size_t stack_size) {
    fiber_t *f = malloc(sizeof(fiber_t));
    f->stack_base = malloc(stack_size);

    // ARM stacks grow DOWNWARDS. Find the top boundary.
    // Force 8-byte alignment (clearing the lowest 3 bits)
    uintptr_t stack_top = ((uintptr_t)f->stack_base + stack_size) & ~7ULL;

    // Allocate 36 bytes for our custom assembly frame
    stack_top -= (9 * sizeof(uint32_t));
    uint32_t *frame = (uint32_t *)stack_top;

    // Map structural layout to match our assembly POP sequence:
    // Frame indices map bottom-to-top relative to how 'pop' reads memory
    frame[0] = 0;             // r8  (via r4 pop)
    frame[1] = 0;             // r9  (via r5 pop)
    frame[2] = 0;             // r10 (via r6 pop)
    frame[3] = 0;             // r11 (via r7 pop)

    frame[4] = 0;             // r4
    frame[5] = 0;             // r5
    frame[6] = 0;             // r6
    frame[7] = 0;             // r7
    frame[8] = (uint32_t)func;// lr (The link register where assembly 'bx r2' jumps)

    // Pass Arguments: The AAPCS states your function expects its first
    // argument in register r0. To keep our code lean, we can handle argument
    // passing inside a small C entry wrapper or pre-fill r0 if using an interrupt path.

    f->sp = (uint32_t)stack_top;
    return f;
}

------------------------------
## 🔀 Comparing Your Two Platforms Side-by-Side
Since your single code-base needs to support both processors, you can cleanly handle this using simple preprocessor conditional switches:

| Feature | RISC-V Target (RV32 / RV64) | ARM Cortex-M0/M0+ (ARMv6-M) |
|---|---|---|
| Stack Alignment | Strict 16-byte alignment | Strict 8-byte alignment |
| Saved Register Count | 14 Registers (ra, sp, s0-s11) | 9 Registers (lr, r4-r11) [2] |
| Hardware Constraint | Can store/load any register directly | High registers must use Low register proxies [2] |
| Floating Point | Optional configuration (F/D extensions) | Completely absent (Software FP only) |

