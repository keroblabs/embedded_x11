# Linux hosted fiber port (libucontext)

Platform layer that satisfies the `framework/fiber` port contract
(`framework/fiber/fiber_port.h`) on a hosted Linux process.

| File | Role |
|---|---|
| `fiber_port.c` | `fiber_port_*` implementation on top of `libucontext` |
| `intr_handler.c/.h` | hosted stand-in for the firmware interrupt controller (`intr_global_set_enabled`, `intr_handler_call_crash`) |
| `fiber_linux.cmake` | source list, include paths, compile definitions, libucontext lookup |

The arena allocator `fiber.c` draws fiber stacks from lives in
`linux_hosted/system/heap.c` so other examples can reuse it; pull it in
with `linux_hosted/system/system.cmake`.

**Cooperative only.** No timer, signal or thread drives the scheduler, so
`fiber_tick_isr()` is never called and preemption never fires. A switch
happens exactly where the application asks for one: `fiber_yield()`,
`fiber_destroy(fiber_self())` or a blocking `fiber_sync` primitive.

Each fiber's machine context is carved out of the top of its own stack
buffer, so `fiber_t::sp` stays a single opaque word and the context is
reclaimed together with the stack.

---

## Installing libucontext

There is no `libucontext` package on Ubuntu/Debian, so build it from
source. (Alpine and Adélie ship `libucontext-dev`; on those distros just
install the package.)

```sh
sudo apt install build-essential git

git clone https://github.com/kaniini/libucontext.git
cd libucontext

make ARCH=x86_64                  # use aarch64 / riscv64 / arm ... as needed
make ARCH=x86_64 check            # optional: run the library's own tests

sudo make ARCH=x86_64 prefix=/usr/local install
sudo ldconfig
```

This installs:

- `/usr/local/include/libucontext/{libucontext.h,bits.h}`
- `/usr/local/lib/libucontext.so.1`, `libucontext.so`, `libucontext.a`
- `/usr/local/lib/pkgconfig/libucontext.pc`

CMake searches `/usr/local` by default, so nothing else is needed.

### Installing without root

```sh
make ARCH=x86_64 prefix=/usr DESTDIR=$HOME/opt/libucontext install
```

then configure the example with:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/opt/libucontext/usr
```

and run it with
`LD_LIBRARY_PATH=$HOME/opt/libucontext/usr/lib ./build/fiber_example`.

### If libucontext is missing

`fiber_linux.cmake` prints a warning and falls back to the glibc
`<ucontext.h>` implementation, which is ABI compatible and works on any
glibc system. Define `FIBER_USE_LIBUCONTEXT=1` (done automatically when
the library is found) to select libucontext instead.

---

## Building the example

```sh
cd linux_hosted/fiber_example
cmake -S . -B build
cmake --build build
./build/fiber_example
```
