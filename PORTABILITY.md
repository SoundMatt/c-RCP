# Portability Audit — RTOS / Bare-Metal Targets (Milestone 40)

## Scope adaptation

cpp-RCP's own milestone 40 shipped a **new** pure-C API layer
(`include/rcp/capi.h` + `capi_impl.hpp`) so that Zephyr/FreeRTOS/NuttX
targets — which can't link a C++17 runtime — could still call into the
C++ core. c-RCP's core **is already** that pure-C99 API: every public
symbol in `include/rcp/rcp.h` and friends is `extern "C"`-compatible
C99, callable directly from an RTOS application with no wrapper layer.
Shipping a second C API on top of an existing C API would be pure
duplication, so this milestone is reinterpreted — as flagged in the
original project plan — as a **portability audit**: what in this
codebase already runs unmodified on a bare-metal/RTOS target, and what
would need a target-specific backend.

## What's already portable

- **The wire format and protocol logic are pure C99 with no OS
  dependency at all**: `rcp.c` (core types, `rcp_strerror`), `wire.c`
  (serialization), `e2e.c` (CRC-16/CCITT-FALSE, replay-guard bitmap
  arithmetic), `faultinject.c`'s rule matching, `dyndata.c`'s codec.
  These compile and run identically on Zephyr, FreeRTOS, NuttX, or no OS
  at all — they touch no mutex, no thread, no clock, no heap beyond a
  handful of fixed-size `calloc`s that a static allocator can satisfy.
- **`mock.c`** (the in-process registry/controller used by the whole
  test suite) has no I/O and only uses `platform.h`'s mutex — it is
  already RTOS-portable once `platform.c` has a target backend (below).

## What depends on a POSIX/Win32-specific backend

All of it funnels through one seam: `src/platform.h` / `src/platform.c`.
Every other module — `mock.c`, `udp.c`, `l2.c`, `shmem.c`, `watchdog.c`,
`deadline.c`, `powerstate.c`, `prioqueue.c`, `sim.c`, `zonegroup.c` — only
ever calls `rcp_mutex_*`, `rcp_cond_*`, `rcp_thread_start[_detached]`,
`rcp_sleep_ms`, and `rcp_monotonic_ms` from `platform.h`; none of them
call `pthread_*`, `CreateThread`, or any other OS primitive directly.
(`powerstate.c`'s own milestone-79 REPLACE dropped its background thread
entirely along with the BusOff-recovery loop it existed to drive — see
below — so it now only calls `rcp_mutex_*`, not the thread/sleep/clock
primitives.)
`platform.c` today implements that seam twice: once over POSIX
`pthread_mutex_t`/`pthread_cond_t`/`pthread_t`, once over Win32
`CRITICAL_SECTION`/`CONDITION_VARIABLE`/`HANDLE`. Porting to a given RTOS
means adding a **third** implementation of exactly that same seam:

| `platform.h` primitive | Zephyr backend | FreeRTOS backend |
|---|---|---|
| `rcp_mutex_t` | `struct k_mutex` | `SemaphoreHandle_t` (binary) |
| `rcp_cond_t` | `struct k_condvar` | `SemaphoreHandle_t` + wait list, or `k_poll` |
| `rcp_thread_t` | `k_tid_t` over a static `k_thread` + stack | `TaskHandle_t` |
| `rcp_thread_start[_detached]` | `k_thread_create` on a pre-sized static stack | `xTaskCreate` |
| `rcp_sleep_ms` | `k_msleep` | `vTaskDelay` |
| `rcp_monotonic_ms` | `k_uptime_get()` | `xTaskGetTickCount()` * portTICK_PERIOD_MS |

No call site outside `platform.c` needs to change for any of these
targets — the seam is already narrow enough.

## Gaps a real RTOS port would still need to close

1. **Static allocation.** `calloc`/`malloc` are used at ~117 call sites
   across `src/*.c` (registries, session objects, dynamic-payload
   buffers, admin snapshots). Bare-metal/RTOS deployments in an ASIL-B
   context typically forbid a general-purpose heap; a real port would
   replace these with fixed-capacity pools or a bounded arena allocator,
   the same way `rcp_zone_group_t` (`zonegroup.c`) already avoids
   `std::vector`-style growth in favor of a fixed `RCP_ZONE_GROUP_MAX`
   array. This is a larger, separate effort than the audit itself and is
   not attempted here.
2. **Background-thread-per-decorator pattern.** `watchdog.c`,
   `deadline.c`, `sim.c`, `prioqueue.c`, and `zonegroup.c` each spawn one
   or more long-lived threads via `rcp_thread_start[_detached]`
   (`powerstate.c` no longer does, as of its own milestone-79 REPLACE —
   see above). RTOS targets with a fixed, small task
   count (common on microcontroller-class hardware) would want these
   collapsed into a smaller number of cooperative tasks or a single
   dispatcher loop rather than one OS thread per decorator instance —
   also out of scope for an audit.
3. **`platform.c` itself has no Zephyr/FreeRTOS/NuttX backend yet** —
   only POSIX and Win32. Adding one is the concrete next step whenever
   an actual RTOS integration is undertaken; per the table above, it is
   additive (a third `#if defined(...)` branch) rather than a redesign.

## Conclusion

Unlike cpp-RCP, which needed an entire new API surface to reach
RTOS targets, c-RCP's public API already *is* that surface. The
remaining portability work is confined to one file (`platform.c`) plus
the static-allocation and thread-count concerns above — both
well-scoped, both deferred to a future milestone rather than blocking
this one, consistent with this project's practice of not promising
code that hasn't been written (see the Code Generation and REST/gRPC
scope notes elsewhere in `ROADMAP.md`).
