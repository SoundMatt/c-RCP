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

### 3. Hardening (v0.3.0)

- Benchmarks (`tests/bench_mock.c`): send round-trip, send with payload,
  concurrent send (8 threads), publish fan-out, registry lookup
- Safety timing evidence (`tests/command_latency_test.c`): sustained workload
  with P99 / Max latency gates enforced, written as FuSa audit evidence

### 4. HARA Expansion (v0.4.0)

- `.fusa-hara.json` expanded to H-001..H-010 and SG-001..SG-010, same
  hazard set as cpp-RCP's HARA (delivery loss, misrouting, watchdog failure,
  replay, priority inversion, rate-limiter/watchdog interaction, unauthorized
  injection, power-state failure, fault-injection persistence)
- `HARA.md` documents ASIL decomposition rationale

---
### Phase 3 — Transport Stack
---

### 5. UDP Transport (v0.5.0)

`include/rcp/udp.h`: length-framed binary command/response protocol over UDP;
static unicast zone discovery with optional multicast announcement.

### 6. mDNS Discovery (v0.6.0)

`include/rcp/mdns.h`: zero-configuration zone controller discovery via mDNS/DNS-SD.

### 7. TLS Transport (v0.7.0)

`include/rcp/tls.h`: mutual TLS channel for zone-controller communication.

### 8. Shared Memory Transport (v0.8.0)

`include/rcp/shmem.h`: zero-copy intra-host command delivery via POSIX shared memory.

### 9. Loaned Samples (v0.9.0)

Loaning controller extension (`rcp_loan_t`, `loan()`/`send_loaned()`) bringing
zero-copy to all transports that support it.

### 10. TSN Transport (v0.10.0)

`include/rcp/tsn.h`: IEEE 802.1Qbv-aware UDP transport for hard real-time delivery.

---
### Phase 4 — Safety Mechanisms
---

### 11. Watchdog & Heartbeat (v0.11.0)

`include/rcp/watchdog.h`: periodic watchdog-kick scheduling; zone health state
machine (Healthy → Degraded → Faulted).

### 12. Deadline Monitoring (v0.12.0)

`include/rcp/deadline.h`: zone-to-HPC liveness — alert when Status stops
arriving within a configured deadline.

### 13. Power State (v0.13.0)

`include/rcp/powerstate.h`: Sleep/Wake command handling; zone power state
machine with bus-off recovery.

### 14. E2E Protection (v0.14.0)

`include/rcp/e2e.h`: sequence counter, CRC-16, replay guard on command frames.

### 15. Priority Queuing (v0.15.0)

`include/rcp/prioqueue.h`: per-zone priority queue honouring Critical/High/Normal.

### 16. Rate Limiting (v0.16.0)

`include/rcp/ratelimit.h`: per-zone token-bucket admission control.

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
