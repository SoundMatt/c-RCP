# c-RCP Roadmap

## Vision

c-RCP is a pure-C99 Remote Control Protocol for automotive zonal architecture —
a feature and API mirror of [cpp-RCP](https://github.com/SoundMatt/cpp-RCP),
built for targets where a C++ toolchain isn't available or isn't wanted
(AUTOSAR Classic ECUs, bare-metal/RTOS zone controllers, MISRA-C-gated codebases).

The project focuses on:

- Reliable command delivery from a central computer to distributed zone controllers
- Safety-first design with traceability to ISO 26262 ASIL-B requirements
- A small, dependency-free C99 core — vtable-based interfaces, swappable transports
- Deterministic latency suitable for hard real-time automotive contexts
- Observability by default — metrics, heartbeats, and watchdog support built in

---

## Guiding Principles

1. Pure C99 first — no OS-specific headers in core interfaces
2. Safety as a first-class concern — requirements in `.fusa-reqs.json`, traced to tests
3. Simplicity over completeness — clean interfaces, not a protocol kitchen sink
4. Testability by default — mock backend ships with the library
5. Zonal architecture native — `rcp_zone_t` is a first-class type, not an afterthought
6. Transport-agnostic — swap in-process mock for UDP or TLS without API changes
7. Explicit ownership — every heap allocation has one clearly documented owner;
   no hidden allocation behind "value semantics" the way C++ containers provide

---

## Process

One PR per milestone below. Each PR must be CI-green (cross-platform build+test
matrix, DCO, and the full `cfusa` gate set) before merge, and tagged with the
milestone's version immediately after merge. See `CONTRIBUTING.md`.

Checkboxes below are updated as milestones land; this file is the durable
cross-session tracker for "what's implemented" and "what's next."

---

## Release Plan

| Phase | Version | Theme | Summary |
|---|---|---|---|
| **Foundation** | v0.1.0 | Foundation | Core interfaces, mock backend, CI, c-FuSa, safety doc seeds |
| **Foundation** | v0.2.0 | Requirements | Full atomic SEOOC ASIL-B requirement set, full c-FuSa coverage |
| **Safety groundwork** | v0.3.0 | Hardening | Mock correctness fixes, benchmarks, safety timing evidence |
| **Safety groundwork** | v0.4.0 | HARA expansion | Comprehensive hazard analysis H-001..H-010, SG-001..SG-010 |
| **Transport stack** | v0.5.0 | UDP transport | Pure-C UDP command/response transport with zone discovery |
| **Transport stack** | v0.6.0 | mDNS discovery | Zero-configuration zone controller discovery via mDNS/DNS-SD |
| **Transport stack** | v0.7.0 | TLS transport | Mutual TLS channel for zone-controller communication |
| **Transport stack** | v0.8.0 | Shared memory | Zero-copy intra-host command delivery via shared memory |
| **Transport stack** | v0.9.0 | Loaned samples | Loaning controller extension bringing zero-copy to all transports |
| **Transport stack** | v0.10.0 | TSN transport | IEEE 802.1Qbv-aware UDP transport for hard real-time Ethernet delivery |
| **Safety mechanisms** | v0.11.0 | Watchdog & heartbeat | Watchdog scheduling, zone health state machine, liveness API |
| **Safety mechanisms** | v0.12.0 | Deadline monitoring | Zone-to-HPC liveness: alert when Status stops arriving within deadline |
| **Safety mechanisms** | v0.13.0 | Power state | Sleep/Wake commands, zone power state machine, bus-off recovery |
| **Safety mechanisms** | v0.14.0 | E2E protection | Sequence counter, CRC-16, replay guard on command frames |
| **Safety mechanisms** | v0.15.0 | Priority queuing | Per-zone priority queue honouring Critical/High/Normal |
| **Safety mechanisms** | v0.16.0 | Rate limiting | Per-zone token-bucket admission control against command flooding |
| **Verification** | v0.17.0 | Zone simulator | Timing-realistic zone controller simulator for SiL/HIL testing |
| **Verification** | v0.18.0 | Fault injection | Structured fault injection to validate watchdog, E2E, and replay-guard mechanisms |
| **Security** | v0.19.0 | Authorization | Command-level access control; ISO 21434 SL-2 policy enforcement |
| **Security** | v0.20.0 | Firmware update | Update command and firmware module for zone controller OTA delivery |
| **Topology** | v0.21.0 | Zone groups | Atomic multi-zone command broadcast with typed zone group sets |
| **Topology** | v0.22.0 | Zone proxy | Transparent zone proxy for multi-hop zonal topologies |
| **Topology** | v0.23.0 | Redundancy | Hot-standby registry and HPC failover for ASIL-B fault tolerance |
| **Topology** | v0.24.0 | Multi-HPC federation | Multi-HPC active coordination over shared zone bus |
| **Tooling** | v0.25.0 | Observability | OpenTelemetry traces and Prometheus metrics adapter |
| **Tooling** | v0.26.0 | Admin API | HTTP admin interface for runtime registry inspection and control |
| **Tooling** | v0.27.0 | Record & replay | Record command/response/status streams to disk; replay for regression and forensics |
| **Tooling** | v0.28.0 | Config | YAML/JSON zone registry configuration |
| **Tooling** | v0.29.0 | Code generation | Zone manifest schema; generator interface stub |
| **Tooling** | v0.30.0 | Dynamic data | Runtime schema registry and typed payload codec for schema-less command payloads |
| **Remote access** | v0.31.0 | gRPC bridge | gRPC transport interface stub |
| **Remote access** | v0.32.0 | REST bridge | HTTP/SSE bridge interface stub |
| **Protocol bridges** | v0.33.0 | SOME/IP bridge | Bridge RCP commands to SOME/IP service methods (stub) |
| **Protocol bridges** | v0.34.0 | CAN bridge | Bridge RCP commands to CAN frames (stub) |
| **Protocol bridges** | v0.35.0 | DDS bridge | Bridge RCP Status to DDS topics (stub) |
| **Protocol bridges** | v0.36.0 | MQTT bridge | Bridge RCP Status to MQTT topics (stub) |
| **Protocol bridges** | v0.37.0 | LIN bridge | Bridge RCP commands to LIN frames (stub) |
| **Protocol bridges** | v0.38.0 | UDS bridge | Bridge RCP commands to ISO 14229 UDS service calls (stub) |
| **Protocol bridges** | v0.39.0 | DoIP bridge | Bridge zone controller diagnostics over ISO 13400 (stub) |
| **Platform** | v0.40.0 | RTOS / bare-metal | Zephyr/FreeRTOS integration notes; threading-model portability audit |
| **Certification** | v0.41.0 | Formal verification | TLA+ specs for health SM, watchdog protocol, anti-replay guard |
| **Certification** | v0.42.0 | ISO 21434 | TARA.md, CYBERSECURITY.md, IEC 62443 SL-2 gap analysis |
| **Certification** | v0.43.0 | Certification | ASIL-D gap analysis, structural coverage report, AUDIT_PACK.md |

**Adaptation note (v0.40.0):** cpp-RCP's v0.40.0 milestone added a C API
(`capi.h`) on top of its C++ core, so C/RTOS integrators wouldn't need a C++
toolchain. c-RCP's core *is already* that C API — there's nothing to bridge.
v0.40.0 is reinterpreted as: audit every module for RTOS-friendliness (no
libc assumptions beyond C99, pluggable clock/mutex/thread shims already
introduced at v0.1.0, no unbounded heap allocation on hot paths) and publish
Zephyr/FreeRTOS/NuttX integration notes, mirroring cpp-RCP's Zephyr/QEMU
integration test where practical.

---

## Milestones

---
### Phase 1 — Foundation
---

### 1. Foundation (v0.1.0) ✅

- Core `include/rcp/rcp.h` interfaces: `rcp_controller_t`, `rcp_registry_t`,
  `rcp_command_t`, `rcp_response_t`, `rcp_status_t` (vtable-based, since C has
  no virtual classes)
- `include/relay/relay.h`: shared `rcp_context_t` (deadline) and error-condition
  mapping, the C port of the subset of RELAY's `relay::` namespace cpp-RCP's
  `rcp.hpp` depends on
- `include/rcp/mock.h` + `src/mock.c` in-process backend
- CI: unit tests (cross-platform: gcc/clang/MSVC), c-FuSa, DCO
- Release workflow: safety artifact regeneration on tag
- Safety artifacts: `.fusa.json`, `.fusa-reqs.json`, `.fusa-hara.json`,
  `.fusa-iec62443.json`, `.fusa-problems.json`
- Docs: README, SAFETY_PLAN, SECURITY, INCIDENT-RESPONSE, CONTRIBUTING

### 2. Requirements (v0.2.0) ✅

- Expanded `.fusa-reqs.json` to the full 198-requirement, 24-group SEOOC
  catalog mirroring cpp-RCP's own v0.2.0 milestone (REQ-ZONE, REQ-PRI,
  REQ-CMD, REQ-STATUS, REQ-ERR, REQ-CMDSTRUCT, REQ-RESP, REQ-STAT, REQ-CTRL,
  REQ-REG — already implemented — plus REQ-UDP, REQ-E2E, REQ-WDG, REQ-DL,
  REQ-PWR, REQ-PQ, REQ-RL, REQ-SIM, REQ-FI, REQ-LOAN, REQ-TLS, REQ-SHMEM,
  REQ-TSN, REQ-MDNS — forward-declared specs for modules landing in
  milestones 5–18, same spec-first pattern go-RCP and cpp-RCP both used in
  their own "Requirements" milestones)
- **Process note**: forward-declaring requirements before their implementing
  module exists means requirement traceability (c-FuSa trace metric 1) is
  intentionally below 100% from this milestone until every module in this
  roadmap is implemented — same as cpp-RCP's real history, where 100%
  requirement coverage wasn't enforced as a hard CI gate until v1.0.1, long
  after its own v0.2.0. `ci.yml`'s `cfusa-trace` job reports the true
  percentage every run but only hard-gates on function-annotation density
  (metric 2, 100% from v0.1.0) until the full catalog is implemented, at
  which point metric 1 becomes a hard 100% gate again too.
- Full c-FuSa `trace`/`check` compliance restored at the certification phase
  once every module above ships.

---
### Phase 2 — Safety Groundwork
---

### 3. Hardening (v0.3.0) ✅

- Benchmarks (`tests/bench_mock.c`): send round-trip, send with payload,
  concurrent send (8 threads), publish fan-out, registry lookup — Unity has
  no BENCHMARK macro, so each case times N iterations and prints mean
  latency (`ctest -R rcp_bench -V`)
- Safety timing evidence (`tests/command_latency_test.c`): sustained workload
  with P99 / Max latency gates enforced (relaxed on shared CI runners),
  writes `COMMAND_LATENCY.md` as FuSa audit evidence

### 4. HARA Expansion (v0.4.0) ✅

- `.fusa-hara.json` expanded to H-001..H-010 and SG-001..SG-010, same
  hazard set as cpp-RCP's HARA (delivery loss, misrouting, watchdog failure,
  replay, priority inversion, rate-limiter/watchdog interaction, unauthorized
  injection, power-state failure, fault-injection persistence)
- `HARA.md` documents the hazard table, safety goals, and residual risks
- **Finding**: recomputing ASIL via `cfusa hara asil` (ISO 26262-3:2018
  Table 4) against cpp-RCP's own S/E/C classifications yields a higher ASIL
  than cpp-RCP's HARA.md records for 6 of 10 hazards (ASIL-C/D vs. its
  ASIL-A/B) — see HARA.md's "ASIL Determination Note". This project treats
  c-FuSa's computed value as authoritative and tracks closing the gap
  (decomposition argument or revised classification) at milestones 41/43.

---
### Phase 3 — Transport Stack
---

### 5. UDP Transport (v0.5.0) ✅

- `include/rcp/wire.h` + `src/wire.c`: length-framed binary command/response/
  status codec shared by UDP and (later) TLS — ported 1:1 from cpp-RCP's
  `wire.hpp`, with the same ownership caveat made explicit: unlike
  `rcp_command_t.payload`'s usual borrowed-by-default convention, a
  wire-decoded command/response/status owns its payload.
- `include/rcp/udp.h` + `src/udp.c`: `rcp_udp_zone_server_t` (binds, serves
  Commands, publishes Status to subscribers), a `rcp_controller_t`
  implementation that dials a zone server, and a `rcp_registry_t` backed by
  UDP controllers with a `dial()` convenience — ported from cpp-RCP's
  `udp::ZoneServer`/`udp::Controller`/`udp::Registry`. POSIX only for now
  (Linux/macOS via BSD sockets); Windows gets the same
  `RCP_ERR_CLOSED`-everywhere stub cpp-RCP uses until a native
  implementation lands.
- `tests/test_wire.c` ports cpp-RCP's `test_wire.cpp` (12 requirements,
  frame round-trip + corruption rejection). `tests/test_udp.c` is a new
  loopback integration smoke test (send round-trip, zone-mismatch
  rejection, publish/subscribe) — cpp-RCP has no equivalent `test_udp.cpp`,
  but go-RCP's own UDP milestone calls for "integration tests with loopback
  interface," so this is a deliberate, quality-motivated addition rather
  than a straight port.
- `src/platform.{h,c}` gained a joinable thread primitive
  (`rcp_thread_start`/`rcp_thread_join`) and a timed condvar wait
  (`rcp_cond_timedwait_until`), needed so the UDP controller can honor
  `rcp_context_t` deadlines on `send()` and cleanly join its socket I/O
  threads on `close()` (the existing detached-thread helper from v0.1.0
  isn't joinable by design).

### 6. mDNS Discovery (v0.6.0) ✅

`include/rcp/mdns.h` + `src/mdns.c`: abstract `rcp_mdns_discoverer_t` /
`rcp_mdns_announcer_t` interfaces (ported from cpp-RCP's `mdns.hpp`) plus a
concrete `rcp_mdns_static_discoverer_t` for testing/static config. No
concrete Announcer ships here — same as cpp-RCP, a full mDNS responder
(Avahi/dns_sd) needs platform APIs outside this library's scope; wrap one
with the interface. `tests/test_mdns.c` ports cpp-RCP's `test_mdns.cpp`
(8 requirements), including a local test-double Announcer implementation.

### 7. TLS Transport (v0.7.0) ✅

`include/rcp/tls.h` + `src/tls.c`: mutual TLS channel interface — ported
from cpp-RCP's `tls.hpp`, which is itself a compile-time stub absent an
OpenSSL backend (`RCP_TLS_OPENSSL`, never defined in this codebase either).
Every transport call returns the new `RCP_ERR_NOT_SUPPORTED` sentinel
(added to `rcp_errc_t`) rather than silently falling back to plaintext —
the safety property cpp-RCP's own stub is built to guarantee. `Config`
(cert/key/ca file paths, `verify_peer`) is fully real and carried through
even though no backend consumes it yet. `tests/test_tls.c` ports cpp-RCP's
`test_tls.cpp` (10 requirements): config defaults, secure-by-default
refusal to send/subscribe/dial, and clean close/error propagation.

### 8. Shared Memory Transport (v0.8.0) ✅

`include/rcp/shmem.h` + `src/shmem.c`: zero-copy intra-host command delivery
via direct in-process calls between a refcounted `rcp_shmem_zone_server_t`
and one or more paired `rcp_controller_t` instances — ported from cpp-RCP's
`shmem.hpp`, which is itself an in-process (not true OS `shm_open`/`mmap`)
implementation; that path remains future work in cpp-RCP too. Unlike
`udp.c`'s ZoneServer/Controller (which are 1:1, spawned by dialing), shmem's
`rcp_shmem_zone_server_t` is independently refcounted since it's shared
directly by reference rather than addressed over a socket — the paired
controller (and each subscription's watcher thread) retains its own
reference. `tests/test_shmem.c` ports cpp-RCP's `test_shmem.cpp`
(8 requirements).

### 9. Loaned Samples (v0.9.0) ✅

- `rcp_loan_t` and two new *optional* `rcp_controller_vtable_t` slots
  (`loan`/`send_loaned`, NULL for controllers that don't support loaning —
  C's stand-in for cpp-RCP's `LoaningController` subtype, since C has no
  subtyping) added to `rcp.h`. `rcp_controller_loan()`/
  `rcp_controller_send_loaned()` return `RCP_ERR_NOT_SUPPORTED` if the
  concrete controller doesn't implement them.
- `include/rcp/loan.h` + `src/loan.c`: a generic wrapper
  (`rcp_loan_controller_new()`) extending any inner controller with a real
  pool (free-list, not just accumulation) of reusable buffers — see the
  header comment for the one deliberate deviation from cpp-RCP's own
  `loan.hpp`, whose pool is write-only (`loan()` never actually reads it
  back, so returned buffers just accumulate for the controller's lifetime).
  `tests/test_loan.c` ports cpp-RCP's `test_loan.cpp` (6 requirements).
- **Scope note**: cpp-RCP's own roadmap prose for this milestone describes
  per-transport zero-copy implementations (shmem/mock/UDP-specific
  pool-backed variants) and a benchmark gate enforcing zero allocations on
  the loaned path — none of that exists in cpp-RCP's actual shipped code
  (only the one generic wrapper `loan.hpp` + `test_loan.cpp` cover), so
  this port mirrors what was actually built, not the more ambitious
  narrative (same judgment call already made for UDP's roadmap-mentioned
  "multicast announcement," which cpp-RCP's `udp.hpp` doesn't implement
  either).

### 10. TSN Transport (v0.10.0) ✅

`include/rcp/tsn.h` + `src/tsn.c`: IEEE 802.1Qbv-aware transport adapter,
ported from cpp-RCP's `tsn.hpp`. `rcp_tsn_controller_new()` wraps any
`rcp_controller_t` (the same generic-decorator vtable pattern used by
[[loan.h]]'s LoaningController) and applies `SO_PRIORITY` to a caller-supplied
socket fd before each `send()`, mapping `rcp_priority_t` to an IEEE 802.1p PCP
value (`RCP_PRIORITY_NORMAL`→2, `HIGH`→5, `CRITICAL`→7 by default, via
`rcp_tsn_pcp_map_t`/`rcp_tsn_default_pcp_map()`). The `setsockopt()` call is
gated to `__linux__` (matching cpp-RCP's own `#if defined(__linux__)` guard);
on other platforms, or when `socket_fd < 0`, the PCP mapping is still
computed but the syscall is skipped — `send()` always delegates to the inner
controller regardless. `rcp_tsn_config_t` additionally carries `vlan_id` and
`cycle_ns` fields (802.1Qbv gate cycle) mirroring cpp-RCP's struct, though —
same as cpp-RCP itself — actual VLAN tagging and gate-schedule programming
are out of scope for this milestone (would require a TSN-capable NIC driver
integration); the fields are carried through for forward compatibility only.
`tests/test_tsn.c` ports all 6 of cpp-RCP's `test_tsn.cpp` cases 1:1, using
`rcp_mock_controller_new()`'s handler callback to capture the priority the
inner controller actually receives, proving pass-through.

---
### Phase 4 — Safety Mechanisms
---

### 11. Watchdog & Heartbeat (v0.11.0) ✅

`include/rcp/watchdog.h` + `src/watchdog.c`: ports cpp-RCP's `watchdog.hpp`
Keeper — periodically sends `RCP_CMD_WATCHDOG` to every registered zone
controller and drives each zone through a health state machine (Healthy →
Degraded → Faulted, recovering directly to Healthy on the next successful
kick). `rcp_watchdog_keeper_new()` takes an array of controllers (retains
each); `rcp_watchdog_keeper_health()` reads a zone's current state;
`rcp_watchdog_keeper_subscribe()` registers a callback fired on every
transition (a small growable array, supporting multiple subscribers like
cpp-RCP's own `std::vector<HealthCallback>`, even though the ported test
suite only ever registers one).
- **Deviation note**: cpp-RCP's Keeper dispatches each zone's kick on its own
  detached thread every cycle, and its destructor only joins the *run*
  thread — a kick still in flight when the Keeper is destroyed captures a
  dangling `this` with no synchronization stopping it. This port kicks
  zones sequentially within the single run thread instead (each kick is
  already bounded by its own `rcp_context_t` timeout), which fully removes
  that lifetime hazard; the cost is zones are kicked one at a time per
  cycle rather than in parallel, acceptable given the small fixed zone count
  and short per-kick timeouts this protocol targets. `rcp_watchdog_keeper_close()`
  sets the closed flag and joins the run thread, checking the flag in ~5ms
  polling increments during the inter-cycle sleep so `close()` returns
  promptly rather than blocking for a full `interval_ms`.
- `tests/test_watchdog.c` ports all 8 of cpp-RCP's `test_watchdog.cpp` cases
  (construct/kick-all, starts-Healthy, Degraded-after-`degrade_after`,
  Faulted-after-`fault_after`, recovery-to-Healthy, callback-fires-on-transition,
  kick-timeout, close-stops-thread), using the same poll-with-deadline
  pattern (rather than a single fixed sleep) already established in
  `test_udp.c` and `test_mock.c` to avoid CI flakiness under scheduling
  jitter — confirmed via 15 repeated local runs (debug + ASan/UBSan) with no
  flakes.
- Hit and fixed the known CFUSA-CY004 false-positive pattern once more
  (`calloc(n, sizeof(*k->states))` — `->` appearing after `calloc(` on the
  same line inside `sizeof`), same extract-to-local-variable fix as prior
  milestones; proactively re-grepped the whole codebase afterward and found
  no other occurrences.

### 12. Deadline Monitoring (v0.12.0) ✅

`include/rcp/deadline.h` + `src/deadline.c`: ports cpp-RCP's `deadline.hpp`
Monitor — subscribes to every registered zone controller's Status stream on
construction and resets a per-zone deadline timer on every incoming Status.
If no Status arrives within `deadline_ms`, the zone transitions to dead and
a `rcp_liveness_event_t` is emitted (once — not re-emitted on subsequent
missed cycles while still dead); the next Status received afterward emits
alive immediately. `rcp_deadline_monitor_new()` starts one background watch
thread per controller (unlike v0.11.0's watchdog Keeper, which kicks zones
sequentially on a single thread — each watch thread here blocks on its own
subscription's channel,
so there's no shared-timeout-budget reason to serialize them, and cpp-RCP's
own per-controller-thread design has no destructor lifetime hazard here
since each thread only touches its own captured state, not a `this` that
could be freed mid-kick).
`rcp_deadline_monitor_subscribe()` supports multiple callbacks via the same
growable-array pattern as `watchdog.c`.
- `tests/test_deadline.c` ports all 5 of cpp-RCP's `test_deadline.cpp` cases
  (construct, dead-event-not-repeated, alive-on-first-status,
  alive()-false-before-first-status, close-stops-threads) plus an explicit
  assertion cpp-RCP's own test never made: that the dead event fires
  *exactly once* across multiple missed deadline cycles (REQ-DL-003,
  "repeated dead events suppressed") rather than merely `> 0`. Confirmed via
  20 repeated local runs (debug + 15x ASan/UBSan) with no flakes.

### 13. Power State (v0.13.0) ✅

`include/rcp/powerstate.h` + `src/powerstate.c`: ports cpp-RCP's
`powerstate.hpp` Manager — sends `RCP_CMD_SLEEP`/`RCP_CMD_WAKE` to zone
controllers and tracks the resulting power state
(Active/Sleeping/BusOff). `rcp_powerstate_manager_sleep()`/`_wake()` return
`RCP_ERR_BUSY` if the zone isn't in the expected state to begin with
(mirroring cpp-RCP's own busy-error precondition checks), and
`RCP_ERR_NOT_FOUND` for an unregistered zone. On send failure a zone
transitions to BusOff; a single background recovery thread retries
`RCP_CMD_WAKE` for every BusOff zone at `recovery_interval_ms` until it
succeeds, using the same ~5ms-polling-increment `close()` responsiveness
pattern as v0.11.0's watchdog Keeper.
`rcp_powerstate_manager_subscribe()` supports multiple callbacks via the
same growable-array pattern as `watchdog.c`/`deadline.c`.
- `tests/test_powerstate.c` ports all 6 of cpp-RCP's `test_powerstate.cpp`
  cases (sleep transitions, wake transitions, BusOff on failure, background
  recovery, `state()` thread-safety under concurrent readers, close stops
  the recovery loop), including custom `AlwaysFail`/`FailThenOk` test
  controllers (ported from cpp-RCP's own custom `rcp::Controller`
  subclasses of the same name) implemented as small `rcp_controller_t`
  vtable structs, matching the existing custom-vtable test-double pattern
  already used in `test_mdns.c`. Confirmed via 20 repeated local runs
  (debug + 15x ASan/UBSan) with no flakes.

### 14. E2E Protection (v0.14.0) ✅

`include/rcp/e2e.h` + `src/e2e.c`: ports cpp-RCP's `e2e.hpp` — three layers
of ISO 26262 Part 7 E2E defence: (1) a per-controller monotonically
incrementing `uint32` sequence counter, (2) a CRC-16/CCITT-FALSE checksum
computed over seq + payload, (3) `rcp_e2e_replay_guard_t`, a 32-slot bitmap
sliding-window replay detector. `rcp_e2e_wrap()`/`rcp_e2e_unwrap()` use
`rcp_bytes_t` for owned buffers (matching this project's existing
byte-buffer convention rather than raw pointer+length out-params).
`rcp_e2e_controller_new()` wraps any controller (the same generic-decorator
vtable pattern used since v0.9.0's loaning controller) and applies wrap()
to every command payload on send(), using `rcp_atomic_inc()` for the
sequence counter instead of C11 `<stdatomic.h>` (unavailable under this
project's C99 standard) — the same atomic-builtins wrapper already used for
refcounting throughout the codebase.
- `tests/test_e2e.c` ports all 8 of cpp-RCP's `test_e2e.cpp` cases (wrap/unwrap
  round-trip, header layout, short-frame rejection, CRC-mismatch rejection,
  and four ReplayGuard cases) plus one cpp-RCP's own test suite never
  covered: an explicit test of the `Controller` wrapper itself (verifying
  the wrapped payload unwraps correctly and the sequence counter increments
  per send) — cpp-RCP's `e2e.hpp` ships a `Controller` class with zero test
  coverage in `test_e2e.cpp`.

### 15. Priority Queuing (v0.15.0) ✅

`include/rcp/prioqueue.h` + `src/prioqueue.c`: ports cpp-RCP's
`prioqueue.hpp` — serialises concurrent senders through a single background
dispatch thread and a binary max-heap ordered by (priority desc, seq asc),
so `RCP_PRIORITY_CRITICAL` always pre-empts queued `HIGH`/`NORMAL` commands
and equal-priority commands stay FIFO. `rcp_prioqueue_controller_new()`
follows the same generic-decorator vtable pattern used since v0.9.0.
- Since C has no `std::promise`/`std::future`, each queued entry uses a
  **rendezvous refcount** (starts at 2: one share for queue membership, one
  for the sending thread) — exactly the pattern already established for
  `udp.c`'s pending-request tracking: whichever side finishes last (the
  dispatch thread completing the entry, or the sender giving up on a
  context timeout) brings the refcount to 0 and frees it, guaranteeing
  correct cleanup regardless of race timing.
- **Deviation note**: cpp-RCP's `close()` flips its closed flag and returns
  immediately without waiting for the dispatch thread to drain (only its
  destructor joins). This port's `close()` joins the dispatch thread before
  returning, matching the stronger close()-blocks-until-drained guarantee
  already established by every other background-thread module in this port
  (watchdog, deadline, powerstate).
- `tests/test_prioqueue.c` ports all 7 of cpp-RCP's `test_prioqueue.cpp`
  cases (basic send, zone passthrough, concurrent-priority no-crash,
  already-expired-context timeout, zone-mismatch passthrough, subscribe
  passthrough, close-rejects-further-sends). Confirmed via 20 repeated
  local runs (debug + 20x ASan/UBSan) with no flakes.

### 16. Rate Limiting (v0.16.0) ✅

`include/rcp/ratelimit.h` + `src/ratelimit.c`: ports cpp-RCP's
`ratelimit.hpp` — a token-bucket admission-control decorator (same
generic-wrapper vtable pattern used since v0.9.0). `rcp_ratelimit_config_t`
carries `rate` (tokens/second refill), `burst` (max accumulation), and
`exempt_critical` (bypass the bucket for `RCP_PRIORITY_CRITICAL`, default
true, so watchdog kicks and emergency actuation are never throttled).
Tokens refill lazily on each `send()` based on elapsed wall time (via
`rcp_monotonic_ms()`), matching cpp-RCP's own `steady_clock`-based lazy
refill rather than a background ticking thread — no dispatch thread needed
for this milestone.
- `tests/test_ratelimit.c` ports all 7 of cpp-RCP's `test_ratelimit.cpp`
  cases (basic send, zone passthrough, bucket exhaustion, critical
  exemption on/off, zone-mismatch passthrough, close-rejects-further-sends).

---
### Phase 5 — Verification
---

### 17. Zone Simulator (v0.17.0)

`include/rcp/sim.h`: timing-realistic zone controller simulator for SiL/HIL testing.

### 18. Fault Injection (v0.18.0)

`include/rcp/faultinject.h`: structured fault injection harness validating the
v0.11.0–v0.16.0 safety mechanisms.

---
### Phase 6 — Security
---

### 19. Authorization (v0.19.0)

`include/rcp/authz.h`: command-level access control; ISO 21434 SL-2 policy enforcement.

### 20. Firmware Update / OTA (v0.20.0)

`include/rcp/firmware.h`: chunked firmware delivery with SHA-256 integrity check and rollback.

---
### Phase 7 — Topology & Scalability
---

### 21. Zone Groups (v0.21.0)

`include/rcp/zonegroup.h`: atomic multi-zone command broadcast.

### 22. Zone Proxy (v0.22.0)

`include/rcp/proxy.h`: transparent proxy for cascaded zonal topologies.

### 23. Redundancy (v0.23.0)

`include/rcp/redundancy.h`: hot-standby registry with automatic promotion on failover.

### 24. Multi-HPC Federation (v0.24.0)

`include/rcp/federation.h`: multiple active HPCs coordinating disjoint zone ownership.

---
### Phase 8 — Tooling
---

### 25. Observability (v0.25.0)

`include/rcp/observe.h`: OpenTelemetry traces and Prometheus-compatible metrics.

### 26. Admin API (v0.26.0)

`include/rcp/admin.h`: HTTP admin interface for runtime registry inspection.

### 27. Record & Replay (v0.27.0)

`include/rcp/record.h`: record command/response/status streams to disk; replay for regression.

### 28. Config (v0.28.0)

`include/rcp/config.h`: YAML/JSON zone registry configuration with hot-reload.

### 29. Code Generation (v0.29.0)

Zone manifest schema and generator stub emitting `fusa:req`-annotated C stubs.

### 30. Dynamic Data (v0.30.0)

`include/rcp/dyndata.h`: runtime schema registry and typed payload codec.

---
### Phase 9 — Remote Access
---

### 31. gRPC Bridge (v0.31.0)

`include/rcp/grpcbridge.h`: gRPC transport interface stub for cloud-connected zone controllers.

### 32. REST Bridge (v0.32.0)

`include/rcp/restbridge.h`: HTTP/SSE bridge interface stub.

---
### Phase 10 — Automotive Protocol Bridges
---

### 33. SOME/IP Bridge (v0.33.0)

`include/rcp/someipbr.h` — bridge to SOME/IP service methods (stub, via c-SOMEIP if available).

### 34. CAN Bridge (v0.34.0)

`include/rcp/canbr.h` — bridge to CAN frames (stub).

### 35. DDS Bridge (v0.35.0)

`include/rcp/ddsbr.h` — bridge Status updates to DDS topics (stub).

### 36. MQTT Bridge (v0.36.0)

`include/rcp/mqttbr.h` — bridge Status to MQTT topics (stub).

### 37. LIN Bridge (v0.37.0)

`include/rcp/linbr.h` — bridge to LIN frames (stub).

### 38. UDS Bridge (v0.38.0)

`include/rcp/udsbr.h` — bridge to ISO 14229 UDS service calls (stub).

### 39. DoIP Bridge (v0.39.0)

`include/rcp/doipbr.h` — ISO 13400 Diagnostics over IP transport (stub).

---
### Phase 11 — Platform
---

### 40. RTOS / Bare-Metal (v0.40.0)

See adaptation note above — RTOS portability audit + integration notes rather
than a new C API (the core is already C).

---
### Phase 12 — Certification & Formal Methods
---

### 41. Formal Verification (v0.41.0)

TLA+ specs for the zone health state machine, watchdog protocol, and anti-replay guard.

### 42. ISO 21434 / Cybersecurity (v0.42.0)

TARA, CYBERSECURITY.md, IEC 62443 SL-2 gap analysis closing `.fusa-iec62443.json` items.

### 43. Certification (v0.43.0)

ASIL-D gap analysis, structural coverage report, audit pack.
