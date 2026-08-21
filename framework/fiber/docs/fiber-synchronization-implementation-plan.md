# Fiber Synchronization — Implementation Plan

> ## ⚠ SUPERSEDED — historical record only
>
> This plan was executed and delivered v1.1's mutex/cond/join. **v2.0 replaced
> the design it describes.** The wait queues, the joiner list and the
> `wait_next` / `joiners_head` / `joiners_tail` FCB fields no longer exist:
> all three primitives are now predicates over `fiber_wait_for()`.
>
> Nothing below describes current code. For the shipping design see
> [fiber-architecture.md](fiber-architecture.md) §10, and for the reasoning
> behind the change see its "What changed in v2.0" header.
>
> Kept because the risk register and the phase ordering are still useful
> context for anyone changing the synchronization layer again.

**Companion to:** [fiber-architecture.md](fiber-architecture.md) · [fiber-implementation-plan.md](fiber-implementation-plan.md)
**Status:** EXECUTED, then superseded by v2.0
**Strategy (as written):** Refactor the internals first (split `struct fiber` into a shared `fiber_internal.h`, add a `BLOCKED` state and joiner list, teach the scheduler to skip BLOCKED), then add `fiber_sync.c` on top. No public lifecycle API breaks; every existing application keeps building.

---

## Phase 0 — Source-tree preparation

| # | Task | Files | Done-when |
|---|---|---|---|
| 0.1 | Move existing fiber docs into `fiber/docs/` and fix back-links in `fiber.h` and the relocatable-code docs | `fiber/docs/*.md`, `fiber/fiber.h`, `docs/relocatable-code-*.md` | All cross-doc links resolve |
| 0.2 | Add `FIBER_MUTEX_MAX` (default 8) and `FIBER_COND_MAX` (default 8) tunables to `fiber/fiber_config.h` | `fiber/fiber_config.h` | Existing builds still compile |

---

## Phase 1 — Internal refactor (no public API change)

Move every field application code does not need into `fiber/fiber_internal.h`, expose only the typedef in `fiber.h`. This lets `fiber_sync.c` touch `state`, `wait_next`, and the joiners list without re-declaring the FCB.

1. **`fiber/fiber_internal.h`** — new header, included only by `fiber.c` and `fiber_sync.c`.
   - `fiber_state_t` enum: `FREE | READY | RUNNING | BLOCKED | ZOMBIE` (BLOCKED is new).
   - Full `struct fiber` with the existing fields **plus** `wait_next`, `joiners_head`, `joiners_tail`.
   - Internal helpers shared between `fiber.c` and `fiber_sync.c`:
     ```c
     /* All called with fiber_port_irq_lock held by the caller. */
     void  fiber_internal_block_self_locked(uint32_t saved_irq);
     void  fiber_internal_wake_locked(struct fiber *f);
     bool_t fiber_internal_in_critical(void);   /* asserts main fiber never blocks */
     ```
   - `extern struct fiber *fiber_internal_current(void);` (already exposed as `fiber_self()`; the locked variant returns the same pointer with the documented invariant that the caller holds the IRQ lock).

2. **`fiber.c` refactor**
   - Delete the local `struct fiber` and `fiber_state_t`; include `fiber_internal.h` instead.
   - Add `wait_next = NULL`, `joiners_head = joiners_tail = NULL` initialisation in `fcb_alloc_locked`.
   - `fiber_sched_swap`: change the "skip ZOMBIE" walk to "skip ZOMBIE **or** BLOCKED"; only ZOMBIE is reclaimed.
   - Termination path: refactor `fiber_trampoline` natural-return and `fiber_destroy` to funnel through `fiber_internal_terminate_locked(self)` that:
     1. Sets `state = ZOMBIE`.
     2. Walks `joiners_head` → calls `fiber_internal_wake_locked` on each, clearing the list.
   - Implement `fiber_internal_block_self_locked(saved_irq)`:
     1. Sets `g_current->state = BLOCKED`.
     2. Releases the IRQ lock with `fiber_port_irq_unlock(saved_irq)`.
     3. Calls `fiber_port_request_switch()` (which yields).
   - Implement `fiber_internal_wake_locked(f)`:
     1. If `f->state != BLOCKED`, no-op (defensive).
     2. Sets `state = READY`; clears `wait_next`.

3. **No public-header change in this phase.** `fiber.h` only learns one new symbol later (none in Phase 1 itself).

**Self-test:** existing terminal app still runs unchanged (no new states reachable).

---

## Phase 2 — `fiber/fiber_sync.h` (public surface)

```c
typedef struct fiber_mutex fiber_mutex_t;
typedef struct fiber_cond  fiber_cond_t;

/* Mutex (FIFO, non-recursive, owner-checked unlock). */
fiber_mutex_t *fiber_mutex_create(void);
int            fiber_mutex_destroy(fiber_mutex_t *m);
int            fiber_mutex_lock(fiber_mutex_t *m);
int            fiber_mutex_trylock(fiber_mutex_t *m);
int            fiber_mutex_unlock(fiber_mutex_t *m);

/* Condition variable (broadcast on signal). */
fiber_cond_t  *fiber_cond_create(void);
int            fiber_cond_destroy(fiber_cond_t *c);
int            fiber_cond_wait(fiber_cond_t *c);
int            fiber_cond_signal(fiber_cond_t *c);

/* Wait-for-termination. */
int            fiber_join(fiber_t *target);
```

Error codes are the same `FIBER_ERR_*` set as `fiber.h`. No new codes are required: invalid use (NULL handle, blocking from main, blocking from critical section, double-unlock by non-owner) returns `FIBER_ERR_INVALID`; pool exhaustion on `*_create` returns `NULL`; `fiber_join(main)` returns `FIBER_ERR_IMMORTAL`.

---

## Phase 3 — `fiber/fiber_sync.c`

1. **Pools** — `static struct fiber_mutex mtx_pool[FIBER_MUTEX_MAX]`, `static struct fiber_cond cond_pool[FIBER_COND_MAX]`. Each carries `in_use`, a wait-queue `head/tail`, and (mutex) an `owner` pointer.
2. **Wait-queue helpers** — `wq_push_tail_locked(head, tail, f)` / `wq_pop_head_locked(head, tail)`. Pure pointer manipulation on `f->wait_next`.
3. **Mutex**
   - `lock`:
     - Reject if `fiber_is_main(self)` or `fiber_internal_in_critical()`.
     - IRQ-lock, then:
       - If `owner == NULL` → `owner = self`, unlock, return.
       - If `owner == self` → unlock, return `FIBER_ERR_INVALID` (no recursion).
       - Else push `self` to wait tail, call `fiber_internal_block_self_locked(prev)` (drops IRQ lock + yields). When resumed, IRQ-relock and confirm `owner == self` (set by the unlocking fiber) before returning.
   - `trylock`: same first check, but pool-state mismatch → `FIBER_ERR_INVALID`; otherwise non-blocking.
   - `unlock`:
     - IRQ-lock; reject if `owner != self`.
     - If wait queue empty → `owner = NULL`, unlock.
     - Else pop head → `owner = head`, mark head `READY` via `fiber_internal_wake_locked`, unlock. The newly-awoken fiber will be picked by the scheduler on its next pass.
4. **Condition variable**
   - `wait`:
     - Reject if main / in critical.
     - IRQ-lock, push `self` to tail, `fiber_internal_block_self_locked(prev)`.
   - `signal` (broadcast):
     - IRQ-lock; walk the entire queue, wake each (`state = READY`, clear `wait_next`); clear `head/tail`; unlock.
5. **Join**
   - Reject if `target == self`, `target == main`, `target == NULL`.
   - IRQ-lock; if `target->state == FREE` or `ZOMBIE` → unlock, return `FIBER_OK`.
   - Else push `self` onto `target->joiners_head/tail` (using `wait_next`), `fiber_internal_block_self_locked(prev)`.
6. **Termination wake-up** — `fiber.c::fiber_internal_terminate_locked` walks the joiners list and wakes every member; cleared on exit. ZOMBIE reclamation already happens later in the scheduler walk and is unchanged.

---

## Phase 4 — Build wiring

| # | Task | Files |
|---|---|---|
| 4.1 | Append `fiber_sync.c` to `SOURCE_FILES_FIBER` | `fiber/fiber.cmake` |
| 4.2 | Confirm `fiber_sync.h` ships with `${FIBER_INCLUDE_PATH}` (already on the include path) | — |
| 4.3 | RV32 build (current default): `cmake --build firmware_bringup/build` | green |
| 4.4 | ARMv6-M build: swap `arch/emulator_arm/arch.cmake` into top-level `CMakeLists.txt`, regenerate, build | green |

---

## Phase 5 — Terminal demo update (acceptance vehicle)

Replace the single `background_fiber` in `source_apps/terminal/source/terminal_main.c` with:

- `timer_fiber` — owns the `timer_data_t`, calls `fiber_cond_signal` on each 1 s expiry, then `fiber_yield`.
- `print_fiber` — `fiber_cond_wait`, then `DEBUG_PRINTK("Counter %d\n", counter++)`, loop.

Both stacks remain statically declared. The condition variable is created from `main` after `fiber_runtime_init`. The existing `terminal_fiber` is unchanged.

**Acceptance:** firmware builds for both RV32 (default) and ARMv6-M; running on the emulator, "Counter N" prints exactly once per second; killing `timer_fiber` from the console halts the prints (proves `print_fiber` is genuinely blocked rather than busy-waiting).

---

## Phase 6 — Polish

| # | Task |
|---|---|
| 6.1 | Spelling pass over the new files and the updated architecture doc |
| 6.2 | `cmake --build … --clean-first` on RV32 and on ARM |
| 6.3 | Update `.claude_tasks/lessons.md` if the BLOCKED-skip invariant or main-never-blocks rule surfaces in any new debugging session |

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Caller blocks inside `fiber_critical_enter` → whole-system deadlock | M | C | Reject at API entry (`FIBER_ERR_INVALID`); document in §12.1 |
| `fiber_destroy` called on a fiber currently parked on a mutex/cond queue → stale `wait_next` | L | H | v1.1 contract: caller must wake the fiber (via the owning sync object) before destroying it. Add a runtime assert later if it becomes a footgun |
| `fiber_join(target)` where `target` is reclaimed (state `FREE`) between the caller's `target` capture and the join call | L | M | Documented contract: caller must keep `target` alive (typically by holding its FCB pointer from `fiber_create`). Returning `FIBER_OK` on already-FREE makes the racy case benign |
| Wait queue corruption if a fiber is on two queues simultaneously | L | H | One `wait_next` field per FCB — structurally impossible to be on two queues at once; APIs assert `f->state == RUNNING` before parking |

---

## Acceptance Criteria

The work is "done" when **all** of:
1. `fiber_sync.h` builds on RV32 and ARMv6-M from the unchanged top-level CMake.
2. The updated terminal app prints once per second on both targets.
3. `fiber_destroy(fiber_main())` still returns `FIBER_ERR_IMMORTAL` (no regression).
4. The original cooperative ping-pong worked example (§10 of the architecture) still runs to completion.
5. Invariants 12.A (main never blocks) and 12.B (no blocking in critical section) are enforced at every blocking entry point.
